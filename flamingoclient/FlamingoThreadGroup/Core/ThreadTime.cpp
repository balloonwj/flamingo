/**
 *  时间显示分档的实现，见 ThreadTime.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadTime.h"

#include <windows.h>

namespace threadgroup {

namespace {

const int64_t kMsPerSecond = 1000;                     //一秒的毫秒数
const int64_t kMsPerMinute = 60 * kMsPerSecond;        //一分钟的毫秒数
const int64_t kMsPerHour = 60 * kMsPerMinute;          //一小时的毫秒数
const int64_t kMsPerDay = 24 * kMsPerHour;             //一天的毫秒数
const int64_t kFileTimeUnitsPerMs = 10000;             //FILETIME 以 100 纳秒为单位
const int64_t kUnixEpochInFileTime = 116444736000000000LL; //1970-01-01 在 FILETIME 里的值
const uint64_t kLowDwordMask = 0xFFFFFFFFULL;           //64 位数的低 32 位
const int kDwordBits = 32;                             //一个 DWORD 的位数

//Unix 毫秒 → 年月日时分（按 UTC 计算，调用方已加上本地偏移）
SYSTEMTIME ToSystemTime(int64_t ms)
{
    const int64_t ft64 = ms * kFileTimeUnitsPerMs + kUnixEpochInFileTime;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(static_cast<uint64_t>(ft64) & kLowDwordMask);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<uint64_t>(ft64) >> kDwordBits);
    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    ::FileTimeToSystemTime(&ft, &st);
    return st;
}

//本地日期序号（自 1970-01-01 起的天数），用来判断今天、昨天
int64_t DayIndex(int64_t localMs)
{
    if (localMs >= 0)
        return localMs / kMsPerDay;
    return (localMs - kMsPerDay + 1) / kMsPerDay;
}

} // namespace

TimeParts ClassifyTime(int64_t timeMs, int64_t nowMs, int64_t localOffsetMs)
{
    TimeParts t;
    const int64_t localTime = timeMs + localOffsetMs;
    const int64_t localNow = nowMs + localOffsetMs;
    const SYSTEMTIME st = ToSystemTime(localTime);
    const SYSTEMTIME now = ToSystemTime(localNow);
    t.m_year = st.wYear;
    t.m_month = st.wMonth;
    t.m_day = st.wDay;
    t.m_hour = st.wHour;
    t.m_minute = st.wMinute;
    const int64_t diff = nowMs - timeMs;
    if (diff < kMsPerMinute)
    {
        t.m_kind = time_just_now;
        return t;
    }
    if (diff < kMsPerHour)
    {
        t.m_kind = time_minutes_ago;
        t.m_minutes = static_cast<int>(diff / kMsPerMinute);
        return t;
    }
    const int64_t days = DayIndex(localNow) - DayIndex(localTime);
    if (days == 0)
        t.m_kind = time_today;
    else if (days == 1)
        t.m_kind = time_yesterday;
    else if (st.wYear == now.wYear)
        t.m_kind = time_this_year;
    else
        t.m_kind = time_older;
    return t;
}

int64_t NowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    const int64_t ft64 = (static_cast<int64_t>(ft.dwHighDateTime) << kDwordBits) | ft.dwLowDateTime;
    return (ft64 - kUnixEpochInFileTime) / kFileTimeUnitsPerMs;
}

int64_t LocalOffsetMs()
{
    TIME_ZONE_INFORMATION tz;
    const DWORD mode = ::GetTimeZoneInformation(&tz);
    LONG bias = tz.Bias;
    if (mode == TIME_ZONE_ID_DAYLIGHT)
        bias += tz.DaylightBias;
    else if (mode == TIME_ZONE_ID_STANDARD)
        bias += tz.StandardBias;
    //Bias 的含义是 UTC = 本地 + Bias（分钟），本地相对 UTC 的偏移取相反数
    return -static_cast<int64_t>(bias) * kMsPerMinute;
}

} // namespace threadgroup
