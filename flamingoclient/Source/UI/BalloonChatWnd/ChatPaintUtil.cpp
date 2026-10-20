/**
 *  ChatPaintUtil.cpp
 *
 *  聊天气泡与文件卡片共用的绘制工具函数，见 ChatPaintUtil.h。实现原样取自 ChatBubble.cpp，
 *  未改动任何绘制参数。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

// ---- ATL 头文件要求的前置依赖，与同目录 ChatBubble.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <gdiplus.h>

#include "ChatPaintUtil.h"
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体
#include "../BalloonMain/P2PBadge.h"  // MeasureP2PBadge / DrawP2PBadge：「P2P」徽章的量尺寸与绘制

namespace balloonchatwnd { namespace chatpaint {

void AddRoundRect(Gdiplus::GraphicsPath& path,
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
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

namespace {

// 把一个角的半径限制在宽、高各一半以内，负数按 0 处理
Gdiplus::REAL ClampCornerRadius(Gdiplus::REAL r, Gdiplus::REAL w, Gdiplus::REAL h)
{
    if (r < 0.0f)
    {
        r = 0.0f;
    }
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    return r;
}

// 往路径里加一个角：半径为 0 时只加一个点（直角），否则加一段 90 度的圆弧
//   cornerX / cornerY：圆弧外接正方形的左上角；startAngle：圆弧起始角（度）
void AddCorner(Gdiplus::GraphicsPath& path, Gdiplus::REAL cornerX, Gdiplus::REAL cornerY, Gdiplus::REAL r,
               Gdiplus::REAL startAngle, Gdiplus::REAL pointX, Gdiplus::REAL pointY)
{
    const Gdiplus::REAL kQuarterTurn = 90.0f;   // 一个角的圆弧所跨的角度（度）
    if (r <= 0.0f)
    {
        path.AddLine(pointX, pointY, pointX, pointY);
        return;
    }
    path.AddArc(cornerX, cornerY, r * 2.0f, r * 2.0f, startAngle, kQuarterTurn);
}

} // namespace

void AddRoundRectCorners(Gdiplus::GraphicsPath& path,
                         Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w, Gdiplus::REAL h,
                         Gdiplus::REAL rTopLeft, Gdiplus::REAL rTopRight,
                         Gdiplus::REAL rBottomRight, Gdiplus::REAL rBottomLeft)
{
    //各角起始角（度），与 AddRoundRect 相同：左上 180、右上 270、右下 0、左下 90
    const Gdiplus::REAL kTopLeftStart = 180.0f;
    const Gdiplus::REAL kTopRightStart = 270.0f;
    const Gdiplus::REAL kBottomRightStart = 0.0f;
    const Gdiplus::REAL kBottomLeftStart = 90.0f;
    const Gdiplus::REAL tl = ClampCornerRadius(rTopLeft, w, h);
    const Gdiplus::REAL tr = ClampCornerRadius(rTopRight, w, h);
    const Gdiplus::REAL br = ClampCornerRadius(rBottomRight, w, h);
    const Gdiplus::REAL bl = ClampCornerRadius(rBottomLeft, w, h);
    AddCorner(path, x, y, tl, kTopLeftStart, x, y);
    AddCorner(path, x + w - tr * 2.0f, y, tr, kTopRightStart, x + w, y);
    AddCorner(path, x + w - br * 2.0f, y + h - br * 2.0f, br, kBottomRightStart, x + w, y + h);
    AddCorner(path, x, y + h - bl * 2.0f, bl, kBottomLeftStart, x, y + h);
    path.CloseFigure();
}

void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags,
                 bool italic, bool underline, bool strike)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? kChatTextBoldWeight : FW_NORMAL,
        italic ? TRUE : FALSE,
        underline ? TRUE : FALSE,
        strike ? TRUE : FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        i18n::UiFontName());
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

int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0, bold ? kChatTextBoldWeight : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

int DrawTextBadgeInRow(HDC hdc, LPCWSTR text, COLORREF bgColor, COLORREF textColor, int left, int rowTop,
                       int rowBottom, int maxRight)
{
    //徽章文字左右、上下各留的空白（像素），与「P2P」徽章相同
    const int kPadX = 5;
    const int kPadY = 1;
    if (hdc == NULL || text == NULL || text[0] == L'\0')
        return left;
    HFONT font = ::CreateFontW(-kBotBadgeFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (font == NULL)
        return left;
    HGDIOBJ old = ::SelectObject(hdc, font);
    SIZE ts = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &ts);
    const int w = ts.cx + 2 * kPadX;
    const int h = ts.cy + 2 * kPadY;
    if (ts.cx <= 0 || left + w > maxRight)
    {
        ::SelectObject(hdc, old);
        ::DeleteObject(font);
        return left;
    }
    const int top = rowTop + (rowBottom - rowTop - h) / 2;
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath pill;
        //圆角半径取高度的一半，两端是完整的半圆
        AddRoundRect(pill, (Gdiplus::REAL)left, (Gdiplus::REAL)top, (Gdiplus::REAL)w, (Gdiplus::REAL)h,
                     (Gdiplus::REAL)h / 2);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(bgColor), GetGValue(bgColor), GetBValue(bgColor)));
        g.FillPath(&brush, &pill);
    }
    RECT rc = { left, top, left + w, top + h };
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, textColor);
    ::DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, old);
    ::DeleteObject(font);
    return rc.right;
}

int DrawP2PBadgeInRow(HDC hdc, int left, int rowTop, int rowBottom, int maxRight)
{
    HFONT font = ::CreateFontW(-kP2PBadgeFontPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (font == NULL)
        return left;
    const SIZE sz = balloonmain::MeasureP2PBadge(hdc, font);
    if (sz.cx <= 0 || left + sz.cx > maxRight)
    {
        ::DeleteObject(font);
        return left;
    }
    const int top = rowTop + (rowBottom - rowTop - sz.cy) / 2;
    const RECT rc = { left, top, left + sz.cx, top + sz.cy };
    balloonmain::DrawP2PBadge(hdc, rc, font);
    ::DeleteObject(font);
    return rc.right;
}

}} // namespace balloonchatwnd::chatpaint
