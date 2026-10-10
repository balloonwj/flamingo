/**
 *  客户端日志接口 CIULog 在 FlamingoMedia.dll 内的实现（日志桥接）。
 *
 *  DLL 里的代码与 exe 一样使用 LOG_INFO / LOG_WARNING / LOG_ERROR 宏，宏展开为
 *  CIULog::Log(...)。exe 里这两个函数由 Source/Common/Log/IULog.cpp 实现（写异步日志文件）；
 *  DLL 不编译那份实现，而是编译本文件：格式化成「[调用处:行号] 正文」的 UTF-8 文本，
 *  经 MediaLogWriteLine 交给宿主，最终仍写进 exe 的同一个日志文件。
 *  这样从 exe 迁入 DLL 的代码不需要改任何日志调用。
 *
 *  只实现两个 Log 重载：Init / Uninit / SetLevel / Flush 由 exe 在进程启动与退出时调用，
 *  DLL 里不会用到；级别过滤由 exe 侧的日志实现统一完成。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <windows.h>
#include <stdarg.h>

#include "../../Source/Common/Log/IULog.h"
#include "LogFormat.h"
#include "MediaLog.h"

namespace {

//把客户端日志级别（LOG_LEVEL_*）换算成跨模块接口的日志级别（media_log_*）
int ToMediaLevel(long nLevel)
{
    if (nLevel >= LOG_LEVEL_ERROR)
    {
        return flamingomedia::media_log_error;
    }
    if (nLevel == LOG_LEVEL_WARNING)
    {
        return flamingomedia::media_log_warning;
    }
    return flamingomedia::media_log_info;
}

} // namespace

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...)
{
    char line[flamingomedia::kMaxLogLineBytes];
    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineW(line, sizeof(line), pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
    {
        return false;
    }
    return flamingomedia::MediaLogWriteLine(ToMediaLevel(nLevel), line);
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
    char line[flamingomedia::kMaxLogLineBytes];
    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineA(line, sizeof(line), pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
    {
        return false;
    }
    return flamingomedia::MediaLogWriteLine(ToMediaLevel(nLevel), line);
}
