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

private:
    RemoteDesktopProtocol() = delete;
    ~RemoteDesktopProtocol() = delete;
    RemoteDesktopProtocol(const RemoteDesktopProtocol&) = delete;
    RemoteDesktopProtocol& operator=(const RemoteDesktopProtocol&) = delete;
};
