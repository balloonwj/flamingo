/**
 *  LockUiState.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include <windows.h>

#include "LockUiState.h"

namespace flamingolock {

CLockUiState::CLockUiState()
    : m_phase(kUiIdle)
    , m_monitor(-1)
    , m_lastActivityMs(0)
{
}

void CLockUiState::Reset()
{
    m_phase = kUiIdle;
    m_monitor = -1;
    m_lastActivityMs = 0;
}

LockUiPhase CLockUiState::Phase() const
{
    return m_phase;
}

int CLockUiState::ActiveMonitor() const
{
    return m_monitor;
}

bool CLockUiState::IsBarVisible() const
{
    return m_phase != kUiIdle;
}

bool CLockUiState::AcceptsKeyboard() const
{
    return m_phase == kUiPassword;
}

void CLockUiState::OnClick(int monitor, uint64_t nowMs)
{
    switch (m_phase)
    {
    //只有动画：在被点击的显示器上出现按钮栏
    case kUiIdle:
        m_phase = kUiButtons;
        m_monitor = monitor;
        m_lastActivityMs = nowMs;
        break;

    //按钮或密码阶段：点在别的显示器上就把按钮栏挪过去，阶段不变
    case kUiButtons:
    case kUiPassword:
        m_monitor = monitor;
        m_lastActivityMs = nowMs;
        break;

    //确认框打开时，点在框外不起作用（必须明确选「取消」或确认）
    default:
        break;
    }
}

LockUiCommand CLockUiState::OnButton(LockUiButton button, uint64_t nowMs)
{
    const bool barButtonsVisible = (m_phase == kUiButtons || m_phase == kUiPassword);
    const bool confirmVisible = (m_phase == kUiConfirmShutdown || m_phase == kUiConfirmRestart);
    switch (button)
    {
    //解锁：出现密码框（已经在密码阶段时只是重新取焦点）
    case kBtnUnlock:
        if (!barButtonsVisible)
        {
            return kCmdNone;
        }
        m_phase = kUiPassword;
        m_lastActivityMs = nowMs;
        return kCmdFocusPassword;

    //重启：出现确认框
    case kBtnRestart:
        if (!barButtonsVisible)
        {
            return kCmdNone;
        }
        m_phase = kUiConfirmRestart;
        m_lastActivityMs = nowMs;
        return kCmdNone;

    //关机：出现确认框
    case kBtnShutdown:
        if (!barButtonsVisible)
        {
            return kCmdNone;
        }
        m_phase = kUiConfirmShutdown;
        m_lastActivityMs = nowMs;
        return kCmdNone;

    //确认框里的确认：按确认框的种类执行关机或重启，界面回到按钮阶段（系统随后会结束本进程）
    case kBtnConfirmOk:
        if (!confirmVisible)
        {
            return kCmdNone;
        }
        {
            const LockUiCommand cmd = (m_phase == kUiConfirmShutdown) ? kCmdShutdown : kCmdRestart;
            m_phase = kUiButtons;
            m_lastActivityMs = nowMs;
            return cmd;
        }

    //确认框里的取消：回到按钮阶段
    case kBtnConfirmCancel:
        if (!confirmVisible)
        {
            return kCmdNone;
        }
        m_phase = kUiButtons;
        m_lastActivityMs = nowMs;
        return kCmdNone;

    //不认识的按钮：忽略
    default:
        return kCmdNone;
    }
}

bool CLockUiState::OnEscape(uint64_t nowMs)
{
    switch (m_phase)
    {
    //确认框或密码框：退回按钮阶段
    case kUiConfirmShutdown:
    case kUiConfirmRestart:
    case kUiPassword:
        m_phase = kUiButtons;
        m_lastActivityMs = nowMs;
        return false;

    //按钮阶段：收起按钮栏
    case kUiButtons:
        m_phase = kUiIdle;
        m_monitor = -1;
        return true;

    //只有动画：没有可退的
    default:
        return false;
    }
}

void CLockUiState::OnActivity(uint64_t nowMs)
{
    if (m_phase != kUiIdle)
    {
        m_lastActivityMs = nowMs;
    }
}

bool CLockUiState::Tick(uint64_t nowMs)
{
    if (m_phase != kUiButtons && m_phase != kUiPassword)
    {
        return false;
    }
    if (nowMs < m_lastActivityMs || nowMs - m_lastActivityMs < kUiIdleTimeoutMs)
    {
        return false;
    }
    m_phase = kUiIdle;
    m_monitor = -1;
    return true;
}

// ---------------------------------------------------------------------------
// 按键判定
// ---------------------------------------------------------------------------

namespace {

// 是否修饰键本身（Shift / Ctrl / Alt 的左右与通用键码）
bool IsModifierVk(uint32_t vk)
{
    switch (vk)
    {
    //Shift、Ctrl、Alt 的通用键码与左右键码
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return true;

    //其余都不是修饰键
    default:
        return false;
    }
}

} // namespace

LockKeyDecision DecideLockKey(const LockKeyEvent& e, bool foregroundIsLock, bool passwordActive)
{
    if (e.m_injected)
    {
        return kKeyBlock;
    }
    if (e.m_vk == VK_LWIN || e.m_vk == VK_RWIN || e.m_winDown)
    {
        return kKeyBlock;
    }
    if (e.m_keyUp)
    {
        return kKeyPass;
    }
    if (e.m_vk == VK_APPS || e.m_vk == VK_SNAPSHOT)
    {
        return kKeyBlock;
    }
    if (e.m_altDown && !IsModifierVk(e.m_vk))
    {
        return kKeyBlock;
    }
    if (e.m_ctrlDown && e.m_vk == VK_ESCAPE)
    {
        return kKeyBlock;
    }
    if (passwordActive && foregroundIsLock)
    {
        return kKeyPass;
    }
    return kKeyBlock;
}

LockKeyDecision DecideLockMouse(uint32_t message, bool injected)
{
    if (!injected || message == WM_MOUSEMOVE)
    {
        return kKeyPass;
    }
    return kKeyBlock;
}

} // namespace flamingolock
