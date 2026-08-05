/**
 *  v2 音视频通话信令编解码（客户端侧），CallProtocolClient.h
 *
 *  构造发往 chatserver 的通话请求 JSON（邀请 / 应答 / 取消 / 挂断）、解析 chatserver
 *  回来的应答与推送 JSON（邀请应答 / 邀请推送 / 中转参数下发 / 状态推送），以及构造
 *  发往 imgserver 中转通道的握手帧、解析其握手应答。
 *
 *  本模块与服务端 chatserversrc/CallProtocol 互为镜像——服务端解析请求 / 构造应答
 *  与推送，客户端构造请求 / 解析应答与推送。整个通话由一个 sessionid（字符串）贯穿；
 *  64 位 userid 一律以字符串编码进 JSON，避免大整数精度丢失。本模块为纯逻辑，只依赖
 *  jsoncpp，不触及网络与 UI，便于单元测试，见 flamingoclient/Source/Tests/
 *  CallProtocolTests.cpp。
 *
 *  覆盖第一期“纯语音通话信令 + 中转骨架”所需的全部信令：
 *    - chatserver：邀请(1701) / 邀请推送(1702) / 应答(1703) / 取消(1704) /
 *                  挂断(1705) / 状态推送(1706) / 中转参数下发(1707)
 *    - imgserver ：中转握手(1855) 及其应答
 *  中转数据帧(1856)承载的音视频帧为不透明二进制，由媒体层定义其内部格式、imgserver
 *  不解析，故不在本协议模块内编解码。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//通话媒体类型，位于 cmd_call_invite 的 media 字段。与服务端 call_media 取值一致。
enum call_media
{
    call_media_audio = 1,   //纯语音通话（第一、二期）
    call_media_video = 2,   //音视频通话（第三期接入视频后使用）
};

//通话中本端扮演的角色。随 cmd_call_signaling 下发，客户端据此决定 UI 与媒体方向。
//与服务端 RelayToken::role_caller / role_callee 取值一致（音视频角色续接远程桌面的
//控制端 / 被控端编号，四种角色全局不重复）。
enum call_role
{
    call_role_caller = 3,   //主叫：发起通话的一方
    call_role_callee = 4,   //被叫：被邀请的一方
};

//通话状态，随 cmd_call_state_notify 推送给端，用于驱动 UI 提示与收尾。
//与服务端 call_state 取值一致。
enum call_state
{
    call_state_ringing      = 1,   //响铃中：邀请已送达被叫，正等待其接听 / 拒绝
    call_state_rejected     = 2,   //已拒绝：被叫拒绝了本次通话
    call_state_canceled     = 3,   //已取消：主叫在被叫应答前撤回了呼叫
    call_state_peer_offline = 4,   //对方离线：被叫当前不在线，无法建立通话
    call_state_timeout      = 5,   //超时：呼叫在限定时间内无人应答，自动结束
    call_state_connected    = 6,   //已接通：被叫接听，双方进入通话、各自去连中转通道
    call_state_ended        = 7,   //已结束：通话结束（任一端挂断或连接中断）
    call_state_busy         = 8,   //对方忙：被叫正在另一通话中，无法接入本次呼叫
};

//邀请应答（cmd_call_invite=1701 的应答）解析结果。
//成功（m_code==0）时 m_sessionId 有效，主叫据此与后续中转参数对应起来；
//失败时仅 m_code / m_msg 有效（如对方离线、对方忙）。
struct CallInviteResponse
{
    int         m_code;        //结果码，0 成功，非 0 见 enum error_code（如对方离线 / 忙）
    std::string m_msg;         //结果描述（成功为 "ok"，失败为诊断信息）
    std::string m_sessionId;   //本次通话会话 id（仅 m_code==0 时有效）

    CallInviteResponse()
        : m_code(-1)           //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//收到的通话邀请（cmd_call_invite_notify=1702 推送）解析结果。被叫据此弹出来电界面，
//显示是谁来电、是语音还是音视频。
struct CallInviteNotify
{
    std::string m_sessionId;   //本次会话 id；应答 / 取消 / 挂断时须原样带回
    int64_t     m_fromId;      //主叫 userid（发起通话的一方）
    std::string m_fromName;    //主叫昵称（UTF-8，用于来电界面文案展示）
    int         m_media;       //媒体类型，取值见 enum call_media

    CallInviteNotify()
        : m_fromId(0)
        , m_media(call_media_audio)
    {
    }
};

//中转参数（cmd_call_signaling=1707 推送）解析结果。接通后 chatserver 给双方各下发一份：
//本端据此连上 imgserver 中转端口、用 m_token 报到、按 m_role 收发音视频帧。
struct CallSignalingInfo
{
    std::string m_sessionId;   //本次会话 id
    int         m_role;        //本端角色，取值见 enum call_role
    std::string m_relayIp;     //imgserver 中转通道的连接地址（点分十进制）
    int         m_relayPort;   //imgserver 中转通道的端口
    std::string m_token;       //relay 令牌，连上中转后作为握手帧 body 提交校验

    CallSignalingInfo()
        : m_role(0)
        , m_relayPort(0)
    {
    }
};

//通话状态推送（cmd_call_state_notify=1706）解析结果。
struct CallStateNotify
{
    std::string m_sessionId;   //本次会话 id
    int         m_state;       //会话状态，取值见 enum call_state
    std::string m_msg;         //可读描述（可空），用于 UI 直接展示

    CallStateNotify()
        : m_state(0)
    {
    }
};

//中转握手应答（cmd_av_relay_hello=1855 的应答）解析结果。
//m_code==0 表示 imgserver 已登记本端、配对成功（或等待对端到达）；非 0 表示
//令牌非法 / 过期 / 会话不存在。
//切换通话媒体类型（cmd_call_switch_media=1708）的应答解析结果。
//m_code==0 表示服务端已更新会话媒体类型并向双方推送了 cmd_call_media_changed_notify；
//非 0 时按 enum error_code 区分原因，调用方据此提示（见 Protocol.h）：
//  error_code_call_not_exist(705)     会话不存在（通话已结束 / sessionid 无效）
//  error_code_call_not_member(706)    请求者不是本次通话双方之一
//  error_code_call_state_invalid(707) 通话未接通，此时不允许切换
struct CallSwitchMediaResponse
{
    int         m_code;   //结果码，0 成功；非 0 见 enum error_code
    std::string m_msg;    //结果描述（可空）

    CallSwitchMediaResponse()
        : m_code(-1)      //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//通话媒体类型已变更的推送（cmd_call_media_changed_notify=1709）解析结果。
//双方（含发起切换者本人）都会收到，故调用方须据 m_operator 是否等于自己来决定
//本端是否自动打开摄像头 —— 按已定交互模型：发起方自动开，另一方默认不开。
struct CallMediaChangedNotify
{
    std::string m_sessionId;   //本次会话 id
    int         m_media;       //变更后的媒体类型，取值见 enum call_media
    int64_t     m_operator;    //发起本次切换的用户 userid

    CallMediaChangedNotify()
        : m_media(0)
        , m_operator(0)
    {
    }
};

struct CallRelayHelloAck
{
    int         m_code;        //结果码，0 成功，非 0 表示握手失败
    std::string m_msg;         //结果描述

    CallRelayHelloAck()
        : m_code(-1)
    {
    }
};

//音视频通话信令编解码工具类；全部为静态方法，不可实例化。
class CallProtocolClient
{
public:
    //==== 主叫 / 被叫 → chatserver 的请求构造 ====

    /**
     *  构造发起通话邀请的请求 JSON 包体（cmd_call_invite=1701，紧凑 UTF-8）。
     *  @param peerUserId 被叫用户 userid（>0）；作为 JSON 的 peerid 字段以字符串编码
     *  @param media      媒体类型，取值见 enum call_media（第一期固定 call_media_audio）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildInviteRequest(int64_t peerUserId, int media);

    /**
     *  构造被叫对邀请的应答请求 JSON 包体（cmd_call_response=1703，紧凑 UTF-8）。
     *  @param sessionId 邀请推送里带来的会话 id（非空）；原样回带
     *  @param accept    true 接听、false 拒绝；写入 accept 字段（1 / 0）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildResponse(const std::string& sessionId, bool accept);

    /**
     *  构造主叫取消呼叫的请求 JSON 包体（cmd_call_cancel=1704，紧凑 UTF-8）。
     *  用于被叫尚未应答前撤回呼叫。
     *  @param sessionId 本次会话 id（非空）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildCancel(const std::string& sessionId);

    /**
     *  构造挂断通话的请求 JSON 包体（cmd_call_hangup=1705，紧凑 UTF-8）。
     *  主叫与被叫均可发，用于接通后挂断 / 关闭通话窗。
     *  @param sessionId 本次会话 id（非空）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildHangup(const std::string& sessionId);

    //==== 客户端 → imgserver 中转通道的握手构造 ====

    /**
     *  构造连上 imgserver 中转通道后的握手帧 JSON 包体（cmd_av_relay_hello=1855，
     *  紧凑 UTF-8）。
     *  @param token relay 令牌（来自 cmd_call_signaling，非空）
     *  @return 紧凑格式的握手 JSON 文本
     */
    static std::string buildRelayHello(const std::string& token);

    //==== chatserver / imgserver → 客户端的应答与推送解析 ====

    /**
     *  解析发起邀请的应答 JSON 包体（cmd_call_invite=1701 的应答）。
     *  只要 JSON 合法且含整型 code 字段即返回 true；JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时 m_code/m_msg 有效，m_code==0 时 m_sessionId 亦有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseInviteResponse(const std::string& jsonbody, CallInviteResponse& out);

    /**
     *  解析收到的通话邀请推送 JSON 包体（cmd_call_invite_notify=1702）。
     *  缺少 sessionid / fromid 或 JSON 非法时返回 false。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 解析成功返回 true，否则 false
     */
    static bool parseInviteNotify(const std::string& jsonbody, CallInviteNotify& out);

    /**
     *  解析中转参数下发推送 JSON 包体（cmd_call_signaling=1707）。
     *  缺少 sessionid / token 或 JSON 非法时返回 false。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 解析成功返回 true，否则 false
     */
    static bool parseSignaling(const std::string& jsonbody, CallSignalingInfo& out);

    /**
     *  解析通话状态推送 JSON 包体（cmd_call_state_notify=1706）。
     *  缺少 sessionid 或 JSON 非法时返回 false。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 解析成功返回 true，否则 false
     */
    static bool parseStateNotify(const std::string& jsonbody, CallStateNotify& out);

    /**
     *  解析中转握手应答 JSON 包体（cmd_av_relay_hello=1855 的应答）。
     *  只要 JSON 合法且含整型 code 字段即返回 true；JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时 m_code/m_msg 有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseRelayHelloAck(const std::string& jsonbody, CallRelayHelloAck& out);

    /**
     *  构造"切换通话媒体类型"请求 JSON 包体（cmd_call_switch_media=1708）。
     *  仅通话已接通时可用；服务端会再校验一次状态（客户端与服务端两侧设防）。
     *  @param sessionId 本次会话 id；为空时返回空串（调用方不应发送）
     *  @param media     目标媒体类型，取值见 enum call_media；非法值返回空串
     *  @return 紧凑格式的请求 JSON 文本；入参非法时返回空串
     */
    static std::string buildSwitchMedia(const std::string& sessionId, int media);

    /**
     *  解析切换媒体类型的应答（cmd_call_switch_media 的应答）。
     *  只要 JSON 合法且含整型 code 即返回 true，调用方据 out.m_code 判定结果与提示文案。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 时 m_code / m_msg 有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseSwitchMediaResponse(const std::string& jsonbody, CallSwitchMediaResponse& out);

    /**
     *  解析"通话媒体类型已变更"的推送（cmd_call_media_changed_notify=1709）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 时各字段有效
     *  @return 解析成功返回 true；JSON 非法、sessionid 为空、或 media 非法时返回 false
     *          （此时 out 内容未定义，调用方不得使用）
     */
    static bool parseMediaChangedNotify(const std::string& jsonbody, CallMediaChangedNotify& out);

private:
    CallProtocolClient() = delete;
    ~CallProtocolClient() = delete;
    CallProtocolClient(const CallProtocolClient&) = delete;
    CallProtocolClient& operator=(const CallProtocolClient&) = delete;
};

} // namespace net
