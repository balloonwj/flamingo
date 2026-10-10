/**
 *  聊天记录管理器时间轴模式的单元测试（Suite TL），HistoryTimelineTests.cpp
 *
 *  覆盖 docs/聊天记录管理器方案.md §11.7 的数据与逻辑部分：
 *    TL1 BuildTimelineYears：按年分组、年月倒序、非法键忽略、只在命中里出现的月份；
 *    TL2 消息量条长度与满格值；
 *    TL3 星期几（公历、闰年、非法日期）与日期文字；
 *    TL4 本地月份起止与年月键（含十二月跨年）；
 *    TL5 HistoryHitNavigator：上一处 / 下一处循环、设当前处、按 msgid 查找、从某位置起找第一处；
 *    TL6 三条新 SQL 的拼装：分组表达式、关键字占位、区间与条数限制；
 *    TL7 本地库：按月、按天统计（按本地时间分段，撤回的不计），命中位置的排序与条数限制；
 *    TL8 HistoryModel 关键字只高亮：列表保留全部消息、页号换算与跳到某天不受关键字影响、切换时的页号；
 *    TL9 F3 / Shift+F3 快捷键；
 *    TL10 各月条数改为按区间统计后与逐行换算本地时间的写法结果一致，跨年、月份多时分批；
 *    TL11 后台查询线程执行时间轴统计（kV2SearchTimelineStats）的结果与直接查本地库一致。
 *  框架 Catch2 v3。本地库用临时 db 文件跑真实 SQLite。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <time.h>
#include <string>
#include <vector>
#include "sqlite3.h"
#include "../UI/BalloonHistory/HistoryTimeline.h"
#include "../UI/BalloonHistory/HistoryWildcard.h"   // MakeSearchLikePattern：TL10 参照查询绑定关键字
#include "../UI/BalloonHistory/HistoryModel.h"
#include "../UI/BalloonHistory/HistoryWindowKeys.h"
#include "../UI/BalloonHistory/V2MsgSearchWorker.h"   // TL11：后台查询执行时间轴统计
#include <memory>
#include "../UI/BalloonHistory/V2MsgStore.h"

using namespace balloonhistory;

namespace {

//会话与消息类型（与 net/Protocol.h 一致）
const int kSingle = 1;
const int kPeer = 200;
const int kTypeText = 1;
//统计用的「当前时间」：本组数据只用全部时间，取一个足够晚的值
const int64_t kNowMs = 4102444800000LL;   // 2100-01-01

//本地时间 → Unix 毫秒
int64_t LocalMs(int year, int month, int day, int hour, int minute)
{
    const int kTmYearBase = 1900;
    const int64_t kMsPerSecond = 1000;
    struct tm t = { 0 };
    t.tm_year = year - kTmYearBase;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_isdst = -1;
    return (int64_t)_mktime64(&t) * kMsPerSecond;
}

//取一个临时目录下的 db 全路径，并先删掉同名旧文件与 WAL 残留（文件名带进程号）
std::wstring FreshTimelineDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//一条文字消息
HistoryRecord TextMsg(int64_t msgid, int64_t ts, const std::string& text)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = kSingle;
    rec.m_cid = kPeer;
    rec.m_fromid = kPeer;
    rec.m_msgtype = kTypeText;
    rec.m_timestamp = ts;
    rec.m_searchText = text;
    rec.m_content = "{\"text\":\"" + text + "\"}";
    return rec;
}

//本会话的默认筛选条件
HistoryFilter ConvFilter()
{
    HistoryFilter f;
    f.m_ctype = kSingle;
    f.m_cid = kPeer;
    return f;
}

/**
 *  测试数据（单聊 200，本地时间）：
 *    1  2025-12-31 23:30 「季度报告初稿」
 *    2  2026-01-01 00:10 「新年快乐」
 *    3  2026-01-01 09:00 「季度报告定稿」
 *    4  2026-01-15 12:00 「午饭」
 *    5  2026-03-02 08:00 「季度工作报告」
 *    6  2026-03-02 08:00 「同一毫秒的另一条」（与 5 同一时间，msgid 更大）
 *    7  2026-03-05 10:00 「已撤回的报告」（撤回）
 */
