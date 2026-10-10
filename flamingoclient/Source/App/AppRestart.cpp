/**
 *  客户端重启自身的实现，见 AppRestart.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "AppRestart.h"

#include <stdlib.h>
#include <string.h>
#include <vector>

namespace apprestart {

const wchar_t* const kWaitPidSwitch = L"--wait-pid";

namespace {

//取本 exe 的完整路径时缓冲区的字符数上限（长路径）
const DWORD kMaxExePathChars = 32768;

//pid 转十进制文字的缓冲区大小
const size_t kPidTextChars = 16;

//十进制
const int kDecimalBase = 10;

std::wstring GetSelfExePath()
{
    std::vector<wchar_t> buf(kMaxExePathChars, L'\0');
    DWORD len = ::GetModuleFileNameW(NULL, &buf[0], kMaxExePathChars);
    if (len == 0 || len >= kMaxExePathChars)
        return std::wstring();
    return std::wstring(&buf[0], len);
}

} // namespace

std::wstring BuildRestartCommandLine(const std::wstring& exePath, DWORD pid)
{
    wchar_t pidText[kPidTextChars] = { 0 };
    swprintf_s(pidText, L"%lu", pid);
    return L"\"" + exePath + L"\" " + kWaitPidSwitch + L" " + pidText;
}

DWORD ParseWaitPid(const wchar_t* cmdLine)
{
    if (cmdLine == NULL)
        return 0;
    const wchar_t* pos = wcsstr(cmdLine, kWaitPidSwitch);
    if (pos == NULL)
        return 0;
    pos += wcslen(kWaitPidSwitch);
    //开关之后至少一个空白，再是数字
    if (*pos != L' ' && *pos != L'\t')
        return 0;
    while (*pos == L' ' || *pos == L'\t')
        ++pos;
    if (*pos < L'0' || *pos > L'9')
        return 0;
    wchar_t* end = NULL;
    unsigned long value = wcstoul(pos, &end, kDecimalBase);
    if (end == pos || (*end != L'\0' && *end != L' ' && *end != L'\t'))
        return 0;
    return (DWORD)value;
}

bool WaitForProcessExit(DWORD pid, DWORD timeoutMs)
{
    if (pid == 0)
        return true;
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == NULL)
        return true;    //进程已不存在（或无权打开，此时也无从等待）
    DWORD r = ::WaitForSingleObject(process, timeoutMs);
    ::CloseHandle(process);
    return r == WAIT_OBJECT_0;
}

bool LaunchRestart()
{
    std::wstring exePath = GetSelfExePath();
    if (exePath.empty())
        return false;
    std::wstring cmd = BuildRestartCommandLine(exePath, ::GetCurrentProcessId());
    //CreateProcessW 可能改写命令行缓冲区，必须传可写的副本
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!::CreateProcessW(exePath.c_str(), &cmdBuf[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return false;
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

} // namespace apprestart
