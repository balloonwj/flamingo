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
 *            1080p 都覆盖；并打印两者在 1080p 上的耗时（Debug 版的耗时不代表 Release 版）；
 *    - SSVP8～SSVP13、SSVP16、SSVP17 按源分辨率定编码档位（docs/远程桌面H264与共享画质提升方案.md 4.1，2026-10-15）：
 *            三档清晰度的长边上限、码率按编码像素数换算（不低于 1080p 的基准）、码率范围变化时按比例换算、远程桌面的参数。
 *            SSVP1 里的 kShareMaxLongSide 自此是 1080p 档的长边上限；
 *    - SSVP14 BGRA 缩放（7 位权重定点，先纵后横）的 SSE2 实现与逐像素实现逐字节一致：4K 缩到 2K 与 1080p、奇数尺寸、
 *            放大、行跨度大于宽度；纯色缩放后不变，2 倍缩小时相邻像素取平均；
 *    - SSVP15 NV12→BGRA 的 SSE2 实现（call::Nv12ToBgraSse2）与逐像素实现逐字节一致：宽度不是 8 的倍数、奇数宽度、行跨度大于宽度；
 *            只转左上角的裁剪版本（Nv12ToBgraCropped）与整幅转换后取左上角相同；
 *    - SSVP18、SSVP19 画面被别人控制键鼠期间帧率提到 kShareControlledFps（2026-10-17）：发帧节奏运行中换帧率上限，
 *            编码参数随控制状态变化时只有帧率不同。
 *
 *  典型用法：FlamingoUnit.exe "SSVP*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdlib.h>
#include <string.h>
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

//==================== 按源分辨率定编码档位（docs/远程桌面H264与共享画质提升方案.md 4.1，2026-10-15） ====================

namespace {

//1080p 基准下的码率（bit/s）：共享的最高、最低，远程桌面的最高
const int kSsvpShareBase = 2000000;
const int kSsvpShareFloor = 300000;
const int kSsvpRdBase = 3000000;

//按共享某一档清晰度算一组档位
ScreenVideoProfile ssvpShareProfile(int srcW, int srcH, int quality)
{
    ScreenVideoProfile p;
    ComputeScreenVideoProfile(srcW, srcH, ShareVideoParamsForQuality(quality), p);
    return p;
}

} // namespace

TEST_CASE("SSVP8 1080p 源：编码原尺寸，码率与改动前相同", "[screenshare]")
{
    const int qualities[] = { share_quality_1080p, share_quality_2k, share_quality_4k };
    for (size_t i = 0; i < sizeof(qualities) / sizeof(qualities[0]); ++i)
    {
        CAPTURE(qualities[i]);
        const ScreenVideoProfile p = ssvpShareProfile(1920, 1080, qualities[i]);
        CHECK(p.m_encW == 1920);
        CHECK(p.m_encH == 1080);
        CHECK(p.m_maxBps == kSsvpShareBase);
        CHECK(p.m_minBps == kSsvpShareFloor);
    }
}

TEST_CASE("SSVP9 2K 档下 2560×1440 源不缩小，码率按像素数换算", "[screenshare]")
{
    const ScreenVideoProfile p = ssvpShareProfile(2560, 1440, share_quality_2k);
    CHECK(p.m_encW == 2560);
    CHECK(p.m_encH == 1440);
    //2 Mbps × 2560 × 1440 / (1920 × 1080) ≈ 3.56 Mbps，按 100 kbps 取整为 3.6 Mbps；下限 0.53 Mbps 取整为 0.5 Mbps
    CHECK(p.m_maxBps == 3600000);
    CHECK(p.m_minBps == 500000);
    //1080p 档：缩到 1920×1080，码率与改动前相同
    const ScreenVideoProfile low = ssvpShareProfile(2560, 1440, share_quality_1080p);
    CHECK(low.m_encW == 1920);
    CHECK(low.m_encH == 1080);
    CHECK(low.m_maxBps == kSsvpShareBase);
    CHECK(low.m_minBps == kSsvpShareFloor);
}

