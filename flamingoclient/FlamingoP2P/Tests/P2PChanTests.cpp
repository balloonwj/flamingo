/**
 *  P2P 通道层的单元测试（Suite P2PCH，标签 [p2pchan]），P2PChanTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 4 期 4a（7.3 节）：
 *    - P2PCH1 分片：空消息一片；恰好一片的边界；多一个字节变两片；分片头带消息号、片号、片数；1024 片可以、1025 片
 *             失败；每片负载上限为 0 或数据为空指针时失败；
 *    - P2PCH2 重组：乱序收齐后交出原消息；重复的片忽略；单片消息直接交出；片数与已记录的不一致时整条丢弃；
 *             片号不小于片数、片数为 0、长度不足的片忽略；
 *    - P2PCH3 重组的超时与表满：1 秒内没收齐的消息在下一片到达时被丢弃；同时有 17 条未收齐时最旧的一条被挤出；
 *    - P2PCH4 复用层：两条数据报通道互不干扰；没打开的通道、类型与本方约定不一致的报文丢弃；类型不认识的通道
 *             打不开；UDP 路径上 100 KB 的消息切成多个报文且每个报文明文不超过单报文上限，中转路径上只要两个；
 *             超过上限返回 p2p_send_too_large；累计字节数只算业务数据。
 *  可靠通道（第 4b 步，两端的复用层接在内存回路上，每毫秒推进一次，报文延迟 1 毫秒）：
 *    - P2PCH5 UDP 路径经 KCP：1 MiB 按序原样送达；回路上按 10% 确定性丢包时同样完整、按序送达，且出现过重传；
 *             传完之后两端都没有定时工作；
 *    - P2PCH6 发送队列：连续发送超过 4 MiB 时返回 p2p_send_would_block（这一次一个字节也没收下）；推进回路让对端
 *             确认之后收到一次可写通知，之后可以继续发送；
 *    - P2PCH7 中转路径：不经 KCP，直接切块发出，每块明文不超过 64 KiB，收到即交出；发送路径积压超过上限时返回
 *             p2p_send_would_block，积压降下来后 CheckWritable 产生一次可写通知。
 *  对方先发、本方后打开通道（第 6 期）：
 *    - P2PCH8 中转路径与 UDP 路径上，本方打开通道之前收到的数据都暂存，打开时不产生事件、NextTimerMs 变为 0，下一次
 *             OnTimer 按原顺序补交，之后的数据照常接在后面；类型与本方约定不一致的暂存报文补交时丢弃；暂存总量不超过
 *             kP2PEarlyDataLimitBytes，超出的报文丢弃。
 *
 *  典型用法：FlamingoUnit.exe "[p2pchan]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

#include "../Chan/P2PChannelMux.h"
#include "../Chan/P2PDatagramFrag.h"
#include "../Core/P2PPacket.h"

using namespace p2p;

namespace {

//分片用例的每片负载上限（字节）：取小值让用例好算
const size_t kSmallFrag = 10;

//重组用例的消息号
const uint32_t kMsgId = 0x01020304;

//P2PCH4 的通道号与大消息长度
const uint16_t kChanA = 1;
const uint16_t kChanB = 2;
const uint16_t kChanUnopened = 9;
const size_t kBigMessage = 100 * 1024;

//模拟时间（毫秒）
const int64_t kT0 = 1000;

//生成可辨认数据时的步长与字节掩码
const size_t kPatternStep = 7;
const size_t kByteMask = 0xFF;

//一段内容可辨认的数据
std::string Pattern(size_t n, int salt)
{
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i)
        s[i] = (char)((i * kPatternStep + (size_t)salt) & kByteMask);
    return s;
}

//把一片交给重组器
bool AddFrag(P2PDatagramReassembler& r, const std::string& frag, int64_t now, std::string& msg)
{
    return r.Add(reinterpret_cast<const uint8_t*>(frag.data()), frag.size(), now, msg);
}

//切片，失败时判本用例失败
std::vector<std::string> SplitOk(uint32_t msgId, const std::string& data, size_t maxFrag)
{
    std::vector<std::string> frags;
    REQUIRE(P2PDatagramFragmenter::Split(msgId, reinterpret_cast<const uint8_t*>(data.data()), data.size(), maxFrag, frags));
    return frags;
}

//记录复用层发出的报文
class RecordingOutput : public IP2PMuxOutput
{
public:
    RecordingOutput()
        : m_queued(0)
    {
    }

    virtual bool SendDataPacket(const std::string& plain, int64_t nowMs) override
    {
        (void)nowMs;
        m_packets.push_back(plain);
        return true;
    }

    virtual size_t QueuedBytes() const override
    {
        return m_queued;
    }

    std::vector<std::string> m_packets;   //发出的报文明文
    size_t                   m_queued;    //模拟发送路径上的积压（字节）
};

//记录收到的数据
class RecordingSink : public IP2PChannelSink
{
public:
    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        m_channels.push_back(channelId);
        m_data.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
    }

    std::vector<uint16_t>    m_channels;   //每块数据的通道号
    std::vector<std::string> m_data;       //收到的数据
};

//把复用层的事件交给接收端（会话在释放互斥量之后做的事）
void Deliver(std::vector<P2PChannelEvent>& events)
{
    for (size_t i = 0; i < events.size(); ++i)
    {
        if (events[i].m_type == p2p_channel_event_data && events[i].m_sink != NULL)
            events[i].m_sink->OnData(events[i].m_channelId, reinterpret_cast<const uint8_t*>(events[i].m_data.data()),
                                     (uint32_t)events[i].m_data.size());
    }
    events.clear();
}

//可靠通道的接收端：拼接收到的字节流，记录可写通知
class StreamSink : public IP2PChannelSink
{
public:
    StreamSink()
        : m_writable(0)
    {
    }

    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        (void)channelId;
        m_stream.append(reinterpret_cast<const char*>(data), len);
    }

    virtual void OnWritable(uint16_t channelId) override
    {
        (void)channelId;
        ++m_writable;
    }

    std::string m_stream;     //收到的字节流
    int         m_writable;   //可写通知的次数
};

//把复用层的事件交给接收端（含可写通知）
void DeliverAll(std::vector<P2PChannelEvent>& events)
{
    for (size_t i = 0; i < events.size(); ++i)
    {
        if (events[i].m_sink == NULL)
            continue;
        if (events[i].m_type == p2p_channel_event_data)
            events[i].m_sink->OnData(events[i].m_channelId, reinterpret_cast<const uint8_t*>(events[i].m_data.data()),
                                     (uint32_t)events[i].m_data.size());
        else if (events[i].m_type == p2p_channel_event_writable)
            events[i].m_sink->OnWritable(events[i].m_channelId);
    }
    events.clear();
}

//可靠通道用例的回路参数：传输量、最多推进的步数（毫秒）、丢包率（千分比）
const size_t kReliableBytes = 1024 * 1024;
const int kMaxSteps = 20000;
const uint32_t kLossPermille = 100;
const uint32_t kPermille = 1000;

//确定性伪随机数（线性同余）的参数
const uint32_t kLcgMul = 1103515245u;
const uint32_t kLcgAdd = 12345u;
const uint32_t kLcgShift = 16;
const uint32_t kLcgSeed = 20261004u;

/**
 *  两端复用层接在内存回路上：每一步把上一步发出的报文交给对端（可按千分比确定性丢弃），再驱动到时刻的定时。
 */
