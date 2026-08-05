/**
 *  「消息盒子」公告列表行实现，AnnounceRow.cpp
 *  设计与约定见 AnnounceRow.h。
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"
#include "AnnounceRow.h"

#include <gdiplus.h>

#include "../BalloonMain/MainPanelScrollView.h"   // ForwardWheelToScrollView（滚轮上抛）

namespace balloonannounce {

// ---- 配色（COLORREF）----
//悬停底色：极淡的暖灰，与 HistoryRow 同色系，两处列表的手感一致。
static const COLORREF kRowHoverBg    = RGB(0xF6, 0xF4, 0xF0);
//选中底色：比悬停色再实一档的暖白，配合左侧竖条足以看出"我在看这条"。
static const COLORREF kRowSelectedBg = RGB(0xFD, 0xF0, 0xE8);
//选中行左侧那道强调竖条（主题橙），与主面板会话列表的置顶竖条同色。
static const COLORREF kRowAccentBar  = RGB(0xF2, 0x6B, 0x3E);
//未读小圆点（红），与各处未读角标同色。
static const COLORREF kRowUnreadDot  = RGB(0xE5, 0x3E, 0x3E);

// ---- 级别色：决定未读圆点与左侧竖条的色调 ----
//普通公告不做强调，用中性灰；提醒用琥珀；警告用红。取值对应 announcement_severity。
static const COLORREF kSeverityInfo    = RGB(0x9A, 0x98, 0x93);
static const COLORREF kSeverityNotice  = RGB(0xE6, 0x8A, 0x2E);
static const COLORREF kSeverityWarning = RGB(0xE5, 0x3E, 0x3E);

// ---- 尺寸（像素）----
static const int kAccentBarW   = 3;    // 选中行左侧强调竖条宽
static const int kUnreadDotD   = 7;    // 未读圆点直径
static const int kUnreadDotX   = 6;    // 未读圆点圆心距行左边的距离（预留给它的槽位中点）
static const int kSeverityBarW = 3;    // 未选中行的级别色条宽（仅"提醒/警告"两级才画）

//按级别取色。越界值按"普通"处理。
static COLORREF SeverityColor(int severity)
{
    //取值与 net/Protocol.h 的 announcement_severity 一一对应：0 普通 1 提醒 2 警告。
    if (severity == 1)
    {
        return kSeverityNotice;
    }
    if (severity == 2)
    {
        return kSeverityWarning;
    }
    return kSeverityInfo;
}

AnnounceRow::AnnounceRow()
    : m_hovered(false)
    , m_selected(false)
    , m_unread(false)
    , m_severity(0)
{
}

void AnnounceRow::SetHovered(bool hovered)
{
    //重复设同一个值时不重绘：鼠标在同一行内移动会连发很多次 WM_MOUSEMOVE，
    //每次都重绘的话整列都在闪。
    if (m_hovered == hovered)
    {
        return;
    }
    m_hovered = hovered;
    Invalidate();
}

void AnnounceRow::SetSelected(bool selected)
{
    if (m_selected == selected)
    {
        return;
    }
    m_selected = selected;
    Invalidate();
}

void AnnounceRow::OnPaint(HDC hdc, const RECT& rcDirty)
{
    const RECT rc = GetRect();

    // 1. 底色。选中态优先于悬停态 —— 正在看的那条即便鼠标移开也该保持高亮。
    if (m_selected || m_hovered)
    {
        const COLORREF bg = m_selected ? kRowSelectedBg : kRowHoverBg;
        HBRUSH brush = ::CreateSolidBrush(bg);
        ::FillRect(hdc, &rc, brush);
        ::DeleteObject(brush);
    }

    // 2. 左侧竖条。选中行画主题橙的强调条；未选中但级别为"提醒 / 警告"的画级别色条，
    //    让重要公告在一列里自己跳出来。普通级别不画，避免整列都是竖条反而没有重点。
    if (m_selected)
    {
        RECT bar = rc;
        bar.right = bar.left + kAccentBarW;
        HBRUSH brush = ::CreateSolidBrush(kRowAccentBar);
        ::FillRect(hdc, &bar, brush);
        ::DeleteObject(brush);
    }
    else if (m_severity == 1 || m_severity == 2)
    {
        RECT bar = rc;
        bar.right = bar.left + kSeverityBarW;
        HBRUSH brush = ::CreateSolidBrush(SeverityColor(m_severity));
        ::FillRect(hdc, &bar, brush);
        ::DeleteObject(brush);
    }

    // 3. 未读圆点，画在行左侧预留的槽位里（布局侧已用左内边距把文字让开）。
    //    用 GDI+ 画圆并开抗锯齿 —— GDI 的 Ellipse 在这个尺寸上边缘毛刺明显。
    if (m_unread)
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        const COLORREF dot = kRowUnreadDot;
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(dot), GetGValue(dot), GetBValue(dot)));

        const Gdiplus::REAL cx = (Gdiplus::REAL)(rc.left + kUnreadDotX);
        const Gdiplus::REAL cy = (Gdiplus::REAL)((rc.top + rc.bottom) / 2);
        const Gdiplus::REAL r  = (Gdiplus::REAL)kUnreadDotD / 2.0f;
        g.FillEllipse(&brush, cx - r, cy - r, (Gdiplus::REAL)kUnreadDotD, (Gdiplus::REAL)kUnreadDotD);
    }

    // 4. 底色与装饰画完再交基类画子控件（标题、发布者·时间两行 Label）。
    //    顺序不能反：反过来底色会把刚画好的文字盖掉。
    balloonwjui::DuiHBox::OnPaint(hdc, rcDirty);
}

bool AnnounceRow::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    //DuiHBox 只是布局容器，自己不消化滚轮，直接上抛给祖先里的滚动视口。
    return balloonmain::ForwardWheelToScrollView(this, pt, zDelta, mkFlags);
}

bool AnnounceWheelLabel::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    //DuiLabel 自己不消化滚轮，直接上抛给祖先里的滚动视口。
    return balloonmain::ForwardWheelToScrollView(this, pt, zDelta, mkFlags);
}

} // namespace balloonannounce
