/**
 *  屏幕共享采集丢失后的重试策略（纯逻辑），说明见 ShareCaptureRetry.h。
 *  balloonwj@qq.com   2026-10-12
 */
#include "ShareCaptureRetry.h"

namespace screenshare {

namespace {

//每次失败后间隔乘以这个倍数
const int64_t kRetryBackoffFactor = 2;

} // namespace

ShareCaptureRetry::ShareCaptureRetry(int64_t intervalMs, int64_t maxIntervalMs)
    : m_firstIntervalMs(intervalMs > 0 ? intervalMs : kShareCaptureRetryIntervalMs)
    , m_maxIntervalMs(0)
    , m_intervalMs(0)
    , m_active(false)
    , m_reported(false)
    , m_nextMs(0)
    , m_attempts(0)
{
    m_maxIntervalMs = (maxIntervalMs > m_firstIntervalMs) ? maxIntervalMs : m_firstIntervalMs;
    m_intervalMs = m_firstIntervalMs;
}

void ShareCaptureRetry::OnLost(int64_t nowMs)
{
    if (m_active)
        return;
    m_active = true;
    m_reported = false;
    m_attempts = 0;
    m_intervalMs = m_firstIntervalMs;
    m_nextMs = nowMs + m_intervalMs;
}

bool ShareCaptureRetry::Active() const
{
    return m_active;
}

bool ShareCaptureRetry::Due(int64_t nowMs) const
{
    return m_active && nowMs >= m_nextMs;
}

int ShareCaptureRetry::OnAttempt(bool ok, int64_t nowMs)
{
    if (!m_active)
        return share_retry_report_none;
    ++m_attempts;
    if (ok)
    {
        //恢复：报告过暂停才需要报告恢复
        const bool reported = m_reported;
        m_active = false;
        m_reported = false;
        return reported ? share_retry_report_resumed : share_retry_report_none;
    }
    //失败：间隔翻倍（不超过最长间隔）后再试；第一次失败时报告暂停
    m_intervalMs *= kRetryBackoffFactor;
    if (m_intervalMs > m_maxIntervalMs)
        m_intervalMs = m_maxIntervalMs;
    m_nextMs = nowMs + m_intervalMs;
    if (!m_reported)
    {
        m_reported = true;
        return share_retry_report_paused;
    }
    return share_retry_report_none;
}

int ShareCaptureRetry::Attempts() const
{
    return m_attempts;
}

void ShareCaptureRetry::Reset()
{
    m_active = false;
    m_reported = false;
    m_attempts = 0;
    m_intervalMs = m_firstIntervalMs;
    m_nextMs = 0;
}

} // namespace screenshare
