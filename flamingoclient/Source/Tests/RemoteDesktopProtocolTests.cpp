/**
 *  v2 远程桌面信令编解码（客户端侧）单元测试（Suite RD），RemoteDesktopProtocolTests.cpp
 *  覆盖：
 *    - 请求构造：buildInviteRequest / buildResponse / buildCancel / buildClose /
 *      buildRelayHello 的字段完整性、类型、紧凑性、64 位 ID 字符串编码；
 *    - 应答 / 推送解析：parseInviteResponse（成功/失败）、parseInviteNotify、
 *      parseRelayInfo、parseStateNotify、parseRelayHelloAck 的字段还原；
 *    - 64 位 userid 经字符串编码 round-trip 不丢精度；
 *    - 畸形输入（非法 JSON / 缺关键字段）一律安全失败返回 false。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-08
 */
#include "catch_amalgamated.hpp"
#include "../net/RemoteDesktopProtocolClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查构造产物
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的推送/应答报文
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

TEST_CASE("RD1 buildInviteRequest 字段完整、紧凑且 peerid 字符串编码")
{
    std::string json = RemoteDesktopProtocolClient::buildInviteRequest(5002);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //peerid 必须为字符串编码（防大整数精度丢失），值为被控端 userid
    CHECK(root["peerid"].isString());
    CHECK(root["peerid"].asString() == "5002");
    //紧凑输出：不含换行
    CHECK(json.find('\n') == std::string::npos);
}

TEST_CASE("RD2 parseInviteResponse 成功取 sessionid、失败取 msg")
{
    //成功应答：code=0，带 sessionid
    {
        std::string body =
            "{\"code\":0,\"msg\":\"ok\",\"sessionid\":\"sess-abc-001\"}";
        RDInviteResponse resp;
        REQUIRE(RemoteDesktopProtocolClient::parseInviteResponse(body, resp));
        CHECK(resp.m_code == 0);
        CHECK(resp.m_msg == "ok");
        CHECK(resp.m_sessionId == "sess-abc-001");
    }
    //失败应答：对方离线，仅 code/msg 有效，sessionid 不应被填
    {
        std::string body =
            "{\"code\":7,\"msg\":\"peer offline\"}";
        RDInviteResponse resp;
        REQUIRE(RemoteDesktopProtocolClient::parseInviteResponse(body, resp));
        CHECK(resp.m_code == 7);
        CHECK(resp.m_msg == "peer offline");
        CHECK(resp.m_sessionId.empty());
    }
}

TEST_CASE("RD3 parseInviteNotify 解析会话/发起方/昵称")
{
    Json::Value v;
    v["sessionid"] = "sess-abc-001";
    v["fromid"]    = "5002";
    v["fromname"]  = u8"苏沐辰";
    std::string body = writeCompact(v);

    RDInviteNotify notify;
    REQUIRE(RemoteDesktopProtocolClient::parseInviteNotify(body, notify));
    CHECK(notify.m_sessionId == "sess-abc-001");
    CHECK(notify.m_fromId == 5002);
    CHECK(notify.m_fromName == u8"苏沐辰");
}

TEST_CASE("RD4 buildResponse 接受/拒绝两态")
{
    //接受
    {
        std::string json = RemoteDesktopProtocolClient::buildResponse("sess-1", true);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "sess-1");
        CHECK(root["accept"].isInt());
        CHECK(root["accept"].asInt() == 1);
    }
    //拒绝
    {
        std::string json = RemoteDesktopProtocolClient::buildResponse("sess-1", false);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["accept"].asInt() == 0);
    }
}

TEST_CASE("RD5 parseRelayInfo 解析中转地址/令牌/角色")
{
    Json::Value v;
    v["sessionid"] = "sess-abc-001";
    v["role"]      = rd_role_controller;
    v["relayip"]   = "192.168.1.100";
    v["relayport"] = 12346;
    v["token"]     = "1|2|3|4|5.deadbeef";
    std::string body = writeCompact(v);

    RDRelayInfo info;
    REQUIRE(RemoteDesktopProtocolClient::parseRelayInfo(body, info));
    CHECK(info.m_sessionId == "sess-abc-001");
    CHECK(info.m_role == rd_role_controller);
    CHECK(info.m_relayIp == "192.168.1.100");
    CHECK(info.m_relayPort == 12346);
    CHECK(info.m_token == "1|2|3|4|5.deadbeef");
}

TEST_CASE("RD6 buildCancel / buildClose 仅带 sessionid")
{
    {
        std::string json = RemoteDesktopProtocolClient::buildCancel("sess-9");
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "sess-9");
    }
    {
        std::string json = RemoteDesktopProtocolClient::buildClose("sess-9");
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "sess-9");
    }
}