struct MuxLoop
{
    RecordingOutput m_outA;      //A 端发出的报文
    RecordingOutput m_outB;      //B 端发出的报文
    P2PChannelMux   m_a;         //A 端
    P2PChannelMux   m_b;         //B 端
    int64_t         m_now;       //模拟时间（毫秒）
    uint32_t        m_lossPermille;   //丢包率（千分比）
    uint32_t        m_lcg;       //伪随机数状态
    uint64_t        m_dropped;   //丢弃的报文个数

    explicit MuxLoop(int path)
        : m_a(m_outA)
        , m_b(m_outB)
        , m_now(kT0)
        , m_lossPermille(0)
        , m_lcg(kLcgSeed)
        , m_dropped(0)
    {
        m_a.SetPath(path);
        m_b.SetPath(path);
    }

    //按丢包率决定是否丢弃
    bool Drop()
    {
        m_lcg = m_lcg * kLcgMul + kLcgAdd;
        return ((m_lcg >> kLcgShift) % kPermille) < m_lossPermille;
    }

    //把一端已发出的报文交给另一端
    void Transfer(RecordingOutput& from, P2PChannelMux& to)
    {
        std::vector<std::string> packets;
        packets.swap(from.m_packets);
        std::vector<P2PChannelEvent> events;
        for (size_t i = 0; i < packets.size(); ++i)
        {
            if (Drop())
            {
                ++m_dropped;
                continue;
            }
            to.OnPacket(packets[i], m_now, events);
        }
        DeliverAll(events);
    }

