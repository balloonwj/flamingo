/**
 *  客户端日志接口 CIULog 在 FlamingoSecretChat.dll 内的实现（日志桥接），做法与 FlamingoP2P 的 HostLogBridge.cpp 相同：
 *  格式化成「[secret] [调用处:行号] 正文」的 UTF-8 文本，经 SecretLogWriteLine 交给宿主，写进 exe 的同一个日志文件。
 *  「[调用处:行号] 正文」的格式化复用 FlamingoMedia 的 LogFormat。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include <windows.h>
#include <stdarg.h>
#include <string.h>

#include "../../Source/Common/Log/IULog.h"
#include "../../FlamingoMedia/Host/LogFormat.h"
#include "SecretLog.h"

namespace {

//把客户端日志级别（LOG_LEVEL_*）换算成跨模块接口的日志级别（secret_log_*）
int ToSecretLevel(long nLevel)
{
    if (nLevel >= LOG_LEVEL_ERROR)
        return secretchat::secret_log_error;
    if (nLevel == LOG_LEVEL_WARNING)
        return secretchat::secret_log_warning;
    return secretchat::secret_log_info;
}

//把前缀写进行首，返回前缀的字节数
int WritePrefix(char* line)
{
    const size_t prefixLen = strlen(secretchat::kSecretLogPrefix);
    memcpy(line, secretchat::kSecretLogPrefix, prefixLen);
    line[prefixLen] = '\0';
    return static_cast<int>(prefixLen);
}

} // namespace

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...)
{
    char line[secretchat::kSecretLogLineBytes];
    int prefixLen = WritePrefix(line);
    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineW(line + prefixLen, secretchat::kSecretLogLineBytes - prefixLen, pszFunctionSig,
                                          nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;
    return secretchat::SecretLogWriteLine(ToSecretLevel(nLevel), line);
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
    char line[secretchat::kSecretLogLineBytes];
    int prefixLen = WritePrefix(line);
    va_list args;
    va_start(args, pszFmt);
    int n = flamingomedia::FormatLogLineA(line + prefixLen, secretchat::kSecretLogLineBytes - prefixLen, pszFunctionSig,
                                          nLineNo, pszFmt, args);
    va_end(args);
    if (n < 0)
        return false;
    return secretchat::SecretLogWriteLine(ToSecretLevel(nLevel), line);
}
