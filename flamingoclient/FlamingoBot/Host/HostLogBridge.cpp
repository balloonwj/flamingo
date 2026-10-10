/**
 *  客户端日志接口 CIULog 在 FlamingoBot.dll 内的实现（日志桥接）。
 *
 *  DLL 里的代码与 exe 一样使用 LOG_INFO / LOG_WARNING / LOG_ERROR 宏，宏展开为 CIULog::Log(...)。
 *  exe 里这两个函数由 Source/Common/Log/IULog.cpp 实现；DLL 不编译那份实现，而是编译本文件：
 *  格式化成「[bot] [调用处:行号] 正文」的 UTF-8 文本，经 BotLogWriteLine 交给宿主，写进 exe 的同一个日志文件。
 *  「[调用处:行号] 正文」这一段的格式化复用 FlamingoMedia 的 LogFormat，与其它 DLL 写出的日志行格式一致。
 *  单元测试工程 FlamingoUnit 已经编译了 exe 的日志实现，不编译本文件。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include <windows.h>
#include <stdarg.h>
#include <string.h>

#include "../../Source/Common/Log/IULog.h"
#include "../../FlamingoMedia/Host/LogFormat.h"
#include "BotLog.h"

namespace {

//把客户端日志级别（LOG_LEVEL_*）换算成跨模块接口的日志级别（bot_log_*）
int ToBotLevel(long nLevel)
{
    if (nLevel >= LOG_LEVEL_ERROR)
        return flamingobot::bot_log_error;
    if (nLevel == LOG_LEVEL_WARNING)
        return flamingobot::bot_log_warning;
    return flamingobot::bot_log_info;
}

//把前缀写进行首，返回前缀的字节数；前缀长度在编译期已知且远小于行缓冲，不会越界
int WritePrefix(char* line)
{
    const size_t prefixLen = strlen(flamingobot::kBotLogPrefix);
    memcpy(line, flamingobot::kBotLogPrefix, prefixLen);
    line[prefixLen] = '\0';
    return static_cast<int>(prefixLen);
}

} // namespace

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...)
{
    char line[flamingobot::kBotLogLineBytes];
    const int prefixLen = WritePrefix(line);
    va_list args;
    va_start(args, pszFmt);
    const int n = flamingomedia::FormatLogLineW(line + prefixLen, flamingobot::kBotLogLineBytes - prefixLen,
                                                pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;
    return flamingobot::BotLogWriteLine(ToBotLevel(nLevel), line);
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
    char line[flamingobot::kBotLogLineBytes];
    const int prefixLen = WritePrefix(line);
    va_list args;
    va_start(args, pszFmt);
    const int n = flamingomedia::FormatLogLineA(line + prefixLen, flamingobot::kBotLogLineBytes - prefixLen,
                                                pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;
    return flamingobot::BotLogWriteLine(ToBotLevel(nLevel), line);
}
