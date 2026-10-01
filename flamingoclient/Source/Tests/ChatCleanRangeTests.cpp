/**
 *  「清理聊天记录」时间范围计算的单元测试（Suite CR，标签 [chatclean]），ChatCleanRangeTests.cpp
 *
 *  覆盖（docs/清理聊天记录方案.md §11.4，用例已与用户确认）：
 *    CR1  各月天数：大月 31、小月 30；2 月平年 28、闰年 29。
 *    CR2  改年份或月份后"日"超出当月天数时改成当月最后一天，没超出的保持不变。
 *    CR3  本地年月日时分与毫秒互相换算：往返一致；相邻两分钟相差 60000 毫秒（跨日、跨月、跨年也一样）。
 *    CR4  预设档位换算成区间；开始为 0 的档位，开始那组下拉框显示为最早可选时刻。
 *    CR5  自定义区间换算：开始取整分钟、结束包含整分钟；结束是当前这一分钟时截到当前时刻；
 *         开始是最早可选时刻时按 0 处理。
 *    CR6  自定义区间校验：开始晚于结束、结束晚于当前这一分钟都不合法；同一分钟合法。
 *    CR7  对话框说明文字。
 *    CR8  清理成功提示。
 *    CR9  年份下拉框的选项。
 *
 *  换算按本机时区进行。为了不依赖跑测试的机器在哪个时区，断言只用两类与时区无关的性质：
 *  往返一致、以及同一时区内两个时刻的毫秒差；需要"当前时刻"的用例都由 LocalMinuteToMs 现场
 *  算出一个固定时刻，而不是写死一个毫秒数。所选日期都避开了夏令时切换日。
 *
 *  框架 Catch2 v3。纯逻辑，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonSettings/ChatCleanRange.h"

#include <string>
#include <vector>

using namespace balloonsettings;

namespace {

//测试里固定的"当前时刻"：2026-08-31 21:45 再过 30.5 秒
const int kNowExtraMs = 30500;

//取测试用的当前时刻（Unix 毫秒）。
int64_t FixedNowMs()
{
    int64_t minuteMs = 0;
    REQUIRE(LocalMinuteToMs(LocalMinute(2026, 8, 31, 21, 45), minuteMs));
    return minuteMs + kNowExtraMs;
}

//把某一分钟换算成毫秒，换算失败直接判用例失败。
int64_t MinuteMs(int year, int month, int day, int hour, int minute)
{
    int64_t ms = 0;
    REQUIRE(LocalMinuteToMs(LocalMinute(year, month, day, hour, minute), ms));
    return ms;
}

//把 LocalMinute 拼成便于阅读的失败信息。
std::string Describe(const LocalMinute& t)
{
    char buf[64] = { 0 };
    sprintf_s(buf, "%04d-%02d-%02d %02d:%02d", t.m_year, t.m_month, t.m_day, t.m_hour, t.m_minute);
    return buf;
}

//宽字符串里是否包含某个子串。
bool Contains(const std::wstring& text, const wchar_t* needle)
{
    return text.find(needle) != std::wstring::npos;
}

} // anonymous namespace

TEST_CASE("CR1 各月天数：大月 31、小月 30，2 月按闰年规则", "[chatclean]")
{
    const int bigMonths[] = { 1, 3, 5, 7, 8, 10, 12 };
    for (size_t i = 0; i < sizeof(bigMonths) / sizeof(bigMonths[0]); ++i)
    {
        INFO("month " << bigMonths[i]);
        CHECK(DaysInMonth(2026, bigMonths[i]) == 31);
    }
    const int smallMonths[] = { 4, 6, 9, 11 };
    for (size_t i = 0; i < sizeof(smallMonths) / sizeof(smallMonths[0]); ++i)
    {
        INFO("month " << smallMonths[i]);
        CHECK(DaysInMonth(2026, smallMonths[i]) == 30);
    }

    // 能被 4 整除且不能被 100 整除是闰年；能被 400 整除也是闰年
    CHECK(DaysInMonth(2023, 2) == 28);
    CHECK(DaysInMonth(2024, 2) == 29);
    CHECK(DaysInMonth(2000, 2) == 29);
    CHECK(DaysInMonth(2100, 2) == 28);

    // 月份越界
    CHECK(DaysInMonth(2026, 0) == 0);
    CHECK(DaysInMonth(2026, 13) == 0);
}

TEST_CASE("CR2 改年份或月份后超出当月天数的日改成当月最后一天", "[chatclean]")
{
    // 1 月 31 日改成 2 月：平年 28 日、闰年 29 日
    LocalMinute a(2025, 2, 31, 10, 30);
    ClampDayToMonth(a);
    CHECK(a == LocalMinute(2025, 2, 28, 10, 30));

    LocalMinute b(2024, 2, 31, 10, 30);
    ClampDayToMonth(b);
    CHECK(b == LocalMinute(2024, 2, 29, 10, 30));

    // 2024-02-29 把年份改成 2025
    LocalMinute c(2025, 2, 29, 0, 0);
    ClampDayToMonth(c);
    CHECK(c == LocalMinute(2025, 2, 28, 0, 0));

    // 3 月 31 日改成 4 月
    LocalMinute d(2026, 4, 31, 23, 59);
    ClampDayToMonth(d);
    CHECK(d == LocalMinute(2026, 4, 30, 23, 59));

    // 没超出的保持不变
    LocalMinute e(2026, 4, 15, 8, 5);
    ClampDayToMonth(e);
    CHECK(e == LocalMinute(2026, 4, 15, 8, 5));
}

TEST_CASE("CR3 本地年月日时分与毫秒互相换算", "[chatclean]")
{
    // 往返一致
    const LocalMinute samples[] =
    {
        LocalMinute(2018, 1, 1, 0, 0),
        LocalMinute(2024, 2, 29, 23, 59),
        LocalMinute(2026, 1, 15, 10, 30),
        LocalMinute(2026, 8, 31, 21, 45),
    };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        INFO(Describe(samples[i]));
        int64_t ms = 0;
        REQUIRE(LocalMinuteToMs(samples[i], ms));
        CHECK(ms % kCleanMinuteMs == 0);

        LocalMinute back;
        REQUIRE(MsToLocalMinute(ms, back));
        CHECK(back == samples[i]);

        // 同一分钟里的任何一毫秒都换算回这一分钟（秒与毫秒舍去）
        LocalMinute lastMs;
        REQUIRE(MsToLocalMinute(ms + kCleanMinuteMs - 1, lastMs));
        CHECK(lastMs == samples[i]);
    }

    // 相邻两分钟相差 60000 毫秒：同一小时内、跨日、跨月、跨年
    CHECK(MinuteMs(2026, 3, 10, 9, 1) - MinuteMs(2026, 3, 10, 9, 0) == kCleanMinuteMs);
    CHECK(MinuteMs(2026, 3, 11, 0, 0) - MinuteMs(2026, 3, 10, 23, 59) == kCleanMinuteMs);
    CHECK(MinuteMs(2024, 3, 1, 0, 0) - MinuteMs(2024, 2, 29, 23, 59) == kCleanMinuteMs);
    CHECK(MinuteMs(2026, 1, 1, 0, 0) - MinuteMs(2025, 12, 31, 23, 59) == kCleanMinuteMs);

    // 字段越界时换算失败
    int64_t ms = 0;
    CHECK_FALSE(LocalMinuteToMs(LocalMinute(2026, 13, 1, 0, 0), ms));
    CHECK_FALSE(LocalMinuteToMs(LocalMinute(2026, 2, 30, 0, 0), ms));
    CHECK_FALSE(LocalMinuteToMs(LocalMinute(2026, 1, 1, 24, 0), ms));
    CHECK_FALSE(LocalMinuteToMs(LocalMinute(2026, 1, 1, 0, 60), ms));
    LocalMinute out;
    CHECK_FALSE(MsToLocalMinute(-1, out));
}

TEST_CASE("CR4 预设档位换算成区间", "[chatclean]")
{
    const int64_t now = FixedNowMs();

    // 档位文字按界面顺序排列
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanAll)) == L"全部");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanLast1Day)) == L"最近 1 天");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanLast7Days)) == L"最近 7 天");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanLast30Days)) == L"最近 30 天");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanBefore7Days)) == L"7 天以前");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanBefore30Days)) == L"30 天以前");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanBefore3Months)) == L"3 个月以前");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanBeforeHalfYear)) == L"半年以前");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanBefore1Year)) == L"1 年以前");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanCustom)) == L"自定义");
    CHECK(std::wstring(ChatCleanPresetLabel(kCleanPresetCount)).empty());

    int64_t b = -1;
    int64_t e = -1;

    // 全部：[0, 当前]
    REQUIRE(PresetToRange(kCleanAll, now, b, e));
    CHECK(b == 0);
    CHECK(e == now);

    // 最近 N 天：[当前 − N 天, 当前]
    const int lastPresets[] = { kCleanLast1Day, kCleanLast7Days, kCleanLast30Days };
    const int lastDays[] = { 1, 7, 30 };
    for (int i = 0; i < 3; ++i)
    {
        INFO("preset " << lastPresets[i]);
        REQUIRE(PresetToRange(lastPresets[i], now, b, e));
        CHECK(b == now - lastDays[i] * kCleanDayMs);
        CHECK(e == now);
    }

    // N 天以前：[0, 当前 − N 天]；3 个月、半年、1 年按 90、180、365 天
    const int beforePresets[] = { kCleanBefore7Days, kCleanBefore30Days, kCleanBefore3Months,
                                  kCleanBeforeHalfYear, kCleanBefore1Year };
    const int beforeDays[] = { 7, 30, 90, 180, 365 };
    for (int i = 0; i < 5; ++i)
    {
        INFO("preset " << beforePresets[i]);
        REQUIRE(PresetToRange(beforePresets[i], now, b, e));
        CHECK(b == 0);
        CHECK(e == now - beforeDays[i] * kCleanDayMs);
    }

    // 自定义与越界档位不产出区间，出参不变
    b = 123;
    e = 456;
    CHECK_FALSE(PresetToRange(kCleanCustom, now, b, e));
    CHECK_FALSE(PresetToRange(kCleanPresetCount, now, b, e));
    CHECK_FALSE(PresetToRange(-1, now, b, e));
    CHECK(b == 123);
    CHECK(e == 456);

    // 开始为 0 的区间：开始那组下拉框显示为最早可选时刻，结束显示当前这一分钟
    CHECK(EarliestSelectableMinute() == LocalMinute(kCleanYearStart, 1, 1, 0, 0));
    LocalMinute startShown;
    LocalMinute endShown;
    REQUIRE(RangeToDisplay(0, now, startShown, endShown));
    CHECK(startShown == LocalMinute(kCleanYearStart, 1, 1, 0, 0));
    CHECK(endShown == LocalMinute(2026, 8, 31, 21, 45));

    // 最近 7 天：开始显示 7 天前的同一分钟
    REQUIRE(PresetToRange(kCleanLast7Days, now, b, e));
    REQUIRE(RangeToDisplay(b, e, startShown, endShown));
    CHECK(startShown == LocalMinute(2026, 8, 24, 21, 45));
    CHECK(endShown == LocalMinute(2026, 8, 31, 21, 45));
}

TEST_CASE("CR5 自定义区间换算：开始取整分钟、结束包含整分钟", "[chatclean]")
{
    const int64_t now = FixedNowMs();
    int64_t b = -1;
    int64_t e = -1;

    // 普通区间
    REQUIRE(CustomToRange(LocalMinute(2025, 3, 1, 8, 0), LocalMinute(2025, 3, 31, 18, 30), now, b, e)
            == kCleanRangeOk);
    CHECK(b == MinuteMs(2025, 3, 1, 8, 0));
    CHECK(e == MinuteMs(2025, 3, 31, 18, 30) + kCleanMinuteMs - 1);

    // 结束就是当前这一分钟：截到当前时刻
    REQUIRE(CustomToRange(LocalMinute(2026, 8, 1, 0, 0), LocalMinute(2026, 8, 31, 21, 45), now, b, e)
            == kCleanRangeOk);
    CHECK(b == MinuteMs(2026, 8, 1, 0, 0));
    CHECK(e == now);

    // 开始是最早可选时刻：按 0 处理
    REQUIRE(CustomToRange(EarliestSelectableMinute(), LocalMinute(2020, 6, 1, 12, 0), now, b, e)
            == kCleanRangeOk);
    CHECK(b == 0);
    CHECK(e == MinuteMs(2020, 6, 1, 12, 0) + kCleanMinuteMs - 1);

    // 开始比最早可选时刻晚一分钟：照常换算，不按 0 处理
    REQUIRE(CustomToRange(LocalMinute(kCleanYearStart, 1, 1, 0, 1), LocalMinute(2020, 6, 1, 12, 0),
                          now, b, e) == kCleanRangeOk);
    CHECK(b == MinuteMs(kCleanYearStart, 1, 1, 0, 1));
}

TEST_CASE("CR6 自定义区间校验", "[chatclean]")
{
    const int64_t now = FixedNowMs();
    int64_t b = -1;
    int64_t e = -1;

    // 开始晚于结束
    CHECK(CustomToRange(LocalMinute(2025, 5, 2, 0, 0), LocalMinute(2025, 5, 1, 23, 59), now, b, e)
          == kCleanRangeStartAfterEnd);
    CHECK(std::wstring(ChatCleanRangeErrorText(kCleanRangeStartAfterEnd)) == L"开始时间不能晚于结束时间");

    // 结束晚于当前这一分钟（下一分钟、下一年都算）
    CHECK(CustomToRange(LocalMinute(2026, 8, 1, 0, 0), LocalMinute(2026, 8, 31, 21, 46), now, b, e)
          == kCleanRangeEndInFuture);
    CHECK(CustomToRange(LocalMinute(2026, 8, 1, 0, 0), LocalMinute(2027, 1, 1, 0, 0), now, b, e)
          == kCleanRangeEndInFuture);
    CHECK(std::wstring(ChatCleanRangeErrorText(kCleanRangeEndInFuture)) == L"结束时间不能晚于当前时间");

    // 开始与结束在同一分钟：合法，区间就是这一整分钟
    REQUIRE(CustomToRange(LocalMinute(2025, 5, 1, 9, 30), LocalMinute(2025, 5, 1, 9, 30), now, b, e)
            == kCleanRangeOk);
    CHECK(e - b == kCleanMinuteMs - 1);

    // 字段越界、早于可选的最早年份
    CHECK(CustomToRange(LocalMinute(2025, 2, 30, 0, 0), LocalMinute(2025, 5, 1, 0, 0), now, b, e)
          == kCleanRangeBadTime);
    CHECK(CustomToRange(LocalMinute(kCleanYearStart - 1, 12, 31, 0, 0), LocalMinute(2025, 5, 1, 0, 0),
                        now, b, e) == kCleanRangeBadTime);

    // 合法时没有提示文字
    CHECK(std::wstring(ChatCleanRangeErrorText(kCleanRangeOk)).empty());
}

TEST_CASE("CR7 对话框说明文字", "[chatclean]")
{
    const std::wstring tail = L"之间的全部单聊和群聊记录，本机与服务器同时删除，对方不受影响，删除后无法恢复。";

    // 有开始时间
    const int64_t b = MinuteMs(2025, 3, 1, 8, 0);
    const int64_t e = MinuteMs(2025, 3, 31, 18, 30) + kCleanMinuteMs - 1;
    CHECK(FormatCleanMinute(b) == L"2025-03-01 08:00");
    CHECK(FormatCleanMinute(e) == L"2025-03-31 18:30");
    CHECK(BuildCleanSummaryText(b, e) == L"将删除 2025-03-01 08:00 至 2025-03-31 18:30 " + tail);

    // 开始为 0：从最早开始
    const std::wstring fromOldest = BuildCleanSummaryText(0, e);
    CHECK(fromOldest == L"将删除 2025-03-31 18:30 以前的全部单聊和群聊记录，本机与服务器同时删除，"
                        L"对方不受影响，删除后无法恢复。");
    CHECK_FALSE(Contains(fromOldest, L"2018"));
}

TEST_CASE("CR8 清理成功提示", "[chatclean]")
{
    // 服务端截断后的结束（某一秒开始前 1 毫秒）照样按所在分钟显示
    const int64_t b = MinuteMs(2025, 3, 1, 8, 0);
    const int64_t e = MinuteMs(2026, 8, 31, 21, 45) + 29999;
    CHECK(BuildCleanDoneText(b, e) == L"已清理 2025-03-01 08:00 至 2026-08-31 21:45 的聊天记录");
    CHECK(BuildCleanDoneText(0, e) == L"已清理 2026-08-31 21:45 以前的聊天记录");
}

TEST_CASE("CR9 年份下拉框的选项", "[chatclean]")
{
    const std::vector<int> years = CleanYearOptions(2026);
    REQUIRE(years.size() == 9);
    for (size_t i = 0; i < years.size(); ++i)
    {
        CHECK(years[i] == kCleanYearStart + (int)i);
    }

    // 当前年份就是起始年份、或早于起始年份（本机时钟错乱）时只有起始年份一项
    REQUIRE(CleanYearOptions(kCleanYearStart).size() == 1);
    const std::vector<int> early = CleanYearOptions(kCleanYearStart - 3);
    REQUIRE(early.size() == 1);
    CHECK(early[0] == kCleanYearStart);
}
