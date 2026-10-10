/**
 *  一对一通话接收端的网络统计，说明见 CallNetStats.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "CallNetStats.h"

namespace call {

namespace {

//RFC 3550 抖动估计的平滑系数：每个样本只向 |D| 靠近 1/16
const double kJitterGain = 1.0 / 16.0;
//千分比的分母
const uint64_t kPermille = 1000;
//四舍五入用的半个单位
const double kHalf = 0.5;

//质量报告各字段的偏移（字节），见 docs/音视频通话-网络状况标识方案.md 第 4.1 节
const size_t kReportOffVersion = 0;
const size_t kReportOffGrade   = 1;
const size_t kReportOffLoss    = 2;
const size_t kReportOffJitter  = 4;
const size_t kReportOffStall   = 6;
const size_t kReportOffEcho    = 8;
const size_t kReportOffHold    = 16;
const size_t kReportOffBuffer  = 20;   //2026-10-06 起占用原保留字段的前 2 字节
const size_t kReportOffFlags   = 22;   //2026-10-06 起占用原保留字段的第 3 字节（标志位），最后 1 字节仍保留
//各字段的字节数
const int kU16Bytes = 2;
const int kU32Bytes = 4;
const int kU64Bytes = 8;
//一个字节的位数与掩码，多字节字段按网络字节序拼装
const int      kBitsPerByte = 8;
const uint64_t kByteMask    = 0xFF;

//按网络字节序把 value 的低 bytes 个字节写到 p
void PutBe(uint64_t value, int bytes, uint8_t* p)
{
    for (int i = 0; i < bytes; ++i)
        p[i] = static_cast<uint8_t>((value >> ((bytes - 1 - i) * kBitsPerByte)) & kByteMask);
}

//按网络字节序从 p 读 bytes 个字节
uint64_t GetBe(const uint8_t* p, int bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < bytes; ++i)
        value = (value << kBitsPerByte) | p[i];
    return value;
}

} // namespace

CallRecvWindow::CallRecvWindow()
    : m_received(0)
    , m_lost(0)
    , m_late(0)
    , m_lossPermille(0)
    , m_jitterMs(0)
{
}

CallRecvStats::CallRecvStats()
    : m_seqStarted(false)
    , m_nextSeq(0)
    , m_tsStarted(false)
    , m_lastTsMs(0)
    , m_lastArriveMs(0)
    , m_jitterMs(0)
    , m_lastHeardMs(0)
    , m_received(0)
    , m_lost(0)
    , m_late(0)
{
}

void CallRecvStats::Reset(int64_t nowMs)
{
    m_seqStarted = false;
    m_nextSeq = 0;
    m_tsStarted = false;
    m_lastTsMs = 0;
    m_lastArriveMs = 0;
    m_jitterMs = 0;
    m_lastHeardMs = nowMs;
    m_received = 0;
    m_lost = 0;
    m_late = 0;
}

void CallRecvStats::OnAudioPacket(bool hasSeq, uint32_t seq, uint64_t tsMs, int64_t nowMs)
{
    m_lastHeardMs = nowMs;
    if (hasSeq)
    {
        if (m_seqStarted)
        {
            //按 32 位回绕求序号差：负数是迟到或重复的包，它对应的时段已经播过或补偿过
            const int32_t diff = static_cast<int32_t>(seq - m_nextSeq);
            if (diff < 0)
            {
                ++m_late;
                return;
            }
            m_lost += static_cast<uint32_t>(diff);
        }
        m_seqStarted = true;
        m_nextSeq = seq + 1;
    }
    ++m_received;

    //抖动：与上一个音频包比较到达间隔与发送间隔
    if (m_tsStarted)
    {
        const double arriveGap = static_cast<double>(nowMs - m_lastArriveMs);
        const double sendGap = static_cast<double>(static_cast<int64_t>(tsMs - m_lastTsMs));
        double d = arriveGap - sendGap;
        if (d < 0)
            d = -d;
        m_jitterMs += (d - m_jitterMs) * kJitterGain;
    }
    m_tsStarted = true;
    m_lastTsMs = tsMs;
    m_lastArriveMs = nowMs;
}

void CallRecvStats::OnAnyMessage(int64_t nowMs)
{
    m_lastHeardMs = nowMs;
}

CallRecvWindow CallRecvStats::TakeWindow()
{
    CallRecvWindow w;
    w.m_received = m_received;
    w.m_lost = m_lost;
    w.m_late = m_late;
    const uint64_t expected = static_cast<uint64_t>(m_received) + m_lost;
    w.m_lossPermille = (expected == 0) ? 0 : static_cast<uint32_t>(m_lost * kPermille / expected);
    w.m_jitterMs = JitterMs();
    m_received = 0;
    m_lost = 0;
    m_late = 0;
    return w;
}

bool CallRecvStats::IsSilent(int64_t nowMs) const
{
    return nowMs - m_lastHeardMs >= kCallSilentMs;
}

uint32_t CallRecvStats::JitterMs() const
{
    return static_cast<uint32_t>(m_jitterMs + kHalf);
}

// ======================== 质量报告（第 2 期）========================

CallQualityReport::CallQualityReport()
    : m_version(kCallReportVersion)
    , m_grade(kCallGradeUnknown)
    , m_lossPermille(0)
    , m_jitterMs(0)
    , m_stallPermille(0)
    , m_bufferMs(0)
    , m_echoTsMs(0)
    , m_holdMs(0)
    , m_flags(0)
{
}

void BuildCallQualityReport(const CallQualityReport& r, std::vector<uint8_t>& out)
{
    //保留字段（偏移 23 的 1 字节）随 assign 填 0
    out.assign(kCallReportBytes, 0);
    out[kReportOffVersion] = kCallReportVersion;
    out[kReportOffGrade] = r.m_grade;
    PutBe(r.m_lossPermille, kU16Bytes, &out[kReportOffLoss]);
    PutBe(r.m_jitterMs, kU16Bytes, &out[kReportOffJitter]);
    PutBe(r.m_stallPermille, kU16Bytes, &out[kReportOffStall]);
    PutBe(r.m_echoTsMs, kU64Bytes, &out[kReportOffEcho]);
    PutBe(r.m_holdMs, kU32Bytes, &out[kReportOffHold]);
    PutBe(r.m_bufferMs, kU16Bytes, &out[kReportOffBuffer]);
    out[kReportOffFlags] = r.m_flags;
}

bool ParseCallQualityReport(const uint8_t* data, size_t len, CallQualityReport& out)
{
    if (data == NULL || len < kCallReportBytes || data[kReportOffVersion] == 0)
        return false;
    out.m_version = data[kReportOffVersion];
    out.m_grade = data[kReportOffGrade];
    out.m_lossPermille = static_cast<uint16_t>(GetBe(data + kReportOffLoss, kU16Bytes));
    out.m_jitterMs = static_cast<uint16_t>(GetBe(data + kReportOffJitter, kU16Bytes));
    out.m_stallPermille = static_cast<uint16_t>(GetBe(data + kReportOffStall, kU16Bytes));
    out.m_echoTsMs = GetBe(data + kReportOffEcho, kU64Bytes);
    out.m_holdMs = static_cast<uint32_t>(GetBe(data + kReportOffHold, kU32Bytes));
    out.m_bufferMs = static_cast<uint16_t>(GetBe(data + kReportOffBuffer, kU16Bytes));
    out.m_flags = data[kReportOffFlags];
    return true;
}

CallReportExchange::CallReportExchange()
    : m_peerReports(0)
    , m_peerSendTsMs(0)
    , m_peerRecvMs(0)
    , m_hasRtt(false)
    , m_rttMs(0)
{
}

void CallReportExchange::Reset()
{
    m_peerReports = 0;
    m_peerSendTsMs = 0;
    m_peerRecvMs = 0;
    m_hasRtt = false;
    m_rttMs = 0;
    m_lastPeer = CallQualityReport();
}

void CallReportExchange::PrepareReport(int64_t nowMs, CallQualityReport& r)
{
    r.m_version = kCallReportVersion;
    if (m_peerReports > 0)
    {
        r.m_echoTsMs = m_peerSendTsMs;
        const int64_t hold = nowMs - m_peerRecvMs;
        r.m_holdMs = (hold > 0) ? static_cast<uint32_t>(hold) : 0;
    }
    else
    {
        r.m_echoTsMs = 0;
        r.m_holdMs = 0;
    }
}

void CallReportExchange::OnPeerReport(const CallQualityReport& r, uint64_t peerSendTsMs, int64_t nowMs)
{
    ++m_peerReports;
    m_peerSendTsMs = peerSendTsMs;
    m_peerRecvMs = nowMs;
    m_lastPeer = r;
    //回带时刻为 0：对方还没收到过本端的报告，没有往返时延可算
    if (r.m_echoTsMs == 0)
        return;
    const int64_t rtt = nowMs - static_cast<int64_t>(r.m_echoTsMs) - static_cast<int64_t>(r.m_holdMs);
    //算出负数说明时钟或字段异常，丢弃这个样本
    if (rtt < 0)
        return;
    m_rttMs = static_cast<uint32_t>(rtt);
    m_hasRtt = true;
}


} // namespace call
