/**
 *  FlamingoLink.dll —— 日志转发的实现，说明见 LinkLog.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/LinkLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <atomic>

#include "FlamingoLink/FlamingoLinkApi.h"

namespace flamingolink {

namespace {

//一行日志格式化后的最大字节数，超出部分截断
const size_t kLogLineBytes = 1024;

//登记的宿主接口；连接线程会读取，故用原子量
std::atomic<ILinkHost*> g_logHost(NULL);

} // anonymous namespace

void SetLogHost(ILinkHost* host)
{
    g_logHost.store(host);
}

void LinkLogf(int level, const char* fmt, ...)
{
    ILinkHost* host = g_logHost.load();
    if (host == NULL || fmt == NULL)
        return;

    char line[kLogLineBytes] = { 0 };
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
    va_end(args);

    host->Log(level, line);
}

} // namespace flamingolink
