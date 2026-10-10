/**
 *  MemoCalendarControls.cpp —— 备忘窗口的月历格子，见 MemoCalendarControls.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （备忘窗口的布局单测要建出完整的控件树）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 RelayComposeLayout.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
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

#include "MemoCalendarControls.h"
#include "../../../third-party/balloonui/balloonui/DuiPaintAA.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"

using namespace balloonwjui;

namespace balloonmemo {

namespace {

// ---- 颜色 ----
const COLORREF kBrand          = RGB(0x1E, 0x6F, 0xFF);   // 选中实心圆、今天描边圆、圆点
const COLORREF kTextInMonth    = RGB(0x2A, 0x2A, 0x2A);   // 本月的数字
const COLORREF kTextOutMonth   = RGB(0xBB, 0xBB, 0xBB);   // 上个月末、下个月初的数字
const COLORREF kTextSelected   = RGB(0xFF, 0xFF, 0xFF);   // 选中那天的数字与圆点
const COLORREF kHoverBg        = RGB(0xF0, 0xF2, 0xF5);   // 悬停时的浅底

// ---- 尺寸（96 DPI 下的逻辑像素，绘制时按控件 DPI 换算）----
const int kLogicalDpi     = 96;    // 逻辑像素对应的 DPI
const int kCircleDiameter = 30;    // 选中 / 今天那个圆的直径
const int kDotDiameter    = 4;     // 有备忘时的圆点直径
const int kDotGap         = 2;     // 圆点与圆下缘的间距（圆点画在圆里面、数字下方）
const int kDayTextPt      = 10;    // 数字的磅值
const int kHoverRadius    = 6;     // 悬停浅底的圆角
const float kTodayRingWidth = 1.5f; // 今天那个描边圆的线宽
const size_t kDayTextBuf  = 8;     // 几号的文字缓冲区（宽字符个数）
const int kDecimalRadix   = 10;    // 几号按十进制显示

int Scale(int px, int dpi)
{
    return ::MulDiv(px, dpi, kLogicalDpi);
}

//在 rc 里按 flags 画一段文字，字体按磅值与 DPI 取
void DrawTextIn(HDC hdc, const std::wstring& text, const RECT& rc, COLORREF color, int pt, bool bold, int dpi, UINT flags)
{
    HFONT font = DuiResMgr::Inst().GetFontByPointSizeForDpi(pt, bold, dpi);
    HGDIOBJ old = ::SelectObject(hdc, font);
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text.c_str(), (int)text.size(), &r, flags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, old);
}

} // anonymous namespace

// ---------------- CMemoDayButton ----------------

CMemoDayButton::CMemoDayButton()
{
    SetButtonType(DuiButton::StylePushButton);
    SetVariant(DuiButton::Variant::Ghost);
}

void CMemoDayButton::SetCell(const CalendarCell& cell)
{
    m_cell = cell;
    wchar_t buf[kDayTextBuf] = { 0 };
    _itow_s(cell.m_dayOfMonth, buf, kDecimalRadix);
    SetText(buf);
    Invalidate();
}

const CalendarCell& CMemoDayButton::Cell() const
{
    return m_cell;
}

void CMemoDayButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int dpi = GetDpi();
    const RECT rc = m_rcItem;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return;

    int d = Scale(kCircleDiameter, dpi);
    if (d > w)
        d = w;
    if (d > h)
        d = h;
    RECT circle;
    circle.left = rc.left + (w - d) / 2;
    circle.top = rc.top + (h - d) / 2;
    circle.right = circle.left + d;
    circle.bottom = circle.top + d;

    if (m_cell.m_isSelected)
    {
        DuiAA::FillEllipse(hdc, circle, kBrand);
    }
    else
    {
        if (IsHover() && IsEnabled())
            DuiAA::FillRoundRect(hdc, circle, kHoverBg, Scale(kHoverRadius, dpi));
        if (m_cell.m_isToday)
            DuiAA::FillEllipse(hdc, circle, CLR_INVALID, kBrand, kTodayRingWidth);
    }

    COLORREF color = m_cell.m_inMonth ? kTextInMonth : kTextOutMonth;
    if (m_cell.m_isToday)
        color = kBrand;
    if (m_cell.m_isSelected)
        color = kTextSelected;
    DrawTextIn(hdc, std::wstring(GetText()), circle, color, kDayTextPt, m_cell.m_isToday || m_cell.m_isSelected,
               dpi, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (m_cell.m_hasEvent)
    {
        const int dot = Scale(kDotDiameter, dpi);
        RECT rcDot;
        rcDot.left = circle.left + (d - dot) / 2;
        rcDot.right = rcDot.left + dot;
        rcDot.bottom = circle.bottom - Scale(kDotGap, dpi) - dot / 2;
        rcDot.top = rcDot.bottom - dot;
        DuiAA::FillEllipse(hdc, rcDot, m_cell.m_isSelected ? kTextSelected : kBrand);
    }
}

} // namespace balloonmemo
