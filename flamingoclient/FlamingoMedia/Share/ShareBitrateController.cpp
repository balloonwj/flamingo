/**
 *  屏幕共享画面的码率自适应，ShareBitrateController.cpp
 *  规则说明见 ShareBitrateController.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareBitrateController.h"

namespace screenshare {

namespace {

//发送阻塞占周期的比例达到 1 / kBlockedRatioDivisor（即四分之一）算拥塞
const int64_t kBlockedRatioDivisor = 4;
//一个周期里关键帧请求达到这个次数算拥塞（新成员加入只会请求一两次）
const int kCongestedKeyRequests = 3;
//拥塞时把码率乘以 kDecreaseNum / kDecreaseDen（即 70%）
const int64_t kDecreaseNum = 7;
const int64_t kDecreaseDen = 10;
//连续这么多个周期不拥塞后开始回升
const int kCalmWindowsBeforeIncrease = 3;
//每次回升加最高码率的 1 / kIncreaseDivisor（即 10%）
const int kIncreaseDivisor = 10;

} // namespace

ShareBitrateController::ShareBitrateController(int maxBps, int minBps)
    : m_maxBps(maxBps)
    , m_minBps(minBps > maxBps ? maxBps : minBps)
    , m_currentBps(maxBps)
    , m_started(false)
    , m_windowStartMs(0)
    , m_blockedMs(0)
    , m_keyRequests(0)
    , m_calmWindows(0)
    , m_feedbackOn(false)
    , m_maxFeedbackMs(0)
    , m_lastFeedbackAt(0)
{
}

void ShareBitrateController::OnFeedbackDelay(int64_t delayMs, int64_t nowMs)
{
    m_feedbackOn = true;
    m_lastFeedbackAt = nowMs;
    const int64_t d = (delayMs < 0) ? 0 : delayMs;
    if (d > m_maxFeedbackMs)
        m_maxFeedbackMs = d;
}

void ShareBitrateController::OnFrameSent(int64_t blockedMs)
{
    if (blockedMs > 0)
        m_blockedMs += blockedMs;
}

void ShareBitrateController::OnKeyFrameRequests(int count)
{
    if (count > 0)
        m_keyRequests += count;
}

bool ShareBitrateController::Evaluate(int64_t nowMs, int& outBps)
{
    if (!m_started)
    {
        m_started = true;
        m_windowStartMs = nowMs;
        m_blockedMs = 0;
        m_keyRequests = 0;
        return false;
    }
    const int64_t elapsed = nowMs - m_windowStartMs;
    if (elapsed < kShareBitrateWindowMs)
        return false;

    bool congested = (m_blockedMs * kBlockedRatioDivisor >= elapsed) || (m_keyRequests >= kCongestedKeyRequests);
    //回执延迟（只在启用后算）：周期内最大延迟超过门限，或回执中断太久
    if (m_feedbackOn &&
        (m_maxFeedbackMs > kShareFeedbackCongestedDelayMs || nowMs - m_lastFeedbackAt > kShareFeedbackTimeoutMs))
        congested = true;
    m_windowStartMs = nowMs;
    m_blockedMs = 0;
    m_keyRequests = 0;
    m_maxFeedbackMs = 0;

    int target = m_currentBps;
    if (congested)
    {
        m_calmWindows = 0;
        target = static_cast<int>(static_cast<int64_t>(m_currentBps) * kDecreaseNum / kDecreaseDen);
        if (target < m_minBps)
            target = m_minBps;
    }
    else
    {
        //不拥塞的周期攒够之后每个周期都回升一步（计数停在门限上，不再增长）
        if (m_calmWindows < kCalmWindowsBeforeIncrease)
            ++m_calmWindows;
        if (m_calmWindows >= kCalmWindowsBeforeIncrease && m_currentBps < m_maxBps)
        {
            target = m_currentBps + m_maxBps / kIncreaseDivisor;
            if (target > m_maxBps)
                target = m_maxBps;
        }
    }
    if (target == m_currentBps)
        return false;
    m_currentBps = target;
    outBps = target;
    return true;
}

int ShareBitrateController::CurrentBps() const
{
    return m_currentBps;
}

void ShareBitrateController::SetRange(int maxBps, int minBps, int currentBps)
{
    if (maxBps <= 0)
        return;
    m_maxBps = maxBps;
    m_minBps = (minBps > maxBps) ? maxBps : minBps;
    int current = currentBps;
    if (current > m_maxBps)
        current = m_maxBps;
    if (current < m_minBps)
        current = m_minBps;
    m_currentBps = current;
    //新范围下重新攒不拥塞的周期，避免换范围后立刻按新上限的 10% 回升
    m_calmWindows = 0;
}

int ShareBitrateController::MaxBps() const
{
    return m_maxBps;
}

int ShareBitrateController::MinBps() const
{
    return m_minBps;
}

} // namespace screenshare
