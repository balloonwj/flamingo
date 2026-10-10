/**
 *  LockDpi.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "LockDpi.h"

namespace flamingolock {

namespace {

const float kDefaultDpi = 96.0f;              // 系统基准 DPI
const int kMdtEffectiveDpi = 0;               // MONITOR_DPI_TYPE::MDT_EFFECTIVE_DPI
//DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的取值（较老的 SDK 头文件没有这个宏）
const HANDLE kPerMonitorV2Context = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));

typedef HANDLE (WINAPI* FnSetThreadDpiAwarenessContext)(HANDLE);              // user32，Win10 1607 起
typedef HRESULT (WINAPI* FnGetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);   // shcore，Win8.1 起

// 取 user32 的 SetThreadDpiAwarenessContext；系统没有时返回 NULL
FnSetThreadDpiAwarenessContext GetSetThreadDpiFn()
{
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 == NULL)
    {
        return NULL;
    }
    return reinterpret_cast<FnSetThreadDpiAwarenessContext>(::GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
}

} // namespace

HANDLE EnterPerMonitorV2Dpi()
{
    FnSetThreadDpiAwarenessContext setDpi = GetSetThreadDpiFn();
    if (setDpi == NULL)
    {
        return NULL;
    }
    return setDpi(kPerMonitorV2Context);
}

void RestoreThreadDpi(HANDLE oldContext)
{
    if (oldContext == NULL)
    {
        return;
    }
    FnSetThreadDpiAwarenessContext setDpi = GetSetThreadDpiFn();
    if (setDpi != NULL)
    {
        setDpi(oldContext);
    }
}

float LockMonitorScale(HMONITOR monitor)
{
    HMODULE shcore = ::LoadLibraryW(L"shcore.dll");
    float scale = 1.0f;
    if (shcore != NULL)
    {
        FnGetDpiForMonitor fn = reinterpret_cast<FnGetDpiForMonitor>(::GetProcAddress(shcore, "GetDpiForMonitor"));
        UINT dpiX = 0;
        UINT dpiY = 0;
        if (fn != NULL && SUCCEEDED(fn(monitor, kMdtEffectiveDpi, &dpiX, &dpiY)) && dpiX > 0)
        {
            scale = static_cast<float>(dpiX) / kDefaultDpi;
        }
        ::FreeLibrary(shcore);
    }
    return scale;
}

} // namespace flamingolock
