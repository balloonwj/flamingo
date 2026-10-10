/**
 *  访问者一侧一个共享的访问状态机，ShareVisitor.h
 *
 *  浏览窗口每打开一个共享就有一个 CShareVisitor，负责 docs/文件共享方案.md 8.2 的整条流程：
 *    1. 第一次请求（列目录）时发 2220 打开共享，应答带回 accessid；
 *    2. 收到推给访问者的 2222 后请传输层连接中转（角色 10，lane 0）；
 *    3. 连上后在 opentimeoutms 内等分享者的 sf_hello（分享者会重发，只认第一个），收不到就判「分享者暂时无法响应」、
 *       发 2221 结束访问会话；
 *    4. 就绪后收发控制通道请求：列目录按游标分批取（sf_list_result 的 more 为真时自动带游标再取），取完一次性交给界面；
 *       同一时刻只有一个列目录请求有效，新的请求作废旧的，迟到的旧应答忽略；单文件共享没有目录可列，改发 sf_stat
 *       查共享根本身，结果按只有这一项的列表交出（第 2 期第 6 步补：此前界面自己造这一项，没有大小与修改时间）；
 *    5. 连续 idletimeoutms 没有请求时发 sf_bye 与 2221，回到空闲状态；下一次请求时自动重新打开（第 1~3 步）；
 *    6. 中转断开、分享者发来 sf_bye、服务端推来 2223 时结束会话并告诉界面原因；之后的请求同样会重新打开；
 *    7. 写操作（新建文件夹、重命名、删除、移动，第 2 期第 2 步）：可同时有多个，各有请求号；就绪之前排队、就绪后发出，
 *       结果按请求号经 OnVisitorOpResult 回报；超时、会话结束时按失败回报；有未完成的写操作时不做空闲回收；
 *    8. 搜索（第 2 期第 3 步）：同一时刻只有一个，新的搜索先发 sf_cancel 取消旧的；就绪之前排队；命中按批经
 *       OnVisitorSearchResult 回报，done 为真时结束；距上一批超过 kShareVisitorRequestTimeoutMs 没有动静按 816 结束；
 *       会话失败时按失败结束；CancelSearch 发 sf_cancel 并不再回报；有未结束的搜索时不做空闲回收；
 *   10. 整棵目录树（第 2 期第 5 步，拖出目录用）：同一时刻一个，新的取代旧的；结果收齐后经 OnVisitorTreeResult 一次交出；
 *       超时、会话失败按失败交出；
 *    9. 缩略图（第 2 期第 4 步）：可同时有多个，各有请求号；就绪之前排队；结果（尺寸与 JPEG）经 OnVisitorThumbResult
 *       回报；超时回 816、会话失败按失败回报；CancelThumb 发 sf_cancel 并不再回报；有未完成的缩略图请求时不做空闲回收。
 *
 *  本类不碰网络与窗口：发信令、连中转、发应用帧、关中转都经 IShareVisitorTransport；状态与结果经 IShareVisitorObserver
 *  告诉界面；时间由调用方传入（单元测试用假时间）。全部方法只在 UI 线程上调用。
 *  单元测试见 FlamingoFileShare/Tests/ShareVisitorTests.cpp（标签 [sharevisitor]）。
 *
 *  典型用法（ShareService 内部）：
 *      CShareVisitor visitor(shareId, &transport, &window);
 *      visitor.List("", now);                         //第一次请求，触发打开
 *      visitor.OnOpenResponse(seq, code, accessId, now);
 *      visitor.OnRelayInfo(relay, now);
 *      visitor.OnRelayConnected(true, now);
 *      visitor.OnFrame(msg, now);                     //中转收到的每个应用帧
 *      visitor.OnTick(now);                           //每秒一次
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "ShareControlFraming.h"
#include "ShareProtocolCodec.h"
#include "ShareSearch.h"   //kShareTreeMaxItems

namespace fileshare {

//访问者一侧的访问状态
enum share_visitor_state
{
    share_visitor_idle           = 0,   //尚未打开，或空闲回收后已关闭；下一次请求时自动打开
    share_visitor_opening        = 1,   //已发 2220，等应答
    share_visitor_awaiting_relay = 2,   //2220 成功，等服务端推来 2222
    share_visitor_connecting     = 3,   //正在连接中转
    share_visitor_waiting_hello  = 4,   //中转已连上，等分享者的 sf_hello
    share_visitor_ready          = 5,   //收到 sf_hello，可以收发请求
    share_visitor_failed         = 6,   //打开失败或会话异常结束，原因见 GetErrorCode；下一次请求时重新打开
};

//2220 应答与 2222 迟迟不到时判失败的时间，单位：毫秒
const int64_t kShareVisitorSignalTimeoutMs = 15000;

//一个列目录请求（含按游标续取的各批）迟迟没有应答时判失败的时间，单位：毫秒
const int64_t kShareVisitorRequestTimeoutMs = 30000;

//2222 里没有带 opentimeoutms / idletimeoutms 时使用的默认值，单位：毫秒（与服务端配置项的默认值一致）
const int64_t kShareVisitorDefaultOpenTimeoutMs = 15000;
const int64_t kShareVisitorDefaultIdleTimeoutMs = 600000;

/**
 *  访问者状态机的传输层，由 ShareService 实现（单元测试用假实现）。
 */
