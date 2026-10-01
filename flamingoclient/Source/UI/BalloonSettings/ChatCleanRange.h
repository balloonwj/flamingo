/**
 *  「清理聊天记录」的时间范围计算，ChatCleanRange.h
 *
 *  系统设置 → 聊天 →「清理聊天记录」对话框里与界面无关的全部计算都在这里：预设档位换算成
 *  时间区间、本地年月日时分与 Unix 毫秒的互换、各月天数、自定义区间的合法性校验，以及对话框
 *  说明文字与成功提示的生成（docs/清理聊天记录方案.md §11）。纯逻辑，不碰窗口与网络，
 *  单元测试见 flamingoclient/Source/Tests/ChatCleanRangeTests.cpp（Suite CR，标签 [chatclean]）。
 *
 *  典型用法：
 *      int64_t beginMs = 0;
 *      int64_t endMs = 0;
 *      if (PresetToRange(kCleanBefore30Days, nowMs, beginMs, endMs))
 *          summary = BuildCleanSummaryText(beginMs, endMs);
 *
 *  时间一律按<u>本机时区</u>解释（与聊天窗、消息记录窗显示的时间一致）；区间是闭区间，
 *  单位为 Unix 毫秒，与协议 1521 的 begintime / endtime 相同。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace balloonsettings {

//年份下拉框的起始年份：产品上线之前没有聊天记录，与消息记录窗「自定义」的年份起点一致
const int kCleanYearStart = 2018;

//一天的毫秒数（24 × 60 × 60 × 1000）
const int64_t kCleanDayMs = 86400000LL;

//一分钟的毫秒数（60 × 1000）
const int64_t kCleanMinuteMs = 60000LL;

/**
 *  清理对话框「时间范围」下拉框的档位。取值即下拉框里的下标，顺序与界面一致。
 *  「3 个月 / 半年 / 1 年」按固定的 90 / 180 / 365 天折算，与消息记录窗「近 3 个月」同一口径。
 */
enum ChatCleanPreset
{
    kCleanAll            = 0,   // 全部：从最早到当前时刻
    kCleanLast1Day       = 1,   // 最近 1 天：[当前 − 1 天, 当前]
    kCleanLast7Days      = 2,   // 最近 7 天：[当前 − 7 天, 当前]
    kCleanLast30Days     = 3,   // 最近 30 天：[当前 − 30 天, 当前]
    kCleanBefore7Days    = 4,   // 7 天以前：[最早, 当前 − 7 天]
    kCleanBefore30Days   = 5,   // 30 天以前：[最早, 当前 − 30 天]
    kCleanBefore3Months  = 6,   // 3 个月以前：[最早, 当前 − 90 天]
    kCleanBeforeHalfYear = 7,   // 半年以前：[最早, 当前 − 180 天]
    kCleanBefore1Year    = 8,   // 1 年以前：[最早, 当前 − 365 天]
    kCleanCustom         = 9,   // 自定义：由开始、结束两组下拉框指定，用户手动改了任一时间时切到此档

    kCleanPresetCount    = 10   // 档位总数（哨兵，便于循环与断言）
};

/**
 *  自定义区间校验的结果。
 */
enum ChatCleanRangeError
{
    kCleanRangeOk            = 0,   // 合法，可以发起清理
    kCleanRangeStartAfterEnd = 1,   // 开始时间晚于结束时间（同一分钟不算晚于）
    kCleanRangeEndInFuture   = 2,   // 结束时间晚于当前这一分钟
    kCleanRangeBadTime       = 3    // 某个字段越界（如 13 月、2 月 30 日）或早于可选的最早年份，无法换算
};

/**
 *  本机时区下精确到分钟的一个时刻，对应对话框里一组「年 / 月 / 日 / 时 / 分」下拉框。
 */
struct LocalMinute
{
    int m_year;     // 年，如 2026；可选范围从 kCleanYearStart 起
    int m_month;    // 月，1–12
    int m_day;      // 日，1–当月天数
    int m_hour;     // 时，0–23
    int m_minute;   // 分，0–59

    LocalMinute()
        : m_year(kCleanYearStart)
        , m_month(1)
        , m_day(1)
        , m_hour(0)
        , m_minute(0)
    {
    }

    LocalMinute(int year, int month, int day, int hour, int minute)
        : m_year(year)
        , m_month(month)
        , m_day(day)
        , m_hour(hour)
        , m_minute(minute)
    {
    }

    //逐字段比较，供单元测试与界面判断"是否改动"使用
    bool operator==(const LocalMinute& other) const;
    bool operator!=(const LocalMinute& other) const;
};

/**
 *  取某个档位在下拉框里的显示文字。
 *    preset：档位，取值见 ChatCleanPreset。
 *  @return 显示文字（进程级常量串）；越界时返回空串。
 */
const wchar_t* ChatCleanPresetLabel(int preset);

/**
 *  某年某月有多少天（公历，含闰年规则：能被 4 整除且不能被 100 整除，或能被 400 整除）。
 *    year：年；month：月，1–12。
 *  @return 28–31；月份越界时返回 0。
 */
int DaysInMonth(int year, int month);

