/**
 *  远程桌面被控端的控制把关实现，RdControlGate.cpp
 *  balloonwj@qq.com   2026-10-12
 */
#include "RdControlGate.h"

#include <algorithm>

namespace remotedesktop {

namespace {

//鼠标按下消息对应的松开消息；不是左、右、中键按下时返回 0
uint32_t buttonUpFor(uint32_t downMessage)
{
    switch (downMessage)
    {
    //左键按下
    case WM_LBUTTONDOWN:
        return WM_LBUTTONUP;
    //右键按下
    case WM_RBUTTONDOWN:
        return WM_RBUTTONUP;
    //中键按下
    case WM_MBUTTONDOWN:
        return WM_MBUTTONUP;
    //移动、滚轮、双击（注入器自己按下又松开）等都不留下按住的按钮
    default:
        return 0;
    }
}

//窗口所属的进程是不是本进程
bool isOwnProcessHwnd(HWND hwnd)
{
    if (hwnd == NULL)
        return false;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    return pid == ::GetCurrentProcessId();
}

} // namespace

RdControlGate::RdControlGate()
    : m_allowed(false)
    , m_probe(NULL)
{
}

void RdControlGate::SetOwnWindowProbe(OwnWindowProbe probe)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_probe = probe;
}

void RdControlGate::SetAllowed(bool allowed, POINT cursor, std::vector<RdInputCommand>& releases)
{
    releases.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (allowed)
    {
        m_allowed = true;
        return;
    }
    m_allowed = false;
    //按键按按下的逆序抬起：组合键里先按下的修饰键最后抬起
    for (size_t i = m_heldKeys.size(); i > 0; --i)
    {
        RdInputCommand up;
        up.type = rd_input_key;
        up.message = WM_KEYUP;
        up.vkey = m_heldKeys[i - 1];
        releases.push_back(up);
    }
    //鼠标按钮在光标当前位置松开，避免把光标拉回控制者最后操作的地方
    for (size_t i = 0; i < m_heldButtons.size(); ++i)
    {
        RdInputCommand up;
        up.type = rd_input_mouse;
        up.message = m_heldButtons[i];
        up.x = cursor.x;
        up.y = cursor.y;
        releases.push_back(up);
    }
    m_heldKeys.clear();
    m_heldButtons.clear();
}

bool RdControlGate::IsAllowed() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_allowed;
}

bool RdControlGate::Filter(const RdInputCommand& cmd)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_allowed)
        return false;
    if (cmd.type != rd_input_mouse && cmd.type != rd_input_key)
        return false;
    //落在本进程窗口上的按下、移动、滚轮不注入；松开类输入照常注入，按下时已注入的按钮才能松开
    if (m_probe != NULL && !IsReleaseMessage(cmd) && m_probe(cmd))
        return false;
    TrackLocked(cmd);
    return true;
}

void RdControlGate::Reset()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_allowed = false;
    m_heldKeys.clear();
    m_heldButtons.clear();
}

bool RdControlGate::IsOnOwnProcessWindow(const RdInputCommand& cmd)
{
    if (cmd.type == rd_input_mouse)
    {
        POINT pt = { cmd.x, cmd.y };
        return isOwnProcessHwnd(::WindowFromPoint(pt));
    }
    return isOwnProcessHwnd(::GetForegroundWindow());
}

bool RdControlGate::IsReleaseMessage(const RdInputCommand& cmd)
{
    if (cmd.type == rd_input_mouse)
        return cmd.message == WM_LBUTTONUP || cmd.message == WM_RBUTTONUP || cmd.message == WM_MBUTTONUP;
    return cmd.message == WM_KEYUP || cmd.message == WM_SYSKEYUP;
}

void RdControlGate::TrackLocked(const RdInputCommand& cmd)
{
    if (cmd.type == rd_input_mouse)
    {
        const uint32_t up = buttonUpFor(cmd.message);
        if (up != 0 && std::find(m_heldButtons.begin(), m_heldButtons.end(), up) == m_heldButtons.end())
            m_heldButtons.push_back(up);
        else if (IsReleaseMessage(cmd))
            m_heldButtons.erase(std::remove(m_heldButtons.begin(), m_heldButtons.end(), cmd.message),
                                m_heldButtons.end());
        return;
    }
    //按键：按下时记下（自动重复的按下不重复记），抬起时移除
    if (cmd.message == WM_KEYDOWN || cmd.message == WM_SYSKEYDOWN)
    {
        if (std::find(m_heldKeys.begin(), m_heldKeys.end(), cmd.vkey) == m_heldKeys.end())
            m_heldKeys.push_back(cmd.vkey);
    }
    else if (cmd.message == WM_KEYUP || cmd.message == WM_SYSKEYUP)
    {
        m_heldKeys.erase(std::remove(m_heldKeys.begin(), m_heldKeys.end(), cmd.vkey), m_heldKeys.end());
    }
}

} // namespace remotedesktop
