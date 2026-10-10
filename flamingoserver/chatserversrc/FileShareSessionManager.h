/**
 *  文件共享的访问会话与传输子会话管理（服务端侧），FileShareSessionManager.h
 *
 *  访问者打开一个共享时，服务端为「这名访问者 × 这个共享」建一个访问会话（只有一条控制通道，
 *  承载列目录、搜索、缩略图与写操作）；访问者申请一次下载或上传时，再在访问会话名下建一个传输
 *  子会话（控制通道 + 若干数据通道）。两类会话都只存在于内存：随任一端的 chatserver 连接断开、
 *  主动结束、共享被取消或成员被移除而结束（方案见 docs/文件共享方案.md 4.2、4.4）。
 *
 *  本类负责：
 *    - 会话表：访问会话 id → 会话信息；每个访问会话名下至多一个传输子会话；
 *    - 名额闸门：整台服务器的访问会话总数、传输子会话总数，以及每个共享同时传输的人数；
 *    - 会话 id 生成：访问会话形如 "share-<序号>-<毫秒>"，传输子会话形如 "<访问会话 id>-t<序号>"，
 *      二者都直接用作中转 sessionid（带 "share-" 前缀，不会与文件夹传输的 id 重名）；
 *    - 中转令牌签发：分享者一端 role_share_owner(9)、访问者一端 role_share_visitor(10)；
 *    - 反查与批量收尾：按用户、按共享、按连接找会话，取消共享 / 移除成员 / 连接断开时一次结束相关会话，
 *      并把被结束的会话交还调用方，由会话层推送通知。
 *
 *  纯逻辑：不触及网络、数据库与日志，时间由调用方传入，便于单元测试（tests/test_filesharesessionmanager.cpp，
 *  Suite SSM）。全部公开方法线程安全，内部一把互斥量；configure 只在服务启动阶段调用。
 *
 *  典型用法（会话层）：
 *      FileShareSessionManager& mgr = Singleton<FileShareSessionManager>::Instance();
 *      std::string accessId;
 *      int code = mgr.openAccess(shareId, ownerId, ownerConn, visitorId, visitorConn, nowMs, accessId);
 *      if (code == error_code_ok)
 *      {
 *          std::string ownerToken, visitorToken;
 *          mgr.signTokens(accessId, ownerId, visitorId, nowMs, ownerToken, visitorToken);
 *          ...（向双方推 cmd_share_relay_info）
 *      }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

//会话所属 chatserver 连接的不透明标识。会话层传一个能唯一标识连接的整数（如会话对象地址），
//本类只用它做相等比较，不解引用
typedef uint64_t ShareConnKey;

//运行参数，main.cpp 读取 etc/chatserver.conf 后经 configure 设置一次（配置项见方案 6.4）
struct FileShareSessionConfig
{
    std::string m_relayTokenKey;        //中转令牌的 HMAC 密钥，与 fileserver 的 relaytokenkey 一致
    std::string m_relayHost;            //fileserver 中转对客户端公布的地址
    int         m_relayPort;            //fileserver 中转端口
    int         m_lanes;                //传输子会话的数据通道条数（sharelanes），1 ~ kShareMaxLanes
    int         m_maxAccess;            //整台服务器同时存在的访问会话数上限（sharemaxaccess）
    int         m_maxTransfers;         //整台服务器同时存在的传输子会话数上限（sharemaxtransfers）
    int         m_maxTransferPerShare;  //每个共享同时传输的人数上限（sharemaxtransferpershare）
    int         m_openTimeoutMs;        //访问者等待分享者 sf_hello 的毫秒数（shareopentimeout）
    int         m_idleTimeoutMs;        //访问会话无请求时回收连接的毫秒数（shareidletimeout）
    int         m_searchMaxResults;     //一次搜索最多返回的条数（sharesearchmaxresults）
    int         m_searchTimeoutMs;      //一次搜索最长耗时毫秒数（sharesearchtimeout）

    FileShareSessionConfig();
};

//一个访问会话（及其名下当前的传输子会话）的快照
struct ShareAccessInfo
{
    std::string  m_accessId;           //访问会话 id，同时是访问会话的中转 sessionid
    int64_t      m_shareId;            //所访问的共享 id
    int64_t      m_ownerId;            //分享者 userid
    int64_t      m_visitorId;          //访问者 userid
    ShareConnKey m_ownerConn;          //分享者提供该共享的那条 chatserver 连接
    ShareConnKey m_visitorConn;        //访问者打开该共享的那条 chatserver 连接
    int64_t      m_createTimeMs;       //建立时间（Unix 毫秒）
    std::string  m_transferId;         //名下当前的传输子会话 id；没有时为空串
    int          m_transferDirection;  //当前传输的方向，取值见 share_transfer_direction；没有传输时为 0

    ShareAccessInfo()
        : m_shareId(0)
        , m_ownerId(0)
        , m_visitorId(0)
        , m_ownerConn(0)
        , m_visitorConn(0)
        , m_createTimeMs(0)
        , m_transferDirection(0)
    {
    }
};

/**
 *  FileShareSessionManager —— 访问会话与传输子会话的内存表与名额闸门（进程内唯一，经 Singleton 访问）。
 */