class IShareVisitorTransport
{
public:
    virtual ~IShareVisitorTransport()
    {
    }

    /**
     *  经主连接发一条信令。
     *  @return 帧 seq；发送失败返回 0
     */
    virtual uint32_t SendCommand(uint16_t cmd, const std::string& body) = 0;

    /**
     *  开始连接中转（异步），结果经 CShareVisitor::OnRelayConnected 告知。
     *  @param relay 2222 推给访问者的中转参数
     *  @return 已开始连接返回 true；无法开始（参数非法等）返回 false
     */
    virtual bool StartRelay(const ShareRelayParams& relay) = 0;

    /**
     *  经中转 lane 0 发一个应用帧。
     *  @return 已发出返回 true
     */
    virtual bool SendFrame(const std::string& frame) = 0;

    /**
     *  关闭中转连接（已关闭时调用无效果）。
     */
    virtual void CloseRelay() = 0;
};

/**
 *  访问者状态机的观察者，由浏览窗口实现。回调都在 UI 线程上。
 */
class IShareVisitorObserver
{
public:
    virtual ~IShareVisitorObserver()
    {
    }

    /**
     *  访问状态变化。
     *  @param state   新状态，share_visitor_state
     *  @param errCode 进入 failed 时为原因，其余为 0
     */
    virtual void OnVisitorStateChanged(int state, int errCode) = 0;

    /**
     *  一个目录列完（或失败）。
     *  @param path    目录的相对路径
     *  @param code    0 成功，否则为错误码
     *  @param entries 全部条目（失败时为空），顺序为分享者给出的顺序
     */
    virtual void OnVisitorListed(const std::string& path, int code, const std::vector<ShareEntry>& entries) = 0;

    /**
     *  写权限位变化（sf_perms 或 2206）。
     *  @param perms 新的写权限位
     */
    virtual void OnVisitorPermsChanged(int perms) = 0;

    /**
     *  共享对本人的状态变化（2203）。界面据此显示或隐藏横幅。
     *  @param state  share_state
     *  @param reason 暂不可用或已失效的原因
     */
    virtual void OnVisitorShareState(int state, int reason) = 0;

    /**
     *  一个写操作（新建文件夹、重命名、删除、移动）有了结果。默认什么也不做。
     *  @param reqId  发请求时返回的请求号
     *  @param opType 帧类型：sf_mkdir、sf_rename、sf_delete、sf_move 之一
     *  @param code   总结果：0 成功，否则为错误码（多条目操作为第一条失败的错误码；会话失败时为会话的失败原因）
     *  @param items  逐条结果（分享者给出的）；会话失败、超时时为空
     */
    virtual void OnVisitorOpResult(int64_t reqId, int opType, int code, const std::vector<ShareOpItem>& items)
    {
        (void)reqId;
        (void)opType;
        (void)code;
        (void)items;
    }

