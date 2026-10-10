/**
 *  密聊窗口防截图的实现，见 CaptureProtect.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "CaptureProtect.h"

#include "../SecretChatApi.h"

namespace secretchat {

namespace {

//RtlGetVersion 的函数类型（ntdll.dll 导出，不受应用程序清单的版本兼容垫片影响）
typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);

//DwmIsCompositionEnabled 的函数类型（dwmapi.dll 导出）
typedef HRESULT(WINAPI* DwmIsCompositionEnabledFn)(BOOL*);

//GetWindowDisplayAffinity / SetWindowDisplayAffinity 的函数类型（user32.dll，Win7 起有）
typedef BOOL(WINAPI* GetWindowDisplayAffinityFn)(HWND, DWORD*);
typedef BOOL(WINAPI* SetWindowDisplayAffinityFn)(HWND, DWORD);

//WDA_NONE
const DWORD kWdaNone = 0;

//取系统内部版本号；取不到时为 0
uint32_t QueryOsBuild()
{
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == NULL)
        return 0;
    RtlGetVersionFn fn = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"));
    if (fn == NULL)
        return 0;
    RTL_OSVERSIONINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) != 0)
        return 0;
    return static_cast<uint32_t>(info.dwBuildNumber);
}

//桌面合成是否开着；Win8 起始终开着，Win7 可能被关掉
bool QueryDwmEnabled()
{
    HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (dwm == NULL)
        return false;
    bool enabled = false;
    DwmIsCompositionEnabledFn fn =
        reinterpret_cast<DwmIsCompositionEnabledFn>(::GetProcAddress(dwm, "DwmIsCompositionEnabled"));
    if (fn != NULL)
    {
        BOOL on = FALSE;
        if (SUCCEEDED(fn(&on)))
            enabled = (on != FALSE);
    }
    ::FreeLibrary(dwm);
    return enabled;
}

//设置窗口的显示亲和性
bool SetAffinity(HWND hwnd, DWORD affinity)
{
    SetWindowDisplayAffinityFn fn = reinterpret_cast<SetWindowDisplayAffinityFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity"));
    return fn != NULL && fn(hwnd, affinity) != FALSE;
}

//读取窗口的显示亲和性；读不到时返回 false
bool GetAffinity(HWND hwnd, DWORD& affinity)
{
    affinity = kWdaNone;
    GetWindowDisplayAffinityFn fn = reinterpret_cast<GetWindowDisplayAffinityFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetWindowDisplayAffinity"));
    return fn != NULL && fn(hwnd, &affinity) != FALSE;
}

} // namespace

int DecideProtectLevel(bool dwmEnabled, uint32_t osBuild)
{
    if (!dwmEnabled)
        return kProtectNone;
    if (osBuild >= kExcludeFromCaptureMinBuild)
        return kProtectExclude;
    return kProtectMonitor;
}

int AffinityToLevel(DWORD affinity)
{
    if (affinity == kWdaExcludeFromCapture)
        return kProtectExclude;
    if (affinity == kWdaMonitor)
        return kProtectMonitor;
    return kProtectNone;
}

int ProbeCaptureProtectLevel()
{
    return DecideProtectLevel(QueryDwmEnabled(), QueryOsBuild());
}

int ApplyCaptureProtect(HWND hwnd)
{
    if (hwnd == NULL || !::IsWindow(hwnd))
        return kProtectNone;
    const int expected = ProbeCaptureProtectLevel();
    if (expected == kProtectExclude && SetAffinity(hwnd, kWdaExcludeFromCapture))
        return kProtectExclude;
    if (expected != kProtectNone && SetAffinity(hwnd, kWdaMonitor))
        return kProtectMonitor;
    //系统判定做不到时仍试一次 WDA_MONITOR：个别环境下桌面合成状态查询不准
    if (SetAffinity(hwnd, kWdaMonitor))
        return kProtectMonitor;
    return kProtectNone;
}

bool IsCaptureProtectIntact(HWND hwnd, int level)
{
    if (level == kProtectNone)
        return true;
    DWORD affinity = kWdaNone;
    if (!GetAffinity(hwnd, affinity))
        return false;
    return AffinityToLevel(affinity) >= level;
}

void RemoveCaptureProtect(HWND hwnd)
{
    if (hwnd != NULL && ::IsWindow(hwnd))
        SetAffinity(hwnd, kWdaNone);
}

} // namespace secretchat
