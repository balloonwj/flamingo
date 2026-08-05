/**
 *  v2 远程桌面信令编解码 + 会话管理单元测试（Suite RDP / RDM），
 *  test_remotedesktopprotocol.cpp
 *  覆盖：
 *    - RemoteDesktopProtocol 的请求解析（invite/response/cancel/close）与应答、
 *      推送构造（invite_response/invite_notify/relay_info/state_notify）的字段
 *      完整性、类型、64 位 ID 字符串编码、畸形输入安全失败；
 *    - RemoteDesktopManager 的会话表 CRUD、按角色签发 relay 令牌（用 RelayToken
 *      校验角色 / uid / peeruid / sessionid 正确），以及 sessionid 不含令牌分隔符。
 *  balloonwj@qq.com   2026-06-08
 */
#include "doctest.h"
#include "../chatserversrc/RemoteDesktopProtocol.h"
#include "../chatserversrc/RemoteDesktopManager.h"
#include "../utils/RelayToken.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

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

// ======================= RemoteDesktopProtocol 编解码 =======================

TEST_CASE("RDP1 parseInviteRequest 取出 peerid（字符串编码）")
{
    int64_t peerid = 0;
    REQUIRE(RemoteDesktopProtocol::parseInviteRequest("{\"peerid\":\"5002\"}", peerid));
    CHECK(peerid == 5002);

    //缺字段 / 非字符串 / 非法 JSON 一律失败
    CHECK_FALSE(RemoteDesktopProtocol::parseInviteRequest("{}", peerid));
    CHECK_FALSE(RemoteDesktopProtocol::parseInviteRequest("{\"peerid\":0}", peerid));
    CHECK_FALSE(RemoteDesktopProtocol::parseInviteRequest("not json", peerid));
}

TEST_CASE("RDP2 buildInviteResponse 成功带 sessionid、失败不带")
{
    //成功：含 sessionid
    {
        std::string json = RemoteDesktopProtocol::buildInviteResponse(0, "ok", "sess-001");
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["code"].asInt() == 0);
        CHECK(root["msg"].asString() == "ok");
        CHECK(root["sessionid"].asString() == "sess-001");
    }
    //失败：契约要求不下发 sessionid 键
    {
        std::string json = RemoteDesktopProtocol::buildInviteResponse(7, "peer offline", "");
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["code"].asInt() == 7);
        CHECK(root["msg"].asString() == "peer offline");
        CHECK_FALSE(root.isMember("sessionid"));
    }
}

TEST_CASE("RDP3 buildInviteNotify 字段完整且 fromid 字符串编码")
{
    std::string json = RemoteDesktopProtocol::buildInviteNotify("sess-001", 5002, u8"苏沐辰");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "sess-001");
    CHECK(root["fromid"].isString());
    CHECK(root["fromid"].asString() == "5002");
    CHECK(root["fromname"].asString() == u8"苏沐辰");
}

TEST_CASE("RDP4 parseResponseRequest 取 sessionid 与 accept")
{
    std::string sessionid;
    int accept = -1;

    REQUIRE(RemoteDesktopProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":1}", sessionid, accept));
    CHECK(sessionid == "s");
    CHECK(accept == 1);

    REQUIRE(RemoteDesktopProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":0}", sessionid, accept));
    CHECK(accept == 0);

    //缺 sessionid 失败
    CHECK_FALSE(RemoteDesktopProtocol::parseResponseRequest("{\"accept\":1}", sessionid, accept));
}

TEST_CASE("RDP5 buildRelayInfo 字段完整")
{
    std::string json = RemoteDesktopProtocol::buildRelayInfo(
        "sess-001", RelayToken::role_controller, "192.168.1.100", 20002, "1|2|3.deadbeef");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "sess-001");
    CHECK(root["role"].asInt() == RelayToken::role_controller);
    CHECK(root["relayip"].asString() == "192.168.1.100");
    CHECK(root["relayport"].asInt() == 20002);
    CHECK(root["token"].asString() == "1|2|3.deadbeef");
}