    /**
     *  搜索来了一批结果（或结束）。默认什么也不做。
     *  @param reqId     Search 返回的请求号
     *  @param hits      这一批命中：m_name 为相对共享根的路径
     *  @param done      搜索是否已结束（之后不再回报这个请求号）
     *  @param truncated 结束时：是否因分享者一端的条数或耗时上限提前结束
     *  @param code      结束时的结果码：0 正常；分享者拒绝（路径不存在、关键字不合法等）、超时、会话失败时为错误码
     */
    /**
     *  一个缩略图请求有了结果。默认什么也不做。
     *  @param reqId  RequestThumb 返回的请求号
     *  @param path   请求的图片路径
     *  @param code   0 成功，否则为错误码
     *  @param width  缩略图宽（像素）；失败时为 0
     *  @param height 缩略图高（像素）；失败时为 0
     *  @param jpeg   JPEG 数据；失败时为空
     */
    virtual void OnVisitorThumbResult(int64_t reqId, const std::string& path, int code, int width, int height,
                                      const std::string& jpeg)
    {
        (void)reqId;
        (void)path;
        (void)code;
        (void)width;
        (void)height;
        (void)jpeg;
    }

    /**
     *  整棵目录树收齐了（或失败）。默认什么也不做。
     *  @param reqId     ListTree 返回的请求号
     *  @param path      请求的目录
     *  @param entries   目录之下的全部条目（m_name 为相对共享根的路径，父目录在子条目之前）；失败时为空
     *  @param truncated 是否超过了请求的条数上限（超过时 entries 只是一部分）
     *  @param code      0 成功，否则为错误码
     */
    virtual void OnVisitorTreeResult(int64_t reqId, const std::string& path, const std::vector<ShareEntry>& entries,
                                     bool truncated, int code)
    {
        (void)reqId;
        (void)path;
        (void)entries;
        (void)truncated;
        (void)code;
    }

    virtual void OnVisitorSearchResult(int64_t reqId, const std::vector<ShareEntry>& hits, bool done, bool truncated, int code)
    {
        (void)reqId;
        (void)hits;
        (void)done;
        (void)truncated;
        (void)code;
    }
};

/**
 *  CShareVisitor —— 访问者一侧一个共享的访问状态机。不可拷贝。
 */
class CShareVisitor
{
public:
    /**
     *  @param shareId   共享 id
     *  @param transport 传输层（不拥有，须在本对象销毁之前一直有效），不可为 NULL
     *  @param observer  观察者（不拥有，可为 NULL）
     */
    CShareVisitor(int64_t shareId, IShareVisitorTransport* transport, IShareVisitorObserver* observer);

    CShareVisitor(const CShareVisitor&) = delete;
    CShareVisitor& operator=(const CShareVisitor&) = delete;

    /**
     *  列一个目录。未打开、空闲回收或失败之后调用时先自动打开；正在打开时排队，就绪后发出；
     *  就绪时直接发出。新的请求作废尚未完成的旧请求。
     *  @param path  目录的相对路径，共享根为空串
     *  @param nowMs 当前时间（毫秒，单调递增即可）
     */
    void List(const std::string& path, int64_t nowMs);

    /**
     *  新建文件夹。未就绪时先打开（同 List），就绪后发出 sf_mkdir。
     *  @param path  新文件夹的相对路径（含它自己的名字）
     *  @param nowMs 当前时间（毫秒）
     *  @return 请求号（大于 0），结果经 OnVisitorOpResult 回报
     */
    int64_t MakeDirectory(const std::string& path, int64_t nowMs);

    /**
     *  重命名一个条目。
     *  @param path    条目的相对路径
     *  @param newName 新名字（一段）
     *  @param nowMs   当前时间（毫秒）
     *  @return 请求号
     */
    int64_t Rename(const std::string& path, const std::string& newName, int64_t nowMs);

