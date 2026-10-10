/**
 *  单聊密聊服务端会话管理单元测试（Suite SC），test_secretchat.cpp
 *  覆盖 docs/密聊方案.md 第 8 节的服务端用例（2026-10-07 按用户要求自行确定）：
 *    - SC1 报到：包体非法 3、版本不符 1208、成功时 enabled 取本人企业开关并登记连接；
 *    - SC2 邀请与同意：应答带会话号与有效期，2603 推给被邀请人全部已报到的连接；同意后发起方收到带公钥与
 *          防截图级别的「已建立」，同意的那条连接收到「已建立」，其余连接收到「已在其他设备处理」；
 *    - SC3 忽略：发起方收到「未接受」，被邀请人其余连接收到「已在其他设备处理」，会话删除；
 *    - SC4 超时：到期后发起方与被邀请人都收到「未接受」，再应答回 1202；
 *    - SC5 撤回：发起方撤回后被邀请人收到「已取消」；不是发起方回 1204；
 *    - SC6 冲突：同一对用户已有待应答或进行中的密聊时回 1203（不分谁发起）；
 *    - SC7 上限：每人每分钟邀请数、每人同时进行的会话数超出时回 1206；
 *    - SC8 校验：对方没有已报到的连接 1201、被拉黑 1207、任一方企业关闭 1200、邀请自己与公钥非法 3、
 *          发起方连接没报到 1204；
 *    - SC9 转发：只转给对方绑定的那条连接并带服务端时间；字段非法 3、非成员 1204、超长 1205、超出每秒帧数 1206，
 *          下一秒恢复；
 *    - SC10 结束：一方结束后另一方收到「已结束、对方退出」，重复结束回成功，之后再发回 1202；
 *    - SC11 断线：进行中的另一方收到「已结束、对方离线」；发起方断开时被邀请人收到「已取消」；被邀请人最后一条
 *           连接断开时发起方收到「未接受」；
 *    - SC12 企业关闭开关：该企业成员参与的会话双方都收到「已结束、管理员已关闭」，其它企业的会话不受影响；
 *    - SC13 防截图级别：密聊中重新报到且级别变了，对方收到状态 6 与新级别；
 *    - SC14 号段判定；
 *    - SC15 待应答时被邀请人结束等同忽略。
 *  balloonwj@qq.com   2026-10-07
 */
#include <stdio.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/SecretChatManager.h"
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

//四条连接：A 一条，B 两条（模拟同一用户两台已报到的设备），C 一条
const SecretConnKey kConnA = 0x1001;
const SecretConnKey kConnB1 = 0x2001;
const SecretConnKey kConnB2 = 0x2002;
const SecretConnKey kConnC = 0x3001;
const int64_t kUserA = 10001;
const int64_t kUserB = 10002;
const int64_t kUserC = 10003;

//A、B 属于企业 7，C 属于企业 8
const int64_t kEnterprise7 = 7;
const int64_t kEnterprise8 = 8;

//请求的 seq 与起始时间（毫秒）
const uint32_t kSeq = 55;
const int64_t kNowMs = 1790990000000LL;

//合法的 X25519 公钥 base64（44 个字符）与 nonce base64（32 个字符）
const char* const kPubA = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
const char* const kPubB = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBA=";
const char* const kNonce = "NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN";

//假的查询：企业开关按 userid 设，没设过的一律开启；所属企业按表；黑名单为 (owner, target) 集合
class FakeGate : public ISecretChatGate
{
public:
    virtual bool isSecretChatEnabledForUser(int64_t userId) override
    {
        std::map<int64_t, bool>::const_iterator it = m_enabled.find(userId);
        return it == m_enabled.end() ? true : it->second;
    }

    virtual int64_t enterpriseOfUser(int64_t userId) override
    {
        if (userId == kUserC)
            return kEnterprise8;
        return kEnterprise7;
    }

    virtual bool isBlockedBy(int64_t ownerId, int64_t targetId) override
    {
        return m_blocked.find(std::make_pair(ownerId, targetId)) != m_blocked.end();
    }

