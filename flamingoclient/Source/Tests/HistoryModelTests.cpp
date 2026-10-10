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
#include "sqlite3.h"   // HM-B1：经 SQLite 的 C 接口批量灌数据
#include <string>
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
    //本地库是 WAL 模式：上次异常退出残留的日志文件会被套用到同名新库上，一并删掉
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//把临时库路径转成 sqlite3_open 要的 UTF-8。不能用 std::string(w.begin(), w.end()) 逐个截断：
//那样非 ASCII 字符会被截坏，编译器也会报 C4244（wchar_t 转 char 可能丢失数据）。
static std::string WidePathToUtf8(const std::wstring& path)
{
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, NULL, 0, NULL, NULL);
    if (bytes <= 1)
    {
        return std::string();
    }
    std::string out((size_t)bytes - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &out[0], bytes, NULL, NULL);
    return out;
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

TEST_CASE("V2M7 显示下界：总数与分页只算下界以上的行，Pin 分类不受限，下界为 0 时与原来相同",
          "[coverage]")
{
    std::wstring path = FreshTempDb(L"flamingo_v2m7.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 10);   //msgid 与时间都取 1..10

    //2 与 8 被钉住：一条在下界以下、一条在下界以上
    bool hit = false;
    REQUIRE(store.SetPinned(1, 10, 2, true, 1000, hit));
    REQUIRE(store.SetPinned(1, 10, 8, true, 1000, hit));

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(4);

    //下界为 0：与不设下界相同，10 条、3 页
    REQUIRE(model.Refresh(0));
    CHECK(model.Total() == 10);
    CHECK(model.PageCount() == 3);

    //下界 6：只剩 6..10 共 5 条、2 页，第一页按时间倒序为 10、9、8、7
    model.SetMinMsgid(6);
    REQUIRE(model.Refresh(0));
    CHECK(model.Total() == 5);
    CHECK(model.PageCount() == 2);
    REQUIRE(model.Records().size() == 4);
    CHECK(model.Records()[0].m_msgid == 10);
    CHECK(model.Records()[3].m_msgid == 7);

    //翻到第 2 页后把下界放宽（覆盖区间往更早处扩展）：页号不被拉回第 1 页
    model.GoToPage(2);
    REQUIRE(model.Refresh(0));
    REQUIRE(model.Records().size() == 1);
    CHECK(model.Records()[0].m_msgid == 6);
    model.SetMinMsgid(0);
    REQUIRE(model.Refresh(0));
    CHECK(model.CurrentPage() == 2);
    CHECK(model.Total() == 10);

    //Pin 分类：下界不起作用，两条被钉住的都在
    model.SetMinMsgid(6);
    model.SetTypeFilter(kHistoryTypePinned);
    REQUIRE(model.Refresh(0));
    CHECK(model.Total() == 2);
}

// =====================================================================
// HM-C1 ~ HM-C4、HM-B1 —— 只换页时沿用上次统计的总数（RefreshPage）
// =====================================================================

TEST_CASE("HM-C1 连续翻页只在第一次刷新时统计一次，每页内容正确", "[historymodel]")
{
    std::wstring path = FreshTempDb(L"flamingo_hmc1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 95);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(10);
    REQUIRE(model.Refresh(0));
    CHECK(model.CountQueryTimes() == 1);
    CHECK(model.Total() == 95);

    // 下一页：第 2 页是 85..76（最新在前）
    model.GoToPage(2);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 1);
    REQUIRE(model.Records().size() == 10);
    CHECK(model.Records().front().m_msgid == 85);

    // 末页：只剩 5..1
    model.GoToPage(model.PageCount());
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 1);
    CHECK(model.CurrentPage() == 10);
    REQUIRE(model.Records().size() == 5);
    CHECK(model.Records().front().m_msgid == 5);

    // 跳回第 2 页
    model.GoToPage(2);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 1);
    CHECK(model.Records().front().m_msgid == 85);
}

