/**
 *  DLL 内部日志出口的实现，见 P2PLog.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PLog.h"

#include <mutex>

namespace p2p {

namespace {

//保护 g_host 的互斥量。写日志时全程持有，保证 P2PLogSetHost(NULL) 返回之后
//不会再有线程拿着旧宿主指针调用 Log（宿主可能紧接着就被销毁）
std::mutex g_hostMutex;

//当前登记的宿主；未登记时为 NULL。只在持有 g_hostMutex 时读写
IP2PHost* g_host = NULL;

} // namespace

void P2PLogSetHost(IP2PHost* host)
{
    std::lock_guard<std::mutex> guard(g_hostMutex);
    g_host = host;
}

bool P2PLogWriteLine(int level, const char* text)
{
    if (text == NULL)
        return false;

    std::lock_guard<std::mutex> guard(g_hostMutex);
    if (g_host == NULL)
        return false;

    g_host->Log(level, text);
    return true;
}

} // namespace p2p