    //推进一毫秒
    void Step()
    {
        ++m_now;
        Transfer(m_outA, m_b);
        Transfer(m_outB, m_a);
        std::vector<P2PChannelEvent> events;
        if (m_a.NextTimerMs() <= m_now)
            m_a.OnTimer(m_now, events);
        if (m_b.NextTimerMs() <= m_now)
            m_b.OnTimer(m_now, events);
        DeliverAll(events);
    }
};

//把一端发出的报文全部交给另一端
void Pump(RecordingOutput& from, P2PChannelMux& to, int64_t now)
{
    std::vector<P2PChannelEvent> events;
    for (size_t i = 0; i < from.m_packets.size(); ++i)
        to.OnPacket(from.m_packets[i], now, events);
    from.m_packets.clear();
    Deliver(events);
}

} // namespace

TEST_CASE("P2PCH1 数据报分片", "[p2pchan]")
{
    //空消息：一片，只有分片头
    std::vector<std::string> frags = SplitOk(kMsgId, std::string(), kSmallFrag);
    REQUIRE(frags.size() == 1);
    CHECK(frags[0].size() == kP2PDatagramFragHeaderBytes);

    //恰好一片与多一个字节
    CHECK(SplitOk(kMsgId, Pattern(kSmallFrag, 1), kSmallFrag).size() == 1);
    frags = SplitOk(kMsgId, Pattern(kSmallFrag + 1, 1), kSmallFrag);
    REQUIRE(frags.size() == 2);
    CHECK(frags[1].size() == kP2PDatagramFragHeaderBytes + 1);
    //分片头：消息号、片号、片数，网络字节序
    const uint8_t* h = reinterpret_cast<const uint8_t*>(frags[1].data());
    CHECK(h[0] == 0x01);
    CHECK(h[1] == 0x02);
    CHECK(h[2] == 0x03);
    CHECK(h[3] == 0x04);
    CHECK(h[4] == 0);
    CHECK(h[5] == 1);
    CHECK(h[6] == 0);
    CHECK(h[7] == 2);

    //片数上限
    CHECK(SplitOk(kMsgId, Pattern(kSmallFrag * kP2PMaxDatagramFragments, 2), kSmallFrag).size()
          == kP2PMaxDatagramFragments);
    std::vector<std::string> out;
    const std::string tooMany = Pattern(kSmallFrag * kP2PMaxDatagramFragments + 1, 3);
    CHECK_FALSE(P2PDatagramFragmenter::Split(kMsgId, reinterpret_cast<const uint8_t*>(tooMany.data()), tooMany.size(),
                                             kSmallFrag, out));
    CHECK(out.empty());
    CHECK(P2PDatagramFragmenter::MaxMessageBytes(kSmallFrag) == kSmallFrag * kP2PMaxDatagramFragments);

    //参数非法
    CHECK_FALSE(P2PDatagramFragmenter::Split(kMsgId, reinterpret_cast<const uint8_t*>("x"), 1, 0, out));
    CHECK_FALSE(P2PDatagramFragmenter::Split(kMsgId, NULL, 1, kSmallFrag, out));
}

