/**
 *  客户端重启自身（App/AppRestart）的单元测试，用例 AR1 ~ AR3（docs/多语言方案.md 第 11 节 1d）。
 *
 *    - AR1 重启命令行的组装与解析互为逆运算（路径含空格）
 *    - AR2 解析 --wait-pid：与其它参数混排时取得到；缺 pid、非数字、写成 --wait-pid=N、pid 后紧跟字母时都返回 0
 *    - AR3 等待进程退出：pid 为 0 或进程已退出时立即返回 true；进程仍在运行时超时返回 false
 *
 *  LaunchRestart 不在这里调用（它会把测试程序自己再启动一遍），由实机核对覆盖。
 *  框架 Catch2 v3，标签 [apprestart]。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string.h>
#include <string>
#include <vector>

#include "../App/AppRestart.h"

namespace {

//等一个短命子进程退出的上限（毫秒）
const DWORD kChildExitWaitMs = 10000;

//检查「进程仍在运行时超时返回 false」用的超时（毫秒）
const DWORD kShortTimeoutMs = 50;

} // namespace

TEST_CASE("AR1 重启命令行的组装与解析", "[apprestart]")
{
    std::wstring cmd = apprestart::BuildRestartCommandLine(L"C:\\Program Files\\Flamingo\\Flamingo.exe", 1234);
    CHECK(cmd == L"\"C:\\Program Files\\Flamingo\\Flamingo.exe\" --wait-pid 1234");
    CHECK(apprestart::ParseWaitPid(cmd.c_str()) == 1234);
}

TEST_CASE("AR2 解析 --wait-pid", "[apprestart]")
{
    CHECK(apprestart::ParseWaitPid(NULL) == 0);
    CHECK(apprestart::ParseWaitPid(L"") == 0);
    CHECK(apprestart::ParseWaitPid(L"--syssettings") == 0);
    CHECK(apprestart::ParseWaitPid(L"--syssettings --wait-pid 42") == 42);
    CHECK(apprestart::ParseWaitPid(L"--wait-pid 42 --mainpanel") == 42);
    CHECK(apprestart::ParseWaitPid(L"--wait-pid\t7") == 7);
    CHECK(apprestart::ParseWaitPid(L"--wait-pid") == 0);
    CHECK(apprestart::ParseWaitPid(L"--wait-pid abc") == 0);
    CHECK(apprestart::ParseWaitPid(L"--wait-pid=42") == 0);
    CHECK(apprestart::ParseWaitPid(L"--wait-pid 42x") == 0);
}

TEST_CASE("AR3 等待进程退出", "[apprestart]")
{
    CHECK(apprestart::WaitForProcessExit(0, kShortTimeoutMs));

    //一个很快就退出的子进程：等它退出后，再按它的 pid 等待应立即得到 true
    wchar_t cmd[] = L"cmd.exe /c exit 0";
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    REQUIRE(::CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));
    DWORD childPid = pi.dwProcessId;
    CHECK(apprestart::WaitForProcessExit(childPid, kChildExitWaitMs));
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    //测试进程自己一直在运行：短超时后返回 false
    CHECK_FALSE(apprestart::WaitForProcessExit(::GetCurrentProcessId(), kShortTimeoutMs));
}
