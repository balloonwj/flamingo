/**
 *  BBR 类拥塞控制的单元测试（Suite P2PCC，标签 [p2pcc]），P2PCcTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 4 期 4c：两端用真实的可靠通道（P2PReliableChannel，经 KCP 并挂 P2PBbrCc），接在虚拟
 *  时钟下的模拟链路上（与第 0b 步原型的模拟相同：瓶颈带宽 + 尾部丢弃队列 + 随机丢包 + 传播时延，每毫秒推进一次），
 *  按第 14.1 节的六种网络条件传输一段数据，核对：
 *    - P2PCC1 各条件下的带宽利用率不低于原型结果减 8 个百分点，瓶颈队列的平均排队延迟不超过给定上限，数据完整送达；
 *    - P2PCC2 稳态（100M、往返 30 毫秒、丢包 0.1%）：进入带宽探测阶段，瓶颈带宽估计在链路有效带宽的 ±20% 以内，
 *             最小往返时延等于传播时延，超时时间按放宽后的规则不小于平均往返时延加 10 毫秒；
 *    - P2PCC3 挂接：可靠通道的 KCP 挂的是本拥塞控制，GetState 能取到状态；没挂的控制块取不到。
 *    - P2PCC4 无丢包长肥链路（100M、往返 30 毫秒）的稳态（2026-10-05）：启动阶段塞进队列的数据要在排空阶段放掉，传 8 秒，
 *            1 秒以后各窗口的平均排队延迟不超过往返传播时延的三分之一，利用率不低于 P2PCC1 的下限；
 *    - P2PCCD1 诊断（手动运行，标签 [.][p2pccdiag]）：无丢包长肥链路上传 15 秒，每 500 毫秒打印一行这段时间的平均
 *            排队延迟、拥塞控制阶段、速率增益、在途上限与实际在途分段数，用来分清排队来自启动阶段还是稳态。
 *    - P2PCCD2 诊断（同上标签）：同一链路传 2 秒，每 20 毫秒打印一行，看启动、排空到带宽探测这一段的细节。
 *
 *  利用率按「整段传输的有效吞吐 / 链路带宽」计，含启动阶段；传输时长比原型（理想 8 秒）短，结果会略低于原型。
 *
 *  典型用法：FlamingoUnit.exe "[p2pcc]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "../Chan/P2PBbrCc.h"
#include "../Chan/P2PChannelMux.h"
#include "../Chan/P2PReliableChannel.h"
#include "../Core/P2PPacket.h"
#include "../third-party/kcp/ikcp.h"

using namespace p2p;

namespace {

//一个 IP 头加 UDP 头的长度（字节）：模拟链路按线上字节计算发送时长
const size_t kIpUdpHeaderBytes = 28;

//微秒与毫秒、秒的换算，比特与字节的换算
const uint64_t kMicrosPerMilli = 1000;
const double kMicrosPerSecond = 1e6;
const double kMillisPerSecond = 1000.0;
const double kBitsPerByte = 8.0;
const double kBitsPerMbit = 1e6;

//队列容量的下限（报文个数）：带宽很低时按毫秒折算的队列太小，至少能放下这么多个满载报文
const double kMinQueuePackets = 16.0;

//被测通道的通道号（也作 KCP 的会话号）
const uint16_t kChannel = 1;

//发送端每次交给 Send 的块大小（字节）
const size_t kSendChunk = 64 * 1024;

//传输最多推进的时长是理想时长的多少倍（防止拥塞控制出错时测试跑不完）
const int kMaxDurationFactor = 4;

//KCP 每个分段的头部长度（字节，ikcp.c 的 IKCP_OVERHEAD）：满载报文的有效负载要减去它
const double kKcpOverheadBytes = 24.0;

//P2PCC2 用的条件（100M、往返 30 毫秒、丢包 0.1%）在 kConds 里的下标
const size_t kSteadyCondIndex = 3;

//P2PCC2 的容差：带宽估计允许偏离 ±20%；最小往返时延允许比传播时延多 2 毫秒（每毫秒推进一次带来的误差）；
//超时时间至少比平均往返时延多 10 毫秒（P2PBbrCc 放宽后的最小余量）
const double kBwTolerance = 0.2;
const uint32_t kMinRttSlackMs = 2;
const int32_t kRtoMinMarginMs = 10;

//确定性伪随机数（线性同余）的参数与种子
const uint32_t kLcgMul = 1103515245u;
const uint32_t kLcgAdd = 12345u;
const uint32_t kLcgShift = 8;
const uint32_t kLcgSeed = 20261003u;
const double kLcgRange = 16777216.0;

//一种网络条件与它的验收标准
struct NetCond
{
    const char* m_name;           //条件名（与第 14.1 节一致）
    double      m_rateMbps;       //瓶颈带宽（Mbit/s）
    int         m_rttMs;          //往返传播时延（毫秒，不含排队）
    double      m_loss;           //每个方向的随机丢包率
    int         m_queueMs;        //瓶颈队列容量，按带宽折算成毫秒
    int         m_durationMs;     //理想传输时长（毫秒）：传输量 = 带宽 × 这个时长
    double      m_minUtil;        //验收：带宽利用率下限（原型结果减 8 个百分点）
    double      m_maxAvgQueueMs;  //验收：平均排队延迟上限（毫秒）
};

//第 14.1 节的六种网络条件；原型结果依次为 98% / 81% / 91% / 89% / 84% / 68%，平均排队 1.3 / 1.3 / 27.7 / 4.0 / 5.4 / 6.3 毫秒
//wan-100m-0 的排队上限原为 40 毫秒（照原型的 27.7 放宽）；2026-10-05 调大排空阶段的轮数上限后实测 6.5 毫秒，收紧到 15 毫秒
const NetCond kConds[] =
{
    { "lan-1g",     1000.0,   1, 0.000,   4,  1000, 0.90,  3.0 },
    { "wifi-200m",   200.0,   5, 0.005,  20,  2000, 0.73,  5.0 },
    { "wan-100m-0",  100.0,  30, 0.000, 100,  3000, 0.83, 15.0 },
    { "wan-100m",    100.0,  30, 0.001, 100,  3000, 0.81, 10.0 },
    { "wan-20m",      20.0,  60, 0.010, 100,  6000, 0.76, 12.0 },
    { "wan-8m-bad",    8.0, 120, 0.050, 100, 10000, 0.60, 15.0 },
};

//在途的一个模拟报文
struct SimPacket
{
    uint64_t    m_deliverUs;   //到达时刻（微秒）
    std::string m_data;        //报文（通道头 + KCP 数据）
};

/**
 *  单向模拟链路：瓶颈带宽 + 尾部丢弃队列 + 随机丢包 + 传播时延（与第 0b 步原型的 LinkSend 相同）。
 */
