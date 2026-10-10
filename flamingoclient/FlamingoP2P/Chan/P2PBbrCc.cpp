/**
 *  BBR 类拥塞控制的实现，见 P2PBbrCc.h。由第 0b 步的原型（tmp/p2p_0b/p2p_probe.cpp 的 BbrCc）整理而来，算法与原型相同。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PBbrCc.h"

#include <string.h>

#include "../third-party/kcp/ikcp.h"

namespace p2p {

namespace {

//启动阶段的增益：2/ln2，每轮把发送速率翻倍所需的增益（BBR 论文的取值）
const double kHighGain = 2.885;

//带宽探测阶段的速率增益循环：1.25 倍试探、0.75 倍回收、其余 6 个阶段按估计带宽
const int kCycleLength = 8;
const double kCycleGains[kCycleLength] = { 1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

//进入带宽探测时从循环的第几个阶段开始：从「按估计带宽」的阶段开始，避免刚排空就加速
const int kCycleStartIndex = 2;

//带宽探测阶段的在途数据增益
const double kProbeBwCwndGain = 2.0;

//一轮的最短时长（毫秒）：局域网往返时延只有一两毫秒，按往返时延分轮时样本噪声太大
const uint32_t kMinRoundMs = 10;

//瓶颈带宽取最近多少轮样本的最大值
const int kBwRounds = 10;

//送达历史的记录个数（时间与累计确认字节的成对记录），用于计算一个时间窗内的送达速率
const int kHistorySize = 512;

//启动阶段判断「带宽已到顶」：连续 kFullBwRounds 轮增长都不足 kFullBwGrowth 倍
const double kFullBwGrowth = 1.25;
const int kFullBwRounds = 3;

//启动阶段判断「丢包过多」：一轮内发出超过 kStartupLossMinSegs 个分段且丢失超过 1 / kStartupLossDivisor（2%）
const uint32_t kStartupLossMinSegs = 50;
const uint32_t kStartupLossDivisor = 50;

//排空阶段最多停留的轮数：成片丢包时丢失的分段一直占着在途数据，只靠在途数据判断会一直退不出来，故要有上限。
//上限不能太小：2026-10-05 之前取 2，往返 30 毫秒的无丢包链路上两轮放不掉启动阶段塞进队列的数据，而带宽探测阶段的在途
//上限是 2 倍带宽时延积、没有把这部分排掉的机制，稳态里一直留着约一个往返时延的排队（用例 P2PCC4，诊断 P2PCCD1、
//P2PCCD2）。模拟实测：取 3 无效，取 4 只排掉一半，取 5 起排干净；取 6 留一轮余量。千兆局域网的利用率随之从 96% 降到
//约 94%，其余条件基本不变
const int kDrainMaxRounds = 6;

//最小往返时延多久没有刷新就进入往返时延探测（毫秒）
const uint32_t kMinRttExpireMs = 10000;

//往返时延探测至少持续的时长（毫秒），以及期间的在途数据上限（分段数）
const uint32_t kProbeRttMinHoldMs = 200;
const uint32_t kProbeRttCwnd = 4;

//在途数据上限的下限与初值（分段数）
const uint32_t kMinCwnd = 4;
const uint32_t kInitialCwnd = 32;

//在途数据上限里为刷新间隔预留的余量：kIntervalHeadroom × 带宽 × 刷新间隔
const double kIntervalHeadroom = 2.0;

//发送额度按距上次刷新的时长计算，时长最多计 kPacingMaxElapsedMs 毫秒（IO 线程卡顿后不一次放出过多数据）。
//取两个 Windows 默认定时器周期（约 15.6 毫秒）：系统没有按要求提高定时器精度时，IO 线程约 16 毫秒才醒一次，上限若
//小于这个间隔，每轮实际发出的少于测得的带宽，测得的带宽又一轮比一轮低，传输越来越慢（第 4f 步实测）
const uint32_t kPacingMaxElapsedMs = 32;

//还没有带宽样本时的发送速率：每毫秒 kHighGain 个分段（第 0b 步原型的取值）
const double kInitialSegsPerMs = 1.0;

//超时时间的余量：max(kRtoVarMultiplier × 偏差, 平均往返时延 / kRtoSrttDivisor, kRtoMinMarginMs)，上限 kRtoMaxMs
const int32_t kRtoVarMultiplier = 4;
const int32_t kRtoSrttDivisor = 4;
const int32_t kRtoMinMarginMs = 10;
const int32_t kRtoMaxMs = 60000;

//挂在 kcp->congest 上的状态
struct BbrState
{
    int      m_mode;                      //阶段，取值见 p2p_bbr_mode
    double   m_btlBw;                     //瓶颈带宽估计（字节 / 毫秒）= 最近 kBwRounds 轮样本的最大值
    double   m_ring[kBwRounds];           //最近各轮的送达速率样本（字节 / 毫秒）
    int      m_ringPos;                   //m_ring 的写入位置
    double   m_roundMax;                  //本轮内的最大送达速率样本
    bool     m_roundAppLimited;           //本轮发送是否受业务方限制（没有足够的数据可发）
    uint32_t m_roundStart;                //本轮开始的时刻（KCP 时间，毫秒）
    uint32_t m_minRtt;                    //最小往返时延（毫秒），0 表示尚无样本
    uint32_t m_minRttStamp;               //最小往返时延最近一次刷新的时刻
    uint32_t m_probeMin;                  //往返时延探测期间测到的最小值
    uint64_t m_delivered;                 //累计已确认的字节数
    uint32_t m_histTime[kHistorySize];    //送达历史：时刻
    uint64_t m_histDelivered[kHistorySize];   //送达历史：当时的累计确认字节数
    int      m_histPos;                   //送达历史的写入位置
    int      m_histCount;                 //送达历史的有效记录数
    double   m_fullBw;                    //启动阶段：上一次带宽明显增长时的值
    int      m_fullCnt;                   //启动阶段：带宽增长不足的连续轮数
    int      m_cycle;                     //带宽探测阶段：当前所处的循环阶段
    uint32_t m_cycleStamp;                //带宽探测阶段：当前循环阶段开始的时刻
    uint32_t m_probeRttUntil;             //往返时延探测：结束时刻
    double   m_pacingGain;                //当前的速率增益
    double   m_cwndGain;                  //当前的在途数据增益
    uint32_t m_lastPacing;                //上次计算发送额度的时刻
    int32_t  m_lastBudget;                //上次给出的发送额度（字节）
    uint32_t m_sentSince;                 //上次给出额度之后实际发出的字节数
    uint32_t m_roundSentSegs;             //本轮发出的分段数（含重传）
    uint32_t m_roundLostSegs;             //本轮判定丢失的分段数（快速重传 + 超时重传）
    uint32_t m_lastXmit;                  //上次看到的 kcp->xmit（超时重传累计数）
    int      m_drainRounds;               //已在排空阶段停留的轮数
};

BbrState* StateOf(ikcpcb* kcp)
{
    return static_cast<BbrState*>(kcp->congest);
}

//按速率计算本次刷新的发送额度：上次没用完的部分最多结转一个分段
uint32_t PacingBudget(BbrState* s, uint32_t now, double rateBytesPerMs, uint32_t mss)
{
    uint32_t elapsed = now - s->m_lastPacing;
    if (elapsed > kPacingMaxElapsedMs)
        elapsed = kPacingMaxElapsedMs;
    s->m_lastPacing = now;
    double carry = (double)s->m_lastBudget - (double)s->m_sentSince;
    if (carry < 0.0)
        carry = 0.0;
    if (carry > (double)mss)
        carry = (double)mss;
    s->m_lastBudget = (int32_t)(rateBytesPerMs * (double)elapsed + carry);
    s->m_sentSince = 0;
    return (uint32_t)s->m_lastBudget;
}

int CcInit(ikcpcb* kcp)
{
    BbrState* s = new BbrState();
    memset(s, 0, sizeof(*s));
    s->m_mode = p2p_bbr_startup;
    s->m_pacingGain = kHighGain;
    s->m_cwndGain = kHighGain;
    s->m_roundStart = kcp->current;
    s->m_lastPacing = kcp->current;
    kcp->congest = s;
    kcp->cwnd = kInitialCwnd;
    return 0;
}

void CcRelease(ikcpcb* kcp)
{
    delete StateOf(kcp);
    kcp->congest = NULL;
}

//以下三个回调必须给出（空实现即可），否则 KCP 会执行自带的窗口增减逻辑

void CcOnAck(ikcpcb* kcp, IUINT32 ackedSegs, IUINT32 ackedBytes, IUINT32 priorInFlight)
{
    (void)kcp;
    (void)ackedSegs;
    (void)ackedBytes;
    (void)priorInFlight;
}

void CcOnFastRetransmit(ikcpcb* kcp, IUINT32 fastRetrans, IUINT32 inflight, IUINT32 priorCwnd)
{
    (void)inflight;
    (void)priorCwnd;
    StateOf(kcp)->m_roundLostSegs += fastRetrans;
}

void CcOnTimeout(ikcpcb* kcp, IUINT32 priorCwnd)
{
    (void)kcp;
    (void)priorCwnd;
}

void CcOnAppLimited(ikcpcb* kcp, IUINT32 inflight)
{
    (void)inflight;
    StateOf(kcp)->m_roundAppLimited = true;
}

//每收到一个确认按回带的发送时间戳算一次往返时延：从这里取样，并放宽 KCP 的超时余量
void CcOnRtt(ikcpcb* kcp, IINT32 rtt)
{
    BbrState* s = StateOf(kcp);
    if (rtt < 0)
        return;
    int32_t margin = kRtoVarMultiplier * (int32_t)kcp->rx_rttval;
    if ((int32_t)kcp->rx_srtt / kRtoSrttDivisor > margin)
        margin = (int32_t)kcp->rx_srtt / kRtoSrttDivisor;
    if (kRtoMinMarginMs > margin)
        margin = kRtoMinMarginMs;
    int32_t rto = (int32_t)kcp->rx_srtt + margin;
    if (rto < (int32_t)kcp->rx_minrto)
        rto = (int32_t)kcp->rx_minrto;
    if (rto > kRtoMaxMs)
        rto = kRtoMaxMs;
    kcp->rx_rto = rto;

    const uint32_t r = (rtt < 1) ? 1 : (uint32_t)rtt;
    if (s->m_mode == p2p_bbr_probe_rtt && (s->m_probeMin == 0 || r < s->m_probeMin))
        s->m_probeMin = r;
    if (s->m_minRtt == 0 || r <= s->m_minRtt)
    {
        s->m_minRtt = r;
        s->m_minRttStamp = kcp->current;
    }
}

void CcOnPktSent(ikcpcb* kcp, IUINT32 sn, IUINT32 ts, IUINT32 len, IUINT32 inflight, IUINT32 xmit)
{
    (void)sn;
    (void)ts;
    (void)inflight;
    (void)xmit;
    BbrState* s = StateOf(kcp);
    s->m_sentSince += len;
    ++s->m_roundSentSegs;
}

void CcOnPktAcked(ikcpcb* kcp, IUINT32 sn, IUINT32 ts, IUINT32 len, IINT32 rtt, IUINT32 xmit)
{
    (void)sn;
    (void)ts;
    (void)rtt;
    (void)xmit;
    StateOf(kcp)->m_delivered += len;
}

//记录送达历史并取一个时间窗内的送达速率，作为本轮的样本候选
void SampleDeliveryRate(BbrState* s, uint32_t now, uint32_t window)
{
    const int last = (s->m_histPos + kHistorySize - 1) % kHistorySize;
    if (s->m_histCount == 0 || s->m_histTime[last] != now)
    {
        s->m_histTime[s->m_histPos] = now;
        s->m_histDelivered[s->m_histPos] = s->m_delivered;
        s->m_histPos = (s->m_histPos + 1) % kHistorySize;
        if (s->m_histCount < kHistorySize)
            ++s->m_histCount;
    }
    //从新到旧找第一条早于时间窗的记录（或最旧的一条），用它与现在的差值算速率
    for (int i = 1; i < s->m_histCount; ++i)
    {
        const int idx = (s->m_histPos + kHistorySize - 1 - i) % kHistorySize;
        const uint32_t age = now - s->m_histTime[idx];
        if (age >= window || i == s->m_histCount - 1)
        {
            if (age > 0)
            {
                const double rate = (double)(s->m_delivered - s->m_histDelivered[idx]) / (double)age;
                if (rate > s->m_roundMax)
                    s->m_roundMax = rate;
            }
            break;
        }
    }
}

//一轮结束：更新瓶颈带宽；启动阶段判断带宽是否已不再增长、丢包是否过多
void EndRound(ikcpcb* kcp, BbrState* s, uint32_t now)
{
    if (!s->m_roundAppLimited || s->m_roundMax > s->m_btlBw)
    {
        s->m_ring[s->m_ringPos] = s->m_roundMax;
        s->m_ringPos = (s->m_ringPos + 1) % kBwRounds;
        double maxBw = 0.0;
        for (int i = 0; i < kBwRounds; ++i)
        {
            if (s->m_ring[i] > maxBw)
                maxBw = s->m_ring[i];
        }
        s->m_btlBw = maxBw;
        if (s->m_mode == p2p_bbr_startup)
        {
            if (s->m_btlBw >= s->m_fullBw * kFullBwGrowth)
            {
                s->m_fullBw = s->m_btlBw;
                s->m_fullCnt = 0;
            }
            else
            {
                ++s->m_fullCnt;
            }
        }
    }
    //超时重传数从 kcp->xmit 的增量取得
    s->m_roundLostSegs += kcp->xmit - s->m_lastXmit;
    s->m_lastXmit = kcp->xmit;
    if (s->m_mode == p2p_bbr_startup && s->m_roundSentSegs > kStartupLossMinSegs
        && s->m_roundLostSegs * kStartupLossDivisor > s->m_roundSentSegs)
        s->m_fullCnt = kFullBwRounds;
    if (s->m_mode == p2p_bbr_drain)
        ++s->m_drainRounds;
    s->m_roundSentSegs = 0;
    s->m_roundLostSegs = 0;
    s->m_roundMax = 0.0;
    s->m_roundAppLimited = false;
    s->m_roundStart = now;
}

//阶段切换
void UpdateMode(ikcpcb* kcp, BbrState* s, uint32_t now, uint32_t window)
{
    const double bdp = s->m_btlBw * (double)(s->m_minRtt > 0 ? s->m_minRtt : 1);
    const double inflight = (double)kcp->nsnd_buf * (double)kcp->mss;
    if (s->m_mode == p2p_bbr_startup && s->m_fullCnt >= kFullBwRounds)
    {
        s->m_mode = p2p_bbr_drain;
        s->m_drainRounds = 0;
    }
    if (s->m_mode == p2p_bbr_drain)
    {
        const double floor = (double)kMinCwnd * (double)kcp->mss;
        if (inflight <= (bdp > floor ? bdp : floor) || s->m_drainRounds >= kDrainMaxRounds)
        {
            s->m_mode = p2p_bbr_probe_bw;
            s->m_cycle = kCycleStartIndex;
            s->m_cycleStamp = now;
        }
    }
    if (s->m_mode == p2p_bbr_probe_bw && now - s->m_cycleStamp >= window)
    {
        s->m_cycle = (s->m_cycle + 1) % kCycleLength;
        s->m_cycleStamp = now;
    }
    if (s->m_mode != p2p_bbr_probe_rtt && s->m_mode != p2p_bbr_startup && s->m_minRtt > 0
        && now - s->m_minRttStamp > kMinRttExpireMs)
    {
        s->m_mode = p2p_bbr_probe_rtt;
        s->m_probeMin = 0;
        s->m_probeRttUntil = now + (s->m_minRtt > kProbeRttMinHoldMs ? s->m_minRtt : kProbeRttMinHoldMs);
    }
    if (s->m_mode == p2p_bbr_probe_rtt && (int32_t)(now - s->m_probeRttUntil) >= 0)
    {
        if (s->m_probeMin > 0)
            s->m_minRtt = s->m_probeMin;
        s->m_minRttStamp = now;
        s->m_mode = p2p_bbr_probe_bw;
        s->m_cycleStamp = now;
    }
}

//每次刷新前调用：取样、分轮、切换阶段，并据此设置增益与在途数据上限
void CcOnTick(ikcpcb* kcp)
{
    BbrState* s = StateOf(kcp);
    const uint32_t now = kcp->current;
    const uint32_t window = (s->m_minRtt > kMinRoundMs) ? s->m_minRtt : kMinRoundMs;
    SampleDeliveryRate(s, now, window);
    if (now - s->m_roundStart >= window)
        EndRound(kcp, s, now);
    UpdateMode(kcp, s, now, window);

    switch (s->m_mode)
    {
    //启动：按高增益增速
    case p2p_bbr_startup:
        s->m_pacingGain = kHighGain;
        s->m_cwndGain = kHighGain;
        break;

    //排空：以高增益的倒数发送，放掉启动阶段多塞进队列的数据
    case p2p_bbr_drain:
        s->m_pacingGain = 1.0 / kHighGain;
        s->m_cwndGain = kHighGain;
        break;

    //带宽探测：按循环阶段取速率增益
    case p2p_bbr_probe_bw:
        s->m_pacingGain = kCycleGains[s->m_cycle];
        s->m_cwndGain = kProbeBwCwndGain;
        break;

    //往返时延探测：按估计带宽发送，在途数据下面压到 kProbeRttCwnd
    default:
        s->m_pacingGain = 1.0;
        s->m_cwndGain = 1.0;
        break;
    }

    uint32_t cwnd = kInitialCwnd;
    if (s->m_btlBw > 0.0)
    {
        const double bdp = s->m_btlBw * (double)(s->m_minRtt > 0 ? s->m_minRtt : 1);
        const double bytes = s->m_cwndGain * bdp + kIntervalHeadroom * s->m_btlBw * (double)kcp->interval;
        cwnd = (uint32_t)(bytes / (double)kcp->mss) + 1;
        if (cwnd < kMinCwnd)
            cwnd = kMinCwnd;
    }
    if (s->m_mode == p2p_bbr_probe_rtt)
        cwnd = kProbeRttCwnd;
    kcp->cwnd = cwnd;
}

IUINT32 CcPacingRate(ikcpcb* kcp)
{
    BbrState* s = StateOf(kcp);
    const double rate = (s->m_btlBw > 0.0) ? s->m_pacingGain * s->m_btlBw
                                           : kHighGain * kInitialSegsPerMs * (double)kcp->mss;
    return PacingBudget(s, kcp->current, rate, kcp->mss);
}

//KCP 的回调表：字段顺序见 ikcp.h 的 struct IKCPOPS
const IKCPOPS kBbrOps =
{
    "flamingo-bbr", CcInit, CcRelease, CcOnAck, CcOnFastRetransmit, CcOnTimeout, CcOnTick, CcOnAppLimited, CcOnRtt,
    CcOnPktSent, CcOnPktAcked, NULL, CcPacingRate
};

} // namespace

const IKCPOPS* P2PBbrCc::Ops()
{
    return &kBbrOps;
}

bool P2PBbrCc::GetState(const IKCPCB* kcp, P2PBbrState& out)
{
    if (kcp == NULL || kcp->ccops != &kBbrOps || kcp->congest == NULL)
        return false;
    const BbrState* s = static_cast<const BbrState*>(kcp->congest);
    out.m_mode = s->m_mode;
    out.m_btlBwBytesMs = s->m_btlBw;
    out.m_minRttMs = s->m_minRtt;
    out.m_pacingGain = s->m_pacingGain;
    out.m_cwnd = kcp->cwnd;
    return true;
}

} // namespace p2p
