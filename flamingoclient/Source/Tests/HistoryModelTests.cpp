/**
 *  v2 历史消息本地库——浏览状态 / 分页模型单元测试（Suite V2M），HistoryModelTests.cpp
 *
 *  覆盖：页数向上取整、切过滤条件后页号重置、GoToPage 夹取、Refresh 取数与分页、
 *  过滤状态正确落到 Filter()。涉及取数的用例用临时 db + 真实 CV2MsgStore。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/HistoryModel.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include <windows.h>

using namespace balloonhistory;

//取临时目录下的 db 全路径并删旧文件，保证每个用例从空库开始。
//
//文件名里带当前进程 id，是为了让<u>并发跑的两个测试进程</u>各用各的库文件。此前这里
//用的是固定文件名，命令行跑一份、同时 VS 里又跑一份时两个进程会共用同一个文件：先
//起的进程正打开着它，后起的进程 DeleteFileW 删不掉（Windows 上被占用的文件删除会
//失败），随后 CV2MsgStore::Open 失败，表现为 REQUIRE(store.Open(...)) 为 false。
//这类失败与被测代码无关，却极易被误判成回归——已经误判过一次。
//
//代价是每跑一次会在临时目录留下几个几十 KB 的 db 文件，由系统的临时目录清理回收。
static std::wstring FreshTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);

    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));

    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    return path;
}

//往 store 灌入 count 条单聊文本消息：msgid/timestamp 取 1..count，便于断言顺序。
static void SeedMessages(CV2MsgStore& store, int ctype, int64_t cid, int count)
{
    for (int i = 1; i <= count; ++i)
    {
        HistoryRecord rec;
        rec.m_msgid = i;
        rec.m_ctype = ctype;
        rec.m_cid = cid;
        rec.m_fromid = cid;
        rec.m_msgtype = 1;
        rec.m_timestamp = i;
        store.Upsert(rec);
    }
}

TEST_CASE("V2M1 ComputePageCount 向上取整、空结果按1页")
{
    CHECK(HistoryModel::ComputePageCount(0, 10) == 1);
    CHECK(HistoryModel::ComputePageCount(1, 10) == 1);
    CHECK(HistoryModel::ComputePageCount(10, 10) == 1);
    CHECK(HistoryModel::ComputePageCount(11, 10) == 2);
    CHECK(HistoryModel::ComputePageCount(27, 10) == 3);
    CHECK(HistoryModel::ComputePageCount(5, 0) == 1);
}

TEST_CASE("V2M2 切过滤条件后页号重置回第1页")
{
    std::wstring path = FreshTempDb(L"flamingo_v2m2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 27);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(10);
    REQUIRE(model.Refresh(0));

    model.GoToPage(3);
    CHECK(model.CurrentPage() == 3);
    model.SetTypeFilter(kHistoryTypeText);
    CHECK(model.CurrentPage() == 1);

    model.GoToPage(3);
    CHECK(model.CurrentPage() == 3);
    model.SetKeyword("x");
    CHECK(model.CurrentPage() == 1);
}

TEST_CASE("V2M3 GoToPage 夹取到 [1, 页数]")
{
    std::wstring path = FreshTempDb(L"flamingo_v2m3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 27);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(10);
    REQUIRE(model.Refresh(0));   //总数27、3页

    model.GoToPage(99);
    CHECK(model.CurrentPage() == 3);
    model.GoToPage(-5);
    CHECK(model.CurrentPage() == 1);
    model.GoToPage(2);
    CHECK(model.CurrentPage() == 2);
}

TEST_CASE("V2M4 Refresh 取数与分页正确")
{
    std::wstring path = FreshTempDb(L"flamingo_v2m4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 27);   //timestamp 1..27

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(10);

    model.GoToPage(1);
    REQUIRE(model.Refresh(0));
    CHECK(model.Total() == 27);
    CHECK(model.PageCount() == 3);
    REQUIRE(model.Records().size() == 10);
    CHECK(model.Records()[0].m_timestamp == 27);   //最新在前

    model.GoToPage(3);
    REQUIRE(model.Refresh(0));
    REQUIRE(model.Records().size() == 7);           //末页 7 条
    CHECK(model.Records()[0].m_timestamp == 7);     //第3页最新一条
    CHECK(model.Records()[6].m_timestamp == 1);     //最旧一条
}

TEST_CASE("V2M6 QueryAll 取全部匹配，不受当前页 / 页大小影响")
{
    std::wstring path = FreshTempDb(L"flamingo_v2m6.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 27);   //全部 msgtype=1，timestamp 1..27

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(8);
    model.GoToPage(2);
    REQUIRE(model.Refresh(0));

    std::vector<HistoryRecord> all;
    REQUIRE(model.QueryAll(0, all));
    REQUIRE(all.size() == 27);
    CHECK(all[0].m_timestamp == 27);    //最新在前
    CHECK(all[26].m_timestamp == 1);    //最旧在末

    //加类型过滤后只取匹配的（seed 全是文本，按图片过滤应为空）。
    model.SetTypeFilter(kHistoryTypeImage);
    REQUIRE(model.QueryAll(0, all));
    CHECK(all.empty());
}

TEST_CASE("V2M5 过滤状态正确落到 Filter()")
{
    HistoryModel model;
    model.SetConversation(2, 555);
    model.SetTypeFilter(kHistoryTypeImage);
    model.SetTimeRange(kHistoryTime30Days);
    model.SetKeyword("hello");
    model.SetPageSize(15);

    const HistoryFilter& f = model.Filter();
    CHECK(f.m_ctype == 2);
    CHECK(f.m_cid == 555);
    CHECK(f.m_type == kHistoryTypeImage);
    CHECK(f.m_range == kHistoryTime30Days);
    CHECK(f.m_keyword == "hello");
    CHECK(f.m_pageSize == 15);
    CHECK(model.CurrentPage() == 1);

    model.SetCustomRange(100, 200);
    CHECK(model.Filter().m_range == kHistoryTimeCustom);
    CHECK(model.Filter().m_customMinMs == 100);
    CHECK(model.Filter().m_customMaxMs == 200);
}

// ---------------------------------------------------------------------------
// HM-EXP  长消息的「展开 / 收起」状态。
//         方案见 docs/消息记录内容交互方案.md §5 F1。
// ---------------------------------------------------------------------------

TEST_CASE("HM-EXP1 展开态按 msgid 记，默认全是折叠的")
{
    balloonhistory::HistoryModel model;

    //从没设过的一律是折叠态。
    CHECK_FALSE(model.IsExpanded(1001));

    model.SetExpanded(1001, true);
    CHECK(model.IsExpanded(1001));
    //只影响这一条，别的消息不受牵连。
    CHECK_FALSE(model.IsExpanded(1002));

    model.SetExpanded(1001, false);
    CHECK_FALSE(model.IsExpanded(1001));
}

TEST_CASE("HM-EXP2 msgid 无效时不记也不崩")
{
    balloonhistory::HistoryModel model;

    //服务端还没分配 msgid 的消息（本端刚发出、回执未到）不进集合 —— 否则所有
    //这类消息会共享 msgid==0 这一个状态，展开一条别的也跟着展开。
    model.SetExpanded(0, true);
    CHECK_FALSE(model.IsExpanded(0));

    model.SetExpanded(-5, true);
    CHECK_FALSE(model.IsExpanded(-5));

    CHECK_FALSE(model.ToggleExpanded(0));
}

TEST_CASE("HM-EXP3 ToggleExpanded 翻转并返回新状态")
{
    balloonhistory::HistoryModel model;

    CHECK(model.ToggleExpanded(2001));      //折叠 → 展开
    CHECK(model.IsExpanded(2001));

    CHECK_FALSE(model.ToggleExpanded(2001)); //展开 → 折叠
    CHECK_FALSE(model.IsExpanded(2001));
}

TEST_CASE("HM-EXP4 换会话清空展开态，改筛选与翻页则保留")
{
    balloonhistory::HistoryModel model;
    model.SetConversation(1, 100);
    model.SetExpanded(3001, true);
    model.SetExpanded(3002, true);
    REQUIRE(model.IsExpanded(3001));

    //改筛选条件：还是同一批消息，用户翻回来时理应保持原样，不能清。
    model.SetTypeFilter(kHistoryTypeImage);
    CHECK(model.IsExpanded(3001));
    model.SetTimeRange(kHistoryTime7Days);
    CHECK(model.IsExpanded(3001));
    model.SetKeyword("abc");
    CHECK(model.IsExpanded(3001));
    model.GoToPage(3);
    CHECK(model.IsExpanded(3001));

    //设成同一个会话不算"换"，也不该清。
    model.SetConversation(1, 100);
    CHECK(model.IsExpanded(3001));

    //真的换了会话才清 —— 那已经是另一批消息了。
    model.SetConversation(1, 200);
    CHECK_FALSE(model.IsExpanded(3001));
    CHECK_FALSE(model.IsExpanded(3002));
}

TEST_CASE("HM-EXP5 ClearExpanded 清掉全部")
{
    balloonhistory::HistoryModel model;
    model.SetExpanded(4001, true);
    model.SetExpanded(4002, true);
    REQUIRE(model.IsExpanded(4001));
    REQUIRE(model.IsExpanded(4002));

    model.ClearExpanded();
    CHECK_FALSE(model.IsExpanded(4001));
    CHECK_FALSE(model.IsExpanded(4002));
}
