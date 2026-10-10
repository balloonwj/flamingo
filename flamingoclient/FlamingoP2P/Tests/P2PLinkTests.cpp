/**
 *  FlamingoP2P 信令编解码与会话状态机的单元测试（Suite P2PSC、P2PLS，标签 [p2plink]），P2PLinkTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 3 期 3d：
 *    - P2PSC1 2301：请求包体；应答的开关与探测服务器；code 非 0 时视为关闭；非法包体；
 *    - P2PSC2 2302：按服务端的格式解析全部字段（peeruid 为字符串或整数）；角色、会话号、时间不合法时失败；
 *    - P2PSC3 2303 / 2304 / 2305 / 2306：2303 请求经服务端转发（加上 fromuid）后能解析回公钥与候选地址；2304 成功时
 *             带 path、失败时不带，NAT 判断未知时不带 nat；2305 往返；2306 的 peeruid 为字符串；
 *  会话状态机接在模拟网络（P2PSimNet）上，信令由测试代码模拟 chatserver 转发：
 *    - P2PLS1 同一局域网：两端就绪，各发一次 2303 与一次成功的 2304（路径为局域网直连）；
 *    - P2PLS2 端口受限锥形 × 端口受限锥形：经打洞就绪，2304 的路径为打洞；
 *    - P2PLS3 对称型 × 对称型且中转也连不上：两端各请求一次中转，随后失败，各发一次失败的 2304（不带 path）与一次
 *             2305（建链失败）；
 *    - P2PLS4 对方的 2303 在本方地址探测完成之前就到了：仍然就绪；
 *    - P2PLS5 对方的 2303 一直没来：在信令时限判失败；
 *    - P2PLS6 数据报通道：两端打开同一通道后原样收发（含切成多片的消息）；重复打开、空接收端、未打开的通道、超长
 *             数据分别返回 p2p_send_bad_channel / p2p_send_bad_channel / p2p_send_bad_channel / p2p_send_too_large；
 *             统计累计字节数；
 *    - P2PLS7 保活与断开判定：空闲时按保活间隔发保活、不断开；网络全断后超过断开判定时间两端都报告断开（链路超时）
 *             并各发一次 2305；
 *    - P2PLS8 业务方关闭：对方收到关闭报文后报告断开（业务结束），本方不报告事件、发一次 2305，之后发送返回已关闭；
 *    - P2PLS9 服务端推来 2305：就绪后报告断开（对方掉线），不再发 2305；就绪之前报告失败并带上错误码。
 *  回落中转（第 3e 步）接在模拟中转上（两端都报到之后才转发，报到之前发来的报文丢弃，与 imgserver 一致）：
 *    - P2PLS10 对称型 × 对称型、中转可用：打洞失败后两端经中转就绪，各发一次成功的 2304（路径为中转，地址对写中转
 *              地址）；应答方晚报到，发起方的「选定」有被丢弃的、靠重发完成；数据报经中转往返；空闲时靠保活维持；
 *    - P2PLS11 强制中转：同一局域网也不打洞，收到对方 2303 后直接走中转，UDP 上一个报文都不发；
 *    - P2PLS12 中转就绪之后中转连接断开：两端报告断开（链路超时），各发一次 2305；
 *    - P2PLS13 连上中转但对方一直不来：在中转时限判失败，发 2305（建链失败）；
 *    - P2PLS14 2302 没带中转参数：打洞失败后直接失败，不请求中转。
 *  事件分发（第 4a 步）：
 *    - P2PLS15 就绪通知与收到数据的回调期间，会话的互斥量没有被持有：回调里另起一个线程调用会话的方法，
 *              它在回调返回之前就能完成。
 *  统计（第 4d 步）：
 *    - P2PLS16 可靠通道传输期间积压字节数大于 0、传完并收到确认后为 0；往返时延接近模拟链路的往返时延（单程 10
 *              毫秒）；无丢包时丢包率为 0，按 10% 丢包传输后丢包率大于 0；累计收发字节数与业务数据一致。
 *  成批发送（第 4e 步）：
 *    - P2PLS17 发送出口声明支持成批发送时，可靠通道传输期间出现多段一批的发送，每批不超过 32 段且除最后一段外都是
 *              满载报文，数据完整送达。
 *  Close 与回调（第 5 期）：
 *    - P2PLS18 接收端在回调里停留 200 毫秒，期间别的线程 Close：Close 在回调返回之后才返回；同一轮里排在后面的数据
 *              不再交给接收端。
 *
 *  典型用法：FlamingoUnit.exe "[p2plink]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <sodium.h>
#include <stdint.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "../../Source/net/Protocol.h"
#include "../Core/P2PPacket.h"
#include "../Link/P2PLinkSession.h"
#include "../Link/P2PSignalCodec.h"
#include "P2PSimNet.h"

using namespace p2p;
using namespace p2psim;

namespace {

//模拟网络里的地址
const uint32_t kStunIp = 0x0B000001;
const uint16_t kStunPort1 = 29904;
const uint16_t kStunPort2 = 29905;
const uint32_t kUnreachableIp = 0x0C000001;
const uint32_t kNatPublicA = 0x64000001;
const uint32_t kNatPublicB = 0xC8000001;
const uint32_t kPrivateA = 0xC0A8010A;
const uint32_t kPrivateB = 0xC0A80114;
const uint32_t kPrivateBOther = 0xC0A80214;
const uint16_t kPortA = 50001;
const uint16_t kPortB = 50002;
const uint8_t kPrefix24 = 24;

//两端的 userid 与会话号
const int64_t kUidA = 5127;
const int64_t kUidB = 5308;
const char* const kSessionId = "p2p-5127-5308-1790990000000-7";

//会话参数：打洞超时 3 秒；保活与断开判定取小一些，让用例跑得快
const int64_t kConnectTimeoutMs = 3000;
const int64_t kKeepaliveMs = 1000;
const int64_t kLinkTimeoutMs = 3000;

//每个用例最多推进的时间（毫秒）
const int64_t kLimitMs = 20000;

//默认链路延迟（毫秒）
const int64_t kLatencyMs = 10;

//模拟网络的随机数种子
const uint32_t kSeed = 7u;

//P2PLS6 的通道号与数据长度
const uint16_t kChannel = 7;
const uint16_t kOtherChannel = 8;
const size_t kDataBytes = 100;

//P2PLS6 多片消息的长度（字节）：UDP 路径上要切成好几片
const size_t kMultiFragBytes = 5000;

//P2PLS15：回调里等待另一个线程完成会话调用的最长时间（毫秒）与轮询间隔（毫秒）
const int kLockProbeWaitMs = 1000;
const int kLockProbePollMs = 5;

//模拟中转：请求之后多久连上（或报告失败）（毫秒），报文经中转的单程延迟（毫秒）
const int64_t kRelayConnectMs = 50;
const int64_t kRelayLatencyMs = 20;

//2302 里的中转地址（P2PLS10 核对 2304 的地址对）
const char* const kRelayHost = "127.0.0.1";
const int kRelayPort = 29903;

//解析一段 JSON 对象
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

std::string Compact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//一个按服务端格式写的 2302 包体；withRelay 为假时不带中转参数
std::string NotifyJson(int role, int64_t peerUid, bool withRelay = true)
{
    Json::Value v;
    v["p2psessionid"] = kSessionId;
    v["role"] = role;
    v["purpose"] = "test";
    v["featuresessionid"] = "";
    v["peeruid"] = std::to_string(peerUid);
    v["peerdeviceid"] = "livetest-peer";
    if (withRelay)
    {
        v["relayip"] = kRelayHost;
        v["relayport"] = kRelayPort;
        v["token"] = "token-text";
    }
    v["connecttimeoutms"] = (Json::Int64)kConnectTimeoutMs;
    v["keepalivems"] = (Json::Int64)kKeepaliveMs;
    v["linktimeoutms"] = (Json::Int64)kLinkTimeoutMs;
    return Compact(v);
}

P2PSessionInfo Info(int role, int64_t peerUid, bool withRelay = true)
{
    P2PSessionInfo info;
    REQUIRE(P2PSignalCodec::ParseSessionNotify(NotifyJson(role, peerUid, withRelay), info));
    return info;
}

//记录下来的一个信令
struct SentSignal
{
    uint16_t    m_cmd;    //命令号
    std::string m_body;   //包体
};

//一端：模拟网络里的 socket + 会话 + 观察者
class SessionPeer : public ISimReceiver, public IP2PDatagramSender, public IP2PSessionObserver, public IP2PChannelSink
{
public:
    SessionPeer(SimNetwork& net, int role, int64_t uid, int64_t peerUid, bool withRelay = true)
        : m_net(net)
        , m_socket(-1)
        , m_uid(uid)
        , m_session(Info(role, peerUid, withRelay), *this, *this)
        , m_ready(0)
        , m_failed(0)
        , m_failCode(-1)
        , m_closed(0)
        , m_closeReason(-1)
        , m_relayRequests(0)
        , m_relayRequestMs(0)
    {
    }

    void Attach(int natId, uint32_t ip, uint16_t port)
    {
        m_socket = m_net.AddSocket(natId, ip, port, this);
        P2PCandidate host;
        host.m_type = p2p_candidate_host;
        host.m_ip = ip;
        host.m_port = port;
        host.m_priority = CandidatePriority(kP2PTypePrefHost, kP2PLocalPrefPhysical);
        host.m_prefixLen = kPrefix24;
        m_hosts.push_back(host);
    }

    //发出的某个命令的个数与最后一个包体
    size_t Count(uint16_t cmd) const
    {
        size_t n = 0;
        for (size_t i = 0; i < m_signals.size(); ++i)
        {
            if (m_signals[i].m_cmd == cmd)
                ++n;
        }
        return n;
    }

    std::string Last(uint16_t cmd) const
    {
        for (size_t i = m_signals.size(); i > 0; --i)
        {
            if (m_signals[i - 1].m_cmd == cmd)
                return m_signals[i - 1].m_body;
        }
        return std::string();
    }

    virtual void OnSimReceive(uint32_t srcIp, uint16_t srcPort, const std::string& data, int64_t nowMs) override
    {
        m_session.OnDatagram(srcIp, srcPort, reinterpret_cast<const uint8_t*>(data.data()), data.size(), nowMs);
    }

    virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override
    {
        m_net.Send(m_socket, ip, port, std::string(reinterpret_cast<const char*>(data), len));
        return true;
    }

    virtual void SendSignal(uint16_t cmd, const std::string& body) override
    {
        SentSignal s;
        s.m_cmd = cmd;
        s.m_body = body;
        m_signals.push_back(s);
    }

    virtual void OnSessionReady(P2PLinkSession* session) override
    {
        (void)session;
        ++m_ready;
    }

    //P2PLS15：在回调里另起线程调用会话的方法，等它在 kLockProbeWaitMs 内完成；返回是否完成。线程存进 thread，
    //由调用方在回调返回之后 join（回调期间若互斥量被持有，那个线程要等回调返回才能完成）
    bool ProbeUnlocked(std::thread& thread, std::atomic<bool>& done)
    {
        done = false;
        thread = std::thread(&SessionPeer::QueryState, this, &done);
        for (int waited = 0; waited < kLockProbeWaitMs && !done; waited += kLockProbePollMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(kLockProbePollMs));
        return done;
    }

    //P2PLS15 的探测线程：调用一次要加会话互斥量的方法
    void QueryState(std::atomic<bool>* done)
    {
        m_session.State();
        *done = true;
    }

    virtual void OnSessionFailed(P2PLinkSession* session, int code) override
    {
        (void)session;
        ++m_failed;
        m_failCode = code;
    }

    virtual void OnSessionClosed(P2PLinkSession* session, int reason) override
    {
        (void)session;
        ++m_closed;
        m_closeReason = reason;
    }

    //请求连中转：只记下请求，由模拟中转在之后的推进里交回结果（不能在回调里同步交回）
    virtual void StartRelay(P2PLinkSession* session) override
    {
        (void)session;
        ++m_relayRequests;
        m_relayRequestMs = m_net.Now();
    }

    //请 IO 线程重新计算定时：模拟推进每一步都会重新取各会话的定时，这里不需要做什么
    virtual void RequestWake() override
    {
    }

    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        (void)channelId;
        m_received.assign(reinterpret_cast<const char*>(data), len);
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
    }

    SimNetwork&               m_net;           //模拟网络
    int                       m_socket;        //socket 编号
    int64_t                   m_uid;           //本方 userid
    P2PLinkSession            m_session;       //会话
    std::vector<P2PCandidate> m_hosts;         //本机候选
    std::vector<SentSignal>   m_signals;       //发出的信令
    int                       m_ready;         //就绪事件个数
    int                       m_failed;        //失败事件个数
    int                       m_failCode;      //最近一次失败的原因
    int                       m_closed;        //断开事件个数
    int                       m_closeReason;   //最近一次断开的原因
    std::string               m_received;      //最近收到的数据
    int                       m_relayRequests; //请求中转的次数
    int64_t                   m_relayRequestMs;//最近一次请求中转的模拟时刻（毫秒）
};

//模拟中转对一端请求的处理方式
enum sim_relay_mode
{
    sim_relay_ok    = 1,   //请求后 kRelayConnectMs 连上
    sim_relay_fail  = 2,   //请求后 kRelayConnectMs 报告连接失败
    sim_relay_never = 3,   //一直不连上也不报告（模拟对方迟迟不来）
};

/**
 *  模拟 imgserver 的一对一中转：两端都报到之后才转发，报到之前发来的报文丢弃并计数；每个报文延迟 kRelayLatencyMs 到达。
 *  报到即「连上」：连上时调用会话的 OnRelayConnected。
 */
