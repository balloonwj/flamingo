/**
 *  v2 文件共享信令编解码与共享状态计算（服务端侧），FileShareProtocol.h
 *
 *  在单聊或群聊里把本机的磁盘、文件夹或文件共享给指定的人（方案见 docs/文件共享方案.md）。
 *  信令走 chatserver 主连接上的 cmd_share_* 2201~2230，文件字节走 fileserver 中转。
 *  本模块负责：
 *    - 解析客户端发来的各条请求（发起、取消、调整成员、拉取列表、在线提供报告、打开、结束访问、
 *      申请传输、结束传输、审计上报）；
 *    - 构造回给客户端的应答与推送（状态变更、权限变更、中转参数、会话结束）；
 *    - 共享卡片（message_type_fileshare）的正文构造、解析，以及按「看的人」合并状态；
 *    - 共享对某个人的状态计算 computeShareState（可用 / 暂不可用 / 已失效及原因）。
 *
 *  字段编码沿用仓库既有惯例：userid、shareid 这类 64 位 ID 以十进制字符串编码传输（客户端若按
 *  IEEE754 双精度解析，超过 2^53 的整数会丢精度），读取时字符串与整数两种写法都接受；字节数、
 *  计数这类数量以 JSON 整数编码。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp 与标准库，不触及网络、数据库与日志，便于单元测试，见
 *  tests/test_fileshareprotocol.cpp（Suite FSP）。会话表、名额闸门与令牌签发在
 *  FileShareSessionManager，鉴权、数据库读写与推送在会话层。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "Protocol.h"   //share_kind / share_perm / share_state 等枚举与 error_code_share_*

//发起共享时 clientshareid 的最大字节数，与 t_file_share.f_client_share_id 的 varchar(64) 对应
const size_t kShareClientShareIdMaxBytes = 64;

//共享显示名的最大字节数（UTF-8），与 t_file_share.f_name 的 varchar(260) 对应
const size_t kShareNameMaxBytes = 260;

//共享显示路径的最大字节数（UTF-8），与 t_file_share.f_display_path 的 varchar(1024) 对应
const size_t kShareDisplayPathMaxBytes = 1024;

//访问会话 id、传输子会话 id 的最大字节数，与 t_file_share_audit.f_access_id 的 varchar(64) 对应
const size_t kShareSessionIdMaxBytes = 64;

//五个写权限位全部置位时的取值（1|2|4|8|16），合法的 perms 一律落在 [0, kSharePermAll] 之内
const int kSharePermAll = share_perm_upload | share_perm_mkdir | share_perm_rename
                          | share_perm_delete | share_perm_move;

//一次发起共享或调整成员最多涉及的成员数。群人数有上限，这里给足余量，防止一个超大数组撑爆处理
const size_t kShareMaxMembersPerRequest = 2000;

//一次申请传输最多带的条目数（多选下载 / 上传的顶层条目），目录内部的文件不计入
const size_t kShareMaxTransferItems = 1000;

//一次审计上报最多带的条数，分享者客户端按此攒批
const size_t kShareAuditBatchMax = 200;

//cmd_share_list 的 scope 字段取值
enum share_list_scope
{
    share_list_scope_mine      = 1,   //我的共享：本人作为分享者的共享，带成员与权限、是否位于本机
    share_list_scope_shared_me = 2,   //共享给我的：本人作为成员的共享，带分享者与本人的权限
};

//cmd_share_request_transfer 的 direction 字段取值
enum share_transfer_direction
{
    share_transfer_download = 1,   //下载：分享者一端读文件，访问者一端写到本机
    share_transfer_upload   = 2,   //上传：访问者一端读本机文件，分享者一端写进共享
};

//cmd_share_transfer_close 的 result 字段取值
enum share_transfer_result
{
    share_transfer_result_success = 0,   //传输子会话里的全部条目都传完并通过校验
    share_transfer_result_failed  = 1,   //失败或被取消，errcode 说明原因
};

//cmd_share_create / cmd_share_update_members 里的一名成员及其写权限
struct ShareMemberSpec
{
    int64_t m_userId;   //成员 userid；JSON 中以十进制字符串编码
    int     m_perms;    //写权限位，取值为 share_perm 的组合，落在 [0, kSharePermAll]；下载不占位

    ShareMemberSpec()
        : m_userId(0)
        , m_perms(0)
    {
    }
};

//cmd_share_create(2201) 请求
struct ShareCreateRequest
{
    std::string                  m_clientShareId;   //客户端生成的 UUID，非空、不超过 kShareClientShareIdMaxBytes 字节；重发去重用
    int                          m_ctype;           //会话类型：conversation_single / conversation_group
    int64_t                      m_cid;             //会话 id：单聊为对端 userid，群聊为群 id；必须为正
    int                          m_kind;            //共享类型，取值见 share_kind
    std::string                  m_name;            //共享名称：分享者起的名字，没起时客户端填「XXX的文件共享」；非空、不超过 kShareNameMaxBytes 字节，
                                                    //是分享者以外的人唯一能看到的称呼
    std::string                  m_displayPath;     //分享者本机上的真实路径，非空、不超过 kShareDisplayPathMaxBytes 字节；只给分享者本人与管理后台看
    bool                         m_includeHidden;   //是否包含隐藏文件与系统文件
    std::vector<ShareMemberSpec> m_members;         //成员与权限；单聊时只能是对端一人，群聊时至少一人且不重复

    ShareCreateRequest()
        : m_ctype(0)
        , m_cid(0)
        , m_kind(0)
        , m_includeHidden(false)
    {
    }
};

//cmd_share_update_members(2204) 请求：三组变更至少一组非空，同一个人只能出现在其中一组、一次
struct ShareUpdateMembersRequest
{
    int64_t                      m_shareId;   //共享 id，必须为正
    std::vector<ShareMemberSpec> m_add;       //追加的成员及其权限
    std::vector<int64_t>         m_remove;    //移除的成员 userid
    std::vector<ShareMemberSpec> m_update;    //修改权限的成员及其新权限

    ShareUpdateMembersRequest()
        : m_shareId(0)
    {
    }
};

//cmd_share_list(2205) 请求；m_ctype 与 m_cid 要么都给（只要该会话里的共享），要么都不给
struct ShareListRequest
{
    int     m_scope;   //取值见 share_list_scope
    int     m_ctype;   //会话类型；0 表示不按会话筛选
    int64_t m_cid;     //会话 id；0 表示不按会话筛选

    ShareListRequest()
        : m_scope(0)
        , m_ctype(0)
        , m_cid(0)
    {
    }
};

//cmd_share_serve_report(2210) 里的一项：某个共享在分享者本机能否提供
struct ShareServeReportItem
{
    int64_t m_shareId;     //共享 id，必须为正
    bool    m_available;   //能否提供
    int     m_reason;      //不能提供的原因，取值见 share_unavailable_reason；能提供时为 0

    ShareServeReportItem()
        : m_shareId(0)
        , m_available(false)
        , m_reason(0)
    {
    }
};

//cmd_share_request_transfer(2224) 里的一个顶层条目
struct ShareTransferItem
{
    std::string m_path;    //下载时为共享内的相对路径（/ 分隔，可为空串表示单文件共享的根），
                           //上传时为本机条目名；服务端只做长度与非空检查，路径安全由分享者一端校验
    bool        m_isDir;   //是否为目录
    int64_t     m_size;    //字节数；目录为其下全部文件之和，未知时可为 0

    ShareTransferItem()
        : m_isDir(false)
        , m_size(0)
    {
    }
};

//cmd_share_request_transfer(2224) 请求
struct ShareTransferRequest
{
    std::string                    m_accessId;     //所属访问会话 id，非空
    int                            m_direction;    //传输方向，取值见 share_transfer_direction
    std::vector<ShareTransferItem> m_items;        //顶层条目，非空且不超过 kShareMaxTransferItems 条
    int64_t                        m_totalBytes;   //全部条目的字节数之和，非负
    int64_t                        m_totalFiles;   //全部条目展开后的文件数，非负

    ShareTransferRequest()
        : m_direction(0)
        , m_totalBytes(0)
        , m_totalFiles(0)
    {
    }
};

//cmd_share_audit_report(2230) 里的一条执行结果
struct ShareAuditItem
{
    int64_t     m_shareId;      //所属共享 id，必须为正；服务端据此核对上报者是不是该共享的分享者
                                //（上报到达时访问会话往往已经结束，从 accessid 查不到共享）
    std::string m_accessId;     //所属访问会话 id，非空
    int64_t     m_visitorId;    //发出请求的访问者 userid，必须为正
    int         m_action;       //动作，取值见 share_audit_action；客户端只能上报 4~9 这几种执行结果
    std::string m_path;         //相对共享根的路径
    std::string m_toPath;       //重命名的新名字或移动的目标目录；其余动作为空
    int64_t     m_size;         //涉及的字节数，非负
    int         m_result;       //0 成功，否则为错误码
    int64_t     m_clientTime;   //客户端执行完成的时间（Unix 毫秒），只作参考，入库用服务端时间

    ShareAuditItem()
        : m_shareId(0)
        , m_visitorId(0)
        , m_action(0)
        , m_size(0)
        , m_result(0)
        , m_clientTime(0)
    {
    }
};

//cmd_share_list(2205) 应答里的一个共享
struct ShareListItem
{
    int64_t                      m_shareId;       //共享 id
    int64_t                      m_ownerId;       //分享者 userid
    int                          m_ctype;         //所在会话类型
    int64_t                      m_cid;           //所在会话 id（分享者视角）
    int                          m_kind;          //共享类型
    std::string                  m_name;          //共享名称
    std::string                  m_displayPath;   //真实路径；只在「我的共享」里写出
    bool                         m_includeHidden; //是否包含隐藏文件
    int                          m_state;         //对请求者的状态，取值见 share_state
    int                          m_reason;        //暂不可用或已失效的原因；可用时为 0
    int64_t                      m_createTime;    //发起时间（Unix 毫秒）
    //以下两项只在「我的共享」里写出
    bool                         m_onThisDevice;  //绑定的设备是否就是请求者当前登录的这台电脑
    std::vector<ShareMemberSpec> m_members;       //全部成员与各自的权限
    //以下一项只在「共享给我的」里写出
    int                          m_myPerms;       //请求者本人的写权限位

    ShareListItem()
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

//cmd_share_relay_info(2222) 的内容。推给分享者与访问者的两帧共用本结构，m_forOwner 决定写出哪些字段
struct ShareRelayInfo
{
    std::string m_accessId;          //访问会话 id
    std::string m_transferId;        //传输子会话 id；为空表示这是访问会话本身的中转参数
    std::string m_relayIp;           //fileserver 的中转地址
    int         m_relayPort;         //fileserver 的监听端口
    std::string m_token;             //本端的中转令牌
    int         m_role;              //本端角色：RelayToken::role_share_owner(9) 或 role_share_visitor(10)
    int         m_lanes;             //数据通道条数；访问会话恒为 0（只有 lane 0 控制通道）
    int         m_openTimeoutMs;     //访问者等待分享者 sf_hello 的毫秒数（配置项 shareopentimeout）
    int         m_idleTimeoutMs;     //访问会话无请求时回收连接的毫秒数（配置项 shareidletimeout）
    int         m_direction;         //传输方向，share_transfer_direction：1 下载 2 上传；只有传输子会话写出，访问会话为 0。
                                     //分享者据此决定以发送端还是接收端的身份建通道（第 1 期第 14 步补充）
    std::string m_p2pSessionId;      //服务端为这次传输建的 P2P 会话号（P2P 方案第 6 期）；只有传输子会话且非空时写出，
                                     //为空表示没建，两端各条通道都走中转
    bool        m_forOwner;          //是否推给分享者；为真时另外写出下面五项
    int64_t     m_shareId;           //共享 id（只给分享者）
    int64_t     m_visitorId;         //访问者 userid（只给分享者）
    std::string m_visitorName;       //访问者显示名（只给分享者）
    int         m_perms;             //访问者此刻的写权限位（只给分享者，分享者据此检查每个请求）
    int         m_searchMaxResults;  //一次搜索最多返回的条数（只给分享者，配置项 sharesearchmaxresults）
    int         m_searchTimeoutMs;   //一次搜索最长耗时毫秒数（只给分享者，配置项 sharesearchtimeout）

    ShareRelayInfo()
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

//共享卡片（message_type_fileshare）正文里创建后不再变化的字段
struct ShareCardContent
{
    int64_t     m_shareId;       //共享 id
    int         m_kind;          //共享类型
    std::string m_name;          //共享名称
    std::string m_displayPath;   //真实路径；2026-10-04 起入库的卡片为空，下发给分享者本人时才补上
    int64_t     m_ownerId;       //分享者 userid

    ShareCardContent()
        : m_shareId(0)
        , m_kind(0)
        , m_ownerId(0)
    {
    }
};

//共享对某个人的状态（computeShareState 的结果，也是合并进卡片与列表的内容）
struct ShareStateView
{
    int m_state;    //取值见 share_state
    int m_reason;   //暂不可用时取 share_unavailable_reason，已失效时取 share_invalid_reason，可用时为 0

    ShareStateView()
        : m_state(share_state_available)
        , m_reason(0)
    {
    }
};

//computeShareState 的输入：计算「某个共享对某个人」的状态所需的全部事实，由会话层从数据库与内存里取齐
struct ShareStateInput
{
    bool m_canceled;             //共享是否已被取消（t_file_share.f_state = 2）
    int  m_cancelReason;         //取消原因，取值见 share_invalid_reason（1~5）；未取消时忽略
    bool m_viewerIsOwner;        //看的人是不是分享者本人
    bool m_viewerIsMember;       //看的人是不是当前成员（t_file_share_member 里有他）
    bool m_viewerInConversation; //看的人是否仍在共享所在的会话里（群聊为仍是群成员；单聊恒为 true）
    bool m_featureEnabled;       //分享者所属企业的文件共享开关是否开启
    bool m_ownerOnline;          //分享者是否有在线连接
    bool m_ownerOnBoundDevice;   //分享者是否在共享绑定的那台电脑上在线
    bool m_rootAvailable;        //绑定的那台电脑是否报告共享根可以提供（cmd_share_serve_report）

    ShareStateInput()
        : m_canceled(false)
        , m_cancelReason(0)
        , m_viewerIsOwner(false)
        , m_viewerIsMember(false)
        , m_viewerInConversation(true)
        , m_featureEnabled(true)
        , m_ownerOnline(false)
        , m_ownerOnBoundDevice(false)
        , m_rootAvailable(false)
    {
    }
};

/**
 *  文件共享信令编解码与状态计算工具类；全部为静态方法，不可实例化。
 *  解析类方法对任何输入都只返回 true / false，不抛异常：JSON 非法、字段缺失、字段类型不对、
 *  取值越界一律返回 false，且此时出参内容未定义，调用方不得取用。
 */