class SimLink
{
public:
    SimLink(const NetCond& c, uint32_t* rng)
        : m_rateBps(c.m_rateMbps * kBitsPerMbit)
        , m_delayUs((uint64_t)c.m_rttMs * kMicrosPerMilli / 2)
        , m_loss(c.m_loss)
        , m_busyUntilUs(0)
        , m_rng(rng)
        , m_accepted(0)
        , m_dropQueue(0)
        , m_dropLoss(0)
        , m_sumQueueUs(0.0)
        , m_winAccepted(0)
        , m_winSumQueueUs(0.0)
    {
        const double queueBytes = m_rateBps / kBitsPerByte * (double)c.m_queueMs / kMillisPerSecond;
        const double minBytes = kMinQueuePackets * (double)(kP2PMaxDatagramBytes + kIpUdpHeaderBytes);
        m_queueLimitBytes = (queueBytes > minBytes) ? queueBytes : minBytes;
    }

    //发出一个报文：按线上字节排进瓶颈队列，放不下就丢弃；随机丢包在排队之后判定
    void Send(uint64_t nowUs, const std::string& plain)
    {
        const double wire = (double)(plain.size() + kP2PHeaderBytes + kP2PAuthTagBytes + kIpUdpHeaderBytes);
        const double txUs = wire * kBitsPerByte * kMicrosPerSecond / m_rateBps;
        const uint64_t start = (nowUs > m_busyUntilUs) ? nowUs : m_busyUntilUs;
        const uint64_t queueUs = start - nowUs;
        const double queuedBytes = (double)queueUs * m_rateBps / kBitsPerByte / kMicrosPerSecond;
        if (queuedBytes + wire > m_queueLimitBytes)
        {
            ++m_dropQueue;
            return;
        }
        m_busyUntilUs = start + (uint64_t)txUs;
        ++m_accepted;
        m_sumQueueUs += (double)queueUs;
        ++m_winAccepted;
        m_winSumQueueUs += (double)queueUs;
        *m_rng = *m_rng * kLcgMul + kLcgAdd;
        if ((double)(*m_rng >> kLcgShift) / kLcgRange < m_loss)
        {
            ++m_dropLoss;
            return;
        }
        SimPacket p;
        p.m_deliverUs = m_busyUntilUs + m_delayUs;
        p.m_data = plain;
        m_queue.push_back(p);
    }