TEST_CASE("SSVP10 3840×2160 源：2K 档缩到 2560×1440，4K 档原尺寸编码", "[screenshare]")
{
    const ScreenVideoProfile k2 = ssvpShareProfile(3840, 2160, share_quality_2k);
    CHECK(k2.m_encW == 2560);
    CHECK(k2.m_encH == 1440);
    CHECK(k2.m_maxBps == 3600000);
    const ScreenVideoProfile k4 = ssvpShareProfile(3840, 2160, share_quality_4k);
    CHECK(k4.m_encW == 3840);
    CHECK(k4.m_encH == 2160);
    CHECK(k4.m_maxBps == 8000000);
    CHECK(k4.m_minBps == 1200000);
}

TEST_CASE("SSVP11 小于 1080p 的源编码原尺寸，码率不低于 1080p 的基准", "[screenshare]")
{
    const int sizes[][2] = { { 1280, 720 }, { 800, 600 }, { 2, 2 } };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        CAPTURE(sizes[i][0], sizes[i][1]);
        const ScreenVideoProfile p = ssvpShareProfile(sizes[i][0], sizes[i][1], share_quality_2k);
        CHECK(p.m_encW == sizes[i][0]);
        CHECK(p.m_encH == sizes[i][1]);
        CHECK(p.m_maxBps == kSsvpShareBase);
        CHECK(p.m_minBps == kSsvpShareFloor);
    }
    //非法源：全为 0
    const ScreenVideoProfile bad = ssvpShareProfile(0, 1080, share_quality_2k);
    CHECK(bad.m_encW == 0);
    CHECK(bad.m_encH == 0);
    CHECK(bad.m_maxBps == 0);
    CHECK(bad.m_minBps == 0);
}

TEST_CASE("SSVP12 竖屏按高判断长边，奇数尺寸取偶数", "[screenshare]")
{
    //竖放的 2K 屏：长边 2560 不超过 2K 档的上限，原尺寸编码
    const ScreenVideoProfile tall = ssvpShareProfile(1440, 2560, share_quality_2k);
    CHECK(tall.m_encW == 1440);
    CHECK(tall.m_encH == 2560);
    CHECK(tall.m_maxBps == 3600000);
    //1080p 档：高缩到 1920
    const ScreenVideoProfile tallLow = ssvpShareProfile(1440, 2560, share_quality_1080p);
    CHECK(tallLow.m_encW == 1080);
    CHECK(tallLow.m_encH == 1920);
    const ScreenVideoProfile odd = ssvpShareProfile(643, 481, share_quality_2k);
    CHECK(odd.m_encW == 642);
    CHECK(odd.m_encH == 480);
}

TEST_CASE("SSVP13 码率范围变化时按比例换到新范围", "[screenshare]")
{
    //满码率：换到新上限
    CHECK(ScaleBitrateToRange(2000000, 2000000, 3600000, 500000) == 3600000);
    //一半：换到新上限的一半
    CHECK(ScaleBitrateToRange(1000000, 2000000, 3600000, 500000) == 1800000);
    //换到更小的范围后低于新下限：取新下限
    CHECK(ScaleBitrateToRange(300000, 3600000, 2000000, 300000) == 300000);
    //超过新上限（不应出现，防御）：取新上限
    CHECK(ScaleBitrateToRange(9000000, 2000000, 3600000, 500000) == 3600000);
    //旧上限非法：取新上限
    CHECK(ScaleBitrateToRange(1000000, 0, 3600000, 500000) == 3600000);
    //新下限大于新上限：按新上限
    CHECK(ScaleBitrateToRange(100000, 2000000, 1000000, 2000000) == 1000000);
}

