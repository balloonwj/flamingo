/**
 *  WindowAttention.cpp
 *
 *  RaiseWindowForAttention 与 Win32WindowRaiseOps 的实现，用途见 WindowAttention.h。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "WindowAttention.h"

namespace balloonchatwnd {

bool Win32WindowRaiseOps::IsMinimized(HWND hwnd)
{
    return ::IsIconic(hwnd) != FALSE;
}

void Win32WindowRaiseOps::Show(HWND hwnd, int nCmdShow)
{
    ::ShowWindow(hwnd, nCmdShow);
}

void Win32WindowRaiseOps::BringToTop(HWND hwnd)
{
    ::BringWindowToTop(hwnd);
}

bool Win32WindowRaiseOps::SetForeground(HWND hwnd)
{
    return ::SetForegroundWindow(hwnd) != FALSE;
}

HWND Win32WindowRaiseOps::GetForeground()
{
    return ::GetForegroundWindow();
}

void Win32WindowRaiseOps::SetZOrderNoActivate(HWND hwnd, HWND insertAfter)
{
    ::SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Win32WindowRaiseOps::FlashUntilForeground(HWND hwnd)
{
    FLASHWINFO fi;
    ::ZeroMemory(&fi, sizeof(fi));
    fi.cbSize    = sizeof(fi);
    fi.hwnd      = hwnd;
    // 标题栏与任务栏按钮一起闪，一直闪到窗口被切到前台为止；次数与间隔取系统默认值（填 0）
    fi.dwFlags   = FLASHW_ALL | FLASHW_TIMERNOFG;
    fi.uCount    = 0;
    fi.dwTimeout = 0;
    ::FlashWindowEx(&fi);
}

WindowRaiseResult RaiseWindowForAttention(HWND hwnd, IWindowRaiseOps& ops)
{
    WindowRaiseResult result;
    if (hwnd == NULL)
    {
        return result;
    }

    // 先记下此刻的前台窗口，后面的显示、设前台都可能改变它。调用方据此在日志里写出
    // 前台当时属于哪个进程，用来判断设前台被拒绝是不是因为用户正在操作别的程序。
    result.m_foregroundBefore = ops.GetForeground();

    // 最小化则还原，否则确保可见；最小化状态下后面的层级调整与晃动都看不见。
    if (ops.IsMinimized(hwnd))
    {
        result.m_wasMinimized = true;
        ops.Show(hwnd, SW_RESTORE);
    }
    else
    {
        ops.Show(hwnd, SW_SHOW);
    }

    // 先照常尝试成为前台：用户此刻正在操作本进程的窗口、或者前台锁已超时时，这一步就够了。
    ops.BringToTop(hwnd);
    result.m_setForegroundReturned = ops.SetForeground(hwnd);

    // 以调用之后的实际前台窗口为准：微软文档说明即使满足放行条件，设前台也可能被拒绝。
    result.m_isForeground = result.m_setForegroundReturned && (ops.GetForeground() == hwnd);
    if (result.m_isForeground)
    {
        return result;
    }

    // 被拒绝：只调整层级、不激活。先设为最上层再恢复普通层级，窗口停在所有普通窗口的
    // 最上面，却不会一直压在别的窗口上；键盘焦点仍在用户正在使用的程序里。
    ops.SetZOrderNoActivate(hwnd, HWND_TOPMOST);
    ops.SetZOrderNoActivate(hwnd, HWND_NOTOPMOST);
    result.m_raisedWithoutFocus = true;

    // 再闪烁任务栏按钮，直到用户切到这个窗口为止。
    ops.FlashUntilForeground(hwnd);
    result.m_flashed = true;
    return result;
}

} // namespace balloonchatwnd
