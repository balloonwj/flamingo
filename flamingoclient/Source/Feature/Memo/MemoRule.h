/**
 *  MemoRule.h
 *
 *  「备忘与提醒」的提醒时间计算（纯逻辑）：按一条备忘的开始时间与重复规则，枚举它的各次事件时间；
 *  事件时间减去提前分钟数就是提醒时间。不碰窗口、不读系统时钟，调用方把「现在」作为参数传进来，
 *  便于 FlamingoUnit 直接断言（Tests/MemoRuleTests.cpp）。
 *
 *  时间一律用「本地分钟序号」表示：从本地时间 1970-01-01 00:00 起算的分钟数，只做公历日历换算、
 *  不涉及时区与夏令时。服务端存的开始时间本来就是本地的年月日时分（docs/备忘与提醒方案.md 第 2 节）。
 *
 *  重复规则（与服务端 Protocol.h 的 memo_repeat_type 一致）：
 *    · 不重复：只有开始时间这一次；
 *    · 每天：开始时间起每天同一时刻；
 *    · 工作日：开始那天起每个周一到周五的同一时刻（开始那天是周末时，从下一个工作日算起）；
 *    · 每周：开始那天起星期掩码里选中的那几天（开始那天没选中时，从下一个选中的日子算起）；
 *    · 每月：开始那天的「几号」，当月没有这一天时取当月最后一天；
 *    · 每年：开始那天的月日，2 月 29 日在平年取 2 月 28 日。
 *  次数上限 count 为 0 时永不结束，否则只有前 count 次事件。
 *
 *  典型用法：
 *      memo::MemoRuleSpec rule = ...;
 *      memo::MemoOccurrence occ;
 *      if (memo::FindEventAtOrAfter(rule, now + rule.m_advance + 1, occ))
 *          下一次提醒时间 = occ.m_event - rule.m_advance;
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <vector>

namespace memo {

//本地分钟序号：从本地时间 1970-01-01 00:00 起算的分钟数
typedef int64_t LocalMinute;

//一天的分钟数
const int64_t kMinutesPerDay = 1440;

//一周的天数
const int kDaysPerWeek = 7;

//每周重复的星期掩码里「周一到周五」五位，工作日重复按它计算
const int kMemoWorkdayMask = 0x1F;

//星期掩码的合法位：bit0 周一 ... bit6 周日
const int kMemoWeekdayAllMask = 0x7F;

//重复方式，取值与服务端 Protocol.h 的 memo_repeat_type 一致
enum MemoRepeat
{
    kMemoRepeatNone    = 0,   //不重复
    kMemoRepeatDaily   = 1,   //每天
    kMemoRepeatWorkday = 2,   //每个工作日（周一到周五）
    kMemoRepeatWeekly  = 3,   //每周，选中的那几天
    kMemoRepeatMonthly = 4,   //每月
    kMemoRepeatYearly  = 5,   //每年
};

/**
 *  一条备忘里与提醒时间有关的字段。与 net::MemoItem 分开定义，本文件因此不依赖报文模块。
 */
struct MemoRuleSpec
{
    int64_t m_start;      //首次事件的本地时间，十进制 yyyymmddhhmm
    int     m_advance;    //提前分钟数，0～1440，0 为到点提醒
    int     m_repeat;     //重复方式，取值见 MemoRepeat
    int     m_weekdays;   //每周重复的星期掩码（bit0 周一 ... bit6 周日），其它重复方式不看
    int     m_count;      //次数上限，0 为永不结束

    MemoRuleSpec() : m_start(0), m_advance(0), m_repeat(kMemoRepeatNone), m_weekdays(0), m_count(1) {}
};

/**
 *  一次事件：事件时间与它是第几次（从 0 起）。
 */
struct MemoOccurrence
{
    LocalMinute m_event;   //事件时间（本地分钟序号），未减去提前分钟
    int64_t     m_index;   //第几次事件，从 0 起

    MemoOccurrence() : m_event(0), m_index(0) {}
};

/**
 *  公历年月日换算成从 1970-01-01 起的天数。
 *    year：年（不限范围）；month：1～12；day：1～31，调用方保证是存在的日期。
 *  @return 天数，1970-01-01 为 0
 */
int64_t DaysFromCivil(int year, int month, int day);

/**
 *  从 1970-01-01 起的天数换算成公历年月日。
 *    days：天数；year / month / day：[出参] 年、月（1～12）、日（1～31）。
 */
void CivilFromDays(int64_t days, int& year, int& month, int& day);

/**
 *  某年某月的天数（含闰年判断）。
 *    month：1～12。
 */
int DaysInMonth(int year, int month);

/**
 *  从 1970-01-01 起第 days 天是星期几。
 *  @return 0 周一 ... 6 周日，与星期掩码的位序一致
 */
int WeekdayOfDays(int64_t days);

/**
 *  年月日时分换算成本地分钟序号。参数取值同 DaysFromCivil，hour 0～23，minute 0～59。
 */
LocalMinute LocalMinuteFromParts(int year, int month, int day, int hour, int minute);

/**
 *  十进制 yyyymmddhhmm 换算成本地分钟序号。
 *    start：yyyymmddhhmm；out：[出参] 本地分钟序号。
 *  @return start 是一个存在的日期时间时返回 true
 */
bool LocalMinuteFromStart(int64_t start, LocalMinute& out);

/**
 *  本地分钟序号换算成十进制 yyyymmddhhmm。
 */
int64_t StartFromLocalMinute(LocalMinute minute);

/**
 *  本地分钟序号拆成年月日时分。
 */
void PartsFromLocalMinute(LocalMinute minute, int& year, int& month, int& day, int& hour, int& minuteOfHour);

/**
 *  找出事件时间 ≥ from 的第一次事件。
 *    rule：备忘的规则；from：本地分钟序号；out：[出参] 找到的那次事件。
 *  @return 找到返回 true；规则不合法、或 from 之后已没有事件（超出次数上限、不重复且已过）返回 false
 */
bool FindEventAtOrAfter(const MemoRuleSpec& rule, LocalMinute from, MemoOccurrence& out);

/**
 *  找出事件时间 ≤ to 的最后一次事件（受次数上限约束：超出上限之后的时间点返回最后一次）。
 *    rule：备忘的规则；to：本地分钟序号；out：[出参] 找到的那次事件。
 *  @return 找到返回 true；规则不合法、或 to 早于第一次事件返回 false
 */
bool FindEventAtOrBefore(const MemoRuleSpec& rule, LocalMinute to, MemoOccurrence& out);

/**
 *  列出事件时间落在 [from, to) 内的各次事件，按时间升序，最多 maxCount 次。月历上标圆点、
 *  当天列表都用它。
 *    out：[出参] 先清空再填入。
 */
void ListEventsInRange(const MemoRuleSpec& rule, LocalMinute from, LocalMinute to, size_t maxCount,
                       std::vector<MemoOccurrence>& out);

/**
 *  规则本身是否合法：开始时间是存在的日期时间、重复方式在范围内、每周重复至少选了一天、次数不为负。
 */
bool IsValidRule(const MemoRuleSpec& rule);

} // namespace memo
