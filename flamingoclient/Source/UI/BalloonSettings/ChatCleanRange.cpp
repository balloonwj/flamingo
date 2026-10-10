/**
 *  「清理聊天记录」时间范围计算的实现，ChatCleanRange.cpp
 *  各函数的口径见 ChatCleanRange.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "ChatCleanRange.h"

#include <stdio.h>   // swprintf_s（时间格式化）
#include <time.h>    // _mktime64 / _localtime64_s（本机时区换算）
#include "../../Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找

namespace balloonsettings {

namespace {

//一秒的毫秒数
const int64_t kSecondMs = 1000;

//struct tm 的年份以 1900 为基准、月份从 0 起算
const int kTmYearBase = 1900;

//一年的月数、一天的小时数、一小时的分钟数：字段合法性判断用
const int kMonthsPerYear = 12;
const int kHoursPerDay = 24;
const int kMinutesPerHour = 60;

//大月与小月的天数、2 月平年与闰年的天数
const int kDaysLongMonth = 31;
const int kDaysShortMonth = 30;
const int kDaysFebCommon = 28;
const int kDaysFebLeap = 29;

//二月的月份值（1 起算）
const int kFebruary = 2;

//闰年规则里的三个除数：每 4 年一闰，逢百年不闰，逢四百年又闰
const int kLeapEvery = 4;
const int kLeapSkipCentury = 100;
const int kLeapKeepEvery = 400;

//「3 个月 / 半年 / 1 年」折算的天数，与消息记录窗「近 3 个月」同一口径
const int kDays3Months = 90;
const int kDaysHalfYear = 180;
const int kDays1Year = 365;

//「最近 N 天 / N 天以前」里的 N
const int kDays1 = 1;
const int kDays7 = 7;
const int kDays30 = 30;

//格式化 "yyyy-MM-dd HH:mm" 用的缓冲区长度（16 个字符加结尾，留足余量）
const int kFormatBufLen = 32;

//各档位的显示文字，下标即 ChatCleanPreset 的取值；只做标记（TR_NOOP），ChatCleanPresetLabel 返回译文
const wchar_t* const kPresetLabels[kCleanPresetCount] =
{
    TR_NOOP(L"全部"),
    TR_NOOP(L"最近 1 天"),
    TR_NOOP(L"最近 7 天"),
    TR_NOOP(L"最近 30 天"),
    TR_NOOP(L"7 天以前"),
    TR_NOOP(L"30 天以前"),
    TR_NOOP(L"3 个月以前"),
    TR_NOOP(L"半年以前"),
    TR_NOOP(L"1 年以前"),
    TR_NOOP(L"自定义"),
};

//说明文字与成功提示的整句模板，只做标记（TR_NOOP），生成时再取译文。
//「以前」一类的 {0} 为截止时间；「之间」一类的 {0}、{1} 为起止时间，均为 yyyy-MM-dd HH:mm
const wchar_t* const kSummaryBefore =
    TR_NOOP(L"将删除 {0} 以前的全部单聊和群聊记录，本机与服务器同时删除，对方不受影响，删除后无法恢复。");
const wchar_t* const kSummaryBetween =
    TR_NOOP(L"将删除 {0} 至 {1} 之间的全部单聊和群聊记录，本机与服务器同时删除，对方不受影响，删除后无法恢复。");
const wchar_t* const kDoneBefore = TR_NOOP(L"已清理 {0} 以前的聊天记录");
const wchar_t* const kDoneBetween = TR_NOOP(L"已清理 {0} 至 {1} 的聊天记录");

bool IsLeapYear(int year)
{
    return (year % kLeapEvery == 0 && year % kLeapSkipCentury != 0) || (year % kLeapKeepEvery == 0);
}

//各字段是否都在合法范围内（日不超过当月天数）。
bool IsValidMinute(const LocalMinute& t)
{
    const int days = DaysInMonth(t.m_year, t.m_month);
    if (days == 0)
        return false;
    if (t.m_day < 1 || t.m_day > days)
        return false;
    if (t.m_hour < 0 || t.m_hour >= kHoursPerDay)
        return false;
    if (t.m_minute < 0 || t.m_minute >= kMinutesPerHour)
        return false;
    return true;
}

} // anonymous namespace

bool LocalMinute::operator==(const LocalMinute& other) const
{
    return m_year == other.m_year
        && m_month == other.m_month
        && m_day == other.m_day
        && m_hour == other.m_hour
        && m_minute == other.m_minute;
}

bool LocalMinute::operator!=(const LocalMinute& other) const
{
    return !(*this == other);
}

const wchar_t* ChatCleanPresetLabel(int preset)
{
    if (preset < 0 || preset >= kCleanPresetCount)
        return L"";
    return TR(kPresetLabels[preset]);
}

int DaysInMonth(int year, int month)
{
    if (month < 1 || month > kMonthsPerYear)
        return 0;
    if (month == kFebruary)
        return IsLeapYear(year) ? kDaysFebLeap : kDaysFebCommon;

    //4、6、9、11 月是小月，其余是大月
    const int kShortMonths[] = { 4, 6, 9, 11 };
    for (size_t i = 0; i < sizeof(kShortMonths) / sizeof(kShortMonths[0]); ++i)
    {
        if (month == kShortMonths[i])
            return kDaysShortMonth;
    }
    return kDaysLongMonth;
}

void ClampDayToMonth(LocalMinute& t)
{
    const int days = DaysInMonth(t.m_year, t.m_month);
    if (days == 0)
        return;
    if (t.m_day > days)
        t.m_day = days;
}

bool LocalMinuteToMs(const LocalMinute& t, int64_t& outMs)
{
    if (!IsValidMinute(t))
        return false;

    struct tm tmLocal = { 0 };
    tmLocal.tm_year = t.m_year - kTmYearBase;
    tmLocal.tm_mon = t.m_month - 1;
    tmLocal.tm_mday = t.m_day;
    tmLocal.tm_hour = t.m_hour;
    tmLocal.tm_min = t.m_minute;
    tmLocal.tm_sec = 0;
    //-1：由 C 运行库按本机时区规则自行判断该时刻是否处于夏令时
    tmLocal.tm_isdst = -1;

    const __time64_t secs = _mktime64(&tmLocal);
    if (secs < 0)
        return false;

    //_mktime64 会把不存在的本地时刻（夏令时切换时跳过的那一小时）归一化成别的时刻，
    //换算回来对不上时按无法换算处理，免得区间悄悄偏移一小时
    LocalMinute back;
    if (!MsToLocalMinute((int64_t)secs * kSecondMs, back) || back != t)
        return false;

    outMs = (int64_t)secs * kSecondMs;
    return true;
}

bool MsToLocalMinute(int64_t ms, LocalMinute& out)
{
    if (ms < 0)
        return false;

    const __time64_t secs = (__time64_t)(ms / kSecondMs);
    struct tm tmLocal = { 0 };
    if (_localtime64_s(&tmLocal, &secs) != 0)
        return false;

    out.m_year = tmLocal.tm_year + kTmYearBase;
    out.m_month = tmLocal.tm_mon + 1;
    out.m_day = tmLocal.tm_mday;
    out.m_hour = tmLocal.tm_hour;
    out.m_minute = tmLocal.tm_min;
    return true;
}

LocalMinute EarliestSelectableMinute()
{
    return LocalMinute(kCleanYearStart, 1, 1, 0, 0);
}

bool PresetToRange(int preset, int64_t nowMs, int64_t& beginMs, int64_t& endMs)
{
    switch (preset)
    {
    //全部：从最早到当前时刻
    case kCleanAll:
        beginMs = 0;
        endMs = nowMs;
        return true;

    //最近 1 天
    case kCleanLast1Day:
        beginMs = nowMs - kDays1 * kCleanDayMs;
        endMs = nowMs;
        return true;

    //最近 7 天
    case kCleanLast7Days:
        beginMs = nowMs - kDays7 * kCleanDayMs;
        endMs = nowMs;
        return true;

    //最近 30 天
    case kCleanLast30Days:
        beginMs = nowMs - kDays30 * kCleanDayMs;
        endMs = nowMs;
        return true;

    //7 天以前
    case kCleanBefore7Days:
        beginMs = 0;
        endMs = nowMs - kDays7 * kCleanDayMs;
        return true;

    //30 天以前
    case kCleanBefore30Days:
        beginMs = 0;
        endMs = nowMs - kDays30 * kCleanDayMs;
        return true;

    //3 个月以前（按 90 天折算）
    case kCleanBefore3Months:
        beginMs = 0;
        endMs = nowMs - kDays3Months * kCleanDayMs;
        return true;

    //半年以前（按 180 天折算）
    case kCleanBeforeHalfYear:
        beginMs = 0;
        endMs = nowMs - kDaysHalfYear * kCleanDayMs;
        return true;

    //1 年以前（按 365 天折算）
    case kCleanBefore1Year:
        beginMs = 0;
        endMs = nowMs - kDays1Year * kCleanDayMs;
        return true;

    //自定义由两组下拉框决定（见 CustomToRange），越界档位同样不产出区间
    default:
        return false;
    }
}

bool RangeToDisplay(int64_t beginMs, int64_t endMs, LocalMinute& startShown, LocalMinute& endShown)
{
    const LocalMinute earliest = EarliestSelectableMinute();
    LocalMinute start = earliest;
    if (beginMs > 0)
    {
        if (!MsToLocalMinute(beginMs, start))
            return false;

        //开始早于下拉框能选到的最早时刻时，显示为最早时刻（下拉框里没有更早的选项）
        int64_t earliestMs = 0;
        if (LocalMinuteToMs(earliest, earliestMs) && beginMs < earliestMs)
            start = earliest;
    }

    LocalMinute end;
    if (!MsToLocalMinute(endMs, end))
        return false;

    startShown = start;
    endShown = end;
    return true;
}

int CustomToRange(const LocalMinute& start, const LocalMinute& end, int64_t nowMs,
                  int64_t& beginMs, int64_t& endMs)
{
    if (start.m_year < kCleanYearStart || end.m_year < kCleanYearStart)
        return kCleanRangeBadTime;

    int64_t startMinuteMs = 0;
    int64_t endMinuteMs = 0;
    if (!LocalMinuteToMs(start, startMinuteMs) || !LocalMinuteToMs(end, endMinuteMs))
        return kCleanRangeBadTime;

    //按分钟比较：开始与结束在同一分钟合法，区间就是这一整分钟
    if (startMinuteMs > endMinuteMs)
        return kCleanRangeStartAfterEnd;

    //结束那一分钟还没开始，说明选到了将来
    if (endMinuteMs > nowMs)
        return kCleanRangeEndInFuture;

    beginMs = (start == EarliestSelectableMinute()) ? 0 : startMinuteMs;

    //结束包含所选的整分钟；这一分钟就是当前这一分钟时截到当前时刻
    const int64_t endOfMinute = endMinuteMs + kCleanMinuteMs - 1;
    endMs = (endOfMinute > nowMs) ? nowMs : endOfMinute;
    return kCleanRangeOk;
}

const wchar_t* ChatCleanRangeErrorText(int err)
{
    switch (err)
    {
    //开始晚于结束
    case kCleanRangeStartAfterEnd:
        return TR(L"开始时间不能晚于结束时间");

    //结束选到了将来
    case kCleanRangeEndInFuture:
        return TR(L"结束时间不能晚于当前时间");

    //字段越界或早于可选的最早年份：正常操作下拉框不会出现，出现即说明界面填值有误
    case kCleanRangeBadTime:
        return TR(L"时间无效，请重新选择");

    //合法，或未知取值
    default:
        return L"";
    }
}

std::wstring FormatCleanMinute(int64_t ms)
{
    LocalMinute t;
    if (!MsToLocalMinute(ms, t))
        return std::wstring();

    wchar_t buf[kFormatBufLen] = { 0 };
    swprintf_s(buf, kFormatBufLen, L"%04d-%02d-%02d %02d:%02d",
               t.m_year, t.m_month, t.m_day, t.m_hour, t.m_minute);
    return buf;
}

std::wstring BuildCleanSummaryText(int64_t beginMs, int64_t endMs)
{
    if (beginMs <= 0)
    {
        return TRF(kSummaryBefore, FormatCleanMinute(endMs));
    }
    return TRF(kSummaryBetween, FormatCleanMinute(beginMs), FormatCleanMinute(endMs));
}

std::wstring BuildCleanDoneText(int64_t beginMs, int64_t endMs)
{
    if (beginMs <= 0)
    {
        return TRF(kDoneBefore, FormatCleanMinute(endMs));
    }
    return TRF(kDoneBetween, FormatCleanMinute(beginMs), FormatCleanMinute(endMs));
}

std::vector<int> CleanYearOptions(int currentYear)
{
    std::vector<int> years;
    const int last = (currentYear < kCleanYearStart) ? kCleanYearStart : currentYear;
    for (int y = kCleanYearStart; y <= last; ++y)
    {
        years.push_back(y);
    }
    return years;
}

} // namespace balloonsettings
