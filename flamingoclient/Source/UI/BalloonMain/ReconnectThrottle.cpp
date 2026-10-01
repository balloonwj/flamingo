/**
 *  ReconnectThrottle.cpp —— 断线重连登录请求节流的实现。说明见 ReconnectThrottle.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "ReconnectThrottle.h"

namespace balloonmain {

ReconnectThrottle::ReconnectThrottle()
    : m_waiting(false)
    , m_waitedTicks(0)
{
}

void ReconnectThrottle::Reset()
{
    m_waiting = false;
    m_waitedTicks = 0;
}

ReconnectTickAction ReconnectThrottle::OnTick()
{
    //没有请求在途：本轮第一次，或上一个请求已经有了结果
    if (!m_waiting)
    {
        return kReconnectTickSend;
    }

    //有请求在途：在兜底次数以内一律等待；超过就认为那次请求已丢失，退出等待、重新发出
    ++m_waitedTicks;
    if (m_waitedTicks > kReconnectResultWaitTicks)
    {
        m_waiting = false;
        m_waitedTicks = 0;
        return kReconnectTickResendStale;
    }
    return kReconnectTickWait;
}

void ReconnectThrottle::OnRequestQueued()
{
    m_waiting = true;
    m_waitedTicks = 0;
}

void ReconnectThrottle::OnResult()
{
    m_waiting = false;
    m_waitedTicks = 0;
}

bool ReconnectThrottle::IsWaiting() const
{
    return m_waiting;
}

} // namespace balloonmain