    //取出到达时刻不晚于 nowUs 的报文
    bool PopDue(uint64_t nowUs, std::string& out)
    {
        if (m_queue.empty() || m_queue.front().m_deliverUs > nowUs)
            return false;
        out.swap(m_queue.front().m_data);
        m_queue.pop_front();
        return true;
    }

    //平均排队延迟（毫秒）
    double AvgQueueMs() const
    {
        return (m_accepted == 0) ? 0.0 : m_sumQueueUs / (double)m_accepted / (double)kMicrosPerMilli;
    }

    //上次调用以来进队报文的平均排队延迟（毫秒），并清零窗口统计；窗口里没有报文时为 0（排队延迟的时间序列用）
    double TakeWindowAvgQueueMs()
    {
        const double avg = (m_winAccepted == 0) ? 0.0
                         : m_winSumQueueUs / (double)m_winAccepted / (double)kMicrosPerMilli;
        m_winAccepted = 0;
        m_winSumQueueUs = 0.0;
        return avg;
    }

    uint64_t DropQueue() const { return m_dropQueue; }

private:
    double                m_rateBps;          //瓶颈带宽（bit/s）
    uint64_t              m_delayUs;          //单程传播时延（微秒）
    double                m_loss;             //随机丢包率
    double                m_queueLimitBytes;  //瓶颈队列容量（字节）
    uint64_t              m_busyUntilUs;      //瓶颈忙到的时刻（微秒）
    uint32_t*             m_rng;              //伪随机数状态（借用）
    std::deque<SimPacket> m_queue;            //在途报文（到达时刻单调递增）
    uint64_t              m_accepted;         //进了队列的报文个数
    uint64_t              m_dropQueue;        //队列满被丢弃的报文个数
    uint64_t              m_dropLoss;         //随机丢弃的报文个数
    double                m_sumQueueUs;       //进了队列的报文的排队时长之和（微秒）
    uint64_t              m_winAccepted;      //当前统计窗口里进了队列的报文个数
    double                m_winSumQueueUs;    //当前统计窗口里进了队列的报文的排队时长之和（微秒）
};

//一端的发送出口：报文交给模拟链路
class LinkOutput : public IP2PMuxOutput
{
public:
    LinkOutput(SimLink& link, uint64_t& nowUs)
        : m_link(link)
        , m_nowUs(nowUs)
    {
    }

    virtual bool SendDataPacket(const std::string& plain, int64_t nowMs) override
    {
        (void)nowMs;
        m_link.Send(m_nowUs, plain);
        return true;
    }

    virtual size_t QueuedBytes() const override
    {
        return 0;
    }

private:
    SimLink&  m_link;    //出向链路（借用）
    uint64_t& m_nowUs;   //模拟时钟（借用）
};