TEST_CASE("HM-C2 改了类型、关键字、时间范围或显示下界之后，翻页前先重新统计", "[historymodel]")
{
    std::wstring path = FreshTempDb(L"flamingo_hmc2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 95);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(10);
    REQUIRE(model.Refresh(0));
    REQUIRE(model.CountQueryTimes() == 1);

    // 类型：灌入的都是文字消息，切到图片后 0 条
    model.SetTypeFilter(kHistoryTypeImage);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 2);
    CHECK(model.Total() == 0);
    model.SetTypeFilter(kHistoryTypeAll);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 3);
    CHECK(model.Total() == 95);

    // 关键字：没有任何消息带这段文字
    model.SetKeyword("not-in-any-message");
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 4);
    CHECK(model.Total() == 0);
    model.SetKeyword("");
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 5);
    CHECK(model.Total() == 95);

    // 时间范围：自定义 [10, 20] 毫秒，含两端共 11 条
    model.SetCustomRange(10, 20);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 6);
    CHECK(model.Total() == 11);
    model.SetTimeRange(kHistoryTimeAll);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 7);
    CHECK(model.Total() == 95);

    // 显示下界：只算 msgid >= 50 的，共 46 条
    model.SetMinMsgid(50);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 8);
    CHECK(model.Total() == 46);

    // 条件没再变：翻页不再统计
    model.GoToPage(2);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 8);
}

TEST_CASE("HM-C3 整页刷新会重新统计，之后翻页用新的总数", "[historymodel]")
{
    std::wstring path = FreshTempDb(L"flamingo_hmc3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 95);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(10);
    REQUIRE(model.Refresh(0));
    model.GoToPage(2);
    REQUIRE(model.RefreshPage(0));
    REQUIRE(model.CountQueryTimes() == 1);

    // 本地库新增 5 条（msgid 96..100）：只换页的刷新察觉不到，这正是数据变化时必须用 Refresh 的原因
    for (int i = 96; i <= 100; ++i)
    {
        HistoryRecord rec;
        rec.m_msgid = i;
        rec.m_ctype = 1;
        rec.m_cid = 10;
        rec.m_fromid = 10;
        rec.m_msgtype = 1;
        rec.m_timestamp = i;
        store.Upsert(rec);
    }
    REQUIRE(model.RefreshPage(0));
    CHECK(model.Total() == 95);

    REQUIRE(model.Refresh(0));
    CHECK(model.CountQueryTimes() == 2);
    CHECK(model.Total() == 100);

    // 之后翻页沿用新的总数：共 10 页，末页 10 条
    model.GoToPage(model.PageCount());
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 2);
    CHECK(model.CurrentPage() == 10);
    CHECK(model.Records().size() == 10);
}

TEST_CASE("HM-C4 沿用缓存总数时，越界的页号仍夹到末页", "[historymodel]")
{
    std::wstring path = FreshTempDb(L"flamingo_hmc4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 95);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(10);
    REQUIRE(model.Refresh(0));

    model.GoToPage(99);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == 1);
    CHECK(model.CurrentPage() == 10);
    REQUIRE(model.Records().size() == 5);
    CHECK(model.Records().front().m_msgid == 5);
}

