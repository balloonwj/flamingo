/**
 *  MemoViewModelTests.cpp
 *
 *  「备忘与提醒」界面层纯逻辑、客户端备忘簿与报文编解码的测试。覆盖与用户确认过的界面用例
 *  MEM-U1～MEM-U6（docs/备忘与提醒方案.md 第 5 节）中不需要创建窗口的部分：
 *    MEM-U1 月历排法：第一列是周一，前后补上个月末、下个月初，今天、选中、有事件的日子各自标出；
 *    MEM-U2 翻月与回到今天（跨年）；
 *    MEM-U3 编辑对话框的默认值与校验；
 *    MEM-U4 群「备忘」页签按角色决定能不能新建，角标只计本次登录后新到的；
 *    MEM-U5 右下角提醒弹窗：多条合并、同一条只列一次、知道了、稍后提醒、错过的合并成一行；
 *    MEM-U6 全屏提醒落在主面板所在显示器（主面板不可见时落在主显示器），60 秒无操作转为右下角弹窗；
 *    MEM-U7 月历年、月下拉框：年份列表的范围，切到另一年月时选中日保持「几号」（2026-10-04 用户反馈）；
 *    MEM-U8 全屏提醒的文字（编辑对话框「预览效果」与真实提醒共用）：标题为空时的占位、时间与群名、备注原样；
 *    MEM-U9 编辑对话框预览：开始时间按 yyyymmddhhmm 换算后再显示，不存在的日期不预览。
 *  另含 MEM-C1～C3：报文编解码与备忘簿的增删改、按天列出事件。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口、不连网络。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <set>
#include <string>
#include <vector>

#include "../UI/BalloonMemo/MemoViewModel.h"
#include "../Feature/Memo/MemoBook.h"
#include "../net/MemoProtocolClient.h"

using namespace balloonmemo;
using memo::LocalMinute;
using memo::LocalMinuteFromParts;
using memo::DaysFromCivil;

namespace {

//本人、群主、一个群
const int64_t kSelf = 5308;
const int64_t kOwner = 5127;
const int64_t kGroup = 268435463LL;

//群角色：普通成员、管理员、群主，与 group_member_role 一致；不是群成员为 -1
const int kRoleMember = 0;
const int kRoleAdmin = 1;
const int kRoleOwner = 2;
const int kRoleNone = -1;

LocalMinute At(int y, int mo, int d, int h, int mi)
{
    return LocalMinuteFromParts(y, mo, d, h, mi);
}

net::MemoItem Memo(int64_t memoid, int ctype, int64_t start, int repeat)
{
    net::MemoItem m;
    m.m_memoid = memoid;
    m.m_ctype = ctype;
    m.m_groupid = (ctype == net::kMemoCtypeGroup) ? kGroup : 0;
    m.m_title = "t" + std::to_string(memoid);
    m.m_start = start;
    m.m_repeat = repeat;
    m.m_count = (repeat == 0) ? 1 : 0;
    m.m_creator = kOwner;
    m.m_editor = kOwner;
    return m;
}

//测试用的 Unix 毫秒 → 本地分钟换算：直接把毫秒当作分钟序号乘 60000 的结果，便于断言
LocalMinute FakeToLocal(int64_t unixMs)
{
    const int64_t kMillisPerMinute = 60000;
    return unixMs / kMillisPerMinute;
}

RECT MakeRect(int l, int t, int r, int b)
{
    RECT rc;
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    return rc;
}

} // namespace

TEST_CASE("MEM-U1 月历排法", "[memo]")
{
    //2026-10：1 号是周四，第一格是 9 月 28 日（周一）
    const int64_t today = DaysFromCivil(2026, 10, 3);
    const int64_t selected = DaysFromCivil(2026, 10, 5);
    std::set<int64_t> eventDays;
    eventDays.insert(DaysFromCivil(2026, 10, 5));
    eventDays.insert(DaysFromCivil(2026, 11, 2));

    std::vector<CalendarCell> cells;
    BuildMonthCells(2026, 10, today, selected, eventDays, cells);
    REQUIRE(cells.size() == static_cast<size_t>(kCalendarCells));
    CHECK(cells[0].m_day == DaysFromCivil(2026, 9, 28));
    CHECK(cells[0].m_dayOfMonth == 28);
    CHECK_FALSE(cells[0].m_inMonth);
    CHECK(cells[3].m_dayOfMonth == 1);
    CHECK(cells[3].m_inMonth);
    CHECK(cells[5].m_isToday);
    CHECK(cells[7].m_isSelected);
    CHECK(cells[7].m_hasEvent);
    CHECK(cells[33].m_dayOfMonth == 31);
    CHECK(cells[34].m_dayOfMonth == 1);
    CHECK_FALSE(cells[34].m_inMonth);
    CHECK(cells[35].m_hasEvent);   //11 月 2 日，下个月的格子上也标圆点

    //1 号恰好是周一：第一格就是 1 号（2026-06-01 是周一）
    BuildMonthCells(2026, 6, today, selected, eventDays, cells);
    CHECK(cells[0].m_dayOfMonth == 1);
    CHECK(cells[0].m_inMonth);

    int64_t firstDay = 0;
    int64_t endDay = 0;
    MonthGridRange(2026, 10, firstDay, endDay);
    CHECK(endDay - firstDay == kCalendarCells);
    CHECK(MonthTitle(2026, 10) == L"2026年10月");
}

TEST_CASE("MEM-U2 翻月与回到今天", "[memo]")
{
    int y = 2026;
    int m = 12;
    ShiftMonth(y, m, 1);
    CHECK(y == 2027);
    CHECK(m == 1);
    ShiftMonth(y, m, -1);
    CHECK(y == 2026);
    CHECK(m == 12);
    y = 2026;
    m = 1;
    ShiftMonth(y, m, -1);
    CHECK(y == 2025);
    CHECK(m == 12);

    //「今天」：回到今天所在的年月，今天既是今天也是选中
    int ty = 0;
    int tm = 0;
    int td = 0;
    const int64_t today = DaysFromCivil(2026, 10, 3);
    memo::CivilFromDays(today, ty, tm, td);
    std::vector<CalendarCell> cells;
    BuildMonthCells(ty, tm, today, today, std::set<int64_t>(), cells);
    int both = 0;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (cells[i].m_isToday && cells[i].m_isSelected)
            ++both;
    }
    CHECK(both == 1);
}

TEST_CASE("MEM-U3 编辑对话框的默认值与校验", "[memo]")
{
    const LocalMinute now = At(2026, 10, 3, 14, 20);
    const int64_t today = DaysFromCivil(2026, 10, 3);

    //今天：下一个整点；别的日子：9:00
    net::MemoItem d = DefaultMemoForDay(today, now, net::kMemoCtypePersonal, 0);
    CHECK(d.m_start == 202610031500LL);
    CHECK(d.m_repeat == 0);
    CHECK(d.m_count == 1);
    CHECK(d.m_advance == 0);
    CHECK(d.m_style == 1);
    d = DefaultMemoForDay(DaysFromCivil(2026, 10, 9), now, net::kMemoCtypeGroup, kGroup);
    CHECK(d.m_start == 202610090900LL);
    CHECK(d.m_groupid == kGroup);
    CHECK(DefaultMemoForDay(today, At(2026, 10, 3, 23, 30), 1, 0).m_start == 202610032359LL);

    //校验
    net::MemoItem ok = Memo(0, net::kMemoCtypePersonal, 202610051000LL, 0);
    CHECK(ValidateMemoEdit(L"周会", L"", ok) == kMemoEditOk);
    CHECK(ValidateMemoEdit(L" \x3000 ", L"", ok) == kMemoEditTitleEmpty);
    CHECK(ValidateMemoEdit(std::wstring(kMemoTitleMaxChars, L'字'), L"", ok) == kMemoEditOk);
    CHECK(ValidateMemoEdit(std::wstring(kMemoTitleMaxChars + 1, L'字'), L"", ok) == kMemoEditTitleTooLong);
    CHECK(ValidateMemoEdit(L"周会", std::wstring(kMemoNoteMaxChars, L'注') + L"\r\n", ok) == kMemoEditOk);
    CHECK(ValidateMemoEdit(L"周会", std::wstring(kMemoNoteMaxChars + 1, L'注'), ok) == kMemoEditNoteTooLong);

    net::MemoItem bad = ok;
    bad.m_start = 202602301000LL;
    CHECK(ValidateMemoEdit(L"周会", L"", bad) == kMemoEditStartInvalid);
    bad = ok;
    bad.m_advance = kMemoAdvanceMaxMinutes + 1;
    CHECK(ValidateMemoEdit(L"周会", L"", bad) == kMemoEditAdvanceInvalid);
    bad = ok;
    bad.m_repeat = 3;
    bad.m_weekdays = 0;
    CHECK(ValidateMemoEdit(L"周会", L"", bad) == kMemoEditWeekdaysEmpty);
    bad.m_weekdays = 1;
    bad.m_count = kMemoCountMax + 1;
    CHECK(ValidateMemoEdit(L"周会", L"", bad) == kMemoEditCountInvalid);
    bad = Memo(0, net::kMemoCtypeGroup, 202610051000LL, 0);
    CHECK(ValidateMemoEdit(L"周会", L"", bad) == kMemoEditTargetsEmpty);
    bad.m_targets.push_back(kSelf);
    CHECK(ValidateMemoEdit(L"周会", L"", bad) == kMemoEditOk);
    CHECK_FALSE(MemoEditErrorText(kMemoEditTitleEmpty).empty());
    CHECK(MemoEditErrorText(kMemoEditOk).empty());

    //服务端拒绝时的提示
    CHECK(MemoFailureText(312, false) == L"标题或备注包含敏感词，未保存");
    CHECK(MemoFailureText(303, true) == L"只有群主或管理员能删除群备忘");
    CHECK(MemoFailureText(1002, false).find(L"上限") != std::wstring::npos);
    CHECK(MemoFailureText(9, false) == L"保存失败（错误码 9）");

    //显示文字
    net::MemoItem w = Memo(1, 1, 202610051000LL, 3);
    w.m_weekdays = 0x1 | 0x4;
    CHECK(RepeatText(w) == L"每周一、三");
    w.m_count = 3;
    CHECK(RepeatText(w) == L"每周一、三，共 3 次");
    CHECK(RepeatText(Memo(1, 1, 202601311000LL, 4)) == L"每月 31 日");
    CHECK(RepeatText(Memo(1, 1, 202802291000LL, 5)) == L"每年 2 月 29 日");
    CHECK(RepeatText(Memo(1, 1, 202610051000LL, 0)) == L"不重复");
    CHECK(AdvanceText(0) == L"到点提醒");
    CHECK(AdvanceText(10) == L"提前 10 分钟");
    CHECK(AdvanceText(120) == L"提前 2 小时");
    CHECK(AdvanceText(1440) == L"提前 1 天");
    CHECK(EventTimeText(At(2026, 10, 3, 9, 5), now) == L"今天 09:05");
    CHECK(EventTimeText(At(2026, 10, 5, 9, 30), now) == L"10月5日 09:30");
    CHECK(EventTimeText(At(2027, 1, 2, 8, 0), now) == L"2027年1月2日 08:00");
}

TEST_CASE("MEM-U4 群备忘按角色决定能不能新建，角标只计新到的", "[memo]")
{
    CHECK(CanEditGroupMemo(kRoleOwner));
    CHECK(CanEditGroupMemo(kRoleAdmin));
    CHECK_FALSE(CanEditGroupMemo(kRoleMember));
    CHECK_FALSE(CanEditGroupMemo(kRoleNone));

    memo::CMemoBook book;
    std::vector<net::MemoItem> initial;
    net::MemoItem old = Memo(1, net::kMemoCtypeGroup, 202610051000LL, 0);
    old.m_targets.push_back(kSelf);
    initial.push_back(old);
    book.ReplaceAll(initial);
    CHECK(book.UnseenCount(kGroup) == 0);   //登录时全量拉回的不算新到

    //别人新建、提醒本人的：计入
    net::MemoItem fresh = Memo(2, net::kMemoCtypeGroup, 202610061000LL, 0);
    fresh.m_targetAll = true;
    CHECK_FALSE(book.ApplyUpsert(fresh, kSelf));
    CHECK(book.UnseenCount(kGroup) == 1);

    //修改已有的一条：不再计入
    old.m_title = "changed";
    CHECK(book.ApplyUpsert(old, kSelf));
    CHECK(book.UnseenCount(kGroup) == 1);

    //本人新建的：不计入
    net::MemoItem mine = Memo(3, net::kMemoCtypeGroup, 202610071000LL, 0);
    mine.m_editor = kSelf;
    mine.m_targets.push_back(kSelf);
    book.ApplyUpsert(mine, kSelf);
    CHECK(book.UnseenCount(kGroup) == 1);

    //点开页签后清零；被删除的那条从角标里去掉
    net::MemoItem another = Memo(4, net::kMemoCtypeGroup, 202610081000LL, 0);
    another.m_targets.push_back(kSelf);
    book.ApplyUpsert(another, kSelf);
    CHECK(book.UnseenCount(kGroup) == 2);
    CHECK(book.ApplyDelete(4));
    CHECK(book.UnseenCount(kGroup) == 1);
    book.MarkGroupSeen(kGroup);
    CHECK(book.UnseenCount(kGroup) == 0);
}

TEST_CASE("MEM-U5 右下角提醒弹窗的合并、知道了、稍后提醒", "[memo]")
{
    CReminderQueue queue;
    std::vector<memo::MemoFire> fires;
    memo::MemoFire a;
    a.m_memoid = 1;
    a.m_event = At(2026, 10, 5, 9, 0);
    a.m_kind = memo::kMemoFireDue;
    memo::MemoFire b = a;
    b.m_memoid = 2;
    memo::MemoFire missed1 = a;
    missed1.m_memoid = 3;
    missed1.m_kind = memo::kMemoFireMissed;
    memo::MemoFire missed2 = missed1;
    missed2.m_memoid = 4;
    fires.push_back(a);
    fires.push_back(b);
    fires.push_back(missed1);
    fires.push_back(missed2);

    CHECK(queue.Add(fires));
    CHECK(queue.DueEntries().size() == 2);
    CHECK(queue.MissedEntries().size() == 2);

    //同一条的同一次事件再来一遍：不重复列出
    CHECK_FALSE(queue.Add(std::vector<memo::MemoFire>(1, a)));
    CHECK(queue.DueEntries().size() == 2);

    //同一条的新一次事件：替换旧的那一行
    memo::MemoFire a2 = a;
    a2.m_event = At(2026, 10, 6, 9, 0);
    CHECK(queue.Add(std::vector<memo::MemoFire>(1, a2)));
    REQUIRE(queue.DueEntries().size() == 2);
    CHECK(queue.DueEntries()[0].m_event == At(2026, 10, 6, 9, 0));

    //知道了
    CHECK(queue.Dismiss(2));
    CHECK_FALSE(queue.Dismiss(2));
    CHECK(queue.DueEntries().size() == 1);

    //稍后提醒：拿出这次事件交给调度器
    LocalMinute event = 0;
    CHECK(queue.TakeForSnooze(1, event));
    CHECK(event == At(2026, 10, 6, 9, 0));
    CHECK(queue.DueEntries().empty());

    //错过的那一行「知道了」：全部错过的一起移除，队列空了窗口就关
    CHECK_FALSE(queue.IsEmpty());
    queue.DismissMissed();
    CHECK(queue.IsEmpty());

    //备忘被删除时从队列里拿掉
    queue.AddOne(9, At(2026, 10, 5, 9, 0), false);
    queue.RemoveMemo(9);
    CHECK(queue.IsEmpty());
}

TEST_CASE("MEM-U6 全屏提醒的显示器与 60 秒转弹窗", "[memo]")
{
    const RECT primary = MakeRect(0, 0, 1920, 1080);
    const RECT second = MakeRect(1920, 0, 4480, 1440);
    RECT rc = ChooseFullscreenMonitor(true, second, primary);
    CHECK(rc.left == 1920);
    CHECK(rc.right == 4480);
    rc = ChooseFullscreenMonitor(false, second, primary);
    CHECK(rc.left == 0);
    CHECK(rc.right == 1920);

    CFullscreenTimeout timeout;
    CHECK_FALSE(timeout.ShouldFallback(1000));
    timeout.Start(1000);
    CHECK_FALSE(timeout.ShouldFallback(1000 + kFullscreenFallbackMs - 1));
    CHECK(timeout.RemainingMs(1000 + kFullscreenFallbackMs - 1) == 1);
    //中途动了一下鼠标：重新计时
    timeout.OnUserActivity(30000);
    CHECK_FALSE(timeout.ShouldFallback(1000 + kFullscreenFallbackMs));
    CHECK(timeout.ShouldFallback(30000 + kFullscreenFallbackMs));
    CHECK(timeout.RemainingMs(30000 + kFullscreenFallbackMs) == 0);

    //GetTickCount 回绕
    CFullscreenTimeout wrap;
    const DWORD kNearWrap = 0xFFFFF000UL;
    wrap.Start(kNearWrap);
    CHECK_FALSE(wrap.ShouldFallback(kNearWrap + 0x2000UL));
    CHECK(wrap.ShouldFallback(kNearWrap + kFullscreenFallbackMs));
}

TEST_CASE("MEM-C1 报文编解码", "[memo]")
{
    net::MemoItem m = Memo(0, net::kMemoCtypeGroup, 202610050930LL, 3);
    m.m_title = "\xE5\x91\xA8\xE4\xBC\x9A";   //「周会」的 UTF-8
    m.m_note = "line1\nline2";
    m.m_weekdays = 1;
    m.m_advance = 10;
    m.m_style = 2;
    m.m_targets.push_back(kSelf);
    const std::string req = net::MemoProtocolClient::buildSaveRequest(m);
    CHECK(req.find("\"start\":\"2026-10-05 09:30\"") != std::string::npos);
    CHECK(req.find("\"groupid\":\"268435463\"") != std::string::npos);
    CHECK(req.find("\"targets\":[\"5308\"]") != std::string::npos);
    CHECK(req.find("creator") == std::string::npos);

    CHECK(net::MemoProtocolClient::buildListRequest(0) == "{}");
    CHECK(net::MemoProtocolClient::buildDeleteRequest(12) == "{\"memoid\":\"12\"}");

    const std::string resp =
        "{\"code\":0,\"msg\":\"ok\",\"memo\":{\"memoid\":\"12\",\"ctype\":2,\"groupid\":\"268435463\","
        "\"creator\":\"5127\",\"editor\":\"5127\",\"title\":\"t\",\"note\":\"\",\"start\":\"2026-10-05 09:30\","
        "\"advance\":10,\"repeat\":3,\"weekdays\":1,\"count\":0,\"style\":1,\"targetall\":false,"
        "\"targets\":[\"5308\"],\"createtime\":1790000000000,\"updatetime\":1790000000001}}";
    net::MemoSaveResult saved;
    REQUIRE(net::MemoProtocolClient::parseSaveResponse(resp, saved));
    CHECK(saved.m_code == 0);
    REQUIRE(saved.m_hasMemo);
    CHECK(saved.m_memo.m_memoid == 12);
    CHECK(saved.m_memo.m_groupid == kGroup);
    CHECK(saved.m_memo.m_start == 202610050930LL);
    CHECK(saved.m_memo.m_targets.size() == 1);
    CHECK(saved.m_memo.m_updateTime == 1790000000001LL);

    net::MemoSaveResult failed;
    REQUIRE(net::MemoProtocolClient::parseSaveResponse("{\"code\":1001,\"msg\":\"invalid memo\"}", failed));
    CHECK(failed.m_code == 1001);
    CHECK_FALSE(failed.m_hasMemo);

    net::MemoNotify n;
    REQUIRE(net::MemoProtocolClient::parseNotify("{\"changetype\":2,\"memoid\":\"12\"}", n));
    CHECK(n.m_changeType == 2);
    CHECK(n.m_memoid == 12);
    CHECK_FALSE(net::MemoProtocolClient::parseNotify("{\"changetype\":3,\"memoid\":\"12\"}", n));
    CHECK_FALSE(net::MemoProtocolClient::parseNotify("{\"changetype\":1}", n));

    net::MemoListResult list;
    REQUIRE(net::MemoProtocolClient::parseListResponse(
        "{\"code\":0,\"msg\":\"ok\",\"memos\":[{\"memoid\":\"1\",\"start\":\"2026-10-05 09:30\"},{\"memoid\":\"0\"}]}", list));
    CHECK(list.m_memos.size() == 1);
}

TEST_CASE("MEM-C2 备忘簿：按天列出事件与有事件的日子", "[memo]")
{
    memo::CMemoBook book;
    CHECK_FALSE(book.IsLoaded());
    std::vector<net::MemoItem> items;
    items.push_back(Memo(1, 1, 202610051000LL, 1));   //每天 10:00
    items.push_back(Memo(2, 1, 202610050830LL, 0));   //10-05 8:30 一次
    net::MemoItem g = Memo(3, net::kMemoCtypeGroup, 202610070900LL, 0);
    g.m_targetAll = true;
    items.push_back(g);
    book.ReplaceAll(items);
    CHECK(book.IsLoaded());

    std::vector<memo::MemoDayEntry> day;
    book.ListDayEntries(DaysFromCivil(2026, 10, 5), day);
    REQUIRE(day.size() == 2);
    CHECK(day[0].m_memoid == 2);
    CHECK(day[1].m_memoid == 1);
    book.ListDayEntries(DaysFromCivil(2026, 10, 4), day);
    CHECK(day.empty());

    std::set<int64_t> days;
    book.CollectEventDays(DaysFromCivil(2026, 10, 1), DaysFromCivil(2026, 10, 8), days);
    CHECK(days.size() == 3);   //5、6、7 日
    CHECK(days.count(DaysFromCivil(2026, 10, 4)) == 0);

    std::vector<net::MemoItem> group;
    book.ListGroup(kGroup, group);
    REQUIRE(group.size() == 1);
    CHECK(group[0].m_memoid == 3);

    std::vector<memo::MemoScheduleInput> inputs;
    book.BuildScheduleInputs(FakeToLocal, inputs);
    CHECK(inputs.size() == 3);

    book.Clear();
    CHECK(book.Items().empty());
    CHECK_FALSE(book.IsLoaded());
}

TEST_CASE("MEM-C3 没提醒到本人的群备忘不进本人的日历与提醒", "[memo]")
{
    //本人是群主：群页签里看得到三条，日历与提醒只取提醒全体的、指定了本人的两条
    memo::CMemoBook book;
    book.SetSelfId(kOwner);
    std::vector<net::MemoItem> items;
    net::MemoItem all = Memo(1, net::kMemoCtypeGroup, 202610051000LL, 0);
    all.m_targetAll = true;
    net::MemoItem toSelf = Memo(2, net::kMemoCtypeGroup, 202610051100LL, 0);
    toSelf.m_targets.push_back(kOwner);
    net::MemoItem toOther = Memo(3, net::kMemoCtypeGroup, 202610051200LL, 0);
    toOther.m_targets.push_back(kSelf);
    items.push_back(all);
    items.push_back(toSelf);
    items.push_back(toOther);
    book.ReplaceAll(items);

    std::vector<net::MemoItem> group;
    book.ListGroup(kGroup, group);
    CHECK(group.size() == 3);
    CHECK_FALSE(book.IsRemindedToSelf(toOther));

    std::vector<memo::MemoDayEntry> day;
    book.ListDayEntries(DaysFromCivil(2026, 10, 5), day);
    REQUIRE(day.size() == 2);
    CHECK(day[0].m_memoid == 1);
    CHECK(day[1].m_memoid == 2);

    std::vector<memo::MemoScheduleInput> inputs;
    book.BuildScheduleInputs(FakeToLocal, inputs);
    CHECK(inputs.size() == 2);

    //个人备忘总是提醒到本人
    net::MemoItem personal = Memo(4, net::kMemoCtypePersonal, 202610051300LL, 0);
    CHECK(book.IsRemindedToSelf(personal));
}

TEST_CASE("MEM-U7 月历年月下拉框的年份范围与切换年月后的选中日", "[memo]")
{
    //今年往前 5 年、往后 10 年
    int first = 0;
    int last = 0;
    CalendarYearRange(2026, 2026, first, last);
    CHECK(first == 2026 - kCalendarYearsBack);
    CHECK(last == 2026 + kCalendarYearsAhead);
    //用「‹」「›」翻出了范围：把显示的年份也包进来
    CalendarYearRange(2026, 2040, first, last);
    CHECK(first == 2021);
    CHECK(last == 2040);
    CalendarYearRange(2026, 2018, first, last);
    CHECK(first == 2018);
    CHECK(last == 2036);
    //收在上下限之内
    CalendarYearRange(2095, 2095, first, last);
    CHECK(last == kCalendarYearMax);
    CalendarYearRange(2002, 2002, first, last);
    CHECK(first == kCalendarYearMin);

    //保持「几号」；那个月没有这一天时取最后一天
    const int64_t oct31 = DaysFromCivil(2026, 10, 31);
    CHECK(KeepDayOfMonth(2026, 11, oct31) == DaysFromCivil(2026, 11, 30));
    CHECK(KeepDayOfMonth(2027, 2, oct31) == DaysFromCivil(2027, 2, 28));
    CHECK(KeepDayOfMonth(2028, 2, oct31) == DaysFromCivil(2028, 2, 29));
    CHECK(KeepDayOfMonth(2030, 1, DaysFromCivil(2026, 10, 8)) == DaysFromCivil(2030, 1, 8));
}

TEST_CASE("MEM-U8 全屏提醒的文字：标题占位、时间与群名、备注原样（预览与真实提醒共用）", "[memo][memo-preview]")
{
    const LocalMinute now = At(2026, 10, 4, 10, 0);
    //个人备忘：时间与 EventTimeText 相同，后面不拼群名；标题与备注原样
    const FullscreenReminderText personal =
        BuildFullscreenReminderText(L"交周报", L"第一行\r\n第二行", At(2026, 10, 4, 17, 30), now, L"");
    CHECK(personal.m_title == L"交周报");
    CHECK(personal.m_time == EventTimeText(At(2026, 10, 4, 17, 30), now));
    CHECK(personal.m_time == L"今天 17:30");
    CHECK(personal.m_note == L"第一行\r\n第二行");

    //群备忘：时间后面拼「 · 群：群名」
    const FullscreenReminderText group =
        BuildFullscreenReminderText(L"周会", L"", At(2026, 10, 6, 9, 30), now, L"研发部");
    CHECK(group.m_time == L"10月6日 09:30 · 群：研发部");
    CHECK(group.m_note.empty());

    //标题为空或全是空白：显示占位文字（编辑对话框里还没填标题就点「预览效果」）
    CHECK(BuildFullscreenReminderText(L"", L"", now, now, L"").m_title == L"（未填写标题）");
    CHECK(BuildFullscreenReminderText(L"  \t ", L"", now, now, L"").m_title == L"（未填写标题）");
    //标题前后有空白但有内容：原样显示，不去空白（与保存后显示的一致）
    CHECK(BuildFullscreenReminderText(L" 周会 ", L"", now, now, L"").m_title == L" 周会 ");
}

TEST_CASE("MEM-U9 编辑对话框预览：开始时间按 yyyymmddhhmm 换算后再显示，不存在的日期不预览", "[memo][memo-preview]")
{
    //2026-10-04 实机核对发现：把 yyyymmddhhmm 直接当本地分钟序号用，时间一行显示成「387197年5月24日 17:40」
    const LocalMinute now = At(2026, 10, 4, 10, 0);
    FullscreenReminderText text;
    REQUIRE(BuildPreviewReminderText(L"提交本周周报", L"别忘了附上数据", 202610040100LL, now, L"", text));
    CHECK(text.m_title == L"提交本周周报");
    CHECK(text.m_time == L"今天 01:00");
    CHECK(text.m_note == L"别忘了附上数据");

    //跨年、群备忘
    REQUIRE(BuildPreviewReminderText(L"年会", L"", 202701150930LL, now, L"研发部", text));
    CHECK(text.m_time == L"2027年1月15日 09:30 · 群：研发部");

    //不存在的日期（2 月 30 日）：返回 false，出参不改动
    FullscreenReminderText kept;
    kept.m_title = L"原样";
    CHECK_FALSE(BuildPreviewReminderText(L"周会", L"", 202702300900LL, now, L"", kept));
    CHECK(kept.m_title == L"原样");
}
