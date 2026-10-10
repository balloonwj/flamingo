/**
 *  顶部浮动工具条的「收起 / 悬停展开」状态，ShareTopBarCollapse.cpp
 *  规则说明见 ShareTopBarCollapse.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareTopBarCollapse.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

TopBarCollapse::TopBarCollapse()
    : m_state(top_bar_pinned)
    , m_lastOverMs(0)
{
}

int TopBarCollapse::State() const
{
    return m_state;
}

void TopBarCollapse::Reset()
{
    m_state = top_bar_pinned;
    m_lastOverMs = 0;
}

bool TopBarCollapse::OnToggleClicked()
{
    //一直展开 → 收起
    if (m_state == top_bar_pinned)
    {
        m_state = top_bar_collapsed;
        return true;
    }
    //临时展开、从隐藏临时显示时按钮是「固定」→ 一直展开
    if (m_state == top_bar_peek || m_state == top_bar_hidden_peek)
    {
        m_state = top_bar_pinned;
        return true;
    }
    return false;
}

bool TopBarCollapse::OnHideClicked()
{
    //整条显示时才有「隐藏」按钮
    if (!TopBarShowsBar(m_state))
        return false;
    m_state = top_bar_hidden;
    return true;
}

bool TopBarCollapse::OnTick(int64_t nowMs, bool overHandle, bool overBar)
{
    return OnTick(nowMs, overHandle, overBar, false);
}

bool TopBarCollapse::OnTick(int64_t nowMs, bool overHandle, bool overBar, bool overEdge)
{
    //隐藏：鼠标到了屏幕边缘的感应区就临时显示，从此刻起算离开时长
    if (m_state == top_bar_hidden)
    {
        if (!overEdge)
            return false;
        m_state = top_bar_hidden_peek;
        m_lastOverMs = nowMs;
        return true;
    }
    //从隐藏临时显示：鼠标在工具条上或仍在感应区里就刷新时刻，离开满 kTopBarPeekHideMs 回到隐藏
    if (m_state == top_bar_hidden_peek)
    {
        if (overBar || overEdge)
        {
            m_lastOverMs = nowMs;
            return false;
        }
        if (nowMs - m_lastOverMs < kTopBarPeekHideMs)
            return false;
        m_state = top_bar_hidden;
        return true;
    }
    //收起：鼠标移到把手上就临时展开，从此刻起算离开时长
    if (m_state == top_bar_collapsed)
    {
        if (!overHandle)
            return false;
        m_state = top_bar_peek;
        m_lastOverMs = nowMs;
        return true;
    }
    //临时展开：鼠标在工具条上就刷新时刻，离开满 kTopBarPeekHideMs 收回
    if (m_state == top_bar_peek)
    {
        if (overBar)
        {
            m_lastOverMs = nowMs;
            return false;
        }
        if (nowMs - m_lastOverMs < kTopBarPeekHideMs)
            return false;
        m_state = top_bar_collapsed;
        return true;
    }
    return false;
}

const wchar_t* TopBarCollapse::ToggleText() const
{
    return (m_state == top_bar_peek || m_state == top_bar_hidden_peek) ? TopBarPinText() : TopBarCollapseText();
}

bool TopBarShowsBar(int state)
{
    return state == top_bar_pinned || state == top_bar_peek || state == top_bar_hidden_peek;
}

bool TopBarShowsHandle(int state)
{
    return state == top_bar_collapsed;
}

RECT ComputeHiddenEdgeZone(const RECT& bar, const RECT& monitor)
{
    RECT z;
    //横向：工具条的范围左右各放宽一段，不超出显示器
    z.left = bar.left - kHiddenEdgeSlackPx;
    z.right = bar.right + kHiddenEdgeSlackPx;
    if (z.left < monitor.left)
        z.left = monitor.left;
    if (z.right > monitor.right)
        z.right = monitor.right;
    //纵向：工具条离顶边近取顶边，否则取底边
    const int toTop = bar.top - monitor.top;
    const int toBottom = monitor.bottom - bar.bottom;
    if (toTop <= toBottom)
    {
        z.top = monitor.top;
        z.bottom = monitor.top + kHiddenEdgeZonePx;
    }
    else
    {
        z.top = monitor.bottom - kHiddenEdgeZonePx;
        z.bottom = monitor.bottom;
    }
    return z;
}

const wchar_t* TopBarHideText()
{
    return TR(L"隐藏（鼠标移到屏幕边缘时重新出现）");
}

const wchar_t* TopBarCollapseText()
{
    return TR(L"收起");
}

const wchar_t* TopBarPinText()
{
    return TR(L"固定");
}

RECT ComputeTopBarHandleRect(const RECT& bar)
{
    RECT h;
    h.left = (bar.left + bar.right) / 2 - kTopBarHandleW / 2;
    h.top = bar.top;
    h.right = h.left + kTopBarHandleW;
    h.bottom = h.top + kTopBarHandleH;
    return h;
}

} // namespace screenshare
