/**
 *  v2 单聊文件夹传输信令编解码（服务端侧），FolderTransferProtocol.h
 *
 *  单聊里把一整个目录连同子目录结构发给对方，先经 chatserver 主连接上的
 *  cmd_folder_invite(1810) ~ cmd_folder_retry(1817) 这八条信令征得接收方同意、下发
 *  中转参数，真正的文件字节走 fileserver 的中转通道（cmd_folder_relay_hello 1857 /
 *  cmd_folder_relay_data 1858），服务端只在内存里解密、重新加密后转发，不落盘、不入库。
 *  本模块负责这八条信令的 JSON 编解码：解析客户端发来的五条请求（邀请 / 应答 / 撤回 /
 *  结束 / 重试），构造回给客户端的应答，以及三条服务器主动推送（邀请推送 / 状态推送 /
 *  中转参数下发）。此外还负责一次传输走到终态后留在双方单聊会话里的那条聊天记录的
 *  content 编解码（msgtype 为 message_type_folder=18，见 buildRecordContent）——
 *  服务端只记这次传输的动作与结局，文件清单与文件内容一概不记。
 *
 *  与远程桌面（RemoteDesktopProtocol）、音视频通话（CallProtocol）两个同类模块同构，
 *  差异有三处：邀请请求与邀请推送多带一组规模统计（目录数 / 文件数 / 总字节数），且规模
 *  超限的应答要回带服务端配置的上限值；中转参数推送多一个数据通道条数 lanes；结束请求与
 *  状态推送多带 errcode / errmsg，使两端看到同一条失败原因。会话状态取值见 Protocol.h 的
 *  enum folder_transfer_state，失败原因取值见同文件的 error_code_folder_*（606~620）。
 *
 *  字段编码沿用仓库既有惯例：userid 这类 64 位 ID 以十进制字符串编码传输（客户端若按
 *  IEEE754 双精度解析，超过 2^53 的整数会丢精度），字节数与计数这类数量以 JSON 整数编码
 *  （做法与 MessageProtocol 的 timestamp、FileProtocol 的 size 一致）。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp 与标准库，不触及网络、数据库与日志，便于单元测试，
 *  见 tests/test_foldertransferprotocol.cpp。内存会话表、超时清理与中转令牌签发在
 *  FolderTransferManager，鉴权、在线判定与推送在会话层 ChatSession。
 *
 *  协议契约见 docs/协议设计-v2.md「文件夹传输协议（单聊）」一章，整体方案见
 *  docs/单聊文件夹传输方案.md。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stdint.h>
#include <string>
#include "Protocol.h"   //folder_transfer_state 与 error_code_folder_* 枚举

//cmd_folder_response(1812) 的 accept 字段取值；除这两个值以外一律视为非法请求
enum folder_response_type
{
    folder_response_reject = 0,   //拒绝：接收端在会话气泡上点了「拒绝」，服务端置会话为已拒绝并立即销毁
    folder_response_accept = 1,   //接收：接收端点了「接收」，服务端给双方各签一枚中转令牌并下发中转参数
};

//cmd_folder_close(1814) 的 result 字段取值；除这两个值以外一律视为非法请求
enum folder_close_result
{
    folder_close_success = 0,   //成功结束：全部文件传完并通过校验，errcode 须为 0，服务端立即销毁会话
    folder_close_failed  = 1,   //失败结束：任一环节出错（含用户中途取消），errcode 须为 error_code_folder_* 之一，
                                //会话保留 folderretrywindow 供重试续传
};

//cmd_folder_invite(1810) 请求：发送端发起一次文件夹传输，带根目录名与三项规模统计。
//规模统计由发送端扫描本地目录树得出，服务端据此与配置的上限比对，超限直接拒绝。
struct FolderInviteRequest
{
    int64_t     peerid;       //接收方 userid；JSON 中以十进制字符串编码，本命令只支持单聊
    std::string foldername;   //根目录名（只有名字、不含路径），接收端据此在选定位置建根目录
    int64_t     totaldirs;    //目录总数，含空目录、不含根目录本身；单位：个
    int64_t     totalfiles;   //文件总数；单位：个
    int64_t     totalsize;    //全部文件字节数合计；单位：字节，64 位（可超过 4 GiB）

    FolderInviteRequest()
        : peerid(0), totaldirs(0), totalfiles(0), totalsize(0)
    {
    }
};

//cmd_folder_close(1814) 请求：任一端结束本次传输，成功与失败共用本结构。
//失败时 errcode 会被服务端原样透传给对端的 cmd_folder_state_notify，保证两端
//看到的失败原因一致。
struct FolderCloseRequest
{
    std::string sessionid;   //本次传输的会话 id，由 cmd_folder_invite 的应答下发
    int         result;      //结束结果，取值见 enum folder_close_result
    int         errcode;     //失败原因，取值见 Protocol.h 的 error_code_folder_*（606~620）；成功结束时为 0
    std::string errmsg;      //面向开发者的英文诊断串，可为空；客户端按 errcode 本地化文案，不直接展示本串

    FolderCloseRequest()
        : result(folder_close_success), errcode(0)
    {
    }
};

/**
 *  一次文件夹传输结束后留在双方单聊会话里的那条聊天记录的内容
 *  （msgtype 为 message_type_folder=18 的 content 对象）。
 *
 *  服务端只记"发生过这次文件夹传输"这个动作本身：会话 id、根目录名、三项规模统计、
 *  以及本次传输的结局。<u>不记</u>文件清单、相对路径、MD5 与文件内容 —— 字节仍旧只在
 *  内存里转发、不落盘不入库，故历史消息里也没有"重新下载"。
 *
 *  这条记录由服务端生成，不由客户端发送：邀请超时是服务端定时器判定的、对方离线是
 *  服务端发现的，这两种结局下发送方客户端可能根本不在线，让客户端补记必然漏。
 */
