/**
 *  一对一通话接收端网络统计的单元测试（标签 [callnet]，docs/音视频通话-网络状况标识方案.md 第 8.1 节），CallNetStatsTests.cpp
 *  覆盖 FlamingoMedia/Call/CallNetStats.h：
 *    - NS1 丢包率：序号连续时为 0；中间缺 5 个、共应收 100 个时为 50‰；序号回绕不误算；迟到、重复的包不计入；
 *    - NS2 抖动：到达间隔与发送间隔一致时为 0；到达间隔交替 10 / 30 ms（发送间隔 20 ms）时收敛到约 10 ms；
 *    - NS3 断流：最后一条消息之后 2.9 秒不算断流、3 秒算；收到新消息立即恢复；只有质量报告、没有音频包时
 *           不算断流且丢包率为 0；一条都没收到时从统计开始起算；
 *    - NS4 质量报告编解码（第 2 期）：24 字节，各字段原样解回；长度不足、版本为 0 时拒绝；版本更高时只读认识的字段；
 *    - NS5 往返时延：对方回带本端上一份报告的发送时刻与停留时长，RTT = 收到时刻 − 回带时刻 − 停留时长；
 *           回带时刻为 0（对方还没收到过本端报告）时不更新；
 *    - NQ4 不考虑老版本（2026-10-05 用户决定）：收不到对方报告时每份照常填写、回带为 0；收到后开始回带。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Call/CallNetStats.h"

using namespace call;
using Catch::Approx;

namespace {

//一个音频包的发送间隔（毫秒）：20 ms 一帧
const int64_t kNsFrameMs = 20;
//统计开始的时刻（毫秒，任意取一个不为 0 的值）
const int64_t kNsStartMs = 100000;
//NS1 一个窗口里应收的包数
const uint32_t kNsPackets = 100;
//NS1 中间丢掉的包数
const uint32_t kNsLost = 5;
//NS2 喂入的包数：足够让抖动估计收敛（每个包只向真实值靠近 1/16）
const int kNsJitterPackets = 400;
//NS2 交替的两种到达间隔（毫秒）
const int64_t kNsShortGapMs = 10;
const int64_t kNsLongGapMs = 30;
//NS2 期望收敛到的抖动（毫秒）与允许误差
const double kNsExpectJitterMs = 10.0;
const double kNsJitterTolerance = 1.0;
//NS3 用到的时长（毫秒）：2.9 秒与 3 秒
const int64_t kNsAlmostSilentMs = 2900;

} // namespace

TEST_CASE("NS1 丢包率：连续为 0，缺 5 个共应收 100 个为 50 千分比，序号回绕不误算，迟到与重复不计入", "[callnet]")
{
    //序号连续
    CallRecvStats stats;
    stats.Reset(kNsStartMs);
    for (uint32_t i = 0; i < kNsPackets; ++i)
        stats.OnAudioPacket(true, i, i * kNsFrameMs, kNsStartMs + i * kNsFrameMs);
    CallRecvWindow w = stats.TakeWindow();
    CHECK(w.m_received == kNsPackets);
    CHECK(w.m_lost == 0);
    CHECK(w.m_lossPermille == 0);

    //窗口取走后计数清零
    w = stats.TakeWindow();
    CHECK(w.m_received == 0);
    CHECK(w.m_lossPermille == 0);

    //中间缺 5 个（序号 40~44），应收 100 个
    CallRecvStats lossy;
    lossy.Reset(kNsStartMs);
    for (uint32_t i = 0; i < kNsPackets; ++i)
    {
        if (i >= 40 && i < 40 + kNsLost)
            continue;
        lossy.OnAudioPacket(true, i, i * kNsFrameMs, kNsStartMs + i * kNsFrameMs);
    }
    w = lossy.TakeWindow();
    CHECK(w.m_received == kNsPackets - kNsLost);
    CHECK(w.m_lost == kNsLost);
    CHECK(w.m_lossPermille == 50);

    //序号回绕：0xFFFFFFFE、0xFFFFFFFF、0、1 是连续的
    CallRecvStats wrap;
    wrap.Reset(kNsStartMs);
    const uint32_t seqs[] = { 0xFFFFFFFEu, 0xFFFFFFFFu, 0u, 1u };
    for (int i = 0; i < 4; ++i)
        wrap.OnAudioPacket(true, seqs[i], i * kNsFrameMs, kNsStartMs + i * kNsFrameMs);
    w = wrap.TakeWindow();
    CHECK(w.m_received == 4);
    CHECK(w.m_lost == 0);

    //迟到（比已收到的序号小）与重复的包：不计入收到数，也不改丢包数
    CallRecvStats late;
    late.Reset(kNsStartMs);
    late.OnAudioPacket(true, 10, 0, kNsStartMs);
    late.OnAudioPacket(true, 12, 2 * kNsFrameMs, kNsStartMs + 2 * kNsFrameMs);   //11 丢了
    late.OnAudioPacket(true, 11, kNsFrameMs, kNsStartMs + 3 * kNsFrameMs);       //11 迟到
    late.OnAudioPacket(true, 12, 2 * kNsFrameMs, kNsStartMs + 4 * kNsFrameMs);   //12 重复
    w = late.TakeWindow();
    CHECK(w.m_received == 2);
    CHECK(w.m_lost == 1);
    CHECK(w.m_late == 2);

    //中转路径没有序号：只计收到数，丢包率恒为 0
    CallRecvStats relay;
    relay.Reset(kNsStartMs);
    for (uint32_t i = 0; i < kNsPackets; ++i)
        relay.OnAudioPacket(false, 0, i * kNsFrameMs, kNsStartMs + i * kNsFrameMs);
    w = relay.TakeWindow();
    CHECK(w.m_received == kNsPackets);
    CHECK(w.m_lossPermille == 0);
}

TEST_CASE("NS2 抖动：到达间隔与发送间隔一致时为 0；交替 10 / 30 ms 时收敛到约 10 ms", "[callnet]")
{
    CallRecvStats steady;
    steady.Reset(kNsStartMs);
    for (int i = 0; i < kNsJitterPackets; ++i)
        steady.OnAudioPacket(true, static_cast<uint32_t>(i), i * kNsFrameMs, kNsStartMs + i * kNsFrameMs);
    CHECK(steady.JitterMs() == 0);

    CallRecvStats jittery;
    jittery.Reset(kNsStartMs);
    int64_t arrive = kNsStartMs;
    for (int i = 0; i < kNsJitterPackets; ++i)
    {
        jittery.OnAudioPacket(true, static_cast<uint32_t>(i), i * kNsFrameMs, arrive);
        arrive += (i % 2 == 0) ? kNsShortGapMs : kNsLongGapMs;
    }
    CHECK(static_cast<double>(jittery.JitterMs()) == Approx(kNsExpectJitterMs).margin(kNsJitterTolerance));
    //窗口里带出当前的抖动估计；取走窗口不清零抖动（它是滑动估计）
    CallRecvWindow w = jittery.TakeWindow();
    CHECK(static_cast<double>(w.m_jitterMs) == Approx(kNsExpectJitterMs).margin(kNsJitterTolerance));
    CHECK(static_cast<double>(jittery.JitterMs()) == Approx(kNsExpectJitterMs).margin(kNsJitterTolerance));
}

TEST_CASE("NS3 断流：2.9 秒不算、3 秒算，收到消息立即恢复；只有质量报告时不算断流；一条没收到时从开始起算", "[callnet]")
{
    CallRecvStats stats;
    stats.Reset(kNsStartMs);
    //一条都没收到：从统计开始起算
    CHECK_FALSE(stats.IsSilent(kNsStartMs + kNsAlmostSilentMs));
    CHECK(stats.IsSilent(kNsStartMs + kCallSilentMs));

    //收到一个音频包后重新计时
    const int64_t t1 = kNsStartMs + kCallSilentMs;
    stats.OnAudioPacket(true, 0, 0, t1);
    CHECK_FALSE(stats.IsSilent(t1));
    CHECK_FALSE(stats.IsSilent(t1 + kNsAlmostSilentMs));
    CHECK(stats.IsSilent(t1 + kCallSilentMs));

    //对方关了麦克风：没有音频包，只有质量报告（任意消息）——不算断流，丢包率为 0
    const int64_t t2 = t1 + kCallSilentMs;
    stats.TakeWindow();
    stats.OnAnyMessage(t2);
    CHECK_FALSE(stats.IsSilent(t2 + kNsAlmostSilentMs));
    const int64_t t3 = t2 + kNsAlmostSilentMs;
    stats.OnAnyMessage(t3);
    CHECK_FALSE(stats.IsSilent(t3 + kNsAlmostSilentMs));
    CallRecvWindow w = stats.TakeWindow();
    CHECK(w.m_received == 0);
    CHECK(w.m_lossPermille == 0);
    //最后一条消息之后 3 秒：断流
    CHECK(stats.IsSilent(t3 + kCallSilentMs));
}

namespace {

//NS4：报告里各字段取互不相同、能看出字节序错误的值
const uint16_t kNsReportLoss = 0x0123;
const uint16_t kNsReportJitter = 0x0456;
const uint16_t kNsReportStall = 0x0789;
const uint64_t kNsReportEcho = 0x0102030405060708ULL;
const uint32_t kNsReportHold = 0x0A0B0C0DU;
const uint8_t  kNsReportGrade = 3;
//NS4：播放缓冲延迟字段（2026-10-06 B4，偏移 20 起 2 字节）
const uint16_t kNsReportBuffer = 0x0ABC;
const size_t   kNsReportOffBuffer = 20;
//NS4：更高版本的报告比 24 字节多带的字节数
const size_t kNsReportExtraBytes = 6;
//NS5：A 发出报告、B 收到与回报、A 收到的本地时刻（毫秒），以及期望的往返时延
const int64_t kNsASendMs = 1000;
const int64_t kNsBRecvMs = 5000;
const int64_t kNsBSendMs = 5030;
const int64_t kNsARecvMs = 1100;
const uint32_t kNsExpectRttMs = 70;
//NQ4：没有对方报告时连续填写的份数
const int kNsQuietReports = 10;

} // namespace

TEST_CASE("NS4 质量报告编解码：24 字节原样解回；过短、版本 0 拒绝；版本更高时只读认识的字段", "[callnet]")
{
    CallQualityReport r;
    r.m_grade = kNsReportGrade;
    r.m_lossPermille = kNsReportLoss;
    r.m_jitterMs = kNsReportJitter;
    r.m_stallPermille = kNsReportStall;
    r.m_echoTsMs = kNsReportEcho;
    r.m_holdMs = kNsReportHold;
    r.m_bufferMs = kNsReportBuffer;
    std::vector<uint8_t> bytes;
    BuildCallQualityReport(r, bytes);
    REQUIRE(bytes.size() == kCallReportBytes);
    CHECK(bytes[0] == kCallReportVersion);
    //网络字节序：丢包率 0x0123 依次是 0x01、0x23
    CHECK(bytes[2] == 0x01);
    CHECK(bytes[3] == 0x23);
    //播放缓冲延迟在偏移 20，网络字节序；之后是标志字节（本用例没设，为 0，见 CallSystemAudioTests 的 CSA3）与 1 字节保留字段
    CHECK(bytes[kNsReportOffBuffer] == 0x0A);
    CHECK(bytes[kNsReportOffBuffer + 1] == 0xBC);
    CHECK(bytes[kNsReportOffBuffer + 2] == 0);
    CHECK(bytes[kNsReportOffBuffer + 3] == 0);

    CallQualityReport back;
    REQUIRE(ParseCallQualityReport(&bytes[0], bytes.size(), back));
    CHECK(back.m_version == kCallReportVersion);
    CHECK(back.m_grade == kNsReportGrade);
    CHECK(back.m_lossPermille == kNsReportLoss);
    CHECK(back.m_jitterMs == kNsReportJitter);
    CHECK(back.m_stallPermille == kNsReportStall);
    CHECK(back.m_echoTsMs == kNsReportEcho);
    CHECK(back.m_holdMs == kNsReportHold);
    CHECK(back.m_bufferMs == kNsReportBuffer);

    //过短
    CHECK_FALSE(ParseCallQualityReport(&bytes[0], kCallReportBytes - 1, back));
    CHECK_FALSE(ParseCallQualityReport(NULL, 0, back));
    //版本 0 不是合法报告
    std::vector<uint8_t> zero = bytes;
    zero[0] = 0;
    CHECK_FALSE(ParseCallQualityReport(&zero[0], zero.size(), back));
    //更高版本、后面多带几个字节：只读认识的字段
    std::vector<uint8_t> newer = bytes;
    newer[0] = kCallReportVersion + 1;
    newer.insert(newer.end(), kNsReportExtraBytes, 0xEE);
    REQUIRE(ParseCallQualityReport(&newer[0], newer.size(), back));
    CHECK(back.m_version == kCallReportVersion + 1);
    CHECK(back.m_lossPermille == kNsReportLoss);
    CHECK(back.m_holdMs == kNsReportHold);
    //默认构造的报告是「未评级」
    CHECK(CallQualityReport().m_grade == kCallGradeUnknown);
}

TEST_CASE("NS5 往返时延：回带时刻 1000、停留 30 ms、1100 收到时为 70 ms；回带时刻为 0 时不更新", "[callnet]")
{
    CallReportExchange a;
    CallReportExchange b;
    a.Reset();
    b.Reset();

    //A 发出第一份报告：还没收到过 B 的报告，回带时刻为 0
    CallQualityReport ra;
    a.PrepareReport(kNsASendMs, ra);
    CHECK(ra.m_echoTsMs == 0);
    CHECK(ra.m_holdMs == 0);

    //B 收到 A 的报告（A 的发送时刻在封装头里）：回带时刻为 0，B 没有往返时延
    b.OnPeerReport(ra, static_cast<uint64_t>(kNsASendMs), kNsBRecvMs);
    CHECK_FALSE(b.HasRtt());
    CHECK(b.HasPeerReport());

    //B 30 ms 后回报：回带 A 的发送时刻与停留时长
    CallQualityReport rb;
    b.PrepareReport(kNsBSendMs, rb);
    CHECK(rb.m_echoTsMs == static_cast<uint64_t>(kNsASendMs));
    CHECK(rb.m_holdMs == static_cast<uint32_t>(kNsBSendMs - kNsBRecvMs));

    //A 收到：RTT = 1100 − 1000 − 30 = 70
    a.OnPeerReport(rb, static_cast<uint64_t>(kNsBSendMs), kNsARecvMs);
    REQUIRE(a.HasRtt());
    CHECK(a.RttMs() == kNsExpectRttMs);

    //回带时刻算出来是负数（时钟异常）：不更新
    CallQualityReport bogus = rb;
    bogus.m_echoTsMs = static_cast<uint64_t>(kNsARecvMs + kNsExpectRttMs);
    a.OnPeerReport(bogus, static_cast<uint64_t>(kNsBSendMs), kNsARecvMs);
    CHECK(a.RttMs() == kNsExpectRttMs);
}

TEST_CASE("NQ4 不考虑老版本：收不到对方报告时每份照常填写、回带为 0、没有往返时延；收到后开始回带", "[callnet]")
{
    CallReportExchange x;
    x.Reset();
    int64_t now = kNsASendMs;
    //连续多份都没有对方报告：照常填写（不存在「停发」），回带字段为 0
    for (int i = 0; i < kNsQuietReports; ++i)
    {
        CallQualityReport r;
        r.m_echoTsMs = kNsReportEcho;
        r.m_holdMs = kNsReportHold;
        x.PrepareReport(now, r);
        CHECK(r.m_version == kCallReportVersion);
        CHECK(r.m_echoTsMs == 0);
        CHECK(r.m_holdMs == 0);
        now += kNsFrameMs;
    }
    CHECK_FALSE(x.HasRtt());
    CHECK(x.ReportsReceived() == 0);

    //收到对方的一份报告后，下一份回带它的发送时刻与停留时长
    CallQualityReport peer;
    x.OnPeerReport(peer, static_cast<uint64_t>(kNsBSendMs), now);
    CHECK(x.ReportsReceived() == 1);
    CallQualityReport r;
    x.PrepareReport(now + kNsFrameMs, r);
    CHECK(r.m_echoTsMs == static_cast<uint64_t>(kNsBSendMs));
    CHECK(r.m_holdMs == static_cast<uint32_t>(kNsFrameMs));
}
