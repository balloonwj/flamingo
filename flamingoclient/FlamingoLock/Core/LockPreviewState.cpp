/**
 *  LockPreviewState.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "LockPreviewState.h"

namespace flamingolock {

CLockPreviewState::CLockPreviewState()
    : m_current(kAnimHonor)
    , m_startMs(0)
    , m_animStartMs(0)
    , m_ending(false)
{
    m_choice = DefaultChoice();
}

LockAnimation CLockPreviewState::Start(const LockAnimationChoice& choice, uint32_t randomValue, uint64_t nowMs)
{
    m_choice = choice;
    //与真正锁屏同一套规则：固定方式播所选的那种，「随机」取模，「轮流播放」从八荣八耻开始
    m_current = PickInitialAnimation(choice, randomValue);
    m_startMs = nowMs;
    m_animStartMs = nowMs;
    m_ending = false;
    return m_current;
}

void CLockPreviewState::RestartClock(uint64_t nowMs)
{
    if (m_ending)
    {
        return;
    }
    m_startMs = nowMs;
    m_animStartMs = nowMs;
}

bool CLockPreviewState::Tick(uint64_t nowMs)
{
    if (m_ending || m_choice.m_mode != kAnimModeRotate)
    {
        return false;
    }
    if (CurrentElapsedMs(nowMs) < kPreviewRotateIntervalMs)
    {
        return false;
    }
    m_animStartMs = nowMs;
    m_current = NextAnimation(m_current);
    return true;
}

bool CLockPreviewState::OnInput(LockPreviewInput input, uint64_t nowMs)
{
    if (m_ending)
    {
        return false;
    }
    const bool inGrace = (nowMs < m_startMs + kPreviewInputGraceMs);
    switch (input)
    {
    //单击与按键：保护时间过后才结束，避免打开预览的那一下点击（如双击的第二下）把预览立刻关掉
    case kPreviewInputMouseButton:
    case kPreviewInputKey:
    case kPreviewInputSysKey:
        if (inGrace)
        {
            return false;
        }
        m_ending = true;
        return true;

    //失去激活、显示器变化、系统注销：任何时候都立即结束
    case kPreviewInputDeactivate:
    case kPreviewInputDisplayChange:
    case kPreviewInputEndSession:
        m_ending = true;
        return true;

    //鼠标移动与字符输入：不结束预览
    default:
        return false;
    }
}

LockAnimation CLockPreviewState::Current() const
{
    return m_current;
}

bool CLockPreviewState::IsEnding() const
{
    return m_ending;
}

uint64_t CLockPreviewState::CurrentElapsedMs(uint64_t nowMs) const
{
    return (nowMs > m_animStartMs) ? nowMs - m_animStartMs : 0;
}

} // namespace flamingolock
