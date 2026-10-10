/**
 *  TrayConnectingBlink.cpp
 *
 *  CTrayConnectingBlink 的实现，类用途与用法见 TrayConnectingBlink.h。全部为两个成员变量的
 *  读写，无任何 Win32 / GDI 副作用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 OnlineCapability.cpp 同例。
#include "TrayConnectingBlink.h"

#include <windows.h>               // OnlineCapability.h 要求调用方先引入 windows.h
#include "OnlineCapability.h"      // ConnectionUiState（kConnUiOnline / kConnUiConnecting / kConnUiOffline）

namespace balloonmain {

CTrayConnectingBlink::CTrayConnectingBlink()
    : m_blinking(false)
    , m_frame(kTrayBlinkFrameGray)
{
}

TrayBlinkAction CTrayConnectingBlink::OnConnUiState(int connUiState)
{
    const bool connecting = (connUiState == kConnUiConnecting);
    if (connecting)
    {
        if (m_blinking)
        {
            return kTrayBlinkNone;
        }
        //第一帧取亮色：从离线恢复在线时，用户点下去立刻能看到图标有了变化
        m_blinking = true;
        m_frame = kTrayBlinkFrameBright;
        return kTrayBlinkStart;
    }

    //在线、离线及越界取值：不交替，停在该连接态的常态帧
    m_frame = RestingFrameFor(connUiState);
    if (!m_blinking)
    {
        return kTrayBlinkNone;
    }
    m_blinking = false;
    return kTrayBlinkStop;
}

TrayBlinkFrame CTrayConnectingBlink::Tick()
{
    if (m_blinking)
    {
        m_frame = (m_frame == kTrayBlinkFrameBright) ? kTrayBlinkFrameGray : kTrayBlinkFrameBright;
    }
    return m_frame;
}

bool CTrayConnectingBlink::IsBlinking() const
{
    return m_blinking;
}

TrayBlinkFrame CTrayConnectingBlink::CurrentFrame() const
{
    return m_frame;
}

TrayBlinkFrame CTrayConnectingBlink::RestingFrameFor(int connUiState)
{
    return (connUiState == kConnUiOnline) ? kTrayBlinkFrameBright : kTrayBlinkFrameGray;
}

CTrayUnreadHold::CTrayUnreadHold()
    : m_held(false)
    , m_resumePending(false)
{
}

TrayUnreadHoldAction CTrayUnreadHold::OnConnUiState(int connUiState)
{
    if (connUiState == kConnUiConnecting)
    {
        m_held = true;
        if (m_resumePending)
        {
            //还没等到恢复又断开了：这次计时作废，下次连上重新计时
            m_resumePending = false;
            return kTrayUnreadHoldCancelResume;
        }
        return kTrayUnreadHoldNone;
    }

    if (connUiState == kConnUiOnline)
    {
        if (m_held && !m_resumePending)
        {
            m_resumePending = true;
            return kTrayUnreadHoldStartResume;
        }
        return kTrayUnreadHoldNone;
    }

    //离线及越界取值：立即放行
    m_held = false;
    if (m_resumePending)
    {
        m_resumePending = false;
        return kTrayUnreadHoldCancelResume;
    }
    return kTrayUnreadHoldNone;
}

void CTrayUnreadHold::OnResumeTimer()
{
    if (!m_resumePending)
    {
        return;
    }
    m_resumePending = false;
    m_held = false;
}

bool CTrayUnreadHold::AllowUnread() const
{
    return !m_held;
}

bool CTrayUnreadHold::IsResumePending() const
{
    return m_resumePending;
}

} // namespace balloonmain
