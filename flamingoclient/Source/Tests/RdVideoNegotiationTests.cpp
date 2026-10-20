/**
 *  远程桌面新旧画面格式的协商与控制端收帧队列（标签 [rdvideo]），RdVideoNegotiationTests.cpp
 *
 *  背景：docs/远程桌面H264与共享画质提升方案.md 4.4、4.7、6.2（2026-10-15）。不改服务端与信令，在引擎帧里双向握手：
 *  控制端发带能力位的关键帧请求，新被控端在每个关键帧之前发能力帧。
 *    - RDV4 被控端选格式：收到「能解 H.264」且本机能编时选 H.264；收到 1 字节请求、或 2 秒内没有请求时选旧格式；
 *           旧格式期间收到「能解 H.264」时切换，并报告「格式变了」（调用方据此让下一帧编成关键帧）；本机编码器中途彻底失败后
 *           回到旧格式且不再切换；
 *    - RDV5 控制端重发请求：仍在收旧格式、收到被控端「能编 H.264」的能力帧、本机能解时，重发带能力位的请求，每秒最多一次；
 *           已在收 H.264、对方不能编或本机不能解时不发；别的途径（连上时、解码需要关键帧时）刚发过请求的一秒内也不发；
 *    - RDV6 控制端收帧队列：超过上限（解码跟不上）时清空、丢到下一个关键帧并要求请求关键帧；关键帧到了恢复；
 *           关键帧请求限频，每秒最多一次（解码出错时同样经它限频）。
 *  2026-10-17 联调发现被控端抓不到画面时 H.264 推流线程空转、占满一个核（docs/远程桌面H264与共享画质提升方案.md 8.5），补充：
 *    - RDV12 推流循环每轮补足的时长：抓到新画面不等；有画面而没有新画面补足 33 毫秒；一帧画面都没有补足 200 毫秒（与旧格式相同）；
 *    - RDV13 交给发帧节奏的「有新画面」：需要关键帧且手里有画面时也算有新画面，会话中途切到 H.264、屏幕静止时也能立即编出第一帧；
 *    - RDV14 抓屏引擎存画面时判断是否与上一帧相同（DXGI 在可见画面没变时也会报告新帧，联调实测静止时仍约 2 Mbps）：
 *           原来没有画面、尺寸变了、任何一个像素不同都算变了并更新副本；逐字节相同时不算，副本不变；行尾的填充字节不参与比较；
 *    - RDV15 发帧节奏与时钟精度（联调实测画面每秒变化约 58 次时只编出约 20 fps）：60 Hz 的新画面，时钟精度 1 毫秒时编出 30 帧，
 *           精度 15.625 毫秒（GetTickCount64 的典型精度）时只有约 20 帧；
 *    - RDV16 推流循环的时钟 RdStreamClockMs 的精度：相邻两次读数的最小增量不超过 2 毫秒。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <stdint.h>
#include <chrono>
#include <vector>

#include "../../FlamingoMedia/RemoteDesktop/RdVideoNegotiation.h"
#include "../../FlamingoMedia/RemoteDesktop/RdVideoFrameQueue.h"
#include "../../FlamingoMedia/Share/ShareVideoPolicy.h"

using namespace remotedesktop;

namespace {

//用例里的时间起点（单调时钟，毫秒）
const int64_t kNegT0 = 50000;

RdKeyframeRequestInfo negRequest(bool hasCaps, bool canDecode, int maxLongSide)
{
    RdKeyframeRequestInfo info;
    info.m_hasCaps = hasCaps;
    info.m_canDecodeH264 = canDecode;
    info.m_maxLongSide = maxLongSide;
    return info;
}

std::vector<unsigned char> negFrame(unsigned char tag)
{
    return std::vector<unsigned char>(4, tag);
}

/**
 *  RDV15：模拟 1 秒钟里按显示器刷新率（60 Hz）不断到来的新画面，读时钟时按 tickMs 的精度取整，数发帧节奏编了几帧。
 *  @param tickMs 时钟精度（毫秒）
 *  @return 编码的帧数
 */
