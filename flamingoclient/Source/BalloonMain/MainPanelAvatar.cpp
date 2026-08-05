/**
 *  MainPanelAvatar.cpp
 *
 *  Monogram avatar HBITMAP 生成实现 —— GDI+ AA 圆 + AA 文字版本。
 *
 *  实现要点:
 *    1) CreateDIBSection 建 32bpp top-down DIB,bits 先清零(全透明)。
 *    2) 用 Gdiplus::Bitmap 直接包 DIB 的 bits(PixelFormat32bppPARGB),
 *       这样 Gdiplus::Graphics 写到该 Bitmap 时,等同于直接写 HBITMAP
 *       的像素;PARGB 表示 premultiplied alpha,可直接喂 AlphaBlend。
 *    3) Graphics 设 SmoothingModeAntiAlias + TextRenderingHintAntiAlias,
 *       FillEllipse 画 AA 圆,DrawString 画 AA 文字。
 *    4) 文字颜色按底色 NTSC luma 切深字 / 白字,避免浅 pastel 底 + 白字
 *       糊成一片。
 *    5) 返回的 HBITMAP 是 premultiplied alpha,圆外像素为 (a=0,r=g=b=0),
 *       配合 DuiTreeView::SetIconUsesAlpha(true) 走 AlphaBlend 路径,
 *       圆形边缘无锯齿。
 *
 *  balloonwj@qq.com   2026-05-26
 */

#include "stdafx.h"
#include "MainPanelAvatar.h"

#include <tchar.h>
#include <vector>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace balloonmain {

namespace {

// GDI+ 懒初始化 token —— 沿用 balloonui 里所有 GDI+ helper 的统一模式
// (DuiAvatar / DuiGif / DuiNinePatch / DuiPaintAA 等);多次调用 GdiplusStartup
// 是安全的(参考 DuiNinePatch.cpp 注释:"reaching across module boundaries;
// GdiplusStartup is safe to call")。本翻译单元保留自己的 token,卸载时无人
// GdiplusShutdown 不影响功能(进程退出时自动回收)。
ULONG_PTR& GdipToken()
{
    static ULONG_PTR s_token = 0;
    return s_token;
}

void EnsureGdiplus()
{
    if (GdipToken() != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&GdipToken(), &input, nullptr);
}

} // anonymous namespace

HBITMAP MakeMonogramAvatar(LPCTSTR text, COLORREF bgColor, int sizePx)
{
    if (sizePx <= 0)
    {
        return NULL;
    }

    EnsureGdiplus();

    // 32bpp top-down DIB,内存顺序 BGRA(GDI 标准 / GDI+ PARGB 同序)。
    // top-down 时 biHeight 为负数,row 0 在 bits 起始处;stride = sizePx * 4。
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sizePx;
    bi.bmiHeader.biHeight      = -sizePx;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hBmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBmp == NULL || bits == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return NULL;
    }

    // 全部清零 → 整张完全透明(alpha=0,premultiplied 下 rgb 也必须为 0)。
    int total = sizePx * sizePx;
    ::ZeroMemory(bits, (size_t)total * 4);

    // 用 Gdiplus::Bitmap 直接包 DIB 的像素 buffer。stride 是正数(top-down)。
    // PARGB 表示 premultiplied alpha——GDI+ 写入时自动 premultiply,符合
    // AlphaBlend(AC_SRC_OVER + AC_SRC_ALPHA)对源位图的要求。
    int stride = sizePx * 4;
    Gdiplus::Bitmap bitmap(sizePx, sizePx, stride,
                           PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits));

    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    // 画 AA 圆 —— FillEllipse 矩形要往内收 0.5 像素,让 AA 边缘正好落在
    // 位图四边内,而不是被裁掉。GDI+ 的 RectF 是浮点参数,直接用即可。
    BYTE r = GetRValue(bgColor);
    BYTE gB = GetGValue(bgColor);
    BYTE b = GetBValue(bgColor);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, r, gB, b));
    g.FillEllipse(&bgBrush,
                  0.5f, 0.5f,
                  static_cast<Gdiplus::REAL>(sizePx) - 1.0f,
                  static_cast<Gdiplus::REAL>(sizePx) - 1.0f);

    // 文字为空就只画底色圆。
    if (text == NULL || text[0] == _T('\0'))
    {
        return hBmp;
    }

    // 取前 1-2 个字符。中文一个就够,英文允许两字母(如 "ME")。
    int len = (int)::_tcslen(text);
    if (len > 2)
    {
        len = 2;
    }

    // 文字色按底色 NTSC luma 切:亮底用深字、暗底用白字。
    int luma = ((int)r * 299 + (int)gB * 587 + (int)b * 114) / 1000;
    Gdiplus::Color textColor = (luma > 160)
                               ? Gdiplus::Color(255,  60,  60,  60)
                               : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush textBrush(textColor);

    // 字号约为 sizePx 的 9/16,与旧版保持一致的视觉比例。GDI+ FontFamily
    // 接 UTF-16 字符串,_T 在 _UNICODE 下就是 wchar_t,直接传即可。
    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
    Gdiplus::REAL fontSize = static_cast<Gdiplus::REAL>(sizePx * 9 / 16);
    Gdiplus::Font font(&fontFamily, fontSize,
                       Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);

    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    Gdiplus::RectF layoutRect(0.0f, 0.0f,
                              static_cast<Gdiplus::REAL>(sizePx),
                              static_cast<Gdiplus::REAL>(sizePx));

    // DrawString 接的也是 wchar_t*;在 _UNICODE 工程下 LPCTSTR 即 wchar_t*。
    g.DrawString(reinterpret_cast<const WCHAR*>(text), len,
                 &font, layoutRect, &fmt, &textBrush);

    // Graphics / Bitmap 析构会自动 flush,bits 已被写满,返回 HBITMAP 即可。
    return hBmp;
}