class FileShareSessionManager
{
public:
    //传输子会话数据通道条数的硬上限：一个传输子会话在服务端占 2 × (lanes + 1) 条连接，Windows 上
    //select 有每线程 64 条的上限（方案 3.2），不能让配置随意放大
    static const int kShareMaxLanes = 4;
    //数据通道条数的默认值
    static const int kDefaultLanes = 2;
#ifdef WIN32
    //整台服务器访问会话数上限的默认值。Windows 上 fileserver 用 select，每个 I/O 线程至多 64 条连接，
    //一个访问会话在服务端占 2 条连接，按连接预算取 30（方案 3.2、6.4）
    static const int kDefaultMaxAccess = 30;
    //整台服务器传输子会话数上限的默认值。一个传输子会话占 2 ×（lanes + 1）条连接，默认 2 条数据通道时
    //为 6 条，Windows 上取 12（方案 6.4）
    static const int kDefaultMaxTransfers = 12;
#else
    //整台服务器访问会话数上限的默认值。Linux 上 fileserver 用 epoll，没有 select 的连接数上限（方案 6.4）
    static const int kDefaultMaxAccess = 1000;
    //整台服务器传输子会话数上限的默认值（Linux，方案 6.4）
    static const int kDefaultMaxTransfers = 256;
#endif
    //每个共享同时传输人数上限的默认值
    static const int kDefaultMaxTransferPerShare = 5;
    //等待分享者响应的默认毫秒数
    static const int kDefaultOpenTimeoutMs = 15000;
    //访问会话空闲回收的默认毫秒数（10 分钟）
    static const int kDefaultIdleTimeoutMs = 600000;
    //一次搜索最多返回条数的默认值
    static const int kDefaultSearchMaxResults = 500;
    //一次搜索最长耗时的默认毫秒数
    static const int kDefaultSearchTimeoutMs = 10000;
    //中转令牌有效期（毫秒）：只在 hello 时校验一次，给足连接与握手的时间即可，与文件夹传输一致
    static const int64_t kRelayTokenTtlMs = 5 * 60 * 1000;

    FileShareSessionManager();
    ~FileShareSessionManager();

    FileShareSessionManager(const FileShareSessionManager&) = delete;
    FileShareSessionManager& operator=(const FileShareSessionManager&) = delete;

    /**
     *  设置运行参数。越界的取值被修正：lanes 大于 kShareMaxLanes 按上限截断、小于 1 取默认值；
     *  各上限与毫秒数小于 1 时取各自的默认值。只在服务启动阶段调用。
     *  @param config 运行参数
     */
    void configure(const FileShareSessionConfig& config);

    /**
     *  取修正后的运行参数。
     *  @return 运行参数（只读引用，configure 之后不再变化）
     */
    const FileShareSessionConfig& getConfig() const;

    /**
     *  打开一个访问会话。调用方已校验过成员身份、功能开关与分享者在线提供状态。
     *  @param shareId     共享 id
     *  @param ownerId     分享者 userid
     *  @param ownerConn   分享者提供该共享的连接
     *  @param visitorId   访问者 userid
     *  @param visitorConn 访问者的连接
     *  @param nowMs       当前时间（Unix 毫秒）
     *  @param accessId    [出参] 成功时为新访问会话 id
     *  @return error_code_ok；访问会话总数已达上限时为 error_code_share_server_busy
     */
    int openAccess(int64_t shareId, int64_t ownerId, ShareConnKey ownerConn,
                   int64_t visitorId, ShareConnKey visitorConn, int64_t nowMs, std::string& accessId);

    /**
     *  取一个访问会话的快照。
     *  @param accessId 访问会话 id
     *  @param out      [出参] 快照
     *  @return 会话存在返回 true
     */
    bool getAccess(const std::string& accessId, ShareAccessInfo& out) const;

