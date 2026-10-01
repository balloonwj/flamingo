/**
 *  客户端日志接口的实现：把 LOG_INFO 等宏的调用格式化成一行正文，交给异步日志 CAsyncLog。
 *  说明见 IULog.h。
 *
 *  balloonwj@qq.com   2026-10-01（本次改写为异步日志；原文件的创建日期不详）
 */
#include "stdafx.h"
#include "IULog.h"
#include "..\Util\EncodeUtil.h"
#include <stdarg.h>
#include <stdio.h>
#include <string>

std::atomic<bool> CIULog::m_bInited(false);
std::atomic<long> CIULog::m_nLevel(LOG_LEVEL_INFO);

namespace {

//逐级创建目录，已存在的层级跳过。pszDirectory 为不带文件名的目录路径，分隔符可以是 \ 或 /
void CreateDirectoryChain(const std::wstring& strDirectory)
{
    std::wstring strPath(strDirectory);
    for (size_t i = 0; i < strPath.size(); ++i)
    {
        if (strPath[i] == L'/')
            strPath[i] = L'\\';
    }

    //从第一个分隔符之后开始逐段创建；盘符（"D:"）与网络路径开头（"\\server"）这两段
    //本身不是可以创建的目录，CreateDirectory 对它们失败即可，不影响后面各段
    for (size_t pos = strPath.find(L'\\', 1); pos != std::wstring::npos; pos = strPath.find(L'\\', pos + 1))
    {
        ::CreateDirectoryW(strPath.substr(0, pos).c_str(), NULL);
    }
    ::CreateDirectoryW(strPath.c_str(), NULL);
}

//把配置文件里的 loglevel 取值换算成 CAsyncLog 的日志级别，规则见 CIULog::SetLevel 的注释
LOG_LEVEL ConfigLevelToLogLevel(int nConfigLevel)
{
    //loglevel 取值：只输出 WARNING 及以上
    const int kConfigLevelWarning = 1;
    //loglevel 取值：只输出 ERROR
    const int kConfigLevelError = 2;

    if (nConfigLevel >= kConfigLevelError)
        return LOG_LEVEL_ERROR;
    if (nConfigLevel == kConfigLevelWarning)
        return LOG_LEVEL_WARNING;
    return LOG_LEVEL_INFO;
}

} // namespace

bool CIULog::Init(bool bToFile, bool bTruncateLongLog, PCWSTR pszLogFilePathPrefix)
{
    //重复初始化时先停掉上一轮的写日志线程；直接再 init 会把一个仍在运行的线程对象覆盖掉，
    //std::thread 在未 join 时被销毁会让进程直接终止
    if (m_bInited)
        Uninit();

    if (!bToFile)
    {
        CAsyncLog::init(nullptr, bTruncateLongLog);
        m_bInited = true;
        return true;
    }

    if (pszLogFilePathPrefix == NULL || pszLogFilePathPrefix[0] == L'\0')
        return false;

    //日志目录：前缀里最后一个分隔符之前的部分
    const std::wstring strPrefix(pszLogFilePathPrefix);
    const size_t nLastSep = strPrefix.find_last_of(L"\\/");
    if (nLastSep != std::wstring::npos)
        CreateDirectoryChain(strPrefix.substr(0, nLastSep));

    //CAsyncLog 以 UTF-8 接收文件名，内部再转回宽字符打开，路径里的任何字符都不会丢失
    const std::string strPrefixUtf8 = EncodeUtil::UnicodeToUtf8(strPrefix);
    CAsyncLog::init(strPrefixUtf8.c_str(), bTruncateLongLog);
    CAsyncLog::setLevel((LOG_LEVEL)m_nLevel.load());
    m_bInited = true;
    return true;
}

void CIULog::Uninit()
{
    //先置标志：此后到达的日志直接输出到调试器，不再进入即将没有人处理的队列
    m_bInited = false;
    CAsyncLog::uninit();
}

void CIULog::SetLevel(int nConfigLevel)
{
    const LOG_LEVEL level = ConfigLevelToLogLevel(nConfigLevel);
    m_nLevel = level;
    CAsyncLog::setLevel(level);
}

bool CIULog::Flush(int nTimeoutMs)
{
    if (!m_bInited)
        return false;

    return CAsyncLog::flush(nTimeoutMs);
}

bool CIULog::Emit(long nLevel, PCSTR pszFunctionSig, int nLineNo, const std::string& strMsg)
{
    if (m_bInited)
        return CAsyncLog::output(nLevel, pszFunctionSig, nLineNo, "%s", strMsg.c_str());

    //未初始化：没有写日志线程，只输出到调试器
    std::string strLine("[");
    strLine += pszFunctionSig;
    strLine += "]";
    strLine += strMsg;
    strLine += "\n";
    ::OutputDebugStringA(strLine.c_str());
    return true;
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...)
{
    if (nLevel < m_nLevel)
        return false;

    va_list ap;
    va_start(ap, pszFmt);
    const int nLength = _vscwprintf(pszFmt, ap);
    va_end(ap);
    if (nLength < 0)
        return false;

    //多留一个字符放结尾的 '\0'，格式化完再去掉
    std::wstring strMsg((size_t)nLength + 1, L'\0');
    va_list aq;
    va_start(aq, pszFmt);
    _vsnwprintf_s(&strMsg[0], strMsg.size(), _TRUNCATE, pszFmt, aq);
    va_end(aq);
    strMsg.resize((size_t)nLength);

    return Emit(nLevel, pszFunctionSig, nLineNo, EncodeUtil::UnicodeToUtf8(strMsg));
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
    if (nLevel < m_nLevel)
        return false;

    va_list ap;
    va_start(ap, pszFmt);
    const int nLength = _vscprintf(pszFmt, ap);
    va_end(ap);
    if (nLength < 0)
        return false;

    //多留一个字符放结尾的 '\0'，格式化完再去掉
    std::string strMsg((size_t)nLength + 1, '\0');
    va_list aq;
    va_start(aq, pszFmt);
    _vsnprintf_s(&strMsg[0], strMsg.size(), _TRUNCATE, pszFmt, aq);
    va_end(aq);
    strMsg.resize((size_t)nLength);

    return Emit(nLevel, pszFunctionSig, nLineNo, strMsg);
}
