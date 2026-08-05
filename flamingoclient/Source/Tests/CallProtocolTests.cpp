/**
 *  v2 音视频通话信令编解码（客户端侧）单元测试（Suite CC），CallProtocolTests.cpp
 *  覆盖：
 *    - 请求构造：buildInviteRequest / buildResponse / buildCancel / buildHangup /
 *      buildRelayHello 的字段完整性、类型、紧凑性、64 位 ID 字符串编码、media 携带；
 *    - 应答 / 推送解析：parseInviteResponse（成功/失败）、parseInviteNotify、
 *      parseSignaling、parseStateNotify、parseRelayHelloAck 的字段还原；
 *    - 64 位 userid 经字符串编码 round-trip 不丢精度；
 *    - 畸形输入（非法 JSON / 缺关键字段）一律安全失败返回 false。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-11
 */
#include "catch_amalgamated.hpp"
#include "../net/CallProtocolClient.h"
#include "../net/Protocol.h"   // error_code_call_*（切换媒体的失败码断言）
//见 MessageProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查构造产物
static bool ccParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的推送/应答报文
static std::string ccWriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

TEST_CASE("CC1 buildInviteRequest 字段完整、紧凑、peerid 字符串编码、media 携带")
{
    std::string json = CallProtocolClient::buildInviteRequest(5002, call_media_audio);

    Json::Value root;
    REQUIRE(ccParseJsonText(json, root));
    //peerid 必须为字符串编码（防大整数精度丢失），值为被叫 userid
    CHECK(root["peerid"].isString());
    CHECK(root["peerid"].asString() == "5002");
    CHECK(root["media"].asInt() == call_media_audio);
    //紧凑输出：不含换行
    CHECK(json.find('\n') == std::string::npos);

    //音视频邀请：media=2
    std::string jv = CallProtocolClient::buildInviteRequest(5002, call_media_video);
    Json::Value rv;
    REQUIRE(ccParseJsonText(jv, rv));
    CHECK(rv["media"].asInt() == call_media_video);
}

TEST_CASE("CC2 parseInviteResponse 成功取 sessionid、失败取 msg")
{
    //成功应答：code=0，带 sessionid
    {
        std::string body =
            "{\"code\":0,\"msg\":\"ok\",\"sessionid\":\"sess-abc-001\"}";
        CallInviteResponse resp;
        REQUIRE(CallProtocolClient::parseInviteResponse(body, resp));
        CHECK(resp.m_code == 0);
        CHECK(resp.m_msg == "ok");
        CHECK(resp.m_sessionId == "sess-abc-001");
    }
    //失败应答：对方忙（错误码 701），仅 code/msg 有效，sessionid 不应被填
    {
        std::string body =
            "{\"code\":701,\"msg\":\"peer busy\"}";
        CallInviteResponse resp;
        REQUIRE(CallProtocolClient::parseInviteResponse(body, resp));
        CHECK(resp.m_code == 701);
        CHECK(resp.m_msg == "peer busy");
        CHECK(resp.m_sessionId.empty());
    }
}

TEST_CASE("CC3 parseInviteNotify 解析会话/发起方/昵称/media")
{
    Json::Value v;
    v["sessionid"] = "sess-abc-001";
    v["fromid"]    = "5002";
    v["fromname"]  = u8"苏沐辰";
    v["media"]     = call_media_audio;
    std::string body = ccWriteCompact(v);

    CallInviteNotify notify;
    REQUIRE(CallProtocolClient::parseInviteNotify(body, notify));
    CHECK(notify.m_sessionId == "sess-abc-001");
    CHECK(notify.m_fromId == 5002);
    CHECK(notify.m_fromName == u8"苏沐辰");
    CHECK(notify.m_media == call_media_audio);
}

TEST_CASE("CC4 buildResponse 接听/拒绝两态")
{
    //接听
    {
        std::string json = CallProtocolClient::buildResponse("sess-1", true);
        Json::Value root;
        REQUIRE(ccParseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "sess-1");
        CHECK(root["accept"].isInt());
        CHECK(root["accept"].asInt() == 1);
    }
    //拒绝
    {
        std::string json = CallProtocolClient::buildResponse("sess-1", false);
        Json::Value root;
        REQUIRE(ccParseJsonText(json, root));
        CHECK(root["accept"].asInt() == 0);
    }
}