//一次传输的结果
struct TransferResult
{
    bool        m_done;          //是否传完
    double      m_util;          //带宽利用率（0～1）
    double      m_avgQueueMs;    //出向瓶颈队列的平均排队延迟（毫秒）
    uint64_t    m_dropQueue;     //出向队列溢出丢弃的报文个数
    P2PBbrState m_state;         //结束时的拥塞控制状态
    int32_t     m_srtt;          //结束时 KCP 的平均往返时延（毫秒）
    int32_t     m_rto;           //结束时 KCP 的超时时间（毫秒）
    std::string m_firstBytes;    //接收端收到的前若干字节（核对内容）
    //traceWindowMs 大于 0 时：每个统计窗口里进队报文的平均排队延迟（毫秒），按时间先后排列
    std::vector<double> m_windowQueueMs;
};

//按一种网络条件从 A 传到 B。traceWindowMs 大于 0 时每隔这么多毫秒记下这段时间的平均排队延迟（P2PCC4 用）；
//printTrace 为 true 时另打印一行：平均排队延迟、拥塞控制的阶段、速率增益、在途上限与实际在途分段数（P2PCCD1 诊断用）
TransferResult RunTransfer(const NetCond& c, int traceWindowMs = 0, bool printTrace = false)
{
    std::vector<double> windowQueueMs;
    uint32_t rng = kLcgSeed;
    SimLink ab(c, &rng);
    SimLink ba(c, &rng);
    uint64_t nowUs = 0;
    LinkOutput outA(ab, nowUs);
    LinkOutput outB(ba, nowUs);
    //KCP 的时间从 1 毫秒起，避免与「从未更新」混淆
    int64_t nowMs = 1;
    P2PReliableChannel a(kChannel, outA, true);
    P2PReliableChannel b(kChannel, outB, true);

    const uint64_t total = (uint64_t)(c.m_rateMbps * kBitsPerMbit / kBitsPerByte * (double)c.m_durationMs / kMillisPerSecond);
    const std::string chunk(kSendChunk, 'p');
    uint64_t sent = 0;
    uint64_t received = 0;
    std::string firstBytes;
    std::string pkt;
    std::string delivered;
    const int64_t capMs = (int64_t)c.m_durationMs * kMaxDurationFactor;
    for (; nowMs < capMs && received < total; ++nowMs)
    {
        nowUs = (uint64_t)nowMs * kMicrosPerMilli;
        while (ab.PopDue(nowUs, pkt))
        {
            delivered.clear();
            b.OnPayload(reinterpret_cast<const uint8_t*>(pkt.data()) + kP2PChannelHeaderBytes,
                        pkt.size() - kP2PChannelHeaderBytes, nowMs, delivered);
            received += delivered.size();
            if (firstBytes.empty())
                firstBytes = delivered;
        }
        while (ba.PopDue(nowUs, pkt))
        {
            delivered.clear();
            a.OnPayload(reinterpret_cast<const uint8_t*>(pkt.data()) + kP2PChannelHeaderBytes,
                        pkt.size() - kP2PChannelHeaderBytes, nowMs, delivered);
        }
        while (sent < total)
        {
            const uint32_t n = (uint32_t)((total - sent < kSendChunk) ? total - sent : kSendChunk);
            if (a.Send(reinterpret_cast<const uint8_t*>(chunk.data()), n, nowMs) != p2p_send_ok)
                break;
            sent += n;
        }
        if (a.NextTimerMs() <= nowMs)
            a.OnTimer(nowMs);
        if (b.NextTimerMs() <= nowMs)
            b.OnTimer(nowMs);
        if (traceWindowMs > 0 && nowMs % traceWindowMs == 0)
        {
            windowQueueMs.push_back(ab.TakeWindowAvgQueueMs());
            if (printTrace)
            {
                P2PBbrState st;
                memset(&st, 0, sizeof(st));
                P2PBbrCc::GetState(a.Kcp(), st);
                printf("P2PCCT %-11s t=%6lldms q=%6.1fms mode=%d gain=%.2f cwnd=%u inflight=%u btlbw=%.0fKB/s minrtt=%ums\n",
                       c.m_name, (long long)nowMs, windowQueueMs.back(), st.m_mode, st.m_pacingGain, st.m_cwnd,
                       (unsigned)a.Kcp()->nsnd_buf, st.m_btlBwBytesMs, st.m_minRttMs);
            }
        }
    }

    TransferResult r;
    r.m_done = (received >= total);
    const double secs = (double)nowMs / kMillisPerSecond;
    const double goodMbps = (double)received * kBitsPerByte / kBitsPerMbit / secs;
    r.m_util = goodMbps / c.m_rateMbps;
    r.m_avgQueueMs = ab.AvgQueueMs();
    r.m_dropQueue = ab.DropQueue();
    memset(&r.m_state, 0, sizeof(r.m_state));
    P2PBbrCc::GetState(a.Kcp(), r.m_state);
    r.m_srtt = a.Kcp()->rx_srtt;
    r.m_rto = a.Kcp()->rx_rto;
    r.m_firstBytes = firstBytes;
    r.m_windowQueueMs.swap(windowQueueMs);
    printf("P2PCC %-11s util=%5.1f%% avgQ=%5.1fms qdrop=%llu mode=%d btlbw=%.0fKB/s minrtt=%ums srtt=%dms rto=%dms %s\n",
           c.m_name, r.m_util * 100.0, r.m_avgQueueMs, (unsigned long long)r.m_dropQueue, r.m_state.m_mode,
           r.m_state.m_btlBwBytesMs, r.m_state.m_minRttMs, r.m_srtt, r.m_rto, r.m_done ? "done" : "CAP");
    return r;
}

} // namespace

