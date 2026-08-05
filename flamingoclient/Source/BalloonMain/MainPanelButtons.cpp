/**
 *  MainPanelButtons.cpp
 *
 *  主面板底部栏两个按钮控件的绘制实现。形状用 GDI+ 抗锯齿，文字用 GDI
 *  DrawText（清晰）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
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
#pragma comment(lib, "gdiplus.lib")

#include "MainPanelButtons.h"
#include "MainPanelIcons.h"

namespace balloonmain {

namespace {

// ---- 配色 --------------------------------------------------------------

// 图标按钮悬停底色（浅灰）。
const Gdiplus::Color kIconBtnHover(255, 0xF0, 0xEF, 0xEC);
// 图标按钮图标色（design --text-muted #6B6B6B）。
const Gdiplus::Color kIconBtnColor(255, 0x6B, 0x6B, 0x6B);

// 主操作按钮白底 / 悬停底。
const Gdiplus::Color kActionFill(255, 255, 255, 255);
const Gdiplus::Color kActionHoverFill(255, 0xF5, 0xF4, 0xF1);
// 主操作按钮边框（design --border-strong，在白底上近似值）。
const Gdiplus::Color kActionBorder(255, 0xDD, 0xDB, 0xD7);
// 主操作按钮加号图标色（与文字同色）。
const Gdiplus::Color kActionIconColor(255, 0x3A, 0x3A, 0x3A);
// 主操作按钮文字色（design --text）。
const COLORREF kActionTextColor = RGB(0x3A, 0x3A, 0x3A);

// ---- 几何常量 ----------------------------------------------------------

const Gdiplus::REAL kIconBtnIconSize   = 17.0f;   // 图标按钮图标尺寸
const Gdiplus::REAL kIconBtnRadius     = 6.0f;    // 图标按钮悬停底圆角
const Gdiplus::REAL kActionMaxHeight   = 28.0f;   // 主操作按钮最大高度
const Gdiplus::REAL kActionRadius      = 6.0f;    // 主操作按钮圆角
const Gdiplus::REAL kActionIconSize    = 13.0f;   // 主操作按钮加号尺寸
const Gdiplus::REAL kActionIconTextGap = 3.0f;    // 加号与文字间距
const int           kActionTextPx      = 12;      // 主操作按钮文字字号
const Gdiplus::REAL kStrokeWidth       = 2.0f;    // 图标描边宽度

// 用四段 90° 圆弧 + 闭合，向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;

    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 用 GDI 绘制文字 —— DrawText 经系统 ClearType 光栅化，清晰锐利。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

// 测量一段文字的像素宽度。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
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

} // anonymous namespace

// =======================================================================
// MainPanelIconButton
// =======================================================================

MainPanelIconButton::MainPanelIconButton()
    : m_iconKind(kIconList)
{
}

void MainPanelIconButton::SetIconKind(int kind)
{
    m_iconKind = kind;
    Invalidate();
}

void MainPanelIconButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 悬停：浅灰圆角底。
    if (IsHover())
    {
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x, (Gdiplus::REAL)y,
                     (Gdiplus::REAL)w, (Gdiplus::REAL)h, kIconBtnRadius);
        Gdiplus::SolidBrush hover(kIconBtnHover);
        g.FillPath(&hover, &path);
    }

    // 图标居中。
    const Gdiplus::REAL iconX = (Gdiplus::REAL)x + ((Gdiplus::REAL)w - kIconBtnIconSize) / 2.0f;
    const Gdiplus::REAL iconY = (Gdiplus::REAL)y + ((Gdiplus::REAL)h - kIconBtnIconSize) / 2.0f;
    DrawLineIcon(g, m_iconKind, iconX, iconY, kIconBtnIconSize,
                 kIconBtnColor, kStrokeWidth);
}

// =======================================================================
// MainPanelActionButton
// =======================================================================

MainPanelActionButton::MainPanelActionButton()
{
}

void MainPanelActionButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 按钮主体高度上限 kActionMaxHeight，垂直居中于控件矩形。
    Gdiplus::REAL btnH = (Gdiplus::REAL)h;
    if (btnH > kActionMaxHeight)
    {
        btnH = kActionMaxHeight;
    }
    const Gdiplus::REAL btnY = (Gdiplus::REAL)y + ((Gdiplus::REAL)h - btnH) / 2.0f;

    const CString text = GetText();
    const int textW = MeasureTextWidth(hdc, (LPCWSTR)text, kActionTextPx);

    // 加号 + 文字作为一个整体水平居中。
    const Gdiplus::REAL groupW = kActionIconSize + kActionIconTextGap + (Gdiplus::REAL)textW;
    const Gdiplus::REAL groupX = (Gdiplus::REAL)x + ((Gdiplus::REAL)w - groupW) / 2.0f;
    const Gdiplus::REAL iconY  = btnY + (btnH - kActionIconSize) / 2.0f;

    // ---- GDI+ 形状层 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x, btnY, (Gdiplus::REAL)w, btnH, kActionRadius);
        Gdiplus::SolidBrush fill(IsHover() ? kActionHoverFill : kActionFill);
        g.FillPath(&fill, &path);
        Gdiplus::Pen border(kActionBorder, 1.0f);
        g.DrawPath(&border, &path);

        DrawLineIcon(g, kIconPlus, groupX, iconY, kActionIconSize,
                     kActionIconColor, kStrokeWidth);
    }

    // ---- GDI 文字层 ----
    RECT textRc = { (int)(groupX + kActionIconSize + kActionIconTextGap),
                    (int)btnY,
                    x + w, (int)(btnY + btnH) };
    DrawGdiText(hdc, (LPCWSTR)text, textRc, kActionTextPx, kActionTextColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

} // namespace balloonmain