namespace {

// 往 path 追加一个圆角矩形子路径（4 段圆弧 + 闭合）。x/y/w/h 为矩形，r 为圆角
// 半径（调用方须已夹到 [0, min(w,h)/2]）。供 MakeMonogramSquareAvatar 复用。
void AddRoundRectPath(Gdiplus::GraphicsPath& path,
                      Gdiplus::REAL x, Gdiplus::REAL y,
                      Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r)
{
    if (r <= 0.0f)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,           y,           d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d,   y,           d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d,   y + h - d,   d, d,   0.0f, 90.0f);
    path.AddArc(x,           y + h - d,   d, d,  90.0f, 90.0f);
    path.CloseFigure();
}

} // anonymous namespace

HBITMAP MakeMonogramSquareAvatar(LPCTSTR text, COLORREF bgColor, int sizePx, int radiusPx)
{
    if (sizePx <= 0)
    {
        return NULL;
    }

    EnsureGdiplus();

    // 与 MakeMonogramAvatar 同样的 32bpp top-down 预乘 alpha DIBSection。
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sizePx;
    bi.bmiHeader.biHeight      = -sizePx;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hBmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBmp == NULL || bits == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return NULL;
    }

    int total = sizePx * sizePx;
    ::ZeroMemory(bits, (size_t)total * 4);

    int stride = sizePx * 4;
    Gdiplus::Bitmap bitmap(sizePx, sizePx, stride,
                           PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits));

    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    // 圆角半径夹到 [0, sizePx/2]。
    Gdiplus::REAL r = static_cast<Gdiplus::REAL>(radiusPx);
    Gdiplus::REAL maxR = static_cast<Gdiplus::REAL>(sizePx) * 0.5f;
    if (r > maxR)
    {
        r = maxR;
    }
    if (r < 0.0f)
    {
        r = 0.0f;
    }

    // 画 AA 圆角矩形底 —— 与圆版一样往内收 0.5 像素，让 AA 边缘落在位图四边内。
    BYTE rr = GetRValue(bgColor);
    BYTE gg = GetGValue(bgColor);
    BYTE bb = GetBValue(bgColor);
    Gdiplus::GraphicsPath path;
    AddRoundRectPath(path, 0.5f, 0.5f,
                     static_cast<Gdiplus::REAL>(sizePx) - 1.0f,
                     static_cast<Gdiplus::REAL>(sizePx) - 1.0f, r);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, rr, gg, bb));
    g.FillPath(&bgBrush, &path);

    // 文字为空就只画底。
    if (text == NULL || text[0] == _T('\0'))
    {
        return hBmp;
    }

    // 取前 1-2 个字符；不做大小写转换，wchar 直接传给 GDI+（emoji 代理对原样保留）。
    int len = (int)::_tcslen(text);
    if (len > 2)
    {
        len = 2;
    }

    int luma = ((int)rr * 299 + (int)gg * 587 + (int)bb * 114) / 1000;
    Gdiplus::Color textColor = (luma > 160)
                               ? Gdiplus::Color(255,  60,  60,  60)
                               : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush textBrush(textColor);

    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
    Gdiplus::REAL fontSize = static_cast<Gdiplus::REAL>(sizePx * 9 / 16);
    Gdiplus::Font font(&fontFamily, fontSize,
                       Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);

    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    Gdiplus::RectF layoutRect(0.0f, 0.0f,
                              static_cast<Gdiplus::REAL>(sizePx),
                              static_cast<Gdiplus::REAL>(sizePx));

    g.DrawString(reinterpret_cast<const WCHAR*>(text), len,
                 &font, layoutRect, &fmt, &textBrush);

    return hBmp;
}

HICON MakeMonogramIcon(LPCTSTR text, COLORREF bgColor, int sizePx)
{
    if (sizePx <= 0)
    {
        return NULL;
    }

    // color 面：复用 monogram 位图（32bpp 预乘 alpha，圆外 alpha=0 全透明）。
    HBITMAP hColor = MakeMonogramAvatar(text, bgColor, sizePx);
    if (hColor == NULL)
    {
        return NULL;
    }

    // AND 掩码面：单色位图，行按 16 位（WORD）对齐。这里取全 0 —— 对 32bpp
    // 带 alpha 的图标，系统按 color 面的 alpha 通道决定每像素可见性，AND 掩码
    // 全 0 表示"不额外抠任何像素"，可见性完全交给 alpha。CreateBitmap 不清零
    // 内存，故必须显式喂一段清零的 bits，避免未初始化位导致花屏。
    const int kMonoAlignBits = 16;                                   // 单色位图行对齐位数
    int maskStride = ((sizePx + kMonoAlignBits - 1) / kMonoAlignBits) * (kMonoAlignBits / 8);
    std::vector<BYTE> maskBits((size_t)maskStride * sizePx, 0);
    HBITMAP hMask = ::CreateBitmap(sizePx, sizePx, 1, 1, maskBits.empty() ? NULL : &maskBits[0]);
    if (hMask == NULL)
    {
        ::DeleteObject(hColor);
        return NULL;
    }

    ICONINFO ii = {};
    ii.fIcon    = TRUE;       // 建图标（非光标）
    ii.hbmColor = hColor;     // 32bpp 预乘 alpha color 面
    ii.hbmMask  = hMask;      // 全 0 AND 掩码

    HICON hIcon = ::CreateIconIndirect(&ii);

    // CreateIconIndirect 内部会拷贝位图，传入的 color / mask 位图由调用方负责
    // 释放（无论成功失败都要释放，否则泄漏）。
    ::DeleteObject(hColor);
    ::DeleteObject(hMask);

    return hIcon;
}

} // namespace balloonmain