class FileShareProtocol
{
public:
    //==== 解析 客户端→服务端 的请求 ====

    /**
     *  解析发起共享请求（cmd_share_create=2201）。
     *  @param json 请求包体（UTF-8 JSON）
     *  @param req  [出参] 解析结果
     *  @return 各字段齐全且合法时返回 true：clientshareid 非空且不超长；ctype 为单聊或群聊；cid 为正；
     *          kind 落在 share_kind 之内；name、displaypath 非空且不超长；includehidden 缺省或为 0 / 1；
     *          members 非空、不超过 kShareMaxMembersPerRequest、userid 为正且不重复、perms 落在
     *          [0, kSharePermAll]；单聊时 members 恰好一人且就是 cid。单文件共享带来的 perms 不算错误，
     *          解析结果里一律清零
     */
    static bool parseCreateRequest(const std::string& json, ShareCreateRequest& req);

    /**
     *  解析只带共享 id 的请求（cmd_share_cancel=2202、cmd_share_open=2220 共用）。
     *  @param json    请求包体
     *  @param shareId [出参] 共享 id
     *  @return shareid 为正时返回 true
     */
    static bool parseShareIdRequest(const std::string& json, int64_t& shareId);

    /**
     *  解析调整成员请求（cmd_share_update_members=2204）。
     *  @param json 请求包体
     *  @param req  [出参] 解析结果
     *  @return shareid 为正、add / remove / update 至少一组非空、三组合计不超过 kShareMaxMembersPerRequest、
     *          所有 userid 为正且同一个人在三组里合计只出现一次、perms 合法时返回 true
     */
    static bool parseUpdateMembersRequest(const std::string& json, ShareUpdateMembersRequest& req);

