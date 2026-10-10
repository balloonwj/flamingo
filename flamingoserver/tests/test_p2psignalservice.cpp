/**
 *  P2P 信令处理单元测试（Suite P2PSV），test_p2psignalservice.cpp
 *  覆盖（对应 docs/P2P通信方案.md 第 2 期第 2c-1 步，2026-10-03 与用户确认的用例）：
 *    - P2PSV1 2301：版本一致时登记，应答 code 0、enabled 取本人企业开关、probe 取自配置；版本不符 906 不登记；包体非法 3；
 *    - P2PSV2 2303：成功时一帧转给对端连接（带 fromuid 与原样 data），一帧回本人（code 0、同 seq）；
 *             非成员 902、不存在 901、超长 905、格式错 3，都只回本人；
 *    - P2PSV3 2304：成员回 0；非成员或格式错回对应错误码；会话不变；
 *    - P2PSV4 2305：成员发来时删除会话、把原因与错误码原样推给对端、回本人 0；非成员 902；
 *    - P2PSV5 2306：成功时给双方各推一份 2302（角色不同），回本人 code 0 与会话号；开关关 907；对端不在线 903；企业关 900；
 *    - P2PSV6 断线：给仍在线的每个对端推 2305（原因 3、错误码 903）；没有会话时不推；
 *    - P2PSV7 业务建会话：两端按用户找连接，成功时推两份 2302，失败时不推；
 *    - P2PSV8 用途（第 5 期 5d 补充）：purposes 不合法时 2301 回 3、不登记；一端没报到该用途时业务建会话回 908、
 *             不推；那一端重新报到带上该用途后可以建；
 *    - P2PSV9 按指定连接建会话（第 6 期，文件共享用）：绑定给定的连接而不是该用户最近报到的那条，2302 推给给定连接；
 *             给定连接没报到 903、用途不支持 908、企业关闭 900，都不推。
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/P2PSignalService.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//把 JSON 文本解析成对象，失败时让用例失败
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

//三条连接与三位用户
const P2PConnKey kConnA = 0x1001;
const P2PConnKey kConnB = 0x2002;
const P2PConnKey kConnC = 0x3003;
const int64_t kUserA = 10001;
const int64_t kUserB = 10002;
const int64_t kUserC = 10003;

//请求的 seq 与当前时间（毫秒）
const uint32_t kSeq = 77;
const int64_t kNowMs = 1790990000000LL;

//配置里的地址探测端口
const int kProbePort1 = 20004;
const int kProbePort2 = 20005;

//假的企业开关：按 userid 查，没设过的一律开启
class FakeGate : public IP2PEnterpriseGate
{
public:
    virtual bool isP2PEnabledForUser(int64_t userId) override
    {
        std::map<int64_t, bool>::const_iterator it = m_enabled.find(userId);
        return it == m_enabled.end() ? true : it->second;
    }

    std::map<int64_t, bool> m_enabled;   //userid → 该用户所在企业是否开启 P2P
};

//构造一份请求上下文
P2PRequestContext ctxOf(P2PConnKey conn, int64_t userId, const char* deviceId)
{
    P2PRequestContext c;
    c.m_conn = conn;
    c.m_userId = userId;
    c.m_deviceId = deviceId;
    c.m_seq = kSeq;
    c.m_nowMs = kNowMs;
    return c;
}

//配置好管理器（开启 2306、两个地址探测端口）
void configure(P2PSessionManager& mgr)
{
    P2PSessionConfig cfg;
    cfg.m_relayTokenKey = "k";
    cfg.m_relayHost = "101.37.25.166";
    cfg.m_relayPort = 20003;
    cfg.m_debugOpen = true;
    P2PProbeAddress p;
    p.m_ip = "101.37.25.166";
    p.m_port = kProbePort1;
    cfg.m_probes.push_back(p);
    p.m_port = kProbePort2;
    cfg.m_probes.push_back(p);
    mgr.configure(cfg);
}

//本组用例报到时带的用途：P2PSV7 用 folder 建业务会话
const char* const kRegisterWithFolder = "{\"version\":1,\"purposes\":[\"folder\"]}";

//让一条连接报到（带用途 folder）
void registerConn(P2PSignalService& svc, P2PConnKey conn, int64_t userId, const char* deviceId)
{
    std::vector<P2POutFrame> out;
    REQUIRE(svc.handle(cmd_p2p_register, ctxOf(conn, userId, deviceId), kRegisterWithFolder, out) == error_code_ok);
}

//经 2306 在发起者与对端之间建会话，返回会话号
std::string openDebug(P2PSignalService& svc, P2PConnKey fromConn, int64_t fromUser, int64_t peerUser)
{
    std::vector<P2POutFrame> out;
    char body[64];
    snprintf(body, sizeof(body), "{\"peeruid\":\"%lld\"}", static_cast<long long>(peerUser));
    REQUIRE(svc.handle(cmd_p2p_debug_open, ctxOf(fromConn, fromUser, ""), body, out) == error_code_ok);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].m_cmd == cmd_p2p_debug_open)
            return parseJson(out[i].m_body)["p2psessionid"].asString();
    }
    FAIL("no 2306 response");
    return std::string();
}

//在 out 里找发给 conn、命令为 cmd 的帧；找不到返回 NULL
const P2POutFrame* findFrame(const std::vector<P2POutFrame>& out, P2PConnKey conn, uint16_t cmd)
{
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].m_conn == conn && out[i].m_cmd == cmd)
            return &out[i];
    }
    return NULL;
}

//断言 out 里只有一帧回给本人的应答，结果码为 code，并返回其包体
Json::Value onlySelfResponse(const std::vector<P2POutFrame>& out, P2PConnKey self, uint16_t cmd, int code)
{
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_conn == self);
    CHECK(out[0].m_cmd == cmd);
    CHECK(out[0].m_seq == kSeq);
    Json::Value v = parseJson(out[0].m_body);
    CHECK(v["code"].asInt() == code);
    return v;
}

} // namespace

TEST_CASE("P2PSV1 2301 报到：登记、企业开关、版本与包体")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    gate.m_enabled[kUserB] = false;
    P2PSignalService svc(mgr, gate);
    std::vector<P2POutFrame> out;

    //版本一致、企业开启：登记，应答带两个地址探测端口
    CHECK(svc.handle(cmd_p2p_register, ctxOf(kConnA, kUserA, "PC-A"), "{\"version\":1}", out) == error_code_ok);
    CHECK(mgr.isRegistered(kConnA));
    Json::Value v = onlySelfResponse(out, kConnA, cmd_p2p_register, error_code_ok);
    CHECK(v["enabled"].asBool());
    REQUIRE(v["probe"].size() == 2);
    CHECK(v["probe"][1]["port"].asInt() == kProbePort2);

    //企业关闭：照样登记（开关可能在运行中打开，建会话时再查），应答 enabled 为 false、probe 为空
    out.clear();
    CHECK(svc.handle(cmd_p2p_register, ctxOf(kConnB, kUserB, "PC-B"), "{\"version\":1}", out) == error_code_ok);
    v = onlySelfResponse(out, kConnB, cmd_p2p_register, error_code_ok);
    CHECK_FALSE(v["enabled"].asBool());
    CHECK(v["probe"].size() == 0);

    //版本不符：906，不登记
    out.clear();
    CHECK(svc.handle(cmd_p2p_register, ctxOf(kConnC, kUserC, "PC-C"), "{\"version\":99}", out) == error_code_p2p_version_unsupported);
    onlySelfResponse(out, kConnC, cmd_p2p_register, error_code_p2p_version_unsupported);
    CHECK_FALSE(mgr.isRegistered(kConnC));

    //包体非法：3，不登记
    out.clear();
    CHECK(svc.handle(cmd_p2p_register, ctxOf(kConnC, kUserC, "PC-C"), "not json", out) == error_code_bad_request);
    onlySelfResponse(out, kConnC, cmd_p2p_register, error_code_bad_request);
    CHECK_FALSE(mgr.isRegistered(kConnC));
}

TEST_CASE("P2PSV2 2303 信令：转发给对端并回本人，各类失败只回本人")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    registerConn(svc, kConnA, kUserA, "PC-A");
    registerConn(svc, kConnB, kUserB, "PC-B");
    registerConn(svc, kConnC, kUserC, "PC-C");
    const std::string sid = openDebug(svc, kConnA, kUserA, kUserB);

    //成功：转给 B（seq 0、fromuid 为 A、data 原样），回 A（code 0、同 seq）
    std::vector<P2POutFrame> out;
    const std::string body = "{\"p2psessionid\":\"" + sid + "\",\"data\":{\"pubkey\":\"QUJD\"}}";
    CHECK(svc.handle(cmd_p2p_signal, ctxOf(kConnA, kUserA, ""), body, out) == error_code_ok);
    REQUIRE(out.size() == 2);
    const P2POutFrame* fwd = findFrame(out, kConnB, cmd_p2p_signal);
    REQUIRE(fwd != NULL);
    CHECK(fwd->m_seq == 0);
    Json::Value f = parseJson(fwd->m_body);
    CHECK(f["p2psessionid"].asString() == sid);
    CHECK(f["fromuid"].asString() == "10001");
    CHECK(f["data"]["pubkey"].asString() == "QUJD");
    const P2POutFrame* resp = findFrame(out, kConnA, cmd_p2p_signal);
    REQUIRE(resp != NULL);
    CHECK(resp->m_seq == kSeq);
    CHECK(parseJson(resp->m_body)["code"].asInt() == error_code_ok);

    //非成员（C）：902；会话不存在：901；格式错：3；超长：905。都只回本人
    out.clear();
    CHECK(svc.handle(cmd_p2p_signal, ctxOf(kConnC, kUserC, ""), body, out) == error_code_p2p_not_member);
    onlySelfResponse(out, kConnC, cmd_p2p_signal, error_code_p2p_not_member);
    out.clear();
    CHECK(svc.handle(cmd_p2p_signal, ctxOf(kConnA, kUserA, ""), "{\"p2psessionid\":\"p2p-nope\",\"data\":{}}", out)
          == error_code_p2p_session_not_exist);
    onlySelfResponse(out, kConnA, cmd_p2p_signal, error_code_p2p_session_not_exist);
    out.clear();
    CHECK(svc.handle(cmd_p2p_signal, ctxOf(kConnA, kUserA, ""), "{\"data\":{}}", out) == error_code_bad_request);
    onlySelfResponse(out, kConnA, cmd_p2p_signal, error_code_bad_request);
    out.clear();
    const std::string huge = "{\"p2psessionid\":\"" + sid + "\",\"data\":{\"k\":\"" + std::string(kP2PSignalDataMaxBytes, 'a') + "\"}}";
    CHECK(svc.handle(cmd_p2p_signal, ctxOf(kConnA, kUserA, ""), huge, out) == error_code_p2p_signal_too_large);
    onlySelfResponse(out, kConnA, cmd_p2p_signal, error_code_p2p_signal_too_large);
}

TEST_CASE("P2PSV3 2304 结果上报：成员回 0，非成员与格式错回错误码，会话不变")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    registerConn(svc, kConnA, kUserA, "PC-A");
    registerConn(svc, kConnB, kUserB, "PC-B");
    registerConn(svc, kConnC, kUserC, "PC-C");
    const std::string sid = openDebug(svc, kConnA, kUserA, kUserB);

    std::vector<P2POutFrame> out;
    const std::string ok = "{\"p2psessionid\":\"" + sid + "\",\"ok\":true,\"path\":1,\"elapsedms\":12}";
    CHECK(svc.handle(cmd_p2p_result, ctxOf(kConnB, kUserB, ""), ok, out) == error_code_ok);
    onlySelfResponse(out, kConnB, cmd_p2p_result, error_code_ok);

    out.clear();
    CHECK(svc.handle(cmd_p2p_result, ctxOf(kConnC, kUserC, ""), ok, out) == error_code_p2p_not_member);
    onlySelfResponse(out, kConnC, cmd_p2p_result, error_code_p2p_not_member);

    out.clear();
    CHECK(svc.handle(cmd_p2p_result, ctxOf(kConnA, kUserA, ""), "{\"p2psessionid\":\"" + sid + "\",\"ok\":true}", out)
          == error_code_bad_request);
    onlySelfResponse(out, kConnA, cmd_p2p_result, error_code_bad_request);
    CHECK(mgr.sessionCount() == 1);
}

TEST_CASE("P2PSV4 2305 结束：删除会话、推给对端、回本人；非成员被拒")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    registerConn(svc, kConnA, kUserA, "PC-A");
    registerConn(svc, kConnB, kUserB, "PC-B");
    registerConn(svc, kConnC, kUserC, "PC-C");
    const std::string sid = openDebug(svc, kConnA, kUserA, kUserB);
    const std::string body = "{\"p2psessionid\":\"" + sid + "\",\"reason\":2,\"errcode\":904}";

    //非成员：902，会话不变
    std::vector<P2POutFrame> out;
    CHECK(svc.handle(cmd_p2p_close, ctxOf(kConnC, kUserC, ""), body, out) == error_code_p2p_not_member);
    onlySelfResponse(out, kConnC, cmd_p2p_close, error_code_p2p_not_member);
    CHECK(mgr.sessionCount() == 1);

    //成员 B：删除会话，推给 A（原因与错误码原样），回 B
    out.clear();
    CHECK(svc.handle(cmd_p2p_close, ctxOf(kConnB, kUserB, ""), body, out) == error_code_ok);
    CHECK(mgr.sessionCount() == 0);
    REQUIRE(out.size() == 2);
    const P2POutFrame* push = findFrame(out, kConnA, cmd_p2p_close);
    REQUIRE(push != NULL);
    CHECK(push->m_seq == 0);
    Json::Value p = parseJson(push->m_body);
    CHECK(p["p2psessionid"].asString() == sid);
    CHECK(p["reason"].asInt() == p2p_close_connect_fail);
    CHECK(p["errcode"].asInt() == error_code_p2p_too_many);
    const P2POutFrame* resp = findFrame(out, kConnB, cmd_p2p_close);
    REQUIRE(resp != NULL);
    CHECK(resp->m_seq == kSeq);
    CHECK(parseJson(resp->m_body)["code"].asInt() == error_code_ok);
}

TEST_CASE("P2PSV5 2306 联调建会话：成功、开关关闭、对端不在线、企业关闭")
{
    //开关关闭：907
    {
        P2PSessionManager mgr;
        P2PSessionConfig cfg;
        cfg.m_debugOpen = false;
        mgr.configure(cfg);
        FakeGate gate;
        P2PSignalService svc(mgr, gate);
        registerConn(svc, kConnA, kUserA, "PC-A");
        registerConn(svc, kConnB, kUserB, "PC-B");
        std::vector<P2POutFrame> out;
        CHECK(svc.handle(cmd_p2p_debug_open, ctxOf(kConnA, kUserA, ""), "{\"peeruid\":\"10002\"}", out)
              == error_code_p2p_debug_disabled);
        onlySelfResponse(out, kConnA, cmd_p2p_debug_open, error_code_p2p_debug_disabled);
    }

    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    registerConn(svc, kConnA, kUserA, "PC-A");
    registerConn(svc, kConnB, kUserB, "PC-B");

    //对端不在线：903
    std::vector<P2POutFrame> out;
    CHECK(svc.handle(cmd_p2p_debug_open, ctxOf(kConnA, kUserA, ""), "{\"peeruid\":\"19999\"}", out)
          == error_code_p2p_peer_offline);
    onlySelfResponse(out, kConnA, cmd_p2p_debug_open, error_code_p2p_peer_offline);

    //对端企业关闭：900
    gate.m_enabled[kUserB] = false;
    out.clear();
    CHECK(svc.handle(cmd_p2p_debug_open, ctxOf(kConnA, kUserA, ""), "{\"peeruid\":\"10002\"}", out) == error_code_p2p_disabled);
    onlySelfResponse(out, kConnA, cmd_p2p_debug_open, error_code_p2p_disabled);
    gate.m_enabled[kUserB] = true;

    //成功：回 A 会话号；A 收到发起方的 2302，B 收到应答方的 2302
    out.clear();
    CHECK(svc.handle(cmd_p2p_debug_open, ctxOf(kConnA, kUserA, ""), "{\"peeruid\":\"10002\"}", out) == error_code_ok);
    REQUIRE(out.size() == 3);
    const P2POutFrame* resp = findFrame(out, kConnA, cmd_p2p_debug_open);
    REQUIRE(resp != NULL);
    CHECK(resp->m_seq == kSeq);
    Json::Value r = parseJson(resp->m_body);
    CHECK(r["code"].asInt() == error_code_ok);
    const std::string sid = r["p2psessionid"].asString();
    CHECK_FALSE(sid.empty());
    const P2POutFrame* toA = findFrame(out, kConnA, cmd_p2p_session_notify);
    const P2POutFrame* toB = findFrame(out, kConnB, cmd_p2p_session_notify);
    REQUIRE(toA != NULL);
    REQUIRE(toB != NULL);
    CHECK(toA->m_seq == 0);
    Json::Value na = parseJson(toA->m_body);
    Json::Value nb = parseJson(toB->m_body);
    CHECK(na["p2psessionid"].asString() == sid);
    CHECK(na["role"].asInt() == p2p_role_controlling);
    CHECK(nb["role"].asInt() == p2p_role_controlled);
    CHECK(na["purpose"].asString() == "test");
    CHECK(na["peeruid"].asString() == "10002");
    CHECK(nb["peeruid"].asString() == "10001");
}

TEST_CASE("P2PSV6 断线：给仍在线的对端推 2305，没有会话时不推")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    registerConn(svc, kConnA, kUserA, "PC-A");
    registerConn(svc, kConnB, kUserB, "PC-B");
    registerConn(svc, kConnC, kUserC, "PC-C");
    const std::string sidAB = openDebug(svc, kConnA, kUserA, kUserB);
    const std::string sidCA = openDebug(svc, kConnC, kUserC, kUserA);

    //A 断开：B、C 各收到一帧 2305（原因 3、错误码 903）
    std::vector<P2POutFrame> out;
    svc.onDisconnect(kConnA, out);
    REQUIRE(out.size() == 2);
    const P2POutFrame* toB = findFrame(out, kConnB, cmd_p2p_close);
    const P2POutFrame* toC = findFrame(out, kConnC, cmd_p2p_close);
    REQUIRE(toB != NULL);
    REQUIRE(toC != NULL);
    Json::Value b = parseJson(toB->m_body);
    CHECK(b["p2psessionid"].asString() == sidAB);
    CHECK(b["reason"].asInt() == p2p_close_peer_offline);
    CHECK(b["errcode"].asInt() == error_code_p2p_peer_offline);
    CHECK(parseJson(toC->m_body)["p2psessionid"].asString() == sidCA);
    CHECK(mgr.sessionCount() == 0);
    CHECK_FALSE(mgr.isRegistered(kConnA));

    //没有会话的连接断开：不推
    out.clear();
    svc.onDisconnect(kConnB, out);
    CHECK(out.empty());
}

TEST_CASE("P2PSV7 业务建会话：两端按用户找连接，成功时推两份 2302，失败时不推")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    registerConn(svc, kConnA, kUserA, "PC-A");
    registerConn(svc, kConnB, kUserB, "PC-B");
    const char* const kFeature = "ft-0001";

    //成功：A 为发起方、B 为应答方，用途与业务会话号原样写进 2302
    std::string sid;
    std::vector<P2POutFrame> out;
    CHECK(svc.createForFeature("folder", kFeature, kUserA, kUserB, kNowMs, sid, out) == error_code_ok);
    CHECK_FALSE(sid.empty());
    REQUIRE(out.size() == 2);
    const P2POutFrame* toA = findFrame(out, kConnA, cmd_p2p_session_notify);
    const P2POutFrame* toB = findFrame(out, kConnB, cmd_p2p_session_notify);
    REQUIRE(toA != NULL);
    REQUIRE(toB != NULL);
    Json::Value na = parseJson(toA->m_body);
    Json::Value nb = parseJson(toB->m_body);
    CHECK(na["p2psessionid"].asString() == sid);
    CHECK(nb["p2psessionid"].asString() == sid);
    CHECK(na["role"].asInt() == p2p_role_controlling);
    CHECK(nb["role"].asInt() == p2p_role_controlled);
    CHECK(na["purpose"].asString() == "folder");
    CHECK(na["featuresessionid"].asString() == kFeature);
    CHECK(na["peeruid"].asString() == "10002");
    CHECK(nb["peeruid"].asString() == "10001");
    CHECK(mgr.sessionCount() == 1);

    //一方没有已报到的连接（老客户端、手机端或不在线）：903，不推、不建会话
    out.clear();
    CHECK(svc.createForFeature("folder", kFeature, kUserA, kUserC, kNowMs, sid, out) == error_code_p2p_peer_offline);
    CHECK(sid.empty());
    CHECK(out.empty());

    //一方企业关闭：900
    gate.m_enabled[kUserB] = false;
    CHECK(svc.createForFeature("folder", kFeature, kUserA, kUserB, kNowMs, sid, out) == error_code_p2p_disabled);
    CHECK(out.empty());
    CHECK(mgr.sessionCount() == 1);
}

TEST_CASE("P2PSV8 用途：purposes 不合法时报到失败，一端没报到该用途时业务建会话回 908")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    std::vector<P2POutFrame> out;

    //purposes 不是数组：3，不登记
    CHECK(svc.handle(cmd_p2p_register, ctxOf(kConnC, kUserC, "PC-C"), "{\"version\":1,\"purposes\":\"folder\"}", out)
          == error_code_bad_request);
    onlySelfResponse(out, kConnC, cmd_p2p_register, error_code_bad_request);
    CHECK_FALSE(mgr.isRegistered(kConnC));

    //A 带 folder、B 不带用途（第 5 期之前的报到包体）：业务建会话 908，不推、不建会话
    registerConn(svc, kConnA, kUserA, "PC-A");
    out.clear();
    REQUIRE(svc.handle(cmd_p2p_register, ctxOf(kConnB, kUserB, "PC-B"), "{\"version\":1}", out) == error_code_ok);
    std::string sid;
    out.clear();
    CHECK(svc.createForFeature("folder", "ft-0002", kUserA, kUserB, kNowMs, sid, out) == error_code_p2p_purpose_unsupported);
    CHECK(sid.empty());
    CHECK(out.empty());
    CHECK(mgr.sessionCount() == 0);

    //B 重新报到、带上 folder：可以建，推两份 2302
    registerConn(svc, kConnB, kUserB, "PC-B");
    CHECK(svc.createForFeature("folder", "ft-0002", kUserA, kUserB, kNowMs, sid, out) == error_code_ok);
    CHECK_FALSE(sid.empty());
    CHECK(out.size() == 2);
}

TEST_CASE("P2PSV9 按指定连接建会话：绑定给定的连接，失败时不推")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    registerConn(svc, kConnA, kUserA, "PC-A");
    registerConn(svc, kConnB, kUserB, "PC-B");
    //A 的第二条连接后报到，是 A 最近报到的那条；业务指定用前一条
    const P2PConnKey kConnA2 = 0x1002;
    registerConn(svc, kConnA2, kUserA, "PC-A-2");
    const char* const kTransfer = "share-1-1759370000000-t1";

    std::string sid;
    std::vector<P2POutFrame> out;
    CHECK(svc.createForConnections("folder", kTransfer, kConnB, kUserB, kConnA, kUserA, kNowMs, sid, out) == error_code_ok);
    CHECK_FALSE(sid.empty());
    REQUIRE(out.size() == 2);
    const P2POutFrame* toB = findFrame(out, kConnB, cmd_p2p_session_notify);
    const P2POutFrame* toA = findFrame(out, kConnA, cmd_p2p_session_notify);
    REQUIRE(toB != NULL);
    REQUIRE(toA != NULL);
    CHECK(findFrame(out, kConnA2, cmd_p2p_session_notify) == NULL);
    CHECK(parseJson(toB->m_body)["role"].asInt() == p2p_role_controlling);
    CHECK(parseJson(toA->m_body)["role"].asInt() == p2p_role_controlled);
    CHECK(parseJson(toA->m_body)["featuresessionid"].asString() == kTransfer);

    //给定的连接没有报到过：903，不推
    const P2PConnKey kConnNobody = 0x7007;
    out.clear();
    CHECK(svc.createForConnections("folder", kTransfer, kConnB, kUserB, kConnNobody, kUserA, kNowMs, sid, out)
          == error_code_p2p_peer_offline);
    CHECK(sid.empty());
    CHECK(out.empty());

    //用途没有报到：908
    CHECK(svc.createForConnections("fileshare", kTransfer, kConnB, kUserB, kConnA, kUserA, kNowMs, sid, out)
          == error_code_p2p_purpose_unsupported);
    CHECK(out.empty());

    //一方企业关闭：900
    gate.m_enabled[kUserA] = false;
    CHECK(svc.createForConnections("folder", kTransfer, kConnB, kUserB, kConnA, kUserA, kNowMs, sid, out)
          == error_code_p2p_disabled);
    CHECK(out.empty());
    CHECK(mgr.sessionCount() == 1);
}

TEST_CASE("P2PSV10 预检（2026-10-04 单文件直传）：checkForFeature 按用户找连接、查开关与用途，不建会话、不推帧")
{
    P2PSessionManager mgr;
    configure(mgr);
    FakeGate gate;
    P2PSignalService svc(mgr, gate);
    std::vector<P2POutFrame> out;

    //两端都没报到：peer_offline
    CHECK(svc.checkForFeature("file", kUserA, kUserB) == error_code_p2p_peer_offline);

    //A 报到 file，B 只报到 folder（旧版客户端）：908
    REQUIRE(svc.handle(cmd_p2p_register, ctxOf(kConnA, kUserA, "PC-A"), "{\"version\":1,\"purposes\":[\"file\",\"folder\"]}", out)
            == error_code_ok);
    REQUIRE(svc.handle(cmd_p2p_register, ctxOf(kConnB, kUserB, "PC-B"), kRegisterWithFolder, out) == error_code_ok);
    out.clear();
    CHECK(svc.checkForFeature("file", kUserA, kUserB) == error_code_p2p_purpose_unsupported);

    //B 也报到 file：能建；但不建会话、不推帧
    REQUIRE(svc.handle(cmd_p2p_register, ctxOf(kConnB, kUserB, "PC-B"), "{\"version\":1,\"purposes\":[\"file\"]}", out)
            == error_code_ok);
    out.clear();
    CHECK(svc.checkForFeature("file", kUserA, kUserB) == error_code_ok);
    CHECK(mgr.sessionCount() == 0);
    CHECK(out.empty());

    //B 所在企业关闭了 P2P：900
    gate.m_enabled[kUserB] = false;
    CHECK(svc.checkForFeature("file", kUserA, kUserB) == error_code_p2p_disabled);
    gate.m_enabled[kUserB] = true;

    //与 createForFeature 结论一致
    std::string sid;
    CHECK(svc.createForFeature("file", "d-1", kUserA, kUserB, kNowMs, sid, out) == error_code_ok);
}