struct FolderTransferRecord
{
    std::string sessionid;    //本次传输的会话 id，与 1810~1817 那组信令里的同名字段一致
    std::string foldername;   //根目录名（只有名字、不含路径），UTF-8
    int64_t     totaldirs;    //目录数，单位：个；含空目录、不含根目录本身
    int64_t     totalfiles;   //文件数，单位：个
    int64_t     totalsize;    //全部文件的字节数之和，单位：字节，64 位（可超过 4 GiB）
    int         result;       //本次传输的结局，取 enum folder_transfer_state 的终态值
                              //（3 已拒绝 / 4 应答前撤回 / 5 对方离线 / 6 邀请超时 / 7 失败 / 8 完成）
    int         errcode;      //失败原因，取 Protocol.h 的 error_code_folder_*（606~620）；
                              //仅 result 为 folder_state_failed(7) 时有意义，其余结局恒为 0

    FolderTransferRecord()
        : totaldirs(0), totalfiles(0), totalsize(0)
        , result(0), errcode(0)
    {
    }
};

//单聊文件夹传输信令编解码工具类；全部为静态方法，不可实例化。
//解析类方法对任何输入都只返回 true / false，不抛异常：JSON 非法、字段缺失、字段类型
//不对、取值越界一律返回 false，且此时出参内容未定义，调用方不得取用。
class FolderTransferProtocol
{
public:
    //==== 解析 客户端→服务端 的请求 ====

    /**
     *  解析发起文件夹传输邀请的请求（cmd_folder_invite=1810）。
     *  @param json 请求包体（UTF-8 JSON）
     *  @param req  [出参] 解析结果；返回 false 时其内容未定义
     *  @return peerid 为正、foldername 非空、三项规模统计均为非负整数时返回 true，
     *          否则 false。三项统计允许同时为 0（发送一个空目录是合法的）
     */
    static bool parseInviteRequest(const std::string& json, FolderInviteRequest& req);

    /**
     *  解析接收端应答邀请的请求（cmd_folder_response=1812）。
     *  @param json      请求包体（UTF-8 JSON）
     *  @param sessionid [出参] 会话 id
     *  @param accept    [出参] 应答结果，取值见 enum folder_response_type
     *  @return sessionid 非空且 accept 恰为 0 或 1 时返回 true，否则 false
     */
    static bool parseResponseRequest(const std::string& json, std::string& sessionid, int& accept);

    /**
     *  解析仅带会话 id 的请求（cmd_folder_cancel=1813 与 cmd_folder_retry=1817 共用）。
     *  两条命令包体结构相同，区别只在服务端的处理：前者撤回尚在响铃中的邀请，后者在
     *  保留期内重试续传。
     *  @param json      请求包体（UTF-8 JSON）
     *  @param sessionid [出参] 会话 id
     *  @return sessionid 非空返回 true，否则 false
     */
    static bool parseSessionRequest(const std::string& json, std::string& sessionid);

    /**
     *  解析结束本次传输的请求（cmd_folder_close=1814）。
     *  @param json 请求包体（UTF-8 JSON）
     *  @param req  [出参] 解析结果；返回 false 时其内容未定义
     *  @return sessionid 非空、result 为 0 或 1、且 errcode 与 result 相符时返回 true：
     *          result 为失败时 errcode 须落在 606~620 之内，result 为成功时 errcode 只能
     *          缺省或为 0。errmsg 可缺省，存在时必须是字符串
     */
    static bool parseCloseRequest(const std::string& json, FolderCloseRequest& req);

