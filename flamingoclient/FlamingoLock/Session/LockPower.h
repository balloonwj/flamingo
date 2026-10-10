/**
 *  锁屏界面上「关机」「重启」的执行：启用本进程的关机特权后调用 ExitWindowsEx。
 *
 *  用 EWX_FORCEIFHUNG：不强制关闭仍在响应的程序（它们照常收到 WM_QUERYENDSESSION，有未保存内容的程序
 *  可以拦下关机，由 Windows 显示「这些应用阻止关机」的画面），只强制关闭没有响应的程序。
 *  方案见 docs/锁屏方案.md 第 4.8 节。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

namespace flamingolock {

/**
 *  电源操作的种类。
 */
enum LockPowerAction
{
    kPowerShutdown = 0,     // 关机并断电
    kPowerRestart,          // 重启
};

/**
 *  执行关机或重启。立即返回，系统随后会向各程序发 WM_QUERYENDSESSION。
 *  @param action 关机或重启
 *  @return 系统接受了请求返回 true；特权启用失败或 ExitWindowsEx 失败返回 false（并记日志）
 */
bool RunPowerAction(LockPowerAction action);

} // namespace flamingolock
