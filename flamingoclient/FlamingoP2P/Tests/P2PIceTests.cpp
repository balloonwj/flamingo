/**
 *  FlamingoP2P 候选地址与地址探测客户端的单元测试（Suite P2PC，标签 [p2pice]），P2PIceTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 3 期 3b（6.1、6.2 节）：
 *    - P2PC1 网卡筛选与优先级：排除未连接、回环网卡与回环 / 链路本地 / 全零地址；虚拟网卡降低优先级而不排除；同一 IP
 *            只留优先级高的一份；按优先级排列且最多 kP2PMaxCandidates 个；
 *    - P2PC2 Binding 请求：32 字节、类型 0x0001、长度字段 12、魔数、事务号、SOFTWARE 属性；
 *    - P2PC3 应答解析：服务端的 32 字节应答与带其它属性的标准应答都能取出 XOR-MAPPED-ADDRESS（RFC 5769 第 2.2 节
 *            示例里的那个属性，192.0.2.1:32853）；事务号不符、类型不对、魔数不对、长度不符、属性越界、只有 IPv6 地址、
 *            报文截断时都失败；
 *    - P2PC4 探测流程（假发送出口 + 手动推进的时间）：两个服务器都应答且端口相同 / 不同；没有应答时按间隔重发、发满
 *            次数后放弃；只有一个服务器应答；重复、迟到与事务号不认识的应答被忽略；服务器列表为空时立即完成；
 *    - P2PC5 候选地址的 JSON 编解码与 IP 文本互转：往返一致，不合法的条目被跳过，最多读 kP2PMaxCandidates 个。
 *
 *  典型用法：FlamingoUnit.exe "[p2pice]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <sodium.h>
#include <string.h>
#include <string>
#include <vector>

#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "../Ice/P2PAddressProber.h"
#include "../Ice/P2PCandidate.h"
#include "../Ice/P2PStunClient.h"

using namespace p2p;

namespace {

//几个 IPv4 地址（主机字节序）
const uint32_t kIp192_168_1_10 = 0xC0A8010A;
const uint32_t kIp10_0_0_5 = 0x0A000005;
const uint32_t kIp172_20_16_1 = 0xAC141001;
const uint32_t kIp172_16_0_9 = 0xAC100009;
const uint32_t kIp169_254_3_4 = 0xA9FE0304;
const uint32_t kIp127_0_0_1 = 0x7F000001;
const uint32_t kIp1_2_3_4 = 0x01020304;
const uint32_t kIpServer = 0x0A010101;

//本链路 socket 的端口、两个探测端口、两个映射端口
const uint16_t kLocalPort = 54429;
const uint16_t kProbePort1 = 29904;
const uint16_t kProbePort2 = 29905;
const uint16_t kMappedPort = 5000;
const uint16_t kMappedPort2 = 6000;

//RFC 5769 第 2.2 节示例的事务号与映射地址
const uint8_t kRfcTxid[kP2PStunTxidBytes] = { 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae };
const uint32_t kRfcIp = 0xC0000201;
const uint16_t kRfcPort = 32853;

//RFC 5769 第 2.2 节示例里的 XOR-MAPPED-ADDRESS 属性（12 字节）
const uint8_t kRfcXorMapped[] = { 0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43 };

//魔数
const uint32_t kMagic = 0x2112A442;

//一个前缀长度：/24 与 /20
const uint8_t kPrefix24 = 24;
const uint8_t kPrefix20 = 20;

//记录下来的一次发送
struct SentDatagram
{
    uint32_t    m_ip;     //目的地址
    uint16_t    m_port;   //目的端口
    std::string m_data;   //报文
};

//假的发送出口：只记录
class FakeSender : public IP2PDatagramSender
{
public:
    virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override
    {
        SentDatagram d;
        d.m_ip = ip;
        d.m_port = port;
        d.m_data.assign(reinterpret_cast<const char*>(data), len);
        m_sent.push_back(d);
        return true;
    }

    //发往某个端口的次数
    size_t CountTo(uint16_t port) const
    {
        size_t n = 0;
        for (size_t i = 0; i < m_sent.size(); ++i)
        {
            if (m_sent[i].m_port == port)
                ++n;
        }
        return n;
    }

    //最近一次发往某个端口的报文
    std::string LastTo(uint16_t port) const
    {
        for (size_t i = m_sent.size(); i > 0; --i)
        {
            if (m_sent[i - 1].m_port == port)
                return m_sent[i - 1].m_data;
        }
        return std::string();
    }

    std::vector<SentDatagram> m_sent;   //全部发送记录
};

void Append16(std::string& s, uint16_t v)
{
    s.push_back((char)(v >> 8));
    s.push_back((char)(v & 0xFF));
}

void Append32(std::string& s, uint32_t v)
{
    Append16(s, (uint16_t)(v >> 16));
    Append16(s, (uint16_t)(v & 0xFFFF));
}

//按 RFC 5389 构造一个 Binding 成功应答：头 + 若干属性（attrs 已按 4 字节对齐）
std::string MakeResponse(const uint8_t* txid, const std::string& attrs, uint16_t type = 0x0101, uint32_t magic = kMagic)
{
    std::string s;
    Append16(s, type);
    Append16(s, (uint16_t)attrs.size());
    Append32(s, magic);
    s.append(reinterpret_cast<const char*>(txid), kP2PStunTxidBytes);
    s += attrs;
    return s;
}

//XOR-MAPPED-ADDRESS 属性（IPv4）
std::string XorMapped(uint32_t ip, uint16_t port)
{
    std::string a;
    Append16(a, 0x0020);
    Append16(a, 8);
    a.push_back(0);
    a.push_back(0x01);
    Append16(a, (uint16_t)(port ^ (kMagic >> 16)));
    Append32(a, ip ^ kMagic);
    return a;
}

//取请求里的事务号
void TxidOf(const std::string& request, uint8_t* txid)
{
    REQUIRE(request.size() >= 20);
    memcpy(txid, request.data() + 8, kP2PStunTxidBytes);
}

bool Parse(const std::string& resp, const uint8_t* txid, uint32_t& ip, uint16_t& port)
{
    return P2PStunClient::ParseResponse(reinterpret_cast<const uint8_t*>(resp.data()), resp.size(), txid, ip, port);
}

P2PAdapterInfo Adapter(const char* name, const char* desc, bool up, bool loopback, uint32_t ip, uint8_t prefix)
{
    P2PAdapterInfo a;
    a.m_name = name;
    a.m_description = desc;
    a.m_up = up;
    a.m_loopback = loopback;
    P2PAdapterAddress addr;
    addr.m_ip = ip;
    addr.m_prefixLen = prefix;
    a.m_addresses.push_back(addr);
    return a;
}

std::vector<P2PProbeServer> TwoServers()
{
    std::vector<P2PProbeServer> servers(2);
    servers[0].m_ip = kIpServer;
    servers[0].m_port = kProbePort1;
    servers[1].m_ip = kIpServer;
    servers[1].m_port = kProbePort2;
    return servers;
}

//让 prober 收到对某个探测端口最近一次请求的应答
bool Answer(P2PAddressProber& prober, const FakeSender& sender, uint16_t probePort, uint32_t ip, uint16_t port, int64_t now)
{
    uint8_t txid[kP2PStunTxidBytes];
    TxidOf(sender.LastTo(probePort), txid);
    const std::string resp = MakeResponse(txid, XorMapped(ip, port));
    return prober.OnStunDatagram(reinterpret_cast<const uint8_t*>(resp.data()), resp.size(), now);
}

} // anonymous namespace

TEST_CASE("P2PC1 网卡筛选与优先级", "[p2pice]")
{
    std::vector<P2PAdapterInfo> adapters;
    adapters.push_back(Adapter("Ethernet", "Intel(R) Ethernet Connection I219-V", true, false, kIp192_168_1_10, kPrefix24));
    P2PAdapterInfo wifi = Adapter("Wi-Fi", "Intel(R) Wi-Fi 6 AX201", true, false, kIp10_0_0_5, kPrefix24);
    P2PAdapterAddress linkLocal;
    linkLocal.m_ip = kIp169_254_3_4;
    linkLocal.m_prefixLen = 16;
    wifi.m_addresses.push_back(linkLocal);
    adapters.push_back(wifi);
    adapters.push_back(Adapter("Down", "Realtek PCIe GbE", false, false, kIp172_16_0_9, 16));
    adapters.push_back(Adapter("Loopback Pseudo-Interface 1", "Software Loopback Interface 1", true, true, kIp127_0_0_1, 8));
    adapters.push_back(Adapter("vEthernet (WSL)", "Hyper-V Virtual Ethernet Adapter", true, false, kIp172_20_16_1, kPrefix20));
    //同一 IP 出现在虚拟网卡上：只留普通网卡那一份
    adapters.push_back(Adapter("VPN", "Hyper-V Virtual Ethernet Adapter #2", true, false, kIp192_168_1_10, kPrefix24));
    adapters.push_back(Adapter("Zero", "Intel(R) Ethernet", true, false, 0, 0));

    const std::vector<P2PCandidate> c = BuildHostCandidates(adapters, kLocalPort);
    REQUIRE(c.size() == 3);
    CHECK(c[0].m_ip == kIp192_168_1_10);
    CHECK(c[1].m_ip == kIp10_0_0_5);
    CHECK(c[2].m_ip == kIp172_20_16_1);
    for (size_t i = 0; i < c.size(); ++i)
    {
        CHECK(c[i].m_type == p2p_candidate_host);
        CHECK(c[i].m_port == kLocalPort);
    }
    CHECK(c[0].m_prefixLen == kPrefix24);
    CHECK(c[2].m_prefixLen == kPrefix20);
    CHECK(c[0].m_priority == CandidatePriority(kP2PTypePrefHost, kP2PLocalPrefPhysical));
    CHECK(c[2].m_priority == CandidatePriority(kP2PTypePrefHost, kP2PLocalPrefVirtual));
    CHECK(c[0].m_priority > c[2].m_priority);
    //本机地址无论如何都比公网映射地址优先
    CHECK(CandidatePriority(kP2PTypePrefHost, kP2PLocalPrefVirtual) > CandidatePriority(kP2PTypePrefSrflx, kP2PLocalPrefPhysical));

    //地址与网卡的分类
    CHECK(IsLoopbackIp(kIp127_0_0_1));
    CHECK_FALSE(IsLoopbackIp(kIp10_0_0_5));
    CHECK(IsLinkLocalIp(kIp169_254_3_4));
    CHECK_FALSE(IsLinkLocalIp(kIp172_16_0_9));
    const char* virtualDescs[] = { "VMware Virtual Ethernet Adapter for VMnet8", "VirtualBox Host-Only Ethernet Adapter",
                                   "hyper-v virtual ethernet adapter", "Docker NAT" };
    for (size_t i = 0; i < sizeof(virtualDescs) / sizeof(virtualDescs[0]); ++i)
        CHECK(IsVirtualAdapter(Adapter("x", virtualDescs[i], true, false, kIp10_0_0_5, kPrefix24)));
    CHECK_FALSE(IsVirtualAdapter(Adapter("x", "Intel(R) Wi-Fi 6 AX201", true, false, kIp10_0_0_5, kPrefix24)));
    //名称里带 vEthernet、描述却不带关键词的，也按名称识别
    CHECK(IsVirtualAdapter(Adapter("vEthernet (Default Switch)", "Some Adapter", true, false, kIp10_0_0_5, kPrefix24)));

    //地址很多时最多取 kP2PMaxCandidates 个
    std::vector<P2PAdapterInfo> many;
    for (uint32_t i = 0; i < kP2PMaxCandidates + 3; ++i)
        many.push_back(Adapter("n", "Intel(R) Ethernet", true, false, kIp10_0_0_5 + i, kPrefix24));
    CHECK(BuildHostCandidates(many, kLocalPort).size() == kP2PMaxCandidates);
}

TEST_CASE("P2PC2 Binding 请求", "[p2pice]")
{
    const std::string r = P2PStunClient::BuildRequest(kRfcTxid);
    REQUIRE(r.size() == P2PStunClient::kRequestBytes);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(r.data());
    //类型 0x0001，长度 12（头之后的字节数），魔数
    CHECK(b[0] == 0x00);
    CHECK(b[1] == 0x01);
    CHECK(b[2] == 0x00);
    CHECK(b[3] == 12);
    CHECK(b[4] == 0x21);
    CHECK(b[5] == 0x12);
    CHECK(b[6] == 0xA4);
    CHECK(b[7] == 0x42);
    CHECK(memcmp(b + 8, kRfcTxid, kP2PStunTxidBytes) == 0);
    //SOFTWARE 属性：类型 0x8022、值 8 字节
    CHECK(b[20] == 0x80);
    CHECK(b[21] == 0x22);
    CHECK(b[22] == 0x00);
    CHECK(b[23] == 8);
}

TEST_CASE("P2PC3 Binding 应答解析", "[p2pice]")
{
    const std::string rfcAttr(reinterpret_cast<const char*>(kRfcXorMapped), sizeof(kRfcXorMapped));
    uint32_t ip = 0;
    uint16_t port = 0;

    //服务端的 32 字节应答：头 + XOR-MAPPED-ADDRESS
    REQUIRE(Parse(MakeResponse(kRfcTxid, rfcAttr), kRfcTxid, ip, port));
    CHECK(ip == kRfcIp);
    CHECK(port == kRfcPort);

    //标准服务器的应答：前后还有别的属性（SOFTWARE 值 11 字节补齐到 12、未知属性）
    std::string software;
    Append16(software, 0x8022);
    Append16(software, 11);
    software += "test vector";
    software.push_back(0x20);
    std::string unknown;
    Append16(unknown, 0x8028);
    Append16(unknown, 4);
    Append32(unknown, 0xDEADBEEF);
    ip = 0;
    REQUIRE(Parse(MakeResponse(kRfcTxid, software + rfcAttr + unknown), kRfcTxid, ip, port));
    CHECK(ip == kRfcIp);
    CHECK(port == kRfcPort);

    //不核对事务号时取出报文里的事务号
    const std::string ok = MakeResponse(kRfcTxid, rfcAttr);
    uint8_t got[kP2PStunTxidBytes] = { 0 };
    REQUIRE(P2PStunClient::ParseResponse(reinterpret_cast<const uint8_t*>(ok.data()), ok.size(), NULL, ip, port, got));
    CHECK(memcmp(got, kRfcTxid, kP2PStunTxidBytes) == 0);

    //各种失败
    uint8_t otherTxid[kP2PStunTxidBytes];
    memcpy(otherTxid, kRfcTxid, kP2PStunTxidBytes);
    otherTxid[0] ^= 0xFF;
    CHECK_FALSE(Parse(ok, otherTxid, ip, port));
    CHECK_FALSE(Parse(MakeResponse(kRfcTxid, rfcAttr, 0x0111), kRfcTxid, ip, port));
    CHECK_FALSE(Parse(MakeResponse(kRfcTxid, rfcAttr, 0x0101, 0x12345678), kRfcTxid, ip, port));
    CHECK_FALSE(Parse(ok.substr(0, ok.size() - 4), kRfcTxid, ip, port));
    std::string badLen = ok;
    badLen[3] = 16;
    CHECK_FALSE(Parse(badLen, kRfcTxid, ip, port));
    //属性长度越界
    std::string overflow;
    Append16(overflow, 0x0020);
    Append16(overflow, 64);
    Append32(overflow, 0);
    CHECK_FALSE(Parse(MakeResponse(kRfcTxid, overflow), kRfcTxid, ip, port));
    //只有 IPv6 的映射地址（地址族 0x02，值 20 字节）
    std::string v6;
    Append16(v6, 0x0020);
    Append16(v6, 20);
    v6.push_back(0);
    v6.push_back(0x02);
    v6.append(18, '\x11');
    CHECK_FALSE(Parse(MakeResponse(kRfcTxid, v6), kRfcTxid, ip, port));
    //没有任何属性
    CHECK_FALSE(Parse(MakeResponse(kRfcTxid, std::string()), kRfcTxid, ip, port));
    CHECK_FALSE(P2PStunClient::ParseResponse(NULL, 0, kRfcTxid, ip, port));
}

TEST_CASE("P2PC4 地址探测流程", "[p2pice]")
{
    REQUIRE(sodium_init() >= 0);

    SECTION("两个服务器看到同一端口：映射与目的地址无关")
    {
        FakeSender sender;
        P2PAddressProber prober(sender);
        prober.Start(TwoServers(), 0);
        CHECK(sender.CountTo(kProbePort1) == 1);
        CHECK(sender.CountTo(kProbePort2) == 1);
        CHECK(sender.LastTo(kProbePort1).size() == P2PStunClient::kRequestBytes);
        CHECK(sender.LastTo(kProbePort1).substr(8, kP2PStunTxidBytes) != sender.LastTo(kProbePort2).substr(8, kP2PStunTxidBytes));
        CHECK_FALSE(prober.IsDone());
        CHECK(prober.NextTimerMs() == kP2PProbeRetransmitMs);

        CHECK(Answer(prober, sender, kProbePort1, kIp1_2_3_4, kMappedPort, 10));
        //重复的应答忽略
        CHECK_FALSE(Answer(prober, sender, kProbePort1, kIp1_2_3_4, kMappedPort, 11));
        //到了重发时刻：只重发没有应答的那个服务器，事务号不变
        const std::string first2 = sender.LastTo(kProbePort2);
        prober.OnTimer(kP2PProbeRetransmitMs);
        CHECK(sender.CountTo(kProbePort1) == 1);
        CHECK(sender.CountTo(kProbePort2) == 2);
        CHECK(sender.LastTo(kProbePort2) == first2);
        CHECK(Answer(prober, sender, kProbePort2, kIp1_2_3_4, kMappedPort, kP2PProbeRetransmitMs + 5));
        CHECK(prober.IsDone());
        CHECK(prober.NatMapping() == p2p_nat_mapping_independent);
        CHECK(prober.AnsweredCount() == 2);
        const std::vector<P2PCandidate> srflx = prober.SrflxCandidates(kP2PLocalPrefPhysical);
        REQUIRE(srflx.size() == 1);
        CHECK(srflx[0].m_type == p2p_candidate_srflx);
        CHECK(srflx[0].m_ip == kIp1_2_3_4);
        CHECK(srflx[0].m_port == kMappedPort);
        CHECK(srflx[0].m_priority == CandidatePriority(kP2PTypePrefSrflx, kP2PLocalPrefPhysical));
    }

    SECTION("两个服务器看到的端口不同：对称型 NAT")
    {
        FakeSender sender;
        P2PAddressProber prober(sender);
        prober.Start(TwoServers(), 0);
        CHECK(Answer(prober, sender, kProbePort1, kIp1_2_3_4, kMappedPort, 1));
        CHECK(Answer(prober, sender, kProbePort2, kIp1_2_3_4, kMappedPort2, 2));
        CHECK(prober.IsDone());
        CHECK(prober.NatMapping() == p2p_nat_mapping_dependent);
        CHECK(prober.SrflxCandidates(kP2PLocalPrefPhysical).size() == 2);
    }

    SECTION("没有应答：按间隔重发，发满次数后放弃")
    {
        FakeSender sender;
        P2PAddressProber prober(sender);
        prober.Start(TwoServers(), 0);
        int64_t now = 0;
        for (int i = 1; i < kP2PProbeMaxAttempts; ++i)
        {
            now += kP2PProbeRetransmitMs;
            //还没到时刻：不发
            prober.OnTimer(now - 1);
            CHECK(sender.CountTo(kProbePort1) == (size_t)i);
            prober.OnTimer(now);
            CHECK(sender.CountTo(kProbePort1) == (size_t)(i + 1));
        }
        CHECK_FALSE(prober.IsDone());
        now += kP2PProbeRetransmitMs;
        prober.OnTimer(now);
        CHECK(prober.IsDone());
        CHECK(sender.CountTo(kProbePort1) == (size_t)kP2PProbeMaxAttempts);
        CHECK(sender.CountTo(kProbePort2) == (size_t)kP2PProbeMaxAttempts);
        CHECK(prober.SrflxCandidates(kP2PLocalPrefPhysical).empty());
        CHECK(prober.NatMapping() == p2p_nat_mapping_unknown);
        //放弃之后迟到的应答忽略
        CHECK_FALSE(Answer(prober, sender, kProbePort1, kIp1_2_3_4, kMappedPort, now + 1));
    }

    SECTION("只有一个服务器应答")
    {
        FakeSender sender;
        P2PAddressProber prober(sender);
        prober.Start(TwoServers(), 0);
        CHECK(Answer(prober, sender, kProbePort1, kIp1_2_3_4, kMappedPort, 1));
        for (int64_t now = kP2PProbeRetransmitMs; !prober.IsDone(); now += kP2PProbeRetransmitMs)
            prober.OnTimer(now);
        CHECK(prober.AnsweredCount() == 1);
        CHECK(prober.NatMapping() == p2p_nat_mapping_unknown);
        CHECK(prober.SrflxCandidates(kP2PLocalPrefPhysical).size() == 1);
    }

    SECTION("事务号不认识的应答忽略；服务器列表为空时立即完成")
    {
        FakeSender sender;
        P2PAddressProber prober(sender);
        prober.Start(TwoServers(), 0);
        const std::string stray = MakeResponse(kRfcTxid, XorMapped(kIp1_2_3_4, kMappedPort));
        CHECK_FALSE(prober.OnStunDatagram(reinterpret_cast<const uint8_t*>(stray.data()), stray.size(), 1));
        CHECK(prober.AnsweredCount() == 0);

        FakeSender idleSender;
        P2PAddressProber idle(idleSender);
        idle.Start(std::vector<P2PProbeServer>(), 0);
        CHECK(idle.IsDone());
        CHECK(idleSender.m_sent.empty());
    }
}

TEST_CASE("P2PC5 候选地址的 JSON 编解码与 IP 文本", "[p2pice]")
{
    //IP 文本
    CHECK(IpToString(kIp192_168_1_10) == "192.168.1.10");
    uint32_t ip = 0;
    CHECK(IpFromString("10.0.0.5", ip));
    CHECK(ip == kIp10_0_0_5);
    CHECK_FALSE(IpFromString("1.2.3", ip));
    CHECK_FALSE(IpFromString("1.2.3.4.5", ip));
    CHECK_FALSE(IpFromString("256.1.1.1", ip));
    CHECK_FALSE(IpFromString("a.b.c.d", ip));
    CHECK_FALSE(IpFromString("", ip));
    CHECK_FALSE(IpFromString("1..2.3", ip));

    //往返：本机地址的前缀长度不发给对方
    std::vector<P2PCandidate> c(2);
    c[0].m_type = p2p_candidate_host;
    c[0].m_ip = kIp192_168_1_10;
    c[0].m_port = kLocalPort;
    c[0].m_priority = CandidatePriority(kP2PTypePrefHost, kP2PLocalPrefPhysical);
    c[0].m_prefixLen = kPrefix24;
    c[1].m_type = p2p_candidate_srflx;
    c[1].m_ip = kIp1_2_3_4;
    c[1].m_port = kMappedPort;
    c[1].m_priority = CandidatePriority(kP2PTypePrefSrflx, kP2PLocalPrefPhysical);
    Json::Value arr;
    CandidatesToJson(c, arr);
    REQUIRE(arr.isArray());
    REQUIRE(arr.size() == 2);
    CHECK(arr[0]["ip"].asString() == "192.168.1.10");
    CHECK(arr[0]["type"].asInt() == p2p_candidate_host);
    CHECK_FALSE(arr[0].isMember("prefixlen"));
    std::vector<P2PCandidate> back;
    REQUIRE(CandidatesFromJson(arr, back) == 2);
    CHECK(back[0].m_ip == kIp192_168_1_10);
    CHECK(back[0].m_port == kLocalPort);
    CHECK(back[0].m_priority == c[0].m_priority);
    CHECK(back[0].m_prefixLen == 0);
    CHECK(back[1].m_type == p2p_candidate_srflx);

    //不合法的条目被跳过
    Json::Value bad(Json::arrayValue);
    Json::Value e;
    e["type"] = 3;
    e["ip"] = "1.2.3.4";
    e["port"] = 1000;
    e["priority"] = 1;
    bad.append(e);
    e["type"] = 1;
    e["ip"] = "300.1.1.1";
    bad.append(e);
    e["ip"] = "1.2.3.4";
    e["port"] = 0;
    bad.append(e);
    e["port"] = 70000;
    bad.append(e);
    e["port"] = "1000";
    bad.append(e);
    Json::Value good;
    good["type"] = 2;
    good["ip"] = "1.2.3.4";
    good["port"] = 1000;
    good["priority"] = 7;
    bad.append(good);
    CHECK(CandidatesFromJson(bad, back) == 1);
    CHECK(back[0].m_port == 1000);
    CHECK(CandidatesFromJson(Json::Value("not an array"), back) == 0);
    CHECK(back.empty());

    //最多读 kP2PMaxCandidates 个
    Json::Value many(Json::arrayValue);
    for (size_t i = 0; i < kP2PMaxCandidates + 4; ++i)
        many.append(good);
    CHECK(CandidatesFromJson(many, back) == kP2PMaxCandidates);
}
