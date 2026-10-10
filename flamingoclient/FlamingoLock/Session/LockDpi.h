/**
 *  锁屏窗口用到的 DPI 小工具：把锁屏线程临时切到 Per-Monitor-V2（锁屏窗口按物理像素建在显示器上），
 *  以及取一块显示器的缩放比例。Win10 1607 之前的系统没有线程级 DPI 上下文、Win8.1 之前没有按显示器取 DPI，
 *  一律在运行时动态取用，取不到时退回系统 DPI 的行为。
 *
 *  原先写在 LockSession.cpp 里，2026-10-07 设置窗口的「预览」（LockPreviewSession.cpp）也要用，提到这里共用。
 *
 *  典型用法（锁屏线程）：
 *      HANDLE old = EnterPerMonitorV2Dpi();
 *      ...取显示器矩形、建锁屏窗口...
 *      RestoreThreadDpi(old);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>

namespace flamingolock {

/**
 *  把当前线程的 DPI 上下文切到 Per-Monitor-V2。
 *  @return 切换前的上下文，交给 RestoreThreadDpi 恢复；系统不支持线程级 DPI 上下文时返回 NULL（什么也没做）
 */
HANDLE EnterPerMonitorV2Dpi();

/**
 *  恢复 EnterPerMonitorV2Dpi 之前的线程 DPI 上下文。
 *  @param oldContext EnterPerMonitorV2Dpi 的返回值；为 NULL 时什么也不做
 */
void RestoreThreadDpi(HANDLE oldContext);

/**
 *  一块显示器的缩放比例（有效 DPI / 96）。
 *  @param monitor 显示器句柄
 *  @return 缩放比例；取不到时返回 1
 */
float LockMonitorScale(HMONITOR monitor);

} // namespace flamingolock
