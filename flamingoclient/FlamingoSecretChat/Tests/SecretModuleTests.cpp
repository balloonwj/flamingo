/**
 *  密聊模块状态机单元测试（标签 [secretdll]），覆盖 docs/密聊方案.md 第 8 节：
 *    - SCD8 邀请 → 同意 → 建立：两个模块实例经测试里的模拟服务端互通，宿主收到的事件顺序与内容正确；
 *    - SCD9 文字：加密后经模拟服务端转给对方、解密正确；重放与倒序的帧被丢弃；发送结果回报；
 *    - SCD10 结束：本人 Close 后不能再发送；对方收到「已结束」后宿主收到通知、会话从表里删除；
 *    - SCD17 图片：按节奏分块发出（在途不超过 4 帧）、对方拼回与原图逐字节相同、发送进度报到 100%；
 *    - SCD18 服务端回 1206（转发过快）时换新序号重发，对方照常收到；传输中途会话结束，未收齐的图片报失败；
 *    - SCD19 邀请失败与忽略：2602 回错误码时宿主收到失败；被邀请方忽略后会话删除。
 *  模块以「无窗口」模式构造，帧由测试直接经 ProcessFrame 交入，发送节奏由测试调用 PumpSend 推进。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Host/SecretChatModule.h"
#include "../Core/SecretProtocol.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace {

//测试双方的 userid 与会话号
const int64_t kUserA = 5101;
const int64_t kUserB = 5102;
const uint64_t kSessionId = 880001;

//模拟的服务端转发时刻
const int64_t kServerTime = 1791000000000LL;

//模拟时钟比真实单调时钟提前的量（毫秒）：模块首次发送用的是真实时钟，测试推进的时间必须在它之后
const int64_t kClockLead = 100000;

//测试推进发送节奏用的时钟起点
int64_t ClockBase()
{
    return static_cast<int64_t>(::GetTickCount64()) + kClockLead;
}

//一张测试图片的字节数：5 块（第 5 块不满），足以观察「在途不超过 4 帧」
const uint32_t kImageBytes = 48 * 1024 * 4 + 1234;

//宿主记下的一帧发出的命令
struct SentFrame
{
    uint16_t    cmd;
    uint32_t    seq;
    std::string body;
};

//假宿主：记下发出的帧与收到的事件
class FakeHost : public secretchat::ISecretChatHost
{
public:
    explicit FakeHost(int64_t me)
        : m_me(me)
        , m_nextSeq(100)
        , m_registeredEnabled(false)
        , m_lastInviteSession(0)
        , m_lastInviteCode(-1)
        , m_incomingSession(0)
        , m_lastState(0)
        , m_lastReason(0)
        , m_imageOk(false)
        , m_imageFailed(0)
        , m_lastSentBytes(0)
        , m_lastTotalBytes(0)
        , m_lastImageCode(-1)
    {
    }

    virtual void Log(int level, const char* text) override
    {
        (void)level;
        (void)text;
    }

    virtual uint32_t SendSecretCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        SentFrame f;
        f.cmd = cmd;
        f.seq = ++m_nextSeq;
        f.body.assign(json, len);
        m_sent.push_back(f);
        return f.seq;
    }

    virtual int64_t GetMyUserId() override
    {
        return m_me;
    }

    virtual void OnRegistered(bool enabled) override
    {
        m_registeredEnabled = enabled;
    }

    virtual void OnInviteSent(uint64_t sessionId, int64_t peerId, uint32_t remainMs, int code) override
    {
        (void)peerId;
        (void)remainMs;
        m_lastInviteSession = sessionId;
        m_lastInviteCode = code;
    }

    virtual void OnInviteArrived(uint64_t sessionId, int64_t fromId, uint32_t remainMs, int peerProtect) override
    {
        (void)fromId;
        (void)remainMs;
        (void)peerProtect;
        m_incomingSession = sessionId;
    }

    virtual void OnSessionState(uint64_t sessionId, int64_t peerId, int state, int reason, int peerProtect) override
    {
        (void)sessionId;
        (void)peerId;
        (void)peerProtect;
        m_lastState = state;
        m_lastReason = reason;
        m_states.push_back(state);
    }

    virtual void OnSecretText(uint64_t sessionId, int64_t peerId, uint32_t msgId, const wchar_t* text,
                              int64_t serverTimeMs) override
    {
        (void)sessionId;
        (void)peerId;
        (void)msgId;
        (void)serverTimeMs;
        m_texts.push_back(text);
    }

    virtual void OnSecretImageBegin(uint64_t sessionId, int64_t peerId, uint32_t imageId, uint32_t totalBytes,
                                    uint32_t width, uint32_t height, int64_t serverTimeMs) override
    {
        (void)sessionId;
        (void)peerId;
        (void)imageId;
        (void)totalBytes;
        (void)serverTimeMs;
        m_imageW = width;
        m_imageH = height;
    }

    virtual void OnSecretImageProgress(uint64_t sessionId, uint32_t imageId, uint32_t receivedBytes) override
    {
        (void)sessionId;
        (void)imageId;
        (void)receivedBytes;
    }

    virtual void OnSecretImage(uint64_t sessionId, uint32_t imageId, const void* bytes, uint32_t len, bool ok) override
    {
        (void)sessionId;
        (void)imageId;
        if (ok)
        {
            m_imageOk = true;
            m_image.assign(static_cast<const char*>(bytes), len);
        }
        else
        {
            ++m_imageFailed;
        }
    }

    virtual void OnSendResult(uint64_t sessionId, uint32_t msgId, int code) override
    {
        (void)sessionId;
        m_sendResults.push_back(std::make_pair(msgId, code));
    }

    virtual void OnImageSendProgress(uint64_t sessionId, uint32_t imageId, uint32_t sentBytes, uint32_t totalBytes,
                                     int code) override
    {
        (void)sessionId;
        (void)imageId;
        m_lastSentBytes = sentBytes;
        m_lastTotalBytes = totalBytes;
        m_lastImageCode = code;
    }

    int64_t                                m_me;
    uint32_t                               m_nextSeq;
    std::vector<SentFrame>                 m_sent;
    bool                                   m_registeredEnabled;
    uint64_t                               m_lastInviteSession;
    int                                    m_lastInviteCode;
    uint64_t                               m_incomingSession;
    int                                    m_lastState;
    int                                    m_lastReason;
    std::vector<int>                       m_states;
    std::vector<std::wstring>              m_texts;
    bool                                   m_imageOk;
    int                                    m_imageFailed;
    std::string                            m_image;
    uint32_t                               m_imageW = 0;
    uint32_t                               m_imageH = 0;
    std::vector<std::pair<uint32_t, int> > m_sendResults;
    uint32_t                               m_lastSentBytes;
    uint32_t                               m_lastTotalBytes;
    int                                    m_lastImageCode;
};

Json::Value ParseJson(const std::string& text)
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

//取宿主最近发出的某个命令的帧
const SentFrame* LastSent(const FakeHost& h, uint16_t cmd)
{
    for (size_t i = h.m_sent.size(); i > 0; --i)
    {
        if (h.m_sent[i - 1].cmd == cmd)
            return &h.m_sent[i - 1];
    }
    return NULL;
}

//初始化一个无窗口的模块并登录、报到成功
void InitModule(secretchat::CSecretChatModule& m, FakeHost& h)
{
    m.SetHeadlessForTest(true);
    secretchat::SecretInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = L".";
    REQUIRE(m.Init(&h, &params));
    m.OnLoggedIn();
    REQUIRE(LastSent(h, cmd_secret_register) != NULL);
    m.ProcessFrame(cmd_secret_register, 0, "{\"code\":0,\"msg\":\"ok\",\"enabled\":1}");
    REQUIRE(h.m_registeredEnabled);
}

//模拟服务端完成「A 邀请 B、B 同意」，两边都进入进行中
void Establish(secretchat::CSecretChatModule& a, FakeHost& ha, secretchat::CSecretChatModule& b, FakeHost& hb)
{
    REQUIRE(a.Invite(kUserB));
    const SentFrame* invite = LastSent(ha, cmd_secret_invite);
    REQUIRE(invite != NULL);
    const std::string pkA = ParseJson(invite->body)["pubkey"].asString();
    char resp[128];
    sprintf_s(resp, "{\"code\":0,\"msg\":\"ok\",\"sessionid\":\"%llu\",\"expirems\":60000}",
              static_cast<unsigned long long>(kSessionId));
    a.ProcessFrame(cmd_secret_invite, invite->seq, resp);
    REQUIRE(ha.m_lastInviteCode == 0);
    REQUIRE(ha.m_lastInviteSession == kSessionId);

    Json::Value notify(Json::objectValue);
    notify["sessionid"] = "880001";
    notify["fromid"] = "5101";
    notify["pubkey"] = pkA;
    notify["expirems"] = 60000;
    notify["peerprotect"] = 2;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    b.ProcessFrame(cmd_secret_invite_notify, 0, Json::writeString(wb, notify));
    REQUIRE(hb.m_incomingSession == kSessionId);

    REQUIRE(b.Respond(kSessionId, true));
    const SentFrame* respond = LastSent(hb, cmd_secret_respond);
    REQUIRE(respond != NULL);
    const std::string pkB = ParseJson(respond->body)["pubkey"].asString();
    REQUIRE(pkB.size() == 44);
    b.ProcessFrame(cmd_secret_respond, respond->seq, "{\"code\":0,\"msg\":\"ok\"}");

    Json::Value toA(Json::objectValue);
    toA["sessionid"] = "880001";
    toA["peerid"] = "5102";
    toA["state"] = secret_state_active;
    toA["reason"] = 0;
    toA["pubkey"] = pkB;
    toA["peerprotect"] = 1;
    a.ProcessFrame(cmd_secret_state_notify, 0, Json::writeString(wb, toA));
    Json::Value toB(Json::objectValue);
    toB["sessionid"] = "880001";
    toB["peerid"] = "5101";
    toB["state"] = secret_state_active;
    toB["reason"] = 0;
    toB["peerprotect"] = 2;
    b.ProcessFrame(cmd_secret_state_notify, 0, Json::writeString(wb, toB));
    REQUIRE(ha.m_lastState == secretchat::session_state_active);
    REQUIRE(hb.m_lastState == secretchat::session_state_active);
}

//模拟服务端转发：把 from 一侧最近发出的、还没转发过的 2608 帧逐一转给 to，并给 from 回应答
//（code 为应答码；为 1206 时不转发）。返回转发的帧数
size_t Relay(secretchat::CSecretChatModule& from, FakeHost& hf, secretchat::CSecretChatModule& to, size_t& cursor,
             int code)
{
    size_t relayed = 0;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    while (cursor < hf.m_sent.size())
    {
        const SentFrame f = hf.m_sent[cursor++];
        if (f.cmd != cmd_secret_message)
            continue;
        Json::Value req = ParseJson(f.body);
        Json::Value resp(Json::objectValue);
        resp["code"] = code;
        resp["msg"] = "x";
        resp["seq"] = req["seq"].asUInt();
        if (code == 0)
        {
            Json::Value notify(Json::objectValue);
            notify["sessionid"] = req["sessionid"];
            notify["seq"] = req["seq"];
            notify["nonce"] = req["nonce"];
            notify["data"] = req["data"];
            notify["servertime"] = static_cast<Json::Int64>(kServerTime);
            to.ProcessFrame(cmd_secret_message_notify, 0, Json::writeString(wb, notify));
            ++relayed;
        }
        from.ProcessFrame(cmd_secret_message, f.seq, Json::writeString(wb, resp));
    }
    return relayed;
}

//生成指定长度、内容可辨认的字节串
std::string MakeBytes(uint32_t len)
{
    std::string s(len, '\0');
    for (uint32_t i = 0; i < len; ++i)
        s[i] = static_cast<char>((i * 13 + 5) & 0xFF);
    return s;
}

} // namespace

TEST_CASE("SCD8 邀请、同意、建立", "[secretdll]")
{
    secretchat::CSecretChatModule a;
    secretchat::CSecretChatModule b;
    FakeHost ha(kUserA);
    FakeHost hb(kUserB);
    InitModule(a, ha);
    InitModule(b, hb);
    Establish(a, ha, b, hb);
    CHECK(a.SessionCount() == 1);
    CHECK(b.SessionCount() == 1);
    //同一对用户不能再发起第二个
    CHECK_FALSE(a.Invite(kUserB));
    //两端安全码一致
    wchar_t codeA[32] = { 0 };
    wchar_t codeB[32] = { 0 };
    REQUIRE(a.GetSafetyCode(kSessionId, codeA, 32));
    REQUIRE(b.GetSafetyCode(kSessionId, codeB, 32));
    CHECK(std::wstring(codeA) == std::wstring(codeB));
    a.Shutdown();
    b.Shutdown();
}

TEST_CASE("SCD9 文字加密转发、解密正确；重放被丢弃", "[secretdll]")
{
    secretchat::CSecretChatModule a;
    secretchat::CSecretChatModule b;
    FakeHost ha(kUserA);
    FakeHost hb(kUserB);
    InitModule(a, ha);
    InitModule(b, hb);
    Establish(a, ha, b, hb);
    size_t cursorA = ha.m_sent.size();

    uint32_t msgId = 0;
    REQUIRE(a.SendText(kSessionId, L"第一条 /f[\"1\"]", &msgId));
    CHECK(msgId == 1);
    const SentFrame* frame = LastSent(ha, cmd_secret_message);
    REQUIRE(frame != NULL);
    //服务端看到的包体里没有明文
    CHECK(frame->body.find("第一条") == std::string::npos);
    const SentFrame copy = *frame;
    CHECK(Relay(a, ha, b, cursorA, 0) == 1);
    REQUIRE(hb.m_texts.size() == 1);
    CHECK(hb.m_texts[0] == L"第一条 /f[\"1\"]");
    REQUIRE(ha.m_sendResults.size() == 1);
    CHECK(ha.m_sendResults[0] == std::make_pair(1u, 0));

    //把同一帧再投一次：序号不大于已收的，丢弃
    Json::Value req = ParseJson(copy.body);
    Json::Value notify(Json::objectValue);
    notify["sessionid"] = req["sessionid"];
    notify["seq"] = req["seq"];
    notify["nonce"] = req["nonce"];
    notify["data"] = req["data"];
    notify["servertime"] = static_cast<Json::Int64>(kServerTime);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    b.ProcessFrame(cmd_secret_message_notify, 0, Json::writeString(wb, notify));
    CHECK(hb.m_texts.size() == 1);

    //B 回复 A
    size_t cursorB = hb.m_sent.size();
    REQUIRE(b.SendText(kSessionId, L"收到", &msgId));
    CHECK(Relay(b, hb, a, cursorB, 0) == 1);
    REQUIRE(ha.m_texts.size() == 1);
    CHECK(ha.m_texts[0] == L"收到");

    //空文字与超长文字不发
    CHECK_FALSE(a.SendText(kSessionId, L"", &msgId));
    std::wstring tooLong(secretchat::kMaxTextChars + 1, L'x');
    CHECK_FALSE(a.SendText(kSessionId, tooLong.c_str(), &msgId));
    a.Shutdown();
    b.Shutdown();
}

TEST_CASE("SCD10 本人结束与对方结束", "[secretdll]")
{
    secretchat::CSecretChatModule a;
    secretchat::CSecretChatModule b;
    FakeHost ha(kUserA);
    FakeHost hb(kUserB);
    InitModule(a, ha);
    InitModule(b, hb);
    Establish(a, ha, b, hb);

    REQUIRE(a.Close(kSessionId));
    CHECK(LastSent(ha, cmd_secret_close) != NULL);
    CHECK(a.SessionCount() == 0);
    uint32_t msgId = 0;
    CHECK_FALSE(a.SendText(kSessionId, L"x", &msgId));

    //B 收到「已结束、对方退出」
    b.ProcessFrame(cmd_secret_state_notify, 0,
                   "{\"sessionid\":\"880001\",\"peerid\":\"5101\",\"state\":4,\"reason\":1,\"peerprotect\":0}");
    CHECK(hb.m_lastState == secretchat::session_state_ended);
    CHECK(hb.m_lastReason == secret_end_peer_left);
    CHECK(b.SessionCount() == 0);
    CHECK_FALSE(b.SendText(kSessionId, L"x", &msgId));
    a.Shutdown();
    b.Shutdown();
}

TEST_CASE("SCD17 图片分块、节奏与拼回", "[secretdll]")
{
    secretchat::CSecretChatModule a;
    secretchat::CSecretChatModule b;
    FakeHost ha(kUserA);
    FakeHost hb(kUserB);
    InitModule(a, ha);
    InitModule(b, hb);
    Establish(a, ha, b, hb);
    size_t cursorA = ha.m_sent.size();

    const std::string image = MakeBytes(kImageBytes);
    uint32_t imageId = 0;
    REQUIRE(a.SendImage(kSessionId, image.data(), static_cast<uint32_t>(image.size()), 320, 200, &imageId));
    CHECK(imageId == 1);
    CHECK_FALSE(a.SendImage(kSessionId, image.data(), secretchat::kMaxImageBytes + 1, 1, 1, &imageId));

    //推进时钟：每 20 毫秒一帧，在途最多 4 帧，未应答时不再多发
    int64_t now = ClockBase();
    for (int i = 0; i < 10; ++i)
    {
        now += secretchat::kImageFrameIntervalMs;
        a.PumpSend(now);
    }
    size_t messages = 0;
    for (size_t i = cursorA; i < ha.m_sent.size(); ++i)
    {
        if (ha.m_sent[i].cmd == cmd_secret_message)
            ++messages;
    }
    CHECK(messages == secretchat::kMaxInflightImageFrames);

    //转发并应答，继续推进直到发完
    for (int round = 0; round < 20 && !hb.m_imageOk; ++round)
    {
        Relay(a, ha, b, cursorA, 0);
        now += secretchat::kImageFrameIntervalMs;
        a.PumpSend(now);
    }
    REQUIRE(hb.m_imageOk);
    CHECK(hb.m_image == image);
    CHECK(hb.m_imageW == 320);
    CHECK(hb.m_imageH == 200);
    CHECK(ha.m_lastSentBytes == kImageBytes);
    CHECK(ha.m_lastTotalBytes == kImageBytes);
    CHECK(ha.m_lastImageCode == 0);
    a.Shutdown();
    b.Shutdown();
}

TEST_CASE("SCD18 转发过快时换新序号重发；中途结束时未收齐的图片报失败", "[secretdll]")
{
    secretchat::CSecretChatModule a;
    secretchat::CSecretChatModule b;
    FakeHost ha(kUserA);
    FakeHost hb(kUserB);
    InitModule(a, ha);
    InitModule(b, hb);
    Establish(a, ha, b, hb);
    size_t cursorA = ha.m_sent.size();

    uint32_t msgId = 0;
    REQUIRE(a.SendText(kSessionId, L"限速", &msgId));
    const uint32_t firstSeq = ParseJson(LastSent(ha, cmd_secret_message)->body)["seq"].asUInt();
    //服务端回 1206：不转发
    Relay(a, ha, b, cursorA, error_code_secret_too_many);
    CHECK(hb.m_texts.empty());
    CHECK(ha.m_sendResults.empty());
    //到点后重发，序号是新的
    const int64_t base = ClockBase();
    CHECK(a.PumpSend(base));
    a.PumpSend(base + 10 * secretchat::kRetryDelayMs);
    const uint32_t secondSeq = ParseJson(LastSent(ha, cmd_secret_message)->body)["seq"].asUInt();
    CHECK(secondSeq > firstSeq);
    Relay(a, ha, b, cursorA, 0);
    REQUIRE(hb.m_texts.size() == 1);
    CHECK(hb.m_texts[0] == L"限速");

    //发一张图片，只转发第一块，然后会话结束
    const std::string image = MakeBytes(kImageBytes);
    uint32_t imageId = 0;
    REQUIRE(a.SendImage(kSessionId, image.data(), static_cast<uint32_t>(image.size()), 10, 10, &imageId));
    size_t stop = cursorA;
    while (stop < ha.m_sent.size() && ha.m_sent[stop].cmd != cmd_secret_message)
        ++stop;
    REQUIRE(stop < ha.m_sent.size());
    size_t oneCursor = stop;
    //只留下第一块：其后发出的块当作还在路上
    ha.m_sent.resize(stop + 1);
    Relay(a, ha, b, oneCursor, 0);
    b.ProcessFrame(cmd_secret_state_notify, 0,
                   "{\"sessionid\":\"880001\",\"peerid\":\"5101\",\"state\":4,\"reason\":2,\"peerprotect\":0}");
    CHECK(hb.m_imageFailed == 1);
    CHECK_FALSE(hb.m_imageOk);
    a.Shutdown();
    b.Shutdown();
}

TEST_CASE("SCD19 邀请失败与忽略", "[secretdll]")
{
    secretchat::CSecretChatModule a;
    secretchat::CSecretChatModule b;
    FakeHost ha(kUserA);
    FakeHost hb(kUserB);
    InitModule(a, ha);
    InitModule(b, hb);

    REQUIRE(a.Invite(kUserB));
    const SentFrame* invite = LastSent(ha, cmd_secret_invite);
    a.ProcessFrame(cmd_secret_invite, invite->seq, "{\"code\":1201,\"msg\":\"peer unavailable\"}");
    CHECK(ha.m_lastInviteCode == error_code_secret_peer_unavailable);
    CHECK(ha.m_lastInviteSession == 0);
    CHECK(a.SessionCount() == 0);

    //B 收到邀请后忽略
    b.ProcessFrame(cmd_secret_invite_notify, 0,
                   "{\"sessionid\":\"77\",\"fromid\":\"5101\",\"pubkey\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\","
                   "\"expirems\":60000,\"peerprotect\":2}");
    CHECK(b.SessionCount() == 1);
    REQUIRE(b.Respond(77, false));
    CHECK(ParseJson(LastSent(hb, cmd_secret_respond)->body)["accept"].asInt() == 0);
    CHECK(b.SessionCount() == 0);
    CHECK_FALSE(b.Respond(77, true));

    //收到撤回
    b.ProcessFrame(cmd_secret_invite_notify, 0,
                   "{\"sessionid\":\"78\",\"fromid\":\"5101\",\"pubkey\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\","
                   "\"expirems\":60000,\"peerprotect\":2}");
    b.ProcessFrame(cmd_secret_state_notify, 0, "{\"sessionid\":\"78\",\"peerid\":\"5101\",\"state\":3,\"reason\":0}");
    CHECK(hb.m_lastState == secretchat::session_state_canceled);
    CHECK(b.SessionCount() == 0);

    //登出时结束全部会话
    b.ProcessFrame(cmd_secret_invite_notify, 0,
                   "{\"sessionid\":\"79\",\"fromid\":\"5101\",\"pubkey\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\","
                   "\"expirems\":60000,\"peerprotect\":2}");
    b.OnLoggingOut();
    CHECK(b.SessionCount() == 0);
    CHECK(LastSent(hb, cmd_secret_close) != NULL);
    a.Shutdown();
    b.Shutdown();
}
