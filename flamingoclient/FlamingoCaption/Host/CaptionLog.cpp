/**
 *  FlamingoCaption.dll 的日志出口，实现见 CaptionLog.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionLog.h"

#include <mutex>

namespace caption {

namespace {

//保护 g_host：识别线程与 UI 线程都会写日志，Shutdown 时 UI 线程把它设为 NULL
std::mutex g_hostMutex;
//日志宿主（不拥有），为 NULL 时日志丢弃
ICaptionHost* g_host = NULL;

} // namespace

void CaptionLogSetHost(ICaptionHost* host)
{
    std::lock_guard<std::mutex> lock(g_hostMutex);
    g_host = host;
}

bool CaptionLogWriteLine(int level, const char* text)
{
    std::lock_guard<std::mutex> lock(g_hostMutex);
    if (g_host == NULL || text == NULL)
        return false;
    g_host->Log(level, text);
    return true;
}

} // namespace caption