    std::map<int64_t, bool>                    m_enabled;   //userid → 所在企业是否开启密聊
    std::set<std::pair<int64_t, int64_t> >     m_blocked;   //(拉黑者, 被拉黑者)
};

//构造一份请求上下文
SecretRequestContext ctxOf(SecretConnKey conn, int64_t userId, int64_t nowMs)
{
    SecretRequestContext c;
    c.m_conn = conn;
    c.m_userId = userId;
    c.m_seq = kSeq;
    c.m_nowMs = nowMs;
    return c;
}

//报到：version 1 与给定的防截图级别
void registerConn(SecretChatManager& mgr, FakeGate& gate, SecretConnKey conn, int64_t userId, int protect)
{
    std::vector<SecretOutFrame> out;
    char body[64];
    snprintf(body, sizeof(body), "{\"version\":1,\"protect\":%d}", protect);
    REQUIRE(mgr.handle(cmd_secret_register, ctxOf(conn, userId, kNowMs), body, gate, out) == error_code_ok);
}

//A、B1、B2、C 全部报到（A 级别 2，B1 级别 1，B2 级别 2，C 级别 0）
void registerAll(SecretChatManager& mgr, FakeGate& gate)
{
    registerConn(mgr, gate, kConnA, kUserA, secret_protect_exclude);
    registerConn(mgr, gate, kConnB1, kUserB, secret_protect_monitor);
    registerConn(mgr, gate, kConnB2, kUserB, secret_protect_exclude);
    registerConn(mgr, gate, kConnC, kUserC, secret_protect_none);
}

//从一组帧里找出发给某条连接、某个命令的第 n 帧（n 从 0 起）；找不到返回 NULL
const SecretOutFrame* findFrame(const std::vector<SecretOutFrame>& out, SecretConnKey conn, uint16_t cmd, int n = 0)
{
    int seen = 0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].m_conn == conn && out[i].m_cmd == cmd)
        {
            if (seen == n)
                return &out[i];
            ++seen;
        }
    }
    return NULL;
}

//发起邀请（A 邀请 userId，默认 B），返回会话号；失败时让用例失败
std::string invite(SecretChatManager& mgr, FakeGate& gate, SecretConnKey from, int64_t fromUser, int64_t peer,
                   int64_t nowMs, std::vector<SecretOutFrame>& out)
{
    char body[160];
    snprintf(body, sizeof(body), "{\"peerid\":\"%lld\",\"pubkey\":\"%s\"}", static_cast<long long>(peer), kPubA);
    REQUIRE(mgr.handle(cmd_secret_invite, ctxOf(from, fromUser, nowMs), body, gate, out) == error_code_ok);
    const SecretOutFrame* resp = findFrame(out, from, cmd_secret_invite);
    REQUIRE(resp != NULL);
    return parseJson(resp->m_body)["sessionid"].asString();
}

//被邀请人在某条连接上同意
void accept(SecretChatManager& mgr, FakeGate& gate, SecretConnKey conn, int64_t user, const std::string& sid,
            std::vector<SecretOutFrame>& out)
{
    std::string body = "{\"sessionid\":\"" + sid + "\",\"accept\":1,\"pubkey\":\"" + kPubB + "\"}";
    REQUIRE(mgr.handle(cmd_secret_respond, ctxOf(conn, user, kNowMs + 1000), body, gate, out) == error_code_ok);
}

//一帧 2608 的包体
std::string messageBody(const std::string& sid, int seq, const std::string& data)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", seq);
    return "{\"sessionid\":\"" + sid + "\",\"seq\":" + buf + ",\"nonce\":\"" + kNonce + "\",\"data\":\"" + data + "\"}";
}

//一帧 2605 的 state 与 reason
void checkState(const SecretOutFrame* f, int state, int reason)
{
    REQUIRE(f != NULL);
    Json::Value v = parseJson(f->m_body);
    CHECK(v["state"].asInt() == state);
    CHECK(v["reason"].asInt() == reason);
}

} // namespace

