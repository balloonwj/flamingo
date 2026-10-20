/**
 *  v2 远程桌面信令编解码 + 会话管理单元测试（Suite RDP / RDM），
 *  test_remotedesktopprotocol.cpp
 *  覆盖：
 *    - RemoteDesktopProtocol 的请求解析（invite/response/cancel/close）与应答、
 *      推送构造（invite_response/invite_notify/relay_info/state_notify）的字段
 *      完整性、类型、64 位 ID 字符串编码、畸形输入安全失败；relay_info 的可选 p2psessionid（RDP8，P2P 方案第 6 期）；
 *    - RemoteDesktopManager 的会话表 CRUD、按角色签发 relay 令牌（用 RelayToken
 *      校验角色 / uid / peeruid / sessionid 正确），以及 sessionid 不含令牌分隔符；
 *    - 控制权限（2026-10-12）：1718 的解析与 1718 应答、1719 推送的构造（RDP9~10），
 *      会话表里控制状态的转换、角色校验与通知对象（RDM9~14）。
 *  balloonwj@qq.com   2026-06-08
 */
#include "doctest.h"
#include "../chatserversrc/RemoteDesktopProtocol.h"
#include "../chatserversrc/RemoteDesktopManager.h"
#include "../chatserversrc/Protocol.h"     // error_code_*（控制权限用例 RDM9~14 断言错误码）
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

TEST_CASE("RDP8 buildRelayInfo 的 p2psessionid：给了才写，没给时与原来相同（P2P 方案第 6 期）")
{
    std::string json = RemoteDesktopProtocol::buildRelayInfo(
        "sess-001", RelayToken::role_controlled, "192.168.1.100", 20002, "1|2|3.deadbeef", "p2p-1-2-3-4");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["p2psessionid"].asString() == "p2p-1-2-3-4");
    CHECK(root["role"].asInt() == RelayToken::role_controlled);

    //不给：不写这个字段，与第 6 期之前的推送逐字相同
    const std::string plain = RemoteDesktopProtocol::buildRelayInfo(
        "sess-001", RelayToken::role_controller, "192.168.1.100", 20002, "1|2|3.deadbeef");
    REQUIRE(parseJsonText(plain, root));
    CHECK_FALSE(root.isMember("p2psessionid"));
    CHECK(plain == RemoteDesktopProtocol::buildRelayInfo("sess-001", RelayToken::role_controller, "192.168.1.100",
                                                          20002, "1|2|3.deadbeef", std::string()));
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

TEST_CASE("RDM3 不存在的 sessionid：查询 / 签 token 均失败")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    RemoteDesktopSessionInfo s;
    std::string token;
    CHECK_FALSE(mgr.getSession("no-such-session", s));
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

//RDM5 已作废、编号不复用：原为「setState 更新会话状态」。会话的 state 字段生产代码从不读写，2026-10-14 与
//setState 一起删除（docs/远程桌面控制权限与观看工具条方案.md 8.2 第 2 条）；会话是否接通由 connected 表示

TEST_CASE("RDM6 掉线宽限：最后一个连接断开后超过宽限期才清理，期间重新登录则保留")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    const int64_t controller = 810006;
    const int64_t controlled = 820006;
    //宽限期 30 秒（毫秒），与默认配置一致
    const int64_t grace = 30 * 1000;
    const int64_t t0 = 7000000;
    mgr.setOfflineGraceMs(grace);
    CHECK(mgr.offlineGraceMs() == grace);

    std::string sid = mgr.createSession(controller, controlled, t0);

    //被控端掉线：宽限期内不清理
    mgr.markUserOffline(controlled, t0 + 1000);
    std::vector<RemoteDesktopSessionInfo> reaped;
    mgr.reapOfflineSessions(t0 + 1000 + grace - 1, reaped);
    CHECK(reaped.empty());

    //宽限期内重新登录：掉线时刻清掉，过了原定期限也不清理
    mgr.markUserOnline(controlled);
    mgr.reapOfflineSessions(t0 + 1000 + grace * 2, reaped);
    CHECK(reaped.empty());

    //再次掉线：重复调用不刷新掉线时刻（以第一次为准）
    mgr.markUserOffline(controller, t0 + 100000);
    mgr.markUserOffline(controller, t0 + 120000);
    mgr.reapOfflineSessions(t0 + 100000 + grace, reaped);
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0].sessionid == sid);
    CHECK(reaped[0].controllerOfflineAt == t0 + 100000);
    CHECK(reaped[0].controlledOfflineAt == 0);
    RemoteDesktopSessionInfo info;
    CHECK_FALSE(mgr.getSession(sid, info));
}

