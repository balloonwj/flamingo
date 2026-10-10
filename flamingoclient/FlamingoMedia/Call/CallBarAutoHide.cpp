/**
 *  视频通话操作条的自动隐藏时序（纯逻辑）的实现，CallBarAutoHide.cpp
 *  说明见 CallBarAutoHide.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CallBarAutoHide.h"

namespace call {

CallBarAutoHide::CallBarAutoHide()
{
    Reset(0);
}

void CallBarAutoHide::Reset(int64_t nowMs)
{
    m_lastActivityMs = nowMs;
    //淡入视为早已完成：完全显示
    m_fadeInStartMs = nowMs - kCallBarFadeInMs;
    m_fadeInFromAlpha = kCallBarAlphaOpaque;
    m_hold = false;
}

void CallBarAutoHide::OnActivity(int64_t nowMs)
{
    const int alpha = Alpha(nowMs);
    //未完全显示时从当前不透明度开始淡入；已完全显示时只重新计时，不重启淡入
    if (alpha < kCallBarAlphaOpaque)
    {
        m_fadeInStartMs = nowMs;
        m_fadeInFromAlpha = alpha;
    }
    m_lastActivityMs = nowMs;
}

void CallBarAutoHide::SetHold(bool hold, int64_t nowMs)
{
    if (hold == m_hold)
        return;
    if (hold)
    {
        //进入保持与一次活动相同：正在淡出时淡入回来
        OnActivity(nowMs);
        m_hold = true;
        return;
    }
    //解除保持：静止时长从此刻起算
    m_hold = false;
    m_lastActivityMs = nowMs;
}

int CallBarAutoHide::Alpha(int64_t nowMs) const
{
    const bool visibleSide = m_hold || (nowMs - m_lastActivityMs < kCallBarIdleMs);
    if (visibleSide)
    {
        //淡入阶段：从 m_fadeInFromAlpha 线性升到完全不透明
        const int64_t elapsed = nowMs - m_fadeInStartMs;
        if (elapsed >= kCallBarFadeInMs)
            return kCallBarAlphaOpaque;
        if (elapsed <= 0)
            return m_fadeInFromAlpha;
        const int64_t span = kCallBarAlphaOpaque - m_fadeInFromAlpha;
        return m_fadeInFromAlpha + static_cast<int>(span * elapsed / kCallBarFadeInMs);
    }
    //淡出阶段：静止时长到点后从完全不透明线性降到 0。静止时长远大于淡入时长，淡出开始时淡入早已完成
    const int64_t elapsed = nowMs - (m_lastActivityMs + kCallBarIdleMs);
    if (elapsed >= kCallBarFadeOutMs)
        return 0;
    return kCallBarAlphaOpaque - static_cast<int>(kCallBarAlphaOpaque * elapsed / kCallBarFadeOutMs);
}

int64_t CallBarAutoHide::MsUntilNextChange(int64_t nowMs) const
{
    const int alpha = Alpha(nowMs);
    const bool visibleSide = m_hold || (nowMs - m_lastActivityMs < kCallBarIdleMs);
    if (visibleSide)
    {
        //还在淡入
        if (alpha < kCallBarAlphaOpaque)
            return 0;
        //保持显示时不会淡出
        if (m_hold)
            return -1;
        return m_lastActivityMs + kCallBarIdleMs - nowMs;
    }
    //还在淡出；已完全隐藏则等下一次活动
    return (alpha > 0) ? 0 : -1;
}

} // namespace call