TEST_CASE("P2PCH2 数据报重组", "[p2pchan]")
{
    const std::string msg = Pattern(kSmallFrag * 3 + 4, 5);
    std::vector<std::string> frags = SplitOk(kMsgId, msg, kSmallFrag);
    REQUIRE(frags.size() == 4);

    //乱序收齐，重复片忽略
    P2PDatagramReassembler r;
    std::string got;
    CHECK_FALSE(AddFrag(r, frags[2], kT0, got));
    CHECK_FALSE(AddFrag(r, frags[0], kT0, got));
    CHECK_FALSE(AddFrag(r, frags[0], kT0, got));
    CHECK_FALSE(AddFrag(r, frags[3], kT0, got));
    CHECK(r.PendingCount() == 1);
    REQUIRE(AddFrag(r, frags[1], kT0, got));
    CHECK(got == msg);
    CHECK(r.PendingCount() == 0);
    //整条交出之后迟到的重复片：当作一条新消息开始重组，不会再次交出
    CHECK_FALSE(AddFrag(r, frags[1], kT0, got));

    //单片消息直接交出
    P2PDatagramReassembler single;
    const std::string oneFrag = Pattern(kSmallFrag, 6);
    REQUIRE(AddFrag(single, SplitOk(kMsgId + 1, oneFrag, kSmallFrag)[0], kT0, got));
    CHECK(got == oneFrag);
    CHECK(single.PendingCount() == 0);

    //片数与已记录的不一致：整条丢弃
    P2PDatagramReassembler mismatch;
    std::vector<std::string> other = SplitOk(kMsgId, Pattern(kSmallFrag * 2, 7), kSmallFrag);
    CHECK_FALSE(AddFrag(mismatch, frags[0], kT0, got));
    CHECK_FALSE(AddFrag(mismatch, other[1], kT0, got));
    CHECK(mismatch.PendingCount() == 0);
    CHECK(mismatch.DroppedCount() == 1);

    //非法的分片头：片号不小于片数、片数为 0、长度不足
    P2PDatagramReassembler bad;
    std::string f = frags[0];
    f[5] = 9;
    CHECK_FALSE(AddFrag(bad, f, kT0, got));
    f = frags[0];
    f[6] = 0;
    f[7] = 0;
    CHECK_FALSE(AddFrag(bad, f, kT0, got));
    CHECK_FALSE(AddFrag(bad, frags[0].substr(0, kP2PDatagramFragHeaderBytes - 1), kT0, got));
    CHECK(bad.PendingCount() == 0);
}

TEST_CASE("P2PCH3 重组的超时与表满", "[p2pchan]")
{
    std::string got;
    //超时：第一片之后 1 秒内没收齐，下一片到达时整条已被丢弃，只能重新开始
    P2PDatagramReassembler r;
    std::vector<std::string> frags = SplitOk(kMsgId, Pattern(kSmallFrag * 2, 8), kSmallFrag);
    CHECK_FALSE(AddFrag(r, frags[0], kT0, got));
    CHECK_FALSE(AddFrag(r, frags[1], kT0 + kP2PDatagramReassemblyMs, got));
    CHECK(r.DroppedCount() == 1);
    CHECK(r.PendingCount() == 1);

    //表满：17 条各收一片，最早的一条被挤出
    P2PDatagramReassembler full;
    for (uint32_t i = 0; i <= kP2PMaxPendingDatagrams; ++i)
    {
        std::vector<std::string> f = SplitOk(i, Pattern(kSmallFrag * 2, (int)i), kSmallFrag);
        CHECK_FALSE(AddFrag(full, f[0], kT0 + i, got));
    }
    CHECK(full.PendingCount() == kP2PMaxPendingDatagrams);
    CHECK(full.DroppedCount() == 1);
    //没被挤出的第 1 条补上第二片即收齐
    std::vector<std::string> f1 = SplitOk(1, Pattern(kSmallFrag * 2, 1), kSmallFrag);
    REQUIRE(AddFrag(full, f1[1], kT0 + kP2PMaxPendingDatagrams + 1, got));
    CHECK(got == Pattern(kSmallFrag * 2, 1));
    //被挤出的是第 0 条：补上它的第二片也收不齐（只会作为一条新消息开始重组）
    std::vector<std::string> f0 = SplitOk(0, Pattern(kSmallFrag * 2, 0), kSmallFrag);
    CHECK_FALSE(AddFrag(full, f0[1], kT0 + kP2PMaxPendingDatagrams + 1, got));
    CHECK(full.PendingCount() == kP2PMaxPendingDatagrams);
}