int negCountEncodes(double tickMs)
{
    const int kFramesPerSecond = 60;
    const double kMsPerSecond = 1000.0;
    screenshare::ShareFramePacer pacer(screenshare::kRdVideoMaxFps, screenshare::kShareKeyFrameIntervalMs);
    int encodes = 0;
    for (int k = 0; k < kFramesPerSecond; ++k)
    {
        const double trueMs = k * kMsPerSecond / kFramesPerSecond;
        const int64_t readMs = static_cast<int64_t>(floor(trueMs / tickMs) * tickMs);
        const screenshare::ShareFramePacer::Decision d = pacer.decide(kNegT0 + readMs, true, false, true);
        if (d.m_encode)
        {
            pacer.onEncoded(kNegT0 + readMs, d.m_keyframe);
            ++encodes;
        }
    }
    return encodes;
}

} // namespace

TEST_CASE("RDV4 被控端选格式：能解且能编选 H.264，1 字节请求或 2 秒没请求选旧格式，旧格式期间可切换", "[rdvideo]")
{
    //能解 H.264 的请求、本机能编：H.264，报告格式变了
    {
        RdFormatChooser c(true);
        c.Start(kNegT0);
        CHECK(c.Current(kNegT0) == rd_format_undecided);
        CHECK(c.OnKeyframeRequest(negRequest(true, true, 1920), kNegT0 + 100));
        CHECK(c.Current(kNegT0 + 100) == rd_format_h264);
        CHECK(c.ControllerMaxLongSide() == 1920);
        //再来一次同样的请求：格式没变
        CHECK_FALSE(c.OnKeyframeRequest(negRequest(true, true, 1920), kNegT0 + 200));
    }
    //旧控制端的 1 字节请求：旧格式
    {
        RdFormatChooser c(true);
        c.Start(kNegT0);
        c.OnKeyframeRequest(negRequest(false, false, 0), kNegT0 + 100);
        CHECK(c.Current(kNegT0 + 100) == rd_format_legacy);
    }
    //2 秒内没有请求：先按旧格式发
    {
        RdFormatChooser c(true);
        c.Start(kNegT0);
        CHECK(c.Current(kNegT0 + kRdFormatWaitMs - 1) == rd_format_undecided);
        CHECK(c.Current(kNegT0 + kRdFormatWaitMs) == rd_format_legacy);
        //之后收到能解 H.264 的请求：切换，报告格式变了
        CHECK(c.OnKeyframeRequest(negRequest(true, true, 2560), kNegT0 + kRdFormatWaitMs + 500));
        CHECK(c.Current(kNegT0 + kRdFormatWaitMs + 500) == rd_format_h264);
        CHECK(c.ControllerMaxLongSide() == 2560);
    }
    //本机不能编：一直旧格式
    {
        RdFormatChooser c(false);
        c.Start(kNegT0);
        c.OnKeyframeRequest(negRequest(true, true, 1920), kNegT0 + 100);
        CHECK(c.Current(kNegT0 + 100) == rd_format_legacy);
    }
    //编码器中途彻底失败：回到旧格式，之后的请求不再切回
    {
        RdFormatChooser c(true);
        c.Start(kNegT0);
        c.OnKeyframeRequest(negRequest(true, true, 1920), kNegT0 + 100);
        REQUIRE(c.Current(kNegT0 + 100) == rd_format_h264);
        c.OnEncoderFailed();
        CHECK(c.Current(kNegT0 + 200) == rd_format_legacy);
        CHECK_FALSE(c.OnKeyframeRequest(negRequest(true, true, 1920), kNegT0 + 300));
        CHECK(c.Current(kNegT0 + 300) == rd_format_legacy);
    }
}

TEST_CASE("RDV5 控制端重发带能力位的请求：旧格式期间收到「能编 H.264」时发，每秒最多一次", "[rdvideo]")
{
    RdCapsResendPacer p(true);
    //仍在收旧格式、对方能编：发
    CHECK(p.OnCapsFrame(kRdCapH264, false, kNegT0));
    //一秒之内：不发
    CHECK_FALSE(p.OnCapsFrame(kRdCapH264, false, kNegT0 + kRdCapsResendIntervalMs - 1));
    //满一秒：再发
    CHECK(p.OnCapsFrame(kRdCapH264, false, kNegT0 + kRdCapsResendIntervalMs));
    //已在收 H.264：不发
    CHECK_FALSE(p.OnCapsFrame(kRdCapH264, true, kNegT0 + 5 * kRdCapsResendIntervalMs));
    //对方不能编：不发
    CHECK_FALSE(p.OnCapsFrame(0, false, kNegT0 + 10 * kRdCapsResendIntervalMs));
    //本机不能解：不发
    RdCapsResendPacer cannot(false);
    CHECK_FALSE(cannot.OnCapsFrame(kRdCapH264, false, kNegT0));

    //连上时刚发过带能力位的请求：被控端随即「能力帧 + H.264 关键帧」，能力帧到达时还没解出 H.264 画面，一秒内不重发，
    //免得每次会话开始都多要一个关键帧；超过一秒仍在收旧格式（最初的请求丢了）才重发
    RdCapsResendPacer fresh(true);
    fresh.OnRequestSent(kNegT0);
    CHECK_FALSE(fresh.OnCapsFrame(kRdCapH264, false, kNegT0 + kRdCapsResendIntervalMs - 1));
    CHECK(fresh.OnCapsFrame(kRdCapH264, false, kNegT0 + kRdCapsResendIntervalMs));
}