TEST_CASE("HM-B1 一个会话 10 万条消息时整页刷新与只换页刷新的耗时", "[.][historymodelbench]")
{
    const int kTotalRows = 100000;
    // 每种刷新重复的次数，取平均以减小抖动
    const int kRepeat = 5;

    std::wstring path = FreshTempDb(L"flamingo_hmb1.db");
    {
        CV2MsgStore creator;
        REQUIRE(creator.Open(path.c_str()));
    }
    {
        // 经 SQLite 的 C 接口在一个事务里灌数据，灌数据不是被测对象
        sqlite3* db = NULL;
        const std::string pathUtf8 = WidePathToUtf8(path);
        REQUIRE(sqlite3_open(pathUtf8.c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_stmt* stmt = NULL;
        REQUIRE(sqlite3_prepare_v2(db,
            "INSERT INTO t_msg(msgid,ctype,cid,fromid,msgtype,content,search_text,timestamp) "
            "VALUES(?,1,10,1,1,'{}','bench',?)", -1, &stmt, NULL) == SQLITE_OK);
        for (int i = 1; i <= kTotalRows; ++i)
        {
            sqlite3_bind_int64(stmt, 1, i);
            sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(i) * 1000);
            REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        REQUIRE(sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(db);
    }

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(30);

    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    double fullMs = 0;
    double pageMs = 0;
    for (int i = 0; i < kRepeat; ++i)
    {
        LARGE_INTEGER t0;
        LARGE_INTEGER t1;
        LARGE_INTEGER t2;
        ::QueryPerformanceCounter(&t0);
        REQUIRE(model.Refresh(0));
        ::QueryPerformanceCounter(&t1);
        model.GoToPage(2 + i);
        REQUIRE(model.RefreshPage(0));
        ::QueryPerformanceCounter(&t2);
        fullMs += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        pageMs += static_cast<double>(t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;
    }
    WARN("HM-B1: Refresh " << fullMs / kRepeat << " ms, RefreshPage " << pageMs / kRepeat
         << " ms (average of " << kRepeat << ", " << kTotalRows << " rows, 30 per page)");
    CHECK(model.Total() == kTotalRows);
}

// =====================================================================
// 重新打开消息记录窗时重置搜索条件：窗口对象随主面板一直存在，不重置的话新打开的窗口
// 搜索框是空的，列表却仍按上次的关键字筛选
// =====================================================================

TEST_CASE("HSR1 重置搜索条件只清关键字与时间范围、回到第 1 页，其余状态不变", "[historysearchreset]")
{
    // 每页条数、自定义起止、显示下界取几个容易辨认的值，只用来核对它们是否被改动
    const int     kPageSize    = 30;
    const int64_t kCustomMinMs = 1000;
    const int64_t kCustomMaxMs = 2000;
    const int64_t kMinMsgid    = 5;

    HistoryModel model;
    model.SetConversation(2, 10);
    model.SetPageSize(kPageSize);
    model.SetTypeFilter(kHistoryTypeText);
    model.SetKeyword("and/or missin");
    model.SetCustomRange(kCustomMinMs, kCustomMaxMs);
    model.SetMinMsgid(kMinMsgid);

    model.ResetSearchFilters();

    const HistoryFilter& f = model.Filter();
    CHECK(f.m_keyword.empty());
    CHECK(f.m_range == kHistoryTimeAll);
    CHECK(f.m_customMinMs == 0);
    CHECK(f.m_customMaxMs == 0);
    CHECK(f.m_page == 1);
    // 以下各项由调用方另行设置或与搜索无关，重置不应改动
    CHECK(f.m_ctype == 2);
    CHECK(f.m_cid == 10);
    CHECK(f.m_type == kHistoryTypeText);
    CHECK(f.m_pageSize == kPageSize);
    CHECK(f.m_minMsgid == kMinMsgid);
}

TEST_CASE("HSR2 窗口原本没开时要重置搜索条件（同一会话也一样）", "[historysearchreset]")
{
    CHECK(ShouldResetHistorySearch(false, true));
    CHECK(ShouldResetHistorySearch(false, false));
}

TEST_CASE("HSR3 窗口开着但换了会话时要重置搜索条件", "[historysearchreset]")
{
    CHECK(ShouldResetHistorySearch(true, false));
}

TEST_CASE("HSR4 窗口开着且是同一会话时保留搜索条件", "[historysearchreset]")
{
    CHECK_FALSE(ShouldResetHistorySearch(true, true));
}

// =====================================================================
// 按发送人筛选
// =====================================================================

namespace {

//群聊 2/500 里三个人发的消息：msgid 与 timestamp 都取 1..n，发送人按下表轮流
//  userid 7：msgid 1、4、7、10（最后发言 10）
//  userid 8：msgid 2、5、8（最后发言 8）
//  userid 9：msgid 3、6、9（最后发言 9）
const int     kSenderCtype = 2;
const int64_t kSenderCid   = 500;
const int     kSenderMsgCount = 10;
const int64_t kSenderIds[3] = { 7, 8, 9 };

void SeedThreeSenders(CV2MsgStore& store)
{
    for (int i = 1; i <= kSenderMsgCount; ++i)
    {
        HistoryRecord rec;
        rec.m_msgid = i;
        rec.m_ctype = kSenderCtype;
        rec.m_cid = kSenderCid;
        rec.m_fromid = kSenderIds[(i - 1) % 3];
        rec.m_msgtype = 1;
        rec.m_timestamp = i;
        store.Upsert(rec);
    }
}

} // namespace

TEST_CASE("HSF1 按发送人统计条数，取出的每一条都是这个人发的", "[historysender]")
{
    std::wstring path = FreshTempDb(L"flamingo_hsf1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedThreeSenders(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(kSenderCtype, kSenderCid);
    model.SetPageSize(30);
    model.SetSenderFilter(7);
    REQUIRE(model.Refresh(0));

    CHECK(model.Total() == 4);
    REQUIRE(model.Records().size() == 4);
    for (size_t i = 0; i < model.Records().size(); ++i)
    {
        CHECK(model.Records()[i].m_fromid == 7);
    }
    //0 表示不限发送人
    model.SetSenderFilter(0);
    REQUIRE(model.Refresh(0));
    CHECK(model.Total() == kSenderMsgCount);
}

TEST_CASE("HSF2 换了发送人，只换页的刷新也会重新统计总数", "[historysender]")
{
    std::wstring path = FreshTempDb(L"flamingo_hsf2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedThreeSenders(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(kSenderCtype, kSenderCid);
    model.SetPageSize(30);
    REQUIRE(model.Refresh(0));
    const int64_t before = model.CountQueryTimes();

    model.SetSenderFilter(8);
    REQUIRE(model.RefreshPage(0));
    CHECK(model.CountQueryTimes() == before + 1);
    CHECK(model.Total() == 3);
}

TEST_CASE("HSF3 重置搜索条件时清掉发送人", "[historysender]")
{
    HistoryModel model;
    model.SetConversation(kSenderCtype, kSenderCid);
    model.SetSenderFilter(9);
    model.ResetSearchFilters();
    CHECK(model.Filter().m_fromid == 0);
}

TEST_CASE("HSF4 发送人列表按最近发言倒序，不含已撤回，受覆盖区间限制", "[historysender]")
{
    std::wstring path = FreshTempDb(L"flamingo_hsf4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedThreeSenders(store);

    //最后发言顺序：7（msgid 10）、9（msgid 9）、8（msgid 8）
    std::vector<int64_t> senders;
    REQUIRE(store.QuerySenders(kSenderCtype, kSenderCid, 0, senders));
    REQUIRE(senders.size() == 3);
    CHECK(senders[0] == 7);
    CHECK(senders[1] == 9);
    CHECK(senders[2] == 8);

    //7 的最后一条撤回后，它的最近发言变成 msgid 7，排到 9、8 之后
    REQUIRE(store.MarkRecalled(kSenderCtype, kSenderCid, 10));
    REQUIRE(store.QuerySenders(kSenderCtype, kSenderCid, 0, senders));
    REQUIRE(senders.size() == 3);
    CHECK(senders[0] == 9);
    CHECK(senders[1] == 8);
    CHECK(senders[2] == 7);

    //覆盖区间下界 msgid 9：只剩 9 发的那一条（10 已撤回）
    REQUIRE(store.QuerySenders(kSenderCtype, kSenderCid, 9, senders));
    REQUIRE(senders.size() == 1);
    CHECK(senders[0] == 9);

    //别的会话没有消息
    REQUIRE(store.QuerySenders(kSenderCtype, kSenderCid + 1, 0, senders));
    CHECK(senders.empty());

    //模型按自己的会话与显示下界取，结果与直接查库一致；发送人筛选不影响名单
    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(kSenderCtype, kSenderCid);
    model.SetSenderFilter(8);
    std::vector<int64_t> viaModel;
    REQUIRE(model.QuerySenders(viaModel));
    REQUIRE(store.QuerySenders(kSenderCtype, kSenderCid, 0, senders));
    CHECK(viaModel == senders);
}

TEST_CASE("HSF5 切换发送人后回到第 1 页", "[historysender]")
{
    std::wstring path = FreshTempDb(L"flamingo_hsf5.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedThreeSenders(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(kSenderCtype, kSenderCid);
    model.SetPageSize(3);
    REQUIRE(model.Refresh(0));
    model.GoToPage(3);
    REQUIRE(model.CurrentPage() == 3);

    model.SetSenderFilter(7);
    CHECK(model.CurrentPage() == 1);
}

// =====================================================================
// 查看上下文：某条消息在全量列表里的第几页
// =====================================================================

TEST_CASE("HCX1 100 条、每页 30 条：最新一条、第 45 条、最老一条分别在第 1、2、4 页", "[historycontext]")
{
    std::wstring path = FreshTempDb(L"flamingo_hcx1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedMessages(store, 1, 10, 100);   //msgid 与 timestamp 都是 1..100，最新在前排列

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(30);

    CHECK(model.PageOfPosition(0, 100, 100) == 1);
    //按最新在前数，第 45 条是 msgid 56；前面有 44 条，落在第 2 页
    CHECK(model.PageOfPosition(0, 56, 56) == 2);
    CHECK(model.PageOfPosition(0, 1, 1) == 4);
}

TEST_CASE("HCX2 同一毫秒有多条消息时，算出的页里确实包含这条消息", "[historycontext]")
{
    std::wstring path = FreshTempDb(L"flamingo_hcx2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    //msgid 1..9 全部同一毫秒，排序时退到按 msgid 倒序
    const int kSameTimeCount = 9;
    const int64_t kSameTime = 1000;
    for (int i = 1; i <= kSameTimeCount; ++i)
    {
        HistoryRecord rec;
        rec.m_msgid = i;
        rec.m_ctype = 1;
        rec.m_cid = 10;
        rec.m_fromid = 10;
        rec.m_msgtype = 1;
        rec.m_timestamp = kSameTime;
        store.Upsert(rec);
    }

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(2);
    REQUIRE(model.Refresh(0));
    for (int i = 1; i <= kSameTimeCount; ++i)
    {
        INFO("msgid " << i);
        const int page = model.PageOfPosition(0, kSameTime, i);
        REQUIRE(page >= 1);
        model.GoToPage(page);
        REQUIRE(model.RefreshPage(0));
        bool found = false;
        for (size_t k = 0; k < model.Records().size(); ++k)
        {
            if (model.Records()[k].m_msgid == i)
            {
                found = true;
            }
        }
        CHECK(found);
    }
}

TEST_CASE("HCX3 搜索状态下查看上下文：筛选条件全部清空，当前页包含这条消息", "[historycontext]")
{
    std::wstring path = FreshTempDb(L"flamingo_hcx3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    const int kCount = 100;
    const int64_t kTarget = 56;
    for (int i = 1; i <= kCount; ++i)
    {
        HistoryRecord rec;
        rec.m_msgid = i;
        rec.m_ctype = 1;
        rec.m_cid = 10;
        rec.m_fromid = (i % 2 == 0) ? 10 : 20;
        rec.m_msgtype = 1;
        rec.m_timestamp = i;
        rec.m_searchText = (i == kTarget) ? "needle" : "hay";
        store.Upsert(rec);
    }

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(30);
    model.SetKeyword("needle");
    model.SetTypeFilter(kHistoryTypeText);
    model.SetSenderFilter(10);
    model.SetTimeRange(kHistoryTime7Days);
    //「近 7 天」按当前时间推算，当前时间要晚于全部消息（时间戳 1..100 毫秒）
    const int64_t kNowMs = 1000;
    REQUIRE(model.Refresh(kNowMs));
    REQUIRE(model.Records().size() == 1);

    const HistoryRecord target = model.Records()[0];
    REQUIRE(model.ShowContextOf(kNowMs, target));

    const HistoryFilter& f = model.Filter();
    CHECK(f.m_keyword.empty());
    CHECK(f.m_type == kHistoryTypeAll);
    CHECK(f.m_fromid == 0);
    CHECK(f.m_range == kHistoryTimeAll);
    CHECK(model.Total() == kCount);
    CHECK(model.CurrentPage() == 2);
    bool found = false;
    for (size_t k = 0; k < model.Records().size(); ++k)
    {
        if (model.Records()[k].m_msgid == kTarget)
        {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("HCX4 「查看上下文」只在有筛选条件时出现，消息在覆盖区间以外时不出现", "[historycontext]")
{
    HistoryFilter none;
    CHECK_FALSE(ShouldOfferViewContext(none, 50));

    HistoryFilter keyword;
    keyword.m_keyword = "abc";
    CHECK(ShouldOfferViewContext(keyword, 50));

    HistoryFilter type;
    type.m_type = kHistoryTypeImage;
    CHECK(ShouldOfferViewContext(type, 50));

    HistoryFilter pinned;
    pinned.m_type = kHistoryTypePinned;
    CHECK(ShouldOfferViewContext(pinned, 50));

    HistoryFilter range;
    range.m_range = kHistoryTime30Days;
    CHECK(ShouldOfferViewContext(range, 50));

    HistoryFilter sender;
    sender.m_fromid = 7;
    CHECK(ShouldOfferViewContext(sender, 50));

    //Pin 列表不受覆盖区间限制，钉住的消息可能在区间以外：全量列表里看不到它，不提供
    HistoryFilter pinnedBelow;
    pinnedBelow.m_type = kHistoryTypePinned;
    pinnedBelow.m_minMsgid = 100;
    CHECK_FALSE(ShouldOfferViewContext(pinnedBelow, 50));
    CHECK(ShouldOfferViewContext(pinnedBelow, 100));

    //msgid 未分配（发送中的消息）不提供
    CHECK_FALSE(ShouldOfferViewContext(keyword, 0));
}

// =====================================================================
// 跳到某一天
// =====================================================================

namespace {

//本地时间某一天里的某个时刻（毫秒）：当天 0 点 + offsetMs
int64_t LocalTimeOf(int year, int month, int day, int64_t offsetMs)
{
    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(LocalDayBoundsMs(year, month, day, start, end));
    return start + offsetMs;
}

const int64_t kMsPerHour = 3600LL * 1000;

//三天的消息：2026-09-10 两条、09-12 三条（其中一条图片）、09-15 两条（其中一条图片）。
//msgid 按时间先后递增
void SeedDays(CV2MsgStore& store)
{
    struct Row { int day; int64_t hour; int msgtype; };
    const Row rows[] = {
        { 10, 9, 1 }, { 10, 18, 1 },
        { 12, 8, 1 }, { 12, 12, 2 }, { 12, 20, 1 },
        { 15, 10, 2 }, { 15, 16, 1 },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        HistoryRecord rec;
        rec.m_msgid = (int64_t)i + 1;
        rec.m_ctype = 1;
        rec.m_cid = 10;
        rec.m_fromid = 10;
        rec.m_msgtype = rows[i].msgtype;
        rec.m_timestamp = LocalTimeOf(2026, 9, rows[i].day, rows[i].hour * kMsPerHour);
        store.Upsert(rec);
    }
}

HistoryDateJump JumpTo(HistoryModel& model, int month, int day)
{
    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(LocalDayBoundsMs(2026, month, day, start, end));
    return model.JumpToDate(0, start, end);
}

bool PageHasMsgid(const HistoryModel& model, int64_t msgid)
{
    for (size_t i = 0; i < model.Records().size(); ++i)
    {
        if (model.Records()[i].m_msgid == msgid)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("HDJ1 那天有消息时，跳到那天最晚的一条", "[historydate]")
{
    std::wstring path = FreshTempDb(L"flamingo_hdj1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedDays(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(2);
    REQUIRE(model.Refresh(0));

    //09-12 最晚的是 msgid 5（20 点）；按最新在前排，它前面有 09-15 的两条，落在第 2 页
    const HistoryDateJump j = JumpTo(model, 9, 12);
    CHECK(j.m_result == kHistoryDateJumpExact);
    CHECK(j.m_msgid == 5);
    CHECK(model.CurrentPage() == 2);
    CHECK(PageHasMsgid(model, 5));
}

TEST_CASE("HDJ2 那天没有消息时，跳到之前最近的一条", "[historydate]")
{
    std::wstring path = FreshTempDb(L"flamingo_hdj2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedDays(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(2);
    REQUIRE(model.Refresh(0));

    //09-13、09-14 都没有消息：跳到 09-12 最晚的 msgid 5
    const HistoryDateJump j = JumpTo(model, 9, 14);
    CHECK(j.m_result == kHistoryDateJumpEarlier);
    CHECK(j.m_msgid == 5);
    CHECK(PageHasMsgid(model, 5));
}

TEST_CASE("HDJ3 所选日期早于全部消息时，跳到末页并报告之前没有", "[historydate]")
{
    std::wstring path = FreshTempDb(L"flamingo_hdj3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedDays(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(2);
    REQUIRE(model.Refresh(0));

    const HistoryDateJump j = JumpTo(model, 9, 1);
    CHECK(j.m_result == kHistoryDateJumpNoneBefore);
    CHECK(j.m_msgid == 0);
    CHECK(model.CurrentPage() == model.PageCount());
}

TEST_CASE("HDJ4 跳转只在当前筛选结果里计算", "[historydate]")
{
    std::wstring path = FreshTempDb(L"flamingo_hdj4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedDays(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(1);
    model.SetTypeFilter(kHistoryTypeImage);
    REQUIRE(model.Refresh(0));
    REQUIRE(model.Total() == 2);

    //图片只有两条：09-15 的 msgid 6 在第 1 页，09-12 的 msgid 4 在第 2 页
    HistoryDateJump j = JumpTo(model, 9, 12);
    CHECK(j.m_result == kHistoryDateJumpExact);
    CHECK(j.m_msgid == 4);
    CHECK(model.CurrentPage() == 2);
    //筛选条件保持不变
    CHECK(model.Filter().m_type == kHistoryTypeImage);

    //09-10 没有图片，之前也没有：报告之前没有
    j = JumpTo(model, 9, 10);
    CHECK(j.m_result == kHistoryDateJumpNoneBefore);
}

TEST_CASE("HDJ5 日期边界按本地时间：当天 23:59:59.999 算当天，次日 0 点不算", "[historydate]")
{
    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(LocalDayBoundsMs(2026, 9, 12, start, end));
    int64_t nextStart = 0;
    int64_t nextEnd = 0;
    REQUIRE(LocalDayBoundsMs(2026, 9, 13, nextStart, nextEnd));
    CHECK(end == nextStart - 1);
    CHECK(end - start == 24 * kMsPerHour - 1);
    //非法日期
    CHECK_FALSE(LocalDayBoundsMs(2026, 2, 30, start, end));
    CHECK_FALSE(LocalDayBoundsMs(2026, 13, 1, start, end));

    std::wstring path = FreshTempDb(L"flamingo_hdj5.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    //msgid 1：09-12 的最后一毫秒；msgid 2：09-13 的 0 点整
    REQUIRE(LocalDayBoundsMs(2026, 9, 12, start, end));
    HistoryRecord a;
    a.m_msgid = 1;
    a.m_ctype = 1;
    a.m_cid = 10;
    a.m_fromid = 10;
    a.m_msgtype = 1;
    a.m_timestamp = end;
    store.Upsert(a);
    HistoryRecord b = a;
    b.m_msgid = 2;
    b.m_timestamp = end + 1;
    store.Upsert(b);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(1, 10);
    model.SetPageSize(30);
    REQUIRE(model.Refresh(0));
    const HistoryDateJump j = model.JumpToDate(0, start, end);
    CHECK(j.m_result == kHistoryDateJumpExact);
    CHECK(j.m_msgid == 1);
}

// =====================================================================
// 换列表后要不要滚回顶部：换页或换筛选后新列表从顶部看起，同一页重画保持位置
// =====================================================================

TEST_CASE("HBS1 页号变了，滚回顶部", "[historyscroll]")
{
    HistoryFilter shown;
    shown.m_page = 2;
    HistoryFilter now = shown;
    now.m_page = 3;
    CHECK(ShouldResetListScroll(shown, now));
}

TEST_CASE("HBS2 会话、类型、时间、关键字、发送人、每页条数任一项变了，滚回顶部", "[historyscroll]")
{
    HistoryFilter shown;
    shown.m_ctype = 1;
    shown.m_cid = 10;

    HistoryFilter f = shown;
    f.m_cid = 11;
    CHECK(ShouldResetListScroll(shown, f));

    f = shown;
    f.m_ctype = 2;
    CHECK(ShouldResetListScroll(shown, f));

    f = shown;
    f.m_type = kHistoryTypeImage;
    CHECK(ShouldResetListScroll(shown, f));

    f = shown;
    f.m_range = kHistoryTime7Days;
    CHECK(ShouldResetListScroll(shown, f));

    f = shown;
    f.m_range = kHistoryTimeCustom;
    f.m_customMinMs = 1;
    f.m_customMaxMs = 2;
    HistoryFilter g = f;
    g.m_customMaxMs = 3;
    CHECK(ShouldResetListScroll(f, g));

    f = shown;
    f.m_keyword = "abc";
    CHECK(ShouldResetListScroll(shown, f));

    f = shown;
    f.m_fromid = 7;
    CHECK(ShouldResetListScroll(shown, f));

    f = shown;
    f.m_pageSize = shown.m_pageSize + 1;
    CHECK(ShouldResetListScroll(shown, f));
}

TEST_CASE("HBS3 完全相同、或只有显示下界变了，保持滚动位置", "[historyscroll]")
{
    HistoryFilter shown;
    shown.m_ctype = 1;
    shown.m_cid = 10;
    shown.m_page = 4;
    CHECK_FALSE(ShouldResetListScroll(shown, shown));

    //往前拉取到更早的消息后覆盖区间下界变了：不是用户的操作，不打断他正在看的位置
    HistoryFilter widened = shown;
    widened.m_minMsgid = 100;
    CHECK_FALSE(ShouldResetListScroll(shown, widened));
}
