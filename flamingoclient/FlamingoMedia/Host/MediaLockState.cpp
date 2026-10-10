/**
 *  MediaLockState.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "MediaLockState.h"

#include <atomic>

namespace flamingomedia {

namespace {

//是否锁屏中：界面线程写，各控制器（界面线程）与媒体工作线程都可能读
std::atomic<bool> g_screenLocked(false);

} // namespace

void SetMediaScreenLocked(bool locked)
{
    g_screenLocked.store(locked);
}

bool IsMediaScreenLocked()
{
    return g_screenLocked.load();
}

} // namespace flamingomedia
