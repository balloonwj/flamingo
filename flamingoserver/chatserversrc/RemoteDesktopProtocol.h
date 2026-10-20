/**
 *  v2 远程桌面信令编解码（服务端侧），RemoteDesktopProtocol.h
 *
 *  与客户端 flamingoclient/Source/net/RemoteDesktopProtocolClient 互为镜像：
 *  服务端解析客户端发来的请求（邀请 / 应答 / 取消 / 结束），构造回给客户端的
 *  应答与推送（邀请应答 / 邀请推送 / 中转参数下发 / 状态推送）。整个会话由一个
 *  sessionid（字符串）贯穿；64 位 userid 一律以字符串编码进 JSON，避免精度丢失。
 *
 *  转发约定：被控端拒绝、控制端取消、任一端结束，服务端一律以 cmd_rd_state_notify
 *  推送给对端（state 取不同值），故服务端只需 buildStateNotify 一种"结束类"推送，
 *  cmd_rd_cancel / cmd_rd_close 仅作 客户端→服务端 方向。
 *
 *  控制权限（2026-10-12）：接通后默认只能查看。控制动作经 cmd_rd_control（1718）上报，
 *  服务端按会话表里的控制状态校验后回应答，并经 cmd_rd_control_notify（1719）转给另一方。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp，不触及网络与数据库，便于单元测试，见
 *  tests/test_remotedesktopprotocol.cpp。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>

//远程桌面会话状态，随 cmd_rd_state_notify 推送给端。与客户端 net::rd_state 取值一致。
enum rd_state
{
    rd_state_ringing      = 1,   //响铃中：邀请已送达被控端，正等待其接受 / 拒绝
    rd_state_rejected     = 2,   //已拒绝：被控端拒绝了本次邀请
    rd_state_canceled     = 3,   //已取消：控制端在被控端应答前撤回了邀请
    rd_state_peer_offline = 4,   //对方离线：被控端当前不在线，无法建立会话
    rd_state_timeout      = 5,   //超时：邀请在限定时间内无人应答，自动结束
    rd_state_disconnected = 6,   //已断开：会话已结束（任一端结束或连接中断）
};

//远程桌面控制动作，cmd_rd_control（1718）的 action 字段，也随 1719 推送转给对方。与客户端 net::rd_control_action 取值一致。
//谁能做、什么状态下能做见 RemoteDesktopManager::applyControl
enum rd_control_action
{
    rd_control_request = 1,   //申请控制：控制端发起，转给被控端，被控端在右下角提示里同意或拒绝
    rd_control_cancel  = 2,   //撤回申请：控制端在被控端处理之前撤回，转给被控端（被控端关掉申请提示）
    rd_control_grant   = 3,   //授予控制：被控端同意申请，或没人申请时直接允许，转给控制端
    rd_control_deny    = 4,   //拒绝申请：被控端拒绝（含提示超时），转给控制端
    rd_control_revoke  = 5,   //收回控制：被控端在控制进行中收回，转给控制端
    rd_control_release = 6,   //放弃控制：控制端自己结束控制，转给被控端
};

//远程桌面的控制状态，随 1718 应答与 1719 推送的 control 字段下发。与客户端 net::rd_control_state 取值一致
enum rd_control_state
{
    rd_control_none      = 0,   //未授权：控制端只能查看，被控端丢弃收到的键鼠输入；会话接通后的初始状态
    rd_control_requested = 1,   //申请中：控制端已申请，等待被控端同意或拒绝
    rd_control_granted   = 2,   //已授权：被控端注入控制端发来的键鼠输入
};

//远程桌面信令编解码工具类；全部为静态方法，不可实例化。
class RemoteDesktopProtocol
{
public:
    //==== 解析 客户端→服务端 的请求 ====

    /**
     *  解析发起邀请请求（cmd_rd_invite=1710）。
     *  @param json   请求包体（UTF-8 JSON）
     *  @param peerid [出参] 被控端 userid（字符串编码还原）
     *  @return JSON 合法且 peerid>0 返回 true，否则 false
     */
    static bool parseInviteRequest(const std::string& json, int64_t& peerid);

    /**
     *  解析被控端应答请求（cmd_rd_response=1712）。
     *  @param json      请求包体（UTF-8 JSON）
     *  @param sessionid [出参] 会话 id
     *  @param accept    [出参] 1 接受 / 0 拒绝
     *  @return JSON 合法且 sessionid 非空返回 true，否则 false
     */
    static bool parseResponseRequest(const std::string& json, std::string& sessionid, int& accept);

    /**
     *  解析仅带会话 id 的请求（cmd_rd_cancel=1713 / cmd_rd_close=1714 共用）。
     *  @param json      请求包体（UTF-8 JSON）
     *  @param sessionid [出参] 会话 id
     *  @return JSON 合法且 sessionid 非空返回 true，否则 false
     */
    static bool parseSessionRequest(const std::string& json, std::string& sessionid);

    /**
     *  解析控制动作请求（cmd_rd_control=1718）。
     *  @param json      请求包体（UTF-8 JSON），形如 {"sessionid":"…","action":1}
     *  @param sessionid [出参] 会话 id
     *  @param action    [出参] 控制动作，见 enum rd_control_action
     *  @return JSON 合法、sessionid 非空且 action 是 1~6 的整数时返回 true，否则 false
     */
    static bool parseControlRequest(const std::string& json, std::string& sessionid, int& action);

    //==== 构造 服务端→客户端 的应答与推送 ====

    /**
     *  构造发起邀请的应答（cmd_rd_invite 的应答）。
     *  成功（code==0 且 sessionid 非空）时写入 sessionid，失败时仅 code/msg。
     */
    static std::string buildInviteResponse(int code, const std::string& msg,
                                           const std::string& sessionid);

    /**
     *  构造邀请推送（cmd_rd_invite_notify=1711），发给被控端。
     *  @param sessionid 会话 id
     *  @param fromid    控制端 userid（字符串编码写入）
     *  @param fromname  控制端昵称（UTF-8）
     */
    static std::string buildInviteNotify(const std::string& sessionid, int64_t fromid,
                                         const std::string& fromname);

    /**
     *  构造中转参数下发推送（cmd_rd_relay_info=1716），授权通过后发给双方各一份。
     *  @param sessionid 会话 id
     *  @param role      本端角色（见 RelayToken::role_*）
     *  @param relayip   imgserver 中转地址
     *  @param relayport imgserver 中转端口
     *  @param token     relay 令牌
     *  @param p2pSessionId 服务端为这次远程桌面建的 P2P 会话号（P2P 方案第 6 期）；为空时不写 p2psessionid 字段，
     *                   两端照旧只走 imgserver 中转
     */
    static std::string buildRelayInfo(const std::string& sessionid, int role,
                                      const std::string& relayip, int relayport,
                                      const std::string& token,
                                      const std::string& p2pSessionId = std::string());

    /**
     *  构造状态推送（cmd_rd_state_notify=1715）。
     *  @param sessionid 会话 id
     *  @param state     状态码，见 enum rd_state
     *  @param msg       可读描述（可空）
     */
    static std::string buildStateNotify(const std::string& sessionid, int state,
                                        const std::string& msg);

    /**
     *  构造控制动作的应答（cmd_rd_control 的应答），失败时同样带上 sessionid 与 action，客户端据此找到对应会话。
     *  @param code      错误码，见 enum error_code；0 表示成功
     *  @param msg       可读描述
     *  @param sessionid 会话 id（解析失败时为空）
     *  @param action    请求里的控制动作，见 enum rd_control_action（解析失败时为 0）
     *  @param control   动作之后的控制状态，见 enum rd_control_state；失败时为服务端当前记录的状态
     */
    static std::string buildControlResponse(int code, const std::string& msg, const std::string& sessionid,
                                            int action, int control);

    /**
     *  构造控制动作推送（cmd_rd_control_notify=1719），发给会话的另一方。
     *  @param sessionid 会话 id
     *  @param action    对方做的控制动作，见 enum rd_control_action
     *  @param control   动作之后的控制状态，见 enum rd_control_state
     *  @param fromid    做动作的人的 userid（字符串编码写入）
     *  @param fromname  做动作的人的昵称（UTF-8），被控端的申请提示里显示
     */
    static std::string buildControlNotify(const std::string& sessionid, int action, int control, int64_t fromid,
                                          const std::string& fromname);

private:
    RemoteDesktopProtocol() = delete;
    ~RemoteDesktopProtocol() = delete;
    RemoteDesktopProtocol(const RemoteDesktopProtocol&) = delete;
    RemoteDesktopProtocol& operator=(const RemoteDesktopProtocol&) = delete;
};