    /**
     *  解析拉取共享列表请求（cmd_share_list=2205）。
     *  @param json 请求包体
     *  @param req  [出参] 解析结果
     *  @return scope 为 1 或 2；ctype 与 cid 要么都缺省，要么 ctype 为单聊或群聊且 cid 为正时返回 true
     */
    static bool parseListRequest(const std::string& json, ShareListRequest& req);

    /**
     *  解析在线提供报告（cmd_share_serve_report=2210）。
     *  @param json  请求包体
     *  @param items [出参] 各项报告
     *  @return items 非空、不超过 kShareMaxMembersPerRequest 项，每项 shareid 为正、available 为 0 / 1，
     *          且 available 为 1 时 reason 缺省或为 0、为 0 时 reason 是合法的 share_unavailable_reason 时返回 true
     */
    static bool parseServeReport(const std::string& json, std::vector<ShareServeReportItem>& items);

    /**
     *  解析结束访问会话请求（cmd_share_close_access=2221）。
     *  @param json     请求包体
     *  @param accessId [出参] 访问会话 id
     *  @param errCode  [出参] 结束原因，缺省为 0（正常关闭）
     *  @return accessid 非空且不超过 kShareSessionIdMaxBytes、reason 缺省或为整数时返回 true
     */
    static bool parseCloseAccessRequest(const std::string& json, std::string& accessId, int& errCode);

