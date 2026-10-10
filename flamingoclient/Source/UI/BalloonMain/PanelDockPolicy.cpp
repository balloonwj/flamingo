/**
 *  主面板贴边自动隐藏的收起 / 展开状态机实现，PanelDockPolicy.cpp
 *  规则与约定见 PanelDockPolicy.h。
 *
 *  本文件是纯逻辑，不 include stdafx.h（工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-02
 */

#include "PanelDockPolicy.h"

namespace balloonmain {

CPanelDockPolicy::CPanelDockPolicy()
    : m_edge(kPanelDockNone)
    , m_hidden(false)
    , m_armed(false)
    , m_leaving(false)
    , m_leaveStartTick(0)
{
}

void CPanelDockPolicy::Dock(PanelDockEdge edge)
{
    if (edge == kPanelDockNone)
    {
        Undock();
        return;
    }
    m_edge = edge;
    m_hidden = false;
    m_armed = true;
    m_leaving = false;
}

void CPanelDockPolicy::Undock()
{
    m_edge = kPanelDockNone;
    m_hidden = false;
    m_armed = false;
    m_leaving = false;
}

bool CPanelDockPolicy::NotifyShownExternally()
{
    if (m_edge == kPanelDockNone)
    {
        return false;
    }
    const bool wasHidden = m_hidden;
    m_hidden = false;
    m_armed = false;
    m_leaving = false;
    return wasHidden;
}

PanelDockAction CPanelDockPolicy::Tick(const PanelDockInput& in)
{
    if (m_edge == kPanelDockNone)
    {
        return kDockActionNone;
    }

    //开关被关掉：退出贴边状态，调用方把窗口放回吸附位置并取消置顶
    if (!in.m_enabled)
    {
        Undock();
        return kDockActionUndock;
    }

    //收起状态：只关心鼠标有没有碰到细条
    if (m_hidden)
    {
        if (in.m_cursorOver)
        {
            m_hidden = false;
            m_armed = true;
            m_leaving = false;
            return kDockActionShow;
        }
        return kDockActionNone;
    }

    //展开状态，鼠标在主面板上：记下已进入过，离开计时作废
    if (in.m_cursorOver)
    {
        m_armed = true;
        m_leaving = false;
        return kDockActionNone;
    }

    //鼠标在外面，但有需要主面板保持展开的情况：离开计时作废，等这些情况结束后重新计时
    if (in.m_mouseButtonDown || in.m_menuOpen || in.m_editingText || !m_armed)
    {
        m_leaving = false;
        return kDockActionNone;
    }

    if (!m_leaving)
    {
        m_leaving = true;
        m_leaveStartTick = in.m_nowTick;
        return kDockActionNone;
    }

    //无符号减法：GetTickCount 回绕后差值仍然正确
    if (in.m_nowTick - m_leaveStartTick >= kDockHideDelayMs)
    {
        m_hidden = true;
        m_armed = false;
        m_leaving = false;
        return kDockActionHide;
    }
    return kDockActionNone;
}

} // namespace balloonmain
