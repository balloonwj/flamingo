/**
 *  P2P 会话管理单元测试（Suite P2PS），test_p2psessionmanager.cpp
 *  覆盖（对应 docs/P2P通信方案.md 第 2 期第 2b 步，2026-10-03 与用户确认的用例）：
 *    - P2PS1 报到登记：版本一致登记成功；版本不符返回 906、不登记；同一连接重复报到以最后一次为准；断开后移除；
 *    - P2PS2 建会话成功：会话号带 p2p- 前缀且不重复；两份推送角色相反、用途与业务会话号相同、对端 uid 与设备标识
 *            互换、中转地址与三项超时取自配置；两枚令牌能验签，角色 11 / 12、会话号相同、uid 与 peeruid 对调；
 *    - P2PS3 建会话被拒：任一方没报到、任一方企业关闭、两端是同一条连接、有人同时进行的会话已达上限，都不建会话；
 *    - P2PS4 信令路由：成员发来的转到对端连接并带发送者 uid；非成员 902；会话不存在 901；结果上报只核对成员身份；
 *    - P2PS5 结束：一方 2305 后会话删除并返回对端连接；一方连接断开时它的全部会话删除并逐个返回对端连接；
 *            之后再对这些会话发信令返回 901；
 *    - P2PS6 联调建会话 2306：开关关闭 907；对端没有已报到的连接 903；企业关闭 900；成功时双方各一份推送、用途为 test；
 *    - P2PS7 某用户最近一次报到的连接；
 *    - P2PS8 用途（第 5 期 5d 补充）：建业务会话要求两端都报到了该用途，否则 908、不建会话；联调会话不检查；
 *            重新报到以最后一次的用途为准。
 *  balloonwj@qq.com   2026-10-03
 */
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/P2PSessionManager.h"
#include "../chatserversrc/Protocol.h"
#include "../utils/RelayToken.h"

namespace {

//测试用的三条连接与三位用户（连接标识只做相等比较，取任意互不相同的整数即可）
const P2PConnKey kConnA = 0x1001;
const P2PConnKey kConnB = 0x2002;
const P2PConnKey kConnC = 0x3003;
const P2PConnKey kConnUnknown = 0x9009;
const int64_t kUserA = 10001;
const int64_t kUserB = 10002;
const int64_t kUserC = 10003;

//测试用的当前时间（毫秒）
const int64_t kNowMs = 1790990000000LL;

//测试用的配置取值
const char* const kTokenKey = "p2p-test-key";
const char* const kRelayHost = "101.37.25.166";
const int kRelayPort = 20003;
const int kConnectTimeoutMs = 3000;
const int kKeepaliveMs = 5000;
const int kLinkTimeoutMs = 15000;
const int kMaxPerUser = 2;

//构造一份测试配置
P2PSessionConfig makeConfig(bool debugOpen)
{
    P2PSessionConfig c;
    c.m_relayTokenKey = kTokenKey;
    c.m_relayHost = kRelayHost;
    c.m_relayPort = kRelayPort;
    c.m_connectTimeoutMs = kConnectTimeoutMs;
    c.m_keepaliveMs = kKeepaliveMs;
    c.m_linkTimeoutMs = kLinkTimeoutMs;
    c.m_maxPerUser = kMaxPerUser;
    c.m_debugOpen = debugOpen;
    return c;
}

//本组用例建业务会话用到的全部用途：夹具里三条连接都报到这些用途，用途检查只在 P2PS8 里单独验证
std::vector<std::string> allPurposes()
{
    std::vector<std::string> v;
    v.push_back("folder");
    v.push_back("call");
    v.push_back("rd");
    return v;
}

//构造一个已配置、三条连接都已报到的管理器
void setupRegistered(P2PSessionManager& mgr, bool debugOpen)
{
    mgr.configure(makeConfig(debugOpen));
    REQUIRE(mgr.registerConnection(kConnA, kUserA, "PC-A", kP2PProtocolVersion, allPurposes()) == error_code_ok);
    REQUIRE(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion, allPurposes()) == error_code_ok);
    REQUIRE(mgr.registerConnection(kConnC, kUserC, "PC-C", kP2PProtocolVersion, allPurposes()) == error_code_ok);
}

//在 A 与 B 之间建一个文件夹传输用途的会话，返回会话号
std::string createAB(P2PSessionManager& mgr, const std::string& feature)
{
    P2PCreateResult r;
    REQUIRE(mgr.tryCreate("folder", feature, kConnA, kConnB, true, true, kNowMs, r) == error_code_ok);
    return r.m_sessionId;
}

} // namespace

