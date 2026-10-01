/**
 *  消息记录窗"本地连续覆盖区间"纯逻辑的单元测试（Suite CV），HistoryCoverageTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryCoverage.h 的五个函数：一页同步应答覆盖的范围、区间合并、
 *  对账、显示下界、是否需要向服务端拉更早的消息。这一层决定"本地哪些消息算完整、哪些该删"，
 *  算错的后果是消息记录窗漏显示或误删本地消息，所以边界逐条钉住。
 *  方案见 docs/清理聊天记录方案.md §7.2、§7.3、§9.2。
 *
 *  典型用法：FlamingoUnit.exe "[coverage]"
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/HistoryCoverage.h"

#include <algorithm>
#include <vector>

using namespace balloonhistory;

namespace {

/**
 *  造一页同步应答。
 *    before：请求锚点（0=从最新开始）；hasMore：是否还有更早的；
 *    ids / count：本页的 msgid 列表（首元素指针与个数，count 为 0 时 ids 可为 NULL）。
 */
SyncPageInfo MakePage(int64_t before, bool hasMore, const int64_t* ids, size_t count)
{
    SyncPageInfo page;
    page.m_beforeMsgid = before;
    page.m_hasMore = hasMore;
    for (size_t i = 0; i < count; ++i)
        page.m_msgids.push_back(ids[i]);
    return page;
}

//造一个已建立的覆盖区间
CoverRange MakeCover(int64_t lo, int64_t hi, bool reachedOldest)
{
    CoverRange c;
    c.m_valid = true;
    c.m_lo = lo;
    c.m_hi = hi;
    c.m_reachedOldest = reachedOldest;
    return c;
}

//由若干 msgid 拼出期望列表并排序，便于与函数结果（顺序不限）比对
std::vector<int64_t> Sorted(std::vector<int64_t> v)
{
    std::sort(v.begin(), v.end());
    return v;
}

} // anonymous namespace

TEST_CASE("CV1 一页同步应答覆盖的范围", "[coverage]")
{
    const int64_t ids[] = { 120, 150, 180 };

    //① 从最新开始拉、还有更多：[本页最早, 本页最新]
    {
        const PageSpan s = ComputePageSpan(MakePage(0, true, ids, 3));
        REQUIRE(s.m_valid);
        CHECK(s.m_lo == 120);
        CHECK(s.m_hi == 180);
        CHECK_FALSE(s.m_reachesOldest);
    }
    //② 从最新开始拉、没有更多：下界为 0，已到最早
    {
        const PageSpan s = ComputePageSpan(MakePage(0, false, ids, 3));
        REQUIRE(s.m_valid);
        CHECK(s.m_lo == 0);
        CHECK(s.m_hi == 180);
        CHECK(s.m_reachesOldest);
    }
    //③ 往前翻（锚点 200）、还有更多：上界是锚点 - 1，而不是本页最新一条
    {
        const PageSpan s = ComputePageSpan(MakePage(200, true, ids, 3));
        REQUIRE(s.m_valid);
        CHECK(s.m_lo == 120);
        CHECK(s.m_hi == 199);
        CHECK_FALSE(s.m_reachesOldest);
    }
    //④ 往前翻、没有更多
    {
        const PageSpan s = ComputePageSpan(MakePage(200, false, ids, 3));
        REQUIRE(s.m_valid);
        CHECK(s.m_lo == 0);
        CHECK(s.m_hi == 199);
        CHECK(s.m_reachesOldest);
    }
    //⑤ 往前翻、一条都没返回：锚点以下服务端一条都没有了
    {
        const PageSpan s = ComputePageSpan(MakePage(200, false, NULL, 0));
        REQUIRE(s.m_valid);
        CHECK(s.m_lo == 0);
        CHECK(s.m_hi == 199);
        CHECK(s.m_reachesOldest);
    }
    //⑥ 从最新开始拉、一条都没返回：没有上界，不产生范围
    {
        const PageSpan s = ComputePageSpan(MakePage(0, false, NULL, 0));
        CHECK_FALSE(s.m_valid);
    }
    //本页 msgid 顺序不限：按最小、最大取
    {
        const int64_t unordered[] = { 180, 120, 150 };
        const PageSpan s = ComputePageSpan(MakePage(0, true, unordered, 3));
        REQUIRE(s.m_valid);
        CHECK(s.m_lo == 120);
        CHECK(s.m_hi == 180);
    }
}

