/**
 *  客户端日志接口 CIULog 在 FlamingoLock.dll 内的实现（日志桥接）。
 *
 *  DLL 里的代码与 exe 一样使用 LOG_INFO / LOG_WARNING / LOG_ERROR 宏，宏展开为 CIULog::Log(...)。
 *  exe 里这两个函数由 Source/Common/Log/IULog.cpp 实现；DLL 不编译那份实现，而是编译本文件：
 *  格式化成「[lock] [调用处:行号] 正文」的 UTF-8 文本，经 LockLogWriteLine 交给宿主，写进 exe 的同一个日志文件。
 *
 *  「[调用处:行号] 正文」这一段的格式化复用 FlamingoMedia 的 LogFormat（编译同一份 FlamingoMedia/Host/LogFormat.cpp），
 *  与其它 DLL 写出的日志行格式一致；本文件只在前面加 "[lock] "。级别过滤由 exe 侧统一完成。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <windows.h>
#include <stdarg.h>
#include <string.h>

#include "../../Source/Common/Log/IULog.h"
#include "../../FlamingoMedia/Host/LogFormat.h"
#include "LockLog.h"

namespace {

//把客户端日志级别（LOG_LEVEL_*）换算成跨模块接口的日志级别（lock_log_*）
int ToLockLevel(long nLevel)
{
    if (nLevel >= LOG_LEVEL_ERROR)
    {
        return flamingolock::lock_log_error;
    }
    if (nLevel == LOG_LEVEL_WARNING)
    {
        return flamingolock::lock_log_warning;
    }
    return flamingolock::lock_log_info;
}

//把前缀写进行首，返回前缀的字节数；前缀长度在编译期已知且远小于行缓冲，不会越界
int WritePrefix(char* line)
{
    const size_t prefixLen = strlen(flamingolock::kLockLogPrefix);
    memcpy(line, flamingolock::kLockLogPrefix, prefixLen);
    line[prefixLen] = '\0';
    return static_cast<int>(prefixLen);
}

} // namespace

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...)
{
    char line[flamingolock::kLockLogLineBytes];
    int prefixLen = WritePrefix(line);

    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineW(line + prefixLen, flamingolock::kLockLogLineBytes - prefixLen,
                                          pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
    {
        return false;
    }
    return flamingolock::LockLogWriteLine(ToLockLevel(nLevel), line);
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
    char line[flamingolock::kLockLogLineBytes];
    int prefixLen = WritePrefix(line);

    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineA(line + prefixLen, flamingolock::kLockLogLineBytes - prefixLen,
                                          pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
    {
        return false;
    }
    return flamingolock::LockLogWriteLine(ToLockLevel(nLevel), line);
}
