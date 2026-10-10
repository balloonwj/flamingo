/**
 *  flamingoAdmin —— FlamingoLink.dll 页面窗口占位控件的实现，说明见 LinkPageSlot.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Widgets/LinkPageSlot.h"

namespace flamingoadmin { namespace ui {

LinkPageSlot::LinkPageSlot()
    : m_onBounds(nullptr)
    , m_onBoundsUser(nullptr)
{
}

void LinkPageSlot::SetBoundsCallback(BoundsFn fn, void* user)
{
    m_onBounds = fn;
    m_onBoundsUser = user;
}

void LinkPageSlot::Layout(const RECT& rcAvail)
{
    balloonwjui::DuiVBox::Layout(rcAvail);
    if (m_onBounds != nullptr)
    {
        m_onBounds(m_onBoundsUser, m_rcItem);
    }
}

}} // namespace flamingoadmin::ui
