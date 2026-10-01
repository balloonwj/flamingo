/**
 *  NinePatchOrientationTests.cpp
 *
 *  balloonui 的 DuiNinePatch::Draw 画位图时的上下方向（标签 [ninepatch]）。修复前它按 GetObject
 *  返回的 biHeight 正负推测位图行序，而 GetObject 对自上而下存储的位图同样返回正值，于是自上而下
 *  存储的 32 位 DIB 都被画成上下颠倒（2026-10-01 修复，改为用 GetDIBits 按自上而下的行序读像素）。
 *
 *    NPO1 源图为自上而下存储的 4×4 DIB，上半红、下半蓝，零内距整张拉伸到一张同样自上而下存储的
 *         40×40 目标 DIB 上，然后<u>直接按内存行序</u>读目标像素（自上而下存储的 DIB，内存第 0 行
 *         就是最上面一行），不经过任何按 biHeight 的换算。不能借用 balloonui 自带
 *         DuiNinePatchTests 原先的取像素写法：它也按 biHeight 翻转行号，与绘制时的翻转相互抵消，
 *         测不出这个问题。修复前本用例失败（上方取到蓝、下方取到红），修复后通过。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <gdiplus.h>

#include "../../../third-party/balloonui/balloonui/DuiNinePatch.h"

namespace {

// 源图与目标图的边长（像素）
const int kSrcSide = 4;
const int kDstSide = 40;
// 目标图上取样的两行：靠上的第 5 行、靠下的倒数第 5 行，避开拉伸插值的过渡带
const int kTopSampleRow    = 5;
const int kBottomSampleRow = kDstSide - 1 - 5;
// 取样列：横向正中
const int kSampleCol = kDstSide / 2;

// 32 位像素在内存里的字节顺序（BGRA）
struct Bgra
{
    BYTE b;
    BYTE g;
    BYTE r;
    BYTE a;
};

// 在作用域内启动 GDI+，离开时关闭（DuiNinePatch 的 32 位路径用 GDI+ 绘制）。
class ScopedGdiplusForNinePatch
{
public:
    ScopedGdiplusForNinePatch()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~ScopedGdiplusForNinePatch()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
    bool      m_ok;      //是否启动成功
};

// 建一张自上而下存储（biHeight 为负）的 32 位 DIB。bits 为像素指针，内存第 0 行即最上面一行。
HBITMAP MakeTopDownDib(int side, void** bits)
{
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = side;
    bi.bmiHeader.biHeight      = -side;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, bits, NULL, 0);
}

// GetObject 读回的 biHeight；读不到 DIBSECTION 时返回 0。
LONG QueriedBiHeight(HBITMAP hbm)
{
    DIBSECTION ds;
    ::ZeroMemory(&ds, sizeof(ds));
    if (::GetObject(hbm, sizeof(ds), &ds) != sizeof(ds))
    {
        return 0;
    }
    return ds.dsBmih.biHeight;
}

} // anonymous namespace

TEST_CASE("NPO1 DuiNinePatch 画自上而下存储的位图时方向正确", "[ninepatch]")
{
    ScopedGdiplusForNinePatch gdiplus;
    REQUIRE(gdiplus.Ok());

    // 源图：上半红、下半蓝（按内存行序写，自上而下存储时第 0 行是最上面）
    void* srcBits = NULL;
    HBITMAP src = MakeTopDownDib(kSrcSide, &srcBits);
    REQUIRE(src != NULL);
    REQUIRE(srcBits != NULL);
    Bgra* sp = (Bgra*)srcBits;
    for (int y = 0; y < kSrcSide; ++y)
    {
        for (int x = 0; x < kSrcSide; ++x)
        {
            Bgra& p = sp[y * kSrcSide + x];
            const bool top = (y < kSrcSide / 2);
            p.r = top ? 255 : 0;
            p.g = 0;
            p.b = top ? 0 : 255;
            p.a = 255;
        }
    }

    // 目标图：同样自上而下存储，先铺白
    void* dstBits = NULL;
    HBITMAP dst = MakeTopDownDib(kDstSide, &dstBits);
    REQUIRE(dst != NULL);
    REQUIRE(dstBits != NULL);
    HDC memDC = ::CreateCompatibleDC(NULL);
    REQUIRE(memDC != NULL);
    HGDIOBJ old = ::SelectObject(memDC, dst);
    RECT rc = { 0, 0, kDstSide, kDstSide };
    HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(memDC, &rc, white);
    ::DeleteObject(white);

    // 零内距 = 整张拉伸；不透明画法，免得透明度混合干扰取色
    balloonwjui::DuiNinePatch::Insets ins;
    balloonwjui::DuiNinePatch::Options opts;
    opts.hasAlpha = false;
    REQUIRE(balloonwjui::DuiNinePatch::Draw(memDC, src, rc, ins, opts));
    ::GdiFlush();

    // 直接按内存行序取样：自上而下存储的目标图，内存第 kTopSampleRow 行就在上方
    const Bgra* dp = (const Bgra*)dstBits;
    const Bgra topPx    = dp[kTopSampleRow * kDstSide + kSampleCol];
    const Bgra bottomPx = dp[kBottomSampleRow * kDstSide + kSampleCol];

    INFO("GetObject biHeight: src=" << QueriedBiHeight(src) << " dst=" << QueriedBiHeight(dst)
         << " (both were created with a negative height)");
    INFO("top sample rgb=(" << (int)topPx.r << "," << (int)topPx.g << "," << (int)topPx.b
         << "), bottom sample rgb=(" << (int)bottomPx.r << "," << (int)bottomPx.g << ","
         << (int)bottomPx.b << ")");
    // 正确的结果：上方是红、下方是蓝
    CHECK(topPx.r > 200);
    CHECK(topPx.b < 50);
    CHECK(bottomPx.b > 200);
    CHECK(bottomPx.r < 50);

    ::SelectObject(memDC, old);
    ::DeleteDC(memDC);
    ::DeleteObject(dst);
    ::DeleteObject(src);
}