/**
 *  改了年份或月份之后，把超出当月天数的"日"改成当月最后一天；没超出的保持不变。
 *  例：1 月 31 日改成 2 月 → 2 月 28 日（闰年 29 日）。
 *    t：[入出参] 要修正的时刻；月份越界时不做修改。
 */
void ClampDayToMonth(LocalMinute& t);

/**
 *  把本机时区的某一分钟换算成该分钟第 0 毫秒的 Unix 毫秒。
 *    t：    要换算的时刻；各字段须在合法范围内（日不超过当月天数）。
 *    outMs：[出参] 换算结果。
 *  @return 字段越界或无法换算时返回 false，outMs 不变。
 */
bool LocalMinuteToMs(const LocalMinute& t, int64_t& outMs);

/**
 *  把 Unix 毫秒换算成本机时区的时刻，秒与毫秒舍去。
 *    ms：  Unix 毫秒，须非负。
 *    out： [出参] 换算结果。
 *  @return 无法换算（负数、超出 C 运行库支持的范围）时返回 false，out 不变。
 */
bool MsToLocalMinute(int64_t ms, LocalMinute& out);

/**
 *  下拉框能选到的最早时刻：kCleanYearStart 年 1 月 1 日 00:00。
 *  开始为 0（从最早开始）的区间，开始那组下拉框显示为这一时刻。
 */
LocalMinute EarliestSelectableMinute();

/**
 *  把预设档位换算成时间区间。
 *    preset： 档位，取值见 ChatCleanPreset。
 *    nowMs：  当前时刻（Unix 毫秒）。「全部 / 最近 N 天」的结束就是它，精确到毫秒。
 *    beginMs：[出参] 区间开始；0 表示从最早开始。
 *    endMs：  [出参] 区间结束（含）。
 *  @return 自定义档位或越界档位返回 false（它们不产出区间），出参不变。
 */
bool PresetToRange(int preset, int64_t nowMs, int64_t& beginMs, int64_t& endMs);

/**
 *  把一个区间换算成两组下拉框要显示的时刻（选预设档位时用来填下拉框）。
 *    beginMs：   区间开始；0 表示从最早开始，此时开始显示为 EarliestSelectableMinute()。
 *    endMs：     区间结束（含）。
 *    startShown：[出参] 开始那组下拉框显示的时刻（不早于最早可选时刻）。
 *    endShown：  [出参] 结束那组下拉框显示的时刻。
 *  @return 任一时刻无法换算时返回 false。
 */
bool RangeToDisplay(int64_t beginMs, int64_t endMs, LocalMinute& startShown, LocalMinute& endShown);

/**
 *  校验自定义区间并换算成毫秒。
 *
 *  开始取所选那一分钟的第 0 毫秒；开始恰为最早可选时刻时按 0 处理（即从最早开始）。结束取所选
 *  那一分钟的最后一毫秒（包含整分钟），若这一分钟就是当前这一分钟，则截到当前时刻。
 *
 *    start：  开始那组下拉框的时刻。
 *    end：    结束那组下拉框的时刻。
 *    nowMs：  当前时刻（Unix 毫秒）。
 *    beginMs：[出参] 区间开始；仅返回 kCleanRangeOk 时有效。
 *    endMs：  [出参] 区间结束（含）；仅返回 kCleanRangeOk 时有效。
 *  @return 校验结果，取值见 ChatCleanRangeError。
 */
int CustomToRange(const LocalMinute& start, const LocalMinute& end, int64_t nowMs,
                  int64_t& beginMs, int64_t& endMs);

/**
 *  取校验结果对应的提示文字，用于对话框里说明为什么不能清理。
 *    err：取值见 ChatCleanRangeError。
 *  @return 提示文字（进程级常量串）；kCleanRangeOk 返回空串。
 */
const wchar_t* ChatCleanRangeErrorText(int err);

/**
 *  把 Unix 毫秒格式化成本机时区的 "yyyy-MM-dd HH:mm"。
 *    ms：Unix 毫秒。
 *  @return 格式化结果；无法换算时返回空串。
 */
std::wstring FormatCleanMinute(int64_t ms);

/**
 *  生成对话框里说明将要删除什么的文字。
 *    beginMs：区间开始；0 表示从最早开始，此时写成"将删除 B 以前的……"。
 *    endMs：  区间结束（含）。
 *  @return 可直接显示的说明文字。
 */
std::wstring BuildCleanSummaryText(int64_t beginMs, int64_t endMs);

/**
 *  生成清理成功后的提示文字。区间取服务端应答里实际生效的那一段（结束可能被服务端截断）。
 *    beginMs：实际生效的区间开始；0 表示从最早开始，此时写成"已清理 B 以前的聊天记录"。
 *    endMs：  实际生效的区间结束（含）。
 *  @return 可直接显示的提示文字。
 */
std::wstring BuildCleanDoneText(int64_t beginMs, int64_t endMs);

/**
 *  年份下拉框的选项：从 kCleanYearStart 到 currentYear，逐年递增。
 *    currentYear：当前年份；早于 kCleanYearStart 时只返回 kCleanYearStart 一项。
 */
std::vector<int> CleanYearOptions(int currentYear);

} // namespace balloonsettings
