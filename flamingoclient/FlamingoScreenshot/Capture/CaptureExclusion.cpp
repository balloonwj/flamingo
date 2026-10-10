/**
 *  CaptureExclusion.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "CaptureExclusion.h"

#include <dwmapi.h>                        // DwmFlush：隐藏窗口后等桌面重新合成一帧
#include "../../Source/Common/Log/IULog.h"        // LOG_INFO / LOG_WARNING

#pragma comment(lib, "dwmapi.lib")

namespace balloonscreenshot {

namespace {

// WDA_EXCLUDEFROMCAPTURE 的取值。较老的 SDK 头文件里没有这个宏，这里按文档值补上。
const DWORD kWdaExcludeFromCapture = 0x00000011;

// 隐藏窗口后，DwmFlush 不可用（桌面合成关闭）时改为固定等待的时长（毫秒），
// 让被遮住的窗口有时间重画出来
const DWORD kHideSettleMs = 100;

// ntdll!RtlGetVersion 的函数原型
typedef LONG (WINAPI* FN_RtlGetVersion)(RTL_OSVERSIONINFOW*);

// 窗口是否在画面里：存在、可见、没有最小化
bool IsWindowOnScreen(HWND hwnd)
{
    return hwnd != NULL && ::IsWindow(hwnd) && ::IsWindowVisible(hwnd) && !::IsIconic(hwnd);
}

} // anonymous namespace

CaptureExclusionMode ChooseExclusionMode(bool windowUsable, DWORD osBuild)
{
    if (!windowUsable)
    {
        return kExcludeNone;
    }
    if (osBuild >= kExcludeFromCaptureMinBuild)
    {
        return kExcludeByAffinity;
    }
    return kExcludeByHiding;
}

DWORD GetWindowsBuildNumber()
{
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == NULL)
    {
        return 0;
    }
    FN_RtlGetVersion fn = (FN_RtlGetVersion)::GetProcAddress(ntdll, "RtlGetVersion");
    if (fn == NULL)
    {
        return 0;
    }
    RTL_OSVERSIONINFOW info;
    ::ZeroMemory(&info, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) != 0)
    {
        return 0;
    }
    return info.dwBuildNumber;
}

CCaptureExclusion::CCaptureExclusion()
    : m_hwnd(NULL)
    , m_mode(kExcludeNone)
    , m_oldAffinity(WDA_NONE)
{
}

CCaptureExclusion::~CCaptureExclusion()
{
    Restore();
}

CaptureExclusionMode CCaptureExclusion::Begin(HWND hwnd)
{
    if (hwnd == NULL)
    {
        return kExcludeNone;
    }
    DWORD build = GetWindowsBuildNumber();
    CaptureExclusionMode mode = ChooseExclusionMode(IsWindowOnScreen(hwnd), build);
    CaptureExclusionMode got = BeginAs(hwnd, mode);
    LOG_INFO("screenshot exclusion: hwnd=0x%p, os build=%u, chosen mode=%d, applied mode=%d.",
             hwnd, (unsigned)build, (int)mode, (int)got);
    return got;
}

CaptureExclusionMode CCaptureExclusion::BeginAs(HWND hwnd, CaptureExclusionMode mode)
{
    // 一个对象只排除一次：上一次还没恢复时先恢复。
    Restore();
    if (mode == kExcludeNone || !IsWindowOnScreen(hwnd))
    {
        return kExcludeNone;
    }

    if (mode == kExcludeByAffinity)
    {
        DWORD old = WDA_NONE;
        if (!::GetWindowDisplayAffinity(hwnd, &old))
        {
            old = WDA_NONE;
        }
        if (::SetWindowDisplayAffinity(hwnd, kWdaExcludeFromCapture))
        {
            m_hwnd = hwnd;
            m_mode = kExcludeByAffinity;
            m_oldAffinity = old;
            return m_mode;
        }
        // 设置失败（例如桌面合成被关闭）：改用隐藏窗口的办法
        LOG_WARNING("screenshot exclusion: SetWindowDisplayAffinity failed, error=%u, falling back to hiding.",
                    (unsigned)::GetLastError());
    }

    ::ShowWindow(hwnd, SW_HIDE);
    m_hwnd = hwnd;
    m_mode = kExcludeByHiding;
    // 等桌面重新合成一帧，让窗口背后的内容重画出来，再交给调用方抓屏
    if (FAILED(::DwmFlush()))
    {
        ::Sleep(kHideSettleMs);
    }
    return m_mode;
}

void CCaptureExclusion::Restore()
{
    if (m_mode == kExcludeNone)
    {
        return;
    }
    if (m_hwnd != NULL && ::IsWindow(m_hwnd))
    {
        if (m_mode == kExcludeByAffinity)
        {
            if (!::SetWindowDisplayAffinity(m_hwnd, m_oldAffinity))
            {
                LOG_WARNING("screenshot exclusion: restoring display affinity failed, error=%u.",
                            (unsigned)::GetLastError());
            }
        }
        else
        {
            // 重新显示但不激活：此时截图界面在最上层，激活它会抢走截图界面的焦点
            ::ShowWindow(m_hwnd, SW_SHOWNA);
        }
    }
    m_hwnd = NULL;
    m_mode = kExcludeNone;
    m_oldAffinity = WDA_NONE;
}

} // namespace balloonscreenshot
