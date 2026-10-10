/**
 *  FlamingoMedia.dll 内部的「是否锁屏中」标志（docs/锁屏方案.md 第 4.9 节，接口版本 13）。
 *
 *  宿主经 IMediaModule::SetScreenLocked 通知锁屏开始与结束，模块据此置位；来电、群通话邀请、远程桌面请求、
 *  屏幕共享开始通知等处理函数查询它，锁屏期间静默处理。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

namespace flamingomedia {

/**
 *  置位或清除锁屏标志。由 CMediaModule::SetScreenLocked 调用。线程安全。
 *  @param locked true 表示锁屏中
 */
void SetMediaScreenLocked(bool locked);

/**
 *  当前是否处于锁屏中。任意线程可调。
 */
bool IsMediaScreenLocked();

} // namespace flamingomedia