TEST_CASE("SSVP14 BGRA 缩放的 SSE2 实现与逐像素实现逐字节一致", "[screenshare]")
{
    //随机图像的种子，固定以便失败时可以复现
    const unsigned kSeed = 20261015;
    //多出来的行尾字节数：模拟采集画面的行跨度大于宽度 × 4
    const int kStridePad = 20;
    //源宽、源高、目标宽、目标高：4K 缩到 2K 与 1080p、奇数尺寸、放大（覆盖右边缘与下边缘的取样）、宽度不是 2 的倍数的输出
    const int cases[][4] = { { 3840, 2160, 2560, 1440 }, { 3840, 2160, 1920, 1080 }, { 643, 481, 320, 240 },
                             { 37, 23, 18, 11 }, { 100, 50, 150, 75 }, { 5, 3, 3, 2 } };
    srand(kSeed);
    BgraScaler fast;
    BgraScaler reference;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const int srcW = cases[i][0];
        const int srcH = cases[i][1];
        const int dstW = cases[i][2];
        const int dstH = cases[i][3];
        const int stride = srcW * 4 + kStridePad;
        std::vector<uint8_t> src(static_cast<size_t>(stride) * srcH);
        for (size_t k = 0; k < src.size(); ++k)
            src[k] = static_cast<uint8_t>(rand() & 0xFF);
        const size_t dstSize = static_cast<size_t>(dstW) * dstH * 4;
        std::vector<uint8_t> a(dstSize, 0);
        std::vector<uint8_t> b(dstSize, 0);
        fast.ScaleSse2(&src[0], stride, srcW, srcH, &a[0], dstW * 4, dstW, dstH);
        reference.ScaleReference(&src[0], stride, srcW, srcH, &b[0], dstW * 4, dstW, dstH);
        size_t firstDiff = dstSize;
        for (size_t k = 0; k < dstSize; ++k)
        {
            if (a[k] != b[k])
            {
                firstDiff = k;
                break;
            }
        }
        CAPTURE(srcW, srcH, dstW, dstH, firstDiff);
        CHECK(firstDiff == dstSize);
    }

    //纯色缩放后颜色不变；2 倍缩小时相邻两个像素取平均
    std::vector<uint8_t> solid = ssvpSolid(64, 32, 200, 100, 50);
    std::vector<uint8_t> out(static_cast<size_t>(32) * 16 * 4);
    reference.ScaleReference(&solid[0], 64 * 4, 64, 32, &out[0], 32 * 4, 32, 16);
    CHECK(out[0] == 50);
    CHECK(out[1] == 100);
    CHECK(out[2] == 200);
    std::vector<uint8_t> pair(static_cast<size_t>(2) * 2 * 4, 0);
    for (int y = 0; y < 2; ++y)
    {
        pair[static_cast<size_t>(y) * 8 + 0] = 10;
        pair[static_cast<size_t>(y) * 8 + 4] = 30;
    }
    std::vector<uint8_t> half(4, 0);
    reference.ScaleReference(&pair[0], 8, 2, 2, &half[0], 4, 1, 1);
    CHECK(half[0] == 20);
}