    /**
     *  在访问会话名下申请一个传输子会话。
     *  @param accessId   访问会话 id
     *  @param visitorId  申请者 userid，必须是该访问会话的访问者
     *  @param direction  传输方向，取值见 share_transfer_direction
     *  @param transferId [出参] 成功时为新传输子会话 id
     *  @return error_code_ok；方向不是 share_transfer_direction 里的取值为 error_code_bad_request；
     *          会话不存在为 error_code_share_not_exist；申请者不是访问者为
     *          error_code_permission_denied；该访问会话已有传输、或该共享同时传输的人数已满为
     *          error_code_share_transfer_busy；整台服务器的传输子会话数已满为 error_code_share_server_busy。
     *          两道名额闸门同时到顶时先报 error_code_share_transfer_busy（对用户更有指导性）
     */
    int requestTransfer(const std::string& accessId, int64_t visitorId, int direction, std::string& transferId);

    /**
     *  结束一个传输子会话，释放名额。
     *  @param transferId 传输子会话 id
     *  @param access     [出参] 所属访问会话的快照（m_transferId 为被结束的那个），供调用方通知对端
     *  @return 传输子会话存在返回 true
     */
    bool closeTransfer(const std::string& transferId, ShareAccessInfo& access);

    /**
     *  结束一个访问会话，连带结束其名下的传输子会话。
     *  @param accessId 访问会话 id
     *  @param closed   [出参] 被结束的会话快照（m_transferId 为连带结束的传输，可能为空）
     *  @return 会话存在返回 true
     */
    bool closeAccess(const std::string& accessId, ShareAccessInfo& closed);

    /**
     *  结束某个共享的全部访问会话（共享被取消、群解散、功能关闭时调用）。
     *  @param shareId 共享 id
     *  @return 被结束的会话快照
     */
    std::vector<ShareAccessInfo> closeAllForShare(int64_t shareId);

    /**
     *  结束某个成员在某个共享上的全部访问会话（成员被移除、退群时调用）。
     *  @param shareId   共享 id
     *  @param visitorId 成员 userid
     *  @return 被结束的会话快照
     */
    std::vector<ShareAccessInfo> closeForMember(int64_t shareId, int64_t visitorId);

    /**
     *  结束涉及某条连接的全部访问会话（该连接作为分享者一端或访问者一端都算），连接断开时调用。
     *  @param conn 连接标识
     *  @return 被结束的会话快照
     */
    std::vector<ShareAccessInfo> closeForConnection(ShareConnKey conn);

    /**
     *  找出某个用户参与的全部访问会话（作为分享者或访问者）。
     *  @param userId 用户 userid
     *  @return 会话快照
     */
    std::vector<ShareAccessInfo> findByUser(int64_t userId) const;

    /**
     *  找出某个共享的全部访问会话。
     *  @param shareId 共享 id
     *  @return 会话快照
     */
    std::vector<ShareAccessInfo> findByShare(int64_t shareId) const;

    //当前访问会话总数
    size_t getAccessCount() const;

    //当前传输子会话总数
    size_t getTransferCount() const;

    /**
     *  为一个会话（访问会话或传输子会话）签发两枚中转令牌。中转 sessionid 就是会话 id 本身。
     *  @param relaySessionId 访问会话 id 或传输子会话 id
     *  @param ownerId        分享者 userid
     *  @param visitorId      访问者 userid
     *  @param nowMs          当前时间（Unix 毫秒），令牌有效期从此起算 kRelayTokenTtlMs
     *  @param ownerToken     [出参] 分享者一端的令牌（role_share_owner，uid=分享者、peeruid=访问者）
     *  @param visitorToken   [出参] 访问者一端的令牌（role_share_visitor，uid 与 peeruid 对调）
     */
    void signTokens(const std::string& relaySessionId, int64_t ownerId, int64_t visitorId, int64_t nowMs,
                    std::string& ownerToken, std::string& visitorToken) const;

private:
    //统计某个共享上当前有传输的不同访问者人数（调用方已持锁）
    size_t countTransferringVisitorsLocked(int64_t shareId) const;

private:
    FileShareSessionConfig                 m_config;          //运行参数；configure 之后只读
    mutable std::mutex                     m_mutex;           //保护下面四项
    std::map<std::string, ShareAccessInfo> m_accesses;        //访问会话 id → 会话信息
    std::map<std::string, std::string>     m_transferToAccess;//传输子会话 id → 所属访问会话 id
    uint64_t                               m_accessSeq;       //访问会话 id 的序号，进程内单调递增
    uint64_t                               m_transferSeq;     //传输子会话 id 的序号，进程内单调递增
};