    /**
     *  删除若干条目（放进分享者电脑的回收站）。
     *  @param paths 条目的相对路径
     *  @param nowMs 当前时间（毫秒）
     *  @return 请求号
     */
    int64_t Delete(const std::vector<std::string>& paths, int64_t nowMs);

    /**
     *  把若干条目移到共享内的另一个目录。
     *  @param paths 条目的相对路径
     *  @param toDir 目标目录的相对路径（空串为共享根）
     *  @param nowMs 当前时间（毫秒）
     *  @return 请求号
     */
    int64_t Move(const std::vector<std::string>& paths, const std::string& toDir, int64_t nowMs);

    //尚未有结果的写操作个数
    size_t GetPendingOpCount() const;

    /**
     *  在某个目录之下按文件名搜索。已有未结束的搜索时先取消它（发 sf_cancel，不再回报）。未就绪时先打开。
     *  @param path    起点目录的相对路径（共享根为空串）
     *  @param keyword 关键字（UTF-8）
     *  @param nowMs   当前时间（毫秒）
     *  @return 请求号（大于 0），结果经 OnVisitorSearchResult 回报
     */
    int64_t Search(const std::string& path, const std::string& keyword, int64_t nowMs);

    /**
     *  取消正在进行的搜索：已发出的发 sf_cancel，之后不再回报。没有搜索时什么也不做。
     */
    void CancelSearch();

    //正在进行（含排队等就绪）的搜索的请求号；没有时为 0
    int64_t GetSearchReqId() const;

    /**
     *  取一张图片的缩略图或预览图。未就绪时先打开。
     *  @param path    图片的相对路径
     *  @param maxEdge 长边像素数
     *  @param nowMs   当前时间（毫秒）
     *  @return 请求号（大于 0），结果经 OnVisitorThumbResult 回报
     */
    int64_t RequestThumb(const std::string& path, int maxEdge, int64_t nowMs);

    /**
     *  取消一个缩略图请求：已发出的发 sf_cancel，之后不再回报。没有这个请求号时什么也不做。
     *  @param reqId RequestThumb 返回的请求号
     */
    void CancelThumb(int64_t reqId);

    //尚未有结果的缩略图请求个数
    size_t GetPendingThumbCount() const;

    /**
     *  取一个目录之下的整棵目录树（拖出目录用）。已有未完成的同类请求时先取消它。未就绪时先打开。
     *  @param path     目录的相对路径
     *  @param maxItems 条数上限（分享者多列一条用来判断是否超过）；不大于 0 时按 kShareTreeMaxItems + 1
     *  @param nowMs    当前时间（毫秒）
     *  @return 请求号（大于 0），结果经 OnVisitorTreeResult 交出
     */
    int64_t ListTree(const std::string& path, int maxItems, int64_t nowMs);

    //正在进行的目录树请求的请求号；没有时为 0
    int64_t GetTreeReqId() const;

    /**
     *  界面关闭：会话已建立时发 sf_bye 与 2221，关闭中转，回到空闲状态；此后的事件一律忽略，直到下一次请求。
     */
    void Close();

    /**
     *  确保访问会话在打开（第 4 期起，没有界面请求时 CShareService 为排队的传输调用）：空闲或失败时发 2220 重新打开，
     *  正在打开或已就绪时什么也不做。主连接断开时同步进入失败（经观察者报告）。
     *  @param nowMs 当前时间（毫秒）
     */
    void EnsureOpen(int64_t nowMs);

    /**
     *  2220 应答。
     *  @return seq 与本对象发出的 2220 相同（已处理）返回 true
     */
    bool OnOpenResponse(uint32_t seq, int code, const std::string& accessId, int64_t nowMs);

    /**
     *  2222（推给访问者、访问会话本身的那一帧）。
     *  @return accessid 与本对象的相同（已处理）返回 true
     */
    bool OnRelayInfo(const ShareRelayParams& relay, int64_t nowMs);

    /**
     *  中转连接结果（传输层在 StartRelay 之后回调）。
     */
    void OnRelayConnected(bool ok, int64_t nowMs);