TEST_CASE("RDM7 掉线只影响该用户参与的会话；响铃中的会话同样按宽限期清理")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    const int64_t a = 810007;
    const int64_t b = 820007;
    const int64_t c = 830007;
    //宽限期 30 秒（毫秒）
    const int64_t grace = 30 * 1000;
    const int64_t t0 = 8000000;
    mgr.setOfflineGraceMs(grace);

    //a 控制 b（仍在响铃）；b 控制 c
    std::string ab = mgr.createSession(a, b, t0);
    std::string bc = mgr.createSession(b, c, t0);
    mgr.markUserOffline(a, t0);
    std::vector<RemoteDesktopSessionInfo> reaped;
    mgr.reapOfflineSessions(t0 + grace, reaped);
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0].sessionid == ab);
    RemoteDesktopSessionInfo info;
    CHECK(mgr.getSession(bc, info));
    mgr.removeSession(bc);
}

TEST_CASE("RDM8 宽限期配置：非正数按默认 30 秒处理")
{
    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    //默认宽限期（毫秒）
    const int64_t defaultGrace = 30 * 1000;
    mgr.setOfflineGraceMs(0);
    CHECK(mgr.offlineGraceMs() == defaultGrace);
    mgr.setOfflineGraceMs(-1);
    CHECK(mgr.offlineGraceMs() == defaultGrace);
    mgr.setOfflineGraceMs(5000);
    CHECK(mgr.offlineGraceMs() == 5000);
    mgr.setOfflineGraceMs(defaultGrace);
}

// ======================= 控制权限（1718 / 1719，2026-10-12） =======================
//方案见 docs/远程桌面控制权限与观看工具条方案.md 第 4.1 节：会话接通后默认未授权，控制端申请、被控端同意，
//被控端也可以直接授予；控制状态记在服务端会话表里

TEST_CASE("RDP9 parseControlRequest 取 sessionid 与 action，缺字段或动作越界时失败")
{
    std::string sessionid;
    int action = 0;
    REQUIRE(RemoteDesktopProtocol::parseControlRequest("{\"sessionid\":\"s-1\",\"action\":1}", sessionid, action));
    CHECK(sessionid == "s-1");
    CHECK(action == rd_control_request);
    REQUIRE(RemoteDesktopProtocol::parseControlRequest("{\"sessionid\":\"s-1\",\"action\":6}", sessionid, action));
    CHECK(action == rd_control_release);

    //缺 sessionid、缺 action、action 不在 1~6、action 不是整数、非法 JSON 一律失败
    CHECK_FALSE(RemoteDesktopProtocol::parseControlRequest("{\"action\":1}", sessionid, action));
    CHECK_FALSE(RemoteDesktopProtocol::parseControlRequest("{\"sessionid\":\"s-1\"}", sessionid, action));
    CHECK_FALSE(RemoteDesktopProtocol::parseControlRequest("{\"sessionid\":\"s-1\",\"action\":0}", sessionid, action));
    CHECK_FALSE(RemoteDesktopProtocol::parseControlRequest("{\"sessionid\":\"s-1\",\"action\":7}", sessionid, action));
    CHECK_FALSE(RemoteDesktopProtocol::parseControlRequest("{\"sessionid\":\"s-1\",\"action\":\"1\"}", sessionid,
                                                           action));
    CHECK_FALSE(RemoteDesktopProtocol::parseControlRequest("not json", sessionid, action));
}

TEST_CASE("RDP10 buildControlResponse / buildControlNotify 字段完整，fromid 按字符串编码")
{
    {
        std::string json = RemoteDesktopProtocol::buildControlResponse(0, "ok", "s-1", rd_control_grant,
                                                                       rd_control_granted);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["code"].asInt() == 0);
        CHECK(root["msg"].asString() == "ok");
        CHECK(root["sessionid"].asString() == "s-1");
        CHECK(root["action"].asInt() == rd_control_grant);
        CHECK(root["control"].asInt() == rd_control_granted);
    }
    {
        //超过 32 位的 userid：确认按字符串原样写出
        const int64_t bigId = 9000000000001LL;
        std::string json = RemoteDesktopProtocol::buildControlNotify("s-2", rd_control_request, rd_control_requested,
                                                                     bigId, "tom");
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "s-2");
        CHECK(root["action"].asInt() == rd_control_request);
        CHECK(root["control"].asInt() == rd_control_requested);
        REQUIRE(root["fromid"].isString());
        CHECK(root["fromid"].asString() == "9000000000001");
        CHECK(root["fromname"].asString() == "tom");
    }
}

