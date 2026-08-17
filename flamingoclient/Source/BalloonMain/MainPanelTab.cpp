/**
 *  MainPanelTab.cpp
 *
 *  主面板图标 Tab 的绘制实现。形状（选中底色 / 底边 / 角标圆）用 GDI+
 *  抗锯齿，角标数字用 GDI DrawText（清晰）。
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
#pragma comment(lib, "gdiplus.lib")

#include "MainPanelTab.h"
#include "MainPanelIcons.h"

namespace balloonmain {

namespace {

// ---- 配色（取自 design main-panel.jsx 的 BigTab）------------------------

// 主题橙 #FF7A45（选中态图标 / 底边）。
const Gdiplus::Color kAccent(255, 0xFF, 0x7A, 0x45);
// 选中态浅橙底（design --accent-soft）。
const Gdiplus::Color kAccentSoft(255, 0xFF, 0xF0, 0xE9);
// 未选中态图标灰（design --text-muted #6B6B6B）。
const Gdiplus::Color kIconIdle(255, 0x6B, 0x6B, 0x6B);
// 未读角标红 #FF4D4F。
const Gdiplus::Color kBadgeRed(255, 0xFF, 0x4D, 0x4F);

// 角标数字色（白）。
const COLORREF kBadgeTextColor = RGB(255, 255, 255);

// 选中态底色 / 底边的 COLORREF 版本 —— 轴对齐矩形用 GDI FillRect 绘制，
// 不走 GDI+ 抗锯齿，避免边缘被柔化、看起来"模糊"。
const COLORREF kAccentRef     = RGB(0xFF, 0x7A, 0x45);
const COLORREF kAccentSoftRef = RGB(0xFF, 0xF0, 0xE9);

// ---- 几何常量 ----------------------------------------------------------

const Gdiplus::REAL kIconSize        = 21.0f;   // 图标显示尺寸
const Gdiplus::REAL kIconStrokeWidth = 2.0f;    // 图标描边宽度（viewBox 单位）
const int           kActiveBarHeight = 2;       // 选中态底边高度（像素）
const Gdiplus::REAL kBadgeDiameter   = 15.0f;   // 未读角标直径

// 用 GDI 绘制一段文字 —— DrawText 经系统 ClearType 光栅化，清晰锐利。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
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

} // anonymous namespace

MainPanelTab::MainPanelTab()
    : m_iconKind(kIconUser)
    , m_active(false)
    , m_badgeCount(0)
{
}

void MainPanelTab::SetIconKind(int kind)
{
    m_iconKind = kind;
    Invalidate();
}

void MainPanelTab::SetActive(bool active)
{
    if (m_active == active)
    {
        return;
    }
    m_active = active;
    Invalidate();
}

void MainPanelTab::SetBadgeCount(int count)
{
    m_badgeCount = (count > 0) ? count : 0;
    Invalidate();
}

void MainPanelTab::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const Gdiplus::REAL iconX = (Gdiplus::REAL)x + ((Gdiplus::REAL)w - kIconSize) / 2.0f;
    const Gdiplus::REAL iconY = (Gdiplus::REAL)y + ((Gdiplus::REAL)h - kIconSize) / 2.0f - 1.0f;

    // 角标圆心：图标右上角附近。
    const Gdiplus::REAL badgeX = iconX + kIconSize - 5.0f;
    const Gdiplus::REAL badgeY = (Gdiplus::REAL)y + 5.0f;

    // ---- 选中态底色 + 底边：GDI FillRect（轴对齐矩形不走 GDI+ 抗锯齿，
    //      避免边缘被柔化、底色看起来"模糊"）----
    if (m_active)
    {
        RECT rcBg = { x, y, x + w, y + h };
        HBRUSH hbg = ::CreateSolidBrush(kAccentSoftRef);
        ::FillRect(hdc, &rcBg, hbg);
        ::DeleteObject(hbg);

        RECT rcBar = { x, y + h - kActiveBarHeight, x + w, y + h };
        HBRUSH hbar = ::CreateSolidBrush(kAccentRef);
        ::FillRect(hdc, &rcBar, hbar);
        ::DeleteObject(hbar);
    }

    // ---- 角标文字与形状尺寸：超过 99 显示 "99+"；多位数把圆角标横向拉成
    //      胶囊（药丸）形，右边缘固定、向左扩展，避免越过 Tab 右沿 ----
    // 角标上限：超过此值显示 "99+"，不在小圆里塞 3 位以上数字。
    const int kBadgeMaxShown = 99;
    CString badgeStr;
    if (m_badgeCount > kBadgeMaxShown)
    {
        badgeStr = _T("99+");
    }
    else
    {
        badgeStr.Format(_T("%d"), m_badgeCount);
    }
    // 每多一个字符（超过 1 位）就把胶囊加宽这么多逻辑像素，容下数字。
    const Gdiplus::REAL kBadgeExtraPerChar = 7.0f;
    int badgeChars = badgeStr.GetLength();
    Gdiplus::REAL badgeW = kBadgeDiameter
        + (Gdiplus::REAL)(badgeChars > 1 ? (badgeChars - 1) : 0) * kBadgeExtraPerChar;
    // 右边缘对齐原小圆的右边缘（badgeX + kBadgeDiameter），向左加宽。
    Gdiplus::REAL badgeLeft = badgeX + kBadgeDiameter - badgeW;

    // ---- 图标 + 角标形状：曲线图形走 GDI+ 抗锯齿 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        DrawLineIcon(g, m_iconKind, iconX, iconY, kIconSize,
                     m_active ? kAccent : kIconIdle, kIconStrokeWidth);

        if (m_badgeCount > 0)
        {
            // 椭圆在 width==height 时即为圆；width>height 时自然呈横向胶囊。
            Gdiplus::SolidBrush badge(kBadgeRed);
            g.FillEllipse(&badge, badgeLeft, badgeY, badgeW, kBadgeDiameter);
        }
    }

    // ---- GDI 文字层：角标数字 ----
    if (m_badgeCount > 0)
    {
        RECT badgeRc = { (int)badgeLeft, (int)badgeY,
                         (int)(badgeLeft + badgeW),
                         (int)(badgeY + kBadgeDiameter) };
        DrawGdiText(hdc, (LPCWSTR)badgeStr, badgeRc, 10, true, kBadgeTextColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
}

} // namespace balloonmain
