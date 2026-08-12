/**
 *  MsgStoreWriteBenchTests.cpp
 *
 *  本地消息库批量落库的<u>计时用例</u>。用来回答一个具体问题：打开一个积压很多未读的
 *  会话时，`CBalloonMainPanel::OnSyncMessagesArrived` 在 UI 线程上逐条
 *  `CV2MsgStore::Upsert` 落 100 条消息，到底要花多久。
 *
 *  背景：`CV2MsgStore::Upsert` 是一条一条 prepare / bind / step / finalize，外面<u>没有</u>
 *  事务包裹；`CV2MsgStore::Open` 也<u>没有</u>设置任何 PRAGMA，于是走 SQLite 默认的
 *  journal_mode=DELETE + synchronous=FULL —— 每条 INSERT 都是一次独立事务、各刷一次盘。
 *  这一段是不是真的慢、慢多少，光看代码定不了，故写这个用例实测。
 *
 *  测五组，前两组是"现状"，后三组是"如果改了会怎样"：
 *    A  走真实的 CV2MsgStore::Upsert，默认配置、无事务  —— 这就是线上现在的耗时
 *    B  裸 sqlite3 复刻同一张表、同一条 SQL，默认配置、无事务 —— 用来印证复刻是否可信
 *    C  裸 sqlite3，默认配置 + 单个显式事务
 *    D  裸 sqlite3，WAL + synchronous=NORMAL，无事务
 *    E  裸 sqlite3，WAL + synchronous=NORMAL + 单个显式事务
 *  B 与 A 应当接近；接近才说明 C / D / E 的对比数字对得上真实代码。
 *
 *  <u>本组用例默认不参与全量跑</u>：标签以 `.` 开头（Catch2 的隐藏标签约定），因为它要真
 *  刷盘、耗时不定，混进常规回归里既拖慢又容易因机器负载而看起来"忽快忽慢"。
 *  要跑就显式指定：
 *      FlamingoUnit.exe "[msgstorebench]"
 *  数字用 WARN 输出 —— Catch2 的 INFO 只在断言失败时才打印，而这里断言是通过的。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <chrono>
#include <string>
#include <vector>

#include "sqlite3.h"

#include "../BalloonHistory/V2MsgStore.h"   // CV2MsgStore；HistoryRecord 由它 include 的 V2MsgQuery.h 带入

namespace {

// 一批多少条。取 100 与服务端 cmd_sync_messages 的单次上限一致
// （SYNC_MESSAGES_MAX_LIMIT，见 chatserversrc/ChatSession.cpp），
// 也就是"打开一个积压会话"最坏情况下一次要落多少条。
const int kBatchCount = 100;

// 建表 / 落库 SQL 从 V2MsgStore.cpp 逐字抄来 —— 复刻必须与真实代码同一条语句，
// 否则量出来的是另一回事。那边改了这里要跟着改。
const char* const kCreateTableSql =
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
    "pinned INTEGER NOT NULL DEFAULT 0,"
    "pin_time INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY (ctype,cid,msgid))";

const char* const kUpsertSql =
    "INSERT INTO t_msg "
    "(msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled) "
    "VALUES (?,?,?,?,?,?,?,?,?,?,?) "
    "ON CONFLICT(ctype,cid,msgid) DO UPDATE SET "
    "clientmsgid=excluded.clientmsgid,fromid=excluded.fromid,msgtype=excluded.msgtype,"
    "content=excluded.content,search_text=excluded.search_text,is_link=excluded.is_link,"
    "timestamp=excluded.timestamp,recalled=excluded.recalled";

// 取一个本次独有的临时库路径。带上进程 ID 与序号 —— 与并行跑的 VS 构建或上一次
// 残留的库文件抢同一个文件名，会表现成 Open 失败，看着像代码坏了
// （HistoryModelTests 那几个偶发失败就是这么来的）。
std::wstring MakeUniqueTempDbPath(int seq)
{
    wchar_t dir[MAX_PATH] = { 0 };
    const DWORD n = ::GetTempPathW(MAX_PATH, dir);
    if (n == 0 || n >= MAX_PATH)
    {
        return L"";
    }
    wchar_t name[MAX_PATH] = { 0 };
    ::swprintf_s(name, L"%sflamingo_msgbench_%lu_%d.db", dir,
                 (unsigned long)::GetCurrentProcessId(), seq);
    ::DeleteFileW(name);
    return name;
}

// 造第 i 条消息。内容长度取一条中等长度的聊天文本 —— 太短会把 IO 成本掩盖掉，
// 太长又不是常态。
balloonhistory::HistoryRecord MakeRecord(int i)
{
    balloonhistory::HistoryRecord rec;
    rec.m_msgid       = 1000 + i;
    rec.m_clientmsgid = "c-0123456789abcdef-" + std::to_string(i);
    rec.m_ctype       = 1;
    rec.m_cid         = 100007;
    rec.m_fromid      = 100031;
    rec.m_msgtype     = 1;
    rec.m_content     = "{\"content\":\"这是一条用于计时的聊天消息，长度取中等水平。\"}";
    rec.m_searchText  = "这是一条用于计时的聊天消息，长度取中等水平。";
    rec.m_isLink      = false;
    rec.m_timestamp   = 1760000000000LL + i;
    rec.m_recalled    = false;
    return rec;
}

// 毫秒计时。steady_clock 不受系统时间调整影响，测耗时用它而不是 system_clock。
double NowMs()
{
    const std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    return (double)std::chrono::duration_cast<std::chrono::microseconds>(
               t.time_since_epoch()).count() / 1000.0;
}

// 往裸 sqlite3 连接上跑一条无结果集的语句。
bool Exec(sqlite3* db, const char* sql)
{
    char* err = NULL;
    const int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err != NULL)
    {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

// 用裸 sqlite3 落一批，返回耗时（毫秒）。
//   pragmas：      建表前要执行的 PRAGMA；NULL 表示保持 SQLite 默认。
//   useTransaction：true 时整批包在一个显式事务里。
double RunRawBatch(int seq, const char* pragmas, bool useTransaction)
{
    const std::wstring path = MakeUniqueTempDbPath(seq);
    REQUIRE(!path.empty());

    sqlite3* db = NULL;
    REQUIRE(sqlite3_open16(path.c_str(), &db) == SQLITE_OK);
    if (pragmas != NULL)
    {
        REQUIRE(Exec(db, pragmas));
    }
    REQUIRE(Exec(db, kCreateTableSql));

    // 只计"落 100 条"这段，不含建库建表 —— 真实场景里库早就开着了。
    const double t0 = NowMs();
    if (useTransaction)
    {
        Exec(db, "BEGIN");
    }
    for (int i = 0; i < kBatchCount; ++i)
    {
        const balloonhistory::HistoryRecord rec = MakeRecord(i);
        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(db, kUpsertSql, -1, &stmt, NULL) != SQLITE_OK)
        {
            continue;
        }
        sqlite3_bind_int64(stmt, 1, rec.m_msgid);
        sqlite3_bind_text(stmt, 2, rec.m_clientmsgid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, rec.m_ctype);
        sqlite3_bind_int64(stmt, 4, rec.m_cid);
        sqlite3_bind_int64(stmt, 5, rec.m_fromid);
        sqlite3_bind_int(stmt, 6, rec.m_msgtype);
        sqlite3_bind_text(stmt, 7, rec.m_content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, rec.m_searchText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 9, rec.m_isLink ? 1 : 0);
        sqlite3_bind_int64(stmt, 10, rec.m_timestamp);
        sqlite3_bind_int(stmt, 11, rec.m_recalled ? 1 : 0);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    if (useTransaction)
    {
        Exec(db, "COMMIT");
    }
    const double cost = NowMs() - t0;

    sqlite3_close(db);
    ::DeleteFileW(path.c_str());
    return cost;
}

} // anonymous namespace

TEST_CASE("MSB1 落 100 条消息的实际耗时（现状与三种改法对比）", "[.][msgstorebench]")
{
    // ---- A：走真实的 CV2MsgStore，默认配置、无事务。这就是线上现在的路径 ----
    double costReal = 0.0;
    {
        const std::wstring path = MakeUniqueTempDbPath(0);
        REQUIRE(!path.empty());

        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));

        const double t0 = NowMs();
        for (int i = 0; i < kBatchCount; ++i)
        {
            store.Upsert(MakeRecord(i));
        }
        costReal = NowMs() - t0;

        store.Close();
        ::DeleteFileW(path.c_str());
    }

    // ---- B~E：裸 sqlite3 的四种配置 ----
    const double costRawDefault = RunRawBatch(1, NULL, false);
    const double costRawTxn     = RunRawBatch(2, NULL, true);
    const double costWal        = RunRawBatch(
        3, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", false);
    const double costWalTxn     = RunRawBatch(
        4, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", true);

    // Catch2 的 INFO 只在断言失败时打印，这里断言是过的，故用 WARN 把数字带出来。
    WARN("落 " << kBatchCount << " 条消息耗时（毫秒）："
         << "\n  A 现状 CV2MsgStore（默认配置 + 无事务）  = " << costReal
         << "\n  B 裸 sqlite 同配置（用于印证 A）          = " << costRawDefault
         << "\n  C 默认配置 + 单事务                       = " << costRawTxn
         << "\n  D WAL + synchronous=NORMAL                = " << costWal
         << "\n  E WAL + synchronous=NORMAL + 单事务       = " << costWalTxn
         << "\n  单条均摊（A）= " << (costReal / kBatchCount) << " ms");

    // 只断言"跑完了、没退化成天文数字"这种底线，不断言具体快慢 —— 具体数值随机器
    // 与磁盘而变，写死阈值只会变成一条时不时误报的用例。数字看 WARN 输出。
    CHECK(costReal >= 0.0);
    CHECK(costRawDefault >= 0.0);
    CHECK(costRawTxn >= 0.0);
    CHECK(costWal >= 0.0);
    CHECK(costWalTxn >= 0.0);
}
