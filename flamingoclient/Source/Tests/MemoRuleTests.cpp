/**
 *  MemoRuleTests.cpp
 *
 *  「备忘与提醒」提醒计算 memo::MemoRule 与调度 memo::CMemoScheduler 的测试，覆盖与用户确认过的
 *  用例 MEM-R1～MEM-R8（docs/备忘与提醒方案.md 第 5 节）：
 *    MEM-R1 不重复：只有开始时间这一次；
 *    MEM-R2 各重复规则：每天、工作日（开始那天是周末）、每周多选、每月（31 号遇到小月、2 月）、每年（闰日）；
 *    MEM-R3 提醒 N 次后结束；
 *    MEM-R4 提前提醒跨天；
 *    MEM-R5 错过的提醒只补最近 24 小时内的，每条备忘只补最近一次；
 *    MEM-R6 稍后提醒在 10 分钟后再提醒一次；
 *    MEM-R7 修改后按新规则重新计算，时间改到过去时不立刻弹出；
 *    MEM-R8 时间回拨与休眠唤醒都不重复弹出。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口、不读系统时钟。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <string>
#include <vector>

#include "../Feature/Memo/MemoRule.h"
#include "../Feature/Memo/MemoScheduler.h"

using namespace memo;

namespace {

//两个测试备忘的 ID
const int64_t kMemoA = 101;
const int64_t kMemoB = 102;

//本地时间 yyyy-mm-dd hh:mm 的分钟序号
LocalMinute At(int y, int mo, int d, int h, int mi)
{
    return LocalMinuteFromParts(y, mo, d, h, mi);
}

MemoRuleSpec Rule(int64_t start, int repeat, int count)
{
    MemoRuleSpec r;
    r.m_start = start;
    r.m_repeat = repeat;
    r.m_count = count;
    return r;
}

//事件时间 ≥ from 的第一次
LocalMinute NextEvent(const MemoRuleSpec& rule, LocalMinute from)
{
    MemoOccurrence occ;
    REQUIRE(FindEventAtOrAfter(rule, from, occ));
    return occ.m_event;
}

//列出 [from, to) 内各次事件的时间
std::vector<LocalMinute> Events(const MemoRuleSpec& rule, LocalMinute from, LocalMinute to)
{
    const size_t kMax = 64;
    std::vector<MemoOccurrence> occ;
    ListEventsInRange(rule, from, to, kMax, occ);
    std::vector<LocalMinute> v;
    for (size_t i = 0; i < occ.size(); ++i)
        v.push_back(occ[i].m_event);
    return v;
}

MemoScheduleInput Input(int64_t memoid, const MemoRuleSpec& rule, LocalMinute updatedAt)
{
    MemoScheduleInput in;
    in.m_memoid = memoid;
    in.m_rule = rule;
    in.m_updatedAt = updatedAt;
    return in;
}

} // namespace

TEST_CASE("MEM-R0 日历换算与星期", "[memo]")
{
    int y = 0;
    int m = 0;
    int d = 0;
    CHECK(DaysFromCivil(1970, 1, 1) == 0);
    CivilFromDays(DaysFromCivil(2028, 2, 29), y, m, d);
    CHECK(y == 2028);
    CHECK(m == 2);
    CHECK(d == 29);
    CHECK(DaysInMonth(2026, 2) == 28);
    CHECK(DaysInMonth(2028, 2) == 29);
    CHECK(DaysInMonth(2100, 2) == 28);
    CHECK(DaysInMonth(2000, 2) == 29);
    //2026-10-03 是星期六，2026-10-05 是星期一
    CHECK(WeekdayOfDays(DaysFromCivil(2026, 10, 3)) == 5);
    CHECK(WeekdayOfDays(DaysFromCivil(2026, 10, 5)) == 0);

    LocalMinute t = 0;
    REQUIRE(LocalMinuteFromStart(202610050930LL, t));
    CHECK(t == At(2026, 10, 5, 9, 30));
    CHECK(StartFromLocalMinute(t) == 202610050930LL);
    CHECK_FALSE(LocalMinuteFromStart(202602291000LL, t));
    CHECK_FALSE(LocalMinuteFromStart(202610052400LL, t));
}

TEST_CASE("MEM-R1 不重复只有一次", "[memo]")
{
    const MemoRuleSpec rule = Rule(202610050930LL, kMemoRepeatNone, 1);
    MemoOccurrence occ;
    REQUIRE(FindEventAtOrAfter(rule, At(2026, 10, 1, 0, 0), occ));
    CHECK(occ.m_event == At(2026, 10, 5, 9, 30));
    CHECK(occ.m_index == 0);
    CHECK_FALSE(FindEventAtOrAfter(rule, At(2026, 10, 5, 9, 31), occ));
    CHECK_FALSE(FindEventAtOrBefore(rule, At(2026, 10, 5, 9, 29), occ));
    REQUIRE(FindEventAtOrBefore(rule, At(2027, 1, 1, 0, 0), occ));
    CHECK(occ.m_event == At(2026, 10, 5, 9, 30));

    //不重复时次数字段写什么都按一次
    const MemoRuleSpec many = Rule(202610050930LL, kMemoRepeatNone, 5);
    CHECK(Events(many, At(2026, 1, 1, 0, 0), At(2027, 1, 1, 0, 0)).size() == 1);
}

TEST_CASE("MEM-R2 各重复规则", "[memo]")
{
    SECTION("每天")
    {
        const MemoRuleSpec rule = Rule(202610050930LL, kMemoRepeatDaily, 0);
        std::vector<LocalMinute> v = Events(rule, At(2026, 10, 5, 9, 30), At(2026, 10, 8, 9, 30));
        REQUIRE(v.size() == 3);
        CHECK(v[2] == At(2026, 10, 7, 9, 30));
        CHECK(NextEvent(rule, At(2026, 10, 6, 9, 31)) == At(2026, 10, 7, 9, 30));
        CHECK(NextEvent(rule, At(2026, 10, 1, 0, 0)) == At(2026, 10, 5, 9, 30));
    }

    SECTION("工作日：开始那天是周六，从周一算起，跳过周末")
    {
        const MemoRuleSpec rule = Rule(202610030900LL, kMemoRepeatWorkday, 0);
        std::vector<LocalMinute> v = Events(rule, At(2026, 10, 1, 0, 0), At(2026, 10, 13, 0, 0));
        REQUIRE(v.size() == 6);
        CHECK(v[0] == At(2026, 10, 5, 9, 0));    //周一
        CHECK(v[4] == At(2026, 10, 9, 9, 0));    //周五
        CHECK(v[5] == At(2026, 10, 12, 9, 0));   //下周一
    }

    SECTION("每周：选周一与周三")
    {
        MemoRuleSpec rule = Rule(202610050800LL, kMemoRepeatWeekly, 0);
        rule.m_weekdays = 0x1 | 0x4;
        std::vector<LocalMinute> v = Events(rule, At(2026, 10, 5, 0, 0), At(2026, 10, 20, 0, 0));
        REQUIRE(v.size() == 5);
        CHECK(v[0] == At(2026, 10, 5, 8, 0));
        CHECK(v[1] == At(2026, 10, 7, 8, 0));
        CHECK(v[2] == At(2026, 10, 12, 8, 0));
        CHECK(v[4] == At(2026, 10, 19, 8, 0));

        //开始那天（周二）没选中：第一次是周三
        rule.m_start = 202610060800LL;
        CHECK(NextEvent(rule, At(2026, 10, 1, 0, 0)) == At(2026, 10, 7, 8, 0));

        //没选任何一天的规则不合法
        rule.m_weekdays = 0;
        MemoOccurrence occ;
        CHECK_FALSE(FindEventAtOrAfter(rule, At(2026, 10, 1, 0, 0), occ));
    }

    SECTION("每月：31 号在小月与 2 月取当月最后一天")
    {
        const MemoRuleSpec rule = Rule(202601311800LL, kMemoRepeatMonthly, 0);
        std::vector<LocalMinute> v = Events(rule, At(2026, 1, 1, 0, 0), At(2026, 6, 1, 0, 0));
        REQUIRE(v.size() == 5);
        CHECK(v[0] == At(2026, 1, 31, 18, 0));
        CHECK(v[1] == At(2026, 2, 28, 18, 0));
        CHECK(v[2] == At(2026, 3, 31, 18, 0));
        CHECK(v[3] == At(2026, 4, 30, 18, 0));
        CHECK(v[4] == At(2026, 5, 31, 18, 0));
        //闰年的 2 月取 29 日
        CHECK(NextEvent(rule, At(2028, 2, 1, 0, 0)) == At(2028, 2, 29, 18, 0));
        //跨年
        CHECK(NextEvent(rule, At(2026, 12, 31, 18, 1)) == At(2027, 1, 31, 18, 0));
    }

    SECTION("每年：2 月 29 日在平年取 2 月 28 日")
    {
        const MemoRuleSpec rule = Rule(202802291000LL, kMemoRepeatYearly, 0);
        std::vector<LocalMinute> v = Events(rule, At(2028, 1, 1, 0, 0), At(2033, 1, 1, 0, 0));
        REQUIRE(v.size() == 5);
        CHECK(v[0] == At(2028, 2, 29, 10, 0));
        CHECK(v[1] == At(2029, 2, 28, 10, 0));
        CHECK(v[3] == At(2031, 2, 28, 10, 0));
        CHECK(v[4] == At(2032, 2, 29, 10, 0));
    }
}

TEST_CASE("MEM-R3 提醒 N 次后结束", "[memo]")
{
    const MemoRuleSpec daily = Rule(202610050930LL, kMemoRepeatDaily, 3);
    std::vector<LocalMinute> v = Events(daily, At(2026, 10, 1, 0, 0), At(2026, 11, 1, 0, 0));
    REQUIRE(v.size() == 3);
    CHECK(v[2] == At(2026, 10, 7, 9, 30));

    MemoOccurrence occ;
    CHECK_FALSE(FindEventAtOrAfter(daily, At(2026, 10, 7, 9, 31), occ));
    //结束之后往前找，得到的是最后一次
    REQUIRE(FindEventAtOrBefore(daily, At(2026, 12, 1, 0, 0), occ));
    CHECK(occ.m_event == At(2026, 10, 7, 9, 30));
    CHECK(occ.m_index == 2);

    //每周选两天、提醒 3 次：第三次是第二周的周一
    MemoRuleSpec weekly = Rule(202610050800LL, kMemoRepeatWeekly, 3);
    weekly.m_weekdays = 0x1 | 0x4;
    v = Events(weekly, At(2026, 10, 1, 0, 0), At(2026, 12, 1, 0, 0));
    REQUIRE(v.size() == 3);
    CHECK(v[2] == At(2026, 10, 12, 8, 0));
    REQUIRE(FindEventAtOrBefore(weekly, At(2026, 11, 30, 0, 0), occ));
    CHECK(occ.m_event == At(2026, 10, 12, 8, 0));

    //每月提醒 2 次
    const MemoRuleSpec monthly = Rule(202601311800LL, kMemoRepeatMonthly, 2);
    REQUIRE(FindEventAtOrBefore(monthly, At(2027, 1, 1, 0, 0), occ));
    CHECK(occ.m_event == At(2026, 2, 28, 18, 0));
    CHECK_FALSE(FindEventAtOrAfter(monthly, At(2026, 3, 1, 0, 0), occ));
}

TEST_CASE("MEM-R4 提前提醒跨天", "[memo]")
{
    MemoRuleSpec rule = Rule(202610050010LL, kMemoRepeatNone, 1);
    rule.m_advance = 30;
    std::vector<MemoScheduleInput> memos(1, Input(kMemoA, rule, At(2026, 10, 1, 0, 0)));

    CMemoScheduler sched;
    LocalMinute wake = 0;
    REQUIRE(sched.NextWake(memos, At(2026, 10, 4, 20, 0), wake));
    CHECK(wake == At(2026, 10, 4, 23, 40));

    std::vector<MemoFire> fires;
    sched.Check(memos, At(2026, 10, 4, 23, 39), fires);
    CHECK(fires.empty());
    CHECK(sched.Check(memos, At(2026, 10, 4, 23, 40), fires));
    REQUIRE(fires.size() == 1);
    CHECK(fires[0].m_kind == kMemoFireDue);
    CHECK(fires[0].m_event == At(2026, 10, 5, 0, 10));
    CHECK(fires[0].m_remindAt == At(2026, 10, 4, 23, 40));
    CHECK_FALSE(sched.NextWake(memos, At(2026, 10, 4, 23, 40), wake));
}

TEST_CASE("MEM-R5 错过的提醒只补 24 小时内的，每条只补最近一次", "[memo]")
{
    //A：每天 9:00；B：不重复，2026-10-03 9:00
    std::vector<MemoScheduleInput> memos;
    memos.push_back(Input(kMemoA, Rule(202610010900LL, kMemoRepeatDaily, 0), At(2026, 9, 30, 0, 0)));
    memos.push_back(Input(kMemoB, Rule(202610030900LL, kMemoRepeatNone, 1), At(2026, 9, 30, 0, 0)));

    //电脑关了几天，2026-10-05 12:00 才开：A 只补 10-05 9:00 这一次；B 已过 24 小时，不补
    CMemoScheduler sched;
    std::vector<MemoFire> fires;
    CHECK(sched.Check(memos, At(2026, 10, 5, 12, 0), fires));
    REQUIRE(fires.size() == 1);
    CHECK(fires[0].m_memoid == kMemoA);
    CHECK(fires[0].m_kind == kMemoFireMissed);
    CHECK(fires[0].m_event == At(2026, 10, 5, 9, 0));

    //两条都已记录：再检查一次什么都不弹
    CHECK(sched.FiredRecords().size() == 2);
    CHECK_FALSE(sched.Check(memos, At(2026, 10, 5, 12, 1), fires));
    CHECK(fires.empty());

    //到点前 5 分钟内的仍算到点提醒，再晚就算错过
    CMemoScheduler late;
    late.Check(memos, At(2026, 10, 1, 9, 5), fires);
    REQUIRE(fires.size() == 1);
    CHECK(fires[0].m_kind == kMemoFireDue);
    CMemoScheduler later;
    later.Check(memos, At(2026, 10, 1, 9, 6), fires);
    REQUIRE(fires.size() == 1);
    CHECK(fires[0].m_kind == kMemoFireMissed);
}

TEST_CASE("MEM-R6 稍后提醒", "[memo]")
{
    std::vector<MemoScheduleInput> memos(1, Input(kMemoA, Rule(202610050930LL, kMemoRepeatNone, 1),
                                                  At(2026, 10, 1, 0, 0)));
    CMemoScheduler sched;
    std::vector<MemoFire> fires;
    sched.Check(memos, At(2026, 10, 5, 9, 30), fires);
    REQUIRE(fires.size() == 1);

    //点了「稍后提醒」：10 分钟后再来一次
    const LocalMinute until = At(2026, 10, 5, 9, 30) + kMemoSnoozeMinutes;
    sched.Snooze(kMemoA, fires[0].m_event, until);
    CHECK(sched.HasSnooze(kMemoA));
    LocalMinute wake = 0;
    REQUIRE(sched.NextWake(memos, At(2026, 10, 5, 9, 31), wake));
    CHECK(wake == until);

    sched.Check(memos, until - 1, fires);
    CHECK(fires.empty());
    sched.Check(memos, until, fires);
    REQUIRE(fires.size() == 1);
    CHECK(fires[0].m_snoozed);
    CHECK(fires[0].m_event == At(2026, 10, 5, 9, 30));
    CHECK_FALSE(sched.HasSnooze(kMemoA));
    sched.Check(memos, until + 1, fires);
    CHECK(fires.empty());

    //备忘被删除后，它的稍后提醒不再弹出
    sched.Snooze(kMemoA, At(2026, 10, 5, 9, 30), until + kMemoSnoozeMinutes);
    std::vector<MemoScheduleInput> none;
    sched.Prune(none);
    CHECK_FALSE(sched.HasSnooze(kMemoA));
    CHECK(sched.FiredRecords().empty());
}

TEST_CASE("MEM-R7 修改后重新计算", "[memo]")
{
    //每天 10:00，今天的已经提醒过
    std::vector<MemoScheduleInput> memos(1, Input(kMemoA, Rule(202610011000LL, kMemoRepeatDaily, 0),
                                                  At(2026, 9, 30, 0, 0)));
    CMemoScheduler sched;
    std::vector<MemoFire> fires;
    sched.Check(memos, At(2026, 10, 5, 10, 0), fires);
    REQUIRE(fires.size() == 1);

    //10:30 改成每天 11:00：今天 11:00 照常提醒
    memos[0].m_rule.m_start = 202610011100LL;
    memos[0].m_updatedAt = At(2026, 10, 5, 10, 30);
    LocalMinute wake = 0;
    REQUIRE(sched.NextWake(memos, At(2026, 10, 5, 10, 30), wake));
    CHECK(wake == At(2026, 10, 5, 11, 0));
    sched.Check(memos, At(2026, 10, 5, 10, 30), fires);
    CHECK(fires.empty());
    sched.Check(memos, At(2026, 10, 5, 11, 0), fires);
    REQUIRE(fires.size() == 1);
    CHECK(fires[0].m_event == At(2026, 10, 5, 11, 0));

    //新建一条开始时间已经过去的备忘：不立刻弹出，明天照常
    std::vector<MemoScheduleInput> fresh(1, Input(kMemoB, Rule(202610050900LL, kMemoRepeatDaily, 0),
                                                  At(2026, 10, 5, 9, 2)));
    sched.Check(fresh, At(2026, 10, 5, 9, 2), fires);
    CHECK(fires.empty());
    REQUIRE(sched.NextWake(fresh, At(2026, 10, 5, 9, 2), wake));
    CHECK(wake == At(2026, 10, 6, 9, 0));

    //修改时取消该备忘的稍后提醒
    sched.Snooze(kMemoA, At(2026, 10, 5, 11, 0), At(2026, 10, 5, 11, 10));
    sched.CancelSnooze(kMemoA);
    CHECK_FALSE(sched.HasSnooze(kMemoA));
}

TEST_CASE("MEM-R8 时间回拨与休眠唤醒不重复弹出", "[memo]")
{
    std::vector<MemoScheduleInput> memos(1, Input(kMemoA, Rule(202610010900LL, kMemoRepeatDaily, 0),
                                                  At(2026, 9, 30, 0, 0)));
    CMemoScheduler sched;
    std::vector<MemoFire> fires;
    sched.Check(memos, At(2026, 10, 5, 9, 0), fires);
    REQUIRE(fires.size() == 1);

    //系统时间被调回到 8:59，再走到 9:00：同一次事件不再弹出
    sched.Check(memos, At(2026, 10, 5, 8, 59), fires);
    CHECK(fires.empty());
    sched.Check(memos, At(2026, 10, 5, 9, 0), fires);
    CHECK(fires.empty());

    //调回到前一天 9:00：前一天那次事件早于已提醒记录，同样不弹
    sched.Check(memos, At(2026, 10, 4, 9, 0), fires);
    CHECK(fires.empty());

    //10-05 晚上休眠，10-06 9:00 期间一直睡着，10:30 唤醒：补一条错过的提醒，只补一次
    sched.Check(memos, At(2026, 10, 6, 10, 30), fires);
    REQUIRE(fires.size() == 1);
    CHECK(fires[0].m_kind == kMemoFireMissed);
    CHECK(fires[0].m_event == At(2026, 10, 6, 9, 0));
    sched.Check(memos, At(2026, 10, 6, 10, 31), fires);
    CHECK(fires.empty());

    //记录从配置读回后（模拟重启）同样不重复
    CMemoScheduler restarted;
    restarted.LoadFiredRecord(kMemoA, At(2026, 10, 6, 9, 0));
    restarted.Check(memos, At(2026, 10, 6, 10, 32), fires);
    CHECK(fires.empty());
}

TEST_CASE("MEM-R9 已提醒记录的读写", "[memo]")
{
    std::map<int64_t, LocalMinute> records;
    records[kMemoB] = At(2026, 10, 6, 9, 0);
    records[kMemoA] = At(2026, 10, 5, 9, 0);
    const std::string text = EncodeFiredRecords(records);
    std::map<int64_t, LocalMinute> back;
    DecodeFiredRecords(text, back);
    CHECK(back == records);

    //Windows 换行、空行、格式不对的行都跳过
    DecodeFiredRecords("101=29000000\r\n\nbad\n=5\n102=\n103=x1\n104=42\n", back);
    REQUIRE(back.size() == 2);
    CHECK(back[101] == 29000000);
    CHECK(back[104] == 42);
}