TEST_CASE("CV2 从最新开始拉的一页与原有区间合并", "[coverage]")
{
    //原来没有区间：以本页建立
    {
        const int64_t ids[] = { 900, 950 };
        const CoverRange c = MergeCoverage(CoverRange(), MakePage(0, true, ids, 2));
        REQUIRE(c.m_valid);
        CHECK(c.m_lo == 900);
        CHECK(c.m_hi == 950);
        CHECK_FALSE(c.m_reachedOldest);
    }

    //接得上（本页最早 900 不高于原上界 950）：下界取小、上界取大
    {
        const int64_t ids[] = { 900, 1000 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(0, true, ids, 2));
        REQUIRE(c.m_valid);
        CHECK(c.m_lo == 500);
        CHECK(c.m_hi == 1000);
    }

    //恰好首尾相接（本页最早 951 = 原上界 + 1）：同样算接得上
    {
        const int64_t ids[] = { 951, 1000 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(0, true, ids, 2));
        CHECK(c.m_lo == 500);
        CHECK(c.m_hi == 1000);
    }

    //接不上（本页最早 2000 远高于原上界 950，中间可能有断层）：以本页重新起算
    {
        const int64_t ids[] = { 2000, 2100 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, true), MakePage(0, true, ids, 2));
        REQUIRE(c.m_valid);
        CHECK(c.m_lo == 2000);
        CHECK(c.m_hi == 2100);
        //旧区间的"已到最早"不再成立：新区间与它之间隔着断层
        CHECK_FALSE(c.m_reachedOldest);
    }

    //原区间已到最早、本页接得上：保留"已到最早"
    {
        const int64_t ids[] = { 900, 1000 };
        const CoverRange c = MergeCoverage(MakeCover(0, 950, true), MakePage(0, true, ids, 2));
        CHECK(c.m_lo == 0);
        CHECK(c.m_reachedOldest);
    }

    //本页无法确定范围（从最新开始拉却为空）：原区间不变
    {
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(0, false, NULL, 0));
        CHECK(c.m_lo == 500);
        CHECK(c.m_hi == 950);
    }
}

TEST_CASE("CV3 往前翻的一页与原有区间合并", "[coverage]")
{
    //以原区间下界为锚点往前翻（典型路径）：接得上，扩展下界
    {
        const int64_t ids[] = { 300, 400 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(500, true, ids, 2));
        REQUIRE(c.m_valid);
        CHECK(c.m_lo == 300);
        CHECK(c.m_hi == 950);
        CHECK_FALSE(c.m_reachedOldest);
    }

    //本页没有更多：扩到 0，标记已到最早
    {
        const int64_t ids[] = { 300, 400 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(500, false, ids, 2));
        CHECK(c.m_lo == 0);
        CHECK(c.m_reachedOldest);
    }

    //锚点落在原区间内部（聊天窗往上翻页的锚点与消息记录窗不同）：同样接得上
    {
        const int64_t ids[] = { 450, 520 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(600, true, ids, 2));
        CHECK(c.m_lo == 450);
        CHECK(c.m_hi == 950);
    }

    //锚点低于原区间下界（本页范围 [100, 299] 与原区间 [500, 950] 之间有空档）：原区间不变
    {
        const int64_t ids[] = { 100, 200 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(300, true, ids, 2));
        CHECK(c.m_lo == 500);
        CHECK(c.m_hi == 950);
        CHECK_FALSE(c.m_reachedOldest);
    }

    //本页整体在原区间之上（本页范围 [2000, 2999] 与原区间 [500, 950] 不相接）：原区间不变
    {
        const int64_t ids[] = { 2000, 2500 };
        const CoverRange c = MergeCoverage(MakeCover(500, 950, false), MakePage(3000, true, ids, 2));
        CHECK(c.m_lo == 500);
        CHECK(c.m_hi == 950);
    }

    //原来没有区间：往前翻的一页不建立区间
    {
        const int64_t ids[] = { 300, 400 };
        const CoverRange c = MergeCoverage(CoverRange(), MakePage(500, true, ids, 2));
        CHECK_FALSE(c.m_valid);
    }
}