class SimRelay
{
public:
    //一端的发送出口
    class End : public IP2PDatagramSender
    {
    public:
        End()
            : m_relay(NULL)
            , m_side(0)
        {
        }

        virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override
        {
            (void)ip;
            (void)port;
            m_relay->Send(m_side, std::string(reinterpret_cast<const char*>(data), len));
            return true;
        }

        SimRelay* m_relay;   //所属模拟中转
        int       m_side;    //本端下标（0 或 1）
    };

    SimRelay(SimNetwork& net, SessionPeer& a, SessionPeer& b)
        : m_net(net)
        , m_dropped(0)
        , m_relayed(0)
        , m_broken(false)
    {
        m_peers[0] = &a;
        m_peers[1] = &b;
        for (int i = 0; i < 2; ++i)
        {
            m_ends[i].m_relay = this;
            m_ends[i].m_side = i;
            m_joined[i] = false;
            m_reported[i] = false;
            m_mode[i] = sim_relay_ok;
        }
    }

    //设置一端请求中转时的处理方式
    void SetMode(int side, int mode)
    {
        m_mode[side] = mode;
    }

    //一端经中转发出一个报文
    void Send(int fromSide, const std::string& data)
    {
        const int to = 1 - fromSide;
        if (m_broken || !m_joined[fromSide] || !m_joined[to])
        {
            ++m_dropped;
            return;
        }
        Pending p;
        p.m_to = to;
        p.m_data = data;
        p.m_atMs = m_net.Now() + kRelayLatencyMs;
        m_queue.push_back(p);
        ++m_relayed;
    }