namespace {

//控制权限用例里的两位参与方与一位旁人
const int64_t kRdCtlController = 7101;
const int64_t kRdCtlControlled = 7102;
const int64_t kRdCtlOutsider   = 7103;

//控制权限用例的建会话时刻（Unix 毫秒，取值无意义，只要固定）
const int64_t kRdCtlNow = 9000000;

//建一条已接通的会话（控制端 kRdCtlController、被控端 kRdCtlControlled），返回 sessionid
std::string makeConnectedSession(RemoteDesktopManager& mgr)
{
    std::string sid = mgr.createSession(kRdCtlController, kRdCtlControlled, kRdCtlNow);
    REQUIRE(mgr.markConnected(sid));
    return sid;
}

} // namespace

TEST_CASE("RDM9 新会话的控制状态为未授权；会话未接通时任何控制动作都回 742")
{
    RemoteDesktopManager mgr;
    std::string sid = mgr.createSession(kRdCtlController, kRdCtlControlled, kRdCtlNow);
    RemoteDesktopSessionInfo info;
    REQUIRE(mgr.getSession(sid, info));
    CHECK_FALSE(info.connected);
    CHECK(info.controlState == rd_control_none);

    RemoteDesktopControlResult r;
    CHECK(mgr.applyControl(sid, kRdCtlController, rd_control_request, r) == error_code_rd_control_state);
    CHECK(mgr.applyControl(sid, kRdCtlControlled, rd_control_grant, r) == error_code_rd_control_state);

    //接通之后仍是未授权
    REQUIRE(mgr.markConnected(sid));
    REQUIRE(mgr.getSession(sid, info));
    CHECK(info.connected);
    CHECK(info.controlState == rd_control_none);

    //不存在的会话标记接通失败
    CHECK_FALSE(mgr.markConnected("no-such-session"));
}

TEST_CASE("RDM10 控制端申请：变为申请中并通知被控端；撤回：回到未授权并通知被控端")
{
    RemoteDesktopManager mgr;
    std::string sid = makeConnectedSession(mgr);

    RemoteDesktopControlResult r;
    REQUIRE(mgr.applyControl(sid, kRdCtlController, rd_control_request, r) == error_code_ok);
    CHECK(r.m_control == rd_control_requested);
    CHECK(r.m_notifyUser == kRdCtlControlled);

    //申请中再申请：成功、不重复通知
    REQUIRE(mgr.applyControl(sid, kRdCtlController, rd_control_request, r) == error_code_ok);
    CHECK(r.m_control == rd_control_requested);
    CHECK(r.m_notifyUser == 0);

    REQUIRE(mgr.applyControl(sid, kRdCtlController, rd_control_cancel, r) == error_code_ok);
    CHECK(r.m_control == rd_control_none);
    CHECK(r.m_notifyUser == kRdCtlControlled);

    //没有待处理的申请时撤回：回 742
    CHECK(mgr.applyControl(sid, kRdCtlController, rd_control_cancel, r) == error_code_rd_control_state);
}

TEST_CASE("RDM11 被控端授予：有无申请都可以；拒绝只在申请中有效")
{
    RemoteDesktopManager mgr;
    RemoteDesktopControlResult r;
    RemoteDesktopSessionInfo info;

    //没人申请，被控端直接授予
    {
        std::string sid = makeConnectedSession(mgr);
        REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_grant, r) == error_code_ok);
        CHECK(r.m_control == rd_control_granted);
        CHECK(r.m_notifyUser == kRdCtlController);
        REQUIRE(mgr.getSession(sid, info));
        CHECK(info.controlState == rd_control_granted);

        //已授权时再授予：成功、不重复通知
        REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_grant, r) == error_code_ok);
        CHECK(r.m_notifyUser == 0);
    }
    //申请之后同意
    {
        std::string sid = makeConnectedSession(mgr);
        REQUIRE(mgr.applyControl(sid, kRdCtlController, rd_control_request, r) == error_code_ok);
        REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_grant, r) == error_code_ok);
        CHECK(r.m_control == rd_control_granted);
        CHECK(r.m_notifyUser == kRdCtlController);
    }
    //申请之后拒绝；没有申请时拒绝回 742
    {
        std::string sid = makeConnectedSession(mgr);
        CHECK(mgr.applyControl(sid, kRdCtlControlled, rd_control_deny, r) == error_code_rd_control_state);
        REQUIRE(mgr.applyControl(sid, kRdCtlController, rd_control_request, r) == error_code_ok);
        REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_deny, r) == error_code_ok);
        CHECK(r.m_control == rd_control_none);
        CHECK(r.m_notifyUser == kRdCtlController);
    }
}

