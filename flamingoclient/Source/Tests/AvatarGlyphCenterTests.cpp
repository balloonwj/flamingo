/**
 *  AvatarGlyphCenterTests.cpp
 *
 *  字形头像居中（2026-10-03）的测试，标签 [glyphcenter]。头像上的字（姓名首字或两个字母）应当
 *  落在头像正中：字的实际笔画外框中心与头像中心在水平、垂直两个方向上都相差不超过 0.5 像素。
 *  原先 GDI+ DrawString 画的字形头像普遍偏左 1~1.5 像素、偏上 0.5~2 像素，GDI DrawText 画的
 *  垂直方向偏低 0.5~1.5 像素（2026-10-03 实测）：
 *    GC1  MakeMonogramAvatar（圆形）在 32 / 36 / 40 三种尺寸下，各个字都居中；
 *    GC2  MakeMonogramSquareAvatar（圆角方形）同上；
 *    GC3  共用画字模块：ComputeCenteredTextOrigin 的落点计算；DrawCenteredGlyphGdi 在各处实际使用的
 *         五种「方框边长 / 字号」组合下画出的字都居中；
 *    GC4  聊天气泡与选人行实际画出来的头像字居中（这两处用 GDI 画字）。
 *
 *  只在内存位图上绘制与取像素，不创建窗口。框架 Catch2 v3；全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cstdlib>   // abs
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonMain/MainPanelAvatar.h"
#include "../UI/BalloonMain/AvatarGlyph.h"
#include "../UI/BalloonMain/MemberPickerRow.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

namespace {

// 32 位 DIB 每像素的字节数，及 B / G / R / A 四个分量在像素内的偏移。
const int kBytesPerPixel = 4;
const int kOffsetBlue    = 0;
const int kOffsetGreen   = 1;
const int kOffsetRed     = 2;
const int kOffsetAlpha   = 3;

// 完全不透明的 alpha 值。
const int kAlphaOpaque = 255;

// 允许的居中误差（像素）：外框中心按整数像素边界算，0.5 是能分辨的最小差。
const double kMaxCenterError = 0.5;

// 判定「这个像素属于字的笔画」：与头像底色三个分量之差的和超过这个值。笔画抗锯齿边缘上覆盖率
// 很低的像素不计入，两侧对称地被排除，不影响中心位置。
const int kInkDiffThreshold = 60;

// 底色与「是不是头像底色」判定的容差。
const int kBgTolerance = 3;

// 找字时从头像外框往里收的像素数，避开圆角或圆形边缘上底色与背景混合的像素。
const int kGlyphSearchInset = 2;

// 测试用的头像底色：亮度低，字是白色；聊天窗与选人行里没有别的元素用这个颜色。
const COLORREF kDiscColor = RGB(0x1E, 0x5A, 0xC8);

// 被测的字：常见的姓、一个笔画集中在中线的字、单个字母与两个字母。
const wchar_t* const kGlyphs[] = { L"韩", L"江", L"苏", L"周", L"我", L"一", L"A", L"ME" };
const int kGlyphCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

// GC1 / GC2 的头像边长（像素）：主面板通讯录、消息列表与搜索下拉实际使用的几种尺寸。
const int kSizes[] = { 32, 36, 40 };
const int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

// GC2 的圆角半径（像素）。
const int kSquareRadiusPx = 8;

// GC4 选人行与消息列表的测试尺寸（像素）。
const int kRowW  = 320;
const int kRowH  = 44;
const int kListW = 400;
const int kListH = 200;

// GC3 的五种「方框边长 / 字号」组合，取自各处头像画字的实际参数：群成员行、聊天气泡、旧列表行、
// 选人行、主面板顶部头像。
struct GdiGlyphCase
{
    int m_box;      // 方框边长（像素）
    int m_fontPx;   // 字号（像素）
};
const GdiGlyphCase kGdiCases[] = { { 26, 13 }, { 34, 16 }, { 40, 16 }, { 32, 14 }, { 56, 23 } };
const int kGdiCaseCount = sizeof(kGdiCases) / sizeof(kGdiCases[0]);

// 在作用域内启动 GDI+，离开时关闭。
class GcScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    GcScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~GcScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 一张图像的像素（RGB，按行存放）及其尺寸。
struct PixelImage
{
    int                   m_w;        // 宽（像素）
    int                   m_h;        // 高（像素）
    std::vector<COLORREF> m_pixels;   // 像素颜色，下标 y * m_w + x
    std::vector<int>      m_alpha;    // 像素 alpha；画在不透明画布上时全为 255
};

// 两个颜色三个分量之差的和。
int ColorDiff(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b))
         + abs((int)GetGValue(a) - (int)GetGValue(b))
         + abs((int)GetBValue(a) - (int)GetBValue(b));
}

// 读一张 32 位 DIB 位图的像素（预乘透明通道；不透明处即原色）。
bool ReadDib(HBITMAP hbm, PixelImage& out)
{
    DIBSECTION ds;
    if (hbm == NULL || ::GetObject(hbm, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBits == NULL)
    {
        return false;
    }
    out.m_w = ds.dsBm.bmWidth;
    out.m_h = (ds.dsBm.bmHeight < 0) ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
    out.m_pixels.resize((size_t)out.m_w * out.m_h);
    out.m_alpha.resize((size_t)out.m_w * out.m_h);
    const BYTE* p = static_cast<const BYTE*>(ds.dsBm.bmBits);
    for (int i = 0; i < out.m_w * out.m_h; ++i)
    {
        const BYTE* px = p + (size_t)i * kBytesPerPixel;
        out.m_pixels[i] = RGB(px[kOffsetRed], px[kOffsetGreen], px[kOffsetBlue]);
        out.m_alpha[i] = px[kOffsetAlpha];
    }
    return true;
}

// 把控件排版后画到白底的 32 位离屏位图上，读出像素。
bool PaintControl(balloonwjui::DuiControl& ctrl, int w, int h, bool forceLayout, PixelImage& out)
{
    RECT all = { 0, 0, w, h };
    if (forceLayout)
    {
        ctrl.ForceLayout(all);
    }
    else
    {
        ctrl.SetRect(all);
    }
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return false;
    }
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);
    ctrl.OnPaint(memDC, all);
    ::GdiFlush();
    const bool ok = ReadDib(bmp, out);
    for (size_t i = 0; i < out.m_alpha.size(); ++i)
    {
        out.m_alpha[i] = kAlphaOpaque;   // 画布不透明；DIB 的 alpha 字节在 GDI 绘制后无意义
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return ok;
}

// 一个矩形区域（含两端）以及它的中心。
struct Box
{
    int m_left;     // 最左像素列
    int m_right;    // 最右像素列
    int m_top;      // 最上像素行
    int m_bottom;   // 最下像素行

    double CenterX() const { return (m_left + m_right) / 2.0; }
    double CenterY() const { return (m_top + m_bottom) / 2.0; }
};

// 找颜色等于 disc（容差内）且不透明的像素的外框，即头像底色所在的范围。找不到返回 false。
bool FindDisc(const PixelImage& img, COLORREF disc, Box& out)
{
    bool found = false;
    for (int y = 0; y < img.m_h; ++y)
    {
        for (int x = 0; x < img.m_w; ++x)
        {
            const size_t i = (size_t)y * img.m_w + x;
            if (img.m_alpha[i] == kAlphaOpaque && ColorDiff(img.m_pixels[i], disc) <= kBgTolerance * 3)
            {
                if (!found)
                {
                    out.m_left = out.m_right = x;
                    out.m_top = out.m_bottom = y;
                    found = true;
                }
                out.m_left   = (x < out.m_left)   ? x : out.m_left;
                out.m_right  = (x > out.m_right)  ? x : out.m_right;
                out.m_top    = (y < out.m_top)    ? y : out.m_top;
                out.m_bottom = (y > out.m_bottom) ? y : out.m_bottom;
            }
        }
    }
    return found;
}

// 在头像外框（往里收 kGlyphSearchInset）内找字的笔画外框：不透明、且与底色明显不同的像素。
bool FindGlyph(const PixelImage& img, const Box& disc, COLORREF discColor, Box& out)
{
    bool found = false;
    for (int y = disc.m_top + kGlyphSearchInset; y <= disc.m_bottom - kGlyphSearchInset; ++y)
    {
        for (int x = disc.m_left + kGlyphSearchInset; x <= disc.m_right - kGlyphSearchInset; ++x)
        {
            const size_t i = (size_t)y * img.m_w + x;
            if (img.m_alpha[i] != kAlphaOpaque || ColorDiff(img.m_pixels[i], discColor) <= kInkDiffThreshold)
            {
                continue;
            }
            if (!found)
            {
                out.m_left = out.m_right = x;
                out.m_top = out.m_bottom = y;
                found = true;
            }
            out.m_left   = (x < out.m_left)   ? x : out.m_left;
            out.m_right  = (x > out.m_right)  ? x : out.m_right;
            out.m_top    = (y < out.m_top)    ? y : out.m_top;
            out.m_bottom = (y > out.m_bottom) ? y : out.m_bottom;
        }
    }
    return found;
}

// 检查一张头像上的字居中：字的笔画外框中心与头像底色外框中心相差不超过 kMaxCenterError。
void CheckGlyphCentered(const PixelImage& img, const wchar_t* glyph)
{
    Box disc;
    REQUIRE(FindDisc(img, kDiscColor, disc));
    Box ink;
    REQUIRE(FindGlyph(img, disc, kDiscColor, ink));
    const double dx = ink.CenterX() - disc.CenterX();
    const double dy = ink.CenterY() - disc.CenterY();
    INFO("glyph " << CW2A(glyph, CP_UTF8).m_psz << ": disc x[" << disc.m_left << "," << disc.m_right
         << "] y[" << disc.m_top << "," << disc.m_bottom << "], ink x[" << ink.m_left << "," << ink.m_right
         << "] y[" << ink.m_top << "," << ink.m_bottom << "], offset (" << dx << ", " << dy << ")");
    CHECK(abs(dx) <= kMaxCenterError);
    CHECK(abs(dy) <= kMaxCenterError);
}

// 在 box 见方、铺满 kDiscColor 的位图上用 DrawCenteredGlyphGdi 画白色的字，读出像素。
bool PaintGdiGlyph(const wchar_t* glyph, int box, int fontPx, PixelImage& out)
{
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return false;
    }
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = box;
    bi.bmiHeader.biHeight      = -box;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    RECT all = { 0, 0, box, box };
    HBRUSH bg = ::CreateSolidBrush(kDiscColor);
    ::FillRect(memDC, &all, bg);
    ::DeleteObject(bg);
    balloonmain::DrawCenteredGlyphGdi(memDC, glyph, all, fontPx, true, RGB(0xFF, 0xFF, 0xFF));
    ::GdiFlush();
    const bool ok = ReadDib(bmp, out);
    for (size_t i = 0; i < out.m_alpha.size(); ++i)
    {
        out.m_alpha[i] = kAlphaOpaque;
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return ok;
}

// 造一条对方发来的文字消息，头像为字形 glyph、底色为 kDiscColor。
balloonchatwnd::ChatMessage MakeGlyphMessage(const wchar_t* glyph)
{
    balloonchatwnd::ChatMessage m = balloonchatwnd::MakeTextMessage(false, _T("text"));
    m.senderUserId      = 9001;
    m.senderAvatarText  = glyph;
    m.senderAvatarColor = kDiscColor;
    return m;
}

} // anonymous namespace

TEST_CASE("GC1 圆形字形头像：各尺寸下字都居中", "[glyphcenter]")
{
    GcScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    for (int s = 0; s < kSizeCount; ++s)
    {
        for (int i = 0; i < kGlyphCount; ++i)
        {
            INFO("size " << kSizes[s]);
            HBITMAP hbm = balloonmain::MakeMonogramAvatar(kGlyphs[i], kDiscColor, kSizes[s]);
            REQUIRE(hbm != NULL);
            PixelImage img;
            REQUIRE(ReadDib(hbm, img));
            ::DeleteObject(hbm);
            CheckGlyphCentered(img, kGlyphs[i]);
        }
    }
}

TEST_CASE("GC2 圆角方形字形头像：各尺寸下字都居中", "[glyphcenter]")
{
    GcScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    for (int s = 0; s < kSizeCount; ++s)
    {
        for (int i = 0; i < kGlyphCount; ++i)
        {
            INFO("size " << kSizes[s]);
            HBITMAP hbm = balloonmain::MakeMonogramSquareAvatar(kGlyphs[i], kDiscColor, kSizes[s], kSquareRadiusPx);
            REQUIRE(hbm != NULL);
            PixelImage img;
            REQUIRE(ReadDib(hbm, img));
            ::DeleteObject(hbm);
            CheckGlyphCentered(img, kGlyphs[i]);
        }
    }
}

TEST_CASE("GC3 共用画字模块：落点计算正确，GDI 版在各处的参数下都画在正中", "[glyphcenter]")
{
    SECTION("落点计算")
    {
        // 32 见方的方框，外框 18 见方、相对原点偏 (2, 5)：两侧各留 7 像素。
        const RECT box = { 10, 20, 42, 52 };
        const RECT ink = { 2, 5, 20, 23 };
        const POINT org = balloonmain::ComputeCenteredTextOrigin(box, ink);
        CHECK(org.x == 10 + 7 - 2);
        CHECK(org.y == 20 + 7 - 5);

        // 宽度差为奇数（33 与 18）：外框靠左，多出的一个像素留在右侧。
        const RECT oddBox = { 0, 0, 33, 33 };
        const POINT odd = balloonmain::ComputeCenteredTextOrigin(oddBox, ink);
        CHECK(odd.x == 7 - 2);
        CHECK(odd.y == 7 - 5);
    }

    SECTION("GDI 版实际画出来居中")
    {
        for (int c = 0; c < kGdiCaseCount; ++c)
        {
            for (int i = 0; i < kGlyphCount; ++i)
            {
                INFO("box " << kGdiCases[c].m_box << ", font " << kGdiCases[c].m_fontPx);
                PixelImage img;
                REQUIRE(PaintGdiGlyph(kGlyphs[i], kGdiCases[c].m_box, kGdiCases[c].m_fontPx, img));
                CheckGlyphCentered(img, kGlyphs[i]);
            }
        }
    }
}

TEST_CASE("GC4 聊天气泡与选人行实际画出来的头像字居中", "[glyphcenter]")
{
    GcScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    SECTION("聊天气泡")
    {
        for (int i = 0; i < kGlyphCount; ++i)
        {
            balloonchatwnd::ChatMessageList list;
            list.AppendMessage(MakeGlyphMessage(kGlyphs[i]));
            PixelImage img;
            REQUIRE(PaintControl(list, kListW, kListH, true, img));
            CheckGlyphCentered(img, kGlyphs[i]);
        }
    }

    SECTION("选人行")
    {
        for (int i = 0; i < kGlyphCount; ++i)
        {
            balloonmain::MemberPickerRow row;
            row.SetCheckVisible(false);
            row.SetRowData(_T("Name"), kGlyphs[i], kDiscColor);
            PixelImage img;
            REQUIRE(PaintControl(row, kRowW, kRowH, false, img));
            CheckGlyphCentered(img, kGlyphs[i]);
        }
    }
}