    //下一个需要处理的时刻：报文到达或请求的结果；没有时为 INT64_MAX
    int64_t NextMs() const
    {
        int64_t next = INT64_MAX;
        if (!m_queue.empty())
            next = m_queue.front().m_atMs;
        for (int i = 0; i < 2; ++i)
        {
            if (!m_reported[i] && m_peers[i]->m_relayRequests > 0 && m_mode[i] != sim_relay_never)
            {
                const int64_t at = m_peers[i]->m_relayRequestMs + kRelayConnectMs;
                if (at < next)
                    next = at;
            }
        }
        return next;
    }

    //处理到时刻的请求结果与报文
    void Run(int64_t nowMs)
    {
        for (int i = 0; i < 2; ++i)
        {
            if (m_reported[i] || m_peers[i]->m_relayRequests == 0 || m_mode[i] == sim_relay_never)
                continue;
            if (nowMs < m_peers[i]->m_relayRequestMs + kRelayConnectMs)
                continue;
            m_reported[i] = true;
            if (m_mode[i] == sim_relay_ok)
            {
                m_joined[i] = true;
                m_peers[i]->m_session.OnRelayConnected(m_ends[i], nowMs);
            }
            else
            {
                m_peers[i]->m_session.OnRelayFailed(nowMs);
            }
        }
        while (!m_queue.empty() && m_queue.front().m_atMs <= nowMs)
        {
            const Pending p = m_queue.front();
            m_queue.pop_front();
            if (!m_broken)
                m_peers[p.m_to]->m_session.OnRelayPacket(reinterpret_cast<const uint8_t*>(p.m_data.data()), p.m_data.size(),
                                                         nowMs);
        }
    }

    //中转连接断开：已报到的两端都收到 OnRelayBroken
    void Break(int64_t nowMs)
    {
        m_broken = true;
        for (int i = 0; i < 2; ++i)
        {
            if (m_joined[i])
                m_peers[i]->m_session.OnRelayBroken(nowMs);
        }
    }

    //一个在途报文
    struct Pending
    {
        int         m_to;     //目的端下标
        std::string m_data;   //报文
        int64_t     m_atMs;   //到达时刻（毫秒）
    };

    SimNetwork&         m_net;           //模拟网络（取时钟）
    SessionPeer*        m_peers[2];      //两端（借用）
    End                 m_ends[2];       //两端的发送出口
    bool                m_joined[2];     //两端是否已报到
    bool                m_reported[2];   //两端请求的结果是否已交回
    int                 m_mode[2];       //两端请求的处理方式，取 sim_relay_mode
    std::deque<Pending> m_queue;         //在途报文（按到达时刻排序：延迟固定，按发送先后即可）
    int                 m_dropped;       //因对方尚未报到或已断开而丢弃的报文个数
    int                 m_relayed;       //转发的报文个数
    bool                m_broken;        //中转连接是否已断开
};

//模拟 chatserver：把 from 新发出的 2303 加上 fromuid 转给 to
void ForwardSignals(SessionPeer& from, SessionPeer& to, size_t& forwarded, int64_t nowMs)
{
    size_t seen = 0;
    for (size_t i = 0; i < from.m_signals.size(); ++i)
    {
        if (from.m_signals[i].m_cmd != cmd_p2p_signal)
            continue;
        ++seen;
        if (seen <= forwarded)
            continue;
        Json::Value req;
        REQUIRE(ParseJson(from.m_signals[i].m_body, req));
        Json::Value fwd;
        fwd["p2psessionid"] = req["p2psessionid"];
        fwd["fromuid"] = std::to_string(from.m_uid);
        fwd["data"] = req["data"];
        P2PPeerSignal sig;
        REQUIRE(P2PSignalCodec::ParseSignalForward(Compact(fwd), sig));
        to.m_session.OnPeerSignal(sig.m_publicKey, sig.m_candidates, nowMs);
        forwarded = seen;
    }
}

//推进网络与两端会话的定时器；forwardA / forwardB 为假时不转发该端的 2303（模拟丢失）；relay 不为空时一并推进模拟中转
void Advance(SimNetwork& net, SessionPeer& a, SessionPeer& b, int64_t untilMs, size_t& fa, size_t& fb,
             bool forwardA = true, bool forwardB = true, SimRelay* relay = NULL)
{
    SessionPeer* peers[] = { &a, &b };
    while (true)
    {
        if (forwardA)
            ForwardSignals(a, b, fa, net.Now());
        if (forwardB)
            ForwardSignals(b, a, fb, net.Now());
        int64_t next = net.NextEventMs();
        for (size_t i = 0; i < 2; ++i)
        {
            if (peers[i]->m_session.NextTimerMs() < next)
                next = peers[i]->m_session.NextTimerMs();
        }
        if (relay != NULL && relay->NextMs() < next)
            next = relay->NextMs();
        if (next > untilMs)
            break;
        net.RunUntil(next);
        if (relay != NULL)
            relay->Run(net.Now());
        for (size_t i = 0; i < 2; ++i)
        {
            if (net.Now() >= peers[i]->m_session.NextTimerMs())
                peers[i]->m_session.OnTimer(net.Now());
        }
    }
    net.RunUntil(untilMs);
}

std::vector<P2PProbeServer> Stun()
{
    std::vector<P2PProbeServer> s(2);
    s[0].m_ip = kStunIp;
    s[0].m_port = kStunPort1;
    s[1].m_ip = kStunIp;
    s[1].m_port = kStunPort2;
    return s;
}

//2304 包体里的字段
Json::Value ResultOf(const SessionPeer& p)
{
    Json::Value v;
    REQUIRE(ParseJson(p.Last(cmd_p2p_result), v));
    return v;
}

} // anonymous namespace

TEST_CASE("P2PSC1 2301 的请求与应答", "[p2plink]")
{
    //带两个用途：按给定顺序写成字符串数组
    std::vector<std::string> purposes;
    purposes.push_back("folder");
    purposes.push_back("call");
    Json::Value req;
    REQUIRE(ParseJson(P2PSignalCodec::BuildRegister(purposes), req));
    CHECK(req["version"].asInt() == kP2PClientProtocolVersion);
    REQUIRE(req["purposes"].isArray());
    REQUIRE(req["purposes"].size() == 2);
    CHECK(req["purposes"][0].asString() == "folder");
    CHECK(req["purposes"][1].asString() == "call");

    //没有用途：照样带上空数组
    REQUIRE(ParseJson(P2PSignalCodec::BuildRegister(std::vector<std::string>()), req));
    REQUIRE(req["purposes"].isArray());
    CHECK(req["purposes"].size() == 0);

    P2PRegisterResult r;
    REQUIRE(P2PSignalCodec::ParseRegisterResponse(
        "{\"code\":0,\"msg\":\"\",\"enabled\":true,\"probe\":[{\"ip\":\"203.0.113.7\",\"port\":20004},"
        "{\"ip\":\"probe.example.com\",\"port\":20005}]}", r));
    CHECK(r.m_code == 0);
    CHECK(r.m_enabled);
    REQUIRE(r.m_probes.size() == 2);
    CHECK(r.m_probes[0].m_host == "203.0.113.7");
    CHECK(r.m_probes[0].m_port == 20004);
    CHECK(r.m_probes[1].m_host == "probe.example.com");

    //企业关闭：enabled 为假、没有 probe
    REQUIRE(P2PSignalCodec::ParseRegisterResponse("{\"code\":0,\"msg\":\"\",\"enabled\":false}", r));
    CHECK_FALSE(r.m_enabled);
    CHECK(r.m_probes.empty());
    //code 非 0：一律视为关闭
    REQUIRE(P2PSignalCodec::ParseRegisterResponse("{\"code\":906,\"msg\":\"x\",\"enabled\":true,\"probe\":[]}", r));
    CHECK(r.m_code == 906);
    CHECK_FALSE(r.m_enabled);
    //非法包体
    CHECK_FALSE(P2PSignalCodec::ParseRegisterResponse("not json", r));
    CHECK_FALSE(P2PSignalCodec::ParseRegisterResponse("{\"msg\":\"no code\"}", r));
}

