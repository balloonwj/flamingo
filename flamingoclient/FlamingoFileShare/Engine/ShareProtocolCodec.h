/**
 *  文件共享信令（cmd_share_* 2201~2230）在客户端一侧的编解码（纯逻辑），ShareProtocolCodec.h
 *
 *  请求由本模块编成 JSON 后经宿主 SendShareCommand 发给 chatserver；chatserver 的应答与推送由本模块解析。
 *  字段名与取值类型逐字对应服务端的 chatserversrc/FileShareProtocol（docs/文件共享方案.md 5.1）：
 *    - shareid、userid、cid、ownerid、visitorid 这类 64 位 ID 写成十进制字符串，读取时字符串与整数两种写法都接受；
 *    - includehidden、available、isdir、onthisdevice 这类开关写成整数 0 / 1；
 *    - 字节数、计数、时间写成 JSON 整数。
 *  两边的一致性由样例字符串保证：客户端测试（ShareProtocolCodecTests.cpp）断言编码结果与样例逐字相同、
 *  解析服务端构造出的样例；服务端测试（tests/test_fileshareprotocol.cpp 的 FSP6）断言能解析同一批请求样例、
 *  构造出同一批应答样例。
 *
 *  本模块只依赖标准库与 vendored 的 jsoncpp，不碰网络、不打日志；解析对任何输入只返回 true / false，不抛异常。
 *  单元测试见 FlamingoFileShare/Tests/ShareProtocolCodecTests.cpp（标签 [sharecodec]）。
 *
 *  典型用法：
 *      std::string body = ShareProtocolCodec::BuildListRequest(share_list_scope_mine, 0, 0);
 *      host->SendShareCommand(cmd_share_list, body.data(), body.size());
 *      ...
 *      ShareListResponse resp;
 *      if (ShareProtocolCodec::ParseListResponse(frameBody, resp) && resp.m_code == 0)
 *          ...
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace fileshare {

//cmd_share_list 的 scope 取值（与服务端 FileShareProtocol.h 的 share_list_scope 一致）
const int kShareListScopeMine = 1;       //我的共享
const int kShareListScopeSharedMe = 2;   //共享给我的

//传输方向，位于 2224 请求、2222（传输子会话）与 st_job 的 direction 字段（与服务端 FileShareProtocol.h 的
//share_transfer_direction 一致）
enum share_transfer_direction
{
    share_transfer_download = 1,   //下载：分享者一端读文件，访问者一端写到本机
    share_transfer_upload   = 2,   //上传：访问者一端读本机文件，分享者一端写进共享
};

//一名成员及其写权限位
struct ShareMemberPerm
{
    int64_t m_userId;   //成员 userid
    int     m_perms;    //写权限位，share_perm 的组合

    ShareMemberPerm()
        : m_userId(0)
        , m_perms(0)
    {
    }
};

//cmd_share_create(2201) 请求的内容
struct ShareCreateParams
{
    std::string                  m_clientShareId;   //客户端生成的 UUID
    int                          m_ctype;           //会话类型：1 单聊 2 群聊
    int64_t                      m_cid;             //会话 id：单聊为对端 userid，群聊为群 id
    int                          m_kind;            //共享类型，share_kind
    std::string                  m_name;            //显示名
    std::string                  m_displayPath;     //显示路径
    bool                         m_includeHidden;   //是否包含隐藏文件与系统文件
    std::vector<ShareMemberPerm> m_members;         //成员及其权限

    ShareCreateParams()
        : m_ctype(0)
        , m_cid(0)
        , m_kind(0)
        , m_includeHidden(false)
    {
    }
};

//cmd_share_serve_report(2210) 里的一项
struct ShareServeItem
{
    int64_t m_shareId;     //共享 id
    bool    m_available;   //共享根能否提供
    int     m_reason;      //不能提供的原因（share_unavailable_reason）；能提供时为 0

    ShareServeItem()
        : m_shareId(0)
        , m_available(false)
        , m_reason(0)
    {
    }
};

//cmd_share_request_transfer(2224) 里的一个顶层条目
struct ShareTransferEntry
{
    std::string m_path;    //下载时为共享内的相对路径（单文件共享为空串）；上传时为本机条目名
    bool        m_isDir;   //是否为目录
    int64_t     m_size;    //字节数；目录为其下全部文件之和

    ShareTransferEntry()
        : m_isDir(false)
        , m_size(0)
    {
    }
};

//cmd_share_audit_report(2230) 里的一条执行结果
struct ShareAuditEntry
{
    int64_t     m_shareId;      //所属共享 id
    std::string m_accessId;     //所属访问会话 id
    int64_t     m_visitorId;    //发出请求的访问者 userid
    int         m_action;       //动作，share_audit_action 的 4~9
    std::string m_path;         //相对共享根的路径
    std::string m_toPath;       //重命名的新名字或移动的目标目录
    int64_t     m_size;         //涉及的字节数
    int         m_result;       //0 成功，否则为错误码
    int64_t     m_clientTime;   //执行完成的本机时间（Unix 毫秒）

    ShareAuditEntry()
        : m_shareId(0)
        , m_visitorId(0)
        , m_action(0)
        , m_size(0)
        , m_result(0)
        , m_clientTime(0)
    {
    }
};

//cmd_share_create(2201) 的应答
struct ShareCreateResponse
{
    int                  m_code;             //结果码
    std::string          m_msg;              //结果描述
    int64_t              m_shareId;          //成功时为共享 id
    std::string          m_clientShareId;    //请求里的 clientshareid，原样带回
    std::vector<int64_t> m_invalidMembers;   //回 819 / 821 时不满足条件的成员

    ShareCreateResponse()
        : m_code(0)
        , m_shareId(0)
    {
    }
};

//cmd_share_list(2205) 应答里的一个共享
struct ShareListEntry
{
    int64_t                      m_shareId;        //共享 id
    int64_t                      m_ownerId;        //分享者 userid
    int                          m_ctype;          //所在会话类型
    int64_t                      m_cid;            //所在会话 id（分享者视角）
    int                          m_kind;           //共享类型
    std::string                  m_name;           //显示名
    std::string                  m_displayPath;    //显示路径
    bool                         m_includeHidden;  //是否包含隐藏文件
    int                          m_state;          //对请求者的状态，share_state
    int                          m_reason;         //暂不可用或已失效的原因
    int64_t                      m_createTime;     //发起时间（Unix 毫秒）
    bool                         m_onThisDevice;   //「我的共享」：绑定的设备是不是本机
    std::vector<ShareMemberPerm> m_members;        //「我的共享」：全部成员与权限
    int                          m_myPerms;        //「共享给我的」：我的写权限位

    ShareListEntry()
        : m_shareId(0)
        , m_ownerId(0)
        , m_ctype(0)
        , m_cid(0)
        , m_kind(0)
        , m_includeHidden(false)
        , m_state(0)
        , m_reason(0)
        , m_createTime(0)
        , m_onThisDevice(false)
        , m_myPerms(0)
    {
    }
};

//cmd_share_list(2205) 的应答
struct ShareListResponse
{
    int                         m_code;     //结果码
    std::string                 m_msg;      //结果描述
    std::vector<ShareListEntry> m_shares;   //共享列表

    ShareListResponse()
        : m_code(0)
    {
    }
};

//cmd_share_relay_info(2222)
struct ShareRelayParams
{
    std::string m_accessId;          //访问会话 id
    std::string m_transferId;        //传输子会话 id；为空表示访问会话本身
    std::string m_relayIp;           //fileserver 中转地址
    int         m_relayPort;         //fileserver 中转端口
    std::string m_token;             //本端的中转令牌
    int         m_role;              //本端角色：9 分享者、10 访问者
    int         m_lanes;             //数据通道条数；访问会话为 0
    int         m_openTimeoutMs;     //访问者等待分享者 sf_hello 的毫秒数
    int         m_idleTimeoutMs;     //访问会话空闲回收的毫秒数
    int         m_direction;         //传输方向：1 下载 2 上传；只有传输子会话带，访问会话为 0
    std::string m_p2pSessionId;      //服务端为这次传输建的 P2P 会话号（P2P 方案第 6 期）；为空表示没建，各条通道走中转
    bool        m_forOwner;          //是不是推给分享者的那一帧（带下面五项）
    int64_t     m_shareId;           //共享 id（只给分享者）
    int64_t     m_visitorId;         //访问者 userid（只给分享者）
    std::string m_visitorName;       //访问者显示名（只给分享者）
    int         m_perms;             //访问者的写权限位（只给分享者）
    int         m_searchMaxResults;  //一次搜索最多返回的条数（只给分享者）
    int         m_searchTimeoutMs;   //一次搜索最长耗时毫秒数（只给分享者）

    ShareRelayParams()
        : m_relayPort(0)
        , m_role(0)
        , m_lanes(0)
        , m_openTimeoutMs(0)
        , m_idleTimeoutMs(0)
        , m_direction(0)
        , m_forOwner(false)
        , m_shareId(0)
        , m_visitorId(0)
        , m_perms(0)
        , m_searchMaxResults(0)
        , m_searchTimeoutMs(0)
    {
    }
};

//共享卡片（msgtype 20）正文，含服务端按看的人合并的字段
struct ShareCardInfo
{
    int64_t     m_shareId;       //共享 id
    int         m_kind;          //共享类型
    std::string m_name;          //显示名
    std::string m_displayPath;   //显示路径
    int64_t     m_ownerId;       //分享者 userid
    bool        m_hasState;      //是否带了合并的状态（sharestate / sharereason）
    int         m_state;         //合并的状态，share_state
    int         m_reason;        //合并的原因
    bool        m_hasPerms;      //是否带了 myperms（分享者本人与已失效的成员不带）
    int         m_myPerms;       //看的人的写权限位

    ShareCardInfo()
        : m_shareId(0)
        , m_kind(0)
        , m_ownerId(0)
        , m_hasState(false)
        , m_state(0)
        , m_reason(0)
        , m_hasPerms(false)
        , m_myPerms(0)
    {
    }
};

/**
 *  ShareProtocolCodec —— 文件共享信令的客户端编解码；全部为静态方法，不可实例化。
 */
