/**
 *  GroupManageButton.cpp
 *
 *  群管理通用操作按钮的绘制实现。
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

#include "GroupManageButton.h"
#include "GroupManagePaint.h"

namespace balloongroupmanage {

namespace {

// 默认字号 / 圆角（像素）。
const int kDefaultTextPx  = 13;
const int kDefaultCornerR = 8;

// ---- 各风格配色 --------------------------------------------------------

// 主按钮：橙色实心。
const COLORREF kPrimaryFillNormal = RGB(0xFF, 0x7A, 0x45);
const COLORREF kPrimaryFillHover  = RGB(0xF0, 0x68, 0x2A);
const COLORREF kPrimaryText       = RGB(0xFF, 0xFF, 0xFF);

// 次按钮：白底 + 灰边。
const COLORREF kSecondaryFillNormal = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kSecondaryFillHover  = RGB(0xF6, 0xF5, 0xF2);
const COLORREF kSecondaryBorder     = RGB(0xD6, 0xD3, 0xCE);
const COLORREF kSecondaryText       = RGB(0x55, 0x55, 0x55);

// 危险实心：淡红底 + 红字。
const COLORREF kDangerFillNormal = RGB(0xFB, 0xE7, 0xE7);
const COLORREF kDangerFillHover  = RGB(0xF6, 0xD6, 0xD6);
const COLORREF kDangerText       = RGB(0xD9, 0x3F, 0x3F);

// 危险描边：透明底 + 红边 + 红字。
const COLORREF kDangerOutlineBorder = RGB(0xE6, 0xB4, 0xB4);
const COLORREF kDangerOutlineHover  = RGB(0xFB, 0xF0, 0xF0);

} // anonymous namespace

GroupManageButton::GroupManageButton()
    : m_style(kGmBtnPrimary)
    , m_textPx(kDefaultTextPx)
    , m_cornerR(kDefaultCornerR)
{
}

void GroupManageButton::SetButtonStyle(int style)
{
    m_style = style;
    Invalidate();
}

void GroupManageButton::SetTextPx(int px)
{
    if (px > 0)
    {
        m_textPx = px;
        Invalidate();
    }
}

void GroupManageButton::SetCornerRadius(int px)
{
    if (px >= 0)
    {
        m_cornerR = px;
        Invalidate();
    }
}

void GroupManageButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const bool hover = IsHover();

    COLORREF fill   = CLR_INVALID;
    COLORREF border = CLR_INVALID;
    COLORREF text   = kSecondaryText;
    int      weight = kGmFontNormal;

    switch (m_style)
    {
    case kGmBtnPrimary:
        fill   = hover ? kPrimaryFillHover : kPrimaryFillNormal;
        text   = kPrimaryText;
        weight = kGmFontSemiBold;
        break;

    case kGmBtnSecondary:
        fill   = hover ? kSecondaryFillHover : kSecondaryFillNormal;
        border = kSecondaryBorder;
        text   = kSecondaryText;
        weight = kGmFontNormal;
        break;

    case kGmBtnDanger:
        fill   = hover ? kDangerFillHover : kDangerFillNormal;
        text   = kDangerText;
        weight = kGmFontSemiBold;
        break;

    case kGmBtnDangerOutline:
        fill   = hover ? kDangerOutlineHover : CLR_INVALID;
        border = kDangerOutlineBorder;
        text   = kDangerText;
        weight = kGmFontNormal;
        break;

    default:
        break;
    }

    GmFillRoundRect(hdc, m_rcItem, (Gdiplus::REAL)m_cornerR,
                    fill, border, 1.0f);

    GmDrawText(hdc, (LPCWSTR)GetText(), m_rcItem, m_textPx, weight, text,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace balloongroupmanage