TEST_CASE("P2PS1 报到登记：版本、重复报到与断开")
{
    P2PSessionManager mgr;
    mgr.configure(makeConfig(false));

    CHECK_FALSE(mgr.isRegistered(kConnA));
    CHECK(mgr.registerConnection(kConnA, kUserA, "PC-A", kP2PProtocolVersion, allPurposes()) == error_code_ok);
    CHECK(mgr.isRegistered(kConnA));

    //版本不符：906，不登记
    CHECK(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion + 1, allPurposes()) == error_code_p2p_version_unsupported);
    CHECK_FALSE(mgr.isRegistered(kConnB));

    //同一连接重复报到：以最后一次为准（换了设备标识后，推给对端的设备标识随之变化）
    CHECK(mgr.registerConnection(kConnA, kUserA, "PC-A2", kP2PProtocolVersion, allPurposes()) == error_code_ok);
    REQUIRE(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion, allPurposes()) == error_code_ok);
    P2PCreateResult r;
    REQUIRE(mgr.tryCreate("folder", "f-1", kConnA, kConnB, true, true, kNowMs, r) == error_code_ok);
    CHECK(r.m_notifyB.m_peerDeviceId == "PC-A2");

    //断开：移除登记
    mgr.unregisterConnection(kConnA);
    CHECK_FALSE(mgr.isRegistered(kConnA));
    CHECK(mgr.isRegistered(kConnB));
}

TEST_CASE("P2PS2 建会话成功：会话号、两份推送与两枚令牌")
{
    P2PSessionManager mgr;
    setupRegistered(mgr, false);

    P2PCreateResult r;
    REQUIRE(mgr.tryCreate("folder", "10001-10002-1790990000000-3", kConnA, kConnB, true, true, kNowMs, r) == error_code_ok);
    CHECK(r.m_sessionId.compare(0, 4, "p2p-") == 0);
    CHECK(r.m_sessionId.size() <= kP2PSessionIdMaxBytes);
    CHECK(r.m_connA == kConnA);
    CHECK(r.m_connB == kConnB);
    CHECK(mgr.sessionCount() == 1);

    //两份推送：角色相反，会话号、用途、业务会话号相同，对端信息互换，配置项照抄
    const P2PSessionNotify& a = r.m_notifyA;
    const P2PSessionNotify& b = r.m_notifyB;
    CHECK(a.m_sessionId == r.m_sessionId);
    CHECK(b.m_sessionId == r.m_sessionId);
    CHECK(a.m_role == p2p_role_controlling);
    CHECK(b.m_role == p2p_role_controlled);
    CHECK(a.m_purpose == "folder");
    CHECK(b.m_purpose == "folder");
    CHECK(a.m_featureSessionId == "10001-10002-1790990000000-3");
    CHECK(b.m_featureSessionId == a.m_featureSessionId);
    CHECK(a.m_peerUid == kUserB);
    CHECK(b.m_peerUid == kUserA);
    CHECK(a.m_peerDeviceId == "PC-B");
    CHECK(b.m_peerDeviceId == "PC-A");
    CHECK(a.m_relayIp == kRelayHost);
    CHECK(a.m_relayPort == kRelayPort);
    CHECK(a.m_connectTimeoutMs == kConnectTimeoutMs);
    CHECK(a.m_keepaliveMs == kKeepaliveMs);
    CHECK(a.m_linkTimeoutMs == kLinkTimeoutMs);
    CHECK(b.m_linkTimeoutMs == kLinkTimeoutMs);

    //两枚令牌：都能验签，角色 11 / 12，会话号相同，uid 与 peeruid 对调
    RelayTokenPayload ta;
    RelayTokenPayload tb;
    REQUIRE(RelayToken::verify(a.m_token, kTokenKey, kNowMs, ta));
    REQUIRE(RelayToken::verify(b.m_token, kTokenKey, kNowMs, tb));
    CHECK(ta.role == RelayToken::role_p2p_controlling);
    CHECK(tb.role == RelayToken::role_p2p_controlled);
    CHECK(ta.sessionid == r.m_sessionId);
    CHECK(tb.sessionid == r.m_sessionId);
    CHECK(ta.uid == kUserA);
    CHECK(ta.peeruid == kUserB);
    CHECK(tb.uid == kUserB);
    CHECK(tb.peeruid == kUserA);
    CHECK(ta.expire > kNowMs);

    //再建一个：会话号不重复
    P2PCreateResult r2;
    REQUIRE(mgr.tryCreate("call", "f-2", kConnA, kConnC, true, true, kNowMs, r2) == error_code_ok);
    CHECK(r2.m_sessionId != r.m_sessionId);
    CHECK(mgr.sessionCount() == 2);
}

