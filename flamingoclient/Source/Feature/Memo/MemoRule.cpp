/**
 *  MemoRule.cpp
 *
 *  提醒时间计算的实现。公历换算用的是「以 3 月 1 日为一年之首、400 年为一个周期」的整数算法，
 *  全程整数运算，没有浮点与时区问题。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 AutoReplyPolicy.cpp 同例。
#include "MemoRule.h"

namespace memo {

//公历 400 年一个周期里的天数
const int64_t kDaysPer400Years = 146097;

//以 3 月 1 日为年首时，0000-03-01 到 1970-01-01 相隔的天数
const int64_t kDaysFromEpoch0000To1970 = 719468;

//一个 400 年周期里的年数
const int64_t kYearsPerEra = 400;

//平年的天数
const int64_t kDaysPerCommonYear = 365;

//闰年规则里的三个周期：每 4 年一闰、每 100 年不闰、每 400 年再闰
const int64_t kLeapCycle4 = 4;
const int64_t kLeapCycle100 = 100;
const int64_t kLeapCycle400 = 400;

//在一个 400 年周期内反推年份时用到的三个修正量：周期内第 1460 天（4 年）、第 36524 天（100 年）、
//第 146096 天（400 年的最后一天）处各有一次闰日边界
const int64_t kDaysPer4Years = 1460;
const int64_t kDaysPer100Years = 36524;
const int64_t kLastDayOf400Years = 146096;

//以 3 月为首月时，月份换算到年内天数的线性系数：153 天对应 5 个月（3～7 月或 8～12 月）
const int64_t kDaysPer5Months = 153;
const int64_t kMonthsPer5 = 5;

//以 3 月为首月时：3 月对应序号 0，所以 3～12 月减 3，1～2 月加 9
const int kMarchBasedOffsetLate = 3;
const int kMarchBasedOffsetEarly = 9;

//以 3 月为首月的序号里，序号 10、11 是次年的 1、2 月
const int kMarchBasedJanuaryIndex = 10;

//一年的月数、一天的小时数、一小时的分钟数
const int kMonthsPerYear = 12;
const int kHoursPerDay = 24;
const int kMinutesPerHour = 60;

//二月的月份号与它在闰年、平年的天数
const int kFebruary = 2;
const int kFebDaysLeap = 29;
const int kFebDaysCommon = 28;

//1970-01-01 是星期四，按「0 周一 ... 6 周日」的序号是 3
const int kWeekdayOfEpoch = 3;

//十进制 yyyymmddhhmm 里各段的位权
const int64_t kStartYearUnit = 100000000LL;
const int64_t kStartMonthUnit = 1000000LL;
const int64_t kStartDayUnit = 10000LL;
const int64_t kStartHourUnit = 100LL;
const int64_t kStartPartBase = 100LL;

//本模块接受的年份范围：早于 1970 年时分钟序号为负，晚于 9999 年时 yyyymmddhhmm 多出一位
const int kMinYear = 1970;
const int kMaxYear = 9999;

//向下取整的整数除法（C++ 的 / 对负数向零取整）
static int64_t FloorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

static bool IsLeapYear(int year)
{
    return (year % kLeapCycle4 == 0 && year % kLeapCycle100 != 0) || year % kLeapCycle400 == 0;
}

int64_t DaysFromCivil(int year, int month, int day)
{
    const int64_t y = static_cast<int64_t>(year) - (month <= kFebruary ? 1 : 0);
    const int64_t era = FloorDiv(y, kYearsPerEra);
    const int64_t yoe = y - era * kYearsPerEra;
    const int64_t mp = (month > kFebruary) ? (month - kMarchBasedOffsetLate) : (month + kMarchBasedOffsetEarly);
    const int64_t doy = (kDaysPer5Months * mp + 2) / kMonthsPer5 + day - 1;
    const int64_t doe = yoe * kDaysPerCommonYear + yoe / kLeapCycle4 - yoe / kLeapCycle100 + doy;
    return era * kDaysPer400Years + doe - kDaysFromEpoch0000To1970;
}

void CivilFromDays(int64_t days, int& year, int& month, int& day)
{
    const int64_t z = days + kDaysFromEpoch0000To1970;
    const int64_t era = FloorDiv(z, kDaysPer400Years);
    const int64_t doe = z - era * kDaysPer400Years;
    const int64_t yoe = (doe - doe / kDaysPer4Years + doe / kDaysPer100Years - doe / kLastDayOf400Years)
                        / kDaysPerCommonYear;
    const int64_t doy = doe - (kDaysPerCommonYear * yoe + yoe / kLeapCycle4 - yoe / kLeapCycle100);
    const int64_t mp = (kMonthsPer5 * doy + 2) / kDaysPer5Months;
    day = static_cast<int>(doy - (kDaysPer5Months * mp + 2) / kMonthsPer5 + 1);
    month = static_cast<int>(mp < kMarchBasedJanuaryIndex ? mp + kMarchBasedOffsetLate : mp - kMarchBasedOffsetEarly);
    year = static_cast<int>(yoe + era * kYearsPerEra + (month <= kFebruary ? 1 : 0));
}

int DaysInMonth(int year, int month)
{
    //1～12 月的天数，二月按平年，闰年另行处理
    static const int kDays[kMonthsPerYear] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > kMonthsPerYear)
        return 0;
    if (month == kFebruary)
        return IsLeapYear(year) ? kFebDaysLeap : kFebDaysCommon;
    return kDays[month - 1];
}

int WeekdayOfDays(int64_t days)
{
    int64_t w = (days + kWeekdayOfEpoch) % kDaysPerWeek;
    if (w < 0)
        w += kDaysPerWeek;
    return static_cast<int>(w);
}

LocalMinute LocalMinuteFromParts(int year, int month, int day, int hour, int minute)
{
    return DaysFromCivil(year, month, day) * kMinutesPerDay + hour * kMinutesPerHour + minute;
}

bool LocalMinuteFromStart(int64_t start, LocalMinute& out)
{
    out = 0;
    if (start <= 0)
        return false;
    const int64_t year64 = start / kStartYearUnit;
    if (year64 < kMinYear || year64 > kMaxYear)
        return false;
    const int year = static_cast<int>(year64);
    const int month = static_cast<int>((start / kStartMonthUnit) % kStartPartBase);
    const int day = static_cast<int>((start / kStartDayUnit) % kStartPartBase);
    const int hour = static_cast<int>((start / kStartHourUnit) % kStartPartBase);
    const int minute = static_cast<int>(start % kStartPartBase);
    if (month < 1 || month > kMonthsPerYear)
        return false;
    if (day < 1 || day > DaysInMonth(year, month))
        return false;
    if (hour < 0 || hour >= kHoursPerDay || minute < 0 || minute >= kMinutesPerHour)
        return false;
    out = LocalMinuteFromParts(year, month, day, hour, minute);
    return true;
}

void PartsFromLocalMinute(LocalMinute minute, int& year, int& month, int& day, int& hour, int& minuteOfHour)
{
    const int64_t days = FloorDiv(minute, kMinutesPerDay);
    const int64_t tod = minute - days * kMinutesPerDay;
    CivilFromDays(days, year, month, day);
    hour = static_cast<int>(tod / kMinutesPerHour);
    minuteOfHour = static_cast<int>(tod % kMinutesPerHour);
}

int64_t StartFromLocalMinute(LocalMinute minute)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minuteOfHour = 0;
    PartsFromLocalMinute(minute, year, month, day, hour, minuteOfHour);
    return year * kStartYearUnit + month * kStartMonthUnit + day * kStartDayUnit
           + hour * kStartHourUnit + minuteOfHour;
}

bool IsValidRule(const MemoRuleSpec& rule)
{
    LocalMinute start = 0;
    if (!LocalMinuteFromStart(rule.m_start, start))
        return false;
    if (rule.m_repeat < kMemoRepeatNone || rule.m_repeat > kMemoRepeatYearly)
        return false;
    if (rule.m_repeat == kMemoRepeatWeekly && (rule.m_weekdays & kMemoWeekdayAllMask) == 0)
        return false;
    if (rule.m_count < 0 || rule.m_advance < 0)
        return false;
    return true;
}

// ---------------- 按星期掩码重复（每天、工作日、每周）----------------

//每天、工作日、每周三种重复都归结为「从开始那天起，掩码里选中的那几天」：每天是七天全选
static int MaskOfRule(const MemoRuleSpec& rule)
{
    switch (rule.m_repeat)
    {
    //每天：一周七天全选
    case kMemoRepeatDaily:
        return kMemoWeekdayAllMask;
    //工作日：周一到周五
    case kMemoRepeatWorkday:
        return kMemoWorkdayMask;
    //每周：用户选中的那几天
    case kMemoRepeatWeekly:
        return rule.m_weekdays & kMemoWeekdayAllMask;
    //其余重复方式不按星期掩码计算
    default:
        return 0;
    }
}

static bool DayInMask(int64_t days, int mask)
{
    return ((mask >> WeekdayOfDays(days)) & 1) != 0;
}

static int64_t PopCountMask(int mask)
{
    int64_t n = 0;
    for (int i = 0; i < kDaysPerWeek; ++i)
    {
        if ((mask >> i) & 1)
            ++n;
    }
    return n;
}

//[firstDay, endDay) 内被掩码选中的天数
static int64_t CountMaskDays(int64_t firstDay, int64_t endDay, int mask)
{
    if (endDay <= firstDay)
        return 0;
    const int64_t weeks = (endDay - firstDay) / kDaysPerWeek;
    int64_t n = weeks * PopCountMask(mask);
    for (int64_t d = firstDay + weeks * kDaysPerWeek; d < endDay; ++d)
    {
        if (DayInMask(d, mask))
            ++n;
    }
    return n;
}

//从 firstDay 起第 index 个（从 0 起）被掩码选中的日子。每连续 7 天恰好有 PopCountMask 个选中的日子
static int64_t MaskDayAt(int64_t firstDay, int64_t index, int mask)
{
    const int64_t pop = PopCountMask(mask);
    int64_t remain = index % pop;
    int64_t d = firstDay + (index / pop) * kDaysPerWeek;
    while (true)
    {
        if (DayInMask(d, mask))
        {
            if (remain == 0)
                return d;
            --remain;
        }
        ++d;
    }
}

// ---------------- 按月、按年重复 ----------------

//每月重复的第 index 次事件：开始那天的「几号」，当月没有这一天时取当月最后一天
static LocalMinute MonthlyEventAt(int y0, int m0, int d0, int64_t tod, int64_t index)
{
    const int64_t total = (m0 - 1) + index;
    const int year = static_cast<int>(y0 + total / kMonthsPerYear);
    const int month = static_cast<int>(total % kMonthsPerYear) + 1;
    const int dim = DaysInMonth(year, month);
    const int day = (d0 < dim) ? d0 : dim;
    return DaysFromCivil(year, month, day) * kMinutesPerDay + tod;
}

//每年重复的第 index 次事件：开始那天的月日，2 月 29 日在平年取 2 月 28 日
static LocalMinute YearlyEventAt(int y0, int m0, int d0, int64_t tod, int64_t index)
{
    const int year = static_cast<int>(y0 + index);
    const int dim = DaysInMonth(year, m0);
    const int day = (d0 < dim) ? d0 : dim;
    return DaysFromCivil(year, m0, day) * kMinutesPerDay + tod;
}

//from / to 所在的年月相对开始那个年月过了几个月（按月重复）或几年（按年重复）
static int64_t PeriodsBetween(const MemoRuleSpec& rule, int y0, int m0, LocalMinute t)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    PartsFromLocalMinute(t, year, month, day, hour, minute);
    if (rule.m_repeat == kMemoRepeatMonthly)
        return static_cast<int64_t>(year - y0) * kMonthsPerYear + (month - m0);
    return static_cast<int64_t>(year - y0);
}

static LocalMinute PeriodEventAt(const MemoRuleSpec& rule, int y0, int m0, int d0, int64_t tod, int64_t index)
{
    if (rule.m_repeat == kMemoRepeatMonthly)
        return MonthlyEventAt(y0, m0, d0, tod, index);
    return YearlyEventAt(y0, m0, d0, tod, index);
}

//次数上限：不重复恒为 1 次，其余按 m_count（0 为永不结束，返回 -1 表示没有上限）
static int64_t CountLimit(const MemoRuleSpec& rule)
{
    if (rule.m_repeat == kMemoRepeatNone)
        return 1;
    if (rule.m_count <= 0)
        return -1;
    return rule.m_count;
}

bool FindEventAtOrAfter(const MemoRuleSpec& rule, LocalMinute from, MemoOccurrence& out)
{
    if (!IsValidRule(rule))
        return false;

    LocalMinute start = 0;
    LocalMinuteFromStart(rule.m_start, start);
    const int64_t limit = CountLimit(rule);

    if (rule.m_repeat == kMemoRepeatNone)
    {
        if (start < from)
            return false;
        out.m_event = start;
        out.m_index = 0;
        return true;
    }

    const int64_t firstDay = FloorDiv(start, kMinutesPerDay);
    const int64_t tod = start - firstDay * kMinutesPerDay;

    const int mask = MaskOfRule(rule);
    if (mask != 0)
    {
        const int64_t fromDay = FloorDiv(from, kMinutesPerDay);
        const int64_t fromTod = from - fromDay * kMinutesPerDay;
        int64_t candidate = (fromTod <= tod) ? fromDay : fromDay + 1;
        if (candidate < firstDay)
            candidate = firstDay;
        const int64_t index = CountMaskDays(firstDay, candidate, mask);
        if (limit >= 0 && index >= limit)
            return false;
        out.m_event = MaskDayAt(firstDay, index, mask) * kMinutesPerDay + tod;
        out.m_index = index;
        return true;
    }

    int y0 = 0;
    int m0 = 0;
    int d0 = 0;
    CivilFromDays(firstDay, y0, m0, d0);
    int64_t index = PeriodsBetween(rule, y0, m0, from);
    if (index < 0)
        index = 0;
    if (PeriodEventAt(rule, y0, m0, d0, tod, index) < from)
        ++index;
    if (limit >= 0 && index >= limit)
        return false;
    out.m_event = PeriodEventAt(rule, y0, m0, d0, tod, index);
    out.m_index = index;
    return true;
}

bool FindEventAtOrBefore(const MemoRuleSpec& rule, LocalMinute to, MemoOccurrence& out)
{
    if (!IsValidRule(rule))
        return false;

    LocalMinute start = 0;
    LocalMinuteFromStart(rule.m_start, start);
    const int64_t limit = CountLimit(rule);

    if (rule.m_repeat == kMemoRepeatNone)
    {
        if (start > to)
            return false;
        out.m_event = start;
        out.m_index = 0;
        return true;
    }

    const int64_t firstDay = FloorDiv(start, kMinutesPerDay);
    const int64_t tod = start - firstDay * kMinutesPerDay;

    const int mask = MaskOfRule(rule);
    if (mask != 0)
    {
        const int64_t toDay = FloorDiv(to, kMinutesPerDay);
        const int64_t toTod = to - toDay * kMinutesPerDay;
        const int64_t candidate = (toTod >= tod) ? toDay : toDay - 1;
        if (candidate < firstDay)
            return false;
        const int64_t selected = CountMaskDays(firstDay, candidate + 1, mask);
        if (selected == 0)
            return false;
        int64_t index = selected - 1;
        if (limit >= 0 && index > limit - 1)
            index = limit - 1;
        out.m_event = MaskDayAt(firstDay, index, mask) * kMinutesPerDay + tod;
        out.m_index = index;
        return true;
    }

    int y0 = 0;
    int m0 = 0;
    int d0 = 0;
    CivilFromDays(firstDay, y0, m0, d0);
    int64_t index = PeriodsBetween(rule, y0, m0, to);
    if (index < 0)
        return false;
    if (PeriodEventAt(rule, y0, m0, d0, tod, index) > to)
        --index;
    if (index < 0)
        return false;
    if (limit >= 0 && index > limit - 1)
        index = limit - 1;
    out.m_event = PeriodEventAt(rule, y0, m0, d0, tod, index);
    out.m_index = index;
    return true;
}

void ListEventsInRange(const MemoRuleSpec& rule, LocalMinute from, LocalMinute to, size_t maxCount,
                       std::vector<MemoOccurrence>& out)
{
    out.clear();
    LocalMinute cursor = from;
    while (out.size() < maxCount && cursor < to)
    {
        MemoOccurrence occ;
        if (!FindEventAtOrAfter(rule, cursor, occ) || occ.m_event >= to)
            break;
        out.push_back(occ);
        cursor = occ.m_event + 1;
    }
}

} // namespace memo
