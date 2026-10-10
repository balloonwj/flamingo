/**
 *  聊天记录管理器时间轴模式的纯逻辑实现，说明见 HistoryTimeline.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "HistoryTimeline.h"

#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <map>

namespace balloonhistory {

namespace {

//struct tm 的 tm_year 从 1900 年起算
const int kTmYearBase = 1900;
//秒换算成毫秒
const int64_t kMsPerSecond = 1000;
//一年的月数
const int kMonthsPerYear = 12;
//年月键 = 年*100+月；日期键 = 年*10000+月*100+日
const int64_t kMonthKeyYearFactor = 100;
const int64_t kDayKeyYearFactor = 10000;
const int64_t kDayKeyMonthFactor = 100;
//百分比满值
const int kPercentFull = 100;
//一周的天数
const int kDaysPerWeek = 7;
//公历每月天数（平年），二月在闰年另加一天
const int kDaysInMonth[kMonthsPerYear] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
//Sakamoto 算法的月份偏移表：一月、二月按上一年计
const int kWeekdayMonthOffset[kMonthsPerYear] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
//公历闰年规则用到的年数
const int kLeapEvery = 4;
const int kLeapSkipEvery = 100;
const int kLeapRestoreEvery = 400;
//二月的月份编号
const int kFebruary = 2;
//日期文字「YYYY-MM-DD」的缓冲区长度
const size_t kDayTextBufLen = 16;

//是否公历闰年
bool IsLeapYear(int year)
{
    return (year % kLeapEvery == 0 && year % kLeapSkipEvery != 0) || year % kLeapRestoreEvery == 0;
}

//年月键拆成年、月；月份不在 1～12 时返回 false
bool SplitMonthKey(int64_t key, int& year, int& month)
{
    year = (int)(key / kMonthKeyYearFactor);
    month = (int)(key % kMonthKeyYearFactor);
    return year > 0 && month >= 1 && month <= kMonthsPerYear;
}

//按年倒序排
bool YearDescending(const HistoryTimelineYear& a, const HistoryTimelineYear& b)
{
    return a.m_year > b.m_year;
}

//按月倒序排
bool MonthDescending(const HistoryTimelineMonth& a, const HistoryTimelineMonth& b)
{
    return a.m_month > b.m_month;
}

//列表排序里 a 是否排在 b 前面（时间更晚，或同毫秒 msgid 更大）
bool PositionBefore(const HistoryPosition& a, const HistoryPosition& b)
{
    if (a.m_timestamp != b.m_timestamp)
    {
        return a.m_timestamp > b.m_timestamp;
    }
    return a.m_msgid > b.m_msgid;
}

} // namespace

std::vector<HistoryTimelineYear> BuildTimelineYears(const std::vector<HistoryPeriodCount>& monthCounts,
                                                    const std::vector<HistoryPeriodCount>& monthHits)
{
    //先按年月键合并：键 → (总数, 命中数)
    std::map<int64_t, HistoryTimelineMonth> months;
    for (size_t i = 0; i < monthCounts.size(); ++i)
    {
        int year = 0;
        int month = 0;
        if (!SplitMonthKey(monthCounts[i].m_key, year, month))
        {
            continue;
        }
        HistoryTimelineMonth& m = months[monthCounts[i].m_key];
        m.m_year = year;
        m.m_month = month;
        m.m_count += monthCounts[i].m_count;
    }
    for (size_t i = 0; i < monthHits.size(); ++i)
    {
        int year = 0;
        int month = 0;
        if (!SplitMonthKey(monthHits[i].m_key, year, month))
        {
            continue;
        }
        HistoryTimelineMonth& m = months[monthHits[i].m_key];
        m.m_year = year;
        m.m_month = month;
        m.m_hits += monthHits[i].m_count;
        //命中是总数的子集；总数查询失败等原因少了这个月时，至少按命中数显示
        if (m.m_count < m.m_hits)
        {
            m.m_count = m.m_hits;
        }
    }

    //再按年分组
    std::map<int, HistoryTimelineYear> years;
    for (std::map<int64_t, HistoryTimelineMonth>::const_iterator it = months.begin(); it != months.end(); ++it)
    {
        HistoryTimelineYear& y = years[it->second.m_year];
        y.m_year = it->second.m_year;
        y.m_count += it->second.m_count;
        y.m_hits += it->second.m_hits;
        y.m_months.push_back(it->second);
    }
    std::vector<HistoryTimelineYear> out;
    for (std::map<int, HistoryTimelineYear>::iterator it = years.begin(); it != years.end(); ++it)
    {
        std::sort(it->second.m_months.begin(), it->second.m_months.end(), MonthDescending);
        out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(), YearDescending);
    return out;
}

int64_t TimelineMaxMonthCount(const std::vector<HistoryTimelineYear>& years)
{
    int64_t maxCount = 0;
    for (size_t y = 0; y < years.size(); ++y)
    {
        for (size_t m = 0; m < years[y].m_months.size(); ++m)
        {
            if (years[y].m_months[m].m_count > maxCount)
            {
                maxCount = years[y].m_months[m].m_count;
            }
        }
    }
    return maxCount;
}

int TimelineBarPercent(int64_t count, int64_t maxCount)
{
    if (count <= 0 || maxCount <= 0)
    {
        return 0;
    }
    int64_t percent = count * kPercentFull / maxCount;
    if (percent < kTimelineBarMinPercent)
    {
        percent = kTimelineBarMinPercent;
    }
    if (percent > kPercentFull)
    {
        percent = kPercentFull;
    }
    return (int)percent;
}

int64_t LocalMonthKeyOf(int64_t timestampMs)
{
    if (timestampMs <= 0)
    {
        return 0;
    }
    const __time64_t t = (__time64_t)(timestampMs / kMsPerSecond);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return 0;
    }
    return (int64_t)(tmv.tm_year + kTmYearBase) * kMonthKeyYearFactor + (tmv.tm_mon + 1);
}

bool LocalMonthBoundsMs(int year, int month, int64_t& outStartMs, int64_t& outEndMs)
{
    outStartMs = 0;
    outEndMs = 0;
    if (month < 1 || month > kMonthsPerYear || year <= kTmYearBase)
    {
        return false;
    }
    struct tm startTm = {};
    startTm.tm_year = year - kTmYearBase;
    startTm.tm_mon = month - 1;
    startTm.tm_mday = 1;
    startTm.tm_isdst = -1;   //是否夏令时交给系统按当地规则判断
    const __time64_t start = _mktime64(&startTm);
    //下月 1 日：月份加一交给 _mktime64 换算（十二月会进到下一年一月）
    struct tm nextTm = {};
    nextTm.tm_year = year - kTmYearBase;
    nextTm.tm_mon = month;
    nextTm.tm_mday = 1;
    nextTm.tm_isdst = -1;
    const __time64_t next = _mktime64(&nextTm);
    if (start == -1 || next == -1)
    {
        return false;
    }
    outStartMs = (int64_t)start * kMsPerSecond;
    outEndMs = (int64_t)next * kMsPerSecond - 1;
    return true;
}

std::vector<HistoryMonthRange> BuildLocalMonthRanges(int64_t oldestMs, int64_t newestMs)
{
    std::vector<HistoryMonthRange> ranges;
    const int64_t first = LocalMonthKeyOf(oldestMs);
    const int64_t last = LocalMonthKeyOf(newestMs);
    if (first == 0 || last == 0 || first > last)
    {
        return ranges;
    }
    //从最晚的那个月往前数，到最早的那个月为止
    int year = (int)(last / kMonthKeyYearFactor);
    int month = (int)(last % kMonthKeyYearFactor);
    for (;;)
    {
        const int64_t key = (int64_t)year * kMonthKeyYearFactor + month;
        if (key < first)
        {
            break;
        }
        int64_t startMs = 0;
        int64_t endMs = 0;
        if (LocalMonthBoundsMs(year, month, startMs, endMs))
        {
            ranges.push_back(HistoryMonthRange(key, startMs, endMs));
        }
        --month;
        if (month < 1)
        {
            month = kMonthsPerYear;
            --year;
        }
    }
    return ranges;
}

int WeekdayOfDayKey(int64_t dayKey)
{
    int year = (int)(dayKey / kDayKeyYearFactor);
    const int month = (int)((dayKey / kDayKeyMonthFactor) % kDayKeyMonthFactor);
    const int day = (int)(dayKey % kDayKeyMonthFactor);
    if (year <= 0 || month < 1 || month > kMonthsPerYear || day < 1)
    {
        return -1;
    }
    int daysInMonth = kDaysInMonth[month - 1];
    if (month == kFebruary && IsLeapYear(year))
    {
        ++daysInMonth;
    }
    if (day > daysInMonth)
    {
        return -1;
    }
    //Sakamoto 算法：一月、二月按上一年的第 13、14 个月计
    if (month < 3)
    {
        --year;
    }
    const int w = (year + year / kLeapEvery - year / kLeapSkipEvery + year / kLeapRestoreEvery
                   + kWeekdayMonthOffset[month - 1] + day) % kDaysPerWeek;
    return w;
}

std::string FormatDayKey(int64_t dayKey)
{
    if (dayKey <= 0)
    {
        return std::string();
    }
    char buf[kDayTextBufLen] = { 0 };
    sprintf_s(buf, "%04d-%02d-%02d", (int)(dayKey / kDayKeyYearFactor),
              (int)((dayKey / kDayKeyMonthFactor) % kDayKeyMonthFactor), (int)(dayKey % kDayKeyMonthFactor));
    return std::string(buf);
}

int64_t PeriodCountOf(const std::vector<HistoryPeriodCount>& counts, int64_t key)
{
    for (size_t i = 0; i < counts.size(); ++i)
    {
        if (counts[i].m_key == key)
        {
            return counts[i].m_count;
        }
    }
    return 0;
}

int DefaultExpandedYear(const std::vector<HistoryTimelineYear>& years, int64_t currentMonthKey)
{
    if (years.empty())
    {
        return 0;
    }
    const int currentYear = (int)(currentMonthKey / kMonthKeyYearFactor);
    for (size_t i = 0; i < years.size(); ++i)
    {
        if (years[i].m_year == currentYear)
        {
            return currentYear;
        }
    }
    return years.front().m_year;
}

// ---- HistoryHitNavigator ----

HistoryHitNavigator::HistoryHitNavigator()
    : m_truncated(false)
    , m_current(-1)
{
}

void HistoryHitNavigator::Reset(const std::vector<HistoryPosition>& hits, bool truncated)
{
    m_hits = hits;
    m_msgids.clear();
    for (size_t i = 0; i < m_hits.size(); ++i)
    {
        m_msgids.insert(m_hits[i].m_msgid);
    }
    m_truncated = truncated;
    m_current = -1;
}

void HistoryHitNavigator::Clear()
{
    m_hits.clear();
    m_msgids.clear();
    m_truncated = false;
    m_current = -1;
}

int HistoryHitNavigator::Count() const
{
    return (int)m_hits.size();
}

bool HistoryHitNavigator::Truncated() const
{
    return m_truncated;
}

int HistoryHitNavigator::Current() const
{
    return m_current;
}

const HistoryPosition* HistoryHitNavigator::At(int index) const
{
    if (index < 0 || index >= (int)m_hits.size())
    {
        return NULL;
    }
    return &m_hits[(size_t)index];
}

int HistoryHitNavigator::Next(bool& wrapped)
{
    wrapped = false;
    if (m_hits.empty())
    {
        m_current = -1;
        return -1;
    }
    if (m_current < 0)
    {
        m_current = 0;
    }
    else if (m_current + 1 >= (int)m_hits.size())
    {
        m_current = 0;
        wrapped = true;
    }
    else
    {
        ++m_current;
    }
    return m_current;
}

int HistoryHitNavigator::Prev(bool& wrapped)
{
    wrapped = false;
    if (m_hits.empty())
    {
        m_current = -1;
        return -1;
    }
    const int last = (int)m_hits.size() - 1;
    if (m_current < 0)
    {
        m_current = last;
    }
    else if (m_current == 0)
    {
        m_current = last;
        wrapped = true;
    }
    else
    {
        --m_current;
    }
    return m_current;
}

bool HistoryHitNavigator::SetCurrent(int index)
{
    if (index < 0 || index >= (int)m_hits.size())
    {
        return false;
    }
    m_current = index;
    return true;
}

int HistoryHitNavigator::IndexOfMsgid(int64_t msgid) const
{
    if (m_msgids.find(msgid) == m_msgids.end())
    {
        return -1;
    }
    for (size_t i = 0; i < m_hits.size(); ++i)
    {
        if (m_hits[i].m_msgid == msgid)
        {
            return (int)i;
        }
    }
    return -1;
}

bool HistoryHitNavigator::Contains(int64_t msgid) const
{
    return m_msgids.find(msgid) != m_msgids.end();
}

int HistoryHitNavigator::FirstAtOrAfter(int64_t timestamp, int64_t msgid) const
{
    const HistoryPosition start(timestamp, msgid);
    for (size_t i = 0; i < m_hits.size(); ++i)
    {
        //m_hits 已按列表排序；第一处不排在起点前面的就是答案
        if (!PositionBefore(m_hits[i], start))
        {
            return (int)i;
        }
    }
    return -1;
}

} // namespace balloonhistory