TEST_CASE("RDV6 控制端收帧队列：解码跟不上时丢到下一个关键帧并请求关键帧，请求限频", "[rdvideo]")
{
    const size_t kCap = 3;
    RdVideoFrameQueue q(kCap);
    //关键帧与两个增量帧：放得下，不请求
    CHECK_FALSE(q.Push(negFrame(1), true));
    CHECK_FALSE(q.Push(negFrame(2), false));
    CHECK_FALSE(q.Push(negFrame(3), false));
    CHECK(q.Size() == kCap);
    CHECK_FALSE(q.WaitingForKeyframe());
    //第四帧（增量）：队列满了，清空、丢到下一个关键帧，要求请求关键帧
    CHECK(q.Push(negFrame(4), false));
    CHECK(q.Size() == 0);
    CHECK(q.WaitingForKeyframe());
    //等关键帧期间的增量帧一律丢弃，不重复要求（请求被限频挡下时，调用方凭 WaitingForKeyframe 稍后补发）
    CHECK_FALSE(q.Push(negFrame(5), false));
    CHECK(q.Size() == 0);
    //关键帧到了：收下，恢复
    CHECK_FALSE(q.Push(negFrame(6), true));
    CHECK_FALSE(q.WaitingForKeyframe());
    CHECK_FALSE(q.Push(negFrame(7), false));
    std::vector<unsigned char> out;
    REQUIRE(q.Pop(out));
    CHECK(out[0] == 6);
    REQUIRE(q.Pop(out));
    CHECK(out[0] == 7);
    CHECK_FALSE(q.Pop(out));
    //队列满时到的恰好是关键帧：清空后只留它，不用请求
    CHECK_FALSE(q.Push(negFrame(8), false));
    CHECK_FALSE(q.Push(negFrame(9), false));
    CHECK_FALSE(q.Push(negFrame(10), false));
    CHECK_FALSE(q.Push(negFrame(11), true));
    CHECK(q.Size() == 1);
    REQUIRE(q.Pop(out));
    CHECK(out[0] == 11);
    //清空：关窗或会话结束
    q.Push(negFrame(12), false);
    q.Clear();
    CHECK(q.Size() == 0);

    //关键帧请求限频：每秒最多一次
    RdKeyRequestLimiter limiter;
    CHECK(limiter.Allow(kNegT0));
    CHECK_FALSE(limiter.Allow(kNegT0 + kRdKeyRequestMinIntervalMs - 1));
    CHECK(limiter.Allow(kNegT0 + kRdKeyRequestMinIntervalMs));
}

TEST_CASE("RDV12 推流循环每轮补足的时长：新画面不等，没有新画面补足 33 毫秒，没有画面补足 200 毫秒", "[rdvideo]")
{
    //抓到新画面：不等，画面变化时的帧率不受影响（DXGI 每来一帧就返回）
    CHECK(RdVideoRoundWaitMs(0, true, true) == 0);
    CHECK(RdVideoRoundWaitMs(5, true, true) == 0);
    //有画面、这一轮没有新画面（GDI 画面没变、抓取出错时沿用上一帧）：补足 kRdVideoIdleRoundMs
    CHECK(RdVideoRoundWaitMs(0, true, false) == kRdVideoIdleRoundMs);
    CHECK(RdVideoRoundWaitMs(10, true, false) == kRdVideoIdleRoundMs - 10);
    //DXGI 在屏幕不变时已经阻塞满等待时长：不再等
    CHECK(RdVideoRoundWaitMs(kRdVideoIdleRoundMs, true, false) == 0);
    CHECK(RdVideoRoundWaitMs(kRdVideoIdleRoundMs + 20, true, false) == 0);
    //一帧画面都没有（隐藏桌面、锁屏时抓取一直失败）：补足 kRdVideoNoFrameRoundMs，与旧格式的抓屏周期相同
    CHECK(kRdVideoNoFrameRoundMs > kRdVideoIdleRoundMs);
    CHECK(RdVideoRoundWaitMs(0, false, false) == kRdVideoNoFrameRoundMs);
    CHECK(RdVideoRoundWaitMs(150, false, false) == kRdVideoNoFrameRoundMs - 150);
    CHECK(RdVideoRoundWaitMs(kRdVideoNoFrameRoundMs + 1, false, false) == 0);
    //没有画面时「新画面」无意义，按没有画面处理
    CHECK(RdVideoRoundWaitMs(0, false, true) == kRdVideoNoFrameRoundMs);
    //时钟异常（已用时长为负）：按 0 处理
    CHECK(RdVideoRoundWaitMs(-5, true, false) == kRdVideoIdleRoundMs);
    CHECK(RdVideoRoundWaitMs(-5, false, false) == kRdVideoNoFrameRoundMs);
}