    /**
     *  中转收到的一个应用帧（已解码）。
     */
    void OnFrame(const ShareControlMessage& msg, int64_t nowMs);

    /**
     *  中转连接异常断开。
     */
    void OnRelayClosed(int64_t nowMs);

    /**
     *  2223：访问会话已结束。
     *  @return accessid 与本对象的相同（已处理）返回 true
     */
    bool OnAccessEnded(const std::string& accessId, int errCode);

    /**
     *  2206：本人在这个共享上的权限变化。
     */
    void OnPermsNotify(int perms);

    /**
     *  2203：共享对本人的状态变化。转告界面；已失效时结束会话（服务端已结束访问会话，不再发 2221）。
     *  @param state  share_state
     *  @param reason 原因
     */
    void OnShareStateNotify(int state, int reason);

    /**
     *  定时检查超时（建议每秒一次）：等应答、等 sf_hello、等列目录结果超时，以及空闲回收。
     */
    void OnTick(int64_t nowMs);

    /**
     *  记下这个访问会话上正在进行的传输子会话个数：服务端在访问会话结束时连带结束其传输子会话，所以有传输时不做
     *  空闲回收。
     *  @param count 传输子会话个数
     */
    void SetTransferCount(int count);

    /**
     *  更换观察者（浏览窗口重新打开时换成新窗口，关闭时置为 NULL）。
     *  @param observer 新的观察者（不拥有，可为 NULL）
     */
    void SetObserver(IShareVisitorObserver* observer);

    //共享 id
    int64_t GetShareId() const;

    //当前状态，share_visitor_state
    int GetState() const;

    //进入 failed 的原因；其余状态为 0
    int GetErrorCode() const;

    //当前访问会话 id；未打开时为空串
    const std::string& GetAccessId() const;

    //sf_hello 给出的共享类型、根名、是否含隐藏文件；就绪之前为默认值。2026-10-04 起分享者只在单文件共享时给根名
    //（就是那个文件的名字，下载时用），磁盘与文件夹共享不给，免得把真实路径里的文件夹名或盘符发给访问者
    int GetKind() const;
    const std::string& GetRootName() const;

    /**
     *  共享名称（2026-10-04 起）：2220 应答成功时服务端给出，由 CShareService 填入；浏览窗口用它作标题与路径栏的第一节。
     *  @param name 共享名称（UTF-8）；服务端没给时为空
     */
    void SetShareName(const std::string& name);

    //共享名称（UTF-8），见 SetShareName；还没打开过或服务端没给时为空
    const std::string& GetShareName() const;

    //当前的写权限位
    int GetPerms() const;

private:
    //发 2220 开始打开
    void Open(int64_t nowMs);

    //就绪后发出排队的列目录请求
    void SendQueuedList(int64_t nowMs);

    //发一个 sf_list（首批或续取）
    bool SendListFrame(const std::string& cursor);

    /**
     *  结束会话并进入 failed：关闭中转，必要时发 2221，把尚未完成的列目录请求按失败交给界面。
     *  @param errCode        原因
     *  @param sendCloseAccess 是否发 2221（服务端已知会话结束时不必发）
     */
    void Fail(int errCode, bool sendCloseAccess);

    //切换状态并通知观察者
    void SetState(int state, int errCode);

    //把尚未完成的列目录请求按失败交给界面并清掉
    void FailPendingList(int errCode);

    //登记一个写操作并按需发出或触发打开；返回请求号
    int64_t RequestOp(ShareControlMessage& req, int64_t nowMs);

    //就绪后发出排队的写操作
    void SendQueuedOps(int64_t nowMs);

    //就绪后发出排队的搜索
    void SendQueuedSearch(int64_t nowMs);

    //发一个 sf_cancel
    void SendCancel(int64_t reqId);

    //结束当前搜索并回报一次 done（code 为结束原因）；没有搜索时什么也不做
    void FinishSearch(int code);

    //就绪后发出排队的缩略图请求
    void SendQueuedThumbs(int64_t nowMs);

    //把尚未有结果的缩略图请求全部按失败回报并清掉
    void FailPendingThumbs(int errCode);