void SeedTimeline(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(TextMsg(1, LocalMs(2025, 12, 31, 23, 30), "季度报告初稿")));
    REQUIRE(store.Upsert(TextMsg(2, LocalMs(2026, 1, 1, 0, 10), "新年快乐")));
    REQUIRE(store.Upsert(TextMsg(3, LocalMs(2026, 1, 1, 9, 0), "季度报告定稿")));
    REQUIRE(store.Upsert(TextMsg(4, LocalMs(2026, 1, 15, 12, 0), "午饭")));
    REQUIRE(store.Upsert(TextMsg(5, LocalMs(2026, 3, 2, 8, 0), "季度工作报告")));
    REQUIRE(store.Upsert(TextMsg(6, LocalMs(2026, 3, 2, 8, 0), "同一毫秒的另一条")));
    REQUIRE(store.Upsert(TextMsg(7, LocalMs(2026, 3, 5, 10, 0), "已撤回的报告")));
    REQUIRE(store.MarkRecalled(kSingle, kPeer, 7));
}

//把分段统计写成 "键:条数,键:条数" 便于整体比较
std::string PeriodText(const std::vector<HistoryPeriodCount>& counts)
{
    std::string s;
    for (size_t i = 0; i < counts.size(); ++i)
    {
        if (!s.empty())
        {
            s += ",";
        }
        s += std::to_string((long long)counts[i].m_key) + ":" + std::to_string((long long)counts[i].m_count);
    }
    return s;
}

//把命中位置写成 msgid 序列
std::string PositionIds(const std::vector<HistoryPosition>& positions)
{
    std::string s;
    for (size_t i = 0; i < positions.size(); ++i)
    {
        if (!s.empty())
        {
            s += ",";
        }
        s += std::to_string((long long)positions[i].m_msgid);
    }
    return s;
}

} // namespace

TEST_CASE("TL1 导航数据：按年分组、年月倒序、非法键忽略、只在命中里出现的月份也列出", "[timeline]")
{
    std::vector<HistoryPeriodCount> counts;
    counts.push_back(HistoryPeriodCount(202601, 3));
    counts.push_back(HistoryPeriodCount(202512, 1));
    counts.push_back(HistoryPeriodCount(202603, 2));
    counts.push_back(HistoryPeriodCount(202613, 9));   //月份越界，忽略
    counts.push_back(HistoryPeriodCount(0, 9));        //空键，忽略
    std::vector<HistoryPeriodCount> hits;
    hits.push_back(HistoryPeriodCount(202601, 1));
    hits.push_back(HistoryPeriodCount(202512, 1));
    hits.push_back(HistoryPeriodCount(202410, 2));     //总数里没有这个月：按命中数补上

    const std::vector<HistoryTimelineYear> years = BuildTimelineYears(counts, hits);
    REQUIRE(years.size() == 3);
    CHECK(years[0].m_year == 2026);
    CHECK(years[0].m_count == 5);
    CHECK(years[0].m_hits == 1);
    REQUIRE(years[0].m_months.size() == 2);
    CHECK(years[0].m_months[0].m_month == 3);
    CHECK(years[0].m_months[0].m_count == 2);
    CHECK(years[0].m_months[0].m_hits == 0);
    CHECK(years[0].m_months[1].m_month == 1);
    CHECK(years[0].m_months[1].m_hits == 1);
    CHECK(years[1].m_year == 2025);
    CHECK(years[1].m_months[0].m_month == 12);
    CHECK(years[2].m_year == 2024);
    CHECK(years[2].m_months[0].m_count == 2);
    CHECK(years[2].m_months[0].m_hits == 2);

    //默认展开：当前月份所在的年；不知道当前月份或那一年不在数据里时取最近的一年
    CHECK(DefaultExpandedYear(years, 202512) == 2025);
    CHECK(DefaultExpandedYear(years, 0) == 2026);
    CHECK(DefaultExpandedYear(years, 199901) == 2026);
    CHECK(DefaultExpandedYear(std::vector<HistoryTimelineYear>(), 202601) == 0);

    //没有任何数据
    CHECK(BuildTimelineYears(std::vector<HistoryPeriodCount>(), std::vector<HistoryPeriodCount>()).empty());

    //分段统计里查某个键
    CHECK(PeriodCountOf(counts, 202603) == 2);
    CHECK(PeriodCountOf(counts, 202602) == 0);
}

