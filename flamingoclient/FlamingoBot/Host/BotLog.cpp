/**
 *  BotLog.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotLog.h"

#include <mutex>

namespace flamingobot {

namespace {

//保护 g_host：日志可能同时来自界面线程与网络线程（OnServerFrame 里丢帧时写的警告）
std::mutex g_hostMutex;

//登记的宿主接口；NULL 表示尚未初始化或已关闭
IBotHost* g_host = NULL;

} // namespace

void BotLogSetHost(IBotHost* host)
{
    std::lock_guard<std::mutex> guard(g_hostMutex);
    g_host = host;
}

bool BotLogWriteLine(int level, const char* text)
{
    if (text == NULL)
        return false;
    std::lock_guard<std::mutex> guard(g_hostMutex);
    if (g_host == NULL)
        return false;
    g_host->Log(level, text);
    return true;
}

} // namespace flamingobot