    /**
     *  解析申请传输请求（cmd_share_request_transfer=2224）。
     *  @param json 请求包体
     *  @param req  [出参] 解析结果
     *  @return accessid 非空不超长、direction 为 1 或 2、items 非空且不超过 kShareMaxTransferItems、
     *          每个条目的 isdir 为 0 / 1 且 size 非负、上传时条目名非空、totalbytes 与 totalfiles 非负时返回 true
     */
    static bool parseTransferRequest(const std::string& json, ShareTransferRequest& req);

    /**
     *  解析结束传输子会话请求（cmd_share_transfer_close=2225）。
     *  @param json       请求包体
     *  @param transferId [出参] 传输子会话 id
     *  @param result     [出参] 结束结果，取值见 share_transfer_result
     *  @param errCode    [出参] 失败原因；成功时为 0
     *  @return transferid 非空不超长、result 为 0 或 1、成功时 errcode 缺省或为 0 时返回 true
     */
    static bool parseTransferClose(const std::string& json, std::string& transferId, int& result, int& errCode);

    /**
     *  解析审计上报（cmd_share_audit_report=2230）。
     *  @param json  请求包体
     *  @param items [出参] 各条执行结果
     *  @return items 非空、不超过 kShareAuditBatchMax 条，每条 shareid 为正、accessid 非空、visitorid 为正、
     *          action 为客户端可上报的 4~9 之一、size 非负时返回 true
     */
    static bool parseAuditReport(const std::string& json, std::vector<ShareAuditItem>& items);

