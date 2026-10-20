/**
 *  屏幕共享码率自适应的单元测试（Suite SSBR，标签 [sharebitrate]），ShareBitrateTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareBitrateController 与编码器的运行中改码率（docs/屏幕共享与录制方案.md 第 9 期 9b）：
 *    - SSBR1 起始为最高码率；不拥塞时一直不变，不足一个周期不评估；
 *    - SSBR2 发送阻塞达到周期的四分之一时降到 70%，反复拥塞时停在下限；
 *    - SSBR3 拥塞之后连续 3 个周期不拥塞才开始回升，之后每个周期加最高码率的 10%，不超过最高码率；
 *    - SSBR4 一个周期里关键帧请求达到 3 次才算拥塞，1~2 次（新成员加入）不降；
 *    - SSBR5 编码器运行中把码率从 2 Mbps 改到 200 kbps 后，同样的画面编出来的数据量明显变小；
 *    - SSBR6 SetRange 换码率范围（源分辨率变了，docs/远程桌面H264与共享画质提升方案.md 4.2，2026-10-15）：当前码率取给定值并
 *            夹在新范围内，连续不拥塞计数清零，之后的回升按新上限的 10%、降到新下限为止；
 *    - SSBR7 回执延迟（远程桌面用，方案 4.5）：周期内最大延迟超过门限算拥塞，启用后超过 3 秒没有回执也算；
 *            从不调用 OnFeedbackDelay 时（屏幕共享）行为与原来完全相同。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objbase.h>
#include <mfapi.h>
#include <math.h>
#include <stdlib.h>
#include <vector>

#include "../../FlamingoMedia/Share/ShareBitrateController.h"
#include "../../FlamingoMedia/Call/MfH264Codec.h"

#pragma comment(lib, "mfplat.lib")

using namespace screenshare;

namespace {

//测试用的最高、最低码率（bit/s）
const int kBrMax = 2000000;
const int kBrMin = 300000;
//一次拥塞后的码率：最高码率的 70%
const int kBrAfterOneDrop = 1400000;
//回升一步的增量：最高码率的 10%
const int kBrStep = 200000;
//评估周期（毫秒），与产品代码一致
const int64_t kBrWindow = kShareBitrateWindowMs;

//初始化本线程的 COM 与 Media Foundation
class BrMfScope
{
public:
    BrMfScope()
    {
        ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ::MFStartup(MF_VERSION);
    }

    ~BrMfScope()
    {
        ::MFShutdown();
        ::CoUninitialize();
    }
};

//统计编码输出的字节数
class BrSink : public call::MfH264EncoderSink
{
public:
    BrSink()
        : m_bytes(0)
    {
    }

    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        (void)data;
        (void)keyframe;
        (void)ptsHns;
        m_bytes += len;
    }

    size_t m_bytes;   //累计输出字节数
};

//画一帧会动的纹理：亮度为横竖两个方向的正弦条纹，随帧号平移，再叠一点噪声；色度为灰。
//编码器的码率控制能在 2 Mbps 与 200 kbps 下都达到目标（纯随机噪声在最高量化下也压不到 2 Mbps，不能用来比较）
void brTextureFrame(std::vector<uint8_t>& nv12, int w, int h, int frame)
{
    //条纹的中心亮度、幅度、横竖两个方向的空间周期（像素）、每帧平移的相位，以及叠加噪声的范围
    const double kMid = 128.0;
    const double kAmp = 60.0;
    const double kPeriodX = 7.0;
    const double kPeriodY = 5.0;
    const double kPhasePerFrame = 0.3;
    const int kNoiseMask = 15;
    const uint8_t kGrayUv = 128;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const double v = kMid + kAmp * sin(x / kPeriodX + frame * kPhasePerFrame) * cos(y / kPeriodY);
            nv12[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(static_cast<int>(v) + (rand() & kNoiseMask));
        }
    }
    for (size_t k = static_cast<size_t>(w) * h; k < nv12.size(); ++k)
        nv12[k] = kGrayUv;
}

//让控制器经历一个拥塞的周期（发送阻塞占整个周期的一半）后评估，返回评估结果
bool brCongestedWindow(ShareBitrateController& ctl, int64_t& now, int& bps)
{
    ctl.OnFrameSent(kBrWindow / 2);
    now += kBrWindow;
    return ctl.Evaluate(now, bps);
}

//让控制器经历一个不拥塞的周期后评估
bool brCalmWindow(ShareBitrateController& ctl, int64_t& now, int& bps)
{
    ctl.OnFrameSent(1);
    now += kBrWindow;
    return ctl.Evaluate(now, bps);
}

} // namespace

TEST_CASE("SSBR1 起始为最高码率，不拥塞时不变", "[sharebitrate]")
{
    ShareBitrateController ctl(kBrMax, kBrMin);
    CHECK(ctl.CurrentBps() == kBrMax);
    int64_t now = 1000;
    int bps = 0;
    //第一次评估只记下周期起点
    CHECK_FALSE(ctl.Evaluate(now, bps));
    //不足一个周期：即使阻塞很久也不评估
    ctl.OnFrameSent(kBrWindow);
    CHECK_FALSE(ctl.Evaluate(now + kBrWindow - 1, bps));
    CHECK(ctl.CurrentBps() == kBrMax);
    //不拥塞的周期：不变（已在最高码率）
    ShareBitrateController calm(kBrMax, kBrMin);
    int64_t t = 0;
    calm.Evaluate(t, bps);
    for (int i = 0; i < 5; ++i)
        CHECK_FALSE(brCalmWindow(calm, t, bps));
    CHECK(calm.CurrentBps() == kBrMax);
}

TEST_CASE("SSBR2 发送阻塞达到四分之一时降到 70%，反复拥塞停在下限", "[sharebitrate]")
{
    ShareBitrateController ctl(kBrMax, kBrMin);
    int64_t now = 0;
    int bps = 0;
    ctl.Evaluate(now, bps);
    //阻塞不到四分之一：不降
    ctl.OnFrameSent(kBrWindow / 4 - 1);
    now += kBrWindow;
    CHECK_FALSE(ctl.Evaluate(now, bps));
    //阻塞正好四分之一：降到 70%
    ctl.OnFrameSent(kBrWindow / 4);
    now += kBrWindow;
    REQUIRE(ctl.Evaluate(now, bps));
    CHECK(bps == kBrAfterOneDrop);
    CHECK(ctl.CurrentBps() == kBrAfterOneDrop);
    //反复拥塞：一路降到下限后不再变化
    for (int i = 0; i < 10; ++i)
        brCongestedWindow(ctl, now, bps);
    CHECK(ctl.CurrentBps() == kBrMin);
    CHECK_FALSE(brCongestedWindow(ctl, now, bps));
}

TEST_CASE("SSBR3 连续 3 个周期不拥塞后开始回升，每周期加 10%，不超过最高码率", "[sharebitrate]")
{
    ShareBitrateController ctl(kBrMax, kBrMin);
    int64_t now = 0;
    int bps = 0;
    ctl.Evaluate(now, bps);
    REQUIRE(brCongestedWindow(ctl, now, bps));
    REQUIRE(ctl.CurrentBps() == kBrAfterOneDrop);
    //前两个不拥塞的周期不变
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
    //第三个开始回升
    REQUIRE(brCalmWindow(ctl, now, bps));
    CHECK(bps == kBrAfterOneDrop + kBrStep);
    REQUIRE(brCalmWindow(ctl, now, bps));
    CHECK(bps == kBrAfterOneDrop + 2 * kBrStep);
    //再拥塞一次：回升中断，重新计数
    REQUIRE(brCongestedWindow(ctl, now, bps));
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
    //一直不拥塞：最终回到最高码率，不超过
    for (int i = 0; i < 20; ++i)
        brCalmWindow(ctl, now, bps);
    CHECK(ctl.CurrentBps() == kBrMax);
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
}

TEST_CASE("SSBR4 一个周期里关键帧请求达到 3 次才算拥塞", "[sharebitrate]")
{
    ShareBitrateController ctl(kBrMax, kBrMin);
    int64_t now = 0;
    int bps = 0;
    ctl.Evaluate(now, bps);
    //两次（例如两个新成员先后加入）：不降
    ctl.OnKeyFrameRequests(1);
    ctl.OnKeyFrameRequests(1);
    now += kBrWindow;
    CHECK_FALSE(ctl.Evaluate(now, bps));
    //三次：降
    ctl.OnKeyFrameRequests(3);
    now += kBrWindow;
    REQUIRE(ctl.Evaluate(now, bps));
    CHECK(bps == kBrAfterOneDrop);
    //非正数忽略
    ctl.OnKeyFrameRequests(0);
    ctl.OnKeyFrameRequests(-5);
    ctl.OnFrameSent(-100);
    now += kBrWindow;
    CHECK_FALSE(ctl.Evaluate(now, bps));
}

TEST_CASE("SSBR7 回执延迟：周期内最大延迟超过门限算拥塞，超过 3 秒没有回执也算，从不调用时行为不变", "[sharebitrate]")
{
    //远程桌面用（docs/远程桌面H264与共享画质提升方案.md 4.5）：控制端每 500 毫秒回送一次发送时刻
    const int64_t kAckEveryMs = 500;
    ShareBitrateController ctl(kBrMax, kBrMin);
    int64_t now = 0;
    int bps = 0;
    ctl.Evaluate(now, bps);
    //延迟都低于门限：不拥塞；也不影响原有两种信号（这里都没有）
    for (int64_t t = kAckEveryMs; t <= kBrWindow; t += kAckEveryMs)
        ctl.OnFeedbackDelay(kShareFeedbackCongestedDelayMs - 1, now + t);
    now += kBrWindow;
    CHECK_FALSE(ctl.Evaluate(now, bps));
    //一个周期里有一次超过门限：拥塞，降到 70%
    ctl.OnFeedbackDelay(100, now + kAckEveryMs);
    ctl.OnFeedbackDelay(kShareFeedbackCongestedDelayMs + 1, now + 2 * kAckEveryMs);
    now += kBrWindow;
    REQUIRE(ctl.Evaluate(now, bps));
    CHECK(bps == kBrAfterOneDrop);
    //回执中断：距最近一次回执超过 3 秒的周期算拥塞
    const int64_t lastAck = now;
    ctl.OnFeedbackDelay(100, lastAck);
    now += kBrWindow;
    CHECK_FALSE(ctl.Evaluate(now, bps));
    //超时之后再过一个完整的评估周期，保证这一次确实评估
    now = lastAck + kShareFeedbackTimeoutMs + kBrWindow;
    REQUIRE(ctl.Evaluate(now, bps));
    CHECK(bps < kBrAfterOneDrop);

    //从不调用 OnFeedbackDelay（屏幕共享）：长时间没有回执也不算拥塞，与原来完全相同
    ShareBitrateController share(kBrMax, kBrMin);
    int64_t t2 = 0;
    share.Evaluate(t2, bps);
    for (int i = 0; i < 5; ++i)
        CHECK_FALSE(brCalmWindow(share, t2, bps));
    CHECK(share.CurrentBps() == kBrMax);
}

TEST_CASE("SSBR6 SetRange 换码率范围：当前码率取给定值，之后的升降以新范围为准", "[sharebitrate]")
{
    //换到 2K 的范围（3.6 Mbps ~ 0.5 Mbps），当前码率按比例换算后由调用方给出
    const int kNewMax = 3600000;
    const int kNewMin = 500000;
    const int kNewCurrent = 2520000;
    ShareBitrateController ctl(kBrMax, kBrMin);
    int64_t now = 0;
    int bps = 0;
    ctl.Evaluate(now, bps);
    //先攒两个不拥塞的周期，验证换范围后计数清零
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
    ctl.SetRange(kNewMax, kNewMin, kNewCurrent);
    CHECK(ctl.CurrentBps() == kNewCurrent);
    CHECK(ctl.MaxBps() == kNewMax);
    CHECK(ctl.MinBps() == kNewMin);
    //连续不拥塞计数清零：要再过 3 个周期才回升，每步加新上限的 10%
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
    CHECK_FALSE(brCalmWindow(ctl, now, bps));
    REQUIRE(brCalmWindow(ctl, now, bps));
    CHECK(bps == kNewCurrent + kNewMax / 10);
    //一直不拥塞：回到新上限
    for (int i = 0; i < 20; ++i)
        brCalmWindow(ctl, now, bps);
    CHECK(ctl.CurrentBps() == kNewMax);
    //反复拥塞：停在新下限
    for (int i = 0; i < 20; ++i)
        brCongestedWindow(ctl, now, bps);
    CHECK(ctl.CurrentBps() == kNewMin);
    //给定的当前码率超出范围：夹到范围内；下限大于上限时按上限
    ctl.SetRange(kBrMax, kBrMin, kNewMax);
    CHECK(ctl.CurrentBps() == kBrMax);
    ctl.SetRange(kBrMax, kBrMin, 1);
    CHECK(ctl.CurrentBps() == kBrMin);
    ctl.SetRange(kBrMin, kBrMax, kBrMax);
    CHECK(ctl.MinBps() == kBrMin);
    CHECK(ctl.CurrentBps() == kBrMin);
}

TEST_CASE("SSBR5 编码器运行中改码率后输出明显变小", "[sharebitrate]")
{
    BrMfScope mf;
    //画面尺寸、帧率、每段帧数、GOP（足够长，避免关键帧干扰比较）
    const int kW = 640;
    const int kH = 360;
    const int kFps = 15;
    const int kFrames = 60;
    const int kGop = 1000;
    //改码率后的目标（bit/s）
    const int kLowBps = 200000;
    //每帧时长（100 纳秒）
    const int64_t kFrameHns = 10000000LL / kFps;
    call::MfH264Encoder encoder;
    REQUIRE(encoder.Init(kW, kH, kFps, kBrMax, kGop));
    std::vector<uint8_t> nv12(static_cast<size_t>(kW) * kH * 3 / 2);
    srand(9);
    BrSink high;
    BrSink low;
    int64_t pts = 0;
    int frame = 0;
    for (int i = 0; i < kFrames; ++i)
    {
        brTextureFrame(nv12, kW, kH, frame++);
        encoder.EncodeNv12(&nv12[0], kW, pts, &high);
        pts += kFrameHns;
    }
    REQUIRE(encoder.SetBitrate(kLowBps));
    for (int i = 0; i < kFrames; ++i)
    {
        brTextureFrame(nv12, kW, kH, frame++);
        encoder.EncodeNv12(&nv12[0], kW, pts, &low);
        pts += kFrameHns;
    }
    CAPTURE(high.m_bytes, low.m_bytes);
    REQUIRE(high.m_bytes > 0);
    CHECK(low.m_bytes * 3 < high.m_bytes);
}
