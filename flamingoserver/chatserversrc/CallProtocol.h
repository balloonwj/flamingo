/**
 *  v2 音视频通话信令编解码（服务端侧），CallProtocol.h
 *
 *  与客户端 flamingoclient/Source/net/CallProtocolClient 互为镜像：服务端解析
 *  客户端发来的请求（邀请 / 应答 / 取消 / 挂断），构造回给客户端的应答与推送
 *  （邀请应答 / 邀请推送 / 中转参数下发 / 状态推送）。整个通话由一个 sessionid
 *  （字符串）贯穿；64 位 userid 一律以字符串编码进 JSON，避免大整数精度丢失。
 *
 *  与远程桌面信令（见 RemoteDesktopProtocol）同构，差异仅两处：邀请请求 / 推送带
 *  media 字段（区分纯语音 / 音视频），中转参数经 cmd_call_signaling 下发（音视频
 *  号段未单设 relay_info）。转发约定：被叫拒绝、主叫取消、任一端挂断、超时、对方
 *  离线 / 忙，服务端一律以 cmd_call_state_notify 推送给对端（state 取不同值），故
 *  服务端只需 buildStateNotify 一种“结束类”推送，cmd_call_cancel / cmd_call_hangup
 *  仅作 客户端→服务端 方向。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp，不触及网络与数据库，便于单元测试，见
 *  tests/test_callprotocol.cpp。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <stdint.h>
#include <string>

//通话状态，随 cmd_call_state_notify 推送给端。与客户端 net::call_state 取值一致。
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

//通话媒体类型，位于 cmd_call_invite 的 media 字段。与客户端 net::call_media 取值一致。
enum call_media
{
    call_media_audio = 1,   //纯语音通话（第一、二期）
    call_media_video = 2,   //音视频通话（第三期接入视频后使用）
};

//音视频通话信令编解码工具类；全部为静态方法，不可实例化。
class CallProtocol
{
public:
    //==== 解析 客户端→服务端 的请求 ====

    /**
     *  解析发起通话邀请请求（cmd_call_invite=1701）。
     *  @param json   请求包体（UTF-8 JSON）
     *  @param peerid [出参] 被叫 userid（由字符串编码还原）
     *  @param media  [出参] 媒体类型，取值见 enum call_media；字段缺失时按 call_media_audio
     *  @return JSON 合法且 peerid>0 返回 true，否则 false
     */
    static bool parseInviteRequest(const std::string& json, int64_t& peerid, int& media);

    /**
     *  解析被叫应答请求（cmd_call_response=1703）。
     *  @param json      请求包体（UTF-8 JSON）
     *  @param sessionid [出参] 会话 id
     *  @param accept    [出参] 1 接听 / 0 拒绝；字段缺失按 0
     *  @return JSON 合法且 sessionid 非空返回 true，否则 false
     */
    static bool parseResponseRequest(const std::string& json, std::string& sessionid, int& accept);

    /**
     *  解析仅带会话 id 的请求（cmd_call_cancel=1704 / cmd_call_hangup=1705 共用）。
     *  @param json      请求包体（UTF-8 JSON）
     *  @param sessionid [出参] 会话 id
     *  @return JSON 合法且 sessionid 非空返回 true，否则 false
     */
    static bool parseSessionRequest(const std::string& json, std::string& sessionid);

    //==== 构造 服务端→客户端 的应答与推送 ====

    /**
     *  构造发起邀请的应答（cmd_call_invite 的应答）。
     *  成功（code==0 且 sessionid 非空）时写入 sessionid，失败时仅 code/msg。
     *  @param code      结果码，0 成功，非 0 见 enum error_code（如对方离线 / 忙）
     *  @param msg       结果描述
     *  @param sessionid 本次通话会话 id（仅成功时下发）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildInviteResponse(int code, const std::string& msg,
                                           const std::string& sessionid);

    /**
     *  构造邀请推送（cmd_call_invite_notify=1702），发给被叫。
     *  @param sessionid 会话 id
     *  @param fromid    主叫 userid（以字符串编码写入，避免精度丢失）
     *  @param fromname  主叫昵称（UTF-8，用于来电界面文案展示）
     *  @param media     媒体类型，取值见 enum call_media
     *  @return 紧凑 JSON 文本
     */
    static std::string buildInviteNotify(const std::string& sessionid, int64_t fromid,
                                         const std::string& fromname, int media);

    /**
     *  构造中转参数下发推送（cmd_call_signaling=1707），接通后发给双方各一份。
     *  本端据此连上 imgserver 中转端口、用 token 报到、按 role 收发音视频帧。
     *  @param sessionid 会话 id
     *  @param role      本端角色（RelayToken::role_caller / role_callee）
     *  @param relayip   imgserver 中转地址（点分十进制）
     *  @param relayport imgserver 中转端口
     *  @param token     relay 令牌
     *  @return 紧凑 JSON 文本
     */
    static std::string buildSignaling(const std::string& sessionid, int role,
                                      const std::string& relayip, int relayport,
                                      const std::string& token);

    /**
     *  构造状态推送（cmd_call_state_notify=1706）。
     *  @param sessionid 会话 id
     *  @param state     状态码，见 enum call_state
     *  @param msg       可读描述（可空），用于 UI 直接展示
     *  @return 紧凑 JSON 文本
     */
    static std::string buildStateNotify(const std::string& sessionid, int state,
                                        const std::string& msg);

    /**
     *  解析 cmd_call_switch_media 请求：{ sessionid, media }。
     *  @param json      请求包体
     *  @param sessionid [出参] 会话 id；缺失或为空返回 false
     *  @param media     [出参] 目标媒体类型，取值见 enum call_media；非法值返回 false
     *  @return 解析成功返回 true
     */
    static bool parseSwitchMediaRequest(const std::string& json, std::string& sessionid, int& media);

    /**
     *  构造 cmd_call_media_changed_notify 推送 JSON：{ sessionid, media, operator }。
     *  双方（含发起者本人）各推一份，收方据 operator 是否等于自己决定要不要自动开摄像头。
     *  @param sessionid  会话 id
     *  @param media      变更后的媒体类型，取值见 enum call_media
     *  @param operatorId 发起本次切换的用户 userid
     */
    static std::string buildMediaChangedNotify(const std::string& sessionid, int media,
                                               int64_t operatorId);

private:
    CallProtocol() = delete;
    ~CallProtocol() = delete;
    CallProtocol(const CallProtocol&) = delete;
    CallProtocol& operator=(const CallProtocol&) = delete;
};
