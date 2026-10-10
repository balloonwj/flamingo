/**
 *  v2 音视频通话信令编解码单元测试（Suite CP），test_callprotocol.cpp
 *  覆盖 CallProtocol 的请求解析（invite/response/cancel/hangup）与应答、推送构造
 *  （invite_response/invite_notify/signaling/state_notify）的字段完整性、类型、
 *  media 字段、64 位 ID 字符串编码、畸形输入安全失败。
 *  CallManager 会话表相关用例见 test_callmanager.cpp（另文）。
 *  balloonwj@qq.com   2026-06-11
 */
#include "doctest.h"
#include "../chatserversrc/CallProtocol.h"
#include "../utils/RelayToken.h"
#include "../jsoncpp1.9.0/json.h"

//把 JSON 文本解析成 Json::Value，供断言检查构造产物
static bool cpParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("CP1 parseInviteRequest 取出 peerid（字符串编码）与 media")
{
    int64_t peerid = 0;
    int     media  = 0;

    REQUIRE(CallProtocol::parseInviteRequest("{\"peerid\":\"5002\",\"media\":1}", peerid, media));
    CHECK(peerid == 5002);
    CHECK(media == call_media_audio);

    //media 缺失按纯语音
    REQUIRE(CallProtocol::parseInviteRequest("{\"peerid\":\"5002\"}", peerid, media));
    CHECK(media == call_media_audio);

    //media=2（音视频）
    REQUIRE(CallProtocol::parseInviteRequest("{\"peerid\":\"5002\",\"media\":2}", peerid, media));
    CHECK(media == call_media_video);

    //缺字段 / 非字符串 peerid / 非法 JSON 一律失败
    CHECK_FALSE(CallProtocol::parseInviteRequest("{}", peerid, media));
    CHECK_FALSE(CallProtocol::parseInviteRequest("{\"peerid\":0}", peerid, media));
    CHECK_FALSE(CallProtocol::parseInviteRequest("not json", peerid, media));
}

TEST_CASE("CP2 buildInviteResponse 成功带 sessionid、失败不带")
{
    //成功：含 sessionid
    {
        std::string json = CallProtocol::buildInviteResponse(0, "ok", "sess-001");
        Json::Value root;
        REQUIRE(cpParseJsonText(json, root));
        CHECK(root["code"].asInt() == 0);
        CHECK(root["msg"].asString() == "ok");
        CHECK(root["sessionid"].asString() == "sess-001");
    }
    //失败：契约要求不下发 sessionid 键
    {
        std::string json = CallProtocol::buildInviteResponse(702, "peer offline", "");
        Json::Value root;
        REQUIRE(cpParseJsonText(json, root));
        CHECK(root["code"].asInt() == 702);
        CHECK(root["msg"].asString() == "peer offline");
        CHECK_FALSE(root.isMember("sessionid"));
    }
}

TEST_CASE("CP3 buildInviteNotify 字段完整、fromid 字符串编码、media 携带")
{
    std::string json = CallProtocol::buildInviteNotify("sess-001", 5002, u8"苏沐辰", call_media_audio);
    Json::Value root;
    REQUIRE(cpParseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "sess-001");
    CHECK(root["fromid"].isString());
    CHECK(root["fromid"].asString() == "5002");
    CHECK(root["fromname"].asString() == u8"苏沐辰");
    CHECK(root["media"].asInt() == call_media_audio);

    //大 userid 以字符串编码，不丢精度（double 无法精确表示 17 位整数）
    std::string big = CallProtocol::buildInviteNotify("s", 10000000000000001LL, "x", call_media_video);
    Json::Value rbig;
    REQUIRE(cpParseJsonText(big, rbig));
    CHECK(rbig["fromid"].asString() == "10000000000000001");
    CHECK(rbig["media"].asInt() == call_media_video);
}

