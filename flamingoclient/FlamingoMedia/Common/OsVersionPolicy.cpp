/**
 *  系统版本策略的实现，见 OsVersionPolicy.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "OsVersionPolicy.h"

#include <windows.h>
#include <string.h>

namespace flamingomedia {

//ntdll 导出的 RtlGetVersion 的函数类型。它在 winternl.h / wdm.h 里才有声明，用户态工程按名称动态取用。
typedef LONG (WINAPI* RtlGetVersionFn)(OSVERSIONINFOEXW* info);

bool IsScreenCaptureSupportedBuild(uint32_t major, uint32_t minor, uint32_t build)
{
    //按「主版本 → 次版本 → 构建号」逐级比较，高一级更大即直接满足
    if (major != kScreenCaptureMinMajor)
    {
        return major > kScreenCaptureMinMajor;
    }
    if (minor != kScreenCaptureMinMinor)
    {
        return minor > kScreenCaptureMinMinor;
    }
    return build >= kScreenCaptureMinBuild;
}

bool GetOsVersion(uint32_t& major, uint32_t& minor, uint32_t& build)
{
    major = 0;
    minor = 0;
    build = 0;
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == NULL)
    {
        return false;
    }
    RtlGetVersionFn fn = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"));
    if (fn == NULL)
    {
        return false;
    }
    OSVERSIONINFOEXW info;
    memset(&info, 0, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
    //RtlGetVersion 成功时返回 STATUS_SUCCESS（0）
    if (fn(&info) != 0)
    {
        return false;
    }
    major = info.dwMajorVersion;
    minor = info.dwMinorVersion;
    build = info.dwBuildNumber;
    return true;
}

} // namespace flamingomedia
