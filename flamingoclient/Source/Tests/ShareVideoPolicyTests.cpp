/**
 *  屏幕共享画面编码纯逻辑的单元测试（Suite SSVP，标签 [screenshare]），ShareVideoPolicyTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareVideoPolicy（docs/屏幕共享与录制方案.md 第 11.4 节 2e）：
 *    - SSVP1 编码尺寸：长边不超过 1920、保持宽高比、宽高取偶数、最小 2×2，非法尺寸为 0×0；
 *    - SSVP2 发帧节奏：首帧编成关键帧；帧率上限内多余的新画面不编；画面没变不编；满 4 秒兜底关键帧；
 *            关键帧请求立即编（不受帧率上限约束、画面没变也编最近一帧），手里没有画面时等下一帧；
 *    - SSVP3 颜色转换：纯色的 Y / U / V 取值符合 BT.601 limited range；与 call::Nv12ToBgra 往返误差很小；
 *    - SSVP4 缩放：2 倍缩小时相邻像素按双线性取均值，左右两半不同颜色的画面缩小后仍分在左右两半；
 *    - SSVP7 SSE2 的颜色转换（第 9 期 9a）与逐像素的参考实现逐字节相同：宽度不是 4 的倍数、行跨度大于宽度、
 *            1080p 都覆盖；并打印两者在 1080p 上的耗时（Debug 版的耗时不代表 Release 版）。
 *
 *  典型用法：FlamingoUnit.exe "SSVP*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdlib.h>
#include <windows.h>
#include <vector>
#include "../../FlamingoMedia/Share/ShareVideoPolicy.h"
#include "../../FlamingoMedia/Call/VideoFrameConvert.h"

using namespace screenshare;

namespace {

//颜色与往返比对的容差（每通道）
const int kSsvpTol = 3;

//造一张纯色的 BGRA 画面
std::vector<uint8_t> ssvpSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.size(); i += 4)
    {
        img[i] = b;
        img[i + 1] = g;
        img[i + 2] = r;
        img[i + 3] = 255;
    }
    return img;
}

bool ssvpNear(int a, int b, int tol)
{
    return abs(a - b) <= tol;
}

} // namespace

TEST_CASE("SSVP1 编码尺寸：长边不超过 1920、保持宽高比、取偶数", "[screenshare]")
{
    int w = 0;
    int h = 0;
    ComputeShareEncodeSize(1920, 1080, kShareMaxLongSide, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    ComputeShareEncodeSize(2560, 1440, kShareMaxLongSide, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    ComputeShareEncodeSize(3840, 2160, kShareMaxLongSide, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    //竖屏：高是长边
    ComputeShareEncodeSize(1440, 2560, kShareMaxLongSide, w, h);
    CHECK(h == 1920);
    CHECK(w == 1080);
    //奇数尺寸的窗口：向下取偶数
    ComputeShareEncodeSize(643, 481, kShareMaxLongSide, w, h);
    CHECK(w == 642);
    CHECK(h == 480);
    //缩放后为奇数也取偶数：3000×1001 → 1920×640.6 → 1920×640
    ComputeShareEncodeSize(3000, 1001, kShareMaxLongSide, w, h);
    CHECK(w == 1920);
    CHECK(h % 2 == 0);
    CHECK(h == 640);
    //极小
    ComputeShareEncodeSize(1, 1, kShareMaxLongSide, w, h);
    CHECK(w == 2);
    CHECK(h == 2);
    //不限长边
    ComputeShareEncodeSize(2560, 1440, 0, w, h);
    CHECK(w == 2560);
    CHECK(h == 1440);
    //非法
    ComputeShareEncodeSize(0, 1080, kShareMaxLongSide, w, h);
    CHECK(w == 0);
    CHECK(h == 0);
}

TEST_CASE("SSVP2 发帧节奏：首帧关键帧、帧率上限、画面不变不编、兜底关键帧、关键帧请求", "[screenshare]")
{
    ShareFramePacer pacer(kShareMaxFps, kShareKeyFrameIntervalMs);
    const int64_t interval = pacer.minIntervalMs();
    CHECK(interval == 1000 / kShareMaxFps);
    int64_t t = 1000;

    //还没有画面：什么也不做
    ShareFramePacer::Decision d = pacer.decide(t, false, false, false);
    CHECK_FALSE(d.m_encode);

    //首帧：编，且是关键帧
    d = pacer.decide(t, true, false, false);
    CHECK(d.m_encode);
    CHECK(d.m_keyframe);
    pacer.onEncoded(t, true);

    //帧率上限内又来了新画面：不编
    d = pacer.decide(t + interval / 2, true, false, true);
    CHECK_FALSE(d.m_encode);
    //间隔够了：编，不是关键帧
    t += interval;
    d = pacer.decide(t, true, false, true);
    CHECK(d.m_encode);
    CHECK_FALSE(d.m_keyframe);
    pacer.onEncoded(t, false);

    //画面没变：不编
    d = pacer.decide(t + interval * 10, false, false, true);
    CHECK_FALSE(d.m_encode);

    //距上次关键帧满 4 秒、又有新画面：编成关键帧
    const int64_t afterKeyInterval = 1000 + kShareKeyFrameIntervalMs;
    d = pacer.decide(afterKeyInterval, true, false, true);
    CHECK(d.m_encode);
    CHECK(d.m_keyframe);
    pacer.onEncoded(afterKeyInterval, true);

    //关键帧请求：刚编过也立即编（不受帧率上限约束），画面没变就编最近一帧
    d = pacer.decide(afterKeyInterval + 1, false, true, true);
    CHECK(d.m_encode);
    CHECK(d.m_keyframe);
    pacer.onEncoded(afterKeyInterval + 1, true);

    //关键帧请求但手里没有画面：不编，等下一帧
    ShareFramePacer fresh(kShareMaxFps, kShareKeyFrameIntervalMs);
    d = fresh.decide(t, false, true, false);
    CHECK_FALSE(d.m_encode);
    d = fresh.decide(t, true, true, false);
    CHECK(d.m_encode);
    CHECK(d.m_keyframe);

    //参数非法时用默认值
    ShareFramePacer defaults(0, 0);
    CHECK(defaults.minIntervalMs() == 1000 / kShareMaxFps);
}

TEST_CASE("SSVP3 颜色转换：纯色取值符合 BT.601 limited range，与 Nv12ToBgra 往返误差很小", "[screenshare]")
{
    const int w = 4;
    const int h = 4;
    std::vector<uint8_t> nv12(static_cast<size_t>(w) * h * 3 / 2);

    struct Case
    {
        uint8_t r, g, b;   //源颜色
        int y, u, v;       //期望的 Y / U / V
    };
    const Case cases[] =
    {
        { 255, 255, 255, 235, 128, 128 },   //白
        { 0, 0, 0, 16, 128, 128 },          //黑
        { 255, 0, 0, 82, 90, 240 },         //红
        { 0, 255, 0, 145, 54, 34 },         //绿
        { 0, 0, 255, 41, 240, 110 },        //蓝
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const Case& c = cases[i];
        std::vector<uint8_t> img = ssvpSolid(w, h, c.r, c.g, c.b);
        ConvertBgraToNv12(img.data(), w * 4, w, h, nv12.data(), w, h);
        INFO("case " << i);
        CHECK(ssvpNear(nv12[0], c.y, kSsvpTol));
        CHECK(ssvpNear(nv12[static_cast<size_t>(w) * h], c.u, kSsvpTol));
        CHECK(ssvpNear(nv12[static_cast<size_t>(w) * h + 1], c.v, kSsvpTol));

        //往返：NV12 → BGRA 应接近原色
        std::vector<uint8_t> back(static_cast<size_t>(w) * h * 4);
        call::Nv12ToBgra(nv12.data(), w, h, w, back.data(), w * 4);
        CHECK(ssvpNear(back[0], c.b, kSsvpTol));
        CHECK(ssvpNear(back[1], c.g, kSsvpTol));
        CHECK(ssvpNear(back[2], c.r, kSsvpTol));
    }
}

TEST_CASE("SSVP4 缩放：缩小后左右两半的颜色仍分在左右两半", "[screenshare]")
{
    //8×4：左半红、右半蓝，缩成 4×2
    const int sw = 8;
    const int sh = 4;
    std::vector<uint8_t> img = ssvpSolid(sw, sh, 255, 0, 0);
    for (int y = 0; y < sh; ++y)
    {
        for (int x = sw / 2; x < sw; ++x)
        {
            uint8_t* p = &img[(static_cast<size_t>(y) * sw + x) * 4];
            p[0] = 255;
            p[1] = 0;
            p[2] = 0;
        }
    }
    const int dw = 4;
    const int dh = 2;
    std::vector<uint8_t> nv12(static_cast<size_t>(dw) * dh * 3 / 2);
    ConvertBgraToNv12(img.data(), sw * 4, sw, sh, nv12.data(), dw, dh);
    std::vector<uint8_t> back(static_cast<size_t>(dw) * dh * 4);
    call::Nv12ToBgra(nv12.data(), dw, dh, dw, back.data(), dw * 4);
    //最左一列偏红、最右一列偏蓝
    const uint8_t* left = &back[0];
    const uint8_t* right = &back[(dw - 1) * 4];
    CHECK(left[2] > left[0]);
    CHECK(right[0] > right[2]);

    //2 倍缩小的纯色画面：颜色不变
    std::vector<uint8_t> gray = ssvpSolid(sw, sh, 128, 128, 128);
    ConvertBgraToNv12(gray.data(), sw * 4, sw, sh, nv12.data(), dw, dh);
    for (int i = 0; i < dw * dh; ++i)
        CHECK(ssvpNear(nv12[i], 126, kSsvpTol));
}

TEST_CASE("SSVP5 摄像头编码尺寸：缩进 640×360 的框内、只缩不放、取偶数", "[screenshare]")
{
    int w = 0;
    int h = 0;
    ComputeCameraEncodeSize(1280, 720, w, h);
    CHECK(w == 640);
    CHECK(h == 360);
    ComputeCameraEncodeSize(640, 480, w, h);
    CHECK(w == 480);
    CHECK(h == 360);
    ComputeCameraEncodeSize(1920, 1080, w, h);
    CHECK(w == 640);
    CHECK(h == 360);
    //小于框的不放大
    ComputeCameraEncodeSize(320, 240, w, h);
    CHECK(w == 320);
    CHECK(h == 240);
    //奇数取偶数
    ComputeCameraEncodeSize(321, 241, w, h);
    CHECK(w == 320);
    CHECK(h == 240);
    ComputeCameraEncodeSize(0, 240, w, h);
    CHECK(w == 0);
    CHECK(h == 0);
}

TEST_CASE("SSVP6 NV12 缩放：亮度与色度都按位置取样，尺寸相同时原样拷贝", "[screenshare]")
{
    //源 8×4：左半 Y=50、右半 Y=200；UV 平面左半 (U,V)=(60,70)、右半 (U,V)=(180,190)；行跨度 10（带两字节填充）
    const int srcW = 8;
    const int srcH = 4;
    const int stride = 10;
    std::vector<uint8_t> src(static_cast<size_t>(stride) * srcH * 3 / 2, 0);
    for (int y = 0; y < srcH; ++y)
    {
        for (int x = 0; x < srcW; ++x)
            src[static_cast<size_t>(y) * stride + x] = (x < srcW / 2) ? 50 : 200;
    }
    uint8_t* uv = &src[static_cast<size_t>(stride) * srcH];
    for (int y = 0; y < srcH / 2; ++y)
    {
        for (int x = 0; x < srcW / 2; ++x)
        {
            uv[y * stride + x * 2] = (x < srcW / 4) ? 60 : 180;
            uv[y * stride + x * 2 + 1] = (x < srcW / 4) ? 70 : 190;
        }
    }
    //缩成 4×2
    std::vector<uint8_t> dst(4 * 2 * 3 / 2, 0);
    ScaleNv12(&src[0], stride, srcW, srcH, &dst[0], 4, 2);
    CHECK(dst[0] == 50);
    CHECK(dst[1] == 50);
    CHECK(dst[2] == 200);
    CHECK(dst[3] == 200);
    CHECK(dst[4] == 50);
    CHECK(dst[7] == 200);
    //UV：一行两对
    CHECK(dst[8] == 60);
    CHECK(dst[9] == 70);
    CHECK(dst[10] == 180);
    CHECK(dst[11] == 190);

    //尺寸相同：去掉行填充后原样拷贝
    std::vector<uint8_t> same(static_cast<size_t>(srcW) * srcH * 3 / 2, 0);
    ScaleNv12(&src[0], stride, srcW, srcH, &same[0], srcW, srcH);
    for (int y = 0; y < srcH; ++y)
    {
        for (int x = 0; x < srcW; ++x)
            CHECK(same[static_cast<size_t>(y) * srcW + x] == src[static_cast<size_t>(y) * stride + x]);
    }
    CHECK(same[static_cast<size_t>(srcW) * srcH] == 60);
}

TEST_CASE("SSVP7 SSE2 的颜色转换与参考实现逐字节相同", "[screenshare]")
{
    //随机图像的种子，固定以便失败时可以复现
    const unsigned kSeed = 20261003;
    //多出来的行尾字节数：模拟采集画面的行跨度大于宽度 × 4
    const int kStridePad = 12;
    //各种尺寸：最小 2×2、宽度不是 4 的倍数、1366×768、1080p
    const int sizes[][2] = { { 2, 2 }, { 6, 4 }, { 34, 10 }, { 1366, 768 }, { 1920, 1080 } };
    srand(kSeed);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        const int w = sizes[i][0];
        const int h = sizes[i][1];
        const int stride = w * 4 + kStridePad;
        std::vector<uint8_t> src(static_cast<size_t>(stride) * h);
        for (size_t k = 0; k < src.size(); ++k)
            src[k] = static_cast<uint8_t>(rand() & 0xFF);
        const size_t nv12Size = static_cast<size_t>(w) * h * 3 / 2;
        std::vector<uint8_t> fast(nv12Size, 0);
        std::vector<uint8_t> reference(nv12Size, 0);
        ConvertBgraToNv12Sse2(&src[0], stride, w, h, &fast[0], w, h);
        ConvertBgraToNv12Reference(&src[0], stride, w, h, &reference[0], w, h);
        size_t firstDiff = nv12Size;
        for (size_t k = 0; k < nv12Size; ++k)
        {
            if (fast[k] != reference[k])
            {
                firstDiff = k;
                break;
            }
        }
        CAPTURE(w, h, firstDiff);
        CHECK(firstDiff == nv12Size);
    }

    //1080p 上各跑若干遍，打印平均耗时（只作记录，不断言快慢：Debug 版与机器负载都会影响结果）
    const int kBenchW = 1920;
    const int kBenchH = 1080;
    const int kBenchRuns = 10;
    std::vector<uint8_t> img(static_cast<size_t>(kBenchW) * kBenchH * 4);
    for (size_t k = 0; k < img.size(); ++k)
        img[k] = static_cast<uint8_t>(rand() & 0xFF);
    std::vector<uint8_t> out(static_cast<size_t>(kBenchW) * kBenchH * 3 / 2);
    LARGE_INTEGER freq;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    LARGE_INTEGER t2;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&t0);
    for (int r = 0; r < kBenchRuns; ++r)
        ConvertBgraToNv12Reference(&img[0], kBenchW * 4, kBenchW, kBenchH, &out[0], kBenchW, kBenchH);
    ::QueryPerformanceCounter(&t1);
    for (int r = 0; r < kBenchRuns; ++r)
        ConvertBgraToNv12Sse2(&img[0], kBenchW * 4, kBenchW, kBenchH, &out[0], kBenchW, kBenchH);
    ::QueryPerformanceCounter(&t2);
    //每毫秒对应的计数
    const double ticksPerMs = static_cast<double>(freq.QuadPart) / 1000.0;
    const double refMs = static_cast<double>(t1.QuadPart - t0.QuadPart) / ticksPerMs / kBenchRuns;
    const double fastMs = static_cast<double>(t2.QuadPart - t1.QuadPart) / ticksPerMs / kBenchRuns;
    WARN("1080p BGRA->NV12: reference " << refMs << " ms, sse2 " << fastMs << " ms per frame");
}