class ShareProtocolCodec
{
public:
    //==== 编码请求 ====

    //cmd_share_create(2201)
    static std::string BuildCreateRequest(const ShareCreateParams& params);

    //只带 shareid 的请求：cmd_share_cancel(2202)、cmd_share_open(2220)
    static std::string BuildShareIdRequest(int64_t shareId);

    //cmd_share_update_members(2204)；三组都可以为空数组，为空时不写出该字段
    static std::string BuildUpdateMembersRequest(int64_t shareId, const std::vector<ShareMemberPerm>& add,
                                                 const std::vector<int64_t>& remove,
                                                 const std::vector<ShareMemberPerm>& update);

    //cmd_share_list(2205)；ctype 为 0 时不按会话筛选，不写 ctype 与 cid
    static std::string BuildListRequest(int scope, int ctype, int64_t cid);

    //cmd_share_serve_report(2210)
    static std::string BuildServeReport(const std::vector<ShareServeItem>& items);

    //cmd_share_close_access(2221)
    static std::string BuildCloseAccessRequest(const std::string& accessId, int reason);

    //cmd_share_request_transfer(2224)
    static std::string BuildTransferRequest(const std::string& accessId, int direction,
                                            const std::vector<ShareTransferEntry>& items,
                                            int64_t totalBytes, int64_t totalFiles);