TEST_CASE("P2PCC1 六种网络条件下的带宽利用率与排队延迟", "[p2pcc]")
{
    for (size_t i = 0; i < sizeof(kConds) / sizeof(kConds[0]); ++i)
    {
        const NetCond& c = kConds[i];
        INFO("condition " << c.m_name);
        const TransferResult r = RunTransfer(c);
        CHECK(r.m_done);
        CHECK(r.m_util >= c.m_minUtil);
        CHECK(r.m_avgQueueMs <= c.m_maxAvgQueueMs);
        CHECK(r.m_firstBytes.substr(0, 1) == "p");
    }
}

TEST_CASE("P2PCC2 稳态下的带宽估计、最小往返时延与超时时间", "[p2pcc]")
{
    const NetCond& c = kConds[kSteadyCondIndex];
    const TransferResult r = RunTransfer(c);
    REQUIRE(r.m_done);
    CHECK(r.m_state.m_mode == p2p_bbr_probe_bw);
    //链路有效带宽：线上字节里约 4.2% 是报文头、认证标签与 IP/UDP 头，按满载报文折算
    const double wirePerPacket = (double)(kP2PMaxDatagramBytes + kIpUdpHeaderBytes);
    const double payloadPerPacket = (double)(kP2PMaxPlainBytes - kP2PChannelHeaderBytes) - kKcpOverheadBytes;
    const double linkBytesPerMs = c.m_rateMbps * kBitsPerMbit / kBitsPerByte / kMillisPerSecond * payloadPerPacket
                                / wirePerPacket;
    CHECK(r.m_state.m_btlBwBytesMs >= linkBytesPerMs * (1.0 - kBwTolerance));
    CHECK(r.m_state.m_btlBwBytesMs <= linkBytesPerMs * (1.0 + kBwTolerance));
    CHECK(r.m_state.m_minRttMs >= (uint32_t)c.m_rttMs);
    CHECK(r.m_state.m_minRttMs <= (uint32_t)c.m_rttMs + kMinRttSlackMs);
    CHECK(r.m_rto >= r.m_srtt + kRtoMinMarginMs);
}

