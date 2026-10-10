/**
 *  LockLog.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "LockLog.h"

#include <mutex>

namespace flamingolock {

namespace {

//保护 g_host：日志可能同时来自宿主的界面线程（Init、RequestLock）与锁屏线程
std::mutex g_hostMutex;

//登记的宿主接口；NULL 表示尚未初始化或已关闭
ILockHost* g_host = NULL;

} // namespace

void LockLogSetHost(ILockHost* host)
{
    std::lock_guard<std::mutex> guard(g_hostMutex);
    g_host = host;
}

bool LockLogWriteLine(int level, const char* text)
{
    if (text == NULL)
    {
        return false;
    }
    std::lock_guard<std::mutex> guard(g_hostMutex);
    if (g_host == NULL)
    {
        return false;
    }
    g_host->Log(level, text);
    return true;
}

} // namespace flamingolock
