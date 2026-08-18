/**
 *  AvatarGrayTests.cpp
 *
 *  头像置灰（AvatarGray）的测试：
 *    · MakeGrayColor 的色彩换算 —— 黑白不变、三分量相等、幂等，以及红绿蓝三原色
 *      按 Rec.601 亮度算出的确切灰阶（把公式钉死，改系数会立刻失败）。
 *    · MakeGrayAvatarBitmap 的像素搬运 —— 颜色灰掉但 alpha 原样保留（圆角外必须
 *      继续透明），以及各种非法入参下老实返回 NULL 让调用方退回画彩色图。
 *
 *  用 Catch2 v3（amalgamated）作框架。位图那几条会真的建一张 DIBSection，但不创建
 *  窗口、不进消息循环。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/AvatarGray.h"

using namespace balloonmain;

// =====================================================================
// GRAY1 ~ GRAY3 —— MakeGrayColor 的色彩换算
// =====================================================================

TEST_CASE("GRAY1 纯黑与纯白换算后不变", "[avatargray]")
{
    // 两个端点必须原样保留：白头像不能灰出一层脏，黑头像也不能被抬亮。
    CHECK(MakeGrayColor(RGB(0, 0, 0)) == RGB(0, 0, 0));
    CHECK(MakeGrayColor(RGB(255, 255, 255)) == RGB(255, 255, 255));
}

TEST_CASE("GRAY2 三原色按 Rec.601 得到确切灰阶，且绿最亮蓝最暗", "[avatargray]")
{
    // 系数 306/601/117（>> 10）算出来的确切值。把它写死是有意为之：日后谁改了
    // 亮度公式，这条会失败，逼他确认是不是真想改置灰的观感。
    const COLORREF grayFromRed   = MakeGrayColor(RGB(255, 0, 0));
    const COLORREF grayFromGreen = MakeGrayColor(RGB(0, 255, 0));
    const COLORREF grayFromBlue  = MakeGrayColor(RGB(0, 0, 255));

    CHECK(grayFromRed   == RGB(76, 76, 76));
    CHECK(grayFromGreen == RGB(150, 150, 150));
    CHECK(grayFromBlue  == RGB(29, 29, 29));

    // 人眼对绿最敏感、对蓝最不敏感，灰阶高低必须体现这个次序。
    CHECK(GetRValue(grayFromGreen) > GetRValue(grayFromRed));
    CHECK(GetRValue(grayFromRed)   > GetRValue(grayFromBlue));

    // 换算结果必须是真正的灰 —— 三个分量相等。
    CHECK(GetRValue(grayFromRed) == GetGValue(grayFromRed));
    CHECK(GetGValue(grayFromRed) == GetBValue(grayFromRed));
}

TEST_CASE("GRAY3 换算是幂等的", "[avatargray]")
{
    // 已经灰过的颜色再灰一次不该继续变深，否则重复重绘会让头像越来越黑。
    const COLORREF kSamples[4] =
    {
        RGB(0xFF, 0x7A, 0x45),    // Header 头像的默认橙色底
        RGB(0x22, 0xC5, 0x5E),    // 在线绿
        RGB(0x12, 0x34, 0x56),
        RGB(0x9C, 0xA3, 0xAF),    // 本来就是灰的
    };

    for (int i = 0; i < 4; ++i)
    {
        INFO("sample index = " << i);
        const COLORREF once  = MakeGrayColor(kSamples[i]);
        const COLORREF twice = MakeGrayColor(once);
        CHECK(once == twice);
    }
}

// =====================================================================
// GRAY4 ~ GRAY5 —— MakeGrayAvatarBitmap 的像素搬运与入参兜底
// =====================================================================

namespace {

// 造一张 2×2 的 32bpp 自上而下 DIBSection，像素按 BGRA 顺序逐个填入，
// 与头像位图（AvatarImageUtil.cpp:158-167）同款格式。
// 返回位图句柄，并通过 outBits 带出像素指针；失败返回 NULL。
HBITMAP MakeTestBitmap2x2(const BYTE srcPixels[16], BYTE** outBits)
{
    const int kSide = 2;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kSide;
    bi.bmiHeader.biHeight      = -kSide;   // 负数 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbm == NULL || bits == NULL)
    {
        if (hbm != NULL)
        {
            ::DeleteObject(hbm);
        }
        return NULL;
    }

    ::CopyMemory(bits, srcPixels, (size_t)kSide * kSide * 4);
    *outBits = (BYTE*)bits;
    return hbm;
}

} // anonymous namespace

TEST_CASE("GRAY4 位图颜色灰掉但 alpha 原样保留", "[avatargray][bitmap]")
{
    // 4 个像素，BGRA 顺序。第 4 个 alpha 特意取 0（模拟圆角外的透明区域），
    // 它的颜色分量也非零，用来验证"透明像素也照灰、但不会被填成不透明"。
    const BYTE srcPixels[16] =
    {
        0,   0,   255, 255,   // 纯红，不透明   → 灰 76
        0,   255, 0,   255,   // 纯绿，不透明   → 灰 150
        255, 0,   0,   128,   // 纯蓝，半透明   → 灰 29
        255, 255, 255, 0,     // 纯白，全透明   → 灰 255，alpha 仍为 0
    };

    BYTE*   pSrcBits = NULL;
    HBITMAP hbmSrc = MakeTestBitmap2x2(srcPixels, &pSrcBits);
    REQUIRE(hbmSrc != NULL);

    HBITMAP hbmGray = MakeGrayAvatarBitmap(hbmSrc);
    REQUIRE(hbmGray != NULL);

    // 取回灰度位图的像素。
    BITMAP bmGray = {};
    REQUIRE(::GetObject(hbmGray, sizeof(bmGray), &bmGray) != 0);
    REQUIRE(bmGray.bmBits != NULL);
    const BYTE* pGray = (const BYTE*)bmGray.bmBits;

    const BYTE kExpectedGray[4] = { 76, 150, 29, 255 };
    const BYTE kExpectedAlpha[4] = { 255, 255, 128, 0 };

    for (int i = 0; i < 4; ++i)
    {
        INFO("pixel index = " << i);
        const BYTE* px = pGray + (size_t)i * 4;
        CHECK(px[0] == kExpectedGray[i]);    // B
        CHECK(px[1] == kExpectedGray[i]);    // G
        CHECK(px[2] == kExpectedGray[i]);    // R
        CHECK(px[3] == kExpectedAlpha[i]);   // A 原样保留
    }

    // 源位图不能被改动 —— 恢复在线时还要拿它画彩色头像。
    for (int i = 0; i < 16; ++i)
    {
        INFO("source byte index = " << i);
        CHECK(pSrcBits[i] == srcPixels[i]);
    }

    ::DeleteObject(hbmGray);
    ::DeleteObject(hbmSrc);
}

TEST_CASE("GRAY5 入参为空时老实返回 NULL", "[avatargray][bitmap]")
{
    // 调用方据 NULL 退回画彩色原图；这里只要求不崩、不返回野句柄。
    CHECK(MakeGrayAvatarBitmap(NULL) == NULL);
}
