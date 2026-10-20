/**
 *  观看窗的 BGRA 绘制器（标签 [screenshare]），ShareFrameDrawerTests.cpp
 *
 *  背景：docs/远程桌面H264与共享画质提升方案.md A7（2026-10-15）。观看窗原来一律用 HALFTONE 的 StretchDIBits 缩放，2K、4K 画面每次
 *  要 30～50 毫秒；ShareFrameDrawer 在缩小且不小于一半时先用 BgraScaler 缩好再原样贴上。
 *    - SSFD1 判断规则：两个方向都缩小（不是原尺寸）就走快速缩放；放大、原尺寸、尺寸不合法都不走；缩小超过 2 倍时先逐次减半的
 *            次数；与云端合成的 CloudFitUsesFastScale 是同一条规则；
 *    - SSFD2 在内存位图上实画：快速缩放路径画出的像素与 BgraScaler 直接缩放的结果逐字节一致（只比较 B、G、R），目标矩形外不动；
 *            原尺寸路径与源一致（含源行跨度大于宽度）；放大走 GDI；画面不合法时什么都不画；
 *    - SSFD3 缩小超过 2 倍时先逐次减半（2×2 平均）：周期 4 像素的黑白竖条缩小 4 倍得到均匀的灰；只做一次双线性则只取到白列
 *            （对照）；比例不超过 2 时与 BgraScaler 逐字节一致。
 *  框架 Catch2 v3。耗时对比见 ShareVideoPerfTests.cpp 的 SSPERF2（隐藏）。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../../FlamingoMedia/Share/ShareFrameDrawer.h"
#include "../../FlamingoMedia/Share/ShareVideoPolicy.h"
#include "../../FlamingoMedia/Record/CloudRecordCompose.h"

using namespace screenshare;

namespace {

//每像素字节数（BGRA）
const int kSfdBpp = 4;
//内存位图的尺寸与底色
const int kSfdCanvasW = 64;
const int kSfdCanvasH = 48;
const uint8_t kSfdFill = 0x11;
//源画面的尺寸
const int kSfdSrcW = 40;
const int kSfdSrcH = 30;

//一块 32 位自上而下的内存位图
class SfdCanvas
{
public:
    SfdCanvas()
        : m_dc(::CreateCompatibleDC(NULL))
        , m_bitmap(NULL)
        , m_old(NULL)
        , m_bits(NULL)
    {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kSfdCanvasW;
        bi.bmiHeader.biHeight = -kSfdCanvasH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        m_bitmap = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        m_bits = static_cast<uint8_t*>(bits);
        if (m_dc != NULL && m_bitmap != NULL)
            m_old = ::SelectObject(m_dc, m_bitmap);
        Reset();
    }

    ~SfdCanvas()
    {
        if (m_dc != NULL)
        {
            if (m_old != NULL)
                ::SelectObject(m_dc, m_old);
            ::DeleteDC(m_dc);
        }
        if (m_bitmap != NULL)
            ::DeleteObject(m_bitmap);
    }

    bool Ok() const
    {
        return m_dc != NULL && m_bits != NULL;
    }

    //全部填成底色
    void Reset()
    {
        if (m_bits != NULL)
            memset(m_bits, kSfdFill, static_cast<size_t>(kSfdCanvasW) * kSfdCanvasH * kSfdBpp);
    }

    HDC Dc() const
    {
        return m_dc;
    }

    //(x, y) 处像素的第 b 个字节（读之前先 GdiFlush）
    uint8_t At(int x, int y, int b) const
    {
        return m_bits[(static_cast<size_t>(y) * kSfdCanvasW + x) * kSfdBpp + b];
    }

private:
    HDC      m_dc;       //内存设备上下文
    HBITMAP  m_bitmap;   //DIB
    HGDIOBJ  m_old;      //选入之前的位图
    uint8_t* m_bits;     //像素
};

//源画面：每个字节按位置取不同的值，相邻像素有差别，缩放错位能被看出来
std::vector<uint8_t> sfdSource(int w, int h, int stride)
{
    std::vector<uint8_t> src(static_cast<size_t>(stride) * h, 0);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w * kSfdBpp; ++x)
            src[static_cast<size_t>(y) * stride + x] = static_cast<uint8_t>((x * 7 + y * 13) & 0xFF);
    }
    return src;
}

//画布上 rc 区域的 B、G、R 是否与 expect（紧凑排列，宽为 rc 的宽）一致，rc 之外是否仍为底色
void sfdCompare(const SfdCanvas& canvas, const RECT& rc, const uint8_t* expect, int expectStride, bool& regionSame,
                bool& outsideUntouched)
{
    const int kColorBytes = 3;
    regionSame = true;
    outsideUntouched = true;
    for (int y = 0; y < kSfdCanvasH; ++y)
    {
        for (int x = 0; x < kSfdCanvasW; ++x)
        {
            const bool inside = x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
            for (int b = 0; b < kColorBytes; ++b)
            {
                const uint8_t got = canvas.At(x, y, b);
                if (inside)
                {
                    const uint8_t want = expect[static_cast<size_t>(y - rc.top) * expectStride + (x - rc.left) * kSfdBpp + b];
                    if (got != want)
                        regionSame = false;
                }
                else if (got != kSfdFill)
                {
                    outsideUntouched = false;
                }
            }
        }
    }
}

} // namespace

TEST_CASE("SSFD1 观看窗与云端合成共用的快速缩放判断：两个方向都缩小（不是原尺寸）；超过 2 倍时先逐次减半", "[screenshare]")
{
    CHECK(ShareUsesFastDownscale(2560, 1440, 1920, 1080));
    CHECK(ShareUsesFastDownscale(3840, 2160, 1920, 1080));
    //远程桌面观看窗的默认宽度 1042 显示 2560 宽的画面（约 2.46 倍）、多人共享时 4K 画面缩进四分之一的格子（约 4 倍）
    CHECK(ShareUsesFastDownscale(2560, 1440, 1042, 586));
    CHECK(ShareUsesFastDownscale(3840, 2160, 958, 539));
    //放大、原尺寸、尺寸不合法、一个方向放大
    CHECK_FALSE(ShareUsesFastDownscale(1280, 720, 1920, 1080));
    CHECK_FALSE(ShareUsesFastDownscale(1920, 1080, 1920, 1080));
    CHECK_FALSE(ShareUsesFastDownscale(0, 1080, 1920, 1080));
    CHECK_FALSE(ShareUsesFastDownscale(1920, 1080, 0, 0));
    CHECK_FALSE(ShareUsesFastDownscale(200, 200, 100, 300));

    //需要几次减半：比例不超过 2 时不减半；2.46 倍减 1 次；4 倍减 1 次（减半后正好 2 倍）；4.1 倍减 2 次
    CHECK(BgraDownscaler::HalvingSteps(2560, 1440, 1920, 1080) == 0);
    CHECK(BgraDownscaler::HalvingSteps(3840, 2160, 1920, 1080) == 0);
    CHECK(BgraDownscaler::HalvingSteps(2560, 1440, 1042, 586) == 1);
    CHECK(BgraDownscaler::HalvingSteps(3840, 2160, 960, 540) == 1);
    CHECK(BgraDownscaler::HalvingSteps(3840, 2160, 936, 526) == 2);

    //云端合成用同一条规则
    const RECT full = { 0, 0, 1920, 1080 };
    const RECT quarter = { 0, 0, 958, 539 };
    CHECK(screenrecord::CloudFitUsesFastScale(2560, 1440, full)
          == ShareUsesFastDownscale(2560, 1440, 1920, 1080));
    CHECK(screenrecord::CloudFitUsesFastScale(3840, 2160, quarter)
          == ShareUsesFastDownscale(3840, 2160, 958, 539));
}

TEST_CASE("SSFD3 缩小超过 2 倍时先逐次减半：黑白竖条缩小 4 倍得到均匀的灰，不出现跳像素的结果", "[screenshare]")
{
    //64×64 的黑白竖条，周期 4 像素：每 4 列里中间两列白、两边两列黑（平均亮度 127.5），缩到 16×16。
    //像素中心对齐的双线性缩小 4 倍时只取每个 4×4 块中间的 2×2，正好全落在白列上
    const int srcSide = 64;
    const int dstSide = 16;
    const int kStripePeriod = 4;
    const uint8_t kBlack = 0;
    const uint8_t kWhite = 255;
    std::vector<uint8_t> src(static_cast<size_t>(srcSide) * srcSide * kSfdBpp, 0);
    for (int y = 0; y < srcSide; ++y)
    {
        for (int x = 0; x < srcSide; ++x)
        {
            const int phase = x % kStripePeriod;
            const uint8_t v = (phase == 1 || phase == 2) ? kWhite : kBlack;
            uint8_t* p = &src[(static_cast<size_t>(y) * srcSide + x) * kSfdBpp];
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = kWhite;
        }
    }
    std::vector<uint8_t> out(static_cast<size_t>(dstSide) * dstSide * kSfdBpp, 0);
    BgraDownscaler down;
    down.Scale(&src[0], srcSide * kSfdBpp, srcSide, srcSide, &out[0], dstSide * kSfdBpp, dstSide, dstSide);
    //竖条的平均亮度是 127.5：每个输出像素都应接近它
    const int kGray = 128;
    const int kTolerance = 2;
    int worst = 0;
    for (size_t i = 0; i < out.size(); i += kSfdBpp)
    {
        const int diff = abs(static_cast<int>(out[i]) - kGray);
        if (diff > worst)
            worst = diff;
    }
    CAPTURE(worst);
    CHECK(worst <= kTolerance);

    //对照：只做一次双线性（BgraScaler 直接缩 4 倍）只取到白列，偏差远大于上面的容差
    std::vector<uint8_t> single(out.size(), 0);
    BgraScaler direct;
    direct.Scale(&src[0], srcSide * kSfdBpp, srcSide, srcSide, &single[0], dstSide * kSfdBpp, dstSide, dstSide);
    int singleWorst = 0;
    for (size_t i = 0; i < single.size(); i += kSfdBpp)
    {
        const int diff = abs(static_cast<int>(single[i]) - kGray);
        if (diff > singleWorst)
            singleWorst = diff;
    }
    CAPTURE(singleWorst);
    CHECK(singleWorst > kTolerance);

    //比例不超过 2 时与 BgraScaler 逐字节一致（不减半）
    const int halfSide = 40;
    std::vector<uint8_t> a(static_cast<size_t>(halfSide) * halfSide * kSfdBpp, 0);
    std::vector<uint8_t> b(a.size(), 0);
    down.Scale(&src[0], srcSide * kSfdBpp, srcSide, srcSide, &a[0], halfSide * kSfdBpp, halfSide, halfSide);
    direct.Scale(&src[0], srcSide * kSfdBpp, srcSide, srcSide, &b[0], halfSide * kSfdBpp, halfSide, halfSide);
    CHECK(a == b);
}

TEST_CASE("SSFD2 绘制器在内存位图上的三条路径：快速缩放与 BgraScaler 一致，原尺寸逐字节贴上，放大走 GDI", "[screenshare]")
{
    SfdCanvas canvas;
    REQUIRE(canvas.Ok());
    ShareFrameDrawer drawer;
    const int stride = kSfdSrcW * kSfdBpp;
    const std::vector<uint8_t> src = sfdSource(kSfdSrcW, kSfdSrcH, stride);
    bool regionSame = false;
    bool outsideUntouched = false;

    //缩小到 30×24（0.75、0.8 倍）：快速缩放
    const RECT shrink = { 5, 4, 35, 28 };
    const int shrinkW = shrink.right - shrink.left;
    const int shrinkH = shrink.bottom - shrink.top;
    CHECK(drawer.Draw(canvas.Dc(), shrink, &src[0], kSfdSrcW, kSfdSrcH, stride) == share_draw_fast_scale);
    CHECK(drawer.LastPath() == share_draw_fast_scale);
    ::GdiFlush();
    std::vector<uint8_t> expect(static_cast<size_t>(shrinkW) * shrinkH * kSfdBpp, 0);
    BgraScaler reference;
    reference.Scale(&src[0], stride, kSfdSrcW, kSfdSrcH, &expect[0], shrinkW * kSfdBpp, shrinkW, shrinkH);
    sfdCompare(canvas, shrink, &expect[0], shrinkW * kSfdBpp, regionSame, outsideUntouched);
    CHECK(regionSame);
    CHECK(outsideUntouched);

    //原尺寸，源行跨度大于宽度：逐字节贴上
    canvas.Reset();
    const int wideStride = stride + 3 * kSfdBpp;
    const std::vector<uint8_t> padded = sfdSource(kSfdSrcW, kSfdSrcH, wideStride);
    const RECT same = { 2, 3, 2 + kSfdSrcW, 3 + kSfdSrcH };
    CHECK(drawer.Draw(canvas.Dc(), same, &padded[0], kSfdSrcW, kSfdSrcH, wideStride) == share_draw_copy);
    ::GdiFlush();
    sfdCompare(canvas, same, &padded[0], wideStride, regionSame, outsideUntouched);
    CHECK(regionSame);
    CHECK(outsideUntouched);

    //放大：走 GDI，目标区域被画上了内容
    canvas.Reset();
    const RECT grow = { 0, 0, 60, 45 };
    CHECK(drawer.Draw(canvas.Dc(), grow, &src[0], kSfdSrcW, kSfdSrcH, stride) == share_draw_gdi);
    ::GdiFlush();
    int changed = 0;
    for (int y = grow.top; y < grow.bottom; ++y)
    {
        for (int x = grow.left; x < grow.right; ++x)
            changed += (canvas.At(x, y, 0) != kSfdFill) ? 1 : 0;
    }
    CHECK(changed > 0);

    //画面不合法：什么都不画
    canvas.Reset();
    CHECK(drawer.Draw(canvas.Dc(), shrink, NULL, kSfdSrcW, kSfdSrcH, stride) == share_draw_none);
    const RECT empty = { 5, 5, 5, 5 };
    CHECK(drawer.Draw(canvas.Dc(), empty, &src[0], kSfdSrcW, kSfdSrcH, stride) == share_draw_none);
    ::GdiFlush();
    const RECT nothing = { 0, 0, 0, 0 };
    sfdCompare(canvas, nothing, NULL, 0, regionSame, outsideUntouched);
    CHECK(outsideUntouched);
}
