/**
 *  测试用：在作用域内把当前线程切换为按显示器感知 DPI（PER_MONITOR_AWARE_V2），退出时恢复。
 *
 *  单元测试程序没有声明 DPI 感知，系统对它一律报告 96 DPI（GetDpiForMonitor、屏幕 DC 的 LOGPIXELSX 都是 96）；
 *  真实客户端声明了按显示器感知（Startup.cpp 的 OptInPerMonitorV2），在 125% 缩放的显示器上按 120 DPI 取字体。
 *  与 DPI 有关的用例要在这个作用域里建窗口、取字体与测量，才与用户看到的一致（2026-10-07，资料窗输入框文字被裁掉的
 *  问题在 96 DPI 下复现不出来）。系统没有 SetThreadDpiAwarenessContext（Windows 10 1607 之前）时什么也不做。
 *
 *  典型用法：
 *      TestDpiAwareScope dpiAware;   //用例开头
 *      ...建窗口、取字体、测量...
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>

class TestDpiAwareScope
{
public:
    TestDpiAwareScope()
        : m_set(NULL)
        , m_previous(NULL)
    {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        m_set = (user32 != NULL)
            ? reinterpret_cast<SetContextFn>(::GetProcAddress(user32, "SetThreadDpiAwarenessContext"))
            : NULL;
        if (m_set != NULL)
            m_previous = m_set(PerMonitorAwareV2());
    }

    ~TestDpiAwareScope()
    {
        if (m_set != NULL && m_previous != NULL)
            m_set(m_previous);
    }

    // 系统是否支持按线程切换 DPI 感知（不支持时作用域不起作用，用例仍按 96 DPI 运行）
    bool Active() const
    {
        return m_set != NULL && m_previous != NULL;
    }

private:
    typedef HANDLE (WINAPI* SetContextFn)(HANDLE);

    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的取值（winuser.h 里定义为 ((DPI_AWARENESS_CONTEXT)-4)）
    static HANDLE PerMonitorAwareV2()
    {
        return reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));
    }

    // 不可复制：析构时恢复线程的 DPI 感知上下文
    TestDpiAwareScope(const TestDpiAwareScope&);
    TestDpiAwareScope& operator=(const TestDpiAwareScope&);

    SetContextFn m_set;    // SetThreadDpiAwarenessContext；系统没有时为 NULL
    HANDLE m_previous;     // 切换之前线程的 DPI 感知上下文，析构时恢复
};
