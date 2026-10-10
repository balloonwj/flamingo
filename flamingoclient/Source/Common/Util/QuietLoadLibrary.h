/**
 *  加载可选 DLL（语言包、功能插件）时屏蔽系统错误框。
 *
 *  DLL 文件损坏、不是 DLL、被截断或位数不符时，LoadLibrary 默认由系统加载器弹出「损坏的映像」对话框，用户
 *  点掉之前程序的启动一直停在这里（2026-10-04 端到端检查实测：语言包换成文本文件后启动被这个框卡住）。
 *  语言包与功能插件都是可选的，加载不了应当安静地按失败处理、记日志，所以加载期间对当前线程设
 *  SEM_FAILCRITICALERRORS，让 LoadLibrary 只返回失败。只改当前线程的设置，加载完立即恢复。
 *
 *  只有头文件。语言包（i18n::Init、i18n::ListPackLanguages）与插件加载器（balloonapp::CPluginLibrary）共用。
 *  单元测试进程启动时已对整个进程屏蔽错误框（Tests/TestCrashDialogSetup.cpp），单测里观察不到弹框，
 *  这一点由 tmp/i18n_phase5/fallback_check.ps1 对真实程序做端到端检查。典型用法：
 *      HMODULE module = quietload::LoadLibraryQuietly(dllPath);
 *      if (module == NULL) { DWORD err = ::GetLastError(); ... }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>

namespace quietload {

/**
 *  加载 DLL，加载期间对当前线程屏蔽系统错误框（「损坏的映像」「找不到文件」之类）。依赖从 DLL 所在目录开始
 *  查找（LOAD_WITH_ALTERED_SEARCH_PATH），与语言包、插件原先的加载方式相同。
 *  @param dllPath DLL 的完整路径；为 NULL 或空串时返回 NULL，GetLastError 为 ERROR_INVALID_PARAMETER
 *  @return 模块句柄，由调用方 FreeLibrary（或按调用方的约定不卸载）；失败返回 NULL，此时 GetLastError 是
 *          LoadLibraryExW 的错误码（如不是 DLL 时为 ERROR_BAD_EXE_FORMAT、找不到时为 ERROR_MOD_NOT_FOUND）。
 *          副作用：调用期间临时修改当前线程的错误模式，返回前恢复
 */
inline HMODULE LoadLibraryQuietly(const wchar_t* dllPath)
{
    if (dllPath == NULL || dllPath[0] == L'\0')
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    DWORD oldMode = 0;
    const BOOL modeChanged = ::SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode);
    HMODULE module = ::LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    const DWORD loadError = ::GetLastError();
    if (modeChanged)
        ::SetThreadErrorMode(oldMode, NULL);
    //恢复错误模式的调用可能改写 last error，调用方要拿到的是加载失败的原因
    ::SetLastError(loadError);
    return module;
}

} // namespace quietload