TEST_CASE("TL2 消息量条：按最大月份归一，有消息时不短于最短长度，没有消息时为 0", "[timeline]")
{
    std::vector<HistoryPeriodCount> counts;
    counts.push_back(HistoryPeriodCount(202601, 1));
    counts.push_back(HistoryPeriodCount(202602, 200));
    counts.push_back(HistoryPeriodCount(202503, 50));
    const std::vector<HistoryTimelineYear> years = BuildTimelineYears(counts, std::vector<HistoryPeriodCount>());
    const int64_t maxCount = TimelineMaxMonthCount(years);
    CHECK(maxCount == 200);
    CHECK(TimelineBarPercent(200, maxCount) == 100);
    CHECK(TimelineBarPercent(50, maxCount) == 25);
    CHECK(TimelineBarPercent(1, maxCount) == kTimelineBarMinPercent);
    CHECK(TimelineBarPercent(0, maxCount) == 0);
    CHECK(TimelineBarPercent(5, 0) == 0);
    CHECK(TimelineBarPercent(300, maxCount) == 100);
    CHECK(TimelineMaxMonthCount(std::vector<HistoryTimelineYear>()) == 0);
}

TEST_CASE("TL3 星期几与日期文字：公历、闰年、非法日期", "[timeline]")
{
    CHECK(WeekdayOfDayKey(20261001) == 4);   //星期四（布局图里的例子）
    CHECK(WeekdayOfDayKey(20260930) == 3);
    CHECK(WeekdayOfDayKey(19700101) == 4);
    CHECK(WeekdayOfDayKey(20240101) == 1);
    CHECK(WeekdayOfDayKey(20000229) == 2);   //闰年的 2 月 29 日
    CHECK(WeekdayOfDayKey(20240303) == 0);   //星期日
    CHECK(WeekdayOfDayKey(20260229) == -1);  //平年没有 2 月 29 日
    CHECK(WeekdayOfDayKey(19000229) == -1);  //整百年不是闰年
    CHECK(WeekdayOfDayKey(20261301) == -1);
    CHECK(WeekdayOfDayKey(20261000) == -1);
    CHECK(WeekdayOfDayKey(0) == -1);

    CHECK(FormatDayKey(20261001) == "2026-10-01");
    CHECK(FormatDayKey(20250105) == "2025-01-05");
    CHECK(FormatDayKey(0).empty());
}

TEST_CASE("TL4 本地月份起止与年月键：十二月跨到下一年，起止两端都属于这个月", "[timeline]")
{
    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(LocalMonthBoundsMs(2026, 3, start, end));
    CHECK(start == LocalMs(2026, 3, 1, 0, 0));
    CHECK(end + 1 == LocalMs(2026, 4, 1, 0, 0));
    CHECK(LocalMonthKeyOf(start) == 202603);
    CHECK(LocalMonthKeyOf(end) == 202603);
    CHECK(LocalMonthKeyOf(end + 1) == 202604);

    REQUIRE(LocalMonthBoundsMs(2025, 12, start, end));
    CHECK(start == LocalMs(2025, 12, 1, 0, 0));
    CHECK(end + 1 == LocalMs(2026, 1, 1, 0, 0));
    CHECK(LocalMonthKeyOf(end) == 202512);

    CHECK_FALSE(LocalMonthBoundsMs(2026, 0, start, end));
    CHECK(start == 0);
    CHECK_FALSE(LocalMonthBoundsMs(2026, 13, start, end));
    CHECK(LocalMonthKeyOf(0) == 0);
}