TEST_CASE("P2PCH4 通道复用层：数据报通道", "[p2pchan]")
{
    RecordingOutput outA;
    RecordingOutput outB;
    P2PChannelMux a(outA);
    P2PChannelMux b(outB);
    a.SetPath(p2p_link_path_lan);
    b.SetPath(p2p_link_path_lan);
    RecordingSink sinkA;
    RecordingSink sinkB;
    REQUIRE(a.Open(kChanA, p2p_channel_datagram, &sinkA) == p2p_send_ok);
    REQUIRE(a.Open(kChanB, p2p_channel_datagram, &sinkA) == p2p_send_ok);
    REQUIRE(b.Open(kChanA, p2p_channel_datagram, &sinkB) == p2p_send_ok);
    REQUIRE(b.Open(kChanB, p2p_channel_datagram, &sinkB) == p2p_send_ok);
    //重复打开、空接收端
    CHECK(a.Open(kChanA, p2p_channel_datagram, &sinkA) == p2p_send_bad_channel);
    CHECK(a.Open(kChanUnopened, p2p_channel_datagram, NULL) == p2p_send_bad_channel);

    //两条通道互不干扰
    const std::string m1 = Pattern(300, 1);
    const std::string m2 = Pattern(200, 2);
    REQUIRE(a.Send(kChanA, reinterpret_cast<const uint8_t*>(m1.data()), (uint32_t)m1.size(), kT0) == p2p_send_ok);
    REQUIRE(a.Send(kChanB, reinterpret_cast<const uint8_t*>(m2.data()), (uint32_t)m2.size(), kT0) == p2p_send_ok);
    Pump(outA, b, kT0);
    REQUIRE(sinkB.m_data.size() == 2);
    CHECK(sinkB.m_channels[0] == kChanA);
    CHECK(sinkB.m_data[0] == m1);
    CHECK(sinkB.m_channels[1] == kChanB);
    CHECK(sinkB.m_data[1] == m2);
    CHECK(a.Send(kChanUnopened, reinterpret_cast<const uint8_t*>("x"), 1, kT0) == p2p_send_bad_channel);

    //UDP 路径上的大消息：切成多个报文，每个报文明文不超过单报文上限
    const std::string big = Pattern(kBigMessage, 3);
    REQUIRE(a.Send(kChanA, reinterpret_cast<const uint8_t*>(big.data()), (uint32_t)big.size(), kT0) == p2p_send_ok);
    REQUIRE(outA.m_packets.size() > 1);
    for (size_t i = 0; i < outA.m_packets.size(); ++i)
        CHECK(outA.m_packets[i].size() <= kP2PMaxPlainBytes);
    Pump(outA, b, kT0);
    REQUIRE(sinkB.m_data.size() == 3);
    CHECK(sinkB.m_data[2] == big);
    CHECK(a.BytesSent() == m1.size() + m2.size() + big.size());
    CHECK(b.BytesReceived() == a.BytesSent());

    //没打开的通道、类型不一致：对端丢弃
    RecordingOutput outC;
    P2PChannelMux c(outC);
    c.SetPath(p2p_link_path_lan);
    RecordingSink sinkC;
    REQUIRE(c.Open(kChanUnopened, p2p_channel_datagram, &sinkC) == p2p_send_ok);
    REQUIRE(c.Open(kChanA, p2p_channel_datagram, &sinkC) == p2p_send_ok);
    REQUIRE(c.Send(kChanUnopened, reinterpret_cast<const uint8_t*>("y"), 1, kT0) == p2p_send_ok);
    REQUIRE(c.Send(kChanA, reinterpret_cast<const uint8_t*>("w"), 1, kT0) == p2p_send_ok);
    REQUIRE(outC.m_packets.size() == 2);
    //第二个报文的通道类型改成可靠：b 那边通道 kChanA 是数据报通道
    outC.m_packets[1][2] = (char)p2p_channel_reliable;
    std::vector<P2PChannelEvent> events;
    for (size_t i = 0; i < outC.m_packets.size(); ++i)
        b.OnPacket(outC.m_packets[i], kT0, events);
    CHECK(events.empty());
    CHECK(sinkB.m_data.size() == 3);

    //类型不认识的通道打不开
    const int kUnknownKind = 9;
    CHECK(a.Open(kChanUnopened, kUnknownKind, &sinkA) == p2p_send_bad_channel);

    //超过上限
    CHECK(a.MaxDatagramMessageBytes()
          == (kP2PMaxPlainBytes - kP2PChannelHeaderBytes - kP2PDatagramFragHeaderBytes) * kP2PMaxDatagramFragments);
    const std::string tooLarge(a.MaxDatagramMessageBytes() + 1, 'z');
    CHECK(a.Send(kChanA, reinterpret_cast<const uint8_t*>(tooLarge.data()), (uint32_t)tooLarge.size(), kT0)
          == p2p_send_too_large);
    CHECK(outA.m_packets.empty());

    //中转路径：单个报文可以到 64 KiB，100 KB 的消息只要两个报文
    RecordingOutput outR;
    P2PChannelMux relay(outR);
    relay.SetPath(p2p_link_path_relay);
    REQUIRE(relay.Open(kChanA, p2p_channel_datagram, &sinkA) == p2p_send_ok);
    REQUIRE(relay.Send(kChanA, reinterpret_cast<const uint8_t*>(big.data()), (uint32_t)big.size(), kT0) == p2p_send_ok);
    CHECK(outR.m_packets.size() == 2);
    for (size_t i = 0; i < outR.m_packets.size(); ++i)
        CHECK(outR.m_packets[i].size() <= kP2PRelayMaxPlainBytes);
}