TEST_CASE("SC1 报到：包体非法、版本不符、成功登记并带企业开关")
{
    SecretChatManager mgr;
    FakeGate gate;
    std::vector<SecretOutFrame> out;
    CHECK(mgr.handle(cmd_secret_register, ctxOf(kConnA, kUserA, kNowMs), "not json", gate, out) == error_code_bad_request);
    CHECK(mgr.handle(cmd_secret_register, ctxOf(kConnA, kUserA, kNowMs), "{\"version\":1,\"protect\":9}", gate, out)
          == error_code_bad_request);
    CHECK(mgr.handle(cmd_secret_register, ctxOf(kConnA, kUserA, kNowMs), "{\"version\":2,\"protect\":2}", gate, out)
          == error_code_secret_version_unsupported);
    CHECK(mgr.registeredCount() == 0);

    out.clear();
    gate.m_enabled[kUserA] = false;
    CHECK(mgr.handle(cmd_secret_register, ctxOf(kConnA, kUserA, kNowMs), "{\"version\":1,\"protect\":2}", gate, out)
          == error_code_ok);
    REQUIRE(out.size() == 1);
    Json::Value v = parseJson(out[0].m_body);
    CHECK(v["code"].asInt() == 0);
    CHECK(v["enabled"].asInt() == 0);
    CHECK(out[0].m_seq == kSeq);
    CHECK(mgr.registeredCount() == 1);
}

TEST_CASE("SC2 邀请与同意：推送范围、公钥与防截图级别")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    CHECK(!sid.empty());
    Json::Value resp = parseJson(findFrame(out, kConnA, cmd_secret_invite)->m_body);
    CHECK(resp["expirems"].asInt64() == kSecretDefaultInviteTimeoutMs);
    //B 的两条连接都收到邀请，带 A 的公钥与级别
    const SecretOutFrame* n1 = findFrame(out, kConnB1, cmd_secret_invite_notify);
    const SecretOutFrame* n2 = findFrame(out, kConnB2, cmd_secret_invite_notify);
    REQUIRE(n1 != NULL);
    REQUIRE(n2 != NULL);
    Json::Value notify = parseJson(n1->m_body);
    CHECK(notify["sessionid"].asString() == sid);
    CHECK(notify["fromid"].asString() == "10001");
    CHECK(notify["pubkey"].asString() == kPubA);
    CHECK(notify["peerprotect"].asInt() == secret_protect_exclude);
    CHECK(findFrame(out, kConnC, cmd_secret_invite_notify) == NULL);
    CHECK(mgr.sessionCount() == 1);

    //B1 同意
    out.clear();
    accept(mgr, gate, kConnB1, kUserB, sid, out);
    const SecretOutFrame* toA = findFrame(out, kConnA, cmd_secret_state_notify);
    REQUIRE(toA != NULL);
    Json::Value a = parseJson(toA->m_body);
    CHECK(a["state"].asInt() == secret_state_active);
    CHECK(a["pubkey"].asString() == kPubB);
    CHECK(a["peerid"].asString() == "10002");
    CHECK(a["peerprotect"].asInt() == secret_protect_monitor);
    const SecretOutFrame* toB1 = findFrame(out, kConnB1, cmd_secret_state_notify);
    checkState(toB1, secret_state_active, 0);
    CHECK(parseJson(toB1->m_body)["peerprotect"].asInt() == secret_protect_exclude);
    checkState(findFrame(out, kConnB2, cmd_secret_state_notify), secret_state_handled_elsewhere, 0);
    CHECK(findFrame(out, kConnB1, cmd_secret_respond) != NULL);
}