TEST_CASE("P2PCC3 可靠通道的 KCP 挂的是本拥塞控制", "[p2pcc]")
{
    uint32_t rng = kLcgSeed;
    SimLink link(kConds[0], &rng);
    uint64_t nowUs = 0;
    LinkOutput out(link, nowUs);
    P2PReliableChannel ch(kChannel, out, true);
    REQUIRE(ch.Kcp() != NULL);
    CHECK(ch.Kcp()->ccops == P2PBbrCc::Ops());
    P2PBbrState st;
    REQUIRE(P2PBbrCc::GetState(ch.Kcp(), st));
    CHECK(st.m_mode == p2p_bbr_startup);
    CHECK(st.m_minRttMs == 0);
    //中转路径不经 KCP；没挂本拥塞控制的控制块取不到状态
    P2PReliableChannel relay(kChannel, out, false);
    CHECK(relay.Kcp() == NULL);
    CHECK_FALSE(P2PBbrCc::GetState(relay.Kcp(), st));
    ikcpcb* plain = ikcp_create(kChannel, NULL);
    CHECK_FALSE(P2PBbrCc::GetState(plain, st));
    ikcp_release(plain);
}

namespace {

//P2PCCD1：无丢包长肥链路（100M、往返 30 毫秒、无丢包）在 kConds 里的下标
const size_t kNoLossLongCondIndex = 2;
//P2PCCD1：诊断时的传输时长（毫秒），比验收用的 3 秒长，看得到稳态
const int kDiagDurationMs = 15000;
//P2PCCD1：时间序列的统计窗口（毫秒）
const int kDiagTraceWindowMs = 500;

} // namespace

TEST_CASE("P2PCCD1 诊断：无丢包长肥链路上排队延迟随时间的变化（手动运行，只打印不断言）", "[.][p2pccdiag]")
{
    NetCond c = kConds[kNoLossLongCondIndex];
    c.m_durationMs = kDiagDurationMs;
    const TransferResult r = RunTransfer(c, kDiagTraceWindowMs, true);
    CHECK(r.m_done);
}

namespace {

//P2PCC4：稳态排队延迟从第几毫秒起统计（启动与排空阶段在这之前结束）
const int kSteadyFromMs = 1000;
//P2PCC4：传输时长（毫秒）。要短于最小往返时延的过期时间（10 秒）：往返时延探测会把队列清空，掩盖稳态的排队
const int kSteadyDurationMs = 8000;
//P2PCC4：稳态平均排队延迟的上限占往返传播时延的比例
const double kSteadyMaxQueueRttFraction = 1.0 / 3.0;

} // namespace

TEST_CASE("P2PCC4 无丢包长肥链路的稳态：启动阶段塞进队列的数据要排空，稳态平均排队延迟不超过往返时延的三分之一", "[p2pcc]")
{
    NetCond c = kConds[kNoLossLongCondIndex];
    c.m_durationMs = kSteadyDurationMs;
    const TransferResult r = RunTransfer(c, kDiagTraceWindowMs);
    REQUIRE(r.m_done);

    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < r.m_windowQueueMs.size(); ++i)
    {
        const int windowEndMs = (int)(i + 1) * kDiagTraceWindowMs;
        if (windowEndMs <= kSteadyFromMs)
            continue;
        sum += r.m_windowQueueMs[i];
        ++count;
    }
    REQUIRE(count > 0);
    const double steadyAvgMs = sum / (double)count;
    INFO("steady average queue " << steadyAvgMs << " ms over " << count << " windows, util " << r.m_util);
    CHECK(steadyAvgMs <= (double)c.m_rttMs * kSteadyMaxQueueRttFraction);
    CHECK(r.m_util >= c.m_minUtil);
}

namespace {

//P2PCCD2：看启动、排空到带宽探测这一段的传输时长（毫秒）与统计窗口（毫秒）
const int kDiagStartupDurationMs = 2000;
const int kDiagStartupWindowMs = 20;

} // namespace

TEST_CASE("P2PCCD2 诊断：无丢包长肥链路上启动、排空到带宽探测的细粒度过程（手动运行，只打印不断言）", "[.][p2pccdiag]")
{
    NetCond c = kConds[kNoLossLongCondIndex];
    c.m_durationMs = kDiagStartupDurationMs;
    const TransferResult r = RunTransfer(c, kDiagStartupWindowMs, true);
    CHECK(r.m_done);
}