TEST_CASE("P2PS3 建会话被拒：没报到、企业关闭、同一连接、达到上限")
{
    P2PSessionManager mgr;
    setupRegistered(mgr, false);
    P2PCreateResult r;

    //任一方没报到：903
    CHECK(mgr.tryCreate("folder", "f", kConnA, kConnUnknown, true, true, kNowMs, r) == error_code_p2p_peer_offline);
    CHECK(mgr.tryCreate("folder", "f", kConnUnknown, kConnB, true, true, kNowMs, r) == error_code_p2p_peer_offline);

    //任一方企业关闭：900
    CHECK(mgr.tryCreate("folder", "f", kConnA, kConnB, false, true, kNowMs, r) == error_code_p2p_disabled);
    CHECK(mgr.tryCreate("folder", "f", kConnA, kConnB, true, false, kNowMs, r) == error_code_p2p_disabled);

    //两端是同一条连接：报文非法
    CHECK(mgr.tryCreate("folder", "f", kConnA, kConnA, true, true, kNowMs, r) == error_code_bad_request);
    CHECK(mgr.sessionCount() == 0);

    //每人同时进行的会话上限为 2：A 已有两个会话后，再与谁建都被拒（904），B 与 C 之间照常
    createAB(mgr, "f-1");
    createAB(mgr, "f-2");
    CHECK(mgr.tryCreate("folder", "f-3", kConnA, kConnB, true, true, kNowMs, r) == error_code_p2p_too_many);
    CHECK(mgr.tryCreate("folder", "f-3", kConnC, kConnA, true, true, kNowMs, r) == error_code_p2p_too_many);
    CHECK(mgr.sessionCount() == 2);
    CHECK(mgr.sessionCountOfUser(kUserA) == 2);
    //B 也已有两个会话，所以 B 与 C 之间被拒；C 本身没有会话
    CHECK(mgr.tryCreate("folder", "f-4", kConnB, kConnC, true, true, kNowMs, r) == error_code_p2p_too_many);
    CHECK(mgr.sessionCountOfUser(kUserC) == 0);
}

TEST_CASE("P2PS4 信令路由与结果上报的成员核对")
{
    P2PSessionManager mgr;
    setupRegistered(mgr, false);
    const std::string sid = createAB(mgr, "f-1");

    //A 发来：转到 B 的连接，带 A 的 uid；B 发来：转到 A
    P2PRoute route;
    REQUIRE(mgr.routeSignal(sid, kConnA, route) == error_code_ok);
    CHECK(route.m_targetConn == kConnB);
    CHECK(route.m_fromUserId == kUserA);
    REQUIRE(mgr.routeSignal(sid, kConnB, route) == error_code_ok);
    CHECK(route.m_targetConn == kConnA);
    CHECK(route.m_fromUserId == kUserB);

    //非成员：902；会话不存在：901
    CHECK(mgr.routeSignal(sid, kConnC, route) == error_code_p2p_not_member);
    CHECK(mgr.routeSignal("p2p-nope", kConnA, route) == error_code_p2p_session_not_exist);

    //结果上报：成员通过，非成员与不存在的会话被拒，会话状态不变
    CHECK(mgr.checkMember(sid, kConnA) == error_code_ok);
    CHECK(mgr.checkMember(sid, kConnC) == error_code_p2p_not_member);
    CHECK(mgr.checkMember("p2p-nope", kConnA) == error_code_p2p_session_not_exist);
    CHECK(mgr.sessionCount() == 1);
}