TEST_CASE("SC3 忽略：发起方收到未接受，其余连接收到已在其他设备处理")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    std::string body = "{\"sessionid\":\"" + sid + "\",\"accept\":0}";
    CHECK(mgr.handle(cmd_secret_respond, ctxOf(kConnB2, kUserB, kNowMs + 10), body, gate, out) == error_code_ok);
    checkState(findFrame(out, kConnA, cmd_secret_state_notify), secret_state_not_accepted, 0);
    checkState(findFrame(out, kConnB1, cmd_secret_state_notify), secret_state_handled_elsewhere, 0);
    CHECK(findFrame(out, kConnB2, cmd_secret_state_notify) == NULL);
    CHECK(mgr.sessionCount() == 0);
}

TEST_CASE("SC4 超时：双方都收到未接受，再应答回 1202")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    //未到期：不处理
    CHECK(mgr.reapExpired(kNowMs + kSecretDefaultInviteTimeoutMs - 1, out) == 0);
    CHECK(out.empty());
    CHECK(mgr.reapExpired(kNowMs + kSecretDefaultInviteTimeoutMs, out) == 1);
    checkState(findFrame(out, kConnA, cmd_secret_state_notify), secret_state_not_accepted, 0);
    checkState(findFrame(out, kConnB1, cmd_secret_state_notify), secret_state_not_accepted, 0);
    checkState(findFrame(out, kConnB2, cmd_secret_state_notify), secret_state_not_accepted, 0);
    out.clear();
    std::string body = "{\"sessionid\":\"" + sid + "\",\"accept\":1,\"pubkey\":\"" + kPubB + "\"}";
    CHECK(mgr.handle(cmd_secret_respond, ctxOf(kConnB1, kUserB, kNowMs + kSecretDefaultInviteTimeoutMs), body, gate, out)
          == error_code_secret_not_exist);

    //到期之后、扫描之前应答同样回 1202
    out.clear();
    const std::string sid2 = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    body = "{\"sessionid\":\"" + sid2 + "\",\"accept\":1,\"pubkey\":\"" + kPubB + "\"}";
    CHECK(mgr.handle(cmd_secret_respond, ctxOf(kConnB1, kUserB, kNowMs + kSecretDefaultInviteTimeoutMs + 5), body, gate,
                     out) == error_code_secret_not_exist);
}

TEST_CASE("SC5 撤回：被邀请人收到已取消，非发起方回 1204")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    std::string body = "{\"sessionid\":\"" + sid + "\"}";
    CHECK(mgr.handle(cmd_secret_cancel, ctxOf(kConnB1, kUserB, kNowMs), body, gate, out) == error_code_secret_not_member);
    out.clear();
    CHECK(mgr.handle(cmd_secret_cancel, ctxOf(kConnA, kUserA, kNowMs), body, gate, out) == error_code_ok);
    checkState(findFrame(out, kConnB1, cmd_secret_state_notify), secret_state_canceled, 0);
    checkState(findFrame(out, kConnB2, cmd_secret_state_notify), secret_state_canceled, 0);
    CHECK(mgr.sessionCount() == 0);
}

TEST_CASE("SC6 冲突：同一对用户已有密聊时回 1203")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    std::string body = std::string("{\"peerid\":\"10002\",\"pubkey\":\"") + kPubA + "\"}";
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs), body, gate, out) == error_code_secret_busy);
    body = std::string("{\"peerid\":\"10001\",\"pubkey\":\"") + kPubB + "\"}";
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnB1, kUserB, kNowMs), body, gate, out) == error_code_secret_busy);
    //与第三人照常可以
    out.clear();
    invite(mgr, gate, kConnA, kUserA, kUserC, kNowMs, out);
    CHECK(mgr.sessionCount() == 2);
}

