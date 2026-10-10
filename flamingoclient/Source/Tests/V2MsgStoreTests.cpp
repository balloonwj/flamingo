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
#include "../UI/BalloonHistory/V2MsgStore.h"
//Pin 那几个用例要自己建一个"旧结构"的库来验证补列，故直接用 sqlite3 的 C 接口，
//不经产品代码 —— 拿产品代码建出来的库已经带上新列，那样测等于什么都没验证。
#include "sqlite3.h"
#include <algorithm>   //CS2：对账取数的结果顺序不限，比对前先排序
#include <string>
#include <windows.h>

using namespace balloonhistory;

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

//取一个临时目录下的 db 全路径，并先删掉同名旧文件，保证每个用例从空库开始。
//@param name  文件名（各用例传不同名，避免跨用例文件占用冲突）
static std::wstring FreshTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    //文件名里带上进程号：两份测试进程同时跑（命令行一份 + VS 里一份）时，
    //固定文件名会让它们共用同一个文件，先起的占着、后起的打开就失败，
    //表现为看似偶发、复跑又好了的失败。同 HistoryModelTests::FreshTempDb。
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

// =====================================================================
// LD1~LD4、LDB1 —— 本地库删除接口（清理聊天记录，见 docs/清理聊天记录方案.md §7.1）
// =====================================================================

//统计某会话在本地库里的全部可见消息条数（不带任何类型 / 时间 / 关键字过滤）
static int64_t CountConversation(CV2MsgStore& store, int ctype, int64_t cid)
{
    HistoryFilter f;
    f.m_ctype = ctype;
    f.m_cid = cid;
    return store.Count(f, 0);
}

TEST_CASE("LD1 按 msgid 删除只动指定会话里的指定行", "[msgdelete]")
{
    std::wstring path = FreshTempDb(L"flamingo_ld1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //单聊 10 三条；单聊 20 有一条 msgid 与之相同的消息 —— 主键含会话，删 10 的那条不能误伤它
    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "b", false, false));
    store.Upsert(MakeRecord(3, 1, 10, 10, 1, 300, "c", false, false));
    store.Upsert(MakeRecord(2, 1, 20, 20, 1, 200, "d", false, false));

    //99 在本地根本没有、0 是非法 msgid：都不算失败，也不计入删除条数
    std::vector<int64_t> ids;
    ids.push_back(2);
    ids.push_back(99);
    ids.push_back(0);
    int deleted = -1;
    REQUIRE(store.DeleteMessages(1, 10, ids, deleted));
    CHECK(deleted == 1);

    CHECK(CountConversation(store, 1, 10) == 2);
    CHECK(CountConversation(store, 1, 20) == 1);
}