TEST_CASE("P2PSC2 2302 的解析", "[p2plink]")
{
    P2PSessionInfo info;
    REQUIRE(P2PSignalCodec::ParseSessionNotify(NotifyJson(1, kUidB), info));
    CHECK(info.m_sessionId == kSessionId);
    CHECK(info.m_role == 1);
    CHECK(info.IsControlling());
    CHECK(info.m_purpose == "test");
    CHECK(info.m_peerUid == kUidB);
    CHECK(info.m_peerDeviceId == "livetest-peer");
    CHECK(info.m_relay.m_host == "127.0.0.1");
    CHECK(info.m_relay.m_port == 29903);
    CHECK(info.m_token == "token-text");
    CHECK(info.m_connectTimeoutMs == kConnectTimeoutMs);
    CHECK(info.m_keepaliveMs == kKeepaliveMs);
    CHECK(info.m_linkTimeoutMs == kLinkTimeoutMs);
    REQUIRE(P2PSignalCodec::ParseSessionNotify(NotifyJson(2, kUidA), info));
    CHECK_FALSE(info.IsControlling());

    //peeruid 写成整数也接受
    Json::Value v;
    REQUIRE(ParseJson(NotifyJson(1, kUidB), v));
    v["peeruid"] = (Json::Int64)kUidB;
    REQUIRE(P2PSignalCodec::ParseSessionNotify(Compact(v), info));
    CHECK(info.m_peerUid == kUidB);
    //角色不是 1 / 2、会话号为空、时间不为正数：失败
    v["role"] = 3;
    CHECK_FALSE(P2PSignalCodec::ParseSessionNotify(Compact(v), info));
    v["role"] = 1;
    v["p2psessionid"] = "";
    CHECK_FALSE(P2PSignalCodec::ParseSessionNotify(Compact(v), info));
    v["p2psessionid"] = kSessionId;
    v["linktimeoutms"] = 0;
    CHECK_FALSE(P2PSignalCodec::ParseSessionNotify(Compact(v), info));
}

TEST_CASE("P2PSC3 2303 / 2304 / 2305 / 2306", "[p2plink]")
{
    //2303：请求经服务端转发（加上 fromuid）后解析回原样的公钥与候选
    std::vector<P2PCandidate> c(1);
    c[0].m_type = p2p_candidate_host;
    c[0].m_ip = kPrivateA;
    c[0].m_port = kPortA;
    c[0].m_priority = CandidatePriority(kP2PTypePrefHost, kP2PLocalPrefPhysical);
    Json::Value req;
    REQUIRE(ParseJson(P2PSignalCodec::BuildSignal(kSessionId, "PUBKEY==", c), req));
    CHECK(req["p2psessionid"].asString() == kSessionId);
    CHECK(req["data"]["pubkey"].asString() == "PUBKEY==");
    REQUIRE(req["data"]["candidates"].isArray());
    Json::Value fwd;
    fwd["p2psessionid"] = kSessionId;
    fwd["fromuid"] = std::to_string(kUidA);
    fwd["data"] = req["data"];
    P2PPeerSignal sig;
    REQUIRE(P2PSignalCodec::ParseSignalForward(Compact(fwd), sig));
    CHECK(sig.m_sessionId == kSessionId);
    CHECK(sig.m_fromUid == kUidA);
    CHECK(sig.m_publicKey == "PUBKEY==");
    REQUIRE(sig.m_candidates.size() == 1);
    CHECK(sig.m_candidates[0].m_ip == kPrivateA);
    //没有公钥：失败
    fwd["data"].removeMember("pubkey");
    CHECK_FALSE(P2PSignalCodec::ParseSignalForward(Compact(fwd), sig));

    //2304：成功带 path；失败不带；NAT 判断未知时不带 nat
    Json::Value ok;
    REQUIRE(ParseJson(P2PSignalCodec::BuildResult(kSessionId, true, p2p_link_path_punched, 412, "a -> b",
                                                  p2p_nat_mapping_independent), ok));
    CHECK(ok["ok"].asBool());
    CHECK(ok["path"].asInt() == p2p_link_path_punched);
    CHECK(ok["elapsedms"].asInt() == 412);
    CHECK(ok["pair"].asString() == "a -> b");
    CHECK(ok["nat"]["samemapping"].asBool());
    Json::Value fail;
    REQUIRE(ParseJson(P2PSignalCodec::BuildResult(kSessionId, false, p2p_link_path_lan, 3000, "",
                                                  p2p_nat_mapping_unknown), fail));
    CHECK_FALSE(fail["ok"].asBool());
    CHECK_FALSE(fail.isMember("path"));
    CHECK_FALSE(fail.isMember("nat"));

    //2305 往返
    P2PCloseInfo close;
    REQUIRE(P2PSignalCodec::ParseClose(P2PSignalCodec::BuildClose(kSessionId, p2p_close_connect_fail, 903), close));
    CHECK(close.m_sessionId == kSessionId);
    CHECK(close.m_reason == p2p_close_connect_fail);
    CHECK(close.m_errCode == 903);
    CHECK_FALSE(P2PSignalCodec::ParseClose("{\"reason\":1}", close));

    //2306
    Json::Value open;
    REQUIRE(ParseJson(P2PSignalCodec::BuildDebugOpen(kUidB), open));
    CHECK(open["peeruid"].asString() == std::to_string(kUidB));
}