TEST_CASE("SC7 上限：每分钟邀请数与同时进行的会话数")
{
    SecretChatManager mgr;
    FakeGate gate;
    SecretChatLimits limits;
    limits.m_invitesPerMinute = 3;
    limits.m_maxSessionsPerUser = 2;
    mgr.setLimits(limits);
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;

    //A 邀请 B 后撤回，重复三次：第四次超出每分钟 3 次
    std::string body = std::string("{\"peerid\":\"10002\",\"pubkey\":\"") + kPubA + "\"}";
    for (int i = 0; i < 3; ++i)
    {
        out.clear();
        const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs + i, out);
        out.clear();
        REQUIRE(mgr.handle(cmd_secret_cancel, ctxOf(kConnA, kUserA, kNowMs + i), "{\"sessionid\":\"" + sid + "\"}", gate,
                           out) == error_code_ok);
    }
    out.clear();
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs + 10), body, gate, out) == error_code_secret_too_many);
    //一分钟后恢复
    out.clear();
    invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs + 60 * 1000 + 10, out);

    //同时进行的会话数：B 已有一个（与 A），再被 C 邀请成为两个，第三个超限
    out.clear();
    invite(mgr, gate, kConnC, kUserC, kUserB, kNowMs + 60 * 1000 + 20, out);
    out.clear();
    registerConn(mgr, gate, 0x4001, 10004, secret_protect_exclude);
    body = std::string("{\"peerid\":\"10002\",\"pubkey\":\"") + kPubA + "\"}";
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(0x4001, 10004, kNowMs + 60 * 1000 + 30), body, gate, out)
          == error_code_secret_too_many);
}

TEST_CASE("SC8 校验：对方没有连接、被拉黑、企业关闭、邀请自己、公钥非法、发起方没报到")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerConn(mgr, gate, kConnA, kUserA, secret_protect_exclude);
    std::vector<SecretOutFrame> out;
    std::string body = std::string("{\"peerid\":\"10002\",\"pubkey\":\"") + kPubA + "\"}";
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs), body, gate, out)
          == error_code_secret_peer_unavailable);

    registerConn(mgr, gate, kConnB1, kUserB, secret_protect_exclude);
    gate.m_blocked.insert(std::make_pair(kUserB, kUserA));
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs), body, gate, out) == error_code_secret_blocked);
    gate.m_blocked.clear();

    gate.m_enabled[kUserB] = false;
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs), body, gate, out) == error_code_secret_disabled);
    gate.m_enabled.clear();

    std::string self = std::string("{\"peerid\":\"10001\",\"pubkey\":\"") + kPubA + "\"}";
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs), self, gate, out) == error_code_bad_request);
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs), "{\"peerid\":\"10002\",\"pubkey\":\"short\"}",
                     gate, out) == error_code_bad_request);
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(0x9999, kUserA, kNowMs), body, gate, out) == error_code_secret_not_member);
    CHECK(mgr.sessionCount() == 0);
    CHECK(mgr.handle(cmd_secret_invite, ctxOf(kConnA, kUserA, kNowMs), body, gate, out) == error_code_ok);
}

TEST_CASE("SC9 转发：只转给绑定的连接，校验与限速")
{
    SecretChatManager mgr;
    FakeGate gate;
    SecretChatLimits limits;
    limits.m_framesPerSecond = 4;
    limits.m_maxFrameBytes = 512;
    mgr.setLimits(limits);
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    accept(mgr, gate, kConnB1, kUserB, sid, out);

    //A → B1
    out.clear();
    const int64_t t = kNowMs + 5000;
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t), messageBody(sid, 1, "QUJD"), gate, out)
          == error_code_ok);
    const SecretOutFrame* fwd = findFrame(out, kConnB1, cmd_secret_message_notify);
    REQUIRE(fwd != NULL);
    Json::Value v = parseJson(fwd->m_body);
    CHECK(v["seq"].asInt() == 1);
    CHECK(v["data"].asString() == "QUJD");
    CHECK(v["nonce"].asString() == kNonce);
    CHECK(v["servertime"].asInt64() == t);
    CHECK(findFrame(out, kConnB2, cmd_secret_message_notify) == NULL);
    Json::Value resp = parseJson(findFrame(out, kConnA, cmd_secret_message)->m_body);
    CHECK(resp["code"].asInt() == 0);
    CHECK(resp["seq"].asInt() == 1);

    //B1 → A
    out.clear();
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnB1, kUserB, t), messageBody(sid, 1, "REVG"), gate, out)
          == error_code_ok);
    CHECK(findFrame(out, kConnA, cmd_secret_message_notify) != NULL);

    //B2 不是绑定的连接
    out.clear();
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnB2, kUserB, t), messageBody(sid, 2, "REVG"), gate, out)
          == error_code_secret_not_member);
    //字段非法：seq 为 0、data 不是 base64、nonce 长度不对
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t), messageBody(sid, 0, "QUJD"), gate, out)
          == error_code_bad_request);
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t), messageBody(sid, 2, "QU*D"), gate, out)
          == error_code_bad_request);
    //超长：整帧超过 512 字节
    std::string big(600, 'A');
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t), messageBody(sid, 2, big), gate, out)
          == error_code_secret_too_large);

    //限速：本秒已转发 2 帧，再 2 帧可以，第 5 帧超限；下一秒恢复
    out.clear();
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t + 1), messageBody(sid, 2, "QUJD"), gate, out)
          == error_code_ok);
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t + 2), messageBody(sid, 3, "QUJD"), gate, out)
          == error_code_ok);
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t + 3), messageBody(sid, 4, "QUJD"), gate, out)
          == error_code_secret_too_many);
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnA, kUserA, t + 1000), messageBody(sid, 4, "QUJD"), gate, out)
          == error_code_ok);
}

