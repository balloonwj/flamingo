/**
 *  flamingoAdmin —— 通用绘制 / 字体辅助实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Widgets/UiHelpers.h"
#include "Theme/AdminTheme.h"

#include <gdiplus.h>
#include <map>

// 让本文件可直接用 REAL（即 Gdiplus::REAL，等价于 float）。
using Gdiplus::REAL;

namespace flamingoadmin { namespace ui {

namespace {

// 96 dpi 下「磅 → 像素高度」换算：1 磅 = 1/72 英寸，96 dpi 下 = 96/72 像素。
// Phase 2 统一按 96 dpi 逻辑像素出图，不做 per-monitor 缩放（后续阶段再处理）。
const int kDpi96 = 96;
const int kPointsPerInch = 72;

// GDI 字重常量：普通 400 / 半粗 600。
const int kFontWeightNormal = FW_NORMAL;
const int kFontWeightBold   = FW_SEMIBOLD;

// 把 COLORREF 转成 GDI+ 颜色（不带 alpha，默认不透明）。
Gdiplus::Color ToGdiColor(COLORREF c)
{
    return Gdiplus::Color(GetRValue(c), GetGValue(c), GetBValue(c));
}

// 构造一条「左上 → 右下」圆角矩形路径，供填充 / 描边复用。
void BuildRoundRectPath(Gdiplus::GraphicsPath& path, const RECT& rc, int radius)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    int d = radius * 2;                       // 圆角处的外切圆直径
    if (d > w) { d = w; }
    if (d > h) { d = h; }
    if (d < 0) { d = 0; }

    const REAL x = static_cast<REAL>(rc.left);
    const REAL y = static_cast<REAL>(rc.top);
    const REAL fd = static_cast<REAL>(d);

    if (d == 0)
    {
        path.AddRectangle(Gdiplus::RectF(x, y,
            static_cast<REAL>(w), static_cast<REAL>(h)));
        return;
    }
    // 四段 90° 圆弧 + 四条边，顺时针闭合。
    path.AddArc(x,                       y,                        fd, fd, 180.0f, 90.0f);
    path.AddArc(x + w - fd,              y,                        fd, fd, 270.0f, 90.0f);
    path.AddArc(x + w - fd,              y + h - fd,               fd, fd,   0.0f, 90.0f);
    path.AddArc(x,                       y + h - fd,               fd, fd,  90.0f, 90.0f);
    path.CloseFigure();
}

} // anonymous namespace

HFONT UiFont(int pointSize, bool bold)
{
    // 缓存键：磅值左移 1 位 + 粗细标志，足以区分本工程用到的全部字体规格。
    static std::map<int, HFONT> s_fontCache;
    const int key = (pointSize << 1) | (bold ? 1 : 0);

    auto it = s_fontCache.find(key);
    if (it != s_fontCache.end())
    {
        return it->second;
    }

    const int heightPx = -::MulDiv(pointSize, kDpi96, kPointsPerInch);
    HFONT font = ::CreateFont(
        heightPx, 0, 0, 0,
        bold ? kFontWeightBold : kFontWeightNormal,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        theme::kFontFamily);

    s_fontCache[key] = font;
    return font;
}

void FillSolidRect(HDC hdc, const RECT& rc, COLORREF color)
{
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(hdc, &rc, brush);
    ::DeleteObject(brush);
}

void FillLine(HDC hdc, int x, int y, int cx, int cy, COLORREF color)
{
    RECT rc = { x, y, x + cx, y + cy };
    FillSolidRect(hdc, rc, color);
}

void FillRoundRect(HDC hdc, const RECT& rc, COLORREF color, int radius)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::GraphicsPath path;
    BuildRoundRectPath(path, rc, radius);
    Gdiplus::SolidBrush brush(ToGdiColor(color));
    g.FillPath(&brush, &path);
}

void StrokeRoundRect(HDC hdc, const RECT& rc, COLORREF color,
                     int radius, float width)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 描边路径向内缩半个线宽，避免边线被控件矩形裁掉一半。
    RECT inner = rc;
    const int inset = static_cast<int>(width / 2.0f + 0.5f);
    ::InflateRect(&inner, -inset, -inset);

    Gdiplus::GraphicsPath path;
    BuildRoundRectPath(path, inner, radius);
    Gdiplus::Pen pen(ToGdiColor(color), width);
    g.DrawPath(&pen, &path);
}

void DrawTextLine(HDC hdc, LPCTSTR text, const RECT& rc, COLORREF color,
                  HFONT font, UINT dtFlags)
{
    HFONT    oldFont = static_cast<HFONT>(::SelectObject(hdc, font));
    int      oldBk   = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldClr  = ::SetTextColor(hdc, color);

    RECT rcText = rc;
    ::DrawText(hdc, text, -1, &rcText, dtFlags);

    ::SetTextColor(hdc, oldClr);
    ::SetBkMode(hdc, oldBk);
    ::SelectObject(hdc, oldFont);
}

void FillDiagonalGradient(HDC hdc, const RECT& rc,
                          COLORREF c0, COLORREF c1, COLORREF c2)
{
    Gdiplus::Graphics g(hdc);

    // 从矩形左上角到右下角的线性渐变。
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::PointF(static_cast<REAL>(rc.left),  static_cast<REAL>(rc.top)),
        Gdiplus::PointF(static_cast<REAL>(rc.right), static_cast<REAL>(rc.bottom)),
        ToGdiColor(c0), ToGdiColor(c2));

    // 插入中间停靠色：0.0 = c0，0.55 = c1，1.0 = c2。
    Gdiplus::Color  colors[3]    = { ToGdiColor(c0), ToGdiColor(c1), ToGdiColor(c2) };
    Gdiplus::REAL   positions[3] = { 0.0f, 0.55f, 1.0f };
    brush.SetInterpolationColors(colors, positions, 3);

    g.FillRectangle(&brush,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

void FillTranslucent(HDC hdc, const RECT& rc, COLORREF color, int alpha)
{
    if (alpha < 0)   { alpha = 0;   }
    if (alpha > 255) { alpha = 255; }

    // GDI+ 默认 SourceOver 合成模式：半透明画刷会与背景已绘内容混合。
    Gdiplus::Graphics g(hdc);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        static_cast<BYTE>(alpha),
        GetRValue(color), GetGValue(color), GetBValue(color)));
    g.FillRectangle(&brush,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

}} // namespace flamingoadmin::ui