TEST_CASE("P2PCH5 可靠通道经 KCP：按序原样送达，丢包时同样完整", "[p2pchan]")
{
    const uint32_t lossCases[] = { 0, kLossPermille };
    for (size_t c = 0; c < sizeof(lossCases) / sizeof(lossCases[0]); ++c)
    {
        INFO("loss permille " << lossCases[c]);
        MuxLoop loop(p2p_link_path_lan);
        loop.m_lossPermille = lossCases[c];
        StreamSink sinkA;
        StreamSink sinkB;
        REQUIRE(loop.m_a.Open(kChanA, p2p_channel_reliable, &sinkA) == p2p_send_ok);
        REQUIRE(loop.m_b.Open(kChanA, p2p_channel_reliable, &sinkB) == p2p_send_ok);
        CHECK(loop.m_a.IsReliable(kChanA));

        const std::string data = Pattern(kReliableBytes, (int)c);
        REQUIRE(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(data.data()), (uint32_t)data.size(), loop.m_now)
                == p2p_send_ok);
        for (int i = 0; i < kMaxSteps && sinkB.m_stream.size() < data.size(); ++i)
            loop.Step();
        REQUIRE(sinkB.m_stream.size() == data.size());
        CHECK(sinkB.m_stream == data);
        CHECK(loop.m_b.BytesReceived() == data.size());
        if (lossCases[c] > 0)
        {
            CHECK(loop.m_dropped > 0);
            CHECK(loop.m_a.RetransmittedSegments() > 0);
        }
        //等确认全部回来之后，两端都没有定时工作
        for (int i = 0; i < kMaxSteps && (loop.m_a.NextTimerMs() != INT64_MAX || loop.m_b.NextTimerMs() != INT64_MAX); ++i)
            loop.Step();
        CHECK(loop.m_a.NextTimerMs() == INT64_MAX);
        CHECK(loop.m_b.NextTimerMs() == INT64_MAX);
        CHECK(loop.m_a.QueuedBytes() == 0);
    }
}