TEST_CASE("SC10 结束：另一方收到已结束、对方退出；重复结束回成功；之后再发回 1202")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    accept(mgr, gate, kConnB1, kUserB, sid, out);
    out.clear();
    std::string body = "{\"sessionid\":\"" + sid + "\"}";
    CHECK(mgr.handle(cmd_secret_close, ctxOf(kConnA, kUserA, kNowMs), body, gate, out) == error_code_ok);
    checkState(findFrame(out, kConnB1, cmd_secret_state_notify), secret_state_ended, secret_end_peer_left);
    CHECK(findFrame(out, kConnB2, cmd_secret_state_notify) == NULL);
    CHECK(mgr.sessionCount() == 0);
    out.clear();
    CHECK(mgr.handle(cmd_secret_close, ctxOf(kConnB1, kUserB, kNowMs), body, gate, out) == error_code_ok);
    CHECK(mgr.handle(cmd_secret_message, ctxOf(kConnB1, kUserB, kNowMs), messageBody(sid, 9, "QUJD"), gate, out)
          == error_code_secret_not_exist);
}

TEST_CASE("SC11 断线：进行中、发起方待应答、被邀请人最后一条连接")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;

    //进行中的会话：B1 断开，A 收到已结束、对方离线
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    accept(mgr, gate, kConnB1, kUserB, sid, out);
    out.clear();
    mgr.onDisconnect(kConnB1, out);
    checkState(findFrame(out, kConnA, cmd_secret_state_notify), secret_state_ended, secret_end_peer_offline);
    CHECK(mgr.sessionCount() == 0);

    //发起方断开：被邀请人 C 收到已取消
    out.clear();
    invite(mgr, gate, kConnA, kUserA, kUserC, kNowMs, out);
    out.clear();
    mgr.onDisconnect(kConnA, out);
    checkState(findFrame(out, kConnC, cmd_secret_state_notify), secret_state_canceled, 0);
    CHECK(mgr.sessionCount() == 0);

    //被邀请人最后一条连接断开：C 邀请 B（B 只剩 B2），B2 断开后 C 收到未接受
    out.clear();
    invite(mgr, gate, kConnC, kUserC, kUserB, kNowMs, out);
    out.clear();
    mgr.onDisconnect(kConnB2, out);
    checkState(findFrame(out, kConnC, cmd_secret_state_notify), secret_state_not_accepted, 0);
    CHECK(mgr.sessionCount() == 0);
    //重复断开不再推送
    out.clear();
    mgr.onDisconnect(kConnB2, out);
    CHECK(out.empty());
}