TEST_CASE("RDV17 推流循环每轮补足的时长：画面没变而指针在动时只补足 16 毫秒，其余情况与指针不动时相同", "[rdvideo]")
{
    //指针动了比没动等得短：远端指针每秒约更新 60 次
    CHECK(kRdVideoCursorRoundMs < kRdVideoIdleRoundMs);
    //有画面、没有新画面、指针动了：补足 kRdVideoCursorRoundMs
    CHECK(RdVideoRoundWaitMs(0, true, false, true) == kRdVideoCursorRoundMs);
    CHECK(RdVideoRoundWaitMs(6, true, false, true) == kRdVideoCursorRoundMs - 6);
    //指针没动：仍补足 kRdVideoIdleRoundMs（与 RDV12 用默认参数调用的结果相同）
    CHECK(RdVideoRoundWaitMs(0, true, false, false) == kRdVideoIdleRoundMs);
    CHECK(RdVideoRoundWaitMs(10, true, false, false) == RdVideoRoundWaitMs(10, true, false));
    //抓到新画面：指针动不动都不等
    CHECK(RdVideoRoundWaitMs(0, true, true, true) == 0);
    CHECK(RdVideoRoundWaitMs(0, true, true, false) == 0);
    //没有画面：指针动不动都补足 kRdVideoNoFrameRoundMs（抓取不可用时不因指针加快空转）
    CHECK(RdVideoRoundWaitMs(0, false, false, true) == kRdVideoNoFrameRoundMs);
    CHECK(RdVideoRoundWaitMs(0, false, true, true) == kRdVideoNoFrameRoundMs);
    //这一轮已经用掉的时间够长：不再等
    CHECK(RdVideoRoundWaitMs(kRdVideoCursorRoundMs, true, false, true) == 0);
    CHECK(RdVideoRoundWaitMs(kRdVideoCursorRoundMs + 20, true, false, true) == 0);
    //时钟异常（已用时长为负）：按 0 处理
    CHECK(RdVideoRoundWaitMs(-5, true, false, true) == kRdVideoCursorRoundMs);
}

TEST_CASE("RDV13 需要关键帧且手里有画面时也交给发帧节奏「有新画面」，屏幕静止时也能编出第一帧", "[rdvideo]")
{
    screenshare::ShareFramePacer pacer(screenshare::kRdVideoMaxFps, screenshare::kShareKeyFrameIntervalMs);
    //对照：会话中途由旧格式切到 H.264、屏幕静止，直接把抓取结果（没有新画面）交给从没编过码的发帧节奏，编不出第一帧
    const screenshare::ShareFramePacer::Decision raw = pacer.decide(kNegT0, false, true, true);
    CHECK_FALSE(raw.m_encode);
    //经 RdVideoPacerHasNewFrame：需要关键帧、手里有画面，算有新画面，立即编成关键帧
    const bool seen = RdVideoPacerHasNewFrame(true, false, true);
    CHECK(seen);
    const screenshare::ShareFramePacer::Decision d = pacer.decide(kNegT0, seen, true, true);
    CHECK(d.m_encode);
    CHECK(d.m_keyframe);
    //其余情况与抓取结果相同：抓到新画面算有；不需要关键帧时静止画面不算（静止不编）；手里没有画面时不算
    CHECK(RdVideoPacerHasNewFrame(true, true, false));
    CHECK_FALSE(RdVideoPacerHasNewFrame(true, false, false));
    CHECK_FALSE(RdVideoPacerHasNewFrame(false, false, true));
    CHECK_FALSE(RdVideoPacerHasNewFrame(false, false, false));
}