    //就绪后发出排队的目录树请求
    void SendQueuedTree(int64_t nowMs);

    //结束目录树请求并交出结果（code 非 0 时条目为空）
    void FinishTree(int code, bool truncated);

    //把尚未有结果的写操作全部按失败回报并清掉
    void FailPendingOps(int errCode);

    //一个写操作：请求内容、是否已发出、等结果的截止时间
    struct PendingOp
    {
        ShareControlMessage m_req;       //请求内容（含请求号）
        bool                m_sent;      //是否已发出
        int64_t             m_deadline;  //等结果的截止时间；未发出时为 0

        PendingOp()
            : m_sent(false)
            , m_deadline(0)
        {
        }
    };

private:
    int64_t                  m_shareId;          //共享 id；构造后不变
    IShareVisitorTransport*  m_transport;        //传输层（不拥有）
    IShareVisitorObserver*   m_observer;         //观察者（不拥有，可为 NULL）
    int                      m_state;            //当前状态，share_visitor_state
    int                      m_errCode;          //进入 failed 的原因
    uint32_t                 m_openSeq;          //尚未收到应答的 2220 的 seq；没有时为 0
    uint32_t                 m_abandonedSeq;     //已放弃等待（关闭或超时）但服务端可能仍会应答的 2220 的 seq；应答成功时立即发 2221，
                                                 //免得服务端留下没人使用的访问会话；没有时为 0
    std::string              m_accessId;         //访问会话 id；未打开时为空串
    int64_t                  m_deadline;         //当前阶段（等 2220 应答、等 2222、等连接、等 sf_hello）的截止时间
    int64_t                  m_openTimeoutMs;    //等 sf_hello 的时长，来自 2222
    int64_t                  m_idleTimeoutMs;    //空闲回收的时长，来自 2222
    int64_t                  m_lastActivityMs;   //最近一次发出请求或收到应答的时间，空闲回收据此计算
    int                      m_kind;             //sf_hello 给出的共享类型
    std::string              m_rootName;         //sf_hello 给出的根名
    std::string              m_shareName;        //共享名称（UTF-8），2220 应答成功时由 CShareService 填入，见 SetShareName
    bool                     m_includeHidden;    //sf_hello 给出的是否含隐藏文件
    int                      m_perms;            //当前的写权限位
    int64_t                  m_nextReqId;        //下一个请求号，在本对象内递增
    int                      m_transferCount;    //正在进行的传输子会话个数；大于 0 时不做空闲回收

    //列目录请求：同一时刻只有一个有效
    bool                     m_listPending;      //是否有尚未完成的列目录请求（含排队等待就绪的）
    bool                     m_listSent;         //该请求是否已经发出（就绪之前为假）
    std::string              m_listPath;         //请求的目录
    int64_t                  m_listReqId;        //请求号；续取各批沿用同一个请求号
    std::vector<ShareEntry>  m_listEntries;      //已收到的各批条目
    int64_t                  m_listDeadline;     //等下一批结果的截止时间

    //写操作：请求号 → 请求；可同时有多个
    std::map<int64_t, PendingOp> m_ops;

    //搜索：同一时刻只有一个
    int64_t                  m_searchReqId;      //请求号；没有搜索时为 0
    bool                     m_searchSent;       //是否已经发出（就绪之前为假）
    std::string              m_searchPath;       //起点目录
    std::string              m_searchKeyword;    //关键字
    int64_t                  m_searchDeadline;   //等下一批结果的截止时间

    //缩略图请求：请求号 → 请求；可同时有多个
    std::map<int64_t, PendingOp> m_thumbs;

    //目录树请求：同一时刻只有一个
    int64_t                  m_treeReqId;        //请求号；没有时为 0
    bool                     m_treeSent;         //是否已经发出
    std::string              m_treePath;         //目录
    int                      m_treeMax;          //条数上限
    std::vector<ShareEntry>  m_treeEntries;      //已收到的条目
    int64_t                  m_treeDeadline;     //等下一批结果的截止时间
};

} // namespace fileshare
