/**
 *  ShotLog.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ShotLog.h"

#include <mutex>

namespace flamingoshot {

namespace {

//保护 g_host：截图界面在宿主的界面线程上运行，但抓屏、编码等将来可能放到别的线程，统一加锁
std::mutex g_hostMutex;

//登记的宿主接口；NULL 表示尚未初始化或已关闭
IScreenshotHost* g_host = NULL;

} // namespace

void ShotLogSetHost(IScreenshotHost* host)
{
    std::lock_guard<std::mutex> guard(g_hostMutex);
    g_host = host;
}

bool ShotLogWriteLine(int level, const char* text)
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

} // namespace flamingoshot
