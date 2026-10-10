/**
 *  SubtitleState.cpp
 *
 *  字幕窗口交互状态的实现，说明见 SubtitleState.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "SubtitleState.h"

namespace subtitle {

namespace {

//取两者中较大的时刻
uint64_t LaterOf(uint64_t a, uint64_t b)
{
    return (a > b) ? a : b;
}

} // anonymous namespace

CSubtitleState::CSubtitleState()
    : m_locked(false)
    , m_part(kPartNone)
    , m_inside(false)
    , m_enterAt(0)
    , m_leaveAt(0)
    , m_releaseAt(0)
    , m_dragging(false)
    , m_menuOpen(false)
    , m_chromeAlpha(0)
    , m_lockAlpha(0)
    , m_lastTick(0)
{
}

void CSubtitleState::SetLocked(bool locked, uint64_t nowMs)
{
    advance(nowMs);
    if (locked == m_locked)
        return;
    m_locked = locked;
    //锁定、解锁都立刻切换：锁定后不再画底色，解锁后小锁按钮立刻消失、底色按悬停重新淡入
    m_chromeAlpha = 0;
    m_lockAlpha = 0;
    if (locked)
    {
        m_dragging = false;
        m_menuOpen = false;
        //光标此刻就在字幕上时，小锁按钮的出现延时从锁定这一刻算起
        m_enterAt = nowMs;
    }
}

bool CSubtitleState::IsLocked() const
{
    return m_locked;
}

void CSubtitleState::OnCursor(int part, uint64_t nowMs)
{
    advance(nowMs);
    const bool inside = (part != kPartNone);
    if (inside && !m_inside)
        m_enterAt = nowMs;
    if (!inside && m_inside)
    {
        m_leaveAt = nowMs;
        if (!m_dragging && !m_menuOpen)
            m_releaseAt = nowMs;
    }
    m_inside = inside;
    m_part = part;
}

void CSubtitleState::SetDragging(bool dragging, uint64_t nowMs)
{
    advance(nowMs);
    if (m_dragging && !dragging && !m_inside && !m_menuOpen)
        m_releaseAt = nowMs;
    m_dragging = dragging;
}

void CSubtitleState::SetMenuOpen(bool open, uint64_t nowMs)
{
    advance(nowMs);
    if (m_menuOpen && !open && !m_inside && !m_dragging)
        m_releaseAt = nowMs;
    m_menuOpen = open;
}

SubtitleVisual CSubtitleState::Tick(uint64_t nowMs)
{
    advance(nowMs);
    const Targets t = computeTargets(LaterOf(nowMs, m_lastTick));
    SubtitleVisual v;
    v.m_chromeAlpha = (int)m_chromeAlpha;
    v.m_lockButtonAlpha = (int)m_lockAlpha;
    //锁定时整窗穿透，只有光标正落在已经浮出的小锁按钮上时例外，那一下点击才能落到按钮上
    v.m_clickThrough = m_locked && !(m_part == kPartLock && m_lockAlpha > 0);
    v.m_buttonsActive = !m_locked && m_chromeAlpha >= kAlphaOpaque;
    v.m_animating = t.m_waiting || v.m_chromeAlpha != t.m_chromeTarget || v.m_lockButtonAlpha != t.m_lockTarget;
    return v;
}

int CSubtitleState::CursorPart() const
{
    return m_part;
}

CSubtitleState::Targets CSubtitleState::computeTargets(uint64_t nowMs) const
{
    Targets t;
    t.m_waiting = false;
    t.m_chromeTarget = 0;
    t.m_chromeFrom = m_lastTick;
    t.m_lockTarget = 0;
    t.m_lockFrom = m_lastTick;

    //底色与工具按钮：只在解锁时显示。光标在字幕上、拖动或调宽中、设置菜单弹出期间显示；
    //这些都结束后若还看得见，先保持 kChromeHideDelayMs 再淡出
    if (!m_locked)
    {
        if (m_inside || m_dragging || m_menuOpen)
        {
            t.m_chromeTarget = kAlphaOpaque;
        }
        else if (m_chromeAlpha > 0 && m_releaseAt != 0)
        {
            const uint64_t expiry = m_releaseAt + kChromeHideDelayMs;
            if (nowMs < expiry)
            {
                t.m_chromeTarget = kAlphaOpaque;
                t.m_waiting = true;
            }
            else
            {
                t.m_chromeFrom = LaterOf(expiry, m_lastTick);
            }
        }
    }

    //小锁按钮：只在锁定时显示。光标进入字幕满 kLockButtonHoverDelayMs 后浮出；已经浮出时光标在字幕上
    //就一直显示；光标离开后保持 kChromeHideDelayMs 再淡出
    if (m_locked)
    {
        if (m_inside)
        {
            if (m_lockAlpha > 0)
            {
                t.m_lockTarget = kAlphaOpaque;
            }
            else
            {
                const uint64_t appearAt = m_enterAt + kLockButtonHoverDelayMs;
                if (nowMs < appearAt)
                {
                    t.m_waiting = true;
                }
                else
                {
                    t.m_lockTarget = kAlphaOpaque;
                    t.m_lockFrom = LaterOf(appearAt, m_lastTick);
                }
            }
        }
        else if (m_lockAlpha > 0 && m_leaveAt != 0)
        {
            const uint64_t expiry = m_leaveAt + kChromeHideDelayMs;
            if (nowMs < expiry)
            {
                t.m_lockTarget = kAlphaOpaque;
                t.m_waiting = true;
            }
            else
            {
                t.m_lockFrom = LaterOf(expiry, m_lastTick);
            }
        }
    }
    return t;
}

void CSubtitleState::advance(uint64_t nowMs)
{
    //第一次推进只记下时刻
    if (m_lastTick == 0)
    {
        m_lastTick = nowMs;
        return;
    }
    if (nowMs <= m_lastTick)
        return;
    const Targets t = computeTargets(nowMs);
    if (nowMs > t.m_chromeFrom)
        m_chromeAlpha = stepAlpha(m_chromeAlpha, t.m_chromeTarget, nowMs - t.m_chromeFrom);
    if (nowMs > t.m_lockFrom)
        m_lockAlpha = stepAlpha(m_lockAlpha, t.m_lockTarget, nowMs - t.m_lockFrom);
    m_lastTick = nowMs;
}

double CSubtitleState::stepAlpha(double current, int target, uint64_t elapsed)
{
    const double opaque = (double)kAlphaOpaque;
    if ((double)target > current)
    {
        const double next = current + opaque * (double)elapsed / (double)kChromeFadeInMs;
        return (next > (double)target) ? (double)target : next;
    }
    if ((double)target < current)
    {
        const double next = current - opaque * (double)elapsed / (double)kChromeFadeOutMs;
        return (next < (double)target) ? (double)target : next;
    }
    return current;
}

} // namespace subtitle
