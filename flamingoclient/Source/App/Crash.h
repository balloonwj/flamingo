/**
 *  Crash.h
 *
 *  崩溃转储的对外入口。转储由两条路径产出，各管一段（详见 docs/崩溃转储方案.md）：
 *
 *    · <u>进程内</u>：Crash.cpp 里的全局对象在启动时注册 SetUnhandledExceptionFilter，
 *      崩溃时就地 MiniDumpWriteDump。覆盖空指针、非法访问这类普通异常。
 *    · <u>系统级</u>：Windows 错误报告（WER）的 LocalDumps。覆盖进程内那条<u>接不住</u>的
 *      情形 —— 主要是 __fastfail（栈 cookie 被覆盖，0xC0000409）：它绕过整个异常分发链、
 *      直接由内核终止进程，注册过的过滤器根本没有机会运行（2026-08-12 实测确认）。
 *
 *  两条路径的产物都落在 <exe 目录>\Users\dump\，排查时不必分头去找。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

/**
 *  按需把本程序的 WER LocalDumps 配置写进注册表。
 *
 *  该配置只能写在 HKLM（没有当前用户级的等价项），因此需要管理员权限。本函数的策略是
 *  <u>只在确认自身已以管理员权限运行时才写，否则静默跳过</u>：
 *
 *    · <u>不主动请求提权</u>、不弹任何界面、不打断启动流程。
 *    · 未提权时只记一条日志说明跳过。此时进程内那条转储路径照常工作，只是 __fastfail
 *      那类崩溃拿不到转储而已。
 *    · 写入是幂等的，且配置是机器级、长期有效，所以<u>一生只需成功写入一次</u>：用户哪天
 *      恰好以管理员身份启动过一次客户端，之后一直以普通权限运行，WER 兜底也一直生效。
 *
 *  调用时机：启动早期、<u>g_szHomePath 就绪之后</u>（转储目录由它拼出）。日志已初始化时
 *  调用更好 —— 否则那两条说明性的日志会丢。
 *
 *  副作用：可能创建转储目录 <exe 目录>\Users\dump\；已提权时写注册表
 *  HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\Flamingo.exe。
 */
void ConfigureCrashDumpEnvironment();

#include <string>

/**
 *  取转储目录的绝对路径（<exe 目录>\Users\dump，不带结尾的反斜杠），目录不存在时先创建。
 *  供界面线程无响应检测（HangWatchdog.h）把转储写到与崩溃转储相同的目录（bugs.md BUG-120）。
 *  调用时机：g_szHomePath 就绪之后。
 *  @return 目录可用时返回路径；创建失败返回空串
 */
std::wstring PrepareCrashDumpDirectory();
