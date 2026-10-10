/**
 *  LockPower.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "LockPower.h"

#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace flamingolock {

namespace {

// 启用本进程令牌里的关机特权（SE_SHUTDOWN_NAME）；普通用户的令牌里有该特权但默认未启用
bool EnableShutdownPrivilege()
{
    HANDLE token = NULL;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        LOG_ERROR("lock power: OpenProcessToken failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        return false;
    }
    TOKEN_PRIVILEGES tp;
    ::ZeroMemory(&tp, sizeof(tp));
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = ::LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid) != FALSE;
    if (ok)
    {
        ok = ::AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL) != FALSE
             && ::GetLastError() == ERROR_SUCCESS;
    }
    if (!ok)
    {
        LOG_ERROR("lock power: enable SE_SHUTDOWN_NAME failed, error=%u.", static_cast<unsigned>(::GetLastError()));
    }
    ::CloseHandle(token);
    return ok;
}

} // namespace

bool RunPowerAction(LockPowerAction action)
{
    if (!EnableShutdownPrivilege())
    {
        return false;
    }
    const UINT how = ((action == kPowerRestart) ? EWX_REBOOT : (EWX_SHUTDOWN | EWX_POWEROFF)) | EWX_FORCEIFHUNG;
    const DWORD reason = SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED;
    if (!::ExitWindowsEx(how, reason))
    {
        LOG_ERROR("lock power: ExitWindowsEx(%s) failed, error=%u.", (action == kPowerRestart) ? "restart" : "shutdown",
                  static_cast<unsigned>(::GetLastError()));
        return false;
    }
    LOG_INFO("lock power: %s requested from the lock screen.", (action == kPowerRestart) ? "restart" : "shutdown");
    return true;
}

} // namespace flamingolock
