/**
 *  GroupManagePaint.cpp
 *
 *  群管理对话框共用绘制工具的实现。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 自带 ATL/WTL 前置依赖，
 *  工程里标 PrecompiledHeader = NotUsing。
 *
 *  balloonwj@qq.com   2026-06-03
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>

#include "GroupManagePaint.h"
#include "../../../third-party/balloonui/balloonui/DuiDpi.h"   // Scale / GetSystemDpi
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体

namespace balloongroupmanage {

namespace {

// 单元测试指定的 DPI；0 表示未指定，取系统 DPI。
int g_dpiOverride = 0;

// 把 GmFontWeight 映射成 GDI 的 LOGFONT 字重值。
int ToGdiWeight(int weight)
{
    if (weight == kGmFontBold)
    {
        return FW_BOLD;
    }
    if (weight == kGmFontSemiBold)
    {
        return FW_SEMIBOLD;
    }
    return FW_NORMAL;
}

// 把 COLORREF 转成不透明的 GDI+ Color。
Gdiplus::Color ToGdipColor(COLORREF c)
{
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

} // anonymous namespace

GmTextSpec GmTextStyleSpec(int style)
{
    // 各档字号（96 DPI 下的像素），取值依据见头文件 GmTextStyle 的说明
    const int kPageTitlePx = 15;
    const int kTitlePx     = 14;
    const int kBodyPx      = 13;
    const int kCaptionPx   = 12;

    GmTextSpec spec;
    spec.m_px     = kBodyPx;
    spec.m_weight = kGmFontNormal;

    switch (style)
    {
    //页面大标题、顶栏群名
    case kGmTextPageTitle:
        spec.m_px     = kPageTitlePx;
        spec.m_weight = kGmFontBold;
        break;

    //卡片标题一类：与条目标题同字号，靠粗体拉开层级
    case kGmTextCardTitle:
        spec.m_px     = kTitlePx;
        spec.m_weight = kGmFontBold;
        break;

    //导航项、设置行标题、表单标签
    case kGmTextItemTitle:
        spec.m_px = kTitlePx;
        break;

    //说明一类的小字
    case kGmTextCaption:
        spec.m_px = kCaptionPx;
        break;

    //正文，以及越界的取值
    case kGmTextBody:
    default:
        break;
    }
    return spec;
}

COLORREF GmInputBorderColor(bool enabled, bool focused, bool hover)
{
    const COLORREF kBorderNormal  = RGB(0xED, 0xED, 0xED);   // 常态 / 禁用
    const COLORREF kBorderHover   = RGB(0xD6, 0xD3, 0xCE);   // 鼠标悬停
    const COLORREF kBorderFocused = RGB(0xFF, 0x7A, 0x45);   // 持有焦点（品牌橙）

    if (!enabled)
    {
        return kBorderNormal;
    }
    if (focused)
    {
        return kBorderFocused;
    }
    if (hover)
    {
        return kBorderHover;
    }
    return kBorderNormal;
}

int GmDpi()
{
    if (g_dpiOverride > 0)
    {
        return g_dpiOverride;
    }
    // 与 DuiResMgr 建默认字体时取的是同一个值（系统 DPI），见 DuiResMgr.cpp
    return balloonwjui::DuiDpi::GetSystemDpi();
}

void GmSetDpiOverride(int dpi)
{
    g_dpiOverride = (dpi > 0) ? dpi : 0;
}

int GmScale(int logicalPx)
{
    return balloonwjui::DuiDpi::Scale(logicalPx, GmDpi());
}

int GmFontPixelHeight(int logicalPx)
{
    return GmScale(logicalPx);
}

namespace {

// 建群管理窗统一使用的文字字体（微软雅黑）。GmDrawText 与 GmMeasureTextWidth
// 共用这一处，保证量出的宽度与实际绘制一致。
//   pixelHeight：96 DPI 下的字号，内部经 GmFontPixelHeight 换算；
//   weight：     字重，取值见 GmFontWeight。
// @return 新建的字体，调用方负责 DeleteObject；失败返回 NULL。
HFONT CreateGmFont(int pixelHeight, int weight)
{
    return ::CreateFontW(
        -GmFontPixelHeight(pixelHeight), 0, 0, 0,
        ToGdiWeight(weight),
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
}

} // anonymous namespace

int GmMeasureTextWidth(LPCWSTR text, int style)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    const GmTextSpec spec = GmTextStyleSpec(style);
    HFONT hFont = CreateGmFont(spec.m_px, spec.m_weight);
    if (hFont == NULL)
    {
        return 0;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        ::DeleteObject(hFont);
        return 0;
    }
    HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOld);
    ::ReleaseDC(NULL, hdc);
    ::DeleteObject(hFont);
    return sz.cx;
}

void GmDrawText(HDC hdc, LPCWSTR text, const RECT& rc,
                int pixelHeight, int weight, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }

    HFONT hFont = CreateGmFont(pixelHeight, weight);
    if (hFont == NULL)
    {
        return;
    }

    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);

    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

void GmAddRoundRect(Gdiplus::GraphicsPath& path,
                    Gdiplus::REAL x, Gdiplus::REAL y,
                    Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    if (r < 0.0f)
    {
        r = 0.0f;
    }

    const Gdiplus::REAL d = r * 2.0f;
    if (d <= 0.0f)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }

    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

void GmFillRoundRect(HDC hdc, const RECT& rc, Gdiplus::REAL radius,
                     COLORREF fill, COLORREF border, Gdiplus::REAL borderWidth)
{
    const Gdiplus::REAL w = (Gdiplus::REAL)(rc.right - rc.left);
    const Gdiplus::REAL h = (Gdiplus::REAL)(rc.bottom - rc.top);
    if (w <= 0.0f || h <= 0.0f)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 描边时内缩半个线宽，使描边落在矩形内、不被裁切。
    Gdiplus::REAL inset = (border != CLR_INVALID) ? (borderWidth / 2.0f) : 0.0f;

    Gdiplus::GraphicsPath path;
    GmAddRoundRect(path,
                   (Gdiplus::REAL)rc.left + inset, (Gdiplus::REAL)rc.top + inset,
                   w - inset * 2.0f, h - inset * 2.0f, radius);

    if (fill != CLR_INVALID)
    {
        Gdiplus::SolidBrush brush(ToGdipColor(fill));
        g.FillPath(&brush, &path);
    }
    if (border != CLR_INVALID && borderWidth > 0.0f)
    {
        Gdiplus::Pen pen(ToGdipColor(border), borderWidth);
        g.DrawPath(&pen, &path);
    }
}

} // namespace balloongroupmanage