TEST_CASE("RD7 parseStateNotify 解析状态码与描述")
{
    Json::Value v;
    v["sessionid"] = "sess-abc-001";
    v["state"]     = rd_state_rejected;
    v["msg"]       = u8"对方拒绝了远程桌面请求";
    std::string body = writeCompact(v);

    RDStateNotify st;
    REQUIRE(RemoteDesktopProtocolClient::parseStateNotify(body, st));
    CHECK(st.m_sessionId == "sess-abc-001");
    CHECK(st.m_state == rd_state_rejected);
    CHECK(st.m_msg == u8"对方拒绝了远程桌面请求");
}

TEST_CASE("RD8 buildRelayHello 与 parseRelayHelloAck round-trip")
{
    //构造握手帧
    std::string hello = RemoteDesktopProtocolClient::buildRelayHello("tok-xyz");
    Json::Value root;
    REQUIRE(parseJsonText(hello, root));
    CHECK(root["token"].asString() == "tok-xyz");

    //成功应答
    {
        RDRelayHelloAck ack;
        REQUIRE(RemoteDesktopProtocolClient::parseRelayHelloAck(
            "{\"code\":0,\"msg\":\"ok\"}", ack));
        CHECK(ack.m_code == 0);
        CHECK(ack.m_msg == "ok");
    }
    //失败应答（令牌过期）
    {
        RDRelayHelloAck ack;
        REQUIRE(RemoteDesktopProtocolClient::parseRelayHelloAck(
            "{\"code\":8,\"msg\":\"token expired\"}", ack));
        CHECK(ack.m_code == 8);
    }
}

TEST_CASE("RD9a 64 位 userid 经字符串编码 round-trip 不丢精度")
{
    //该值超过 2^53，若以 JSON number 承载会被 double 截断
    const int64_t bigId = 9007199254740993LL;

    //构造侧：peerid 字符串值须与原值一致
    std::string json = RemoteDesktopProtocolClient::buildInviteRequest(bigId);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["peerid"].asString() == "9007199254740993");

    //解析侧：从字符串编码的 fromid 还原回精确的 int64
    Json::Value v;
    v["sessionid"] = "s";
    v["fromid"]    = "9007199254740993";
    v["fromname"]  = "x";
    RDInviteNotify notify;
    REQUIRE(RemoteDesktopProtocolClient::parseInviteNotify(writeCompact(v), notify));
    CHECK(notify.m_fromId == bigId);
}

TEST_CASE("RD9b 非法 JSON 一律安全失败")
{
    RDInviteResponse resp;
    CHECK_FALSE(RemoteDesktopProtocolClient::parseInviteResponse("", resp));
    CHECK_FALSE(RemoteDesktopProtocolClient::parseInviteResponse("not json", resp));

    RDInviteNotify notify;
    CHECK_FALSE(RemoteDesktopProtocolClient::parseInviteNotify("[]", notify));

    RDRelayInfo info;
    CHECK_FALSE(RemoteDesktopProtocolClient::parseRelayInfo("garbage", info));

    RDStateNotify st;
    CHECK_FALSE(RemoteDesktopProtocolClient::parseStateNotify("", st));

    RDRelayHelloAck ack;
    CHECK_FALSE(RemoteDesktopProtocolClient::parseRelayHelloAck("nope", ack));
}

TEST_CASE("RD9c 缺关键字段返回 false")
{
    //邀请应答缺 code
    {
        RDInviteResponse resp;
        CHECK_FALSE(RemoteDesktopProtocolClient::parseInviteResponse(
            "{\"msg\":\"ok\"}", resp));
    }
    //邀请推送缺 sessionid / 缺 fromid
    {
        RDInviteNotify notify;
        CHECK_FALSE(RemoteDesktopProtocolClient::parseInviteNotify(
            "{\"fromid\":\"5002\"}", notify));
        CHECK_FALSE(RemoteDesktopProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\"}", notify));
    }
    //中转参数缺 token / 缺 sessionid
    {
        RDRelayInfo info;
        CHECK_FALSE(RemoteDesktopProtocolClient::parseRelayInfo(
            "{\"sessionid\":\"s\",\"relayip\":\"1.2.3.4\",\"relayport\":1}", info));
        CHECK_FALSE(RemoteDesktopProtocolClient::parseRelayInfo(
            "{\"token\":\"t\"}", info));
    }
    //状态推送缺 sessionid
    {
        RDStateNotify st;
        CHECK_FALSE(RemoteDesktopProtocolClient::parseStateNotify(
            "{\"state\":2}", st));
    }
    //握手应答缺 code
    {
        RDRelayHelloAck ack;
        CHECK_FALSE(RemoteDesktopProtocolClient::parseRelayHelloAck(
            "{\"msg\":\"ok\"}", ack));
    }
}