TEST_CASE("RDV14 抓屏引擎存画面时判断是否与上一帧相同：相同的画面不算新画面，行尾填充不参与比较", "[rdvideo]")
{
    //4×3 的画面，抓取后端的行跨度比宽 × 4 多 8 个字节的填充
    const int kW = 4;
    const int kH = 3;
    const int kBytesPerPixel = 4;
    const int kPadding = 8;
    const int kStride = kW * kBytesPerPixel + kPadding;
    std::vector<BYTE> src(static_cast<size_t>(kStride) * kH, 0x11);
    FrameView view;
    view.m_data = &src[0];
    view.m_width = kW;
    view.m_height = kH;
    view.m_stride = kStride;

    //原来没有画面：算变了，副本为紧密排列（不含填充）
    std::vector<BYTE> dst;
    int dstW = 0;
    int dstH = 0;
    CHECK(RdStoreFrameIfChanged(view, dst, dstW, dstH));
    CHECK(dstW == kW);
    CHECK(dstH == kH);
    CHECK(dst.size() == static_cast<size_t>(kW) * kH * kBytesPerPixel);

    //同样的画面：不算变了
    CHECK_FALSE(RdStoreFrameIfChanged(view, dst, dstW, dstH));

    //只有行尾的填充字节不同：不算变了
    src[kW * kBytesPerPixel] = 0x99;
    CHECK_FALSE(RdStoreFrameIfChanged(view, dst, dstW, dstH));

    //最后一行最后一个像素的一个字节不同：算变了，副本随之更新；再来一次同样的画面就不算了
    const size_t lastByte = static_cast<size_t>(kStride) * (kH - 1) + kW * kBytesPerPixel - 1;
    src[lastByte] = 0x22;
    CHECK(RdStoreFrameIfChanged(view, dst, dstW, dstH));
    CHECK(dst[dst.size() - 1] == 0x22);
    CHECK_FALSE(RdStoreFrameIfChanged(view, dst, dstW, dstH));

    //第一行第一个字节不同：算变了，后面各行照样复制进副本
    src[0] = 0x33;
    src[lastByte] = 0x44;
    CHECK(RdStoreFrameIfChanged(view, dst, dstW, dstH));
    CHECK(dst[0] == 0x33);
    CHECK(dst[dst.size() - 1] == 0x44);

    //尺寸变了（只取前两行）：算变了，副本换成新尺寸
    view.m_height = kH - 1;
    CHECK(RdStoreFrameIfChanged(view, dst, dstW, dstH));
    CHECK(dstH == kH - 1);
    CHECK(dst.size() == static_cast<size_t>(kW) * (kH - 1) * kBytesPerPixel);
    CHECK_FALSE(RdStoreFrameIfChanged(view, dst, dstW, dstH));
}

TEST_CASE("RDV15 发帧节奏与时钟精度：60 Hz 的新画面，1 毫秒精度编出 30 帧，15.6 毫秒精度只有约 20 帧", "[rdvideo]")
{
    //时钟精度 1 毫秒：每两帧编一帧，正好是帧率上限
    CHECK(negCountEncodes(1.0) == screenshare::kRdVideoMaxFps);
    //时钟精度 15.625 毫秒（GetTickCount64 的典型精度）：33 毫秒的最短间隔读成 31 毫秒、不够，要等到读成 46～47 毫秒，
    //每三帧才编一帧
    const double kTickCountMs = 15.625;
    const int coarse = negCountEncodes(kTickCountMs);
    CHECK(coarse <= screenshare::kRdVideoMaxFps * 2 / 3 + 1);
}

TEST_CASE("RDV16 推流循环的时钟精度：相邻两次读数的最小增量不超过 2 毫秒", "[rdvideo]")
{
    //最多取样这么久（毫秒），足够看到若干次增量
    const long long kSampleMs = 100;
    const int64_t kMaxStepMs = 2;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int64_t last = RdStreamClockMs();
    int64_t minStep = 0;
    int steps = 0;
    bool monotonic = true;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < kSampleMs)
    {
        const int64_t now = RdStreamClockMs();
        if (now < last)
            monotonic = false;
        if (now > last)
        {
            const int64_t step = now - last;
            if (steps == 0 || step < minStep)
                minStep = step;
            ++steps;
            last = now;
        }
    }
    CHECK(monotonic);
    REQUIRE(steps > 0);
    CHECK(minStep <= kMaxStepMs);
}