TEST_CASE("TL5 命中跳转：下一处、上一处循环，设当前处，按 msgid 查找，从某位置起找第一处", "[timeline]")
{
    HistoryHitNavigator nav;
    bool wrapped = true;
    CHECK(nav.Next(wrapped) == -1);
    CHECK_FALSE(wrapped);
    CHECK(nav.Prev(wrapped) == -1);
    CHECK(nav.Current() == -1);
    CHECK(nav.At(0) == NULL);

    //三处命中，最新在前
    std::vector<HistoryPosition> hits;
    hits.push_back(HistoryPosition(3000, 30));
    hits.push_back(HistoryPosition(2000, 21));
    hits.push_back(HistoryPosition(2000, 20));
    nav.Reset(hits, true);
    CHECK(nav.Count() == 3);
    CHECK(nav.Truncated());
    CHECK(nav.Current() == -1);

    //下一处：从第一处开始，到最后一处之后绕回
    CHECK(nav.Next(wrapped) == 0);
    CHECK_FALSE(wrapped);
    CHECK(nav.Next(wrapped) == 1);
    CHECK(nav.Next(wrapped) == 2);
    CHECK(nav.Next(wrapped) == 0);
    CHECK(wrapped);

    //上一处：在第一处时绕回最后一处
    CHECK(nav.Prev(wrapped) == 2);
    CHECK(wrapped);
    CHECK(nav.Prev(wrapped) == 1);
    CHECK_FALSE(wrapped);
    REQUIRE(nav.At(1) != NULL);
    CHECK(nav.At(1)->m_msgid == 21);

    //还没有当前处时，上一处取最后一处
    nav.Reset(hits, false);
    CHECK_FALSE(nav.Truncated());
    CHECK(nav.Prev(wrapped) == 2);
    CHECK_FALSE(wrapped);

    //设当前处
    CHECK(nav.SetCurrent(1));
    CHECK(nav.Current() == 1);
    CHECK_FALSE(nav.SetCurrent(3));
    CHECK_FALSE(nav.SetCurrent(-1));
    CHECK(nav.Current() == 1);

    //按 msgid 查找
    CHECK(nav.IndexOfMsgid(20) == 2);
    CHECK(nav.IndexOfMsgid(99) == -1);
    CHECK(nav.Contains(30));
    CHECK_FALSE(nav.Contains(31));

    //从某个位置起往下找第一处：起点本身是命中、起点在两处之间、起点在最后一处之后
    CHECK(nav.FirstAtOrAfter(3000, 30) == 0);
    CHECK(nav.FirstAtOrAfter(2500, 1) == 1);
    CHECK(nav.FirstAtOrAfter(2000, 20) == 2);
    CHECK(nav.FirstAtOrAfter(2000, 19) == -1);
    CHECK(nav.FirstAtOrAfter(9999, 1) == 0);

    nav.Clear();
    CHECK(nav.Count() == 0);
    CHECK_FALSE(nav.Contains(30));
}

TEST_CASE("TL6 时间轴三条 SQL：按本地时间分组、关键字占位、时间区间与条数限制", "[timeline]")
{
    HistoryFilter f = ConvFilter();
    std::string sql = BuildMonthCountSql(f, kNowMs);
    CHECK(sql.find("strftime('%Y%m', timestamp/1000, 'unixepoch', 'localtime')") != std::string::npos);
    CHECK(sql.find("GROUP BY k ORDER BY k DESC") != std::string::npos);
    CHECK(sql.find("recalled=0") != std::string::npos);
    CHECK(sql.find("LIKE ?") == std::string::npos);

    f.m_keyword = "报告";
    sql = BuildMonthCountSql(f, kNowMs);
    CHECK(sql.find("search_text LIKE ? ESCAPE '\\'") != std::string::npos);

    sql = BuildDayCountSql(f, kNowMs, 100, 200);
    CHECK(sql.find("strftime('%Y%m%d'") != std::string::npos);
    CHECK(sql.find("timestamp>=100 AND timestamp<=200") != std::string::npos);
    CHECK(sql.find("LIKE ?") != std::string::npos);

    sql = BuildPositionsSql(f, kNowMs, 2000);
    CHECK(sql.find("SELECT timestamp, msgid FROM") == 0);
    CHECK(sql.find("ORDER BY timestamp DESC, msgid DESC LIMIT 2000") != std::string::npos);
    CHECK(BuildPositionsSql(f, kNowMs, 0).find("LIMIT 1") != std::string::npos);
}