TEST_CASE("CC5 parseSignaling 解析中转地址/令牌/角色")
{
    Json::Value v;
    v["sessionid"] = "sess-abc-001";
    v["role"]      = call_role_caller;
    v["relayip"]   = "192.168.1.100";
    v["relayport"] = 20003;
    v["token"]     = "1|3|4|5|6.deadbeef";
    std::string body = ccWriteCompact(v);

    CallSignalingInfo info;
    REQUIRE(CallProtocolClient::parseSignaling(body, info));
    CHECK(info.m_sessionId == "sess-abc-001");
    CHECK(info.m_role == call_role_caller);
    CHECK(info.m_relayIp == "192.168.1.100");
    CHECK(info.m_relayPort == 20003);
    CHECK(info.m_token == "1|3|4|5|6.deadbeef");
}

TEST_CASE("CC6 buildCancel / buildHangup 仅带 sessionid")
{
    {
        std::string json = CallProtocolClient::buildCancel("sess-9");
        Json::Value root;
        REQUIRE(ccParseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "sess-9");
    }
    {
        std::string json = CallProtocolClient::buildHangup("sess-9");
        Json::Value root;
        REQUIRE(ccParseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "sess-9");
    }
}

TEST_CASE("CC7 parseStateNotify 解析状态码与描述（8 种状态）")
{
    //逐个状态值都能还原
    const int states[] = {
        call_state_ringing, call_state_rejected, call_state_canceled,
        call_state_peer_offline, call_state_timeout, call_state_connected,
        call_state_ended, call_state_busy
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        Json::Value v;
        v["sessionid"] = "sess-abc-001";
        v["state"]     = states[i];
        v["msg"]       = "x";
        CallStateNotify st;
        REQUIRE(CallProtocolClient::parseStateNotify(ccWriteCompact(v), st));
        CHECK(st.m_state == states[i]);
    }

    //带中文描述的接通态
    Json::Value v;
    v["sessionid"] = "sess-abc-001";
    v["state"]     = call_state_busy;
    v["msg"]       = u8"对方正在通话中";
    CallStateNotify st;
    REQUIRE(CallProtocolClient::parseStateNotify(ccWriteCompact(v), st));
    CHECK(st.m_sessionId == "sess-abc-001");
    CHECK(st.m_state == call_state_busy);
    CHECK(st.m_msg == u8"对方正在通话中");
}

TEST_CASE("CC8 buildRelayHello 与 parseRelayHelloAck round-trip")
{
    //构造握手帧
    std::string hello = CallProtocolClient::buildRelayHello("tok-xyz");
    Json::Value root;
    REQUIRE(ccParseJsonText(hello, root));
    CHECK(root["token"].asString() == "tok-xyz");

    //成功应答
    {
        CallRelayHelloAck ack;
        REQUIRE(CallProtocolClient::parseRelayHelloAck(
            "{\"code\":0,\"msg\":\"ok\"}", ack));
        CHECK(ack.m_code == 0);
        CHECK(ack.m_msg == "ok");
    }
    //失败应答（令牌过期，错误码 8 = 会话凭证失效）
    {
        CallRelayHelloAck ack;
        REQUIRE(CallProtocolClient::parseRelayHelloAck(
            "{\"code\":8,\"msg\":\"token expired\"}", ack));
        CHECK(ack.m_code == 8);
    }
}

TEST_CASE("CC9a 64 位 userid 经字符串编码 round-trip 不丢精度")
{
    //该值超过 2^53，若以 JSON number 承载会被 double 截断
    const int64_t bigId = 9007199254740993LL;

    //构造侧：peerid 字符串值须与原值一致
    std::string json = CallProtocolClient::buildInviteRequest(bigId, call_media_audio);
    Json::Value root;
    REQUIRE(ccParseJsonText(json, root));
    CHECK(root["peerid"].asString() == "9007199254740993");

    //解析侧：从字符串编码的 fromid 还原回精确的 int64
    Json::Value v;
    v["sessionid"] = "s";
    v["fromid"]    = "9007199254740993";
    v["fromname"]  = "x";
    CallInviteNotify notify;
    REQUIRE(CallProtocolClient::parseInviteNotify(ccWriteCompact(v), notify));
    CHECK(notify.m_fromId == bigId);
}