TEST_CASE("P2PLS1 同一局域网：就绪并上报局域网直连", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, Stun(), 0);
    b.m_session.Begin(b.m_hosts, Stun(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    CHECK(a.m_session.State() == p2p_session_connected);
    CHECK(b.m_session.State() == p2p_session_connected);
    CHECK(a.m_ready == 1);
    CHECK(b.m_ready == 1);
    CHECK(a.Count(cmd_p2p_signal) == 1);
    CHECK(b.Count(cmd_p2p_signal) == 1);
    REQUIRE(a.Count(cmd_p2p_result) == 1);
    CHECK(b.Count(cmd_p2p_result) == 1);
    const Json::Value r = ResultOf(a);
    CHECK(r["ok"].asBool());
    CHECK(r["path"].asInt() == p2p_link_path_lan);
    CHECK(a.m_session.Path() == p2p_link_path_lan);
    CHECK(a.Count(cmd_p2p_close) == 0);
}

TEST_CASE("P2PLS2 端口受限锥形 × 端口受限锥形：打洞就绪", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int natA = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    const int natB = net.AddNat(sim_nat_port_restricted, kNatPublicB);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateBOther, kPortB);
    a.m_session.Begin(a.m_hosts, Stun(), 0);
    b.m_session.Begin(b.m_hosts, Stun(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs + kP2PGatherBudgetMs, fa, fb);
    CHECK(a.m_ready == 1);
    CHECK(b.m_ready == 1);
    CHECK(ResultOf(a)["path"].asInt() == p2p_link_path_punched);
    CHECK(ResultOf(b)["path"].asInt() == p2p_link_path_punched);
    CHECK(ResultOf(a)["nat"]["samemapping"].asBool());
}

TEST_CASE("P2PLS3 对称型 × 对称型且中转也连不上：失败并上报", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int natA = net.AddNat(sim_nat_symmetric, kNatPublicA);
    const int natB = net.AddNat(sim_nat_symmetric, kNatPublicB);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateBOther, kPortB);
    SimRelay relay(net, a, b);
    relay.SetMode(0, sim_relay_fail);
    relay.SetMode(1, sim_relay_fail);
    a.m_session.Begin(a.m_hosts, Stun(), 0);
    b.m_session.Begin(b.m_hosts, Stun(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kLimitMs, fa, fb, true, true, &relay);
    CHECK(a.m_relayRequests == 1);
    CHECK(b.m_relayRequests == 1);
    CHECK(a.m_session.State() == p2p_session_failed);
    CHECK(b.m_session.State() == p2p_session_failed);
    CHECK(a.m_failed == 1);
    CHECK(b.m_failed == 1);
    CHECK(a.m_ready == 0);
    REQUIRE(a.Count(cmd_p2p_result) == 1);
    const Json::Value r = ResultOf(a);
    CHECK_FALSE(r["ok"].asBool());
    CHECK_FALSE(r.isMember("path"));
    REQUIRE(a.Count(cmd_p2p_close) == 1);
    P2PCloseInfo close;
    REQUIRE(P2PSignalCodec::ParseClose(a.Last(cmd_p2p_close), close));
    CHECK(close.m_reason == p2p_close_connect_fail);
    CHECK(b.Count(cmd_p2p_close) == 1);
}

TEST_CASE("P2PLS4 对方的 2303 先到：仍然就绪", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    //A 的探测服务器不可达：要等探测放弃（约 0.75 秒）才发 2303；B 没有探测服务器，立刻发 2303
    std::vector<P2PProbeServer> unreachable(1);
    unreachable[0].m_ip = kUnreachableIp;
    unreachable[0].m_port = kStunPort1;
    a.m_session.Begin(a.m_hosts, unreachable, 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kP2PProbeRetransmitMs, fa, fb);
    CHECK(b.Count(cmd_p2p_signal) == 1);
    CHECK(a.Count(cmd_p2p_signal) == 0);
    CHECK(a.m_session.State() == p2p_session_gathering);
    Advance(net, a, b, kLimitMs, fa, fb);
    CHECK(a.m_ready == 1);
    CHECK(b.m_ready == 1);
}

TEST_CASE("P2PLS5 对方的 2303 一直没来：按时判失败", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    //B 的 2303 不转发给 A
    Advance(net, a, b, kLimitMs, fa, fb, true, false);
    CHECK(a.m_session.State() == p2p_session_failed);
    CHECK(a.m_failed == 1);
    CHECK(a.Count(cmd_p2p_close) == 1);
}

TEST_CASE("P2PLS6 数据报通道", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    REQUIRE(a.m_session.State() == p2p_session_connected);
    REQUIRE(b.m_session.State() == p2p_session_connected);

    CHECK(a.m_session.OpenChannel(kChannel, p2p_channel_datagram, &a) == p2p_send_ok);
    CHECK(b.m_session.OpenChannel(kChannel, p2p_channel_datagram, &b) == p2p_send_ok);
    //同一通道号再开、空接收端：都不行
    CHECK(a.m_session.OpenChannel(kChannel, p2p_channel_datagram, &a) == p2p_send_bad_channel);
    CHECK(a.m_session.OpenChannel(kOtherChannel, p2p_channel_datagram, NULL) == p2p_send_bad_channel);

    std::string data(kDataBytes, '\0');
    for (size_t i = 0; i < kDataBytes; ++i)
        data[i] = (char)i;
    const int64_t now = net.Now();
    CHECK(a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>(data.data()), (uint32_t)data.size(), now) == p2p_send_ok);
    Advance(net, a, b, now + kLatencyMs * 3, fa, fb);
    CHECK(b.m_received == data);
    //未打开的通道、超长数据
    CHECK(a.m_session.SendData(kOtherChannel, reinterpret_cast<const uint8_t*>(data.data()), 1, net.Now()) == p2p_send_bad_channel);
    //多片消息：UDP 路径上切成好几片，对端收齐后原样交出
    const std::string multi(kMultiFragBytes, 'm');
    CHECK(a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>(multi.data()), (uint32_t)multi.size(), net.Now())
          == p2p_send_ok);
    Advance(net, a, b, net.Now() + kLatencyMs * 3, fa, fb);
    CHECK(b.m_received == multi);
    const std::string tooLong(a.m_session.MaxDatagramMessageBytes() + 1, 'x');
    CHECK(a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>(tooLong.data()), (uint32_t)tooLong.size(), net.Now())
          == p2p_send_too_large);
    P2PLinkStats st;
    memset(&st, 0, sizeof(st));
    st.m_cbSize = sizeof(st);
    a.m_session.GetStats(&st);
    CHECK(st.m_path == p2p_link_path_lan);
    CHECK(st.m_bytesSent == kDataBytes + kMultiFragBytes);
    P2PLinkStats sb;
    memset(&sb, 0, sizeof(sb));
    sb.m_cbSize = sizeof(sb);
    b.m_session.GetStats(&sb);
    CHECK(sb.m_bytesReceived == kDataBytes + kMultiFragBytes);
}

TEST_CASE("P2PLS7 保活与断开判定", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    REQUIRE(a.m_session.State() == p2p_session_connected);

    //空闲很久：靠保活维持，不断开
    const uint64_t sentBefore = net.SentCount();
    Advance(net, a, b, net.Now() + kLinkTimeoutMs * 3, fa, fb);
    CHECK(a.m_session.State() == p2p_session_connected);
    CHECK(b.m_session.State() == p2p_session_connected);
    CHECK(net.SentCount() > sentBefore);

    //网络全断：超过断开判定时间后两端都报告断开（链路超时）并各发一次 2305
    const int kAllLost = 1000;
    net.SetLinkProfile(kLatencyMs, 0, kAllLost);
    Advance(net, a, b, net.Now() + kLinkTimeoutMs + kKeepaliveMs * 2, fa, fb);
    CHECK(a.m_closed == 1);
    CHECK(b.m_closed == 1);
    CHECK(a.m_closeReason == p2p_close_link_timeout);
    CHECK(a.Count(cmd_p2p_close) == 1);
    CHECK(b.Count(cmd_p2p_close) == 1);
    CHECK(a.m_session.IsFinished());
}

TEST_CASE("P2PLS8 业务方关闭", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    REQUIRE(a.m_session.State() == p2p_session_connected);
    REQUIRE(a.m_session.OpenChannel(kChannel, p2p_channel_datagram, &a) == p2p_send_ok);

    a.m_session.Close(p2p_close_business_end, net.Now());
    CHECK(a.m_session.State() == p2p_session_closed);
    CHECK(a.m_closed == 0);
    CHECK(a.Count(cmd_p2p_close) == 1);
    P2PCloseInfo close;
    REQUIRE(P2PSignalCodec::ParseClose(a.Last(cmd_p2p_close), close));
    CHECK(close.m_reason == p2p_close_business_end);
    Advance(net, a, b, net.Now() + kLatencyMs * 3, fa, fb);
    CHECK(b.m_closed == 1);
    CHECK(b.m_closeReason == p2p_close_business_end);
    CHECK(b.Count(cmd_p2p_close) == 0);
    CHECK(a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>("x"), 1, net.Now()) == p2p_send_closed);
}