TEST_CASE("TL7 本地库：按本地时间分月、分天统计，撤回的不计；命中位置的排序与条数限制", "[timeline]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshTimelineDb(L"tl7.db").c_str()));
    SeedTimeline(store);

    //按月：12 月 31 日 23:30 与 1 月 1 日 00:10 分属两年（按本地时间分段）
    HistoryFilter f = ConvFilter();
    std::vector<HistoryPeriodCount> months;
    REQUIRE(store.CountByLocalMonth(f, kNowMs, months));
    CHECK(PeriodText(months) == "202603:2,202601:3,202512:1");

    //命中关键字的各月
    f.m_keyword = "报告";
    REQUIRE(store.CountByLocalMonth(f, kNowMs, months));
    CHECK(PeriodText(months) == "202603:1,202601:1,202512:1");
    //通配符
    f.m_keyword = "季度*报告";
    f.m_wildcard = true;
    REQUIRE(store.CountByLocalMonth(f, kNowMs, months));
    CHECK(PeriodText(months) == "202603:1,202601:1,202512:1");

    //按天：只统计给定区间
    f = ConvFilter();
    std::vector<HistoryPeriodCount> days;
    REQUIRE(store.CountByLocalDay(f, kNowMs, LocalMs(2025, 12, 31, 0, 0), LocalMs(2026, 1, 15, 23, 59), days));
    CHECK(PeriodText(days) == "20260115:1,20260101:2,20251231:1");
    REQUIRE(store.CountByLocalDay(f, kNowMs, LocalMs(2026, 3, 1, 0, 0), LocalMs(2026, 3, 31, 0, 0), days));
    CHECK(PeriodText(days) == "20260302:2");

    //命中位置：排序同列表（同毫秒 msgid 大的在前），撤回的不出现
    f.m_keyword = "报告";
    std::vector<HistoryPosition> positions;
    REQUIRE(store.QueryPositions(f, kNowMs, kTimelineMaxHits, positions));
    CHECK(PositionIds(positions) == "5,3,1");
    f.m_keyword.clear();
    REQUIRE(store.QueryPositions(f, kNowMs, kTimelineMaxHits, positions));
    CHECK(PositionIds(positions) == "6,5,4,3,2,1");
    REQUIRE(store.QueryPositions(f, kNowMs, 2, positions));
    CHECK(PositionIds(positions) == "6,5");
    CHECK(positions[0].m_timestamp == LocalMs(2026, 3, 2, 8, 0));

    //未打开的库
    CV2MsgStore closed;
    CHECK_FALSE(closed.CountByLocalMonth(f, kNowMs, months));
    CHECK(months.empty());
    CHECK_FALSE(closed.QueryPositions(f, kNowMs, 1, positions));
}

TEST_CASE("TL8 关键字只高亮：列表保留全部消息，页号换算、跳到某天与导出都不受关键字影响", "[timeline]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshTimelineDb(L"tl8.db").c_str()));
    SeedTimeline(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(kSingle, kPeer);
    model.SetPageSize(2);
    model.SetKeyword("报告");
    REQUIRE(model.Refresh(kNowMs));
    //列表模式：按关键字筛选
    CHECK(model.Total() == 3);
    CHECK_FALSE(model.KeywordHighlightOnly());
    CHECK(model.QueryFilter().m_keyword == "报告");

    //切到只高亮：回到第 1 页，列表是全部 6 条，关键字仍留在 Filter() 里
    model.GoToPage(2);
    model.SetKeywordHighlightOnly(true);
    CHECK(model.CurrentPage() == 1);
    CHECK(model.KeywordHighlightOnly());
    CHECK(model.Filter().m_keyword == "报告");
    CHECK(model.QueryFilter().m_keyword.empty());
    const int64_t countsBefore = model.CountQueryTimes();
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.CountQueryTimes() == countsBefore + 1);   //条件变了，只换页的刷新也重新统计
    CHECK(model.Total() == 6);
    CHECK(model.PageCount() == 3);

    //页号换算按全部消息算：msgid 3 排第 4 条，在第 2 页
    CHECK(model.PageOfPosition(kNowMs, LocalMs(2026, 1, 1, 9, 0), 3) == 2);
    //只换页时沿用总数
    model.GoToPage(3);
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.CountQueryTimes() == countsBefore + 1);
    REQUIRE(model.Records().size() == 2);
    CHECK(model.Records()[1].m_msgid == 1);

    //跳到 2026-01-15：那天最晚的一条是 4（不是命中关键字的那条），在第 2 页
    int64_t dayStart = 0;
    int64_t dayEnd = 0;
    REQUIRE(LocalDayBoundsMs(2026, 1, 15, dayStart, dayEnd));
    const HistoryDateJump jump = model.JumpToDate(kNowMs, dayStart, dayEnd);
    CHECK(jump.m_result == kHistoryDateJumpExact);
    CHECK(jump.m_msgid == 4);
    CHECK(model.CurrentPage() == 2);

    //导出：全部 6 条
    std::vector<HistoryRecord> all;
    REQUIRE(model.QueryAll(kNowMs, all));
    CHECK(all.size() == 6);

    //切回列表模式：按关键字筛选，回到第 1 页
    model.SetKeywordHighlightOnly(false);
    CHECK(model.CurrentPage() == 1);
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.Total() == 3);

    //没有关键字时切换不影响结果，停在原来那一页
    model.SetKeyword("");
    REQUIRE(model.Refresh(kNowMs));
    model.GoToPage(2);
    model.SetKeywordHighlightOnly(true);
    CHECK(model.CurrentPage() == 2);
}