TEST_CASE("CV4 对账：范围内本地有而本页没有的列为待删", "[coverage]")
{
    //从最新开始拉：范围 [120, 180]
    {
        const int64_t pageIds[] = { 120, 150, 180 };
        std::vector<int64_t> local;
        local.push_back(100);   //范围外（更早），不动
        local.push_back(120);
        local.push_back(130);   //范围内、本页没有：待删
        local.push_back(150);
        local.push_back(170);   //范围内、本页没有：待删
        local.push_back(180);
        local.push_back(200);   //范围外（对账期间才到的新消息），不动
        std::vector<int64_t> expected;
        expected.push_back(130);
        expected.push_back(170);
        CHECK(Sorted(ComputeReconcileDeletions(MakePage(0, true, pageIds, 3), local)) == expected);
    }

    //往前翻、没有更多：范围 [0, 199]，本页没有的本地行全部待删
    {
        const int64_t pageIds[] = { 150 };
        std::vector<int64_t> local;
        local.push_back(10);
        local.push_back(150);
        local.push_back(199);
        local.push_back(200);   //等于锚点，范围外
        std::vector<int64_t> expected;
        expected.push_back(10);
        expected.push_back(199);
        CHECK(Sorted(ComputeReconcileDeletions(MakePage(200, false, pageIds, 1), local)) == expected);
    }

    //本页有而本地没有的，不影响结果
    {
        const int64_t pageIds[] = { 120, 150, 180 };
        std::vector<int64_t> local;
        local.push_back(150);
        CHECK(ComputeReconcileDeletions(MakePage(0, true, pageIds, 3), local).empty());
    }

    //从最新开始拉却一条都没返回：不对账（没有上界，可能误删刚到的新消息）
    {
        std::vector<int64_t> local;
        local.push_back(100);
        local.push_back(200);
        CHECK(ComputeReconcileDeletions(MakePage(0, false, NULL, 0), local).empty());
    }
}

TEST_CASE("CV5 消息记录窗的显示下界", "[coverage]")
{
    //有区间、未到最早：只显示下界以上的
    CHECK(CoverageMinVisibleMsgid(MakeCover(500, 950, false), false) == 500);

    //已到最早：不限制
    CHECK(CoverageMinVisibleMsgid(MakeCover(0, 950, true), false) == 0);

    //没有区间（刚升级、刚登录、或断网时回填到不了）：不限制，照旧显示全部本地记录。
    //2026-09-30 与用户确认改为此口径：若"不显示、等回填"，断网时消息记录窗会一直是空的
    CHECK(CoverageMinVisibleMsgid(CoverRange(), false) == 0);

    //Pin 分类：无论有没有区间都不限制
    CHECK(CoverageMinVisibleMsgid(MakeCover(500, 950, false), true) == 0);
    CHECK(CoverageMinVisibleMsgid(CoverRange(), true) == 0);
}

TEST_CASE("CV6 是否需要向服务端拉更早的消息", "[coverage]")
{
    const CoverRange cover = MakeCover(500, 950, false);
    const int64_t kOldestCoveredMs = 1700000000000LL;
    const int64_t kOneDayMs = 24LL * 60 * 60 * 1000;

    //在最后一页、未到最早、不是 Pin 分类、没有时间筛选：需要拉
    CHECK(ShouldFetchOlder(cover, true, false, false, 0, kOldestCoveredMs));

    //时间筛选的下界早于区间内最早一条：区间还没盖住筛选的时间段，需要拉
    CHECK(ShouldFetchOlder(cover, true, false, true, kOldestCoveredMs - kOneDayMs, kOldestCoveredMs));

    //时间筛选的下界不早于区间内最早一条：区间已盖住，不拉（含恰好相等）
    CHECK_FALSE(ShouldFetchOlder(cover, true, false, true, kOldestCoveredMs, kOldestCoveredMs));
    CHECK_FALSE(ShouldFetchOlder(cover, true, false, true, kOldestCoveredMs + kOneDayMs, kOldestCoveredMs));

    //已到最早、不在最后一页、Pin 分类、区间未建立：都不拉
    CHECK_FALSE(ShouldFetchOlder(MakeCover(0, 950, true), true, false, false, 0, kOldestCoveredMs));
    CHECK_FALSE(ShouldFetchOlder(cover, false, false, false, 0, kOldestCoveredMs));
    CHECK_FALSE(ShouldFetchOlder(cover, true, true, false, 0, kOldestCoveredMs));
    CHECK_FALSE(ShouldFetchOlder(CoverRange(), true, false, false, 0, kOldestCoveredMs));
}