TEST_CASE("P2PLS9 服务端推来 2305", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    //就绪之后：报告断开（对方掉线），不再发 2305
    {
        SimNetwork net(kSeed);
        net.SetLinkProfile(kLatencyMs, 0, 0);
        const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
        SessionPeer a(net, 1, kUidA, kUidB);
        SessionPeer b(net, 2, kUidB, kUidA);
        a.Attach(nat, kPrivateA, kPortA);
        b.Attach(nat, kPrivateB, kPortB);
        a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
        b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
        size_t fa = 0;
        size_t fb = 0;
        Advance(net, a, b, kConnectTimeoutMs, fa, fb);
        REQUIRE(a.m_session.State() == p2p_session_connected);
        a.m_session.OnServerClose(p2p_close_peer_offline, 903, net.Now());
        CHECK(a.m_closed == 1);
        CHECK(a.m_closeReason == p2p_close_peer_offline);
        CHECK(a.Count(cmd_p2p_close) == 0);
        CHECK(a.m_session.IsFinished());
    }
    //就绪之前：报告失败，带上错误码
    {
        SimNetwork net(kSeed);
        const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
        SessionPeer a(net, 1, kUidA, kUidB);
        a.Attach(nat, kPrivateA, kPortA);
        a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
        a.m_session.OnServerClose(p2p_close_connect_fail, 903, 5);
        CHECK(a.m_failed == 1);
        CHECK(a.m_failCode == 903);
        CHECK(a.Count(cmd_p2p_close) == 0);
        CHECK(a.m_session.State() == p2p_session_failed);
    }
}

namespace {

//建一对对称型 NAT 后面的会话（打洞必然失败），带模拟中转
struct SymmetricPair
{
    SimNetwork  m_net;     //模拟网络
    SessionPeer m_a;       //发起方
    SessionPeer m_b;       //应答方
    SimRelay    m_relay;   //模拟中转
    size_t      m_fa;      //已转发的 A 的 2303 个数
    size_t      m_fb;      //已转发的 B 的 2303 个数

    explicit SymmetricPair(bool withRelay = true)
        : m_net(kSeed)
        , m_a(m_net, 1, kUidA, kUidB, withRelay)
        , m_b(m_net, 2, kUidB, kUidA, withRelay)
        , m_relay(m_net, m_a, m_b)
        , m_fa(0)
        , m_fb(0)
    {
        m_net.SetLinkProfile(kLatencyMs, 0, 0);
        m_net.AddStunServer(kStunIp, kStunPort1);
        m_net.AddStunServer(kStunIp, kStunPort2);
        const int natA = m_net.AddNat(sim_nat_symmetric, kNatPublicA);
        const int natB = m_net.AddNat(sim_nat_symmetric, kNatPublicB);
        m_a.Attach(natA, kPrivateA, kPortA);
        m_b.Attach(natB, kPrivateBOther, kPortB);
    }

    void Begin()
    {
        m_a.m_session.Begin(m_a.m_hosts, Stun(), 0);
        m_b.m_session.Begin(m_b.m_hosts, Stun(), 0);
    }

    void AdvanceTo(int64_t untilMs)
    {
        Advance(m_net, m_a, m_b, untilMs, m_fa, m_fb, true, true, &m_relay);
    }
};

} // namespace

TEST_CASE("P2PLS10 对称型 × 对称型、中转可用：经中转就绪并收发数据", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SymmetricPair p;
    p.Begin();
    p.AdvanceTo(kLimitMs);
    REQUIRE(p.m_a.m_session.State() == p2p_session_connected);
    REQUIRE(p.m_b.m_session.State() == p2p_session_connected);
    CHECK(p.m_a.m_ready == 1);
    CHECK(p.m_b.m_ready == 1);
    CHECK(p.m_a.m_relayRequests == 1);
    CHECK(p.m_b.m_relayRequests == 1);
    CHECK(p.m_a.m_session.Path() == p2p_link_path_relay);
    CHECK(p.m_b.m_session.Path() == p2p_link_path_relay);
    //两端各只发一次 2304，成功、路径为中转、地址对写中转地址
    REQUIRE(p.m_a.Count(cmd_p2p_result) == 1);
    REQUIRE(p.m_b.Count(cmd_p2p_result) == 1);
    const Json::Value r = ResultOf(p.m_a);
    CHECK(r["ok"].asBool());
    CHECK(r["path"].asInt() == p2p_link_path_relay);
    CHECK(r["pair"].asString() == std::string("relay ") + kRelayHost + ":" + std::to_string(kRelayPort));
    CHECK(ResultOf(p.m_b)["path"].asInt() == p2p_link_path_relay);
    CHECK(p.m_a.Count(cmd_p2p_close) == 0);
    //应答方晚 kP2PControlledExtraMs 才改走中转：发起方先发的「选定」被丢弃，靠重发完成
    CHECK(p.m_relay.m_dropped > 0);

    //数据报经中转往返
    REQUIRE(p.m_a.m_session.OpenChannel(kChannel, p2p_channel_datagram, &p.m_a) == p2p_send_ok);
    REQUIRE(p.m_b.m_session.OpenChannel(kChannel, p2p_channel_datagram, &p.m_b) == p2p_send_ok);
    const std::string ping = "ping over relay";
    const std::string pong = "pong over relay";
    const int relayedBefore = p.m_relay.m_relayed;
    CHECK(p.m_a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>(ping.data()), (uint32_t)ping.size(),
                                   p.m_net.Now()) == p2p_send_ok);
    CHECK(p.m_b.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>(pong.data()), (uint32_t)pong.size(),
                                   p.m_net.Now()) == p2p_send_ok);
    p.AdvanceTo(p.m_net.Now() + kRelayLatencyMs * 3);
    CHECK(p.m_b.m_received == ping);
    CHECK(p.m_a.m_received == pong);
    CHECK(p.m_relay.m_relayed >= relayedBefore + 2);

    //空闲很久：保活经中转维持，不断开
    p.AdvanceTo(p.m_net.Now() + kLinkTimeoutMs * 3);
    CHECK(p.m_a.m_session.State() == p2p_session_connected);
    CHECK(p.m_b.m_session.State() == p2p_session_connected);
}

TEST_CASE("P2PLS11 强制中转：同一局域网也不打洞，直接经中转就绪", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    SimRelay relay(net, a, b);
    a.m_session.SetForceRelay(true);
    b.m_session.SetForceRelay(true);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb, true, true, &relay);
    REQUIRE(a.m_session.State() == p2p_session_connected);
    REQUIRE(b.m_session.State() == p2p_session_connected);
    CHECK(a.m_session.Path() == p2p_link_path_relay);
    CHECK(ResultOf(a)["path"].asInt() == p2p_link_path_relay);
    //没有探测服务器、不打洞：UDP 上一个报文都没有发
    CHECK(net.SentCount() == 0);
}

TEST_CASE("P2PLS12 中转就绪之后中转连接断开：两端报告断开并发 2305", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SymmetricPair p;
    p.Begin();
    p.AdvanceTo(kLimitMs);
    REQUIRE(p.m_a.m_session.State() == p2p_session_connected);
    REQUIRE(p.m_b.m_session.State() == p2p_session_connected);
    p.m_relay.Break(p.m_net.Now());
    CHECK(p.m_a.m_closed == 1);
    CHECK(p.m_b.m_closed == 1);
    CHECK(p.m_a.m_closeReason == p2p_close_link_timeout);
    CHECK(p.m_a.Count(cmd_p2p_close) == 1);
    CHECK(p.m_b.Count(cmd_p2p_close) == 1);
    CHECK(p.m_a.m_session.IsFinished());
}