TEST_CASE("P2PS5 结束：主动结束与连接断开")
{
    P2PSessionManager mgr;
    setupRegistered(mgr, false);

    //主动结束：只有成员能结束，结束后会话删除、返回对端连接
    const std::string sid1 = createAB(mgr, "f-1");
    P2PEndedSession ended;
    CHECK(mgr.close(sid1, kConnC, ended) == error_code_p2p_not_member);
    CHECK(mgr.sessionCount() == 1);
    REQUIRE(mgr.close(sid1, kConnB, ended) == error_code_ok);
    CHECK(ended.m_sessionId == sid1);
    CHECK(ended.m_peerConn == kConnA);
    CHECK(ended.m_peerUserId == kUserA);
    CHECK(mgr.sessionCount() == 0);
    P2PRoute route;
    CHECK(mgr.routeSignal(sid1, kConnA, route) == error_code_p2p_session_not_exist);
    CHECK(mgr.close(sid1, kConnA, ended) == error_code_p2p_session_not_exist);

    //连接断开：A 的两个会话（与 B、与 C）全部删除，逐个返回对端连接；B 与 C 之间的会话不受影响
    P2PCreateResult r;
    REQUIRE(mgr.tryCreate("folder", "f-2", kConnA, kConnB, true, true, kNowMs, r) == error_code_ok);
    const std::string sidAB = r.m_sessionId;
    REQUIRE(mgr.tryCreate("call", "f-3", kConnC, kConnA, true, true, kNowMs, r) == error_code_ok);
    const std::string sidCA = r.m_sessionId;
    REQUIRE(mgr.tryCreate("rd", "f-4", kConnB, kConnC, true, true, kNowMs, r) == error_code_ok);
    const std::string sidBC = r.m_sessionId;

    std::vector<P2PEndedSession> list = mgr.unregisterConnection(kConnA);
    REQUIRE(list.size() == 2);
    std::set<P2PConnKey> peers;
    std::set<std::string> ids;
    for (size_t i = 0; i < list.size(); ++i)
    {
        peers.insert(list[i].m_peerConn);
        ids.insert(list[i].m_sessionId);
    }
    CHECK(peers.count(kConnB) == 1);
    CHECK(peers.count(kConnC) == 1);
    CHECK(ids.count(sidAB) == 1);
    CHECK(ids.count(sidCA) == 1);
    CHECK(mgr.sessionCount() == 1);
    CHECK(mgr.routeSignal(sidAB, kConnB, route) == error_code_p2p_session_not_exist);
    CHECK(mgr.routeSignal(sidBC, kConnB, route) == error_code_ok);

    //没有会话的连接断开：返回空
    CHECK(mgr.unregisterConnection(kConnUnknown).empty());
}

TEST_CASE("P2PS6 联调建会话：开关、对端不在线、企业关闭与成功")
{
    P2PCreateResult r;

    //开关关闭：907
    {
        P2PSessionManager mgr;
        setupRegistered(mgr, false);
        CHECK(mgr.debugOpen(kConnA, kUserB, true, true, kNowMs, r) == error_code_p2p_debug_disabled);
        CHECK(mgr.sessionCount() == 0);
    }

    P2PSessionManager mgr;
    setupRegistered(mgr, true);

    //对端没有已报到的连接：903；本人或对端企业关闭：900
    const int64_t kUserNobody = 19999;
    CHECK(mgr.debugOpen(kConnA, kUserNobody, true, true, kNowMs, r) == error_code_p2p_peer_offline);
    CHECK(mgr.debugOpen(kConnA, kUserB, false, true, kNowMs, r) == error_code_p2p_disabled);
    CHECK(mgr.debugOpen(kConnA, kUserB, true, false, kNowMs, r) == error_code_p2p_disabled);
    CHECK(mgr.sessionCount() == 0);

    //成功：发起者为发起方，对端那条已报到的连接为应答方，用途为 test
    REQUIRE(mgr.debugOpen(kConnA, kUserB, true, true, kNowMs, r) == error_code_ok);
    CHECK(r.m_connA == kConnA);
    CHECK(r.m_connB == kConnB);
    CHECK(r.m_notifyA.m_purpose == "test");
    CHECK(r.m_notifyB.m_purpose == "test");
    CHECK(r.m_notifyA.m_role == p2p_role_controlling);
    CHECK(r.m_notifyB.m_role == p2p_role_controlled);
    CHECK(r.m_notifyA.m_peerUid == kUserB);
    CHECK(mgr.sessionCount() == 1);
}

TEST_CASE("P2PS8 用途：两端都报到了该用途才建业务会话，联调会话不检查，重新报到以最后一次为准")
{
    P2PSessionManager mgr;
    mgr.configure(makeConfig(true));
    const std::vector<std::string> folderOnly(1, "folder");
    const std::vector<std::string> none;
    REQUIRE(mgr.registerConnection(kConnA, kUserA, "PC-A", kP2PProtocolVersion, folderOnly) == error_code_ok);
    REQUIRE(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion, none) == error_code_ok);
    P2PCreateResult r;

    //B 没有报到 folder：908，两个方向都一样；两端都没报到的用途同样 908；都不建会话
    CHECK(mgr.tryCreate("folder", "f-1", kConnA, kConnB, true, true, kNowMs, r) == error_code_p2p_purpose_unsupported);
    CHECK(mgr.tryCreate("folder", "f-1", kConnB, kConnA, true, true, kNowMs, r) == error_code_p2p_purpose_unsupported);
    CHECK(mgr.tryCreate("call", "f-2", kConnA, kConnB, true, true, kNowMs, r) == error_code_p2p_purpose_unsupported);
    CHECK(mgr.sessionCount() == 0);

    //企业关闭优先于用途：仍是 900
    CHECK(mgr.tryCreate("folder", "f-1", kConnA, kConnB, false, true, kNowMs, r) == error_code_p2p_disabled);

    //联调会话（用途 test）不检查用途
    CHECK(mgr.debugOpen(kConnA, kUserB, true, true, kNowMs, r) == error_code_ok);
    CHECK(mgr.sessionCount() == 1);

    //B 重新报到、带上 folder：可以建
    REQUIRE(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion, folderOnly) == error_code_ok);
    CHECK(mgr.tryCreate("folder", "f-3", kConnA, kConnB, true, true, kNowMs, r) == error_code_ok);
    CHECK(r.m_notifyA.m_purpose == "folder");

    //B 再次报到、不带 folder（注销了业务方）：又不能建，已有的会话不受影响
    REQUIRE(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion, none) == error_code_ok);
    CHECK(mgr.tryCreate("folder", "f-4", kConnA, kConnB, true, true, kNowMs, r) == error_code_p2p_purpose_unsupported);
    CHECK(mgr.sessionCount() == 2);
}