TEST_CASE("P2PCH6 可靠通道的发送队列上限与可写通知", "[p2pchan]")
{
    MuxLoop loop(p2p_link_path_lan);
    StreamSink sinkA;
    StreamSink sinkB;
    REQUIRE(loop.m_a.Open(kChanA, p2p_channel_reliable, &sinkA) == p2p_send_ok);
    REQUIRE(loop.m_b.Open(kChanA, p2p_channel_reliable, &sinkB) == p2p_send_ok);

    //不推进回路，一直发到被拒绝
    const std::string chunk = Pattern(kP2PKcpSendChunkBytes, 1);
    size_t accepted = 0;
    int result = p2p_send_ok;
    while (result == p2p_send_ok)
    {
        result = loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(chunk.data()), (uint32_t)chunk.size(), loop.m_now);
        if (result == p2p_send_ok)
            accepted += chunk.size();
    }
    CHECK(result == p2p_send_would_block);
    CHECK(accepted >= kP2PReliableQueueLimitBytes);
    CHECK(loop.m_a.QueuedBytes() >= kP2PReliableQueueLimitBytes);
    CHECK(sinkA.m_writable == 0);

    //推进回路：对端确认之后，A 收到一次可写通知，且全部数据按序送达（确认比数据晚一步到达 A，两件事都等到）
    for (int i = 0; i < kMaxSteps && (sinkB.m_stream.size() < accepted || sinkA.m_writable == 0); ++i)
        loop.Step();
    CHECK(sinkA.m_writable == 1);
    REQUIRE(sinkB.m_stream.size() == accepted);
    CHECK(sinkB.m_stream.substr(0, chunk.size()) == chunk);
    CHECK(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(chunk.data()), (uint32_t)chunk.size(), loop.m_now)
          == p2p_send_ok);
}

TEST_CASE("P2PCH7 可靠通道在中转路径上直接透传", "[p2pchan]")
{
    MuxLoop loop(p2p_link_path_relay);
    StreamSink sinkA;
    StreamSink sinkB;
    REQUIRE(loop.m_a.Open(kChanA, p2p_channel_reliable, &sinkA) == p2p_send_ok);
    REQUIRE(loop.m_b.Open(kChanA, p2p_channel_reliable, &sinkB) == p2p_send_ok);

    //不经 KCP：直接切块，每块明文不超过 64 KiB，没有定时工作
    const std::string data = Pattern(kBigMessage * 3, 2);
    REQUIRE(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(data.data()), (uint32_t)data.size(), loop.m_now)
            == p2p_send_ok);
    REQUIRE(loop.m_outA.m_packets.size() == (data.size() + kP2PRelayMaxPlainBytes - kP2PChannelHeaderBytes - 1)
                                            / (kP2PRelayMaxPlainBytes - kP2PChannelHeaderBytes));
    for (size_t i = 0; i < loop.m_outA.m_packets.size(); ++i)
        CHECK(loop.m_outA.m_packets[i].size() <= kP2PRelayMaxPlainBytes);
    CHECK(loop.m_a.NextTimerMs() == INT64_MAX);
    loop.Step();
    CHECK(sinkB.m_stream == data);

    //发送路径积压超过上限：被拒绝；积压降下来后 CheckWritable 产生一次可写通知
    loop.m_outA.m_queued = kP2PReliableQueueLimitBytes;
    CHECK(loop.m_a.QueuedBytes() == kP2PReliableQueueLimitBytes);
    CHECK(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(data.data()), 1, loop.m_now) == p2p_send_would_block);
    std::vector<P2PChannelEvent> events;
    loop.m_a.CheckWritable(events);
    CHECK(events.empty());
    loop.m_outA.m_queued = 0;
    loop.m_a.CheckWritable(events);
    REQUIRE(events.size() == 1);
    CHECK(events[0].m_type == p2p_channel_event_writable);
    CHECK(events[0].m_channelId == kChanA);
    events.clear();
    loop.m_a.CheckWritable(events);
    CHECK(events.empty());
}

