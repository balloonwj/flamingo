/**
 *  FlamingoSecretChat.dll 内部日志出口的实现，见 SecretLog.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "SecretLog.h"

#include <mutex>

namespace secretchat {

namespace {

//保护 g_host 的互斥量。写日志时全程持有，保证 SecretLogSetHost(NULL) 返回之后不会再有线程拿着旧宿主指针写日志
std::mutex g_hostMutex;

//当前登记的宿主；未登记时为 NULL。只在持有 g_hostMutex 时读写
ISecretChatHost* g_host = NULL;

} // namespace

void SecretLogSetHost(ISecretChatHost* host)
{
    std::lock_guard<std::mutex> guard(g_hostMutex);
    g_host = host;
}

bool SecretLogWriteLine(int level, const char* text)
{
    if (text == NULL)
        return false;
    std::lock_guard<std::mutex> guard(g_hostMutex);
    if (g_host == NULL)
        return false;
    g_host->Log(level, text);
    return true;
}

} // namespace secretchat