TEST_CASE("CC9b 非法 JSON 一律安全失败")
{
    CallInviteResponse resp;
    CHECK_FALSE(CallProtocolClient::parseInviteResponse("", resp));
    CHECK_FALSE(CallProtocolClient::parseInviteResponse("not json", resp));

    CallInviteNotify notify;
    CHECK_FALSE(CallProtocolClient::parseInviteNotify("[]", notify));

    CallSignalingInfo info;
    CHECK_FALSE(CallProtocolClient::parseSignaling("garbage", info));

    CallStateNotify st;
    CHECK_FALSE(CallProtocolClient::parseStateNotify("", st));

    CallRelayHelloAck ack;
    CHECK_FALSE(CallProtocolClient::parseRelayHelloAck("nope", ack));
}

TEST_CASE("CC9c 缺关键字段返回 false")
{
    //邀请应答缺 code
    {
        CallInviteResponse resp;
        CHECK_FALSE(CallProtocolClient::parseInviteResponse(
            "{\"msg\":\"ok\"}", resp));
    }
    //邀请推送缺 sessionid / 缺 fromid
    {
        CallInviteNotify notify;
        CHECK_FALSE(CallProtocolClient::parseInviteNotify(
            "{\"fromid\":\"5002\"}", notify));
        CHECK_FALSE(CallProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\"}", notify));
    }
    //中转参数缺 token / 缺 sessionid
    {
        CallSignalingInfo info;
        CHECK_FALSE(CallProtocolClient::parseSignaling(
            "{\"sessionid\":\"s\",\"relayip\":\"1.2.3.4\",\"relayport\":1}", info));
        CHECK_FALSE(CallProtocolClient::parseSignaling(
            "{\"token\":\"t\"}", info));
    }
    //状态推送缺 sessionid
    {
        CallStateNotify st;
        CHECK_FALSE(CallProtocolClient::parseStateNotify(
            "{\"state\":2}", st));
    }
    //握手应答缺 code
    {
        CallRelayHelloAck ack;
        CHECK_FALSE(CallProtocolClient::parseRelayHelloAck(
            "{\"msg\":\"ok\"}", ack));
    }
}

// =====================================================================
// CC-SM. 通话中切换媒体类型（cmd_call_switch_media / cmd_call_media_changed_notify）
// =====================================================================

TEST_CASE("CC-SM1 buildSwitchMedia 字段完整且紧凑")
{
    std::string json = CallProtocolClient::buildSwitchMedia("sess-2026", call_media_video);
    REQUIRE_FALSE(json.empty());

    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);

    CHECK(root["sessionid"].isString());
    CHECK(root["sessionid"].asString() == "sess-2026");
    CHECK(root["media"].isInt());
    CHECK(root["media"].asInt() == call_media_video);

    //紧凑格式：不含缩进与换行。
    CHECK(json.find(char(10)) == std::string::npos);
}

TEST_CASE("CC-SM2 buildSwitchMedia 对非法入参返回空串")
{
    //会话 id 为空：服务端必判非法，提前挡住省一次往返。
    CHECK(CallProtocolClient::buildSwitchMedia("", call_media_video).empty());
    //媒体类型不在 enum call_media 之内。
    CHECK(CallProtocolClient::buildSwitchMedia("sess-1", 0).empty());
    CHECK(CallProtocolClient::buildSwitchMedia("sess-1", 99).empty());
    CHECK(CallProtocolClient::buildSwitchMedia("sess-1", -1).empty());
    //两者都合法才产出报文。
    CHECK_FALSE(CallProtocolClient::buildSwitchMedia("sess-1", call_media_audio).empty());
}

