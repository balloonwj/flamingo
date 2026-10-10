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

#include "GroupManageNavItem.h"
#include "GroupManageIcons.h"
#include "GroupManagePaint.h"
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体
#include "../../Common/I18n/I18nLayout.h"   // i18n::MeasureUiTextWidth：按半粗体量导航文字宽度

namespace balloongroupmanage {

namespace {

// ---- 布局常量（96 DPI 下的像素，绘制时经 GmScale 换算）-------------------

const int kHighlightInsetX = 6;    // 选中卡片左右内缩
const int kHighlightInsetY = 3;    // 选中卡片上下内缩
const int kHighlightRadius = 8;    // 选中卡片圆角
const int kIconX           = 16;   // 图标左缘相对项左缘的偏移
const int kIconSize        = 16;   // 图标边长
const int kLabelX          = 42;   // 文字左缘相对项左缘的偏移
const int kBadgeRightPad   = 16;   // 徽章右缘相对项右缘的内缩
const int kBadgeHeight      = 18;  // 徽章高度
const int kBadgeHPad        = 7;   // 徽章文字左右内边距

// 导航项文字与计数徽章的字号取自字号表（GmTextStyleSpec）：导航项文字按条目标题，
// 计数按说明文字；计数徽章另用半粗字重，与设计稿一致。
const GmTextStyle kLabelStyle = kGmTextItemTitle;
const GmTextStyle kBadgeStyle = kGmTextCaption;
const int kLabelBadgeGap    = 6;   // 文字右缘与徽章左缘之间至少留出的距离

// 量导航项所需宽度时，计数徽章按这段文字的宽度预留（两位数；三位数时徽章略宽，文字以省略号结尾）
const wchar_t kBadgeReserveText[] = L"99";

// 图标描边宽度。单位是图标 24 格视图坐标（见 DrawGmNavIcon），已随图标边长缩放，
// 不再按 DPI 换算。
const float kIconStrokeWidth = 2.0f;

// ---- 配色（取自 design/群管理1/6/8.png）------------------------------------
// 选中项是浮在浅灰导航底上的白色圆角卡片加浅描边；普通项选中时只有图标变成品牌橙，
// 文字用深色加粗；危险项无论选中与否，图标与文字都是红色。

const COLORREF kHighlightBg     = RGB(0xFF, 0xFF, 0xFF);   // 选中卡片底
const COLORREF kHighlightBorder = RGB(0xEC, 0xEC, 0xEB);   // 选中卡片描边

const COLORREF kIconActive   = RGB(0xFF, 0x7A, 0x45);   // 选中图标（品牌橙）
const COLORREF kIconNormal   = RGB(0x9A, 0x9A, 0x9A);   // 未选图标
const COLORREF kTextActive   = RGB(0x1A, 0x1A, 0x1A);   // 选中文字
const COLORREF kTextNormal   = RGB(0x6B, 0x6B, 0x6B);   // 未选文字
const COLORREF kDangerColor  = RGB(0xEF, 0x44, 0x44);   // 危险项图标 / 文字

// 选中卡片描边宽度（像素）。恒为 1px 细线，不随 DPI 加粗。
const float kHighlightBorderWidth = 1.0f;

const COLORREF kBadgeAlertBg   = RGB(0xE5, 0x4B, 0x4B);   // 红色徽章底
const COLORREF kBadgeAlertText = RGB(0xFF, 0xFF, 0xFF);   // 红色徽章字
const COLORREF kBadgeGrayBg    = RGB(0xE7, 0xE6, 0xE2);   // 灰色徽章底
const COLORREF kBadgeGrayText  = RGB(0x8C, 0x8C, 0x8C);   // 灰色徽章字

// 测量一段文字在给定字号下的宽度（当前 DPI 下的像素）。
//   pixelHeight：96 DPI 下的字号，与 GmDrawText 一样经 GmFontPixelHeight 换算，
//                量出的宽度才与实际绘制一致。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -GmFontPixelHeight(pixelHeight), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
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

    // 各几何尺寸换算到当前 DPI。
    const int insetX     = GmScale(kHighlightInsetX);
    const int insetY     = GmScale(kHighlightInsetY);
    const int hiRadius   = GmScale(kHighlightRadius);
    const int iconX      = GmScale(kIconX);
    const int iconSize   = GmScale(kIconSize);
    const int labelX     = GmScale(kLabelX);
    const int badgeRPad  = GmScale(kBadgeRightPad);
    const int badgeH     = GmScale(kBadgeHeight);
    const int badgeHPad  = GmScale(kBadgeHPad);
    const int labelGap   = GmScale(kLabelBadgeGap);

