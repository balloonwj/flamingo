/**
 *  MemoClock.cpp
 *
 *  读系统时钟的几个函数的实现，用的是 Win32 的 GetLocalTime 与 CRT 的 localtime_s。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h（工程里对应的 PrecompiledHeader 设为 NotUsing）
#include "MemoClock.h"
#include <windows.h>
#include <time.h>

namespace memo {

//毫秒与秒、分钟的换算
const int64_t kMillisPerSecond = 1000;
const int64_t kSecondsPerMinute = 60;

//struct tm 的年份从 1900 起算、月份从 0 起算
const int kTmYearBase = 1900;
const int kTmMonthBase = 1;

LocalMinute NowLocalMinute()
{
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    return LocalMinuteFromParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

LocalMinute LocalMinuteFromUnixMs(int64_t unixMs)
{
    if (unixMs <= 0)
        return 0;
    const time_t seconds = static_cast<time_t>(unixMs / kMillisPerSecond);
    struct tm local;
    if (localtime_s(&local, &seconds) != 0)
        return 0;
    return LocalMinuteFromParts(local.tm_year + kTmYearBase, local.tm_mon + kTmMonthBase, local.tm_mday,
                                local.tm_hour, local.tm_min);
}

int64_t MillisUntilLocalMinute(LocalMinute target)
{
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    const LocalMinute now = LocalMinuteFromParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    const int64_t elapsedInMinute = st.wSecond * kMillisPerSecond + st.wMilliseconds;
    const int64_t millis = (target - now) * kSecondsPerMinute * kMillisPerSecond - elapsedInMinute;
    return millis > 0 ? millis : 0;
}

} // namespace memo
