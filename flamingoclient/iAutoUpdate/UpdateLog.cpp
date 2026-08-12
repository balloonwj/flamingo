/**
 *  更新器日志的实现，UpdateLog.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "UpdateLog.h"

#include <vector>

namespace iupdate {

namespace {

//时间戳缓冲长度（字符数）。格式 "[2026-08-12 18:30:45] " 固定 22 字符，留足余量。
const size_t kTimestampBufLen = 64;

//UTF-8 BOM。写在新建文件的开头，好让记事本一类的工具正确识别中文。
const unsigned char kUtf8Bom[3] = { 0xEF, 0xBB, 0xBF };

//把宽字符串转成 UTF-8 字节。转换失败返回空串。
std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }

    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                             NULL, 0, NULL, NULL);
    if (needed <= 0)
    {
        return std::string();
    }

    std::string result;
    result.resize((size_t)needed);
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                          &result[0], needed, NULL, NULL);
    return result;
}

//取当前本地时间的时间戳前缀，形如 "[2026-08-12 18:30:45] "。
std::wstring MakeTimestamp()
{
    SYSTEMTIME st;
    ::GetLocalTime(&st);

    wchar_t buf[kTimestampBufLen] = { 0 };
    ::swprintf_s(buf, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

} // namespace

UpdateLog& UpdateLog::Instance()
{
    static UpdateLog s_instance;
    return s_instance;
}

UpdateLog::UpdateLog()
    : m_file(INVALID_HANDLE_VALUE)
{
}

UpdateLog::~UpdateLog()
{
    Close();
}

bool UpdateLog::Open(const std::wstring& logDir)
{
    Close();

    if (logDir.empty())
    {
        return false;
    }

    //目录可能还不存在（全新安装后第一次升级），先建一次；已存在时 CreateDirectory
    //会失败并置 ERROR_ALREADY_EXISTS，这是正常情况，不必处理。
    ::CreateDirectoryW(logDir.c_str(), NULL);

    std::wstring path = logDir;
    if (path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
    {
        path += L'\\';
    }
    path += L"update.log";

    //FILE_SHARE_READ：允许升级过程中有人打开日志看进展
    m_file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    //新建的空文件先写 BOM
    LARGE_INTEGER size;
    size.QuadPart = 0;
    if (::GetFileSizeEx(m_file, &size) && size.QuadPart == 0)
    {
        DWORD written = 0;
        ::WriteFile(m_file, kUtf8Bom, sizeof(kUtf8Bom), &written, NULL);
    }

    return true;
}

void UpdateLog::Close()
{
    if (m_file != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(m_file);
        m_file = INVALID_HANDLE_VALUE;
    }
}

void UpdateLog::Write(const std::wstring& text)
{
    if (m_file == INVALID_HANDLE_VALUE)
    {
        //日志没打开就当无事发生：日志本身出问题绝不该影响升级流程
        return;
    }

    const std::string line = ToUtf8(MakeTimestamp() + text + L"\r\n");
    if (line.empty())
    {
        return;
    }

    DWORD written = 0;
    ::WriteFile(m_file, line.c_str(), (DWORD)line.size(), &written, NULL);
    //每行都刷盘：升级过程中进程可能异常终止，缓存在内存里的日志就丢了，
    //而那恰恰是最需要看的几行。
    ::FlushFileBuffers(m_file);
}

void UpdateLog::WriteWithError(const std::wstring& text, DWORD lastError)
{
    wchar_t buf[kTimestampBufLen] = { 0 };
    ::swprintf_s(buf, L"，错误码 %u", lastError);
    Write(text + buf);
}

} // namespace iupdate
