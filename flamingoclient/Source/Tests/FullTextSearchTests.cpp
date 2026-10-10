/**
 *  本地消息库全文索引的单元测试（Suite FTS），FullTextSearchTests.cpp
 *
 *  覆盖 CV2MsgStore 的全文索引（t_msg_fts，trigram 分词）与 V2MsgQuery 的相关纯逻辑：
 *    FTS1  CanUseTrigramSearch 按 Unicode 字符数判断（不少于三个字）；MakeFtsPhrase 用双引号包起来、
 *          内部双引号写成两个；三条搜索语句的全文索引版本先 MATCH 再 LIKE，两个占位；
 *    FTS2  新库打开即可用全文索引；三种跨会话搜索在用与不用全文索引时结果完全相同（中英文、
 *          大小写、% _ 双引号等特殊字符、少于三个字的关键字、已撤回、覆盖区间之外的旧行）；
 *    FTS3  存量库（没有全文索引的旧库）打开后先不用全文索引、搜索照常；分批回填若干次后可用，
 *          结果仍相同；索引里每行只有一份；
 *    FTS4  触发器保持索引同步：新增、改 search_text、删除；回填之前就被删掉的行不会让回填出错，
 *          索引通过 FTS5 自带的完整性检查；
 *    FTS5  回填进度记在库里，关掉再打开从上次的位置接着回填；
 *    FTS6  后台查询线程在空闲时自动把存量库回填完毕；
 *    FTSB（隐藏，标签 [.][globalsearchbench]）：20 万条记录下回填全部存量数据的耗时，以及三种跨会话
 *          搜索用与不用全文索引的耗时，只输出数字、不做断言。
 *  用临时 db 文件跑真实 SQLite（FlamingoUnit 编入的 sqlite3.c 定义了 SQLITE_ENABLE_FTS5）。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/V2MsgSearchWorker.h"
#include "sqlite3.h"   // 另开一条连接直接查索引表与进度表

#include <stdio.h>     // FTSB 输出耗时
#include <string>
#include <vector>
#include <windows.h>

using namespace balloonhistory;

namespace {

//会话类型（与 net::conversation_type 一致）
const int kSingle = 1;
const int kGroup  = 2;
//消息类型（与 net/Protocol.h 的 message_type 一致）
const int kTypeText   = 1;
const int kTypeFile   = 3;
const int kTypeFolder = 18;
//查询上限：足够放下全部测试数据
const int kLimit = 100;
//FTS6 等后台回填完成的最长时间（毫秒）与轮询间隔
const DWORD kBackfillWaitMs = 20000;
const DWORD kPollMs = 10;

//取一个临时目录下的 db 全路径，并先删掉同名旧文件与 WAL 残留（文件名带进程号）
std::wstring FreshFtsDb(const wchar_t* name)
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

//构造一条入库记录
HistoryRecord MakeFtsRecord(int64_t msgid, int ctype, int64_t cid, int msgtype, int64_t timestamp,
                            const std::string& searchText)
{
    HistoryRecord rec;
    rec.m_msgid      = msgid;
    rec.m_ctype      = ctype;
    rec.m_cid        = cid;
    rec.m_fromid     = cid;
    rec.m_msgtype    = msgtype;
    rec.m_timestamp  = timestamp;
    rec.m_searchText = searchText;
    return rec;
}

//放入一组覆盖各种情形的数据
void SeedFtsStore(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(MakeFtsRecord(1, kSingle, 10, kTypeText, 1000, "周五交设计稿")));
    REQUIRE(store.Upsert(MakeFtsRecord(2, kSingle, 10, kTypeText, 2000, "Design Review 设计稿 v2")));
    REQUIRE(store.Upsert(MakeFtsRecord(3, kGroup, 20, kTypeText, 3000, "群里的设计稿")));
    REQUIRE(store.Upsert(MakeFtsRecord(4, kGroup, 30, kTypeFile, 4000, "设计稿.pdf")));
    REQUIRE(store.Upsert(MakeFtsRecord(5, kGroup, 20, kTypeText, 5000, "增长 50% 的报表")));
    REQUIRE(store.Upsert(MakeFtsRecord(6, kSingle, 11, kTypeText, 6000, "a_b_c 变量名")));
    REQUIRE(store.Upsert(MakeFtsRecord(7, kSingle, 11, kTypeText, 7000, "他说\"你好世界\"然后走了")));
    REQUIRE(store.Upsert(MakeFtsRecord(8, kSingle, 12, kTypeText, 8000, "撤回的设计稿")));
    REQUIRE(store.MarkRecalled(kSingle, 12, 8));
    REQUIRE(store.Upsert(MakeFtsRecord(9, kSingle, 13, kTypeText, 9000, "REPORT 2026")));
    REQUIRE(store.Upsert(MakeFtsRecord(10, kSingle, 13, kTypeText, 10000, "report 草稿")));
    REQUIRE(store.Upsert(MakeFtsRecord(11, kGroup, 30, kTypeFolder, 11000, "设计稿备份")));
    //会话 (1,10) 的覆盖区间下界是 2 且没拉到最早：msgid 1 那一行不可见
    CoverRange cover;
    cover.m_valid = true;
    cover.m_lo = 2;
    cover.m_hi = 2;
    cover.m_reachedOldest = false;
    REQUIRE(store.SetCoverage(kSingle, 10, cover));
}

//FTS2、FTS3 比对用的关键字：中英文、大小写、特殊字符、少于三个字、找不到
std::vector<std::string> CompareKeywords()
{
    std::vector<std::string> k;
    k.push_back("设计稿");
    k.push_back("design");
    k.push_back("DESIGN REVIEW");
    k.push_back("50%");
    k.push_back("a_b");
    k.push_back("\"你好");
    k.push_back("你好世界");
    k.push_back("report");
    k.push_back("撤回的");
    k.push_back("不存在的词");
    k.push_back("设计");
    k.push_back("Rev");
    k.push_back("稿.pdf");
    return k;
}

//比较两份会话汇总是否逐项相同
void CheckSameHits(const std::vector<ConversationHit>& a, const std::vector<ConversationHit>& b)
{
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK(a[i].m_ctype == b[i].m_ctype);
        CHECK(a[i].m_cid == b[i].m_cid);
        CHECK(a[i].m_hitCount == b[i].m_hitCount);
        CHECK(a[i].m_latestMsgid == b[i].m_latestMsgid);
        CHECK(a[i].m_latestText == b[i].m_latestText);
    }
}

//比较两份记录是否逐条相同（按 msgid 与会话）
void CheckSameRecords(const std::vector<HistoryRecord>& a, const std::vector<HistoryRecord>& b)
{
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK(a[i].m_msgid == b[i].m_msgid);
        CHECK(a[i].m_ctype == b[i].m_ctype);
        CHECK(a[i].m_cid == b[i].m_cid);
    }
}

//对每个关键字，三种跨会话搜索在用与不用全文索引时结果都相同；返回结果非空的关键字个数
int CompareWithAndWithoutFullText(CV2MsgStore& store)
{
    const std::vector<std::string> keywords = CompareKeywords();
    std::vector<HistoryConversationKey> scope;
    scope.push_back(HistoryConversationKey(kSingle, 10));
    scope.push_back(HistoryConversationKey(kGroup, 30));
    int nonEmpty = 0;
    for (size_t i = 0; i < keywords.size(); ++i)
    {
        INFO("keyword: " << keywords[i]);
        std::vector<ConversationHit> convFts;
        std::vector<ConversationHit> convLike;
        std::vector<HistoryRecord> fileFts;
        std::vector<HistoryRecord> fileLike;
        std::vector<ConversationHit> mgrFts;
        std::vector<ConversationHit> mgrLike;
        std::vector<ConversationHit> mgrScopeFts;
        std::vector<ConversationHit> mgrScopeLike;

        store.SetFullTextAllowed(true);
        REQUIRE(store.SearchConversations(keywords[i], kLimit, convFts));
        REQUIRE(store.SearchFileMessages(keywords[i], kLimit, fileFts));
        REQUIRE(store.SearchConversationsForManager(keywords[i], kLimit, std::vector<HistoryConversationKey>(), mgrFts));
        REQUIRE(store.SearchConversationsForManager(keywords[i], kLimit, scope, mgrScopeFts));

        store.SetFullTextAllowed(false);
        REQUIRE(store.SearchConversations(keywords[i], kLimit, convLike));
        REQUIRE(store.SearchFileMessages(keywords[i], kLimit, fileLike));
        REQUIRE(store.SearchConversationsForManager(keywords[i], kLimit, std::vector<HistoryConversationKey>(), mgrLike));
        REQUIRE(store.SearchConversationsForManager(keywords[i], kLimit, scope, mgrScopeLike));
        store.SetFullTextAllowed(true);

        CheckSameHits(convFts, convLike);
        CheckSameRecords(fileFts, fileLike);
        CheckSameHits(mgrFts, mgrLike);
        CheckSameHits(mgrScopeFts, mgrScopeLike);
        if (!convLike.empty() || !fileLike.empty() || !mgrLike.empty())
        {
            ++nonEmpty;
        }
    }
    return nonEmpty;
}

//另开一条连接
sqlite3* OpenRaw(const std::wstring& path)
{
    sqlite3* db = NULL;
    if (sqlite3_open16(path.c_str(), &db) != SQLITE_OK)
    {
        if (db != NULL)
        {
            sqlite3_close(db);
        }
        return NULL;
    }
    return db;
}

//执行一条查询、取第一行第一列的整数；出错返回 -1
int64_t QueryInt(sqlite3* db, const char* sql)
{
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return -1;
    }
    int64_t v = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        v = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return v;
}

//索引里匹配某个短语（已加好双引号）的行数
int64_t CountFtsMatch(sqlite3* db, const std::string& phrase)
{
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM t_msg_fts WHERE t_msg_fts MATCH ?", -1, &stmt, NULL) != SQLITE_OK)
    {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, phrase.c_str(), -1, SQLITE_TRANSIENT);
    int64_t v = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        v = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return v;
}

//把库变成没有全文索引的旧库：去掉三个触发器、索引表与进度表
void DropFullText(const std::wstring& path)
{
    sqlite3* db = OpenRaw(path);
    REQUIRE(db != NULL);
    REQUIRE(sqlite3_exec(db,
        "DROP TRIGGER t_msg_fts_ins; DROP TRIGGER t_msg_fts_upd; DROP TRIGGER t_msg_fts_del;"
        "DROP TABLE t_msg_fts; DROP TABLE t_fts_meta;", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
}

} // namespace

TEST_CASE("FTS1 关键字字数判断、短语转义与全文索引版本的语句", "[globalsearch]")
{
    CHECK_FALSE(CanUseTrigramSearch(""));
    CHECK_FALSE(CanUseTrigramSearch("ab"));
    CHECK(CanUseTrigramSearch("abc"));
    CHECK_FALSE(CanUseTrigramSearch("设计"));
    CHECK(CanUseTrigramSearch("设计稿"));
    CHECK_FALSE(CanUseTrigramSearch("a设"));
    CHECK(CanUseTrigramSearch("a设b"));
    //四字节的字符（表情）算一个字
    CHECK_FALSE(CanUseTrigramSearch("\xF0\x9F\x98\x80" "a"));
    CHECK(CanUseTrigramSearch("\xF0\x9F\x98\x80" "ab"));

    CHECK(MakeFtsPhrase("设计稿") == "\"设计稿\"");
    CHECK(MakeFtsPhrase("say \"hi\"") == "\"say \"\"hi\"\"\"");
    CHECK(MakeFtsPhrase("a OR b*") == "\"a OR b*\"");

    //全文索引版本：先 MATCH 后 LIKE；不用时没有 MATCH
    const std::string conv = BuildSearchConversationsSql(10, true);
    const size_t matchPos = conv.find("t_msg_fts MATCH ?");
    const size_t likePos = conv.find("LIKE ?");
    REQUIRE(matchPos != std::string::npos);
    REQUIRE(likePos != std::string::npos);
    CHECK(matchPos < likePos);
    CHECK(BuildSearchConversationsSql(10).find("MATCH") == std::string::npos);
    CHECK(BuildSearchFileMessagesSql(10, true).find("t_msg_fts MATCH ?") != std::string::npos);
    CHECK(BuildSearchFileMessagesSql(10).find("MATCH") == std::string::npos);
    std::vector<HistoryConversationKey> scope;
    scope.push_back(HistoryConversationKey(kSingle, 10));
    CHECK(BuildManagerSearchSql(10, scope, true).find("t_msg_fts MATCH ?") != std::string::npos);
    CHECK(BuildManagerSearchSql(10, scope).find("MATCH") == std::string::npos);
}

TEST_CASE("FTS2 新库即可用全文索引，结果与只用 LIKE 完全相同", "[globalsearch]")
{
    std::wstring path = FreshFtsDb(L"flamingo_fts2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    CHECK(store.IsFullTextReady());
    SeedFtsStore(store);

    //已有回填进度时再回填一次什么也不做
    CHECK(store.BackfillFullTextStep(100) == 0);

    const int nonEmpty = CompareWithAndWithoutFullText(store);
    //关键字里大部分有命中，比对不是在一堆空结果上进行
    CHECK(nonEmpty >= 9);

    //几处具体结果：大小写不敏感、已撤回的不算、覆盖区间外的旧行不算
    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversations("design", kLimit, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 10);
    REQUIRE(store.SearchConversations("撤回的", kLimit, hits));
    CHECK(hits.empty());
    REQUIRE(store.SearchConversations("周五交", kLimit, hits));
    CHECK(hits.empty());
    REQUIRE(store.SearchConversations("report", kLimit, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_hitCount == 2);

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("FTS3 存量库先走 LIKE，分批回填后改用全文索引，结果不变", "[globalsearch]")
{
    std::wstring path = FreshFtsDb(L"flamingo_fts3.db");
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        SeedFtsStore(store);
        store.Close();
    }
    DropFullText(path);

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    CHECK_FALSE(store.IsFullTextReady());
    //回填之前搜索照常（走 LIKE）
    std::vector<ConversationHit> before;
    REQUIRE(store.SearchConversations("设计稿", kLimit, before));
    CHECK(before.size() == 2);

    //每批 3 个 rowid，11 行要回填 4 批
    const int kRowsPerStep = 3;
    int steps = 0;
    int total = 0;
    while (!store.IsFullTextReady())
    {
        const int rows = store.BackfillFullTextStep(kRowsPerStep);
        REQUIRE(rows >= 0);
        total += rows;
        ++steps;
        REQUIRE(steps < 100);
    }
    CHECK(steps == 4);
    CHECK(total == 11);

    CompareWithAndWithoutFullText(store);
    std::vector<ConversationHit> after;
    REQUIRE(store.SearchConversations("设计稿", kLimit, after));
    CheckSameHits(after, before);

    //索引里每行一份：含「设计稿」的行共 6 条（含已撤回与覆盖区间外的，索引不管这些）
    sqlite3* raw = OpenRaw(path);
    REQUIRE(raw != NULL);
    CHECK(CountFtsMatch(raw, "\"设计稿\"") == 6);
    sqlite3_close(raw);

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("FTS4 触发器保持索引同步，回填前删掉的行不影响回填", "[globalsearch]")
{
    std::wstring path = FreshFtsDb(L"flamingo_fts4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    REQUIRE(store.IsFullTextReady());
    SeedFtsStore(store);

    sqlite3* raw = OpenRaw(path);
    REQUIRE(raw != NULL);

    //新增
    REQUIRE(store.Upsert(MakeFtsRecord(100, kSingle, 50, kTypeText, 20000, "季度经营分析")));
    CHECK(CountFtsMatch(raw, "\"经营分析\"") == 1);
    //同一条消息再写一次、search_text 变了：旧文字不再命中，新文字命中，没有重复
    REQUIRE(store.Upsert(MakeFtsRecord(100, kSingle, 50, kTypeText, 20000, "年度预算草案")));
    CHECK(CountFtsMatch(raw, "\"经营分析\"") == 0);
    CHECK(CountFtsMatch(raw, "\"预算草案\"") == 1);
    REQUIRE(store.Upsert(MakeFtsRecord(100, kSingle, 50, kTypeText, 20000, "年度预算草案")));
    CHECK(CountFtsMatch(raw, "\"预算草案\"") == 1);
    //删除
    std::vector<int64_t> ids;
    ids.push_back(100);
    int deleted = 0;
    REQUIRE(store.DeleteMessages(kSingle, 50, ids, deleted));
    CHECK(deleted == 1);
    CHECK(CountFtsMatch(raw, "\"预算草案\"") == 0);
    CHECK(sqlite3_exec(raw, "INSERT INTO t_msg_fts(t_msg_fts) VALUES('integrity-check')", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(raw);
    store.Close();

    //存量库：回填之前删掉一行（索引里本来就没有它），之后回填照常完成，索引完好
    DropFullText(path);
    REQUIRE(store.Open(path.c_str()));
    REQUIRE_FALSE(store.IsFullTextReady());
    ids.clear();
    ids.push_back(3);
    REQUIRE(store.DeleteMessages(kGroup, 20, ids, deleted));
    CHECK(deleted == 1);
    //回填之前新增的一行由触发器写进索引，回填再写一次也只有一份
    REQUIRE(store.Upsert(MakeFtsRecord(101, kGroup, 20, kTypeText, 21000, "群里的设计稿二稿")));
    while (!store.IsFullTextReady())
    {
        REQUIRE(store.BackfillFullTextStep(4) >= 0);
    }
    raw = OpenRaw(path);
    REQUIRE(raw != NULL);
    CHECK(CountFtsMatch(raw, "\"群里的设计稿\"") == 1);
    CHECK(sqlite3_exec(raw, "INSERT INTO t_msg_fts(t_msg_fts) VALUES('integrity-check')", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(raw);
    CompareWithAndWithoutFullText(store);

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("FTS5 回填进度记在库里，重新打开后接着回填", "[globalsearch]")
{
    std::wstring path = FreshFtsDb(L"flamingo_fts5.db");
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        SeedFtsStore(store);
        store.Close();
    }
    DropFullText(path);

    const int kRowsPerStep = 4;
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        //第一批是 rowid 0～3，rowid 从 1 开始，只有 3 行
        REQUIRE(store.BackfillFullTextStep(kRowsPerStep) == 3);
        CHECK_FALSE(store.IsFullTextReady());
        store.Close();
    }

    sqlite3* raw = OpenRaw(path);
    REQUIRE(raw != NULL);
    CHECK(QueryInt(raw, "SELECT v FROM t_fts_meta WHERE k='backfill_next'") == kRowsPerStep);
    CHECK(QueryInt(raw, "SELECT v FROM t_fts_meta WHERE k='backfill_done'") == 0);
    sqlite3_close(raw);

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    CHECK_FALSE(store.IsFullTextReady());
    //接着回填 rowid 4～7、8～11
    CHECK(store.BackfillFullTextStep(kRowsPerStep) == kRowsPerStep);
    CHECK(store.BackfillFullTextStep(kRowsPerStep) == 4);
    CHECK(store.IsFullTextReady());
    CompareWithAndWithoutFullText(store);
    store.Close();

    //再打开时直接可用
    CV2MsgStore again;
    REQUIRE(again.Open(path.c_str()));
    CHECK(again.IsFullTextReady());
    again.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("FTS6 后台查询线程空闲时把存量库回填完毕", "[globalsearch]")
{
    std::wstring path = FreshFtsDb(L"flamingo_fts6.db");
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        SeedFtsStore(store);
        store.Close();
    }
    DropFullText(path);

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    REQUIRE_FALSE(store.IsFullTextReady());
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));
    REQUIRE(worker.UsesOwnConnection());

    //等进度表里出现「已完成」
    sqlite3* raw = OpenRaw(path);
    REQUIRE(raw != NULL);
    const DWORD start = ::GetTickCount();
    while (QueryInt(raw, "SELECT v FROM t_fts_meta WHERE k='backfill_done'") != 1)
    {
        REQUIRE(::GetTickCount() - start < kBackfillWaitMs);
        ::Sleep(kPollMs);
    }
    CHECK(CountFtsMatch(raw, "\"设计稿\"") == 6);
    sqlite3_close(raw);

    worker.Stop();
    store.Close();
    ::DeleteFileW(path.c_str());
}

namespace {

//FTSB 的规模与 GlobalSearchStoreTests.cpp 的 GSM9 相同：400 个会话 × 每个 500 条
const int kBenchConversations = 400;
const int kBenchPerConversation = 500;
const int kBenchFileEvery = 50;
const int kBenchHitEvery = 100;
const size_t kBenchBatch = 5000;
const int kBenchRowsPerStep = 2000;

//当前时刻（毫秒，带小数）
double FtsNowMs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

} // namespace

TEST_CASE("FTSB 20 万条记录下回填与搜索的耗时（基准，不参与常规运行）", "[.][globalsearchbench]")
{
    std::wstring path = FreshFtsDb(L"flamingo_ftsb.db");
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        std::vector<HistoryRecord> batch;
        batch.reserve(kBenchBatch);
        int64_t msgid = 1;
        for (int c = 0; c < kBenchConversations; ++c)
        {
            const int ctype = (c % 2 == 0) ? kSingle : kGroup;
            const int64_t cid = 1000 + c;
            for (int i = 0; i < kBenchPerConversation; ++i)
            {
                const int64_t ts = 1700000000000LL + msgid * 1000;
                if (i % kBenchFileEvery == 0)
                {
                    batch.push_back(MakeFtsRecord(msgid, ctype, cid, kTypeFile, ts, "季度报告-附件.pdf"));
                }
                else if (i % kBenchHitEvery == 1)
                {
                    batch.push_back(MakeFtsRecord(msgid, ctype, cid, kTypeText, ts, "设计稿已经放到共享盘了"));
                }
                else
                {
                    batch.push_back(MakeFtsRecord(msgid, ctype, cid, kTypeText, ts, "收到，我晚点把材料整理好发到群里"));
                }
                ++msgid;
                if (batch.size() >= kBenchBatch)
                {
                    REQUIRE(store.UpsertBatch(batch));
                    batch.clear();
                }
            }
        }
        if (!batch.empty())
        {
            REQUIRE(store.UpsertBatch(batch));
        }
        store.Close();
    }
    DropFullText(path);

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    const double t0 = FtsNowMs();
    double maxStepMs = 0.0;
    int steps = 0;
    while (!store.IsFullTextReady())
    {
        const double s0 = FtsNowMs();
        REQUIRE(store.BackfillFullTextStep(kBenchRowsPerStep) >= 0);
        const double stepMs = FtsNowMs() - s0;
        if (stepMs > maxStepMs)
        {
            maxStepMs = stepMs;
        }
        ++steps;
    }
    const double backfillMs = FtsNowMs() - t0;

    std::vector<ConversationHit> hits;
    std::vector<HistoryRecord> recs;
    double ms[2][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
    for (int mode = 0; mode < 2; ++mode)
    {
        store.SetFullTextAllowed(mode == 0);
        double a = FtsNowMs();
        REQUIRE(store.SearchConversations("设计稿", 50, hits));
        ms[mode][0] = FtsNowMs() - a;
        a = FtsNowMs();
        REQUIRE(store.SearchFileMessages("季度报告", 50, recs));
        ms[mode][1] = FtsNowMs() - a;
        a = FtsNowMs();
        REQUIRE(store.SearchConversationsForManager("设计稿", 50, std::vector<HistoryConversationKey>(), hits));
        ms[mode][2] = FtsNowMs() - a;
    }
    std::printf("FTSB backfill %.1f ms in %d steps (max step %.1f ms); full-text: conversations %.1f ms, "
                "files %.1f ms, manager %.1f ms; LIKE: conversations %.1f ms, files %.1f ms, manager %.1f ms\n",
                backfillMs, steps, maxStepMs, ms[0][0], ms[0][1], ms[0][2], ms[1][0], ms[1][1], ms[1][2]);
    std::fflush(stdout);

    store.Close();
    ::DeleteFileW(path.c_str());
}