    //==== 构造 服务端→客户端 的应答与推送 ====

    /**
     *  构造发起邀请的应答（cmd_folder_invite 的应答）。
     *  成功（code 为 0 且 sessionid 非空）时写入 sessionid 与初始状态，失败时只写 code / msg。
     *  规模超限（609 / 610）那两种失败要回带上限值，走 buildInviteLimitResponse，不走本函数。
     *  @param code      结果码，0 成功，非 0 见 Protocol.h 的 enum error_code
     *  @param msg       结果描述
     *  @param sessionid 服务端为本次传输生成的会话 id（仅成功时下发）
     *  @param state     建会话后的初始状态，按协议恒为 folder_state_ringing（响铃中）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildInviteResponse(int code, const std::string& msg,
                                           const std::string& sessionid, int state);

    /**
     *  构造规模超限时的邀请应答（cmd_folder_invite 的应答，code 为
     *  error_code_folder_too_many_files 或 error_code_folder_too_large）。
     *  客户端一律用应答里带回的上限值做提示，不得硬编码——上限是服务端配置项，随部署环境变化。
     *  @param code     结果码，609 文件数超限 / 610 总字节数超限
     *  @param msg      结果描述
     *  @param maxfiles 服务端配置的文件数上限（配置项 foldermaxfiles）；单位：个
     *  @param maxsize  服务端配置的总字节数上限（配置项 foldermaxsize）；单位：字节
     *  @return 紧凑 JSON 文本
     */
    static std::string buildInviteLimitResponse(int code, const std::string& msg,
                                                int64_t maxfiles, int64_t maxsize);

    /**
     *  构造邀请推送（cmd_folder_invite_notify=1811），发给接收端。
     *  @param sessionid 会话 id
     *  @param fromid    发送端 userid（以十进制字符串编码写入，避免客户端丢精度）
     *  @param fromname  发送端昵称（UTF-8），供接收端气泡显示"谁要发给你"
     *  @param req       发送端 1810 请求的解析结果；目录名与三项规模统计由此原样透传，
     *                   其中的 peerid 字段不写入推送（接收端就是自己，无须回带）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildInviteNotify(const std::string& sessionid, int64_t fromid,
                                         const std::string& fromname,
                                         const FolderInviteRequest& req);

    /**
     *  构造只带会话 id 的通用应答（cmd_folder_response / cmd_folder_cancel /
     *  cmd_folder_close / cmd_folder_retry 四条命令的应答共用）。
     *  @param code      结果码，0 成功，非 0 见 Protocol.h 的 enum error_code
     *  @param msg       结果描述
     *  @param sessionid 请求里那个会话 id；非空即原样带回（含出错应答），供客户端定位是哪一次传输
     *  @return 紧凑 JSON 文本
     */
    static std::string buildSessionResponse(int code, const std::string& msg,
                                            const std::string& sessionid);

    /**
     *  构造状态推送（cmd_folder_state_notify=1815）。
     *  协议规定只有"已失败"态才带有意义的失败原因，其余状态 errcode 恒为 0，故本函数在
     *  state 不是 folder_state_failed 时一律写入 errcode 0 与空 errmsg，忽略传入值——
     *  这条不变式集中在此处保证，各调用点不必各写一遍（见 docs/协议设计-v2.md 12.2.6）。
     *  @param sessionid 会话 id
     *  @param state     会话状态，取值见 Protocol.h 的 enum folder_transfer_state
     *  @param errcode   失败原因，取值见 error_code_folder_*；仅 state 为 folder_state_failed 时写入
     *  @param errmsg    英文诊断串，可为空；仅 state 为 folder_state_failed 时写入
     *  @return 紧凑 JSON 文本
     */
    static std::string buildStateNotify(const std::string& sessionid, int state,
                                        int errcode, const std::string& errmsg);

    /**
     *  构造中转参数下发推送（cmd_folder_relay_info=1816）。接收端同意（1812）或重试成功
     *  （1817）后，向两端各推一帧：sessionid 与 lanes 相同，token 与 role 各不相同。
     *  地址字段名沿用远程桌面 cmd_rd_relay_info 与音视频通话的中转参数推送（relayip /
     *  relayport），客户端已有的解析代码因而可以复用。
     *  @param sessionid 会话 id
     *  @param role      本端角色：RelayToken::role_folder_sender(5) 或 role_folder_receiver(6)
     *  @param relayip   fileserver 的中转地址（点分十进制），即上传下载用的那个监听地址
     *  @param relayport fileserver 的监听端口，本功能不新开端口
     *  @param token     本端的中转令牌，本端 N+1 条连接共用同一枚（见 utils/RelayToken.h）
     *  @param lanes     数据通道条数 N，不含 lane 0 那条控制通道；客户端据此共建 N+1 条连接
     *  @return 紧凑 JSON 文本
     */
    static std::string buildRelayInfo(const std::string& sessionid, int role,
                                      const std::string& relayip, int relayport,
                                      const std::string& token, int lanes);