TEST_CASE("CC-SM3 parseSwitchMediaResponse 区分成功与三类失败")
{
    CallSwitchMediaResponse okResp;
    REQUIRE(CallProtocolClient::parseSwitchMediaResponse(
        "{\"code\":0,\"msg\":\"ok\"}", okResp));
    CHECK(okResp.m_code == 0);
    CHECK(okResp.m_msg == "ok");

    //会话不存在（通话已结束 / id 无效）
    CallSwitchMediaResponse noSess;
    REQUIRE(CallProtocolClient::parseSwitchMediaResponse(
        "{\"code\":705,\"msg\":\"call not exist\"}", noSess));
    CHECK(noSess.m_code == error_code_call_not_exist);

    //非通话双方
    CallSwitchMediaResponse notMember;
    REQUIRE(CallProtocolClient::parseSwitchMediaResponse("{\"code\":706}", notMember));
    CHECK(notMember.m_code == error_code_call_not_member);
    CHECK(notMember.m_msg.empty());   //msg 缺失时留空，不影响 code 判定

    //未接通就切换
    CallSwitchMediaResponse badState;
    REQUIRE(CallProtocolClient::parseSwitchMediaResponse("{\"code\":707}", badState));
    CHECK(badState.m_code == error_code_call_state_invalid);
}

TEST_CASE("CC-SM4 parseSwitchMediaResponse 对畸形报文安全失败且不误判成功")
{
    CallSwitchMediaResponse resp;
    CHECK_FALSE(CallProtocolClient::parseSwitchMediaResponse("", resp));
    CHECK_FALSE(CallProtocolClient::parseSwitchMediaResponse("not a json", resp));
    CHECK_FALSE(CallProtocolClient::parseSwitchMediaResponse("[1,2]", resp));      //顶层非对象
    CHECK_FALSE(CallProtocolClient::parseSwitchMediaResponse("{}", resp));         //缺 code
    CHECK_FALSE(CallProtocolClient::parseSwitchMediaResponse("{\"code\":\"0\"}", resp));  //code 非整型

    //全程未被写成 0，避免"解析失败却被当成切换成功"。
    CHECK(resp.m_code != 0);
}

TEST_CASE("CC-SM5 parseMediaChangedNotify 取出会话/媒体/发起者三个字段")
{
    CallMediaChangedNotify n;
    REQUIRE(CallProtocolClient::parseMediaChangedNotify(
        "{\"sessionid\":\"sess-9\",\"media\":2,\"operator\":\"5002\"}", n));
    CHECK(n.m_sessionId == "sess-9");
    CHECK(n.m_media == call_media_video);
    CHECK(n.m_operator == 5002);

    //降级方向同样可解析。
    CallMediaChangedNotify down;
    REQUIRE(CallProtocolClient::parseMediaChangedNotify(
        "{\"sessionid\":\"sess-9\",\"media\":1,\"operator\":\"5001\"}", down));
    CHECK(down.m_media == call_media_audio);
}

TEST_CASE("CC-SM6 parseMediaChangedNotify 缺 operator 取 0（按非本端发起处理）")
{
    //operator 缺失时取 0，上层退化为"不是我发起的"，即不自动开摄像头——安全的一侧。
    CallMediaChangedNotify n;
    REQUIRE(CallProtocolClient::parseMediaChangedNotify(
        "{\"sessionid\":\"sess-9\",\"media\":2}", n));
    CHECK(n.m_operator == 0);
}

TEST_CASE("CC-SM7 parseMediaChangedNotify 对畸形输入与非法媒体类型安全失败")
{
    CallMediaChangedNotify n;
    CHECK_FALSE(CallProtocolClient::parseMediaChangedNotify("", n));
    CHECK_FALSE(CallProtocolClient::parseMediaChangedNotify("{bad", n));
    CHECK_FALSE(CallProtocolClient::parseMediaChangedNotify("[]", n));

    //缺 sessionid / sessionid 为空：无法定位是哪通通话。
    CHECK_FALSE(CallProtocolClient::parseMediaChangedNotify("{\"media\":2}", n));
    CHECK_FALSE(CallProtocolClient::parseMediaChangedNotify(
        "{\"sessionid\":\"\",\"media\":2}", n));

    //媒体类型非法：落地未知类型会让 UI 进入不可名状的中间态，整条丢弃。
    CHECK_FALSE(CallProtocolClient::parseMediaChangedNotify(
        "{\"sessionid\":\"s\",\"media\":0}", n));
    CHECK_FALSE(CallProtocolClient::parseMediaChangedNotify(
        "{\"sessionid\":\"s\",\"media\":99}", n));
}