    //==== 构造 服务端→客户端 的应答与推送 ====

    /**
     *  构造只带结果码的通用应答（cmd_share_cancel / cmd_share_update_members / cmd_share_serve_report /
     *  cmd_share_close_access / cmd_share_transfer_close / cmd_share_audit_report 的应答共用）。
     *  @param code 结果码，0 成功，非 0 见 Protocol.h 的 error_code
     *  @param msg  结果描述（英文）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

    /**
     *  构造发起共享的应答（cmd_share_create=2201）。
     *  @param code           结果码
     *  @param msg            结果描述
     *  @param shareId        成功时为共享 id；失败时传 0，不写出
     *  @param clientShareId  请求里的 clientshareid，原样带回，供客户端把应答对上本机记录
     *  @param invalidMembers 因跨企业等原因不满足条件的成员 userid（code 为 821 时写出，供界面标出这些人）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildCreateResponse(int code, const std::string& msg, int64_t shareId,
                                           const std::string& clientShareId,
                                           const std::vector<int64_t>& invalidMembers);

    /**
     *  构造调整成员的应答（cmd_share_update_members=2204）：在只有结果码的应答之外，被拒时可带不满足条件的成员。
     *  @param code           结果码
     *  @param msg            结果描述
     *  @param invalidMembers 因跨企业、不属于会话而不满足条件的新增成员 userid（为空时不写出，此时与
     *                        buildSimpleResponse 的结果逐字相同）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildUpdateMembersResponse(int code, const std::string& msg,
                                                  const std::vector<int64_t>& invalidMembers);

    /**
     *  构造拉取共享列表的应答（cmd_share_list=2205）。
     *  @param code  结果码
     *  @param msg   结果描述
     *  @param scope 请求的 scope，决定每项写出「我的共享」专有的字段还是「共享给我的」专有的字段；
     *               真实路径 displaypath 只在「我的共享」里写出（2026-10-04 起，分享者的路径不发给别人）
     *  @param items 列表项
     *  @return 紧凑 JSON 文本
     */
    static std::string buildListResponse(int code, const std::string& msg, int scope,
                                         const std::vector<ShareListItem>& items);