    //cmd_share_transfer_close(2225)；result 取 0 成功 / 1 失败，成功时 errcode 写 0
    static std::string BuildTransferClose(const std::string& transferId, int result, int errCode);

    //cmd_share_audit_report(2230)
    static std::string BuildAuditReport(const std::vector<ShareAuditEntry>& items);

    //==== 解析应答与推送 ====

    //只有 {code, msg} 的应答（2202、2204、2210、2221、2225、2230）
    static bool ParseSimpleResponse(const std::string& json, int& code, std::string& msg);

    //cmd_share_create(2201) 的应答
    static bool ParseCreateResponse(const std::string& json, ShareCreateResponse& out);

    //cmd_share_update_members(2204) 的应答：被拒时可带 invalidmembers（不满足条件的新增成员），没有时 invalid 为空
    static bool ParseUpdateMembersResponse(const std::string& json, int& code, std::string& msg,
                                           std::vector<int64_t>& invalid);

    //cmd_share_list(2205) 的应答
    static bool ParseListResponse(const std::string& json, ShareListResponse& out);

    //cmd_share_open(2220) 的应答：成功时带 accessid
    static bool ParseOpenResponse(const std::string& json, int& code, std::string& msg, std::string& accessId);

    //cmd_share_open(2220) 的应答，连同共享名称一起取（2026-10-04 起服务端在成功时带 name；没有时 name 为空）。
    //  name：[出参] 共享名称（UTF-8），浏览窗口用它作标题与路径栏的第一节
    static bool ParseOpenResponse(const std::string& json, int& code, std::string& msg, std::string& accessId,
                                  std::string& name);

    //cmd_share_request_transfer(2224) 的应答：成功时带 transferid
    static bool ParseTransferResponse(const std::string& json, int& code, std::string& msg, std::string& transferId);

    //cmd_share_state_notify(2203)
    static bool ParseStateNotify(const std::string& json, int64_t& shareId, int& state, int& reason);

    //cmd_share_perms_notify(2206)
    static bool ParsePermsNotify(const std::string& json, int64_t& shareId, int64_t& userId, int& perms);

    //cmd_share_relay_info(2222)；带 shareid 的是推给分享者的那一帧
    static bool ParseRelayInfo(const std::string& json, ShareRelayParams& out);

    //cmd_share_access_state_notify(2223)
    static bool ParseAccessEnded(const std::string& json, std::string& accessId, std::string& transferId,
                                 int& errCode);

    //共享卡片正文（msgtype 20），含合并字段
    static bool ParseCardContent(const std::string& json, ShareCardInfo& out);

private:
    ShareProtocolCodec() = delete;
};

} // namespace fileshare