TEST_CASE("RDP6 parseSessionRequest（cancel/close 共用）取 sessionid")
{
    std::string sessionid;
    REQUIRE(RemoteDesktopProtocol::parseSessionRequest("{\"sessionid\":\"s-9\"}", sessionid));
    CHECK(sessionid == "s-9");

    //缺 sessionid / 非法 JSON 失败
    CHECK_FALSE(RemoteDesktopProtocol::parseSessionRequest("{}", sessionid));
    CHECK_FALSE(RemoteDesktopProtocol::parseSessionRequest("garbage", sessionid));
}

TEST_CASE("RDP7 buildStateNotify 字段完整")
{
    std::string json = RemoteDesktopProtocol::buildStateNotify(
        "sess-001", rd_state_rejected, "rejected");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "sess-001");
    CHECK(root["state"].asInt() == rd_state_rejected);
    CHECK(root["msg"].asString() == "rejected");
}

// ======================= RemoteDesktopManager 会话表 / 签 token =======================

TEST_CASE("RDM1 createSession 登记会话、sessionid 不含令牌分隔符")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    std::string sid = mgr.createSession(100007, 200001, 1000);

    REQUIRE_FALSE(sid.empty());
    //sessionid 进 RelayToken 的 payload，须不含分隔符 '.' 与 '|'
    CHECK(sid.find('.') == std::string::npos);
    CHECK(sid.find('|') == std::string::npos);

    RemoteDesktopSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.controllerId == 100007);
    CHECK(s.controlledId == 200001);
    CHECK(s.state == rd_state_ringing);
    CHECK(s.createTime == 1000);
}

TEST_CASE("RDM2 removeSession 后查不到")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    std::string sid = mgr.createSession(1, 2, 1000);

    REQUIRE(mgr.removeSession(sid));
    RemoteDesktopSessionInfo s;
    CHECK_FALSE(mgr.getSession(sid, s));
    //再次删除返回 false（本无该会话）
    CHECK_FALSE(mgr.removeSession(sid));
}

TEST_CASE("RDM3 不存在的 sessionid：查询 / 改状态 / 签 token 均失败")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    RemoteDesktopSessionInfo s;
    std::string token;
    CHECK_FALSE(mgr.getSession("no-such-session", s));
    CHECK_FALSE(mgr.setState("no-such-session", rd_state_disconnected));
    CHECK_FALSE(mgr.signTokenFor("no-such-session", RelayToken::role_controller, 1000, token));
}

TEST_CASE("RDM4 signTokenFor 双方令牌：同会话、相反角色、uid 与 peeruid 对调")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    const std::string key = "relay-test-key";
    mgr.configure(key, "192.168.1.100", 20002);
    CHECK(mgr.getRelayHost() == "192.168.1.100");
    CHECK(mgr.getRelayPort() == 20002);

    std::string sid = mgr.createSession(100007, 200001, 1000);

    std::string tokCtrl;
    std::string tokCtrled;
    REQUIRE(mgr.signTokenFor(sid, RelayToken::role_controller, 1000, tokCtrl));
    REQUIRE(mgr.signTokenFor(sid, RelayToken::role_controlled, 1000, tokCtrled));

    RelayTokenPayload a;
    RelayTokenPayload b;
    //令牌有效期 5 分钟，now=2000 仍在有效期内
    REQUIRE(RelayToken::verify(tokCtrl, key, 2000, a));
    REQUIRE(RelayToken::verify(tokCtrled, key, 2000, b));

    //同一会话
    CHECK(a.sessionid == sid);
    CHECK(b.sessionid == sid);
    //角色相反
    CHECK(a.role == RelayToken::role_controller);
    CHECK(b.role == RelayToken::role_controlled);
    //控制端令牌 uid=控制端、peeruid=被控端；被控端令牌对调
    CHECK(a.uid == 100007);
    CHECK(a.peeruid == 200001);
    CHECK(b.uid == 200001);
    CHECK(b.peeruid == 100007);
}

TEST_CASE("RDM5 setState 更新会话状态")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    std::string sid = mgr.createSession(1, 2, 1000);

    REQUIRE(mgr.setState(sid, rd_state_disconnected));
    RemoteDesktopSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == rd_state_disconnected);
}