    /**
     *  构造打开共享的应答（cmd_share_open=2220）。
     *  @param code     结果码
     *  @param msg      结果描述
     *  @param accessId 成功时为新建的访问会话 id；失败时传空串，不写出
     *  @return 紧凑 JSON 文本
     */
    static std::string buildOpenResponse(int code, const std::string& msg, const std::string& accessId);

    /**
     *  构造打开共享的应答（cmd_share_open=2220），成功时带上共享名称（2026-10-04 起）：访问者的浏览窗口用它作标题与
     *  路径栏的第一节，不再用分享者握手时给出的、取自真实路径的根名。
     *  @param code     结果码
     *  @param msg      结果描述
     *  @param accessId 成功时为新建的访问会话 id；失败时传空串，不写出
     *  @param name     共享名称；为空时不写出
     *  @return 紧凑 JSON 文本
     */
    static std::string buildOpenResponse(int code, const std::string& msg, const std::string& accessId,
                                         const std::string& name);

    /**
     *  构造申请传输的应答（cmd_share_request_transfer=2224）。
     *  @param code       结果码
     *  @param msg        结果描述
     *  @param transferId 成功时为新建的传输子会话 id；失败时传空串，不写出
     *  @return 紧凑 JSON 文本
     */
    static std::string buildTransferResponse(int code, const std::string& msg, const std::string& transferId);

    /**
     *  构造状态变更推送（cmd_share_state_notify=2203），推给某一个人，内容是该共享对此人的状态。
     *  @param shareId 共享 id
     *  @param view    对此人的状态与原因
     *  @return 紧凑 JSON 文本
     */
    static std::string buildStateNotify(int64_t shareId, const ShareStateView& view);

    /**
     *  构造权限变更推送（cmd_share_perms_notify=2206）。
     *  @param shareId 共享 id
     *  @param userId  权限被调整的成员 userid
     *  @param perms   新的写权限位
     *  @return 紧凑 JSON 文本
     */
    static std::string buildPermsNotify(int64_t shareId, int64_t userId, int perms);

    /**
     *  构造中转参数推送（cmd_share_relay_info=2222）。info.m_forOwner 为真时另外写出分享者需要的五项。
     *  @param info 推送内容
     *  @return 紧凑 JSON 文本
     */
    static std::string buildRelayInfo(const ShareRelayInfo& info);

