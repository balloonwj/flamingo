/**
 *  WindowAttention.h
 *
 *  收到窗口抖动时，把聊天窗调到用户眼前。
 *
 *  Windows 限制后台进程把自己的窗口设为前台：用户正在操作别的程序时，
 *  SetForegroundWindow 会被拒绝，系统只闪烁任务栏按钮（见微软 SetForegroundWindow
 *  文档的 Remarks）。同一台机器上开两个客户端互发抖动时，发送方刚被点击、是前台进程，
 *  接收方的调用必然被拒绝，窗口留在发送方窗口后面，对方看不到晃动。
 *
 *  RaiseWindowForAttention 的做法：先照常尝试设为前台；被拒绝时改为只调整层级 ——
 *  设为最上层再恢复普通层级（两步都不激活窗口），使窗口出现在其他普通窗口之上，
 *  再闪烁任务栏按钮直到用户切到该窗口。键盘焦点仍留在用户正在使用的程序里，
 *  用户正在输入的内容不会被打进聊天窗。
 *
 *  系统调用经 IWindowRaiseOps 接口传入，单元测试用记录调用顺序的假实现替换
 *  （Tests/WindowAttentionTests.cpp），产品代码传 Win32WindowRaiseOps。
 *
 *  典型用法：
 *      balloonchatwnd::Win32WindowRaiseOps ops;
 *      balloonchatwnd::WindowRaiseResult r = balloonchatwnd::RaiseWindowForAttention(m_hWnd, ops);
 *      // 按 r 的各字段写诊断日志
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <windows.h>

namespace balloonchatwnd {

/**
 *  IWindowRaiseOps —— RaiseWindowForAttention 用到的系统调用集合。
 *
 *  每个方法对应一个 Win32 调用，参数与返回值的含义同该调用。抽成接口只是为了让
 *  单元测试能记录调用顺序、并模拟"设前台被拒绝"这类无法在测试进程里稳定复现的情况。
 */
class IWindowRaiseOps
{
public:
    virtual ~IWindowRaiseOps() {}

    // 窗口当前是否处于最小化状态（对应 IsIconic）。
    //   hwnd：目标窗口，调用方保证非 NULL。
    virtual bool IsMinimized(HWND hwnd) = 0;

    // 按指定方式显示窗口（对应 ShowWindow）。
    //   hwnd：目标窗口；nCmdShow：SW_RESTORE / SW_SHOW 等。
    virtual void Show(HWND hwnd, int nCmdShow) = 0;

    // 把窗口提到 Z 序顶端并尝试激活（对应 BringWindowToTop）。后台进程调用时激活
    // 同样会被系统拒绝，因此不能单靠它把窗口提到其他程序的窗口之上。
    virtual void BringToTop(HWND hwnd) = 0;

    // 尝试把窗口设为前台（对应 SetForegroundWindow）。
    // 返回：系统报告的结果，true 表示已设为前台。
    virtual bool SetForeground(HWND hwnd) = 0;

    // 取当前的前台窗口（对应 GetForegroundWindow）；没有前台窗口时返回 NULL。
    virtual HWND GetForeground() = 0;

    // 只调整窗口层级，不移动、不改尺寸、不激活（对应 SetWindowPos 加
    // SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE）。
    //   hwnd：目标窗口；insertAfter：HWND_TOPMOST / HWND_NOTOPMOST 等。
    virtual void SetZOrderNoActivate(HWND hwnd, HWND insertAfter) = 0;

    // 闪烁窗口的任务栏按钮与标题栏，直到该窗口被用户切到前台为止（对应 FlashWindowEx
    // 加 FLASHW_ALL | FLASHW_TIMERNOFG）。
    virtual void FlashUntilForeground(HWND hwnd) = 0;
};

/**
 *  Win32WindowRaiseOps —— IWindowRaiseOps 的真实实现，逐个转调对应的 Win32 函数。
 *  无状态，可在栈上临时构造。
 */
class Win32WindowRaiseOps : public IWindowRaiseOps
{
public:
    bool IsMinimized(HWND hwnd) override;
    void Show(HWND hwnd, int nCmdShow) override;
    void BringToTop(HWND hwnd) override;
    bool SetForeground(HWND hwnd) override;
    HWND GetForeground() override;
    void SetZOrderNoActivate(HWND hwnd, HWND insertAfter) override;
    void FlashUntilForeground(HWND hwnd) override;
};

/**
 *  WindowRaiseResult —— RaiseWindowForAttention 的执行结果，供调用方写诊断日志。
 */
struct WindowRaiseResult
{
    WindowRaiseResult()
        : m_foregroundBefore(NULL)
        , m_wasMinimized(false)
        , m_setForegroundReturned(false)
        , m_isForeground(false)
        , m_raisedWithoutFocus(false)
        , m_flashed(false)
    {
    }

    HWND m_foregroundBefore;        // 调用前的前台窗口（可能属于其他进程）；未执行时为 NULL
    bool m_wasMinimized;            // 调用前窗口是否处于最小化状态
    bool m_setForegroundReturned;   // SetForegroundWindow 的返回值
    bool m_isForeground;            // 执行完毕后本窗口是否确实是前台窗口
    bool m_raisedWithoutFocus;      // 是否因未能设为前台而改为只调整层级（置顶再取消置顶）
    bool m_flashed;                 // 是否闪烁了任务栏按钮
};

/**
 *  把窗口调到用户眼前：还原或显示窗口，尝试设为前台；设不成前台时只把窗口提到
 *  普通窗口的最上面（不激活、不抢键盘焦点），并闪烁任务栏按钮。
 *    hwnd：目标顶层窗口；为 NULL 时不调用任何系统函数，直接返回默认结果。
 *    ops： 系统调用的实现，产品代码传 Win32WindowRaiseOps，单测传假实现；
 *          只在本次调用期间使用，不保存。
 *  返回：各步骤的执行情况，见 WindowRaiseResult。
 *  副作用：可能改变窗口的显示状态、层级与前台状态，可能使任务栏按钮开始闪烁。
 */
WindowRaiseResult RaiseWindowForAttention(HWND hwnd, IWindowRaiseOps& ops);

} // namespace balloonchatwnd
