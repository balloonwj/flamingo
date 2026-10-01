/**
 *  HistoryDateRangeTests.cpp
 *
 *  消息记录窗口「选日期」弹层的纯逻辑（BalloonHistory/HistoryDateRange）测试，标签
 *  [history][daterange]：
 *
 *    DR1 起止日期合法（含同一天）时通过，起点是开始那天 0 点，终点是结束那天最后一毫秒。
 *    DR2 开始或结束日期不存在时返回对应的错误。
 *    DR3 开始晚于结束时返回错误。
 *    DR4 日期筛选生效后的标签文字：都在今年时省略年份，否则两端都带年份，同一天只显示一个日期。
 *    DR5 弹层预置的起止日期：有日期筛选取它的起止，否则取当前页第一条的日期，页为空取今天。
 *
 *  时间换算按本机时区进行，用例里的期望值也一律用 LocalDayBoundsMs 现算，不写死毫秒数。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonHistory/HistoryDateRange.h"
#include "../UI/BalloonHistory/HistoryModel.h"   // LocalDayBoundsMs

using namespace balloonhistory;

namespace {

//用例里当作"今年"的年份，取值无特殊含义
const int kThisYear = 2026;

//某一天中午 12 点的 Unix 毫秒（落在当天中间，避开时区换算的边界）。
int64_t NoonOf(int year, int month, int day)
{
    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(LocalDayBoundsMs(year, month, day, start, end));
    return start + (end - start) / 2;
}

bool SameDate(const HistoryDate& d, int year, int month, int day)
{
    return d.m_year == year && d.m_month == month && d.m_day == day;
}

} // anonymous namespace

TEST_CASE("DR1 起止日期合法时通过，起点为开始那天 0 点、终点为结束那天最后一毫秒", "[history][daterange]")
{
    int64_t startMs = 0;
    int64_t endMs = 0;
    CHECK(CheckHistoryDateRange(HistoryDate(2026, 9, 1), HistoryDate(2026, 9, 30), startMs, endMs)
          == kHistoryDateRangeOk);
    int64_t s = 0;
    int64_t e = 0;
    REQUIRE(LocalDayBoundsMs(2026, 9, 1, s, e));
    CHECK(startMs == s);
    REQUIRE(LocalDayBoundsMs(2026, 9, 30, s, e));
    CHECK(endMs == e);

    //起止为同一天：筛选那一整天
    CHECK(CheckHistoryDateRange(HistoryDate(2024, 2, 29), HistoryDate(2024, 2, 29), startMs, endMs)
          == kHistoryDateRangeOk);
    REQUIRE(LocalDayBoundsMs(2024, 2, 29, s, e));
    CHECK(startMs == s);
    CHECK(endMs == e);

    //跨年
    CHECK(CheckHistoryDateRange(HistoryDate(2025, 12, 30), HistoryDate(2026, 1, 2), startMs, endMs)
          == kHistoryDateRangeOk);
}

TEST_CASE("DR2 开始或结束日期不存在时返回对应的错误", "[history][daterange]")
{
    int64_t startMs = 0;
    int64_t endMs = 0;
    CHECK(CheckHistoryDateRange(HistoryDate(2026, 2, 30), HistoryDate(2026, 3, 1), startMs, endMs)
          == kHistoryDateRangeStartInvalid);
    CHECK(CheckHistoryDateRange(HistoryDate(2026, 3, 1), HistoryDate(2026, 4, 31), startMs, endMs)
          == kHistoryDateRangeEndInvalid);
    CHECK(CheckHistoryDateRange(HistoryDate(2025, 2, 29), HistoryDate(2025, 3, 1), startMs, endMs)
          == kHistoryDateRangeStartInvalid);   //2025 年不是闰年
    CHECK(CheckHistoryDateRange(HistoryDate(2026, 13, 1), HistoryDate(2026, 13, 2), startMs, endMs)
          == kHistoryDateRangeStartInvalid);   //两个都不存在时先报开始日期
}

TEST_CASE("DR3 开始晚于结束时返回错误", "[history][daterange]")
{
    int64_t startMs = 0;
    int64_t endMs = 0;
    CHECK(CheckHistoryDateRange(HistoryDate(2026, 9, 2), HistoryDate(2026, 9, 1), startMs, endMs)
          == kHistoryDateRangeStartAfterEnd);
    CHECK(CheckHistoryDateRange(HistoryDate(2026, 1, 1), HistoryDate(2025, 12, 31), startMs, endMs)
          == kHistoryDateRangeStartAfterEnd);
}

TEST_CASE("DR4 标签文字：今年省略年份，其余带年份，同一天只显示一个日期", "[history][daterange]")
{
    CHECK(FormatHistoryDateRangeLabel(HistoryDate(2026, 9, 1), HistoryDate(2026, 9, 30), kThisYear)
          == L"9月1日 至 9月30日");
    CHECK(FormatHistoryDateRangeLabel(HistoryDate(2025, 9, 1), HistoryDate(2025, 9, 30), kThisYear)
          == L"2025年9月1日 至 2025年9月30日");
    //跨年：两端都带年份，免得读成同一年
    CHECK(FormatHistoryDateRangeLabel(HistoryDate(2025, 12, 30), HistoryDate(2026, 1, 2), kThisYear)
          == L"2025年12月30日 至 2026年1月2日");
    //同一天
    CHECK(FormatHistoryDateRangeLabel(HistoryDate(2026, 9, 1), HistoryDate(2026, 9, 1), kThisYear)
          == L"9月1日");
    CHECK(FormatHistoryDateRangeLabel(HistoryDate(2024, 2, 29), HistoryDate(2024, 2, 29), kThisYear)
          == L"2024年2月29日");
}

TEST_CASE("DR5 弹层预置日期：有日期筛选取其起止，否则取当前页第一条，页为空取今天", "[history][daterange]")
{
    HistoryDate from;
    HistoryDate to;
    const int64_t now = NoonOf(2026, 10, 1);

    //有日期筛选：取它的起止
    int64_t s = 0;
    int64_t e = 0;
    REQUIRE(LocalDayBoundsMs(2026, 9, 1, s, e));
    const int64_t customMin = s;
    REQUIRE(LocalDayBoundsMs(2026, 9, 30, s, e));
    const int64_t customMax = e;
    ComputeHistoryDatePopupDefaults(true, customMin, customMax, true, NoonOf(2026, 9, 15), now,
                                    from, to);
    CHECK(SameDate(from, 2026, 9, 1));
    CHECK(SameDate(to, 2026, 9, 30));

    //没有日期筛选：起止都取当前页第一条消息的日期
    ComputeHistoryDatePopupDefaults(false, 0, 0, true, NoonOf(2026, 8, 20), now, from, to);
    CHECK(SameDate(from, 2026, 8, 20));
    CHECK(SameDate(to, 2026, 8, 20));

    //当前页为空：取今天
    ComputeHistoryDatePopupDefaults(false, 0, 0, false, 0, now, from, to);
    CHECK(SameDate(from, 2026, 10, 1));
    CHECK(SameDate(to, 2026, 10, 1));
}