TEST_CASE("CV7 连续往前拉取何时停", "[coverage]")
{
    const int kPageSize = 8;
    const int64_t kBefore = 40;

    //筛选结果还没多出一页、未到最早、轮数未到上限：接着拉
    CHECK(ShouldContinueFetchOlder(1, kMaxFetchOlderRounds, kBefore, kBefore + 3, kPageSize, false));

    //已多出一整页（含恰好一页）：停
    CHECK_FALSE(ShouldContinueFetchOlder(1, kMaxFetchOlderRounds, kBefore, kBefore + kPageSize,
                                         kPageSize, false));
    CHECK_FALSE(ShouldContinueFetchOlder(1, kMaxFetchOlderRounds, kBefore, kBefore + 100,
                                         kPageSize, false));

    //已拉到服务端最早一条：停
    CHECK_FALSE(ShouldContinueFetchOlder(1, kMaxFetchOlderRounds, kBefore, kBefore, kPageSize, true));

    //轮数到上限：停，即便一条符合筛选的都没多出来
    CHECK(ShouldContinueFetchOlder(kMaxFetchOlderRounds - 1, kMaxFetchOlderRounds, kBefore, kBefore,
                                   kPageSize, false));
    CHECK_FALSE(ShouldContinueFetchOlder(kMaxFetchOlderRounds, kMaxFetchOlderRounds, kBefore, kBefore,
                                         kPageSize, false));
}

TEST_CASE("HC1 是否还有更早的记录没有加载", "[coverage]")
{
    // 覆盖区间内最早一条的时间与一个更早的时间点（Unix 毫秒）
    const int64_t oldestCovered = 1790000000000LL;
    const int64_t earlier = oldestCovered - 86400000LL;

    // 区间还没到最早：有
    CHECK(HasUnloadedOlder(MakeCover(500, 950, false), false, false, 0, oldestCovered));
    // 时间筛选的下界早于区间内最早一条：区间没盖住整个时间段，仍然有
    CHECK(HasUnloadedOlder(MakeCover(500, 950, false), false, true, earlier, oldestCovered));

    // 已经拉到最早
    CHECK_FALSE(HasUnloadedOlder(MakeCover(0, 950, true), false, false, 0, oldestCovered));
    // Pin 分类
    CHECK_FALSE(HasUnloadedOlder(MakeCover(500, 950, false), true, false, 0, oldestCovered));
    // 时间筛选的下界已在区间之内（不早于区间内最早一条）
    CHECK_FALSE(HasUnloadedOlder(MakeCover(500, 950, false), false, true, oldestCovered, oldestCovered));
    // 没有覆盖区间（离线、调试入口）
    CHECK_FALSE(HasUnloadedOlder(CoverRange(), false, false, 0, oldestCovered));

    // 与"要不要去拉"一致：停在最后一页时两者相同，不在最后一页时只是"有"、但不去拉
    CHECK(ShouldFetchOlder(MakeCover(500, 950, false), true, false, false, 0, oldestCovered));
    CHECK_FALSE(ShouldFetchOlder(MakeCover(500, 950, false), false, false, false, 0, oldestCovered));
}

TEST_CASE("HC2 顶部副标题文字", "[coverage]")
{
    CHECK(BuildHistorySubtitleText(100, true) == L"已加载 100 条消息，翻到最后一页会继续加载更早的");
    CHECK(BuildHistorySubtitleText(100, false) == L"共 100 条消息");
    CHECK(BuildHistorySubtitleText(0, false) == L"共 0 条消息");
}

TEST_CASE("HC3 底部信息栏文字", "[coverage]")
{
    CHECK(BuildHistoryFooterText(100, 1, 4, true, false) == L"已加载 100 条 · 第 1/4 页");
    CHECK(BuildHistoryFooterText(200, 7, 7, false, false) == L"共 200 条 · 第 7/7 页");
    CHECK(BuildHistoryFooterText(100, 4, 4, true, true)
          == L"已加载 100 条 · 第 4/4 页 · 正在加载更早的消息…");
}