TEST_CASE("LD2 按时间范围分批删除：跨会话、含两端、每批不超过上限、删到 0 为止", "[msgdelete]")
{
    std::wstring path = FreshTempDb(L"flamingo_ld2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //三个会话，时间铺在 100~500；落在 [200, 300] 的共 5 条
    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a1", false, false));
    store.Upsert(MakeRecord(2, 1, 10, 10, 1, 200, "a2", false, false));
    store.Upsert(MakeRecord(3, 1, 10, 10, 1, 300, "a3", false, false));
    store.Upsert(MakeRecord(4, 1, 20, 20, 1, 200, "b1", false, false));
    store.Upsert(MakeRecord(5, 1, 20, 20, 1, 300, "b2", false, false));
    store.Upsert(MakeRecord(6, 1, 20, 20, 1, 400, "b3", false, false));
    store.Upsert(MakeRecord(7, 2, 30, 11, 1, 300, "g1", false, false));
    store.Upsert(MakeRecord(8, 2, 30, 12, 1, 500, "g2", false, false));

    //每批最多 2 行：应当是 2、2、1，第四次为 0
    const int kBatch = 2;
    int total = 0;
    int rounds = 0;
    const int kMaxRounds = 10;   //防止实现有误时死循环
    while (rounds < kMaxRounds)
    {
        int deleted = -1;
        REQUIRE(store.DeleteByTimeRangeBatch(200, 300, kBatch, deleted));
        CHECK(deleted >= 0);
        CHECK(deleted <= kBatch);
        ++rounds;
        if (deleted == 0)
            break;
        total += deleted;
    }
    CHECK(total == 5);
    CHECK(rounds == 4);

    //区间外的三条都还在，区间内的一条都查不到
    CHECK(CountConversation(store, 1, 10) == 1);
    CHECK(CountConversation(store, 1, 20) == 1);
    CHECK(CountConversation(store, 2, 30) == 1);

    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    std::vector<HistoryRecord> rows;
    REQUIRE(store.QueryPage(f, 0, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_msgid == 1);
}

TEST_CASE("LD3 批大小非正时不删任何行", "[msgdelete]")
{
    std::wstring path = FreshTempDb(L"flamingo_ld3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    store.Upsert(MakeRecord(1, 1, 10, 10, 1, 100, "a", false, false));

    int deleted = -1;
    REQUIRE(store.DeleteByTimeRangeBatch(0, 1000, 0, deleted));
    CHECK(deleted == 0);
    CHECK(CountConversation(store, 1, 10) == 1);
}

TEST_CASE("LD4 数据库未打开时两个删除接口都返回失败", "[msgdelete]")
{
    CV2MsgStore store;
    std::vector<int64_t> ids;
    ids.push_back(1);
    int deleted = -1;
    CHECK_FALSE(store.DeleteMessages(1, 10, ids, deleted));
    CHECK_FALSE(store.DeleteByTimeRangeBatch(0, 1000, 10, deleted));
}

//LDB1 用：造一份 totalRows 条消息的库，按 batch 行一批删掉较早的 90%，打印单批最长与总耗时。
//@param totalRows      造多少条消息
//@param batch          每批删除的行数
//@param conversations  消息均匀分布到多少个会话里
static void RunRangeDeleteBench(int totalRows, int batch, int conversations)
{
    const int kTotalRows = totalRows;
    const int kBatch = batch;
    const int kConversations = conversations;

    std::wstring path = FreshTempDb(L"flamingo_ldb1.db");
    {
        //先经产品代码建表建索引，再用 SQLite 的 C 接口在一个事务里灌数据：
        //逐条 Upsert 各自一个隐式事务，灌 10 万条要等很久，而灌数据不是被测对象
        CV2MsgStore creator;
        REQUIRE(creator.Open(path.c_str()));
    }
    {
        sqlite3* db = NULL;
        const std::string pathUtf8 = WidePathToUtf8(path);
        REQUIRE(sqlite3_open(pathUtf8.c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_stmt* stmt = NULL;
        REQUIRE(sqlite3_prepare_v2(db,
            "INSERT INTO t_msg(msgid,ctype,cid,fromid,msgtype,content,search_text,timestamp) "
            "VALUES(?,1,?,1,1,'{}','bench',?)", -1, &stmt, NULL) == SQLITE_OK);
        for (int i = 1; i <= kTotalRows; ++i)
        {
            sqlite3_bind_int64(stmt, 1, i);
            sqlite3_bind_int64(stmt, 2, 10 + (i % kConversations));
            sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(i) * 1000);
            REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        REQUIRE(sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(db);
    }

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //清理较早的 90%，与服务端 MDB1 的口径一致
    const int64_t endMs = static_cast<int64_t>(kTotalRows) * 9 / 10 * 1000;
    double maxBatchMs = 0;
    double totalMs = 0;
    int totalDeleted = 0;
    int batches = 0;
    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    while (true)
    {
        LARGE_INTEGER t0;
        LARGE_INTEGER t1;
        ::QueryPerformanceCounter(&t0);
        int deleted = 0;
        REQUIRE(store.DeleteByTimeRangeBatch(0, endMs, kBatch, deleted));
        ::QueryPerformanceCounter(&t1);
        const double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        totalMs += ms;
        if (ms > maxBatchMs)
            maxBatchMs = ms;
        if (deleted == 0)
            break;
        totalDeleted += deleted;
        ++batches;
    }

    WARN("LDB1: deleted " << totalDeleted << " rows in " << batches << " batches of " << kBatch
         << ", total " << totalMs << " ms, max single batch " << maxBatchMs << " ms, average "
         << (batches > 0 ? totalMs / batches : 0) << " ms");
    CHECK(totalDeleted == kTotalRows * 9 / 10);
}

TEST_CASE("LDB1 本地库 10 万条消息按时间范围分批删除的单批耗时", "[.][msgdeletebench]")
{
    //方案 §12 的目标：单批不超过 50 毫秒。依次测三种批大小，据结果定产品代码里的批大小
    const int kTotalRows = 100000;
    const int kConversations = 200;
    const int kBatchSizes[] = { 500, 1000, 2000 };
    for (size_t i = 0; i < sizeof(kBatchSizes) / sizeof(kBatchSizes[0]); ++i)
    {
        RunRangeDeleteBench(kTotalRows, kBatchSizes[i], kConversations);
    }
}

// =====================================================================
// CS1~CS2 —— 覆盖区间的读写与对账取数（规则见 UI/BalloonHistory/HistoryCoverage.h）
// =====================================================================

//造一个已建立的覆盖区间
static CoverRange MakeStoredCover(int64_t lo, int64_t hi, bool reachedOldest)
{
    CoverRange c;
    c.m_valid = true;
    c.m_lo = lo;
    c.m_hi = hi;
    c.m_reachedOldest = reachedOldest;
    return c;
}

TEST_CASE("CS1 覆盖区间写入后读回一致，不同会话互不影响，全部清空后都没有区间", "[coverage]")
{
    std::wstring path = FreshTempDb(L"flamingo_cs1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //从没写过：读回未建立
    CoverRange got;
    REQUIRE(store.GetCoverage(1, 10, got));
    CHECK_FALSE(got.m_valid);

    //单聊 10 与群 10 同 cid 不同 ctype，各写各的
    REQUIRE(store.SetCoverage(1, 10, MakeStoredCover(500, 950, false)));
    REQUIRE(store.SetCoverage(2, 10, MakeStoredCover(0, 300, true)));

    REQUIRE(store.GetCoverage(1, 10, got));
    REQUIRE(got.m_valid);
    CHECK(got.m_lo == 500);
    CHECK(got.m_hi == 950);
    CHECK_FALSE(got.m_reachedOldest);

    REQUIRE(store.GetCoverage(2, 10, got));
    REQUIRE(got.m_valid);
    CHECK(got.m_lo == 0);
    CHECK(got.m_hi == 300);
    CHECK(got.m_reachedOldest);

    //覆盖写入
    REQUIRE(store.SetCoverage(1, 10, MakeStoredCover(100, 1000, false)));
    REQUIRE(store.GetCoverage(1, 10, got));
    CHECK(got.m_lo == 100);
    CHECK(got.m_hi == 1000);

    //写入未建立的区间等于删掉该会话的记录
    REQUIRE(store.SetCoverage(2, 10, CoverRange()));
    REQUIRE(store.GetCoverage(2, 10, got));
    CHECK_FALSE(got.m_valid);

    //全部清空
    REQUIRE(store.ClearAllCoverage());
    REQUIRE(store.GetCoverage(1, 10, got));
    CHECK_FALSE(got.m_valid);

    //数据库未打开时各接口都返回失败
    CV2MsgStore closed;
    CHECK_FALSE(closed.GetCoverage(1, 10, got));
    CHECK_FALSE(closed.SetCoverage(1, 10, MakeStoredCover(1, 2, false)));
    CHECK_FALSE(closed.ClearAllCoverage());
}

TEST_CASE("CS2 对账取数只返回指定会话、指定 msgid 范围内的行", "[coverage]")
{
    std::wstring path = FreshTempDb(L"flamingo_cs2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    store.Upsert(MakeRecord(100, 1, 10, 10, 1, 1000, "a", false, false));
    store.Upsert(MakeRecord(150, 1, 10, 10, 1, 1500, "b", false, false));
    store.Upsert(MakeRecord(200, 1, 10, 10, 1, 2000, "c", false, false));
    store.Upsert(MakeRecord(250, 1, 10, 10, 1, 2500, "d", false, false));
    //另一个会话里落在同一范围内的行不能混进来
    store.Upsert(MakeRecord(160, 1, 20, 20, 1, 1600, "e", false, false));
    //已撤回的行同样要取出来：对账时服务端也会返回已撤回的消息，二者口径一致
    store.Upsert(MakeRecord(180, 1, 10, 10, 1, 1800, "f", false, true));

    std::vector<int64_t> ids;
    REQUIRE(store.QueryMsgidsInRange(1, 10, 150, 200, ids));
    std::sort(ids.begin(), ids.end());
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == 150);
    CHECK(ids[1] == 180);
    CHECK(ids[2] == 200);

    CV2MsgStore closed;
    CHECK_FALSE(closed.QueryMsgidsInRange(1, 10, 0, 1000, ids));
}

TEST_CASE("CS4 删除某会话的全部消息与覆盖区间，其它会话不受影响", "[coverage][groupexit]")
{
    std::wstring path = FreshTempDb(L"flamingo_cs4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //群 10 三条、单聊 10 一条（同 cid 不同 ctype）、群 20 一条
    store.Upsert(MakeRecord(100, 2, 10, 7, 1, 1000, "a", false, false));
    store.Upsert(MakeRecord(110, 2, 10, 8, 1, 1100, "b", false, false));
    store.Upsert(MakeRecord(120, 2, 10, 7, 1, 1200, "c", false, true));
    store.Upsert(MakeRecord(130, 1, 10, 10, 1, 1300, "d", false, false));
    store.Upsert(MakeRecord(140, 2, 20, 7, 1, 1400, "e", false, false));
    REQUIRE(store.SetCoverage(2, 10, MakeStoredCover(100, 120, true)));
    REQUIRE(store.SetCoverage(1, 10, MakeStoredCover(130, 130, true)));

    int deleted = -1;
    REQUIRE(store.DeleteConversation(2, 10, deleted));
    CHECK(deleted == 3);

    std::vector<int64_t> ids;
    REQUIRE(store.QueryMsgidsInRange(2, 10, 0, 1000000, ids));
    CHECK(ids.empty());
    CoverRange got;
    REQUIRE(store.GetCoverage(2, 10, got));
    CHECK_FALSE(got.m_valid);

    //单聊 10 与群 20 都还在
    REQUIRE(store.QueryMsgidsInRange(1, 10, 0, 1000000, ids));
    CHECK(ids.size() == 1);
    REQUIRE(store.GetCoverage(1, 10, got));
    CHECK(got.m_valid);
    REQUIRE(store.QueryMsgidsInRange(2, 20, 0, 1000000, ids));
    CHECK(ids.size() == 1);

    //再删一次：本来就没有数据，照样成功，删 0 行
    REQUIRE(store.DeleteConversation(2, 10, deleted));
    CHECK(deleted == 0);

    //数据库未打开时返回失败
    CV2MsgStore closed;
    CHECK_FALSE(closed.DeleteConversation(2, 10, deleted));
    CHECK(deleted == 0);
}

TEST_CASE("CS5 按清空锚点删除某会话的本地消息：只删不大于锚点的，覆盖区间清掉，其它会话不受影响", "[coverage][clearconv]")
{
    std::wstring path = FreshTempDb(L"flamingo_cs5.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //单聊 10：三条；群 10（同 cid 不同 ctype）：一条
    store.Upsert(MakeRecord(100, 1, 10, 10, 1, 1000, "a", false, false));
    store.Upsert(MakeRecord(110, 1, 10, 7, 1, 1100, "b", false, false));
    store.Upsert(MakeRecord(120, 1, 10, 10, 1, 1200, "c", false, false));
    store.Upsert(MakeRecord(130, 2, 10, 7, 1, 1300, "d", false, false));
    REQUIRE(store.SetCoverage(1, 10, MakeStoredCover(100, 120, true)));

    //锚点 110：100、110 删掉，120 留下（清空之后才到的消息）
    int deleted = -1;
    REQUIRE(store.DeleteConversationUpTo(1, 10, 110, deleted));
    CHECK(deleted == 2);
    std::vector<int64_t> ids;
    REQUIRE(store.QueryMsgidsInRange(1, 10, 0, 1000000, ids));
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 120);
    //覆盖区间作废：下次打开消息记录窗时从服务端重新验证
    CoverRange got;
    REQUIRE(store.GetCoverage(1, 10, got));
    CHECK_FALSE(got.m_valid);
    //群 10 不受影响
    REQUIRE(store.QueryMsgidsInRange(2, 10, 0, 1000000, ids));
    CHECK(ids.size() == 1);

    //锚点为 0（清空的是一个没有消息的会话）：一条都不删
    REQUIRE(store.DeleteConversationUpTo(1, 10, 0, deleted));
    CHECK(deleted == 0);

    //数据库未打开时返回失败
    CV2MsgStore closed;
    CHECK_FALSE(closed.DeleteConversationUpTo(1, 10, 110, deleted));
}

TEST_CASE("CS3 区间内最早一条的时间：只看指定会话、msgid 不小于下界的行", "[coverage]")
{
    std::wstring path = FreshTempDb(L"flamingo_cs3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //msgid 与时间不一定同序（服务端时钟回拨、或手工造的数据），取的是时间的最小值
    store.Upsert(MakeRecord(100, 1, 10, 10, 1, 1000, "a", false, false));
    store.Upsert(MakeRecord(200, 1, 10, 10, 1, 2000, "b", false, false));
    store.Upsert(MakeRecord(300, 1, 10, 10, 1, 1500, "c", false, false));
    //另一个会话里更早的行不能算进来
    store.Upsert(MakeRecord(250, 1, 20, 20, 1, 500, "d", false, false));

    int64_t ts = -1;
    bool found = false;
    REQUIRE(store.QueryOldestTimestampFrom(1, 10, 150, ts, found));
    CHECK(found);
    CHECK(ts == 1500);

    //下界以上一行都没有
    REQUIRE(store.QueryOldestTimestampFrom(1, 10, 400, ts, found));
    CHECK_FALSE(found);

    CV2MsgStore closed;
    CHECK_FALSE(closed.QueryOldestTimestampFrom(1, 10, 0, ts, found));
}

// =====================================================================
// HS2 —— 消息记录窗按关键字搜索的耗时基准（默认不运行，标签 [.][historysearchbench]）
//
// 搜索走 search_text LIKE '%关键字%'，前导通配符用不上索引，只能在该会话的行里逐行比对。
// 本基准在一个会话里造 10 万条消息，分别测"关键字命中约 1%""关键字一条都不命中""不带关键字"
// 三种情况下统计条数（Count）与取第一页（QueryPage，每页 30 条）的耗时，各跑 5 次取平均。
// 只测量、不断言耗时：数字报给用户，再决定要不要改用全文索引。
// =====================================================================

namespace {

// 基准里每种查询重复的次数，取平均以减小抖动
const int kSearchBenchRepeat = 5;

// 跑一种查询 kSearchBenchRepeat 次，返回 Count 与 QueryPage 各自的平均耗时（毫秒）以及统计到的条数
void RunSearchBench(CV2MsgStore& store, const char* keywordUtf8, double& countMs, double& pageMs,
                    int64_t& total)
{
    HistoryFilter f;
    f.m_ctype = 1;
    f.m_cid = 10;
    f.m_keyword = keywordUtf8;
    f.m_page = 1;
    f.m_pageSize = 30;

    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    double sumCount = 0;
    double sumPage = 0;
    for (int i = 0; i < kSearchBenchRepeat; ++i)
    {
        LARGE_INTEGER t0;
        LARGE_INTEGER t1;
        LARGE_INTEGER t2;
        ::QueryPerformanceCounter(&t0);
        total = store.Count(f, 0);
        ::QueryPerformanceCounter(&t1);
        std::vector<HistoryRecord> page;
        REQUIRE(store.QueryPage(f, 0, page));
        ::QueryPerformanceCounter(&t2);
        sumCount += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        sumPage += static_cast<double>(t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;
    }
    countMs = sumCount / kSearchBenchRepeat;
    pageMs = sumPage / kSearchBenchRepeat;
}

} // anonymous namespace

TEST_CASE("HS2 一个会话 10 万条消息时按关键字搜索的耗时", "[.][historysearchbench]")
{
    const int kTotalRows = 100000;
    // 每隔多少条放一条带关键字的消息（命中约 1%）
    const int kHitEvery = 100;

    std::wstring path = FreshTempDb(L"flamingo_hs2.db");
    {
        CV2MsgStore creator;
        REQUIRE(creator.Open(path.c_str()));
    }
    {
        // 与 LDB1 相同：经 SQLite 的 C 接口在一个事务里灌数据，灌数据不是被测对象
        sqlite3* db = NULL;
        const std::string pathUtf8 = WidePathToUtf8(path);
        REQUIRE(sqlite3_open(pathUtf8.c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_stmt* stmt = NULL;
        REQUIRE(sqlite3_prepare_v2(db,
            "INSERT INTO t_msg(msgid,ctype,cid,fromid,msgtype,content,search_text,timestamp) "
            "VALUES(?,1,10,1,1,'{}',?,?)", -1, &stmt, NULL) == SQLITE_OK);
        // 正文取一段常见长度的中文（UTF-8），每隔 kHitEvery 条带上关键字"测试环境"
        const std::string plain = "这一版的改动比较多，主要集中在消息列表和输入区两块，大家回归的时候重点看一下。";
        const std::string withHit = "测试环境的数据库我重置了一下，账号还是原来那几个。" + plain;
        for (int i = 1; i <= kTotalRows; ++i)
        {
            const std::string& text = (i % kHitEvery == 0) ? withHit : plain;
            sqlite3_bind_int64(stmt, 1, i);
            sqlite3_bind_text(stmt, 2, text.c_str(), (int)text.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(i) * 1000);
            REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        REQUIRE(sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(db);
    }

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    double countMs = 0;
    double pageMs = 0;
    int64_t total = 0;

    RunSearchBench(store, "", countMs, pageMs, total);
    WARN("HS2 no keyword: total " << total << ", count " << countMs << " ms, first page " << pageMs << " ms");
    CHECK(total == kTotalRows);

    RunSearchBench(store, "测试环境", countMs, pageMs, total);
    WARN("HS2 keyword hits 1%: total " << total << ", count " << countMs << " ms, first page " << pageMs << " ms");
    CHECK(total == kTotalRows / kHitEvery);

    RunSearchBench(store, "一条都不会命中的关键字", countMs, pageMs, total);
    WARN("HS2 keyword hits none: total " << total << ", count " << countMs << " ms, first page " << pageMs << " ms");
    CHECK(total == 0);
}

//==================== 群接龙 2c：按主键取一行、只改 content 列 ====================

TEST_CASE("V2S-RELAY1 QueryRecord 按主键取一行，取不到时 found 为 false", "[grouprelay]")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s_relay1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    HistoryRecord rec = MakeRecord(9001, 2, 600001, 100001, 11, 1759400000000LL, u8"周五团建报名", false, false);
    rec.m_clientmsgid = "relay-1";
    rec.m_content     = "{\"relay\":{\"title\":\"t\"}}";
    rec.m_raw         = "{\"msgid\":\"9001\"}";
    REQUIRE(store.Upsert(rec));

    //取到：各字段与写入时一致
    HistoryRecord got;
    bool found = false;
    REQUIRE(store.QueryRecord(2, 600001, 9001, got, found));
    CHECK(found);
    CHECK(got.m_msgid == 9001);
    CHECK(got.m_clientmsgid == "relay-1");
    CHECK(got.m_ctype == 2);
    CHECK(got.m_cid == 600001);
    CHECK(got.m_fromid == 100001);
    CHECK(got.m_msgtype == 11);
    CHECK(got.m_content == rec.m_content);
    CHECK(got.m_searchText == rec.m_searchText);
    CHECK(got.m_timestamp == 1759400000000LL);
    CHECK(got.m_raw == rec.m_raw);

    //msgid 不存在、会话不对：执行成功但 found 为 false，出参不被改写
    HistoryRecord untouched;
    untouched.m_msgid = 42;
    found = true;
    REQUIRE(store.QueryRecord(2, 600001, 9999, untouched, found));
    CHECK_FALSE(found);
    CHECK(untouched.m_msgid == 42);
    found = true;
    REQUIRE(store.QueryRecord(2, 600002, 9001, untouched, found));
    CHECK_FALSE(found);
    found = true;
    REQUIRE(store.QueryRecord(1, 600001, 9001, untouched, found));
    CHECK_FALSE(found);

    //msgid 非正：返回 false
    CHECK_FALSE(store.QueryRecord(2, 600001, 0, untouched, found));
    store.Close();
}

TEST_CASE("V2S-RELAY2 UpdateContent 只改 content 列，其余列不动", "[grouprelay]")
{
    std::wstring path = FreshTempDb(L"flamingo_v2s_relay2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    HistoryRecord rec = MakeRecord(9002, 2, 600001, 100001, 11, 1759400000000LL, "search me", false, false);
    rec.m_content = "{\"relay\":{\"title\":\"old\"}}";
    rec.m_raw     = "{\"msgid\":\"9002\",\"content\":{\"title\":\"old\"}}";
    REQUIRE(store.Upsert(rec));
    bool hit = false;
    REQUIRE(store.SetPinned(2, 600001, 9002, true, 1759400001000LL, hit));
    REQUIRE(hit);

    //命中：content 换成新值
    const std::string newContent = "{\"relay\":{\"title\":\"old\",\"state\":1}}";
    hit = false;
    REQUIRE(store.UpdateContent(2, 600001, 9002, newContent, hit));
    CHECK(hit);

    HistoryRecord got;
    bool found = false;
    REQUIRE(store.QueryRecord(2, 600001, 9002, got, found));
    REQUIRE(found);
    CHECK(got.m_content == newContent);
    //raw、search_text、Pin、撤回都不动
    CHECK(got.m_raw == rec.m_raw);
    CHECK(got.m_searchText == "search me");
    CHECK(got.m_pinned);
    CHECK(got.m_pinTime == 1759400001000LL);
    CHECK_FALSE(got.m_recalled);

    //没命中（本机没有这条）：执行成功，hit 为 false
    hit = true;
    REQUIRE(store.UpdateContent(2, 600001, 9999, newContent, hit));
    CHECK_FALSE(hit);

    //msgid 非正：返回 false
    CHECK_FALSE(store.UpdateContent(2, 600001, -1, newContent, hit));
    store.Close();
}