TEST_CASE("RDM12 已授权时被控端收回、控制端放弃：回到未授权并通知对方；未授权时回 742")
{
    RemoteDesktopManager mgr;
    RemoteDesktopControlResult r;
    std::string sid = makeConnectedSession(mgr);

    //未授权时收回、放弃都不允许
    CHECK(mgr.applyControl(sid, kRdCtlControlled, rd_control_revoke, r) == error_code_rd_control_state);
    CHECK(mgr.applyControl(sid, kRdCtlController, rd_control_release, r) == error_code_rd_control_state);

    REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_grant, r) == error_code_ok);
    REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_revoke, r) == error_code_ok);
    CHECK(r.m_control == rd_control_none);
    CHECK(r.m_notifyUser == kRdCtlController);

    REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_grant, r) == error_code_ok);
    REQUIRE(mgr.applyControl(sid, kRdCtlController, rd_control_release, r) == error_code_ok);
    CHECK(r.m_control == rd_control_none);
    CHECK(r.m_notifyUser == kRdCtlControlled);
}

TEST_CASE("RDM13 角色与参与方校验：角色不对或不是参与方回 741，会话不存在回 740")
{
    RemoteDesktopManager mgr;
    RemoteDesktopControlResult r;
    std::string sid = makeConnectedSession(mgr);

    //控制端不能授予、拒绝、收回
    CHECK(mgr.applyControl(sid, kRdCtlController, rd_control_grant, r) == error_code_rd_not_participant);
    CHECK(mgr.applyControl(sid, kRdCtlController, rd_control_deny, r) == error_code_rd_not_participant);
    CHECK(mgr.applyControl(sid, kRdCtlController, rd_control_revoke, r) == error_code_rd_not_participant);
    //被控端不能申请、撤回、放弃
    CHECK(mgr.applyControl(sid, kRdCtlControlled, rd_control_request, r) == error_code_rd_not_participant);
    CHECK(mgr.applyControl(sid, kRdCtlControlled, rd_control_cancel, r) == error_code_rd_not_participant);
    CHECK(mgr.applyControl(sid, kRdCtlControlled, rd_control_release, r) == error_code_rd_not_participant);
    //旁人什么都不能做
    CHECK(mgr.applyControl(sid, kRdCtlOutsider, rd_control_request, r) == error_code_rd_not_participant);
    CHECK(mgr.applyControl(sid, kRdCtlOutsider, rd_control_grant, r) == error_code_rd_not_participant);
    //会话不存在
    CHECK(mgr.applyControl("no-such-session", kRdCtlController, rd_control_request, r) == error_code_rd_not_exist);
    //动作越界
    CHECK(mgr.applyControl(sid, kRdCtlController, 0, r) == error_code_bad_request);

    //以上被拒的动作都没有改变控制状态
    RemoteDesktopSessionInfo info;
    REQUIRE(mgr.getSession(sid, info));
    CHECK(info.controlState == rd_control_none);
}

TEST_CASE("RDM14 已授权时控制端再申请：成功、状态不变、不通知被控端")
{
    RemoteDesktopManager mgr;
    RemoteDesktopControlResult r;
    std::string sid = makeConnectedSession(mgr);
    REQUIRE(mgr.applyControl(sid, kRdCtlControlled, rd_control_grant, r) == error_code_ok);

    REQUIRE(mgr.applyControl(sid, kRdCtlController, rd_control_request, r) == error_code_ok);
    CHECK(r.m_control == rd_control_granted);
    CHECK(r.m_notifyUser == 0);
}