TEST_CASE("SSVP15 NV12→BGRA 的 SSE2 实现与逐像素实现逐字节一致", "[screenshare]")
{
    //随机数据的种子，固定以便失败时可以复现
    const unsigned kSeed = 20261016;
    //多出来的行尾字节数：解码器输出的行跨度常大于宽度
    const int kStridePad = 32;
    //宽、高：最小、宽度不是 8 的倍数、奇数宽度（行尾单独处理）、1080p
    const int sizes[][2] = { { 2, 2 }, { 8, 2 }, { 14, 6 }, { 13, 7 }, { 1366, 768 }, { 1920, 1080 } };
    srand(kSeed);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        const int w = sizes[i][0];
        const int h = sizes[i][1];
        const int stride = w + kStridePad;
        //Y 平面 h 行，UV 平面 (h + 1) / 2 行，行跨度相同
        std::vector<uint8_t> nv12(static_cast<size_t>(stride) * (h + (h + 1) / 2));
        for (size_t k = 0; k < nv12.size(); ++k)
            nv12[k] = static_cast<uint8_t>(rand() & 0xFF);
        const size_t outSize = static_cast<size_t>(w) * h * 4;
        std::vector<uint8_t> fast(outSize, 0);
        std::vector<uint8_t> reference(outSize, 0);
        call::Nv12ToBgraSse2(&nv12[0], w, h, stride, &fast[0], w * 4);
        call::Nv12ToBgraReference(&nv12[0], w, h, stride, &reference[0], w * 4);
        size_t firstDiff = outSize;
        for (size_t k = 0; k < outSize; ++k)
        {
            if (fast[k] != reference[k])
            {
                firstDiff = k;
                break;
            }
        }
        CAPTURE(w, h, firstDiff);
        CHECK(firstDiff == outSize);
    }

    //裁剪版本（2026-10-15）：解码器按 16 对齐把 1366×768 解成 1376×776，只转左上角 1366×768，
    //结果与整幅转换后取左上角逐字节相同
    const int kPlaneW = 1376;
    const int kPlaneH = 776;
    const int kCropW = 1366;
    const int kCropH = 768;
    const int planeStride = kPlaneW + kStridePad;
    std::vector<uint8_t> padded(static_cast<size_t>(planeStride) * (kPlaneH + kPlaneH / 2));
    for (size_t k = 0; k < padded.size(); ++k)
        padded[k] = static_cast<uint8_t>(rand() & 0xFF);
    std::vector<uint8_t> whole(static_cast<size_t>(kPlaneW) * kPlaneH * 4, 0);
    call::Nv12ToBgraReference(&padded[0], kPlaneW, kPlaneH, planeStride, &whole[0], kPlaneW * 4);
    std::vector<uint8_t> cropped(static_cast<size_t>(kCropW) * kCropH * 4, 0);
    call::Nv12ToBgraCropped(&padded[0], kPlaneH, planeStride, kCropW, kCropH, &cropped[0], kCropW * 4);
    bool same = true;
    for (int y = 0; y < kCropH && same; ++y)
    {
        same = memcmp(&cropped[static_cast<size_t>(y) * kCropW * 4], &whole[static_cast<size_t>(y) * kPlaneW * 4],
                      static_cast<size_t>(kCropW) * 4) == 0;
    }
    CHECK(same);
}

TEST_CASE("SSVP16 清晰度档位对应的长边上限，未知取值按 2K 档", "[screenshare]")
{
    CHECK(ShareMaxLongSideForQuality(share_quality_1080p) == 1920);
    CHECK(ShareMaxLongSideForQuality(share_quality_2k) == 2560);
    CHECK(ShareMaxLongSideForQuality(share_quality_4k) == 3840);
    CHECK(ShareMaxLongSideForQuality(-1) == 2560);
    CHECK(ShareMaxLongSideForQuality(99) == 2560);
    CHECK(kShareQualityDefault == share_quality_2k);
    const ScreenVideoParams params = ShareVideoParamsForQuality(share_quality_4k);
    CHECK(params.m_maxLongSide == 3840);
    CHECK(params.m_fps == kShareMaxFps);
    CHECK(params.m_baseBps == kSsvpShareBase);
    CHECK(params.m_floorBps == kSsvpShareFloor);
}

TEST_CASE("SSVP17 远程桌面的编码参数：长边 2560、30 fps、1080p 基准 3 Mbps", "[screenshare]")
{
    const ScreenVideoParams rd = RdScreenVideoParams();
    CHECK(rd.m_maxLongSide == 2560);
    CHECK(rd.m_fps == 30);
    CHECK(rd.m_baseBps == kSsvpRdBase);
    CHECK(rd.m_floorBps == kSsvpShareFloor);
    ScreenVideoProfile p;
    ComputeScreenVideoProfile(1920, 1080, rd, p);
    CHECK(p.m_maxBps == kSsvpRdBase);
    ComputeScreenVideoProfile(2560, 1440, rd, p);
    CHECK(p.m_encW == 2560);
    //3 Mbps × 1.78 ≈ 5.33 Mbps，取整为 5.3 Mbps
    CHECK(p.m_maxBps == 5300000);
    CHECK(p.m_minBps == 500000);
    ComputeScreenVideoProfile(3840, 2160, rd, p);
    CHECK(p.m_encW == 2560);
    CHECK(p.m_encH == 1440);
}

