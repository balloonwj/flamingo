/**
 *  GroupSideTab.cpp
 *
 *  边栏 Tab 的绘制实现。文字与下划线均用 GDI（DrawText / FillRect），
 *  轴对齐、边缘锐利。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
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

#include "GroupSideTab.h"
#include "../../../third-party/balloonui/balloonui/DuiPaintAA.h"   // 角标的抗锯齿圆角块
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体

namespace balloonchatwnd {

namespace {

// ---- 配色 --------------------------------------------------------------

const COLORREF kActiveColor   = RGB(0xFF, 0x7A, 0x45);   // 选中文字 / 下划线
const COLORREF kInactiveColor = RGB(0x8A, 0x8A, 0x8A);   // 未选中文字

const int kTextPx     = 12;   // Tab 文字字号
const int kUnderlineH = 2;    // 选中下划线高度

// ---- 角标 ----
const COLORREF kBadgeBg    = RGB(0xF5, 0x3F, 0x3F);   // 角标红底
const COLORREF kBadgeFg    = RGB(0xFF, 0xFF, 0xFF);   // 角标白字
const int kBadgeH          = 14;   // 角标高
const int kBadgeMinW       = 14;   // 角标最小宽（一位数时是圆）
const int kBadgePadH       = 4;    // 角标文字左右留白
const int kBadgeTextPx     = 10;   // 角标文字字号
const int kBadgeGap        = 2;    // 角标与文字右上角的间距
const int kBadgeMaxShown   = 99;   // 超过它显示「99+」
const size_t kBadgeBufSize = 8;    // 角标文字缓冲区（宽字符个数）

// 用 GDI 绘制文字。
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

} // anonymous namespace

GroupSideTab::GroupSideTab()
    : m_active(false)
    , m_badgeCount(0)
{
}

void GroupSideTab::SetBadgeCount(int count)
{
    if (count < 0)
    {
        count = 0;
    }
    if (count == m_badgeCount)
    {
        return;
    }
    m_badgeCount = count;
    Invalidate();
}

void GroupSideTab::SetActive(bool active)
{
    m_active = active;
    Invalidate();
}

void GroupSideTab::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // Tab 文字 —— 选中用主题色加粗，未选中用灰色。
    const COLORREF textColor = m_active ? kActiveColor : kInactiveColor;
    DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, kTextPx, m_active, textColor,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 角标：文字右上方一个红底白字的圆角块
    if (m_badgeCount > 0)
    {
        wchar_t badge[kBadgeBufSize] = { 0 };
        if (m_badgeCount > kBadgeMaxShown)
        {
            wcscpy_s(badge, L"99+");
        }
        else
        {
            swprintf_s(badge, L"%d", m_badgeCount);
        }
        SIZE textSz = { 0, 0 };
        {
            HFONT hFont = ::CreateFontW(-kTextPx, 0, 0, 0, m_active ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
            HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
            const CString text = GetText();
            ::GetTextExtentPoint32W(hdc, (LPCWSTR)text, text.GetLength(), &textSz);
            ::SelectObject(hdc, hOld);
            ::DeleteObject(hFont);
        }
        SIZE badgeSz = { 0, 0 };
        {
            HFONT hFont = ::CreateFontW(-kBadgeTextPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
            HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
            ::GetTextExtentPoint32W(hdc, badge, (int)wcslen(badge), &badgeSz);
            ::SelectObject(hdc, hOld);
            ::DeleteObject(hFont);
        }
        int bw = badgeSz.cx + kBadgePadH * 2;
        if (bw < kBadgeMinW)
        {
            bw = kBadgeMinW;
        }
        const int cx = (m_rcItem.left + m_rcItem.right) / 2;
        const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;
        RECT rcBadge;
        rcBadge.left = cx + textSz.cx / 2 + kBadgeGap;
        rcBadge.right = rcBadge.left + bw;
        rcBadge.bottom = cy - textSz.cy / 2 + kBadgeH / 2 + kBadgeGap;
        rcBadge.top = rcBadge.bottom - kBadgeH;
        //贴着右缘放不下时整体左移，不让角标跑出 Tab
        if (rcBadge.right > m_rcItem.right)
        {
            const int shift = rcBadge.right - m_rcItem.right;
            rcBadge.left -= shift;
            rcBadge.right -= shift;
        }
        balloonwjui::DuiAA::FillRoundRect(hdc, rcBadge, kBadgeBg, kBadgeH / 2);
        DrawGdiText(hdc, badge, rcBadge, kBadgeTextPx, false, kBadgeFg,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 选中态：底部 2px 主题色下划线。
    if (m_active)
    {
        RECT bar = { m_rcItem.left, m_rcItem.bottom - kUnderlineH,
                     m_rcItem.right, m_rcItem.bottom };
        HBRUSH br = ::CreateSolidBrush(kActiveColor);
        ::FillRect(hdc, &bar, br);
        ::DeleteObject(br);
    }
}

int GroupSideTab::MeasureTabTextWidth(LPCWSTR text)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }
    //按选中态的字重量：选中的 Tab 字更粗、更宽，按它分配宽度就不会在切换选中时被截断
    HFONT hFont = ::CreateFontW(-kTextPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    HFONT hOldFont = (hFont != NULL) ? (HFONT)::SelectObject(hdc, hFont) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size);
    if (hFont != NULL)
    {
        ::SelectObject(hdc, hOldFont);
        ::DeleteObject(hFont);
    }
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

} // namespace balloonchatwnd
