/**
 *  客户端日志接口 CIULog 在 FlamingoFileShare.dll 内的实现（日志桥接）。
 *
 *  DLL 里的代码与 exe 一样使用 LOG_INFO / LOG_WARNING / LOG_ERROR 宏，宏展开为 CIULog::Log(...)。
 *  exe 里这两个函数由 Source/Common/Log/IULog.cpp 实现（写异步日志文件）；DLL 不编译那份实现，
 *  而是编译本文件：格式化成「[share] [调用处:行号] 正文」的 UTF-8 文本，经 ShareLogWriteLine
 *  交给宿主，最终写进 exe 的同一个日志文件。这样与 exe 共用的源文件不需要改任何日志调用。
 *
 *  「[调用处:行号] 正文」这一段的格式化直接复用 FlamingoMedia 的 LogFormat（与该 DLL 编译同一份
 *  FlamingoMedia/Host/LogFormat.cpp），两个 DLL 写出的日志行格式一致；本文件只在前面加 "[share] "。
 *
 *  只实现两个 Log 重载：Init / Uninit / SetLevel / Flush 由 exe 在进程启动与退出时调用，DLL 里
 *  不会用到；级别过滤由 exe 侧的日志实现统一完成。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <windows.h>
#include <stdarg.h>
#include <string.h>

#include "../../Source/Common/Log/IULog.h"
#include "../../FlamingoMedia/Host/LogFormat.h"
#include "ShareLog.h"

namespace {

//把客户端日志级别（LOG_LEVEL_*）换算成跨模块接口的日志级别（fileshare_log_*）
int ToShareLevel(long nLevel)
{
    if (nLevel >= LOG_LEVEL_ERROR)
        return fileshare::fileshare_log_error;
    if (nLevel == LOG_LEVEL_WARNING)
        return fileshare::fileshare_log_warning;
    return fileshare::fileshare_log_info;
}

//把前缀写进行首，返回前缀的字节数；前缀长度在编译期已知且远小于行缓冲，不会越界
int WritePrefix(char* line)
{
    const size_t prefixLen = strlen(fileshare::kShareLogPrefix);
    memcpy(line, fileshare::kShareLogPrefix, prefixLen);
    line[prefixLen] = '\0';
    return static_cast<int>(prefixLen);
}

} // namespace

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...)
{
    char line[fileshare::kShareLogLineBytes];
    int prefixLen = WritePrefix(line);

    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineW(line + prefixLen, fileshare::kShareLogLineBytes - prefixLen,
                                          pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;

    return fileshare::ShareLogWriteLine(ToShareLevel(nLevel), line);
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
    char line[fileshare::kShareLogLineBytes];
    int prefixLen = WritePrefix(line);

    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineA(line + prefixLen, fileshare::kShareLogLineBytes - prefixLen,
                                          pszFunctionSig, nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;

    return fileshare::ShareLogWriteLine(ToShareLevel(nLevel), line);
}
