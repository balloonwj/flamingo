/**
 *  客户端重启自身：改了需要重启才生效的设置（目前是界面语言）后，由用户确认「立即重启」时使用。
 *
 *  做法：先用「本 exe 路径 + --wait-pid <本进程 pid>」启动新进程，再走正常的退出流程。新进程在启动最早期
 *  （App/Startup.cpp，日志初始化之前）看到 --wait-pid，就等旧进程退出后再继续，避免新旧两个进程同时打开
 *  同一份配置与本地数据库。等待有上限，旧进程迟迟不退时照常启动。
 *
 *  典型用法：
 *      if (apprestart::LaunchRestart())
 *          RequestAppExit();                                  //调用方自己走正常的退出流程
 *      ...
 *      DWORD pid = apprestart::ParseWaitPid(lpstrCmdLine);     //新进程启动时
 *      if (pid != 0)
 *          apprestart::WaitForProcessExit(pid, apprestart::kWaitPreviousMs);
 *
 *  本文件与 AppRestart.cpp 只依赖 <windows.h> 与标准库，不包含 stdafx.h；在 Flamingo.vcxproj 里须标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <string>

namespace apprestart {

//命令行里携带旧进程 pid 的开关
extern const wchar_t* const kWaitPidSwitch;

//新进程等待旧进程退出的最长时间（毫秒）。旧进程正常退出只需一两秒，超过上限说明它卡住了，新进程照常启动
const DWORD kWaitPreviousMs = 15000;

/**
 *  组装重启用的命令行。
 *  @param exePath exe 的完整路径
 *  @param pid     当前（旧）进程的 pid
 *  @return 形如 "\"E:\...\Flamingo.exe\" --wait-pid 1234"
 */
std::wstring BuildRestartCommandLine(const std::wstring& exePath, DWORD pid);

/**
 *  从命令行里取 --wait-pid 之后的 pid。
 *  @param cmdLine 命令行，可为 NULL；可以含其它参数
 *  @return pid；没有这个开关、或其后不是正整数时返回 0
 */
DWORD ParseWaitPid(const wchar_t* cmdLine);

/**
 *  等某个进程退出。
 *  @param pid       进程 pid
 *  @param timeoutMs 最长等待时间（毫秒）
 *  @return 进程已退出（或根本不存在）返回 true；超时仍在运行返回 false
 */
bool WaitForProcessExit(DWORD pid, DWORD timeoutMs);

/**
 *  以 BuildRestartCommandLine 的命令行启动本 exe 的新进程。调用成功后，调用方应立即走正常的退出流程。
 *  @return 新进程启动成功返回 true
 */
bool LaunchRestart();

} // namespace apprestart