TEST_CASE("TL10 按区间统计各月条数：与逐行换算本地时间的写法结果一致，跨年、月份多时分批", "[timeline]")
{
    //各月区间：按月份倒序，相邻两月首尾相接，十二月跨到下一年
    const std::vector<HistoryMonthRange> ranges = BuildLocalMonthRanges(LocalMs(2025, 11, 15, 0, 0),
                                                                        LocalMs(2026, 2, 1, 0, 0));
    REQUIRE(ranges.size() == 4);
    CHECK(ranges[0].m_key == 202602);
    CHECK(ranges[1].m_key == 202601);
    CHECK(ranges[2].m_key == 202512);
    CHECK(ranges[3].m_key == 202511);
    CHECK(ranges[0].m_startMs == LocalMs(2026, 2, 1, 0, 0));
    CHECK(ranges[2].m_endMs + 1 == ranges[1].m_startMs);
    CHECK(ranges[3].m_startMs == LocalMs(2025, 11, 1, 0, 0));
    CHECK(BuildLocalMonthRanges(0, LocalMs(2026, 1, 1, 0, 0)).empty());
    CHECK(BuildLocalMonthRanges(LocalMs(2026, 3, 1, 0, 0), LocalMs(2026, 1, 1, 0, 0)).empty());
    REQUIRE(BuildLocalMonthRanges(LocalMs(2026, 3, 9, 0, 0), LocalMs(2026, 3, 20, 0, 0)).size() == 1);

    //SQL：月份在外层，没有月份时为空串
    HistoryFilter f = ConvFilter();
    const std::string sql = BuildMonthRangeCountSql(f, kNowMs, ranges);
    CHECK(sql.find("(VALUES (202602,") != std::string::npos);
    CHECK(sql.find(") AS m CROSS JOIN t_msg") != std::string::npos);
    CHECK(sql.find("GROUP BY k ORDER BY k DESC") != std::string::npos);
    CHECK(BuildMonthRangeCountSql(f, kNowMs, std::vector<HistoryMonthRange>()).empty());
    CHECK(BuildEdgeTimestampSql(f, kNowMs, true).find("ORDER BY timestamp DESC LIMIT 1") != std::string::npos);

    //本地库：本组数据再加两条 1990 年的消息，月份跨 400 多个，要分两批统计
    const std::wstring path = FreshTimelineDb(L"tl10.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedTimeline(store);
    REQUIRE(store.Upsert(TextMsg(31, LocalMs(1990, 1, 5, 8, 0), "很早的季度报告")));
    REQUIRE(store.Upsert(TextMsg(32, LocalMs(1990, 3, 31, 23, 59), "很早的消息")));
    REQUIRE(BuildLocalMonthRanges(LocalMs(1990, 1, 5, 8, 0), LocalMs(2026, 3, 2, 8, 0)).size()
            > kMonthRangeSqlMaxMonths);

    //参照：另开一条连接，直接执行逐行换算本地时间的写法
    sqlite3* ref = NULL;
    REQUIRE(sqlite3_open16(path.c_str(), &ref) == SQLITE_OK);
    const char* const kKeywords[] = { "", "报告", "季度*报告" };
    for (size_t k = 0; k < sizeof(kKeywords) / sizeof(kKeywords[0]); ++k)
    {
        INFO("keyword #" << k);
        HistoryFilter kf = ConvFilter();
        kf.m_keyword = kKeywords[k];
        kf.m_wildcard = (k == 2);
        std::vector<HistoryPeriodCount> expect;
        sqlite3_stmt* stmt = NULL;
        REQUIRE(sqlite3_prepare_v2(ref, BuildMonthCountSql(kf, kNowMs).c_str(), -1, &stmt, NULL) == SQLITE_OK);
        if (HasKeywordFilter(kf))
        {
            const std::string like = MakeSearchLikePattern(kf.m_keyword, kf.m_wildcard);
            sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_TRANSIENT);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            expect.push_back(HistoryPeriodCount(sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1)));
        }
        sqlite3_finalize(stmt);

        std::vector<HistoryPeriodCount> actual;
        REQUIRE(store.CountByLocalMonth(kf, kNowMs, actual));
        CHECK(PeriodText(actual) == PeriodText(expect));
    }
    sqlite3_close(ref);
    //具体数字核对一次：1990 年两个月各一条，2026 年 3 月两条（撤回的不计）
    std::vector<HistoryPeriodCount> months;
    REQUIRE(store.CountByLocalMonth(ConvFilter(), kNowMs, months));
    CHECK(PeriodText(months) == "202603:2,202601:3,202512:1,199003:1,199001:1");

    //没有符合条件的消息：成功、结果为空
    HistoryFilter none = ConvFilter();
    none.m_keyword = "不会命中";
    REQUIRE(store.CountByLocalMonth(none, kNowMs, months));
    CHECK(months.empty());
}