TEST_CASE("SC12 企业关闭开关：只结束该企业成员参与的会话")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    registerConn(mgr, gate, 0x5001, 10005, secret_protect_exclude);   //企业 7 的另一人
    std::vector<SecretOutFrame> out;
    const std::string sidAB = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    accept(mgr, gate, kConnB1, kUserB, sidAB, out);
    out.clear();
    invite(mgr, gate, kConnC, kUserC, 10005, kNowMs, out);   //企业 8 的 C 邀请企业 7 的人：也算涉及企业 7

    out.clear();
    CHECK(mgr.onFeatureDisabled(kEnterprise8, gate, out) == 1);
    checkState(findFrame(out, kConnC, cmd_secret_state_notify), secret_state_ended, secret_end_disabled);
    checkState(findFrame(out, 0x5001, cmd_secret_state_notify), secret_state_ended, secret_end_disabled);
    CHECK(findFrame(out, kConnA, cmd_secret_state_notify) == NULL);
    CHECK(mgr.sessionCount() == 1);

    out.clear();
    CHECK(mgr.onFeatureDisabled(kEnterprise7, gate, out) == 1);
    checkState(findFrame(out, kConnA, cmd_secret_state_notify), secret_state_ended, secret_end_disabled);
    checkState(findFrame(out, kConnB1, cmd_secret_state_notify), secret_state_ended, secret_end_disabled);
    CHECK(mgr.sessionCount() == 0);
}

TEST_CASE("SC13 防截图级别变化：对方收到状态 6 与新级别")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    accept(mgr, gate, kConnB1, kUserB, sid, out);
    //A 重新报到，级别不变：不推
    out.clear();
    REQUIRE(mgr.handle(cmd_secret_register, ctxOf(kConnA, kUserA, kNowMs), "{\"version\":1,\"protect\":2}", gate, out)
            == error_code_ok);
    CHECK(findFrame(out, kConnB1, cmd_secret_state_notify) == NULL);
    //A 级别变为 0：B1 收到状态 6
    out.clear();
    REQUIRE(mgr.handle(cmd_secret_register, ctxOf(kConnA, kUserA, kNowMs), "{\"version\":1,\"protect\":0}", gate, out)
            == error_code_ok);
    const SecretOutFrame* f = findFrame(out, kConnB1, cmd_secret_state_notify);
    checkState(f, secret_state_peer_protect, 0);
    CHECK(parseJson(f->m_body)["peerprotect"].asInt() == secret_protect_none);
}

TEST_CASE("SC14 号段判定")
{
    CHECK(SecretChatManager::isSecretCommand(cmd_secret_register));
    CHECK(SecretChatManager::isSecretCommand(cmd_secret_message_notify));
    CHECK(SecretChatManager::isSecretCommand(2699));
    CHECK_FALSE(SecretChatManager::isSecretCommand(2599));
    CHECK_FALSE(SecretChatManager::isSecretCommand(cmd_thread_list_posts));
}

TEST_CASE("SC15 待应答时被邀请人结束等同忽略；2603 等推送命令由客户端发来按非法应答")
{
    SecretChatManager mgr;
    FakeGate gate;
    registerAll(mgr, gate);
    std::vector<SecretOutFrame> out;
    const std::string sid = invite(mgr, gate, kConnA, kUserA, kUserB, kNowMs, out);
    out.clear();
    CHECK(mgr.handle(cmd_secret_close, ctxOf(kConnB1, kUserB, kNowMs), "{\"sessionid\":\"" + sid + "\"}", gate, out)
          == error_code_ok);
    checkState(findFrame(out, kConnA, cmd_secret_state_notify), secret_state_not_accepted, 0);
    checkState(findFrame(out, kConnB2, cmd_secret_state_notify), secret_state_handled_elsewhere, 0);
    CHECK(mgr.sessionCount() == 0);
    out.clear();
    CHECK(mgr.handle(cmd_secret_invite_notify, ctxOf(kConnA, kUserA, kNowMs), "{}", gate, out) == error_code_bad_request);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_conn == kConnA);
}
