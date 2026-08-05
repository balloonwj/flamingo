/**
 *  v2 历史消息本地库——SQLite 读写集成测试（Suite V2S），V2MsgStoreTests.cpp
 *
 *  覆盖：建表幂等 + 持久化、Upsert 去重与更新、Count 按类型/时间/关键字过滤、
 *  QueryPage 倒序与分页、撤回消息排除、单聊群聊按 (ctype,cid) 隔离、关键字命中
 *  文件名与正文；以及消息 Pin 那一套——存量库缺列自动补齐、Upsert 不冲掉 Pin 标记、
 *  SetPinned 的命中语义、ReplacePinnedSet 的对账与时间下界（V2S12-V2S22，
 *  见 docs/消息Pin接入方案.md）。
 *  用临时 db 文件，跑真实 SQLite（third-party amalgamation）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../BalloonHistory/V2MsgStore.h"
//Pin 那几个用例要自己建一个"旧结构"的库来验证补列，故直接用 sqlite3 的 C 接口，
//不经产品代码 —— 拿产品代码建出来的库已经带上新列，那样测等于什么都没验证。
#include "sqlite3.h"
#include <string>
#include <windows.h>

using namespace balloonhistory;

//取一个临时目录下的 db 全路径，并先删掉同名旧文件，保证每个用例从空库开始。
//@param name  文件名（各用例传不同名，避免跨用例文件占用冲突）
static std::wstring FreshTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    std::wstring path = dir;
    path += name;
    ::DeleteFileW(path.c_str());
    return path;
}

//构造一条入库记录，铺平各用例的样板代码。
static HistoryRecord MakeRecord(int64_t msgid, int ctype, int64_t cid, int64_t fromid,
                                int msgtype, int64_t timestamp, const std::string& searchText,
                                bool isLink, bool recalled)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = fromid;
    rec.m_msgtype = msgtype;
    rec.m_timestamp = timestamp;
    rec.m_searchText = searchText;
    rec.m_isLink = isLink;
    rec.m_recalled = recalled;
    return rec;
}

TEST_CASE("V2S1 建表幂等且数据持久化")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s1.db");
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        REQUIRE(store.Open(path.c_str()));   //再次 Open 幂等
        REQUIRE(store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "hello", false, false)));
    }
    //重开同一文件，数据仍在（验证落盘）。
    CV2MsgStore store2;
    REQUIRE(store2.Open(path.c_str()));
    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    CHECK(store2.Count(f, 0) == 1);
    store2.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("V2S2 Upsert 按主键去重并覆盖字段")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(5, 1, 10, 10, 1, 100, "old", false, false)));
    //同一 (ctype=1,cid=10,msgid=5) 再写一次，应覆盖而非新增。
    REQUIRE(store.Upsert(MakeRecord(5, 1, 10, 10, 1, 200, "new", false, false)));

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    CHECK(store.Count(f, 0) == 1);

    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_searchText == "new");
    CHECK(rows[0].m_timestamp == 200);
}

TEST_CASE("V2S3 Count 按类型/时间/关键字过滤")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    const int64_t now = 1000000000000LL;
    const int64_t msPerDay = 24LL * 60LL * 60LL * 1000LL;

    //文本(非链接) / 文本(链接) / 图片 / 文件 / 语音，均 ctype=1 cid=10。
    store.Upsert(MakeRecord(1, 1, 10, 10, 1, now,                 "hello", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, now,                 "see http://x", true, false));
    store.Upsert(MakeRecord(3, 1, 10, 10, 2, now,                 "", false, false));
    store.Upsert(MakeRecord(4, 1, 10, 10, 3, now,                 "report.pdf", false, false));
    store.Upsert(MakeRecord(5, 1, 10, 10, 4, now - 10 * msPerDay, "", false, false));   //语音，10天前

    HistoryFilter base;
    base.m_ctype = 1;
    base.m_cid = 10;
    CHECK(store.Count(base, now) == 5);

    HistoryFilter t = base;
    t.m_type = kHistoryTypeText;
    CHECK(store.Count(t, now) == 1);     //仅非链接文本

    HistoryFilter lk = base;
    lk.m_type = kHistoryTypeLink;
    CHECK(store.Count(lk, now) == 1);

    HistoryFilter im = base;
    im.m_type = kHistoryTypeImage;
    CHECK(store.Count(im, now) == 1);

    HistoryFilter fi = base;
    fi.m_type = kHistoryTypeFile;
    CHECK(store.Count(fi, now) == 1);

    HistoryFilter vo = base;
    vo.m_type = kHistoryTypeVoice;
    CHECK(store.Count(vo, now) == 1);

    //近7天：把10天前的语音排除在外，余 4 条。
    HistoryFilter recent = base;
    recent.m_range = kHistoryTime7Days;
    CHECK(store.Count(recent, now) == 4);

    //关键字命中文件名。
    HistoryFilter kw = base;
    kw.m_keyword = "report";
    CHECK(store.Count(kw, now) == 1);
}

TEST_CASE("V2S4 QueryPage 时间倒序与分页")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "b", false, false));
    store.Upsert(MakeRecord(3, 1, 10, 10, 1, 300, "c", false, false));
    store.Upsert(MakeRecord(4, 1, 10, 10, 1, 400, "d", false, false));
    store.Upsert(MakeRecord(5, 1, 10, 10, 1, 500, "e", false, false));

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    f.m_pageSize = 2;

    std::vector<HistoryRecord> rows;

    f.m_page = 1;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_timestamp == 500);   //最新在前
    CHECK(rows[1].m_timestamp == 400);

    f.m_page = 2;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_timestamp == 300);
    CHECK(rows[1].m_timestamp == 200);

    f.m_page = 3;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_timestamp == 100);

    f.m_page = 4;
    REQUIRE(store.QueryPage(f, 0, rows));
    CHECK(rows.empty());
}

TEST_CASE("V2S5 撤回消息被排除")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s5.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "keep", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "gone", false, true));   //已撤回

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    CHECK(store.Count(f, 0) == 1);

    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_searchText == "keep");
}

TEST_CASE("V2S6 单聊与群聊按 (ctype,cid) 隔离")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s6.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //相同 cid=100、相同 msgid=1，但 ctype 不同：主键允许两行共存、互不串。
    store.Upsert(MakeRecord(1, 1, 100, 10, 1, 100, "single", false, false));
    store.Upsert(MakeRecord(1, 2, 100, 20, 1, 100, "group", false, false));

    HistoryFilter single;
    single.m_ctype = 1;
    single.m_cid = 100;
    CHECK(store.Count(single, 0) == 1);

    HistoryFilter group;
    group.m_ctype = 2;
    group.m_cid = 100;
    CHECK(store.Count(group, 0) == 1);

    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(single, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_searchText == "single");
}

TEST_CASE("V2S7 关键字命中文件名与正文")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s7.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "约个会议时间", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 3, 200, "季度报告.xlsx", false, false));

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;

    f.m_keyword = "会议";
    CHECK(store.Count(f, 0) == 1);

    f.m_keyword = "报告";
    CHECK(store.Count(f, 0) == 1);

    f.m_keyword = "xlsx";
    CHECK(store.Count(f, 0) == 1);

    f.m_keyword = "无此词";
    CHECK(store.Count(f, 0) == 0);
}

// =====================================================================
// V2S8-V2S11. MarkRecalled —— 收到撤回推送后把本地记录标记为已撤回
// =====================================================================

TEST_CASE("V2S8 MarkRecalled 后该条从 Count 与 QueryPage 中消失")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s8.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //两条都以未撤回入库，随后撤回其中一条（与 V2S5 呼应：那里验证"入库即已撤回"被排除，
    //这里验证"入库后再标记撤回"同样被排除）。
    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "keep", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "gone", false, false));

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    REQUIRE(store.Count(f, 0) == 2);

    CHECK(store.MarkRecalled(1, 10, 2));

    CHECK(store.Count(f, 0) == 1);
    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_searchText == "keep");
}

TEST_CASE("V2S9 MarkRecalled 重复调用幂等")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s9.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "keep", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "gone", false, false));

    //服务端可能因重连补推同一条撤回通知，重复标记必须无副作用。
    CHECK(store.MarkRecalled(1, 10, 2));
    CHECK(store.MarkRecalled(1, 10, 2));
    CHECK(store.MarkRecalled(1, 10, 2));

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    CHECK(store.Count(f, 0) == 1);
}

TEST_CASE("V2S10 MarkRecalled 对本地不存在的消息不产生副作用")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s10.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "keep", false, false));

    //本机从未同步过这条历史消息（比如在别的设备上发出）——命中 0 行属正常，算成功。
    CHECK(store.MarkRecalled(1, 10, 999));

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    CHECK(store.Count(f, 0) == 1);   //已有记录未被误伤

    //msgid 非正说明服务器根本没给这条消息分配 ID，直接判失败、不去查库。
    CHECK_FALSE(store.MarkRecalled(1, 10, 0));
    CHECK_FALSE(store.MarkRecalled(1, 10, -5));
    CHECK(store.Count(f, 0) == 1);
}

TEST_CASE("V2S11 MarkRecalled 只影响指定的 (ctype,cid,msgid)")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s11.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //单聊与群聊共用 cid=100、msgid=1（主键允许共存，见 V2S6），另加一条同会话的别的消息。
    store.Upsert(MakeRecord(1, 1, 100, 10, 1, 100, "single", false, false));
    store.Upsert(MakeRecord(1, 2, 100, 20, 1, 100, "group", false, false));
    store.Upsert(MakeRecord(2, 1, 100, 10, 1, 200, "single2", false, false));

    //只撤回单聊那条 msgid=1。
    CHECK(store.MarkRecalled(1, 100, 1));

    HistoryFilter fs;
    fs.m_ctype = 1;
    fs.m_cid = 100;
    std::vector<HistoryRecord> single;
    REQUIRE(store.QueryPage(fs, 0, single));
    REQUIRE(single.size() == 1);
    CHECK(single[0].m_searchText == "single2");   //同会话的另一条不受影响

    //群聊里那条同 msgid 的消息必须完好。
    HistoryFilter fg;
    fg.m_ctype = 2;
    fg.m_cid = 100;
    std::vector<HistoryRecord> group;
    REQUIRE(store.QueryPage(fg, 0, group));
    REQUIRE(group.size() == 1);
    CHECK(group[0].m_searchText == "group");
}

// ---------------------------------------------------------------------------
// V2S12-V2S22  消息 Pin：存量库补列、Upsert 不冲 pin、SetPinned 的命中语义、
//              ReplacePinnedSet 的对账与下界语义。
//              方案见 docs/消息Pin接入方案.md，用例对应其第 9 节第 20~30 条。
// ---------------------------------------------------------------------------

//按"加 Pin 列之前"的旧结构建一个库，用来验证存量库补列。
//这里刻意手写旧建表语句而不是复用产品代码 —— 产品代码里的建表语句已经带上新列了，
//拿它建出来的库根本不是"存量库"，测了也白测。
static void CreateLegacyDb(const std::wstring& path)
{
    sqlite3* db = NULL;
    REQUIRE(sqlite3_open16(path.c_str(), &db) == SQLITE_OK);
    const char* legacySql =
        "CREATE TABLE IF NOT EXISTS t_msg ("
        "msgid INTEGER NOT NULL,"
        "clientmsgid TEXT,"
        "ctype INTEGER NOT NULL,"
        "cid INTEGER NOT NULL,"
        "fromid INTEGER NOT NULL,"
        "msgtype INTEGER NOT NULL,"
        "content TEXT,"
        "search_text TEXT,"
        "is_link INTEGER NOT NULL DEFAULT 0,"
        "timestamp INTEGER NOT NULL,"
        "recalled INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (ctype,cid,msgid))";
    REQUIRE(sqlite3_exec(db, legacySql, NULL, NULL, NULL) == SQLITE_OK);
    //塞一条旧数据，验证补列不会破坏既有内容
    const char* seedSql =
        "INSERT INTO t_msg (msgid,clientmsgid,ctype,cid,fromid,msgtype,content,"
        "search_text,is_link,timestamp,recalled) "
        "VALUES (1,'old-1',1,10,10,1,'{}','legacy',0,100,0)";
    REQUIRE(sqlite3_exec(db, seedSql, NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
}

//查某张表里有没有某个列（直接读 PRAGMA，不经产品代码，免得用产品代码验证产品代码）。
static bool DbHasColumn(const std::wstring& path, const char* column)
{
    sqlite3* db = NULL;
    if (sqlite3_open16(path.c_str(), &db) != SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    bool found = false;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(t_msg)", -1, &stmt, NULL) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            if (name != NULL && std::string((const char*)name) == column)
            {
                found = true;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

//造一条对账用的 Pin 记录（msgid + Pin 时间），铺平各用例的样板代码。
static PinnedItem MakePinnedItem(int64_t msgid, int64_t pinTime)
{
    PinnedItem item;
    item.m_msgid = msgid;
    item.m_pinTime = pinTime;
    return item;
}

TEST_CASE("V2S12 存量库缺列自动补齐，旧数据完好且带 pin 的查询不再报错")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s12.db");
    CreateLegacyDb(path);

    //补列之前确实没有这两列，否则这个用例等于什么都没验证
    REQUIRE_FALSE(DbHasColumn(path, "pinned"));
    REQUIRE_FALSE(DbHasColumn(path, "pin_time"));

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    CHECK(DbHasColumn(path, "pinned"));
    CHECK(DbHasColumn(path, "pin_time"));

    //旧数据仍在，且能正常查出来。QueryPage 的 SELECT 列表里已经带上新列了，
    //补列没成功的话这里会直接查失败（no such column）
    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_searchText == "legacy");
    //补上的列取默认值
    CHECK_FALSE(rows[0].m_pinned);
    CHECK(rows[0].m_pinTime == 0);

    //Pin 分类的查询也要能跑通（它在 WHERE 里直接用 pinned 列）
    HistoryFilter pinFilter = f;
    pinFilter.m_type = kHistoryTypePinned;
    CHECK(store.Count(pinFilter, 0) == 0);
}

TEST_CASE("V2S13 补列幂等：反复 Open / Close 不重复 ALTER、不报错")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s13.db");
    CreateLegacyDb(path);

    for (int i = 0; i < 3; ++i)
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        REQUIRE(store.Open(path.c_str()));   //同一实例再次 Open 也应幂等
        store.Close();
    }

    CHECK(DbHasColumn(path, "pinned"));

    //数据没被反复的补列折腾坏
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    CHECK(store.Count(f, 0) == 1);
}

TEST_CASE("V2S14 Upsert 重复落库不冲掉 Pin 标记")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s14.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "first", false, false)));

    bool hit = false;
    REQUIRE(store.SetPinned(1, 10, 1, true, 555, hit));
    CHECK(hit);

    //同一条消息再次落库（同步回填与实时推送撞上同一条时就是这个时序）：内容要更新，
    //但 Pin 标记必须原样保留 —— 这正是把 INSERT OR REPLACE 换成显式 UPSERT 的原因，
    //用 REPLACE 的话这一句会静默地把 pinned 冲成 0
    REQUIRE(store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "second", false, false)));

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_searchText == "second");   //内容确实更新了
    CHECK(rows[0].m_pinned);                   //Pin 标记还在
    CHECK(rows[0].m_pinTime == 555);
}

TEST_CASE("V2S15 SetPinned 置位 / 清除后，Pin 分类随之出现与消失")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s15.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "b", false, false));

    HistoryFilter pinFilter;
    pinFilter.m_ctype = 1;
    pinFilter.m_cid = 10;
    pinFilter.m_type = kHistoryTypePinned;

    CHECK(store.Count(pinFilter, 0) == 0);

    bool hit = false;
    REQUIRE(store.SetPinned(1, 10, 2, true, 777, hit));
    CHECK(hit);

    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(pinFilter, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_msgid == 2);
    CHECK(rows[0].m_pinned);
    CHECK(rows[0].m_pinTime == 777);

    //取消后从 Pin 分类消失，且 pin_time 一并归零（不留"没钉住却有 Pin 时间"的半截状态）
    REQUIRE(store.SetPinned(1, 10, 2, false, 0, hit));
    CHECK(hit);
    CHECK(store.Count(pinFilter, 0) == 0);

    HistoryFilter all;
    all.m_ctype = 1;
    all.m_cid = 10;
    std::vector<HistoryRecord> allRows;
    REQUIRE(store.QueryPage(all, 0, allRows));
    REQUIRE(allRows.size() == 2);
    for (size_t i = 0; i < allRows.size(); ++i)
    {
        CHECK(allRows[i].m_pinTime == 0);
    }
}

TEST_CASE("V2S16 SetPinned 对本地没有的消息返回成功但 outHit 为 false")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s16.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));

    //本机从没同步过 msgid=999 这条老消息：不算失败，但要如实告诉调用方没命中，
    //调用方据此向服务端补拉一次消息体，否则用户会"明明钉了却在 Pin 分类里找不到"
    bool hit = true;
    CHECK(store.SetPinned(1, 10, 999, true, 100, hit));
    CHECK_FALSE(hit);

    //命中时 outHit 为 true
    hit = false;
    CHECK(store.SetPinned(1, 10, 1, true, 100, hit));
    CHECK(hit);

    //msgid 非正是参数错误，与"没命中"要区分开
    hit = true;
    CHECK_FALSE(store.SetPinned(1, 10, 0, true, 100, hit));
    CHECK_FALSE(hit);
    CHECK_FALSE(store.SetPinned(1, 10, -5, true, 100, hit));
}

TEST_CASE("V2S17 SetPinned 只影响指定的 (ctype,cid,msgid)")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s17.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //单聊与群聊共用 cid=100、msgid=1（主键允许共存，见 V2S6）
    store.Upsert(MakeRecord(1, 1, 100, 10, 1, 100, "single", false, false));
    store.Upsert(MakeRecord(1, 2, 100, 20, 1, 100, "group", false, false));
    store.Upsert(MakeRecord(2, 1, 100, 10, 1, 200, "single2", false, false));

    bool hit = false;
    REQUIRE(store.SetPinned(1, 100, 1, true, 300, hit));
    CHECK(hit);

    HistoryFilter fs;
    fs.m_ctype = 1;
    fs.m_cid = 100;
    fs.m_type = kHistoryTypePinned;
    std::vector<HistoryRecord> single;
    REQUIRE(store.QueryPage(fs, 0, single));
    REQUIRE(single.size() == 1);
    CHECK(single[0].m_searchText == "single");

    //群聊里那条同 msgid 的消息不该被带上
    HistoryFilter fg;
    fg.m_ctype = 2;
    fg.m_cid = 100;
    fg.m_type = kHistoryTypePinned;
    CHECK(store.Count(fg, 0) == 0);
}

TEST_CASE("V2S18 已撤回的消息即便 pinned 为 1 也不出现在 Pin 分类里")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s18.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));
    bool hit = false;
    REQUIRE(store.SetPinned(1, 10, 1, true, 100, hit));

    HistoryFilter pinFilter;
    pinFilter.m_ctype = 1;
    pinFilter.m_cid = 10;
    pinFilter.m_type = kHistoryTypePinned;
    REQUIRE(store.Count(pinFilter, 0) == 1);

    //撤回之后自然从 Pin 分类消失 —— 所有查询恒含 recalled=0，服务端那边也会顺手删掉
    //Pin 行，故不需要为此额外推一条 pin=0 的通知
    CHECK(store.MarkRecalled(1, 10, 1));
    CHECK(store.Count(pinFilter, 0) == 0);
}

TEST_CASE("V2S19 ReplacePinnedSet 全量对账：多的清零、缺的置位、别的会话不受影响")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s19.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "b", false, false));
    store.Upsert(MakeRecord(3, 1, 10, 10, 1, 300, "c", false, false));
    store.Upsert(MakeRecord(4, 1, 20, 10, 1, 400, "other", false, false));

    bool hit = false;
    //本地现状：本会话的 1 与 2 钉着；另一个会话的 4 也钉着
    REQUIRE(store.SetPinned(1, 10, 1, true, 111, hit));
    REQUIRE(store.SetPinned(1, 10, 2, true, 222, hit));
    REQUIRE(store.SetPinned(1, 20, 4, true, 444, hit));

    //服务端说：这个会话现在钉的是 2 与 3。于是 1 要被清掉、3 要被补上
    std::vector<PinnedItem> serverPins;
    serverPins.push_back(MakePinnedItem(2, 222));
    serverPins.push_back(MakePinnedItem(3, 333));
    REQUIRE(store.ReplacePinnedSet(1, 10, serverPins, 0));

    HistoryFilter pinFilter;
    pinFilter.m_ctype = 1;
    pinFilter.m_cid = 10;
    pinFilter.m_type = kHistoryTypePinned;
    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(pinFilter, 0, rows));
    REQUIRE(rows.size() == 2);
    //时间倒序：3 在前、2 在后
    CHECK(rows[0].m_msgid == 3);
    CHECK(rows[1].m_msgid == 2);
    //Pin 时间取服务端给的那一份：3 是本地原本没有的，2 的旧值 222 也被对齐成 222
    CHECK(rows[0].m_pinTime == 333);
    CHECK(rows[1].m_pinTime == 222);

    //别的会话纹丝不动
    HistoryFilter otherFilter;
    otherFilter.m_ctype = 1;
    otherFilter.m_cid = 20;
    otherFilter.m_type = kHistoryTypePinned;
    CHECK(store.Count(otherFilter, 0) == 1);
}

TEST_CASE("V2S20 ReplacePinnedSet 带时间下界：早于下界的本地 Pin 保持不动")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s20.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //四条消息，时间依次为 100 / 200 / 300 / 400
    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "oldest", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "old", false, false));
    store.Upsert(MakeRecord(3, 1, 10, 10, 1, 300, "new", false, false));
    store.Upsert(MakeRecord(4, 1, 10, 10, 1, 400, "newest", false, false));

    bool hit = false;
    //本地四条全钉着
    for (int64_t id = 1; id <= 4; ++id)
    {
        REQUIRE(store.SetPinned(1, 10, id, true, id * 10, hit));
    }

    //模拟服务端应答被截断：只返回了最近两条（4 与 3），下界取返回集合里最老那条的
    //时间戳 300。此时 1 与 2 虽然不在返回集合里也<u>不能</u>清 —— 它们只是没被这次
    //拉取带回来，服务端那边仍然钉着。漏掉这个下界就会把它们错杀，而且只有在同一会话
    //Pin 超过单次返回上限时才暴露，日常根本测不出来
    std::vector<PinnedItem> truncated;
    truncated.push_back(MakePinnedItem(4, 40));
    truncated.push_back(MakePinnedItem(3, 30));
    REQUIRE(store.ReplacePinnedSet(1, 10, truncated, 300));

    HistoryFilter pinFilter;
    pinFilter.m_ctype = 1;
    pinFilter.m_cid = 10;
    pinFilter.m_type = kHistoryTypePinned;
    CHECK(store.Count(pinFilter, 0) == 4);

    //换成全量对账（下界 0）：这时 1 与 2 才应该被清掉
    REQUIRE(store.ReplacePinnedSet(1, 10, truncated, 0));
    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(pinFilter, 0, rows));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_msgid == 4);
    CHECK(rows[1].m_msgid == 3);
}

TEST_CASE("V2S21 ReplacePinnedSet 传空列表且下界为 0 时清空该会话全部 Pin")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s21.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "b", false, false));

    bool hit = false;
    REQUIRE(store.SetPinned(1, 10, 1, true, 100, hit));
    REQUIRE(store.SetPinned(1, 10, 2, true, 200, hit));

    std::vector<PinnedItem> none;
    REQUIRE(store.ReplacePinnedSet(1, 10, none, 0));

    HistoryFilter pinFilter;
    pinFilter.m_ctype = 1;
    pinFilter.m_cid = 10;
    pinFilter.m_type = kHistoryTypePinned;
    CHECK(store.Count(pinFilter, 0) == 0);

    //消息本身不该被删掉，只是不再钉着
    HistoryFilter all;
    all.m_ctype = 1;
    all.m_cid = 10;
    CHECK(store.Count(all, 0) == 2);
}

TEST_CASE("V2S22 ReplacePinnedSet 把 Pin 时间一并对齐成服务端给的值")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s22.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));

    //本地记的 Pin 时间是个过时值（比如推送丢过一次、后来又在别的设备上重钉了）
    bool hit = false;
    REQUIRE(store.SetPinned(1, 10, 1, true, 111, hit));

    //对账时以服务端那份为准。清零那一步会把 pin_time 一并归零（不留"没钉住却有
    //Pin 时间"的半截状态），所以置位时必须把正确的时间写回去 —— 少了这一步，
    //每对账一次就把所有 Pin 时间抹平成 0
    std::vector<PinnedItem> pins;
    pins.push_back(MakePinnedItem(1, 987654321));
    REQUIRE(store.ReplacePinnedSet(1, 10, pins, 0));

    HistoryFilter pinFilter;
    pinFilter.m_ctype = 1;
    pinFilter.m_cid = 10;
    pinFilter.m_type = kHistoryTypePinned;
    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(pinFilter, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_pinned);
    CHECK(rows[0].m_pinTime == 987654321);
}