    // 选中卡片：白底加浅描边，普通项与危险项相同。
    if (m_active)
    {
        RECT hi = { left + insetX, top + insetY,
                    m_rcItem.right - insetX,
                    m_rcItem.bottom - insetY };
        GmFillRoundRect(hdc, hi, (Gdiplus::REAL)hiRadius,
                        kHighlightBg, kHighlightBorder, kHighlightBorderWidth);
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
        const Gdiplus::REAL iconY = (Gdiplus::REAL)(top + (h - iconSize) / 2);
        DrawGmNavIcon(g, m_iconKind,
                      (Gdiplus::REAL)(left + iconX), iconY,
                      (Gdiplus::REAL)iconSize,
                      Gdiplus::Color(255, GetRValue(iconColor),
                                     GetGValue(iconColor), GetBValue(iconColor)),
                      kIconStrokeWidth);
    }

    // 计数徽章（先算出徽章占宽，再据此收缩文字可用区，避免文字与徽章重叠）。
    int badgeLeft = m_rcItem.right - badgeRPad;
    if (m_count > 0)
    {
        CString countText;
        countText.Format(_T("%d"), m_count);
        const int badgePx = GmTextStyleSpec(kBadgeStyle).m_px;
        const int textW = MeasureTextWidth(hdc, (LPCWSTR)countText, badgePx);
        int badgeW = textW + badgeHPad * 2;
        if (badgeW < badgeH)
        {
            badgeW = badgeH;   // 单数字时保持近正方形
        }
        const int badgeRight = m_rcItem.right - badgeRPad;
        badgeLeft = badgeRight - badgeW;
        const int badgeTop = top + (h - badgeH) / 2;
        RECT badgeRc = { badgeLeft, badgeTop, badgeRight, badgeTop + badgeH };

        const COLORREF bg   = m_alert ? kBadgeAlertBg : kBadgeGrayBg;
        const COLORREF fg   = m_alert ? kBadgeAlertText : kBadgeGrayText;
        GmFillRoundRect(hdc, badgeRc, (Gdiplus::REAL)(badgeH / 2),
                        bg, CLR_INVALID, 0.0f);
        GmDrawText(hdc, (LPCWSTR)countText, badgeRc, badgePx,
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
    const GmTextSpec labelSpec = GmTextStyleSpec(kLabelStyle);
    //选中项加粗，其余按字号表的字重
    const int textWeight = m_active ? kGmFontSemiBold : labelSpec.m_weight;
    RECT textRc = { left + labelX, top, badgeLeft - labelGap, m_rcItem.bottom };
    GmDrawText(hdc, (LPCWSTR)m_label, textRc, labelSpec.m_px, textWeight, textColor,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

int GroupManageNavItem::MeasureRequiredWidth(bool reserveBadge) const
{
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }

    // 文字：选中项是半粗体，按半粗体量，选中与否都放得下
    const GmTextSpec labelSpec = GmTextStyleSpec(kLabelStyle);
    const int labelW = i18n::MeasureUiTextWidth((LPCWSTR)m_label, GmFontPixelHeight(labelSpec.m_px), true);

    // 徽章：与 OnPaint 的算法相同，宽度不足高度时取高度
    int badgeW = 0;
    if (m_count > 0 || reserveBadge)
    {
        CString countText;
        countText.Format(_T("%d"), m_count);
        const int badgePx = GmTextStyleSpec(kBadgeStyle).m_px;
        int textW = MeasureTextWidth(hdc, (LPCWSTR)countText, badgePx);
        if (reserveBadge)
        {
            const int reserveW = MeasureTextWidth(hdc, kBadgeReserveText, badgePx);
            if (reserveW > textW)
            {
                textW = reserveW;
            }
        }
        badgeW = textW + GmScale(kBadgeHPad) * 2;
        if (badgeW < GmScale(kBadgeHeight))
        {
            badgeW = GmScale(kBadgeHeight);
        }
    }
    ::ReleaseDC(NULL, hdc);

    return GmScale(kLabelX) + labelW + GmScale(kLabelBadgeGap) + badgeW + GmScale(kBadgeRightPad);
}

} // namespace balloongroupmanage
