/**
 *  flamingoAdmin —— 悬浮式 auto-hide 滚动条控件 OverlayScrollbar 实现。
 *
 *  绘制、拖动、点击轨道与淡入淡出全部由 balloonui DuiScrollBar 完成（2026-10-04
 *  起）；本类只把宿主的「内容总高 + 滚动偏移」换算成 DuiScrollBar 的「范围 +
 *  页大小 + 位置」，并把滚轮交还宿主。详见 OverlayScrollbar.h。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Widgets/OverlayScrollbar.h"
#include "Util/ScrollbarMetrics.h"

using namespace balloonwjui;

namespace flamingoadmin {

OverlayScrollbar::OverlayScrollbar()
    : DuiScrollBar(false)
{
}

void OverlayScrollbar::SetMetrics(int contentH, int scroll)
{
    m_contentH = contentH;
    m_scroll = scroll;
    ApplyMetrics();
}

void OverlayScrollbar::Flash()
{
    if (!ScrollbarNeeded(m_contentH, ViewH()))
    {
        return;   // 内容放得下，无需显示滚动条
    }
    TriggerShow();
}

void OverlayScrollbar::Layout(const RECT& rcAvail)
{
    DuiScrollBar::Layout(rcAvail);
    // 可视区高度取自本控件矩形，矩形变了滚动范围与页大小跟着变
    ApplyMetrics();
}

int OverlayScrollbar::ViewH() const
{
    return m_rcItem.bottom - m_rcItem.top;
}

void OverlayScrollbar::ApplyMetrics()
{
    const int viewH = ViewH();
    int maxScroll = m_contentH - viewH;
    if (maxScroll < 0)
    {
        maxScroll = 0;
    }
    // DuiScrollBar 的 SetPage 会把小于 1 的页大小抬到 1，这里按同样规则比较
    const int page = (viewH > 0) ? viewH : 1;

    // 只在数值确有变化时才设置。宿主常在自己的 OnPaint 里调 SetMetrics（内容总高要绘制时才算得出，
    // 如 OrgDetailPanel），而 DuiScrollBar 的 SetRange / SetPage 不比较新旧值、每次都会 Invalidate：
    // 照调不误的话，每绘制一次本控件就再失效一次，形成持续重绘。WM_PAINT 一直排在队列里，优先级
    // 更低的 WM_TIMER 就取不到，DuiAnimMgr 的动画因此停住，表现为顶部提示停在淡入的第一帧不消失
    //（2026-10-04 实测）。SetPos 内部已会跳过未变化的情况。
    // DuiScrollBar 的 max 是「最大有效滚动偏移」，页大小决定滑块长度
    if (GetMin() != 0 || GetMax() != maxScroll)
    {
        SetRange(0, maxScroll);
    }
    if (GetPage() != page)
    {
        SetPage(page);
    }
    SetPos(ClampScrollOffset(m_scroll, m_contentH, viewH), false);
}

balloonwjui::DuiControl* OverlayScrollbar::HitTest(POINT pt)
{
    // 内容放得下时整条「穿透」给宿主，使点击 / 悬停列表内容不被这条竖条拦截
    if (!ScrollbarNeeded(m_contentH, ViewH()))
    {
        return nullptr;
    }
    return DuiScrollBar::HitTest(pt);
}

bool OverlayScrollbar::OnMouseWheel(POINT /*pt*/, short /*zDelta*/, UINT /*mkFlags*/)
{
    return false;
}

} // namespace flamingoadmin
