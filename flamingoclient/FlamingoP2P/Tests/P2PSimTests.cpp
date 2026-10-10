/**
 *  FlamingoP2P 连通性检查在模拟网络下的单元测试（Suite P2PN，标签 [p2psim]），P2PSimTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 3 期 3c（6.3 节、第 11 节「模拟网络下的连通性检查」）。每个场景都走完整流程：
 *  两端各用 P2PAddressProber 向模拟的两个地址探测服务器取公网映射地址，交换候选地址（应答方晚 kSignalSkewMs 开始，
 *  模拟 2303 的先后），再由 P2PConnectivityChecker 打洞；报文都经 P2PLinkCipher 加密。
 *    - P2PN0 模拟网络自检：四种 NAT 的映射与过滤、局域网直通、全丢包、同种子结果一致；
 *    - P2PN1 同一局域网：选定对方的同网段本机地址；
 *    - P2PN2 两边都是公网主机：选定对方的本机地址；
 *    - P2PN3 完全锥形 × 端口受限锥形：经公网映射地址打通；
 *    - P2PN4 端口受限锥形 × 端口受限锥形，两边私网网段相同（对方的「同网段」本机地址其实不通）：仍经公网映射地址打通；
 *    - P2PN5 对称型 × 完全锥形（对称型一方分别做发起方与应答方）：经对端反射地址打通；
 *    - P2PN6 对称型 × 端口受限锥形：打不通，发起方在建链超时时刻判失败，应答方多等 kP2PControlledExtraMs；
 *    - P2PN7 对称型 × 对称型：打不通；
 *    - P2PN8 端口受限锥形 × 端口受限锥形，30% 丢包、延迟 40～70 毫秒（乱序）：仍在超时之内打通；
 *    - P2PN9 地址探测服务器不可达、两边在不同 NAT 后：没有公网映射地址，判失败；
 *    - P2PN10 直接驱动状态机：发起方收不到选定确认时按间隔重发选定；应答方对同一地址的选定重复确认、
 *             对其它地址的选定不理（选定后不切换）；认证不过的报文不会走到状态机。
 *
 *  典型用法：FlamingoUnit.exe "[p2psim]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <sodium.h>
#include <string.h>
#include <string>
#include <vector>

#include "../Core/P2PCrypto.h"
#include "../Core/P2PPacket.h"
#include "../Ice/P2PAddressProber.h"
#include "../Ice/P2PCandidate.h"
#include "../Ice/P2PConnectivityChecker.h"
#include "P2PSimNet.h"

using namespace p2p;
using namespace p2psim;

namespace {

//两个地址探测服务器（同一公网地址的两个端口）
const uint32_t kStunIp = 0x0B000001;
const uint16_t kStunPort1 = 29904;
const uint16_t kStunPort2 = 29905;

//两个 NAT 的公网地址、两台公网主机的地址
const uint32_t kNatPublicA = 0x64000001;
const uint32_t kNatPublicB = 0xC8000001;
const uint32_t kPublicHostA = 0x32000001;
const uint32_t kPublicHostB = 0x3C000001;

//私网地址：A 在 192.168.1.10，B 在 192.168.2.20；P2PN4 里 B 也用 192.168.1.20（与 A 同网段但不同局域网）
const uint32_t kPrivateA = 0xC0A8010A;
const uint32_t kPrivateB = 0xC0A80214;
const uint32_t kPrivateBSameSubnet = 0xC0A80114;
const uint8_t kPrefix24 = 24;

//两端 socket 的端口
const uint16_t kPortA = 50001;
const uint16_t kPortB = 50002;

//建链超时（毫秒）：取 2302 的默认值
const int64_t kConnectTimeoutMs = 3000;

//应答方晚开始的时间（毫秒）：模拟 2303 先后到达
const int64_t kSignalSkewMs = 30;

//每个场景最多推进的时间（毫秒）：超时加应答方的额外等待，再留余量
const int64_t kScenarioLimitMs = 10000;

//模拟网络的默认链路：延迟 10 毫秒、无抖动、无丢包
const int64_t kDefaultLatencyMs = 10;

//P2PN8 的链路：延迟 40 毫秒、抖动 30 毫秒、丢包 30%
const int64_t kLossyLatencyMs = 40;
const int64_t kLossyJitterMs = 30;
const int kLossyPermille = 300;

//模拟网络的随机数种子
const uint32_t kSeed = 20261004u;

//一端：模拟网络里的一个 socket，带密钥、加解密、地址探测与连通性检查
class SimPeer : public ISimReceiver, public IP2PDatagramSender
{
public:
    SimPeer(SimNetwork& net, bool controlling)
        : m_net(net)
        , m_socket(-1)
        , m_controlling(controlling)
        , m_prober(*this)
        , m_checker(*this, m_cipher, controlling, kConnectTimeoutMs)
        , m_doneMs(-1)
        , m_stunDatagrams(0)
        , m_rejected(0)
    {
        m_keys.Generate();
    }

    //接到模拟网络上，并登记本机地址（作为本方的 host 候选）
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

    //本方要发给对方的候选：本机地址加地址探测得到的公网映射地址
    std::vector<P2PCandidate> Candidates() const
    {
        std::vector<P2PCandidate> all = m_hosts;
        const std::vector<P2PCandidate> srflx = m_prober.SrflxCandidates(kP2PLocalPrefPhysical);
        all.insert(all.end(), srflx.begin(), srflx.end());
        return all;
    }

    virtual void OnSimReceive(uint32_t srcIp, uint16_t srcPort, const std::string& data, int64_t nowMs) override
    {
        const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
        const int kind = ClassifyDatagram(d, data.size());
        if (kind == p2p_datagram_stun)
        {
            ++m_stunDatagrams;
            m_prober.OnStunDatagram(d, data.size(), nowMs);
            return;
        }
        if (kind != p2p_datagram_link)
            return;
        uint8_t type = 0;
        std::string plain;
        if (m_cipher.Open(d, data.size(), type, plain) != p2p_open_ok)
        {
            ++m_rejected;
            return;
        }
        m_checker.OnPacket(srcIp, srcPort, type, plain, nowMs);
    }

    virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override
    {
        m_net.Send(m_socket, ip, port, std::string(reinterpret_cast<const char*>(data), len));
        return true;
    }

    //检查状态到了终态（就绪或失败）时记下时刻
    void NoteDone(int64_t nowMs)
    {
        if (m_doneMs < 0 && (m_checker.State() == p2p_check_connected || m_checker.State() == p2p_check_failed))
            m_doneMs = nowMs;
    }

    SimNetwork&              m_net;            //所在的模拟网络
    int                      m_socket;         //模拟网络里的 socket 编号
    bool                     m_controlling;    //是否为发起方
    P2PKeyPair               m_keys;           //本方密钥对
    P2PLinkCipher            m_cipher;         //加解密
    P2PAddressProber         m_prober;         //地址探测
    P2PConnectivityChecker   m_checker;        //连通性检查
    std::vector<P2PCandidate> m_hosts;         //本机候选
    int64_t                  m_doneMs;         //检查到终态的时刻；-1 表示还没有
    int                      m_stunDatagrams;  //收到的 STUN 报文个数
    int                      m_rejected;       //解密认证不过的报文个数
};

//两端交换公钥后各自推导会话密钥（相当于经 2303 交换 pubkey）
void ExchangeKeys(SimPeer& a, SimPeer& b)
{
    REQUIRE(a.m_cipher.Init(a.m_controlling, a.m_keys, b.m_keys.PublicKey()));
    REQUIRE(b.m_cipher.Init(b.m_controlling, b.m_keys, a.m_keys.PublicKey()));
}

//把网络与两端的定时器一起推进到 untilMs：按时刻先后处理报文到达与各自的定时器
void Advance(SimNetwork& net, SimPeer& a, SimPeer& b, bool probing, bool checking, int64_t untilMs)
{
    SimPeer* peers[] = { &a, &b };
    while (true)
    {
        int64_t next = net.NextEventMs();
        for (size_t i = 0; i < 2; ++i)
        {
            if (probing && !peers[i]->m_prober.IsDone() && peers[i]->m_prober.NextTimerMs() < next)
                next = peers[i]->m_prober.NextTimerMs();
            if (checking && peers[i]->m_checker.State() == p2p_check_running && peers[i]->m_checker.NextTimerMs() < next)
                next = peers[i]->m_checker.NextTimerMs();
        }
        if (next > untilMs)
            break;
        net.RunUntil(next);
        for (size_t i = 0; i < 2; ++i)
        {
            if (probing && !peers[i]->m_prober.IsDone() && net.Now() >= peers[i]->m_prober.NextTimerMs())
                peers[i]->m_prober.OnTimer(net.Now());
            if (checking && peers[i]->m_checker.State() == p2p_check_running && net.Now() >= peers[i]->m_checker.NextTimerMs())
                peers[i]->m_checker.OnTimer(net.Now());
            peers[i]->NoteDone(net.Now());
        }
        if (checking && a.m_doneMs >= 0 && b.m_doneMs >= 0)
            break;
    }
    net.RunUntil(untilMs);
}

//跑一个完整场景：地址探测、交换候选、打洞。返回发起方开始打洞的时刻
int64_t RunScenario(SimNetwork& net, SimPeer& a, SimPeer& b)
{
    REQUIRE(sodium_init() >= 0);
    ExchangeKeys(a, b);
    std::vector<P2PProbeServer> stun(2);
    stun[0].m_ip = kStunIp;
    stun[0].m_port = kStunPort1;
    stun[1].m_ip = kStunIp;
    stun[1].m_port = kStunPort2;
    a.m_prober.Start(stun, net.Now());
    b.m_prober.Start(stun, net.Now());
    //地址探测最多约 1 秒
    const int64_t probeLimit = net.Now() + 2000;
    while (!(a.m_prober.IsDone() && b.m_prober.IsDone()) && net.Now() < probeLimit)
        Advance(net, a, b, true, false, net.Now() + kP2PProbeRetransmitMs);
    REQUIRE(a.m_prober.IsDone());
    REQUIRE(b.m_prober.IsDone());

    const int64_t start = net.Now();
    a.m_checker.Start(a.m_hosts, b.Candidates(), start);
    Advance(net, a, b, false, true, start + kSignalSkewMs);
    b.m_checker.Start(b.m_hosts, a.Candidates(), net.Now());
    Advance(net, a, b, false, true, start + kScenarioLimitMs);
    return start;
}

//检查两端都就绪，且选定的是能互通的同一对地址
void CheckConnected(SimNetwork& net, const SimPeer& a, const SimPeer& b)
{
    INFO("state A " << a.m_checker.State() << ", state B " << b.m_checker.State());
    REQUIRE(a.m_checker.State() == p2p_check_connected);
    REQUIRE(b.m_checker.State() == p2p_check_connected);
    CHECK(a.m_rejected == 0);
    CHECK(b.m_rejected == 0);
    (void)net;
}

} // anonymous namespace

TEST_CASE("P2PN0 模拟网络自检", "[p2psim]")
{
    //一个只记录收包的接收方
    struct Recorder : public ISimReceiver
    {
        Recorder()
            : m_count(0)
            , m_lastIp(0)
            , m_lastPort(0)
        {
        }

        virtual void OnSimReceive(uint32_t srcIp, uint16_t srcPort, const std::string& data, int64_t nowMs) override
        {
            (void)data;
            (void)nowMs;
            ++m_count;
            m_lastIp = srcIp;
            m_lastPort = srcPort;
        }
        int      m_count;      //收到的报文个数
        uint32_t m_lastIp;     //最近一个报文的来源地址
        uint16_t m_lastPort;   //最近一个报文的来源端口
    };

    const uint16_t kOtherPort = 7000;
    const std::string kData = "x";
    const int natTypes[] = { sim_nat_full_cone, sim_nat_restricted, sim_nat_port_restricted, sim_nat_symmetric };
    for (size_t t = 0; t < sizeof(natTypes) / sizeof(natTypes[0]); ++t)
    {
        INFO("nat type " << natTypes[t]);
        SimNetwork net(kSeed);
        Recorder inner;
        Recorder outer;
        Recorder outer2;
        Recorder outerSamePort;
        const int nat = net.AddNat((SimNatType)natTypes[t], kNatPublicA);
        const int in = net.AddSocket(nat, kPrivateA, kPortA, &inner);
        const int out = net.AddSocket(-1, kPublicHostB, kPortB, &outer);
        const int out2 = net.AddSocket(-1, kPublicHostB, kOtherPort, &outer2);
        const int out3 = net.AddSocket(-1, kPublicHostA, kPortB, &outerSamePort);

        //内部先向 outer 发：outer 看到的来源是 NAT 的公网地址与第一个外部端口
        net.Send(in, kPublicHostB, kPortB, kData);
        net.RunUntil(kDefaultLatencyMs * 2);
        REQUIRE(outer.m_count == 1);
        CHECK(outer.m_lastIp == kNatPublicA);
        CHECK(outer.m_lastPort == kSimFirstExternalPort);
        const uint16_t ext = outer.m_lastPort;

        //outer 回包：四种 NAT 都放行
        net.Send(out, kNatPublicA, ext, kData);
        //同一 IP 的另一端口回包：完全锥形与受限锥形放行，端口受限锥形与对称型不放行
        net.Send(out2, kNatPublicA, ext, kData);
        //没发过包的 IP：只有完全锥形放行
        net.Send(out3, kNatPublicA, ext, kData);
        net.RunUntil(net.Now() + kDefaultLatencyMs * 2);
        const int expected = (natTypes[t] == sim_nat_full_cone) ? 3
                           : (natTypes[t] == sim_nat_restricted) ? 2 : 1;
        CHECK(inner.m_count == expected);

        //内部再向另一个目的地址发：对称型分配新的外部端口，锥形沿用原端口
        net.Send(in, kPublicHostA, kPortB, kData);
        net.RunUntil(net.Now() + kDefaultLatencyMs * 2);
        if (natTypes[t] == sim_nat_symmetric)
            CHECK(outerSamePort.m_lastPort != ext);
        else
            CHECK(outerSamePort.m_lastPort == ext);
    }

    //局域网：同一 NAT 后面的两台主机按私网地址直接互通，看到的是对方的私网地址
    {
        SimNetwork net(kSeed);
        Recorder x;
        Recorder y;
        const int nat = net.AddNat(sim_nat_symmetric, kNatPublicA);
        const int sx = net.AddSocket(nat, kPrivateA, kPortA, &x);
        net.AddSocket(nat, kPrivateBSameSubnet, kPortB, &y);
        net.Send(sx, kPrivateBSameSubnet, kPortB, kData);
        net.RunUntil(kDefaultLatencyMs * 2);
        CHECK(y.m_count == 1);
        CHECK(y.m_lastIp == kPrivateA);
        CHECK(y.m_lastPort == kPortA);
    }

    //全丢包与可复现：同种子、同操作序列的结果相同
    uint64_t delivered[2] = { 0, 0 };
    for (int round = 0; round < 2; ++round)
    {
        SimNetwork net(kSeed);
        Recorder r;
        const int s = net.AddSocket(-1, kPublicHostA, kPortA, &r);
        net.AddSocket(-1, kPublicHostB, kPortB, &r);
        net.SetLinkProfile(kDefaultLatencyMs, kLossyJitterMs, kLossyPermille);
        const int kPackets = 200;
        for (int i = 0; i < kPackets; ++i)
            net.Send(s, kPublicHostB, kPortB, kData);
        net.RunUntil(kScenarioLimitMs);
        delivered[round] = net.DeliveredCount();
        CHECK(net.DeliveredCount() + net.DroppedCount() == (uint64_t)kPackets);
        CHECK(net.DroppedCount() > 0);
    }
    CHECK(delivered[0] == delivered[1]);
    {
        SimNetwork net(kSeed);
        Recorder r;
        const int s = net.AddSocket(-1, kPublicHostA, kPortA, &r);
        net.AddSocket(-1, kPublicHostB, kPortB, &r);
        const int kAllLost = 1000;
        net.SetLinkProfile(kDefaultLatencyMs, 0, kAllLost);
        net.Send(s, kPublicHostB, kPortB, kData);
        net.RunUntil(kScenarioLimitMs);
        CHECK(r.m_count == 0);
    }
}

TEST_CASE("P2PN1 同一局域网：选定同网段本机地址", "[p2psim]")
{
    SimNetwork net(kSeed);
    net.SetLinkProfile(kDefaultLatencyMs, 0, 0);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int nat = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(nat, kPrivateA, kPortA);
    b.Attach(nat, kPrivateBSameSubnet, kPortB);
    RunScenario(net, a, b);
    CheckConnected(net, a, b);
    CHECK(a.m_checker.SelectedOrigin() == p2p_check_origin_same_subnet);
    CHECK(a.m_checker.SelectedIp() == kPrivateBSameSubnet);
    CHECK(a.m_checker.SelectedPort() == kPortB);
    CHECK(b.m_checker.SelectedIp() == kPrivateA);
    CHECK(b.m_checker.SelectedPort() == kPortA);
}

TEST_CASE("P2PN2 两边都是公网主机", "[p2psim]")
{
    SimNetwork net(kSeed);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(-1, kPublicHostA, kPortA);
    b.Attach(-1, kPublicHostB, kPortB);
    RunScenario(net, a, b);
    CheckConnected(net, a, b);
    CHECK(a.m_checker.SelectedIp() == kPublicHostB);
    CHECK(a.m_checker.SelectedPort() == kPortB);
    //公网主机的本机地址与公网映射地址相同：选定的是本机地址那一项
    CHECK(a.m_checker.SelectedOrigin() == p2p_check_origin_host);
    CHECK(b.m_checker.SelectedIp() == kPublicHostA);
}

TEST_CASE("P2PN3 完全锥形 × 端口受限锥形", "[p2psim]")
{
    SimNetwork net(kSeed);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int natA = net.AddNat(sim_nat_full_cone, kNatPublicA);
    const int natB = net.AddNat(sim_nat_port_restricted, kNatPublicB);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateB, kPortB);
    RunScenario(net, a, b);
    CheckConnected(net, a, b);
    CHECK(a.m_checker.SelectedIp() == kNatPublicB);
    CHECK(b.m_checker.SelectedIp() == kNatPublicA);
    CHECK(a.m_checker.SelectedOrigin() == p2p_check_origin_srflx);
    CHECK(a.m_prober.NatMapping() == p2p_nat_mapping_independent);
}

TEST_CASE("P2PN4 端口受限锥形 × 端口受限锥形，私网网段相同", "[p2psim]")
{
    SimNetwork net(kSeed);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int natA = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    const int natB = net.AddNat(sim_nat_port_restricted, kNatPublicB);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateBSameSubnet, kPortB);
    RunScenario(net, a, b);
    CheckConnected(net, a, b);
    //对方的「同网段」本机地址在另一个局域网里，不通；选定的是公网映射地址
    CHECK(a.m_checker.SelectedIp() == kNatPublicB);
    CHECK(a.m_checker.SelectedOrigin() == p2p_check_origin_srflx);
    CHECK(b.m_checker.SelectedIp() == kNatPublicA);
}

TEST_CASE("P2PN5 对称型 × 完全锥形", "[p2psim]")
{
    for (int symmetricIsControlling = 0; symmetricIsControlling < 2; ++symmetricIsControlling)
    {
        INFO("symmetric side is controlling: " << symmetricIsControlling);
        SimNetwork net(kSeed);
        net.AddStunServer(kStunIp, kStunPort1);
        net.AddStunServer(kStunIp, kStunPort2);
        const int natSym = net.AddNat(sim_nat_symmetric, kNatPublicA);
        const int natCone = net.AddNat(sim_nat_full_cone, kNatPublicB);
        SimPeer a(net, true);
        SimPeer b(net, false);
        SimPeer& sym = symmetricIsControlling ? a : b;
        SimPeer& cone = symmetricIsControlling ? b : a;
        sym.Attach(natSym, kPrivateA, kPortA);
        cone.Attach(natCone, kPrivateB, kPortB);
        RunScenario(net, a, b);
        CheckConnected(net, a, b);
        CHECK(sym.m_prober.NatMapping() == p2p_nat_mapping_dependent);
        //对称型一方选定的是锥形一方的公网映射地址；锥形一方看到的是对称型一方新开的映射（对端反射地址）
        CHECK(sym.m_checker.SelectedIp() == kNatPublicB);
        CHECK(cone.m_checker.SelectedIp() == kNatPublicA);
        CHECK(cone.m_checker.SelectedOrigin() == p2p_check_origin_prflx);
    }
}

TEST_CASE("P2PN6 对称型 × 端口受限锥形：打不通，按时判失败", "[p2psim]")
{
    SimNetwork net(kSeed);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int natA = net.AddNat(sim_nat_symmetric, kNatPublicA);
    const int natB = net.AddNat(sim_nat_port_restricted, kNatPublicB);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateB, kPortB);
    const int64_t start = RunScenario(net, a, b);
    CHECK(a.m_checker.State() == p2p_check_failed);
    CHECK(b.m_checker.State() == p2p_check_failed);
    //发起方在建链超时时刻判失败（定时器按探测间隔推进，允许晚一个间隔）；应答方多等一段
    CHECK(a.m_doneMs >= start + kConnectTimeoutMs);
    CHECK(a.m_doneMs <= start + kConnectTimeoutMs + kP2PCheckProbeIntervalMs);
    CHECK(b.m_doneMs >= start + kSignalSkewMs + kConnectTimeoutMs + kP2PControlledExtraMs);
    CHECK(b.m_doneMs <= start + kSignalSkewMs + kConnectTimeoutMs + kP2PControlledExtraMs + kP2PCheckProbeIntervalMs);
    //超时之前一直在重发探测
    CHECK(a.m_checker.ProbesSent() >= (uint64_t)(kConnectTimeoutMs / kP2PCheckProbeIntervalMs));
}

TEST_CASE("P2PN7 对称型 × 对称型：打不通", "[p2psim]")
{
    SimNetwork net(kSeed);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int natA = net.AddNat(sim_nat_symmetric, kNatPublicA);
    const int natB = net.AddNat(sim_nat_symmetric, kNatPublicB);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateB, kPortB);
    RunScenario(net, a, b);
    CHECK(a.m_checker.State() == p2p_check_failed);
    CHECK(b.m_checker.State() == p2p_check_failed);
}

TEST_CASE("P2PN8 30% 丢包与乱序下仍能打通", "[p2psim]")
{
    SimNetwork net(kSeed);
    net.SetLinkProfile(kLossyLatencyMs, kLossyJitterMs, kLossyPermille);
    net.AddStunServer(kStunIp, kStunPort1);
    net.AddStunServer(kStunIp, kStunPort2);
    const int natA = net.AddNat(sim_nat_port_restricted, kNatPublicA);
    const int natB = net.AddNat(sim_nat_port_restricted, kNatPublicB);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateB, kPortB);
    const int64_t start = RunScenario(net, a, b);
    CheckConnected(net, a, b);
    CHECK(a.m_doneMs < start + kConnectTimeoutMs);
    CHECK(net.DroppedCount() > 0);
}

TEST_CASE("P2PN9 地址探测服务器不可达：判失败", "[p2psim]")
{
    SimNetwork net(kSeed);
    //不加地址探测服务器：请求发出去没有应答
    const int natA = net.AddNat(sim_nat_full_cone, kNatPublicA);
    const int natB = net.AddNat(sim_nat_full_cone, kNatPublicB);
    SimPeer a(net, true);
    SimPeer b(net, false);
    a.Attach(natA, kPrivateA, kPortA);
    b.Attach(natB, kPrivateB, kPortB);
    RunScenario(net, a, b);
    CHECK(a.m_prober.SrflxCandidates(kP2PLocalPrefPhysical).empty());
    CHECK(a.m_checker.State() == p2p_check_failed);
    CHECK(b.m_checker.State() == p2p_check_failed);
}

TEST_CASE("P2PN10 选定确认丢失与选定后不切换", "[p2psim]")
{
    REQUIRE(sodium_init() >= 0);

    //记录发出的报文并用对方的密钥解开
    struct Capture : public IP2PDatagramSender
    {
        virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override
        {
            m_ips.push_back(ip);
            m_ports.push_back(port);
            m_data.push_back(std::string(reinterpret_cast<const char*>(data), len));
            return true;
        }
        std::vector<uint32_t>    m_ips;     //目的地址
        std::vector<uint16_t>    m_ports;   //目的端口
        std::vector<std::string> m_data;    //报文
    };

    const uint32_t kAddrX = kNatPublicB;
    const uint32_t kAddrY = kPublicHostB;
    P2PKeyPair ka;
    P2PKeyPair kb;
    ka.Generate();
    kb.Generate();
    P2PLinkCipher ca;
    P2PLinkCipher cb;
    REQUIRE(ca.Init(true, ka, kb.PublicKey()));
    REQUIRE(cb.Init(false, kb, ka.PublicKey()));

    //应答方：收到 X 的选定即就绪并确认；X 再发选定，再确认；Y 的选定不理
    {
        Capture out;
        P2PConnectivityChecker controlled(out, cb, false, kConnectTimeoutMs);
        std::vector<P2PCandidate> remotes(1);
        remotes[0].m_type = p2p_candidate_srflx;
        remotes[0].m_ip = kAddrX;
        remotes[0].m_port = kPortA;
        remotes[0].m_priority = CandidatePriority(kP2PTypePrefSrflx, kP2PLocalPrefPhysical);
        controlled.Start(std::vector<P2PCandidate>(), remotes, 0);
        controlled.OnPacket(kAddrX, kPortA, p2p_packet_nominate, std::string(), 10);
        CHECK(controlled.State() == p2p_check_connected);
        CHECK(controlled.SelectedIp() == kAddrX);
        size_t acksToX = 0;
        for (size_t i = 0; i < out.m_data.size(); ++i)
        {
            uint8_t type = 0;
            std::string plain;
            if (ca.Open(reinterpret_cast<const uint8_t*>(out.m_data[i].data()), out.m_data[i].size(), type, plain) == p2p_open_ok
                && type == p2p_packet_nominate_ack && out.m_ips[i] == kAddrX)
                ++acksToX;
        }
        CHECK(acksToX == 1);
        const size_t before = out.m_data.size();
        controlled.OnPacket(kAddrX, kPortA, p2p_packet_nominate, std::string(), 20);
        CHECK(out.m_data.size() == before + 1);
        controlled.OnPacket(kAddrY, kPortA, p2p_packet_nominate, std::string(), 30);
        CHECK(out.m_data.size() == before + 1);
        CHECK(controlled.SelectedIp() == kAddrX);
    }

    //发起方：探测得到应答后，过了等待时间就选定；收不到确认时每个间隔重发一次选定，收到确认即就绪
    {
        Capture out;
        P2PConnectivityChecker controlling(out, ca, true, kConnectTimeoutMs);
        std::vector<P2PCandidate> remotes(1);
        remotes[0].m_type = p2p_candidate_srflx;
        remotes[0].m_ip = kAddrX;
        remotes[0].m_port = kPortB;
        remotes[0].m_priority = CandidatePriority(kP2PTypePrefSrflx, kP2PLocalPrefPhysical);
        controlling.Start(std::vector<P2PCandidate>(), remotes, 0);
        REQUIRE(out.m_data.size() == 1);
        //用应答方的密钥解开探测，取出事务号，构造探测应答（事务号 + 看到的来源地址）
        uint8_t type = 0;
        std::string probe;
        REQUIRE(cb.Open(reinterpret_cast<const uint8_t*>(out.m_data[0].data()), out.m_data[0].size(), type, probe) == p2p_open_ok);
        REQUIRE(type == p2p_packet_probe);
        REQUIRE(probe.size() >= kP2PCheckTxidBytes);
        std::string ack = probe.substr(0, kP2PCheckTxidBytes);
        ack.append(6, '\0');
        controlling.OnPacket(kAddrX, kPortB, p2p_packet_probe_ack, ack, 5);
        //唯一的一项、也是最优先的一项已连通：立即选定
        size_t nominates = 0;
        for (size_t i = 0; i < out.m_data.size(); ++i)
        {
            std::string plain;
            if (cb.Open(reinterpret_cast<const uint8_t*>(out.m_data[i].data()), out.m_data[i].size(), type, plain) == p2p_open_ok
                && type == p2p_packet_nominate)
                ++nominates;
        }
        CHECK(nominates == 1);
        CHECK(controlling.State() == p2p_check_running);
        //没有确认：每个间隔重发一次
        const size_t sentBefore = out.m_data.size();
        controlling.OnTimer(5 + kP2PCheckProbeIntervalMs);
        controlling.OnTimer(5 + 2 * kP2PCheckProbeIntervalMs);
        CHECK(out.m_data.size() >= sentBefore + 2);
        //确认来自别的地址：不算
        controlling.OnPacket(kAddrY, kPortB, p2p_packet_nominate_ack, std::string(), 120);
        CHECK(controlling.State() == p2p_check_running);
        controlling.OnPacket(kAddrX, kPortB, p2p_packet_nominate_ack, std::string(), 130);
        CHECK(controlling.State() == p2p_check_connected);
        CHECK(controlling.SelectedIp() == kAddrX);
        CHECK(controlling.SelectedPort() == kPortB);
        CHECK(controlling.NextTimerMs() == INT64_MAX);
    }
}
