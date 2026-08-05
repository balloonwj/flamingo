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
#define _RICHEDIT_VER   0x0200
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

namespace balloongroupmanage {

namespace {

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

void GmDrawText(HDC hdc, LPCWSTR text, const RECT& rc,
                int pixelHeight, int weight, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }

    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        ToGdiWeight(weight),
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
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