TEST_CASE("CP8 buildInviteResponse 的 p2psessionid：成功且给了才写，没给时与原来相同（P2P 方案第 6 期）")
{
    std::string json = CallProtocol::buildInviteResponse(0, "ok", "sess-001", "p2p-1-2-3-4");
    Json::Value root;
    REQUIRE(cpParseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "sess-001");
    CHECK(root["p2psessionid"].asString() == "p2p-1-2-3-4");

    //没给：不写这个字段，与第 6 期之前的应答逐字相同
    const std::string plain = CallProtocol::buildInviteResponse(0, "ok", "sess-001");
    REQUIRE(cpParseJsonText(plain, root));
    CHECK_FALSE(root.isMember("p2psessionid"));
    CHECK(plain == CallProtocol::buildInviteResponse(0, "ok", "sess-001", std::string()));

    //失败：即使给了也不写
    REQUIRE(cpParseJsonText(CallProtocol::buildInviteResponse(702, "peer offline", "", "p2p-x"), root));
    CHECK_FALSE(root.isMember("p2psessionid"));
}

TEST_CASE("CP9 buildInviteNotify 的 p2psessionid：给了才写，没给时与原来相同（P2P 方案第 6 期）")
{
    const int64_t kRingMs = 45000;
    std::string json = CallProtocol::buildInviteNotify("sess-001", 5002, "x", call_media_video, kRingMs, "p2p-1-2-3-4");
    Json::Value root;
    REQUIRE(cpParseJsonText(json, root));
    CHECK(root["p2psessionid"].asString() == "p2p-1-2-3-4");
    CHECK(root["ringms"].asInt64() == kRingMs);
    CHECK(root["media"].asInt() == call_media_video);

    const std::string plain = CallProtocol::buildInviteNotify("sess-001", 5002, "x", call_media_video, kRingMs);
    REQUIRE(cpParseJsonText(plain, root));
    CHECK_FALSE(root.isMember("p2psessionid"));
    CHECK(plain == CallProtocol::buildInviteNotify("sess-001", 5002, "x", call_media_video, kRingMs, std::string()));
}

TEST_CASE("CP4 parseResponseRequest 取 sessionid 与 accept")
{
    std::string sessionid;
    int accept = -1;

    REQUIRE(CallProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":1}", sessionid, accept));
    CHECK(sessionid == "s");
    CHECK(accept == 1);

    REQUIRE(CallProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":0}", sessionid, accept));
    CHECK(accept == 0);

    //缺 sessionid 失败
    CHECK_FALSE(CallProtocol::parseResponseRequest("{\"accept\":1}", sessionid, accept));
}

TEST_CASE("CP5 buildSignaling 字段完整（中转参数下发）")
{
    std::string json = CallProtocol::buildSignaling(
        "sess-001", RelayToken::role_caller, "192.168.1.100", 20003, "1|3|7.deadbeef");
    Json::Value root;
    REQUIRE(cpParseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "sess-001");
    CHECK(root["role"].asInt() == RelayToken::role_caller);
    CHECK(root["relayip"].asString() == "192.168.1.100");
    CHECK(root["relayport"].asInt() == 20003);
    CHECK(root["token"].asString() == "1|3|7.deadbeef");
}

TEST_CASE("CP6 parseSessionRequest（cancel/hangup 共用）取 sessionid")
{
    std::string sessionid;
    REQUIRE(CallProtocol::parseSessionRequest("{\"sessionid\":\"s-9\"}", sessionid));
    CHECK(sessionid == "s-9");

    //缺 sessionid / 非法 JSON 失败
    CHECK_FALSE(CallProtocol::parseSessionRequest("{}", sessionid));
    CHECK_FALSE(CallProtocol::parseSessionRequest("garbage", sessionid));
}

TEST_CASE("CP7 buildStateNotify 字段完整（8 种状态之一）")
{
    std::string json = CallProtocol::buildStateNotify(
        "sess-001", call_state_connected, "connected");
    Json::Value root;
    REQUIRE(cpParseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "sess-001");
    CHECK(root["state"].asInt() == call_state_connected);
    CHECK(root["msg"].asString() == "connected");
}


