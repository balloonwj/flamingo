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

#include "GroupSideTab.h"

namespace balloonchatwnd {

namespace {

// ---- 配色 --------------------------------------------------------------

const COLORREF kActiveColor   = RGB(0xFF, 0x7A, 0x45);   // 选中文字 / 下划线
const COLORREF kInactiveColor = RGB(0x8A, 0x8A, 0x8A);   // 未选中文字

const int kTextPx     = 12;   // Tab 文字字号
const int kUnderlineH = 2;    // 选中下划线高度

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

GroupSideTab::GroupSideTab()
    : m_active(false)
{
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
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

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

} // namespace balloonchatwnd
