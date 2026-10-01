/**
 *  HistoryDateRange.cpp
 *
 *  HistoryDateRange.h 的实现：消息记录窗口「选日期」弹层的日期换算、检查、标签文字与预置日期。
 *  本文件不 include stdafx.h，与 FlamingoUnit 测试工程逐字共用同一份源码。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include <time.h>
#include <stdio.h>

#include "HistoryDateRange.h"
#include "HistoryModel.h"   // LocalDayBoundsMs：某一天的起止 Unix 毫秒，并判断这一天是否存在

namespace balloonhistory {

namespace {

const int64_t kMsPerSecond = 1000;   //毫秒换算成秒
const int kTmYearBase = 1900;        //struct tm 的 tm_year 从 1900 年起算
const size_t kDateTextChars = 32;    //一个日期文字的缓冲区长度（字符），「2026年12月31日」远小于它

//把一个日期写成文字：withYear 为 true 时为「2026年9月1日」，否则为「9月1日」。
std::wstring DateText(const HistoryDate& d, bool withYear)
{
    wchar_t buf[kDateTextChars] = { 0 };
    if (withYear)
    {
        swprintf(buf, kDateTextChars, L"%d年%d月%d日", d.m_year, d.m_month, d.m_day);
    }
    else
    {
        swprintf(buf, kDateTextChars, L"%d月%d日", d.m_month, d.m_day);
    }
    return std::wstring(buf);
}

bool SameDay(const HistoryDate& a, const HistoryDate& b)
{
    return a.m_year == b.m_year && a.m_month == b.m_month && a.m_day == b.m_day;
}

} // anonymous namespace

bool HistoryDateOf(int64_t timestampMs, HistoryDate& out)
{
    __time64_t t = (__time64_t)(timestampMs / kMsPerSecond);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return false;
    }
    out.m_year = tmv.tm_year + kTmYearBase;
    out.m_month = tmv.tm_mon + 1;
    out.m_day = tmv.tm_mday;
    return true;
}

HistoryDateRangeCheck CheckHistoryDateRange(const HistoryDate& from, const HistoryDate& to,
                                            int64_t& outStartMs, int64_t& outEndMs)
{
    int64_t fromStart = 0;
    int64_t fromEnd = 0;
    if (!LocalDayBoundsMs(from.m_year, from.m_month, from.m_day, fromStart, fromEnd))
    {
        return kHistoryDateRangeStartInvalid;
    }
    int64_t toStart = 0;
    int64_t toEnd = 0;
    if (!LocalDayBoundsMs(to.m_year, to.m_month, to.m_day, toStart, toEnd))
    {
        return kHistoryDateRangeEndInvalid;
    }
    if (fromStart > toStart)
    {
        return kHistoryDateRangeStartAfterEnd;
    }
    outStartMs = fromStart;
    outEndMs = toEnd;
    return kHistoryDateRangeOk;
}

std::wstring FormatHistoryDateRangeLabel(const HistoryDate& from, const HistoryDate& to,
                                         int currentYear)
{
    //两端都在今年才省略年份；否则两端都带年份，免得「2025年12月30日 至 1月2日」这样读不通
    const bool withYear = (from.m_year != currentYear || to.m_year != currentYear);
    if (SameDay(from, to))
    {
        return DateText(from, withYear);
    }
    return DateText(from, withYear) + L" 至 " + DateText(to, withYear);
}

void ComputeHistoryDatePopupDefaults(bool hasCustomRange, int64_t customMinMs, int64_t customMaxMs,
                                     bool hasFirstRecord, int64_t firstRecordMs, int64_t nowMs,
                                     HistoryDate& outFrom, HistoryDate& outTo)
{
    if (hasCustomRange && customMaxMs >= customMinMs
        && HistoryDateOf(customMinMs, outFrom) && HistoryDateOf(customMaxMs, outTo))
    {
        return;
    }
    const int64_t baseMs = hasFirstRecord ? firstRecordMs : nowMs;
    if (!HistoryDateOf(baseMs, outFrom))
    {
        HistoryDateOf(nowMs, outFrom);
    }
    outTo = outFrom;
}

} // namespace balloonhistory