    /**
     *  构造会话结束推送（cmd_share_access_state_notify=2223）。收到即表示该访问会话（transferid 为空时）
     *  或该传输子会话已经结束，errcode 说明原因（0 表示对端正常关闭）。
     *  @param accessId   访问会话 id
     *  @param transferId 传输子会话 id；结束的是访问会话本身时传空串
     *  @param errCode    结束原因
     *  @return 紧凑 JSON 文本
     */
    static std::string buildAccessEndedNotify(const std::string& accessId, const std::string& transferId,
                                              int errCode);

    //==== 共享卡片（message_type_fileshare）====

    /**
     *  构造共享卡片的正文（入库的 content，只含创建后不变的字段）。m_displayPath 为空时不写出 displaypath：
     *  2026-10-04 起入库的卡片不带真实路径，下发给分享者本人时才由 augmentCardContent 补上。
     *  @param card 卡片内容
     *  @return 紧凑 JSON 文本
     */
    static std::string buildCardContent(const ShareCardContent& card);

    /**
     *  解析共享卡片的正文。合并字段（sharestate 等）存在与否都不影响解析。
     *  @param json 卡片正文
     *  @param card [出参] 解析结果
     *  @return shareid 与 ownerid 为正、kind 合法、name 非空时返回 true
     */
    static bool parseCardContent(const std::string& json, ShareCardContent& card);

    /**
     *  把「看的人」的状态与权限合并进卡片正文，下发前调用（入库的正文不含这些字段）。
     *  分享者在本机上的真实路径（displaypath）只给分享者本人看（2026-10-04 起）：ownerDisplayPath 非空时写入，
     *  为 NULL 或空串时从正文里去掉。2026-10-04 之前入库的卡片正文带着这个字段，同样按此去掉。
     *  @param content    卡片正文（入库的那份）
     *  @param view       对看的人的状态
     *  @param includePerms 是否写出 myperms（看的人是分享者本人时传 false）
     *  @param perms      看的人的写权限位；includePerms 为 false 时忽略
     *  @param ownerDisplayPath 看的人是分享者本人时传共享记录里的真实路径，其余的人传 NULL；只读借用，不持有
     *  @return 合并后的正文；content 解析不了时原样返回，不让一条坏消息挡住整次同步
     */
    static std::string augmentCardContent(const std::string& content, const ShareStateView& view,
                                          bool includePerms, int perms, const std::string* ownerDisplayPath);

    /**
     *  从卡片正文里去掉真实路径（displaypath），其余字段不变。查不到共享记录、无从合并状态时，下发前用它
     *  保证真实路径不会随旧卡片发给分享者以外的人。
     *  @param content 卡片正文
     *  @return 去掉路径后的正文；content 解析不了或本来就不带路径时原样返回
     */
    static std::string removeCardDisplayPath(const std::string& content);

    //==== 状态计算与取值校验 ====

    /**
     *  计算某个共享对某个人的状态。判定顺序：已取消 → 看的人已不是成员 → 功能已关闭 → 分享者不在线 →
     *  分享者在别的电脑上 → 共享根不可提供 → 可用。分享者本人视为恒为「成员」。
     *  @param in 所需事实
     *  @return 状态与原因
     */
    static ShareStateView computeShareState(const ShareStateInput& in);

    /**
     *  判定写权限位是否合法。
     *  @param perms 待判定的值
     *  @return 落在 [0, kSharePermAll] 之内返回 true
     */
    static bool isValidPerms(int perms);

    /**
     *  判定共享类型是否合法。
     *  @param kind 待判定的值
     *  @return 为 share_kind 之一返回 true
     */
    static bool isValidKind(int kind);

    /**
     *  判定一个审计动作是否允许由分享者客户端上报。
     *  @param action 待判定的值
     *  @return 为 share_audit_download_file(4) ~ share_audit_move(9) 之一返回 true；其余动作只能由服务端记录
     */
    static bool isClientReportableAction(int action);

private:
    FileShareProtocol() = delete;
    ~FileShareProtocol() = delete;
    FileShareProtocol(const FileShareProtocol&) = delete;
    FileShareProtocol& operator=(const FileShareProtocol&) = delete;
};