TEST_CASE("P2PLS13 连上中转但对方一直不来：按中转时限判失败", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SymmetricPair p;
    p.m_relay.SetMode(1, sim_relay_never);
    p.Begin();
    p.AdvanceTo(kLimitMs);
    CHECK(p.m_a.m_relayRequests == 1);
    CHECK(p.m_a.m_session.State() == p2p_session_failed);
    CHECK(p.m_a.m_failed == 1);
    CHECK(p.m_a.m_ready == 0);
    REQUIRE(p.m_a.Count(cmd_p2p_close) == 1);
    P2PCloseInfo close;
    REQUIRE(P2PSignalCodec::ParseClose(p.m_a.Last(cmd_p2p_close), close));
    CHECK(close.m_reason == p2p_close_connect_fail);
    CHECK_FALSE(ResultOf(p.m_a)["ok"].asBool());
    //发起方一直在重发「选定」，全部因对方没报到而被丢弃
    CHECK(p.m_relay.m_dropped > 1);
    CHECK(p.m_relay.m_relayed == 0);
}

TEST_CASE("P2PLS14 2302 没带中转参数：打洞失败后直接失败，不请求中转", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SymmetricPair p(false);
    CHECK_FALSE(p.m_a.m_session.CanRelay());
    p.Begin();
    p.AdvanceTo(kLimitMs);
    CHECK(p.m_a.m_relayRequests == 0);
    CHECK(p.m_b.m_relayRequests == 0);
    CHECK(p.m_a.m_session.State() == p2p_session_failed);
    CHECK(p.m_b.m_session.State() == p2p_session_failed);
    CHECK(p.m_a.Count(cmd_p2p_close) == 1);
}

namespace {

//P2PLS15 的应答方：就绪通知里探测会话互斥量
class ReadyProbePeer : public SessionPeer
{
public:
    ReadyProbePeer(SimNetwork& net, int role, int64_t uid, int64_t peerUid)
        : SessionPeer(net, role, uid, peerUid)
        , m_unlockedInReady(false)
    {
    }

    virtual void OnSessionReady(P2PLinkSession* session) override
    {
        SessionPeer::OnSessionReady(session);
        m_unlockedInReady = ProbeUnlocked(m_readyThread, m_readyDone);
    }

    bool              m_unlockedInReady;   //就绪回调里另一个线程是否在回调返回之前完成了会话调用
    std::thread       m_readyThread;       //就绪回调里起的探测线程
    std::atomic<bool> m_readyDone;         //探测线程是否已完成
};

//P2PLS15 的接收端：收到数据的回调里探测会话互斥量
class DataProbeSink : public IP2PChannelSink
{
public:
    explicit DataProbeSink(SessionPeer& owner)
        : m_owner(owner)
        , m_calls(0)
        , m_unlockedInData(false)
    {
    }

    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        (void)channelId;
        (void)data;
        (void)len;
        ++m_calls;
        m_unlockedInData = m_owner.ProbeUnlocked(m_thread, m_done);
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
    }

    SessionPeer&      m_owner;            //所属一端（借用）
    int               m_calls;            //OnData 的次数
    bool              m_unlockedInData;   //收到数据的回调里另一个线程是否在回调返回之前完成了会话调用
    std::thread       m_thread;           //探测线程
    std::atomic<bool> m_done;             //探测线程是否已完成
};

} // namespace

TEST_CASE("P2PLS15 回调期间会话互斥量没有被持有", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    ReadyProbePeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    REQUIRE(b.m_ready == 1);
    if (b.m_readyThread.joinable())
        b.m_readyThread.join();
    CHECK(b.m_unlockedInReady);

    DataProbeSink sink(b);
    REQUIRE(a.m_session.OpenChannel(kChannel, p2p_channel_datagram, &a) == p2p_send_ok);
    REQUIRE(b.m_session.OpenChannel(kChannel, p2p_channel_datagram, &sink) == p2p_send_ok);
    REQUIRE(a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>("probe"), 5, net.Now()) == p2p_send_ok);
    Advance(net, a, b, net.Now() + kLatencyMs * 3, fa, fb);
    REQUIRE(sink.m_calls == 1);
    if (sink.m_thread.joinable())
        sink.m_thread.join();
    CHECK(sink.m_unlockedInData);
}

namespace {

//P2PLS16：可靠通道的通道号、每次传输的字节数、模拟丢包率（千分比）与等待传完的最长模拟时间（毫秒）
const uint16_t kStatsChannel = 11;
const size_t kStatsBytes = 200 * 1024;
const int kStatsLossPermille = 100;
const int64_t kStatsWaitMs = 30000;

//P2PLS16：往返时延的允许范围（毫秒）：模拟链路往返 2 × kLatencyMs，加上 KCP 的刷新间隔与确认延迟
const uint32_t kStatsMinRttMs = (uint32_t)(2 * kLatencyMs);
const uint32_t kStatsMaxRttMs = (uint32_t)(2 * kLatencyMs + 15);

//P2PLS17 生成可辨认数据用的模数（不是 2 的幂，避免与分段边界对齐）
const size_t kStatsPatternModulo = 251;

//拼接可靠通道收到的字节流
class StreamSink : public IP2PChannelSink
{
public:
    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        (void)channelId;
        m_stream.append(reinterpret_cast<const char*>(data), len);
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
    }

    std::string m_stream;   //收到的字节流
};

P2PLinkStats StatsOf(P2PLinkSession& session)
{
    P2PLinkStats st;
    memset(&st, 0, sizeof(st));
    st.m_cbSize = sizeof(st);
    session.GetStats(&st);
    return st;
}

} // namespace

TEST_CASE("P2PLS16 统计：积压、往返时延、丢包率与累计字节数", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    SessionPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    REQUIRE(a.m_session.State() == p2p_session_connected);

    StreamSink sinkA;
    StreamSink sinkB;
    REQUIRE(a.m_session.OpenChannel(kStatsChannel, p2p_channel_reliable, &sinkA) == p2p_send_ok);
    REQUIRE(b.m_session.OpenChannel(kStatsChannel, p2p_channel_reliable, &sinkB) == p2p_send_ok);

    //无丢包：传输期间有积压，传完并收到确认后积压为 0、丢包率为 0，往返时延接近模拟链路
    const std::string data(kStatsBytes, 's');
    REQUIRE(a.m_session.SendData(kStatsChannel, reinterpret_cast<const uint8_t*>(data.data()), (uint32_t)data.size(),
                                 net.Now()) == p2p_send_ok);
    CHECK(StatsOf(a.m_session).m_queuedBytes > 0);
    const int64_t deadline = net.Now() + kStatsWaitMs;
    while (net.Now() < deadline && (sinkB.m_stream.size() < data.size() || StatsOf(a.m_session).m_queuedBytes > 0))
        Advance(net, a, b, net.Now() + kLatencyMs, fa, fb);
    REQUIRE(sinkB.m_stream == data);
    P2PLinkStats st = StatsOf(a.m_session);
    CHECK(st.m_queuedBytes == 0);
    CHECK(st.m_lossPermille == 0);
    CHECK(st.m_rttMs >= kStatsMinRttMs);
    CHECK(st.m_rttMs <= kStatsMaxRttMs);
    CHECK(st.m_bytesSent == kStatsBytes);
    CHECK(StatsOf(b.m_session).m_bytesReceived == kStatsBytes);

    //10% 丢包：同样完整送达，丢包率大于 0
    net.SetLinkProfile(kLatencyMs, 0, kStatsLossPermille);
    REQUIRE(a.m_session.SendData(kStatsChannel, reinterpret_cast<const uint8_t*>(data.data()), (uint32_t)data.size(),
                                 net.Now()) == p2p_send_ok);
    const int64_t deadline2 = net.Now() + kStatsWaitMs;
    while (net.Now() < deadline2 && sinkB.m_stream.size() < 2 * data.size())
        Advance(net, a, b, net.Now() + kLatencyMs, fa, fb);
    REQUIRE(sinkB.m_stream.size() == 2 * data.size());
    st = StatsOf(a.m_session);
    CHECK(st.m_lossPermille > 0);
    CHECK(st.m_bytesSent == 2 * kStatsBytes);
}

