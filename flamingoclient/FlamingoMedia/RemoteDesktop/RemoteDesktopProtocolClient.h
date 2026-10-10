/**
 *  v2 远程桌面信令编解码（客户端侧）：构造发往 chatserver 的远程桌面请求
 *  JSON、解析 chatserver 回来的应答 / 推送 JSON，以及构造发往 imgserver
 *  中转通道的握手帧、解析其握手应答。
 *
 *  本模块与服务端 chatserversrc/RemoteDesktopProtocol 互为镜像——服务端解析
 *  请求 / 构造应答与推送，客户端构造请求 / 解析应答与推送。整个远程桌面会话
 *  由一个 sessionid（字符串）贯穿；64 位 userid 一律以字符串编码进 JSON，避免
 *  大整数精度丢失。本模块是纯逻辑，只依赖 jsoncpp，不触及网络与 UI，便于单元
 *  测试，见 flamingoclient/Source/Tests/RemoteDesktopProtocolTests.cpp。
 *
 *  覆盖第一期"看屏 + 鼠标键盘控制"所需的全部信令：
 *    - chatserver：邀请(1710) / 邀请推送(1711) / 应答(1712) / 取消(1713) /
 *                  结束(1714) / 状态推送(1715) / 中转参数下发(1716)
 *    - imgserver ：中转握手(1853) 及其应答
 *  中转数据帧(1854)承载的屏幕帧 / 控制命令为不透明二进制，由引擎层定义其内部
 *  格式、imgserver 不解析，故不在本协议模块内编解码。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//远程桌面会话中本端扮演的角色。随 cmd_rd_relay_info 下发，客户端据此决定连上
//imgserver 中转后是"推屏幕、收命令"还是"收屏幕、发命令"。与服务端取值一致。
enum rd_role
{
    rd_role_controller = 1,   //控制端：观看对方桌面并发出鼠标键盘命令（发起邀请的一方）
    rd_role_controlled = 2,   //被控端：采集本机屏幕推给对方、执行对方的控制命令（被邀请的一方）
};

//远程桌面会话状态，随 cmd_rd_state_notify 推送给端，用于驱动 UI 提示与收尾。
//与服务端取值一致。
enum rd_state
{
    rd_state_ringing      = 1,   //响铃中：邀请已送达被控端，正等待其接受 / 拒绝
    rd_state_rejected     = 2,   //已拒绝：被控端拒绝了本次邀请
    rd_state_canceled     = 3,   //已取消：控制端在被控端应答前撤回了邀请
    rd_state_peer_offline = 4,   //对方离线：被控端当前不在线，无法建立会话
    rd_state_timeout      = 5,   //超时：邀请在限定时间内无人应答，自动结束
    rd_state_disconnected = 6,   //已断开：会话已结束（任一端挂断或连接中断）
};

//邀请应答（cmd_rd_invite=1710 的应答）解析结果。
//成功（m_code==0）时 m_sessionId 有效，控制端据此与后续中转参数对应起来；
//失败时仅 m_code / m_msg 有效（如对方离线、对方忙）。
struct RDInviteResponse
{
    int         m_code;        //结果码，0 成功，非 0 见 enum error_code（如对方离线 / 忙）
    std::string m_msg;         //结果描述（成功为 "ok"，失败为诊断信息）
    std::string m_sessionId;   //本次远程桌面会话 id（仅 m_code==0 时有效）

    RDInviteResponse()
        : m_code(-1)           //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//收到的远程桌面邀请（cmd_rd_invite_notify=1711 推送）解析结果。被控端据此弹出
//授权确认窗，显示是谁要控制本机。
struct RDInviteNotify
{
    std::string m_sessionId;   //本次会话 id；应答 / 取消 / 结束时须原样带回
    int64_t     m_fromId;      //控制端 userid（发起邀请的一方）
    std::string m_fromName;    //控制端昵称（UTF-8，用于授权窗文案展示）

    RDInviteNotify()
        : m_fromId(0)
    {
    }
};

//中转参数（cmd_rd_relay_info=1716 推送）解析结果。授权通过后 chatserver 给双方
//各下发一份：本端据此连上 imgserver 中转端口、用 m_token 报到、按 m_role 收发。
struct RDRelayInfo
{
    std::string m_sessionId;   //本次会话 id
    int         m_role;        //本端角色，取值见 enum rd_role
    std::string m_relayIp;     //imgserver 中转通道的连接地址（点分十进制）
    int         m_relayPort;   //imgserver 中转通道的端口
    std::string m_token;       //relay 令牌，连上中转后作为握手帧 body 提交校验
    std::string m_p2pSessionId;   //服务端为这次远程桌面建的 P2P 会话号（P2P 方案第 6 期，可选）；为空表示只走中转

    RDRelayInfo()
        : m_role(0)
        , m_relayPort(0)
    {
    }
};

//远程桌面状态推送（cmd_rd_state_notify=1715）解析结果。
struct RDStateNotify
{
    std::string m_sessionId;   //本次会话 id
    int         m_state;       //会话状态，取值见 enum rd_state
    std::string m_msg;         //可读描述（可空），用于 UI 直接展示

    RDStateNotify()
        : m_state(0)
    {
    }
};

//中转握手应答（cmd_rd_relay_hello=1853 的应答）解析结果。
//m_code==0 表示 imgserver 已登记本端、配对成功（或等待对端到达）；非 0 表示
//令牌非法 / 过期 / 会话不存在。
struct RDRelayHelloAck
{
    int         m_code;        //结果码，0 成功，非 0 表示握手失败
    std::string m_msg;         //结果描述

    RDRelayHelloAck()
        : m_code(-1)
    {
    }
};

//远程桌面信令编解码工具类；全部为静态方法，不可实例化。
class RemoteDesktopProtocolClient
{
public:
    //==== 控制端 / 被控端 → chatserver 的请求构造 ====

    /**
     *  构造发起远程桌面邀请的请求 JSON 包体（cmd_rd_invite=1710，紧凑 UTF-8）。
     *  @param peerUserId  被控端用户 userid（>0）；作为 JSON 的 peerid 字段以字符串编码
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildInviteRequest(int64_t peerUserId);

    /**
     *  构造被控端对邀请的应答请求 JSON 包体（cmd_rd_response=1712，紧凑 UTF-8）。
     *  @param sessionId 邀请推送里带来的会话 id（非空）；原样回带
     *  @param accept    true 接受、false 拒绝；写入 accept 字段（1 / 0）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildResponse(const std::string& sessionId, bool accept);

    /**
     *  构造控制端取消邀请的请求 JSON 包体（cmd_rd_cancel=1713，紧凑 UTF-8）。
     *  用于被控端尚未应答前撤回邀请。
     *  @param sessionId 本次会话 id（非空）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildCancel(const std::string& sessionId);

    /**
     *  构造结束远程桌面会话的请求 JSON 包体（cmd_rd_close=1714，紧凑 UTF-8）。
     *  控制端与被控端均可发，用于挂断 / 关闭窗口。
     *  @param sessionId 本次会话 id（非空）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildClose(const std::string& sessionId);

    //==== 客户端 → imgserver 中转通道的握手构造 ====

    /**
     *  构造连上 imgserver 中转通道后的握手帧 JSON 包体（cmd_rd_relay_hello=1853，
     *  紧凑 UTF-8）。
     *  @param token relay 令牌（来自 cmd_rd_relay_info，非空）
     *  @return 紧凑格式的握手 JSON 文本
     */
    static std::string buildRelayHello(const std::string& token);

    //==== chatserver / imgserver → 客户端的应答与推送解析 ====

    /**
     *  解析发起邀请的应答 JSON 包体（cmd_rd_invite=1710 的应答）。
     *  只要 JSON 合法且含整型 code 字段即返回 true；JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时 m_code/m_msg 有效，m_code==0 时 m_sessionId 亦有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseInviteResponse(const std::string& jsonbody, RDInviteResponse& out);

    /**
     *  解析收到的远程桌面邀请推送 JSON 包体（cmd_rd_invite_notify=1711）。
     *  缺少 sessionid / fromid 或 JSON 非法时返回 false。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 解析成功返回 true，否则 false
     */
    static bool parseInviteNotify(const std::string& jsonbody, RDInviteNotify& out);

    /**
     *  解析中转参数下发推送 JSON 包体（cmd_rd_relay_info=1716）。
     *  缺少 sessionid / token 或 JSON 非法时返回 false。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 解析成功返回 true，否则 false
     */
    static bool parseRelayInfo(const std::string& jsonbody, RDRelayInfo& out);

    /**
     *  解析远程桌面状态推送 JSON 包体（cmd_rd_state_notify=1715）。
     *  缺少 sessionid 或 JSON 非法时返回 false。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 解析成功返回 true，否则 false
     */
    static bool parseStateNotify(const std::string& jsonbody, RDStateNotify& out);

    /**
     *  解析中转握手应答 JSON 包体（cmd_rd_relay_hello=1853 的应答）。
     *  只要 JSON 合法且含整型 code 字段即返回 true；JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时 m_code/m_msg 有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseRelayHelloAck(const std::string& jsonbody, RDRelayHelloAck& out);

private:
    RemoteDesktopProtocolClient() = delete;
    ~RemoteDesktopProtocolClient() = delete;
    RemoteDesktopProtocolClient(const RemoteDesktopProtocolClient&) = delete;
    RemoteDesktopProtocolClient& operator=(const RemoteDesktopProtocolClient&) = delete;
};

} // namespace net
