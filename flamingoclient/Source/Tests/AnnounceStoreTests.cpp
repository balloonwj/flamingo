/**
 *  平台公告本地库读写单元测试（Suite ANS），AnnounceStoreTests.cpp
 *
 *  用内存库（:memory:）驱动 balloonhistory::CAnnounceStore，覆盖：
 *    ANS1  写入后能原样读回，字段逐个比对；
 *    ANS2  同一公告ID重复写入是覆盖而非插入两行；
 *    ANS3  重复写入<u>不</u>冲掉已读与撤回标记（这是不能用 INSERT OR REPLACE 的原因）；
 *    ANS4  已读只进不退：服务端下发 read=1 会跟上，下发 0 不会把本地已读抹掉；
 *    ANS5  QueryAll 的排序为「置顶优先、发布时间倒序、同时间按ID倒序」；
 *    ANS6  已撤回的不出现在 QueryAll 结果里；
 *    ANS7  GetUnreadCount 只数「未撤回且未读」的；
 *    ANS8  GetMaxId 空库返回 0，且<u>包含</u>已撤回的公告；
 *    ANS9  参数非法（公告ID非正）一律拒绝写入；
 *    ANS10 库未打开时读写不崩、一律返回失败 / 零值；
 *    ANS11 存量库缺列自动补齐，且补列后旧数据还在。
 *
 *  注：CAnnounceStore 是单例，本套件各用例通过重新 Open(:memory:) 拿到干净的库
 *  ——同名重开会先关掉旧连接，内存库随之丢弃，等价于清空。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-05
 */
#include "catch_amalgamated.hpp"
#include "../BalloonHistory/AnnounceStore.h"
//sqlite3.h 由工程的附加包含目录（third-party/sqlite-src-3530100）提供，不写相对路径。
//这里直接用裸 sqlite3 API，是为了手工造一个"缺列的旧库"来验证补列机制（见 ANS11）。
#include "sqlite3.h"

#include <windows.h>
#include <string>

using namespace balloonhistory;

namespace {

//取一个干净的内存库：先关再开，确保不带上一个用例的数据。
static CAnnounceStore& FreshStore()
{
    CAnnounceStore& store = CAnnounceStore::GetInstance();
    store.Close();
    REQUIRE(store.Open(L":memory:"));
    return store;
}

//造一条公告，字段给出可辨认的值，避免"全是 0 也能通过"的假阳性。
static AnnounceRecord MakeRecord(int64_t id, int pinned = 0, int64_t sentTimeMs = 1754300000000LL)
{
    AnnounceRecord rec;
    rec.m_announcementId = id;
    rec.m_title          = "title-" + std::to_string(id);
    rec.m_body           = "body-" + std::to_string(id);
    rec.m_severity       = 1;
    rec.m_senderName     = "sender-" + std::to_string(id);
    rec.m_pinned         = pinned;
    rec.m_sentTimeMs     = sentTimeMs;
    rec.m_read           = 0;
    rec.m_recalled       = 0;
    return rec;
}

//在系统临时目录下造一个唯一的库文件路径（用于必须落盘的补列测试）。
static std::wstring MakeTempDbPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);

    std::wstring path = dir;
    path += L"flamingo_announce_test_";
    path += tag;
    path += L"_";
    path += std::to_wstring(::GetCurrentProcessId());
    path += L".db";

    //上一轮跑测试留下的同名文件会让"存量库"前提失真，先删干净。
    ::DeleteFileW(path.c_str());
    return path;
}

} // namespace

TEST_CASE("ANS1 写入后能原样读回")
{
    CAnnounceStore& store = FreshStore();

    AnnounceRecord rec = MakeRecord(10023, 1, 1754300000000LL);
    rec.m_severity = 2;
    rec.m_read     = 1;
    REQUIRE(store.Upsert(rec));

    std::vector<AnnounceRecord> all;
    REQUIRE(store.QueryAll(all));
    REQUIRE(all.size() == 1);

    CHECK(all[0].m_announcementId == 10023);
    CHECK(all[0].m_title == "title-10023");
    CHECK(all[0].m_body == "body-10023");
    CHECK(all[0].m_severity == 2);
    CHECK(all[0].m_senderName == "sender-10023");
    CHECK(all[0].m_pinned == 1);
    CHECK(all[0].m_sentTimeMs == 1754300000000LL);
    CHECK(all[0].m_read == 1);
    CHECK(all[0].m_recalled == 0);
}