TEST_CASE("P2PCH8 对方先发、本方后打开的通道：数据暂存，打开后按原顺序补交", "[p2pchan]")
{
    //B 打开通道之前推进的步数（毫秒）：UDP 路径上 KCP 会在这段时间里发出首批分段
    const int kEarlySteps = 5;
    const int paths[] = { p2p_link_path_relay, p2p_link_path_lan };
    for (size_t c = 0; c < sizeof(paths) / sizeof(paths[0]); ++c)
    {
        INFO("path " << paths[c]);
        MuxLoop loop(paths[c]);
        StreamSink sinkA;
        StreamSink sinkB;
        REQUIRE(loop.m_a.Open(kChanA, p2p_channel_reliable, &sinkA) == p2p_send_ok);

        //B 还没打开：A 先发的一段到了 B 也不交出，暂存在 B 的复用层里
        const std::string first = Pattern(kBigMessage * 3, (int)c);
        REQUIRE(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(first.data()), (uint32_t)first.size(), loop.m_now)
                == p2p_send_ok);
        for (int i = 0; i < kEarlySteps; ++i)
            loop.Step();
        CHECK(sinkB.m_stream.empty());
        CHECK(loop.m_b.EarlyBytes() > 0);
        CHECK_FALSE(loop.m_b.HasEarlyReplay());

        //B 打开：本身不交出数据，NextTimerMs 立即到期
        REQUIRE(loop.m_b.Open(kChanA, p2p_channel_reliable, &sinkB) == p2p_send_ok);
        CHECK(loop.m_b.HasEarlyReplay());
        CHECK(loop.m_b.NextTimerMs() == 0);
        CHECK(sinkB.m_stream.empty());

        //A 接着发第二段：它比补交先到 B，也要排在第一段后面
        const std::string second = Pattern(kBigMessage, (int)c + 1);
        REQUIRE(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(second.data()), (uint32_t)second.size(),
                              loop.m_now) == p2p_send_ok);
        for (int i = 0; i < kMaxSteps && sinkB.m_stream.size() < first.size() + second.size(); ++i)
            loop.Step();
        CHECK(sinkB.m_stream == first + second);
        CHECK(loop.m_b.EarlyBytes() == 0);
        CHECK_FALSE(loop.m_b.HasEarlyReplay());
    }

    //暂存的报文类型与本方打开时约定的不一致：补交时丢弃
    {
        MuxLoop loop(p2p_link_path_relay);
        StreamSink sinkA;
        RecordingSink sinkB;
        REQUIRE(loop.m_a.Open(kChanA, p2p_channel_reliable, &sinkA) == p2p_send_ok);
        REQUIRE(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>("abc"), 3, loop.m_now) == p2p_send_ok);
        loop.Step();
        REQUIRE(loop.m_b.EarlyBytes() > 0);
        REQUIRE(loop.m_b.Open(kChanA, p2p_channel_datagram, &sinkB) == p2p_send_ok);
        loop.Step();
        CHECK(sinkB.m_data.empty());
        CHECK(loop.m_b.EarlyBytes() == 0);
        CHECK_FALSE(loop.m_b.HasEarlyReplay());
    }

    //暂存总量有上限：超出的报文丢弃
    {
        MuxLoop loop(p2p_link_path_relay);
        StreamSink sinkA;
        REQUIRE(loop.m_a.Open(kChanA, p2p_channel_reliable, &sinkA) == p2p_send_ok);
        const std::string huge = Pattern(kP2PEarlyDataLimitBytes + kBigMessage, 5);
        REQUIRE(loop.m_a.Send(kChanA, reinterpret_cast<const uint8_t*>(huge.data()), (uint32_t)huge.size(), loop.m_now)
                == p2p_send_ok);
        loop.Step();
        CHECK(loop.m_b.EarlyBytes() <= kP2PEarlyDataLimitBytes);
        CHECK(loop.m_b.EarlyBytes() + kP2PRelayMaxPlainBytes > kP2PEarlyDataLimitBytes);
    }
}
