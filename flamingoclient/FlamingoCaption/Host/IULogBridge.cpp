/**
 *  客户端日志接口 CIULog 在 FlamingoCaption.dll 内的实现（日志桥接）。
 *
 *  做法与 FlamingoMedia/Host/IULogBridge.cpp 相同：DLL 里的代码与 exe 一样使用 LOG_INFO 等宏，宏展开为
 *  CIULog::Log(...)；本文件把它格式化成「[调用处:行号] 正文」的 UTF-8 文本（格式化函数与 FlamingoMedia 共用
 *  FlamingoMedia/Host/LogFormat.cpp），经 CaptionLogWriteLine 交给宿主，最终写进 exe 的日志文件。
 *  只实现两个 Log 重载；级别过滤由 exe 侧统一完成。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <windows.h>
#include <stdarg.h>

#include "../../Source/Common/Log/IULog.h"
#include "../../FlamingoMedia/Host/LogFormat.h"
#include "CaptionLog.h"

namespace {

//把客户端日志级别（LOG_LEVEL_*）换算成字幕 DLL 接口的日志级别（caption_log_*）
int ToCaptionLevel(long nLevel)
{
    if (nLevel >= LOG_LEVEL_ERROR)
        return caption::caption_log_error;
    if (nLevel == LOG_LEVEL_WARNING)
        return caption::caption_log_warning;
    return caption::caption_log_info;
}

} // namespace

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...)
{
    char line[caption::kMaxLogLineBytes];
    va_list args;
    va_start(args, pszFmt);
    const int n = flamingomedia::FormatLogLineW(line, sizeof(line), pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;
    return caption::CaptionLogWriteLine(ToCaptionLevel(nLevel), line);
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
    char line[caption::kMaxLogLineBytes];
    va_list args;
    va_start(args, pszFmt);
    const int n = flamingomedia::FormatLogLineA(line, sizeof(line), pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;
    return caption::CaptionLogWriteLine(ToCaptionLevel(nLevel), line);
}