TEST_CASE("ANS2 同一公告ID重复写入是覆盖而非插入两行")
{
    CAnnounceStore& store = FreshStore();

    REQUIRE(store.Upsert(MakeRecord(1)));

    //登录时全量拉一次、随后推送又来一次，同一条公告落库两遍是常态。
    AnnounceRecord updated = MakeRecord(1);
    updated.m_title = "updated";
    REQUIRE(store.Upsert(updated));

    std::vector<AnnounceRecord> all;
    REQUIRE(store.QueryAll(all));
    REQUIRE(all.size() == 1);
    CHECK(all[0].m_title == "updated");
}

TEST_CASE("ANS3 重复写入不冲掉已读与撤回标记")
{
    CAnnounceStore& store = FreshStore();

    REQUIRE(store.Upsert(MakeRecord(1)));
    REQUIRE(store.MarkRead(1));
    REQUIRE(store.Upsert(MakeRecord(2)));
    REQUIRE(store.MarkRecalled(2));

    //服务端重新下发这两条（read=0、recalled=0）。若实现用的是 INSERT OR REPLACE，
    //整行替换会把上面两个标记冲掉——用户读过的公告下次登录又变回未读，撤回的又冒出来。
    REQUIRE(store.Upsert(MakeRecord(1)));
    REQUIRE(store.Upsert(MakeRecord(2)));

    std::vector<AnnounceRecord> all;
    REQUIRE(store.QueryAll(all));
    //2 号已撤回，不出现在列表里；1 号仍在且仍是已读。
    REQUIRE(all.size() == 1);
    CHECK(all[0].m_announcementId == 1);
    CHECK(all[0].m_read == 1);

    //撤回标记也还在：未读数不该把已撤回的算进去。
    CHECK(store.GetUnreadCount() == 0);
}

TEST_CASE("ANS4 已读只进不退")
{
    CAnnounceStore& store = FreshStore();

    //本端还没读过。
    REQUIRE(store.Upsert(MakeRecord(1)));
    CHECK(store.GetUnreadCount() == 1);

    //服务端下发 read=1（在别的设备上读过），本地要跟上。
    AnnounceRecord readOnServer = MakeRecord(1);
    readOnServer.m_read = 1;
    REQUIRE(store.Upsert(readOnServer));
    CHECK(store.GetUnreadCount() == 0);

    //反过来：本地已读、服务端因为上报还没到而仍下发 0，不能把本地的已读抹掉。
    REQUIRE(store.Upsert(MakeRecord(1)));
    CHECK(store.GetUnreadCount() == 0);
}

TEST_CASE("ANS5 QueryAll 按置顶优先、时间倒序、同时间按ID倒序排列")
{
    CAnnounceStore& store = FreshStore();

    //刻意打乱写入顺序，确保验的是 SQL 的排序而不是插入顺序。
    REQUIRE(store.Upsert(MakeRecord(1, 0, 1000)));   //普通、最旧
    REQUIRE(store.Upsert(MakeRecord(2, 1, 1000)));   //置顶
    REQUIRE(store.Upsert(MakeRecord(3, 0, 3000)));   //普通、最新
    REQUIRE(store.Upsert(MakeRecord(4, 0, 2000)));   //普通、居中
    REQUIRE(store.Upsert(MakeRecord(5, 0, 3000)));   //与 3 同时间，ID 更大

    std::vector<AnnounceRecord> all;
    REQUIRE(store.QueryAll(all));
    REQUIRE(all.size() == 5);

    //置顶的排最前；其余按时间由新到旧；时间相同时ID大的在前（顺序稳定，刷新不跳动）。
    CHECK(all[0].m_announcementId == 2);
    CHECK(all[1].m_announcementId == 5);
    CHECK(all[2].m_announcementId == 3);
    CHECK(all[3].m_announcementId == 4);
    CHECK(all[4].m_announcementId == 1);
}

TEST_CASE("ANS6 已撤回的不出现在列表里")
{
    CAnnounceStore& store = FreshStore();

    REQUIRE(store.Upsert(MakeRecord(1)));
    REQUIRE(store.Upsert(MakeRecord(2)));
    REQUIRE(store.MarkRecalled(1));

    std::vector<AnnounceRecord> all;
    REQUIRE(store.QueryAll(all));
    REQUIRE(all.size() == 1);
    CHECK(all[0].m_announcementId == 2);

    //不存在的ID标记撤回是幂等操作，不算失败。
    CHECK(store.MarkRecalled(999));
}

TEST_CASE("ANS7 GetUnreadCount 只数未撤回且未读的")
{
    CAnnounceStore& store = FreshStore();

    REQUIRE(store.Upsert(MakeRecord(1)));
    REQUIRE(store.Upsert(MakeRecord(2)));
    REQUIRE(store.Upsert(MakeRecord(3)));
    CHECK(store.GetUnreadCount() == 3);

    REQUIRE(store.MarkRead(1));
    CHECK(store.GetUnreadCount() == 2);

    //撤回一条未读的：它也不该再算进未读数，否则角标上会挂着一个点不开的数字。
    REQUIRE(store.MarkRecalled(2));
    CHECK(store.GetUnreadCount() == 1);

    REQUIRE(store.MarkRead(3));
    CHECK(store.GetUnreadCount() == 0);

    //不存在的ID标记已读是幂等操作。
    CHECK(store.MarkRead(999));
    CHECK(store.GetUnreadCount() == 0);
}