TEST_CASE("SSVP18 发帧节奏运行中换帧率上限：提到 30 帧后间隔缩短，改回 15 帧恢复，兜底关键帧计时不受影响", "[screenshare]")
{
    ShareFramePacer pacer(kShareMaxFps, kShareKeyFrameIntervalMs);
    const int64_t slow = pacer.minIntervalMs();
    int64_t t = 1000;
    pacer.onEncoded(t, true);

    //被控制：上限提到 kShareControlledFps，间隔缩短到 1000 / 30
    pacer.SetMaxFps(kShareControlledFps);
    const int64_t fast = pacer.minIntervalMs();
    CHECK(fast == 1000 / kShareControlledFps);
    CHECK(fast < slow);
    //距上次编码刚满新间隔的新画面：编（15 帧时这个间隔还编不了）
    ShareFramePacer::Decision d = pacer.decide(t + fast, true, false, true);
    CHECK(d.m_encode);
    CHECK_FALSE(d.m_keyframe);
    t += fast;
    pacer.onEncoded(t, false);
    //不到新间隔：仍不编
    d = pacer.decide(t + fast - 1, true, false, true);
    CHECK_FALSE(d.m_encode);

    //控制结束：改回 kShareMaxFps，原来的间隔恢复
    pacer.SetMaxFps(kShareMaxFps);
    CHECK(pacer.minIntervalMs() == slow);
    d = pacer.decide(t + fast, true, false, true);
    CHECK_FALSE(d.m_encode);
    d = pacer.decide(t + slow, true, false, true);
    CHECK(d.m_encode);

    //换帧率不重置兜底关键帧的计时：距首帧（关键帧）满 4 秒的新画面仍编成关键帧
    pacer.SetMaxFps(kShareControlledFps);
    d = pacer.decide(1000 + kShareKeyFrameIntervalMs, true, false, true);
    CHECK(d.m_encode);
    CHECK(d.m_keyframe);

    //参数非法时按 kShareMaxFps
    pacer.SetMaxFps(0);
    CHECK(pacer.minIntervalMs() == 1000 / kShareMaxFps);
    pacer.SetMaxFps(-5);
    CHECK(pacer.minIntervalMs() == 1000 / kShareMaxFps);
}

TEST_CASE("SSVP19 屏幕共享的编码参数随控制状态变化：被控制时只有帧率变成 30，长边与码率不变", "[screenshare]")
{
    //被控制期间的帧率与远程桌面相同
    CHECK(kShareControlledFps == RdScreenVideoParams().m_fps);
    const int qualities[] = { share_quality_1080p, share_quality_2k, share_quality_4k, -1 };
    for (size_t i = 0; i < sizeof(qualities) / sizeof(qualities[0]); ++i)
    {
        const ScreenVideoParams base = ShareVideoParamsForQuality(qualities[i]);
        //没被控制：与 ShareVideoParamsForQuality 完全相同
        const ScreenVideoParams idle = ShareVideoParamsForControl(qualities[i], false);
        CHECK(idle.m_maxLongSide == base.m_maxLongSide);
        CHECK(idle.m_fps == kShareMaxFps);
        CHECK(idle.m_baseBps == base.m_baseBps);
        CHECK(idle.m_floorBps == base.m_floorBps);
        //被控制：只有帧率不同
        const ScreenVideoParams ctl = ShareVideoParamsForControl(qualities[i], true);
        CHECK(ctl.m_maxLongSide == base.m_maxLongSide);
        CHECK(ctl.m_fps == kShareControlledFps);
        CHECK(ctl.m_baseBps == base.m_baseBps);
        CHECK(ctl.m_floorBps == base.m_floorBps);
    }
}
