/**
 *  HistoryDateRange.h
 *
 *  消息记录窗口「选日期」弹层用到的纯逻辑：本地日期与 Unix 毫秒的换算、起止日期的合法性检查、
 *  日期筛选生效后显示在时间下拉框位置上的标签文字、弹层打开时预置的起止日期。不碰窗口，
 *  FlamingoUnit 直接测试（Tests/HistoryDateRangeTests.cpp）。
 *
 *  背景：消息记录窗口原先有两个选日期的入口 ——「跳到日期」按钮（定位，不筛选）与时间下拉框里
 *  的「自定义...」（按起止日期筛选）。两者看着相像、效果不同，用户分不清，2026-10-01 合并为
 *  一个「选日期」弹层：上面选「从」「到」两个日期，下面「跳到这一天」「只看这段时间」两个按钮。
 *
 *  典型用法：
 *      int64_t startMs = 0, endMs = 0;
 *      if (CheckHistoryDateRange(from, to, startMs, endMs) == kHistoryDateRangeOk)
 *          model.SetCustomRange(startMs, endMs);
 *      label = FormatHistoryDateRangeLabel(from, to, nowYear);   //「9月1日 至 9月30日」
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <stdint.h>
#include <string>

namespace balloonhistory {

/**
 *  一个本地日期。
 */
struct HistoryDate
{
    int m_year;    //年，如 2026
    int m_month;   //月，1~12
    int m_day;     //日，1~31

    HistoryDate()
        : m_year(0)
        , m_month(0)
        , m_day(0)
    {
    }

    HistoryDate(int year, int month, int day)
        : m_year(year)
        , m_month(month)
        , m_day(day)
    {
    }
};

/**
 *  起止日期检查的结果。
 */
enum HistoryDateRangeCheck
{
    kHistoryDateRangeOk            = 0,   // 合法：两个日期都存在，且开始不晚于结束（可以是同一天）
    kHistoryDateRangeStartInvalid  = 1,   // 开始日期不存在（如 2 月 30 日、13 月）
    kHistoryDateRangeEndInvalid    = 2,   // 结束日期不存在
    kHistoryDateRangeStartAfterEnd = 3,   // 开始日期晚于结束日期
};

/**
 *  取 Unix 毫秒对应的本地日期。
 *    timestampMs：Unix 毫秒。
 *    out：        [出参] 本地日期；返回 false 时不可用。
 *  @return 换算成功返回 true。无副作用。
 */
bool HistoryDateOf(int64_t timestampMs, HistoryDate& out);

/**
 *  检查「从」「到」两个日期，合法时算出筛选用的起止时间。
 *    from / to： 用户选的开始、结束日期。
 *    outStartMs：[出参] 开始那天 0 点的 Unix 毫秒；仅返回 kHistoryDateRangeOk 时有效。
 *    outEndMs：  [出参] 结束那天最后一毫秒的 Unix 毫秒；仅返回 kHistoryDateRangeOk 时有效。
 *  @return 检查结果，见 HistoryDateRangeCheck；先查开始日期、再查结束日期、最后查先后。无副作用。
 */
HistoryDateRangeCheck CheckHistoryDateRange(const HistoryDate& from, const HistoryDate& to,
                                            int64_t& outStartMs, int64_t& outEndMs);

/**
 *  日期筛选生效后，显示在时间下拉框位置上的标签文字。
 *    · 起止都在今年：「9月1日 至 9月30日」；
 *    · 不在今年或跨年：两端都带年份，「2025年9月1日 至 2025年9月30日」；
 *    · 起止为同一天：只显示一个日期，「9月1日」或「2025年9月1日」。
 *    from / to：  筛选的开始、结束日期（调用方保证已经过 CheckHistoryDateRange）。
 *    currentYear：今年的年份，决定要不要省略年份。
 *  @return 标签文字（不含末尾的关闭叉号）。无副作用。
 */
std::wstring FormatHistoryDateRangeLabel(const HistoryDate& from, const HistoryDate& to,
                                         int currentYear);

/**
 *  「选日期」弹层打开时预置的起止日期。
 *    · 当前已有日期筛选：取它的起止；
 *    · 否则「从」「到」都取当前页第一条消息的日期 —— 最常见的用法是在它附近前后挪几天；
 *    · 当前页为空时都取今天。
 *    hasCustomRange： 当前是否有日期筛选（时间范围为自定义）。
 *    customMinMs / customMaxMs：日期筛选的起止 Unix 毫秒；hasCustomRange 为 false 时忽略。
 *    hasFirstRecord： 当前页是否有消息。
 *    firstRecordMs：  当前页第一条消息的时间（Unix 毫秒）；hasFirstRecord 为 false 时忽略。
 *    nowMs：          当前时间（Unix 毫秒）。
 *    outFrom / outTo：[出参] 预置的开始、结束日期。
 *  无副作用。
 */
void ComputeHistoryDatePopupDefaults(bool hasCustomRange, int64_t customMinMs, int64_t customMaxMs,
                                     bool hasFirstRecord, int64_t firstRecordMs, int64_t nowMs,
                                     HistoryDate& outFrom, HistoryDate& outTo);

} // namespace balloonhistory