TEST_CASE("P2PS7 某用户最近一次报到的连接（第 5 期：业务会话按用户找连接）")
{
    P2PSessionManager mgr;
    setupRegistered(mgr, false);
    CHECK(mgr.latestConnectionOf(kUserA) == kConnA);
    CHECK(mgr.latestConnectionOf(kUserB) == kConnB);
    //没有已报到连接的用户：0
    const int64_t kUserNobody = 19999;
    CHECK(mgr.latestConnectionOf(kUserNobody) == 0);

    //同一用户第二条连接报到：取最近的那条；它断开后回到前一条
    const P2PConnKey kConnA2 = 0x1002;
    REQUIRE(mgr.registerConnection(kConnA2, kUserA, "PC-A-2", kP2PProtocolVersion, allPurposes()) == error_code_ok);
    CHECK(mgr.latestConnectionOf(kUserA) == kConnA2);
    mgr.unregisterConnection(kConnA2);
    CHECK(mgr.latestConnectionOf(kUserA) == kConnA);
    mgr.unregisterConnection(kConnA);
    CHECK(mgr.latestConnectionOf(kUserA) == 0);
}

TEST_CASE("P2PS9 预检（2026-10-04 单文件直传）：checkCreate 与 tryCreate 的返回码一致，但不建会话、不占名额")
{
    P2PSessionManager mgr;
    mgr.configure(makeConfig(false));
    const std::vector<std::string> withFile(1, "file");
    const std::vector<std::string> none;
    P2PCreateResult r;

    //一端没报到：两者都回 peer_offline
    REQUIRE(mgr.registerConnection(kConnA, kUserA, "PC-A", kP2PProtocolVersion, withFile) == error_code_ok);
    CHECK(mgr.checkCreate("file", kConnA, kConnB, true, true) == error_code_p2p_peer_offline);

    //B 报到了但没有 file 用途：908
    REQUIRE(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion, none) == error_code_ok);
    CHECK(mgr.checkCreate("file", kConnA, kConnB, true, true) == error_code_p2p_purpose_unsupported);
    CHECK(mgr.checkCreate("file", kConnA, kConnB, true, true) ==
          mgr.tryCreate("file", "d-0", kConnA, kConnB, true, true, kNowMs, r));

    //企业关闭优先于用途：900
    CHECK(mgr.checkCreate("file", kConnA, kConnB, true, false) == error_code_p2p_disabled);
    //同一条连接：3
    CHECK(mgr.checkCreate("file", kConnA, kConnA, true, true) == error_code_bad_request);

    //B 带上 file 后能建；预检不建会话
    REQUIRE(mgr.registerConnection(kConnB, kUserB, "PC-B", kP2PProtocolVersion, withFile) == error_code_ok);
    CHECK(mgr.checkCreate("file", kConnA, kConnB, true, true) == error_code_ok);
    CHECK(mgr.sessionCount() == 0);

    //名额（每人 kMaxPerUser 个）用满之后预检回 too_many，且预检本身不占名额
    for (int i = 0; i < kMaxPerUser; ++i)
    {
        CHECK(mgr.checkCreate("file", kConnA, kConnB, true, true) == error_code_ok);
        REQUIRE(mgr.tryCreate("file", "d-" + std::to_string(i + 1), kConnA, kConnB, true, true, kNowMs, r) == error_code_ok);
    }
    CHECK(mgr.sessionCount() == (size_t)kMaxPerUser);
    CHECK(mgr.checkCreate("file", kConnA, kConnB, true, true) == error_code_p2p_too_many);
}
