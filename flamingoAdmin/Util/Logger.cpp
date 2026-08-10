/**
 *  flamingoAdmin —— 轻量文件日志器实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Util/Logger.h"

#include <cstdarg>
#include <cstdio>

namespace flamingoadmin {

namespace {

// 日志文件名（位于 exe 同目录）。
const TCHAR* const kLogFileName = _T("flamingoAdmin.log");

// 单条日志格式化缓冲区大小（宽字符数）。足够容纳一条关键路径日志。
const int kLogBufferChars = 1024;

// 把 LogLevel 映射成行首的 4 字符标记，便于在日志里对齐与 grep。
const TCHAR* LevelTag(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:  return _T("INFO");
    case LogLevel::Warn:  return _T("WARN");
    case LogLevel::Error: return _T("ERRO");
    default:              return _T("????");
    }
}

} // anonymous namespace

CString Logger::ResolveLogPath()
{
    // 取 exe 完整路径，截到目录部分，再拼日志文件名。
    TCHAR exePath[MAX_PATH] = {};
    ::GetModuleFileName(nullptr, exePath, MAX_PATH);

    CString dir = exePath;
    int slash = dir.ReverseFind(_T('\\'));
    if (slash > 0)
    {
        dir = dir.Left(slash);
    }
    return dir + _T("\\") + kLogFileName;
}

void Logger::Init()
{
    // 启动分隔行：便于在日志文件里区分不同次运行。
    Writef(LogLevel::Info, _T("================ flamingoAdmin 启动 ================"));
}

void Logger::Writef(LogLevel level, LPCTSTR format, ...)
{
    // 1) 格式化日志正文。超长按 _TRUNCATE 截断，不溢出缓冲区。
    TCHAR body[kLogBufferChars] = {};
    va_list args;
    va_start(args, format);
    _vsntprintf_s(body, kLogBufferChars, _TRUNCATE, format, args);
    va_end(args);

    // 2) 取本地时间，拼成「时间 [级别] 正文」整行（带回车换行）。
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    CString line;
    line.Format(_T("%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] %s\r\n"),
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                LevelTag(level), body);

    // 3) 整行转 UTF-8 字节序列。utf8Len 含结尾 '\0'。
    int utf8Len = ::WideCharToMultiByte(CP_UTF8, 0, line, -1,
                                        nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 1)
    {
        return;     // 转换失败或空行，直接放弃本条
    }
    std::vector<char> utf8(static_cast<size_t>(utf8Len));
    ::WideCharToMultiByte(CP_UTF8, 0, line, -1,
                          utf8.data(), utf8Len, nullptr, nullptr);

    // 4) 以追加模式写文件。日志低频产生，每次开关文件即可，无需常驻句柄。
    //    日志路径在首次调用时解析一次并缓存。
    static CString s_logPath = ResolveLogPath();
    FILE* fp = nullptr;
    if (_tfopen_s(&fp, s_logPath, _T("ab")) == 0 && fp != nullptr)
    {
        // 写 utf8Len - 1 字节：不写结尾的 '\0'。
        fwrite(utf8.data(), 1, static_cast<size_t>(utf8Len - 1), fp);
        fclose(fp);
    }

#ifdef _DEBUG
    // Debug 配置下同时输出到调试器输出窗口，便于实时观察。
    ::OutputDebugString(line);
#endif
}

} // namespace flamingoadmin