TEST_CASE("CP10 一对一通话卡片：呼叫时的 content 字段齐全，id 与时间按字符串编码")
{
    const std::string content = CallProtocol::buildCardContent("5127-5308-1-1", call_media_video, 5127, u8"张勇", 5308,
                                                               1791167956000LL);
    Json::Value root;
    REQUIRE(cpParseJsonText(content, root));
    CHECK(root["kind"].asString() == "call");
    CHECK(root["sessionid"].asString() == "5127-5308-1-1");
    CHECK(root["media"].asInt() == call_media_video);
    CHECK(root["caller"].asString() == "5127");
    CHECK(root["callername"].asString() == u8"张勇");
    CHECK(root["callee"].asString() == "5308");
    CHECK(root["starttime"].asString() == "1791167956000");
    CHECK_FALSE(root.isMember("result"));
    CHECK_FALSE(root.isMember("duration"));
    CHECK(CallProtocol::cardMedia(content) == call_media_video);
}

TEST_CASE("CP11 一对一通话卡片改写：接通过的带时长（秒），没接通的不带；media 以结束时为准")
{
    const int64_t start = 1791167956000LL;
    const std::string content = CallProtocol::buildCardContent("s1", call_media_audio, 5127, u8"张勇", 5308, start);
    //接通 1 秒后开始算，171.9 秒后挂断：按秒向下取整为 171；通话中切到了视频
    const int64_t connect = start + 1000;
    const int64_t end = connect + 171900;
    const std::string ended = CallProtocol::buildEndedCardContent(content, "s1", call_media_video, 5127, u8"张勇", 5308,
                                                                  start, connect, call_state_ended, end);
    Json::Value root;
    REQUIRE(cpParseJsonText(ended, root));
    CHECK(root["kind"].asString() == "call");
    CHECK(root["callername"].asString() == u8"张勇");
    CHECK(root["result"].asInt() == call_state_ended);
    CHECK(root["endtime"].asString() == std::to_string(end));
    CHECK(root["duration"].asInt64() == 171);
    CHECK(root["media"].asInt() == call_media_video);

    //没接通（拒绝）：有结果与结束时间，没有时长
    const std::string rejected = CallProtocol::buildEndedCardContent(content, "s1", call_media_audio, 5127, u8"张勇",
                                                                     5308, start, 0, call_state_rejected, start + 5000);
    Json::Value r;
    REQUIRE(cpParseJsonText(rejected, r));
    CHECK(r["result"].asInt() == call_state_rejected);
    CHECK_FALSE(r.isMember("duration"));
}

TEST_CASE("CP12 一对一通话卡片改写：原 content 不可用时按会话信息重建；结束早于接通按 0 秒")
{
    const int64_t start = 1000000LL;
    const std::string ended = CallProtocol::buildEndedCardContent("not json", "s2", call_media_audio, 7, "A", 8, start,
                                                                  start + 5000, call_state_ended, start + 3000);
    Json::Value root;
    REQUIRE(cpParseJsonText(ended, root));
    CHECK(root["kind"].asString() == "call");
    CHECK(root["sessionid"].asString() == "s2");
    CHECK(root["caller"].asString() == "7");
    CHECK(root["callee"].asString() == "8");
    CHECK(root["duration"].asInt64() == 0);
}

TEST_CASE("CP13 cardMedia 只认一对一通话卡片；cmd_call_card_notify 的字段与各自视角的 cid")
{
    CHECK(CallProtocol::cardMedia("{\"kind\":\"groupcall\",\"media\":1}") == 0);
    CHECK(CallProtocol::cardMedia("{\"kind\":\"call\",\"media\":9}") == 0);
    CHECK(CallProtocol::cardMedia("garbage") == 0);
    CHECK(CallProtocol::cardMedia("{\"kind\":\"call\",\"media\":1}") == call_media_audio);

    const std::string content = CallProtocol::buildCardContent("s3", call_media_audio, 5127, "A", 5308, 1);
    const std::string notify = CallProtocol::buildCardNotify("s3", 9001, 5308, content);
    Json::Value root;
    REQUIRE(cpParseJsonText(notify, root));
    CHECK(root["sessionid"].asString() == "s3");
    CHECK(root["msgid"].asString() == "9001");
    CHECK(root["cid"].asString() == "5308");
    REQUIRE(root["content"].isObject());
    CHECK(root["content"]["kind"].asString() == "call");
}
