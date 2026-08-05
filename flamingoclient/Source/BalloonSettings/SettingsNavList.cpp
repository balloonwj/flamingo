/**
 *  SettingsNavList.cpp —— 系统设置左侧圆角胶囊导航实现。
 *
 *  圆角胶囊用 balloonui 现成的 DuiVBox::PaintBackground（抗锯齿圆角填充）绘制，
 *  避免自写 RoundRect；文字用 DuiResMgr 默认字体。配色取自设计稿橙色系。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"
#include "SettingsNavList.h"

#include "../../balloonui/balloonui/DuiResMgr.h"                 // 默认字体
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h" // DuiVBox::PaintBackground

namespace balloonsettings {

// ---- 行尺寸（逻辑像素）-------------------------------------------------
static const int kNavItemH    = 40;   // 单行高度（较初版收紧）

// ---- 胶囊几何（相对行矩形的内缩量，逻辑像素）--------------------------
static const int kPillInsetX  = 10;   // 胶囊左右各内缩，形成与列表边的留白
static const int kPillInsetY  = 4;    // 胶囊上下各内缩，形成行间留白
static const int kPillRadius  = 8;    // 胶囊圆角半径
static const int kTextLeftPad = 24;   // 标题文字相对行左边的左内距（含胶囊留白 + 呼吸感）

// ---- 配色（取自 design/系统设置*.png 橙色系）--------------------------
static const COLORREF kAccentOrange  = RGB(0xF5, 0x6A, 0x3C);   // 选中文字（品牌橙）
static const COLORREF kSelPillBg     = RGB(0xFD, 0xED, 0xE4);   // 选中胶囊浅橙底
static const COLORREF kHoverPillBg   = RGB(0xF4, 0xF4, 0xF6);   // hover 胶囊浅灰底
static const COLORREF kTextNormalClr = RGB(0x44, 0x44, 0x48);   // 普通行文字深灰

CSettingsNavList::CSettingsNavList()
{
    SetRowHeight(kNavItemH);
    // 注册行绘制回调：user 传 this，PaintRowThunk 再转回成员 PaintRow。
    SetPaintRowCallback(&CSettingsNavList::PaintRowThunk, this);
}

void CSettingsNavList::SetTitles(const std::vector<CString>& titles)
{
    m_titles = titles;
    SetRowCount((int)m_titles.size());
}

void CSettingsNavList::PaintRowThunk(void* user, HDC hdc, int rowIndex,
                                     const RECT& rowRect, bool selected, bool hover)
{
    CSettingsNavList* self = static_cast<CSettingsNavList*>(user);
    if (self != NULL)
    {
        self->PaintRow(hdc, rowIndex, rowRect, selected, hover);
    }
}

void CSettingsNavList::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect,
                                bool selected, bool hover)
{
    if (rowIndex < 0 || rowIndex >= (int)m_titles.size())
    {
        return;
    }

    // 胶囊底色：选中 = 浅橙；hover（未选中）= 浅灰；其余不画。
    COLORREF pillBg = CLR_INVALID;
    if (selected)
    {
        pillBg = kSelPillBg;
    }
    else if (hover)
    {
        pillBg = kHoverPillBg;
    }

    if (pillBg != CLR_INVALID)
    {
        RECT pill;
        pill.left   = rowRect.left + kPillInsetX;
        pill.top    = rowRect.top + kPillInsetY;
        pill.right  = rowRect.right - kPillInsetX;
        pill.bottom = rowRect.bottom - kPillInsetY;
        balloonwjui::DuiVBox::PaintBackground(hdc, pill, pillBg, kPillRadius);
    }

    // 标题文字：选中橙、其余深灰；左对齐 + 垂直居中。
    HFONT hOldFont = (HFONT)::SelectObject(hdc, balloonwjui::DuiResMgr::Inst().GetDefaultFont());
    int oldBk = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF txtColor = selected ? kAccentOrange : kTextNormalClr;
    COLORREF oldClr = ::SetTextColor(hdc, txtColor);

    RECT rcText = rowRect;
    rcText.left += kTextLeftPad;
    rcText.right -= kPillInsetX;
    ::DrawText(hdc, m_titles[(size_t)rowIndex].GetString(), -1, &rcText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    ::SetTextColor(hdc, oldClr);
    ::SetBkMode(hdc, oldBk);
    ::SelectObject(hdc, hOldFont);
}

} // namespace balloonsettings