namespace {

//P2PLS17：传输量（字节）
const size_t kBatchBytes = 300 * 1024;

//P2PLS17 的一端：声明支持成批发送，记录每一批的情况，再按段交给模拟网络
class BatchingPeer : public SessionPeer
{
public:
    BatchingPeer(SimNetwork& net, int role, int64_t uid, int64_t peerUid)
        : SessionPeer(net, role, uid, peerUid)
        , m_batches(0)
        , m_multiBatches(0)
        , m_maxSegments(0)
        , m_badLayout(false)
    {
    }

    virtual size_t BatchSegmentBytes() const override
    {
        return kP2PMaxDatagramBytes;
    }

    virtual bool SendBatch(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override
    {
        ++m_batches;
        const size_t segments = (len + kP2PMaxDatagramBytes - 1) / kP2PMaxDatagramBytes;
        if (segments > 1)
            ++m_multiBatches;
        if (segments > m_maxSegments)
            m_maxSegments = segments;
        //除最后一段外都是满载报文：总长度减去最后一段之后恰好是整数个满载报文
        const size_t last = len - (segments - 1) * kP2PMaxDatagramBytes;
        if (last == 0 || last > kP2PMaxDatagramBytes)
            m_badLayout = true;
        return IP2PDatagramSender::SendBatch(ip, port, data, len);
    }

    int    m_batches;        //SendBatch 的次数
    int    m_multiBatches;   //多于一段的批次数
    size_t m_maxSegments;    //一批最多的段数
    bool   m_badLayout;      //是否出现过段的排布不合规
};

} // namespace

TEST_CASE("P2PLS17 成批发送：可靠通道传输时多段一批发出", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    BatchingPeer a(net, 1, kUidA, kUidB);
    BatchingPeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    REQUIRE(a.m_session.State() == p2p_session_connected);

    StreamSink sinkA;
    StreamSink sinkB;
    REQUIRE(a.m_session.OpenChannel(kStatsChannel, p2p_channel_reliable, &sinkA) == p2p_send_ok);
    REQUIRE(b.m_session.OpenChannel(kStatsChannel, p2p_channel_reliable, &sinkB) == p2p_send_ok);
    std::string data(kBatchBytes, '\0');
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = (char)(i % kStatsPatternModulo);
    REQUIRE(a.m_session.SendData(kStatsChannel, reinterpret_cast<const uint8_t*>(data.data()), (uint32_t)data.size(),
                                 net.Now()) == p2p_send_ok);
    const int64_t deadline = net.Now() + kStatsWaitMs;
    while (net.Now() < deadline && sinkB.m_stream.size() < data.size())
        Advance(net, a, b, net.Now() + kLatencyMs, fa, fb);
    REQUIRE(sinkB.m_stream == data);
    CHECK(a.m_multiBatches > 0);
    CHECK(a.m_maxSegments <= kP2PMaxBatchSegments);
    CHECK_FALSE(a.m_badLayout);
    CHECK_FALSE(b.m_badLayout);
}

namespace {

//P2PLS18：接收端在回调里停留的时长（毫秒）
const int kCallbackHoldMs = 200;

//P2PLS18 的一端：别的线程上的发包与信令只计数，不碰模拟网络与信令表（它们不是线程安全的，测试线程同时在用）
class ThreadAwarePeer : public SessionPeer
{
public:
    ThreadAwarePeer(SimNetwork& net, int role, int64_t uid, int64_t peerUid)
        : SessionPeer(net, role, uid, peerUid)
        , m_owner(std::this_thread::get_id())
        , m_offThreadCalls(0)
    {
    }

    virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override
    {
        if (std::this_thread::get_id() != m_owner)
        {
            ++m_offThreadCalls;
            return true;
        }
        return SessionPeer::SendTo(ip, port, data, len);
    }

    virtual void SendSignal(uint16_t cmd, const std::string& body) override
    {
        if (std::this_thread::get_id() != m_owner)
        {
            ++m_offThreadCalls;
            return;
        }
        SessionPeer::SendSignal(cmd, body);
    }

    std::thread::id  m_owner;            //测试线程
    std::atomic<int> m_offThreadCalls;   //别的线程上的发包与信令次数
};

//P2PLS18 的接收端：第一次回调时另起线程 Close 链路，自己再停留 kCallbackHoldMs，记录 Close 是否在回调返回之前返回
class CloseRaceSink : public IP2PChannelSink
{
public:
    explicit CloseRaceSink(P2PLinkSession& session)
        : m_session(session)
        , m_calls(0)
        , m_callbackDone(false)
        , m_closeReturnedEarly(false)
    {
    }

    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        (void)channelId;
        (void)data;
        (void)len;
        ++m_calls;
        if (m_calls > 1)
            return;
        m_closer = std::thread(&CloseRaceSink::CloseProc, this);
        std::this_thread::sleep_for(std::chrono::milliseconds(kCallbackHoldMs));
        m_callbackDone = true;
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
    }

    //别的线程上的 Close：返回时回调若还没结束，说明 Close 没有等它
    void CloseProc()
    {
        m_session.Close(p2p_close_business_end, 0);
        m_closeReturnedEarly = !m_callbackDone;
    }

    P2PLinkSession&   m_session;              //被测会话（借用）
    int               m_calls;                //OnData 的次数
    std::atomic<bool> m_callbackDone;         //第一次回调是否已结束
    std::atomic<bool> m_closeReturnedEarly;   //Close 是否在回调结束之前返回
    std::thread       m_closer;               //调用 Close 的线程
};

} // namespace

TEST_CASE("P2PLS18 别的线程 Close 时等正在进行的回调结束，之后不再分发", "[p2plink]")
{
    REQUIRE(sodium_init() >= 0);
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLatencyMs, 0, 0);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SessionPeer a(net, 1, kUidA, kUidB);
    ThreadAwarePeer b(net, 2, kUidB, kUidA);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateB, kPortB);
    a.m_session.Begin(a.m_hosts, std::vector<P2PProbeServer>(), 0);
    b.m_session.Begin(b.m_hosts, std::vector<P2PProbeServer>(), 0);
    size_t fa = 0;
    size_t fb = 0;
    Advance(net, a, b, kConnectTimeoutMs, fa, fb);
    REQUIRE(b.m_session.State() == p2p_session_connected);

    CloseRaceSink sink(b.m_session);
    REQUIRE(a.m_session.OpenChannel(kChannel, p2p_channel_datagram, &a) == p2p_send_ok);
    REQUIRE(b.m_session.OpenChannel(kChannel, p2p_channel_datagram, &sink) == p2p_send_ok);
    //两条消息同时到达：属于同一轮分发
    REQUIRE(a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>("one"), 3, net.Now()) == p2p_send_ok);
    REQUIRE(a.m_session.SendData(kChannel, reinterpret_cast<const uint8_t*>("two"), 3, net.Now()) == p2p_send_ok);
    Advance(net, a, b, net.Now() + kLatencyMs * 3, fa, fb);
    if (sink.m_closer.joinable())
        sink.m_closer.join();
    CHECK(sink.m_calls == 1);
    CHECK(sink.m_callbackDone);
    CHECK_FALSE(sink.m_closeReturnedEarly);
    CHECK(b.m_session.IsFinished());
    //Close 在别的线程上发出了关闭报文与 2305
    CHECK(b.m_offThreadCalls == 2);
}
