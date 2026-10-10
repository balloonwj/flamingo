/**
 *  DLL 内部日志出口的实现，见 MediaLog.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "MediaLog.h"

#include <atomic>

namespace flamingomedia {

//当前登记的宿主。登记与解除在 UI 线程，写日志可能在任意线程，故用原子指针。
static std::atomic<IMediaHost*> s_logHost(NULL);

void MediaLogSetHost(IMediaHost* host)
{
    s_logHost.store(host);
}

bool MediaLogWriteLine(int level, const char* text)
{
    IMediaHost* host = s_logHost.load();
    if (host == NULL || text == NULL)
    {
        return false;
    }
    host->Log(level, text);
    return true;
}

} // namespace flamingomedia