    //==== 传输结束后留在聊天记录里的那条记录条（message_type_folder=18）====

    /**
     *  构造文件夹传输记录条的 content JSON（msgtype=message_type_folder=18）。
     *
     *  与 buildStateNotify 同样在此集中保证一条不变式：只有 result 为
     *  folder_state_failed(7) 时才写入有意义的 errcode，其余结局一律写 0，忽略传入值。
     *  这样各调用点不必各写一遍"非失败态要把 errcode 清零"。
     *
     *  @param rec 待写入的记录内容；其 result 应当是一个终态值（3~8），
     *             非终态值本函数照写不误，由调用方保证（会话层只在终态处调用）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildRecordContent(const FolderTransferRecord& rec);

    /**
     *  解析文件夹传输记录条的 content JSON。供客户端渲染与单元测试使用。
     *  @param json 记录条的 content 对象（UTF-8 JSON）
     *  @param rec  [出参] 解析结果；返回 false 时其内容未定义
     *  @return sessionid 与 foldername 非空、三项规模统计为非负整数、result 为
     *          folder_state_rejected(3) ~ folder_state_completed(8) 之间的终态值、
     *          且 errcode 与 result 相符（result 为 7 时须落在 606~620 之内，其余
     *          结局只能缺省或为 0）时返回 true，否则 false
     */
    static bool parseRecordContent(const std::string& json, FolderTransferRecord& rec);

    /**
     *  由会话 id 与尝试序号派生这条记录条的 clientmsgid
     *  （形如 "folder-<sessionid>-<attemptSeq>"）。
     *
     *  取一个由这两者决定的固定串而不是随机 UUID，是为了在两个层面上各自成立：
     *    - <u>同一次尝试之内幂等</u>：一次尝试走到终态的判定可能被触发多次（典型是双方
     *      各发一帧 cmd_folder_close，先到的那帧结束会话、后到的那帧按幂等处理），而
     *      落库前的去重查询以「发送方 userid + clientmsgid」为键
     *      （MessageManager::getMsgIdByClientId），键固定即保证只留一条记录。
     *    - <u>不同尝试之间互不去重</u>：序号进了键，故一笔传输先失败（序号 1）、在保留期内
     *      重试又成功（序号 2）时会留下两条记录，聊天记录里能看到完整的过程，而不是被
     *      第一条失败记录挡住。
     *
     *  前缀 "folder-" 使它不会与客户端生成的 UUID 型 clientmsgid 相撞。
     *
     *  @param sessionid  本次传输的会话 id
     *  @param attemptSeq 本次尝试的序号，取自 FolderTransferSessionInfo::attemptSeq；
     *                    单位：次，从 1 起算，每受理一次 cmd_folder_retry 加一
     *  @return 拼好的 clientmsgid
     */
    static std::string buildRecordClientMsgId(const std::string& sessionid, int attemptSeq);

    //==== 取值校验（供会话层复用，避免各处各写一份范围判断）====

    /**
     *  判定一个会话状态取值是否为"终态"——会话走到这里即结束，双方界面转入最终形态，
     *  也正是要留下一条聊天记录的那些取值。
     *  @param state 待判定的状态值
     *  @return 落在 folder_state_rejected(3) ~ folder_state_completed(8) 之内返回 true；
     *          响铃中(1) 与传输中(2) 是过程态，返回 false
     */
    static bool isTerminalState(int state);

    /**
     *  判定一个会话状态取值是否合法。
     *  @param state 待判定的状态值
     *  @return 落在 folder_state_ringing(1) ~ folder_state_completed(8) 之内返回 true。
     *          该枚举取值连续，故按上下界判定即可
     */
    static bool isValidState(int state);

    /**
     *  判定一个失败原因错误码是否属于文件夹传输这一组。
     *  @param errcode 待判定的错误码
     *  @return 落在 error_code_folder_rejected(606) ~ error_code_folder_busy(620) 之内
     *          返回 true。该组错误码取值连续，故按上下界判定即可
     */
    static bool isValidErrorCode(int errcode);

private:
    FolderTransferProtocol() = delete;
    ~FolderTransferProtocol() = delete;
    FolderTransferProtocol(const FolderTransferProtocol&) = delete;
    FolderTransferProtocol& operator=(const FolderTransferProtocol&) = delete;
};
