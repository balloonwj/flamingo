/**
 *  GroupManageNavItem.cpp
 *
 *  左侧导航项的绘制实现：选中高亮底（GDI+ 圆角）+ 引导图标（GroupManageIcons）
 *  + 文字（GDI）+ 右侧计数徽章（圆角胶囊）。
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

#include "GroupManageNavItem.h"
#include "GroupManageIcons.h"
#include "GroupManagePaint.h"

namespace balloongroupmanage {

namespace {

// ---- 布局常量（像素）--------------------------------------------------

const int kHighlightInsetX = 6;    // 选中高亮底左右内缩
const int kHighlightInsetY = 3;    // 选中高亮底上下内缩
const int kHighlightRadius = 8;    // 选中高亮底圆角
const int kIconX           = 16;   // 图标左缘相对项左缘的偏移
const int kIconSize        = 16;   // 图标边长
const int kLabelX          = 42;   // 文字左缘相对项左缘的偏移
const int kLabelPx         = 12;   // 文字字号
const int kBadgeRightPad   = 16;   // 徽章右缘相对项右缘的内缩
const int kBadgeHeight      = 18;  // 徽章高度
const int kBadgeTextPx      = 11;  // 徽章文字字号
const int kBadgeHPad        = 7;   // 徽章文字左右内边距

// ---- 配色 --------------------------------------------------------------

const COLORREF kHighlightNormal = RGB(0xF1, 0xF0, 0xED);   // 选中高亮底（普通项）
const COLORREF kHighlightDanger = RGB(0xFC, 0xEC, 0xEC);   // 选中高亮底（危险项）

const COLORREF kIconActive   = RGB(0x33, 0x33, 0x33);   // 选中图标 / 文字
const COLORREF kIconNormal   = RGB(0x6E, 0x6E, 0x6E);   // 未选图标
const COLORREF kTextActive   = RGB(0x2A, 0x2A, 0x2A);   // 选中文字
const COLORREF kTextNormal   = RGB(0x46, 0x46, 0x46);   // 未选文字
const COLORREF kDangerColor  = RGB(0xD9, 0x3F, 0x3F);   // 危险项图标 / 文字

const COLORREF kBadgeAlertBg   = RGB(0xE5, 0x4B, 0x4B);   // 红色徽章底
const COLORREF kBadgeAlertText = RGB(0xFF, 0xFF, 0xFF);   // 红色徽章字
const COLORREF kBadgeGrayBg    = RGB(0xE7, 0xE6, 0xE2);   // 灰色徽章底
const COLORREF kBadgeGrayText  = RGB(0x8C, 0x8C, 0x8C);   // 灰色徽章字

// 测量一段文字在给定字号下的宽度（像素）。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
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
    HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOld);
    ::DeleteObject(hFont);
    return sz.cx;
}

} // anonymous namespace

GroupManageNavItem::GroupManageNavItem()
    : m_iconKind(0)
    , m_danger(false)
    , m_count(0)
    , m_alert(false)
    , m_active(false)
{
}

void GroupManageNavItem::SetNavInfo(int iconKind, LPCTSTR label, bool danger)
{
    m_iconKind = iconKind;
    m_label    = (label != NULL) ? label : _T("");
    m_danger   = danger;
    Invalidate();
}

void GroupManageNavItem::SetBadge(int count, bool alert)
{
    m_count = count;
    m_alert = alert;
    Invalidate();
}

void GroupManageNavItem::SetActive(bool active)
{
    m_active = active;
    Invalidate();
}

void GroupManageNavItem::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int left = m_rcItem.left;
    const int top  = m_rcItem.top;
    const int w    = m_rcItem.right  - m_rcItem.left;
    const int h    = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 选中高亮底。
    if (m_active)
    {
        RECT hi = { left + kHighlightInsetX, top + kHighlightInsetY,
                    m_rcItem.right - kHighlightInsetX,
                    m_rcItem.bottom - kHighlightInsetY };
        const COLORREF hiColor = m_danger ? kHighlightDanger : kHighlightNormal;
        GmFillRoundRect(hdc, hi, (Gdiplus::REAL)kHighlightRadius,
                        hiColor, CLR_INVALID, 0.0f);
    }

    // 图标。
    COLORREF iconColor;
    if (m_danger)
    {
        iconColor = kDangerColor;
    }
    else
    {
        iconColor = m_active ? kIconActive : kIconNormal;
    }
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::REAL iconY = (Gdiplus::REAL)(top + (h - kIconSize) / 2);
        DrawGmNavIcon(g, m_iconKind,
                      (Gdiplus::REAL)(left + kIconX), iconY,
                      (Gdiplus::REAL)kIconSize,
                      Gdiplus::Color(255, GetRValue(iconColor),
                                     GetGValue(iconColor), GetBValue(iconColor)),
                      2.0f);
    }

    // 计数徽章（先算出徽章占宽，再据此收缩文字可用区，避免文字与徽章重叠）。
    int badgeLeft = m_rcItem.right - kBadgeRightPad;
    if (m_count > 0)
    {
        CString countText;
        countText.Format(_T("%d"), m_count);
        const int textW = MeasureTextWidth(hdc, (LPCWSTR)countText, kBadgeTextPx);
        int badgeW = textW + kBadgeHPad * 2;
        if (badgeW < kBadgeHeight)
        {
            badgeW = kBadgeHeight;   // 单数字时保持近正方形
        }
        const int badgeRight = m_rcItem.right - kBadgeRightPad;
        badgeLeft = badgeRight - badgeW;
        const int badgeTop = top + (h - kBadgeHeight) / 2;
        RECT badgeRc = { badgeLeft, badgeTop, badgeRight, badgeTop + kBadgeHeight };

        const COLORREF bg   = m_alert ? kBadgeAlertBg : kBadgeGrayBg;
        const COLORREF fg   = m_alert ? kBadgeAlertText : kBadgeGrayText;
        GmFillRoundRect(hdc, badgeRc, (Gdiplus::REAL)(kBadgeHeight / 2),
                        bg, CLR_INVALID, 0.0f);
        GmDrawText(hdc, (LPCWSTR)countText, badgeRc, kBadgeTextPx,
                   kGmFontSemiBold, fg,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 文字。
    COLORREF textColor;
    if (m_danger)
    {
        textColor = kDangerColor;
    }
    else
    {
        textColor = m_active ? kTextActive : kTextNormal;
    }
    const int textWeight = m_active ? kGmFontSemiBold : kGmFontNormal;
    RECT textRc = { left + kLabelX, top, badgeLeft - 6, m_rcItem.bottom };
    GmDrawText(hdc, (LPCWSTR)m_label, textRc, kLabelPx, textWeight, textColor,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

} // namespace balloongroupmanage
