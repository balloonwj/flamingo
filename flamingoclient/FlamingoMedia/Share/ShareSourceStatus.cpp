/**
 *  屏幕共享「共享内容状态」的计算，说明见 ShareSourceStatus.h。
 *  balloonwj@qq.com   2026-10-14
 */
#include "ShareSourceStatus.h"

#include "ShareCaptureRetry.h"            // share_capture_retry_report
#include "../../Source/net/Protocol.h"    // screenshare_source_state

namespace screenshare {

int ComputeShareSourceState(bool closed, bool paused, bool captureSuspended, bool minimized)
{
    if (closed)
        return screenshare_source_closed;
    if (paused || captureSuspended)
        return screenshare_source_paused;
    if (minimized)
        return screenshare_source_minimized;
    return screenshare_source_sharing;
}

ShareSourceStatus::ShareSourceStatus()
    : m_closed(false)
    , m_paused(false)
    , m_suspended(false)
    , m_minimized(false)
{
}

void ShareSourceStatus::OnSourceSwitched(bool capturerOk)
{
    //新内容的关闭与最小化状态要重新观察；采集器建好说明不再挂起，建不起来时挂起与否保持原样
    m_closed = false;
    m_minimized = false;
    if (capturerOk)
        m_suspended = false;
}

void ShareSourceStatus::OnRetryReport(int report)
{
    if (report == share_retry_report_paused)
        m_suspended = true;
    else if (report == share_retry_report_resumed)
        m_suspended = false;
}

void ShareSourceStatus::OnPauseChanged(bool paused)
{
    m_paused = paused;
}

void ShareSourceStatus::OnCaptureClosed()
{
    m_closed = true;
}

void ShareSourceStatus::OnMinimizedObserved(bool minimized)
{
    m_minimized = minimized;
}

int ShareSourceStatus::State() const
{
    return ComputeShareSourceState(m_closed, m_paused, m_suspended, m_minimized);
}

bool ShareSourceStatus::IsClosed() const
{
    return m_closed;
}

} // namespace screenshare