TEST_CASE("TL11 后台查询执行时间轴统计：结果与直接查本地库一致，命中位置按条数上限截取，库没打开时失败", "[timeline]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshTimelineDb(L"tl11.db").c_str()));
    SeedTimeline(store);

    //有关键字：各月条数不含关键字，各月命中数与命中位置含关键字
    V2SearchRequest req;
    req.m_kind = kV2SearchTimelineStats;
    req.m_filter = ConvFilter();
    req.m_filter.m_keyword = "季度*报告";
    req.m_filter.m_wildcard = true;
    req.m_nowMs = kNowMs;
    req.m_limit = 2;
    req.m_seq = 7;
    std::unique_ptr<V2SearchResponse> resp(CV2MsgSearchWorker::Execute(&store, req));
    REQUIRE(resp.get() != NULL);
    CHECK(resp->m_ok);
    CHECK(resp->m_seq == 7);
    CHECK(resp->m_kind == kV2SearchTimelineStats);
    std::vector<HistoryPeriodCount> expectCounts;
    REQUIRE(store.CountByLocalMonth(ConvFilter(), kNowMs, expectCounts));
    CHECK(PeriodText(resp->m_monthCounts) == PeriodText(expectCounts));
    CHECK(PeriodText(resp->m_monthHits) == "202603:1,202601:1,202512:1");
    //命中位置只取了上限那么多条，接收方据此判断有截断
    CHECK(PositionIds(resp->m_positions) == "5,3");

    //没有关键字：只有各月条数
    req.m_filter.m_keyword.clear();
    resp.reset(CV2MsgSearchWorker::Execute(&store, req));
    CHECK(resp->m_ok);
    CHECK(PeriodText(resp->m_monthCounts) == PeriodText(expectCounts));
    CHECK(resp->m_monthHits.empty());
    CHECK(resp->m_positions.empty());

    //库没打开
    CV2MsgStore closed;
    resp.reset(CV2MsgSearchWorker::Execute(&closed, req));
    CHECK_FALSE(resp->m_ok);
    resp.reset(CV2MsgSearchWorker::Execute(NULL, req));
    CHECK_FALSE(resp->m_ok);
}

TEST_CASE("TL9 F3 下一处、Shift+F3 上一处；按住 Ctrl 或 Alt 时不处理，其余按键不受 Shift 影响", "[timeline],[historykeys]")
{
    CHECK(DecideHistoryKey(VK_F3, false, false, false, false, false) == kHistoryKeyNextHit);
    CHECK(DecideHistoryKey(VK_F3, false, false, true, true, true) == kHistoryKeyPrevHit);
    CHECK(DecideHistoryKey(VK_F3, true, false, false, false, false) == kHistoryKeyNone);
    CHECK(DecideHistoryKey(VK_F3, false, true, false, false, false) == kHistoryKeyNone);
    //其余按键与五参数版本相同
    CHECK(DecideHistoryKey(VK_NEXT, false, false, true, false, false) == kHistoryKeyNextPage);
    CHECK(DecideHistoryKey(VK_ESCAPE, false, false, true, true, true) == kHistoryKeyClearSearch);
    CHECK(DecideHistoryKey('F', true, false, true, false, false) == kHistoryKeyFocusSearch);
    //五参数版本不认 F3
    CHECK(DecideHistoryKey(VK_F3, false, false, false, false) == kHistoryKeyNone);
}