TEST_CASE("ANS8 GetMaxId 空库返回 0 且包含已撤回的公告")
{
    CAnnounceStore& store = FreshStore();

    //空库返回 0，正好等于"全量拉取"的游标值。
    CHECK(store.GetMaxId() == 0);

    REQUIRE(store.Upsert(MakeRecord(10)));
    REQUIRE(store.Upsert(MakeRecord(20)));
    CHECK(store.GetMaxId() == 20);

    //已撤回的ID一样占位。漏算的话每次登录都会把这几条重新拉一遍。
    REQUIRE(store.MarkRecalled(20));
    CHECK(store.GetMaxId() == 20);
}

TEST_CASE("ANS9 公告ID非正一律拒绝写入")
{
    CAnnounceStore& store = FreshStore();

    CHECK_FALSE(store.Upsert(MakeRecord(0)));
    CHECK_FALSE(store.Upsert(MakeRecord(-1)));
    CHECK_FALSE(store.MarkRead(0));
    CHECK_FALSE(store.MarkRecalled(-5));

    std::vector<AnnounceRecord> all;
    REQUIRE(store.QueryAll(all));
    CHECK(all.empty());
}

TEST_CASE("ANS10 库未打开时读写不崩")
{
    CAnnounceStore& store = CAnnounceStore::GetInstance();
    store.Close();
    REQUIRE_FALSE(store.IsOpen());

    CHECK_FALSE(store.Upsert(MakeRecord(1)));
    CHECK_FALSE(store.MarkRead(1));
    CHECK_FALSE(store.MarkRecalled(1));

    std::vector<AnnounceRecord> all;
    CHECK_FALSE(store.QueryAll(all));
    CHECK(all.empty());

    CHECK(store.GetMaxId() == 0);
    CHECK(store.GetUnreadCount() == 0);

    //空路径参数同样安全。
    CHECK_FALSE(store.Open(NULL));
}

TEST_CASE("ANS11 存量库缺列自动补齐且旧数据还在")
{
    //建表语句是 CREATE TABLE IF NOT EXISTS，库文件已存在时它整条跳过，后加的列
    //永远不会自己出现。这里手工造一个"只有早期几列"的旧库，再用正式的 Open 打开，
    //验证缺的列被 ALTER 补上、并且旧数据没丢。必须用文件库——内存库一关就没了。
    const std::wstring dbPath = MakeTempDbPath(L"schema");

    //一、手工建一个缺列的旧库：只有主键、标题与发布时间三列。
    {
        sqlite3* raw = NULL;
        REQUIRE(sqlite3_open16(dbPath.c_str(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw,
            "CREATE TABLE t_announce ("
            "announcementid INTEGER PRIMARY KEY,"
            "title TEXT,"
            "senttime INTEGER)",
            NULL, NULL, NULL) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw,
            "INSERT INTO t_announce (announcementid,title,senttime) VALUES (7,'old-row',555)",
            NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }

    //二、用正式实现打开这个旧库：Open 里的 EnsureColumnsLocked 应当把缺的列补齐。
    CAnnounceStore& store = CAnnounceStore::GetInstance();
    store.Close();
    REQUIRE(store.Open(dbPath.c_str()));

    //三、读得出来就说明补列成功——缺列时这条 SELECT 会因 no such column 整个失败。
    std::vector<AnnounceRecord> all;
    REQUIRE(store.QueryAll(all));
    REQUIRE(all.size() == 1);
    CHECK(all[0].m_announcementId == 7);
    //旧数据还在。
    CHECK(all[0].m_title == "old-row");
    CHECK(all[0].m_sentTimeMs == 555);
    //补出来的列取默认值，不是垃圾值。
    CHECK(all[0].m_severity == 0);
    CHECK(all[0].m_pinned == 0);
    CHECK(all[0].m_read == 0);
    CHECK(all[0].m_recalled == 0);
    CHECK(all[0].m_body.empty());
    CHECK(all[0].m_senderName.empty());

    //四、补列后的库能正常写读。
    REQUIRE(store.Upsert(MakeRecord(8)));
    REQUIRE(store.MarkRead(7));
    CHECK(store.GetUnreadCount() == 1);
    CHECK(store.GetMaxId() == 8);

    store.Close();
    ::DeleteFileW(dbPath.c_str());
}
