/**
 *  主面板全局搜索——本地消息库查询的集成测试（Suite GSM），GlobalSearchStoreTests.cpp
 *
 *  覆盖 CV2MsgStore 为主面板搜索下拉新增的两个查询与 LIKE 匹配串的构造：
 *    · SearchConversations：按会话汇总命中关键字的聊天记录 —— 条数、最近一条命中、按最近
 *      命中时间倒序、limit；排除已撤回的消息；文件、文件夹传输记录、文件共享卡片三类不计入
 *      （它们归「文件」标签）；
 *    · SearchFileMessages：只返回上述三类，文件名命中、按时间倒序、排除撤回、limit；
 *    · 两者都按消息记录窗相同的覆盖区间规则只计可见的行（区间有效且没拉到最早时，区间下界
 *      以下的旧行不计），保证搜索里看到的条数与点进消息记录窗后看到的一致；
 *    · MakeLikePattern：关键字里的 %、_、\ 按普通字符匹配；空关键字不返回任何结果；
 *    · IDX1 / IDX2：「文件」查询用的 (msgtype, timestamp) 索引在新库与存量库里都会建出来，
 *      且查询计划确实用到了它；
 *    · GSM9（隐藏，标签 [.][globalsearchbench]）：20 万条记录下两种查询的耗时，供判断要不要
 *      把查询挪到后台线程，只输出数字、不做断言。
 *  用临时 db 文件，跑真实 SQLite。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/HistoryCoverage.h"
#include "sqlite3.h"   // IDX1 / IDX2 直接查 sqlite_master 与查询计划
#include <stdio.h>   // GSM9 输出耗时
#include <string>
#include <vector>
#include <windows.h>

using namespace balloonhistory;

namespace {

//会话类型（与 net::conversation_type 取值一致）
const int kSingle = 1;
const int kGroup  = 2;

//消息类型（与 net/Protocol.h 的 message_type 取值一致）
const int kTypeText      = 1;
const int kTypeFile      = 3;
const int kTypeMerged    = 15;
const int kTypeFolder    = 18;
const int kTypeFileShare = 20;

//取一个临时目录下的 db 全路径，并先删掉同名旧文件，保证每个用例从空库开始。文件名带进程号，
//两份测试进程同时跑时不会共用同一个文件（同 V2MsgStoreTests::FreshTempDb）。
//  name：文件名，各用例传不同名
std::wstring FreshTempDb(const wchar_t* name)
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

//构造一条入库记录
//  msgid / ctype / cid / msgtype / timestamp / searchText：对应列的值
//  recalled：是否已撤回
HistoryRecord MakeRecord(int64_t msgid, int ctype, int64_t cid, int msgtype, int64_t timestamp,
                         const std::string& searchText, bool recalled)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = cid;
    rec.m_msgtype = msgtype;
    rec.m_timestamp = timestamp;
    rec.m_searchText = searchText;
    rec.m_recalled = recalled;
    return rec;
}

} // namespace

TEST_CASE("GSM1 按会话汇总命中的聊天记录：条数、最近一条、按最近命中时间倒序", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //单聊 10：两条命中（较早），一条不命中
    REQUIRE(store.Upsert(MakeRecord(1, kSingle, 10, kTypeText, 1000, "周五交设计稿", false)));
    REQUIRE(store.Upsert(MakeRecord(2, kSingle, 10, kTypeText, 2000, "设计稿改好了", false)));
    REQUIRE(store.Upsert(MakeRecord(3, kSingle, 10, kTypeText, 2500, "收到", false)));
    //群 20：一条命中（最近）
    REQUIRE(store.Upsert(MakeRecord(4, kGroup, 20, kTypeText, 3000, "群里的设计稿", false)));
    //群 30：没有命中
    REQUIRE(store.Upsert(MakeRecord(5, kGroup, 30, kTypeText, 4000, "午饭吃什么", false)));

    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversations("设计稿", 10, hits));
    REQUIRE(hits.size() == 2);

    //最近命中的会话排在前面
    CHECK(hits[0].m_ctype == kGroup);
    CHECK(hits[0].m_cid == 20);
    CHECK(hits[0].m_hitCount == 1);
    CHECK(hits[0].m_latestMsgid == 4);
    CHECK(hits[0].m_latestTimestamp == 3000);
    CHECK(hits[0].m_latestText == "群里的设计稿");

    CHECK(hits[1].m_ctype == kSingle);
    CHECK(hits[1].m_cid == 10);
    CHECK(hits[1].m_hitCount == 2);
    //最近一条命中是 msgid 2（msgid 3 不命中，不算）
    CHECK(hits[1].m_latestMsgid == 2);
    CHECK(hits[1].m_latestTimestamp == 2000);
    CHECK(hits[1].m_latestText == "设计稿改好了");
}

TEST_CASE("GSM2 已撤回的消息不计入，会话里只剩撤回的命中时整个会话不出现", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(1, kSingle, 10, kTypeText, 1000, "设计稿 v1", false)));
    REQUIRE(store.Upsert(MakeRecord(2, kSingle, 10, kTypeText, 2000, "设计稿 v2", true)));
    REQUIRE(store.Upsert(MakeRecord(3, kSingle, 11, kTypeText, 3000, "设计稿 v3", true)));

    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversations("设计稿", 10, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 10);
    CHECK(hits[0].m_hitCount == 1);
    CHECK(hits[0].m_latestMsgid == 1);
}

TEST_CASE("GSM3 文件、文件夹、共享卡片不计入聊天记录汇总；合并转发的子消息计入", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(1, kSingle, 10, kTypeFile, 1000, "设计稿.pdf", false)));
    REQUIRE(store.Upsert(MakeRecord(2, kSingle, 10, kTypeFolder, 2000, "设计稿素材", false)));
    REQUIRE(store.Upsert(MakeRecord(3, kSingle, 10, kTypeFileShare, 3000, "设计稿共享", false)));
    REQUIRE(store.Upsert(MakeRecord(4, kGroup, 20, kTypeMerged, 4000, "聊天记录 设计稿定了", false)));

    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversations("设计稿", 10, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_ctype == kGroup);
    CHECK(hits[0].m_cid == 20);
    CHECK(hits[0].m_latestMsgid == 4);
}

TEST_CASE("GSM4 覆盖区间：区间有效且没拉到最早时，下界以下的旧行不计；没有区间或已到最早时都计", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //会话 10：区间 [100, 200]，没拉到最早 —— msgid 50 在断层以下，不可见
    REQUIRE(store.Upsert(MakeRecord(50, kSingle, 10, kTypeText, 500, "设计稿 旧", false)));
    REQUIRE(store.Upsert(MakeRecord(150, kSingle, 10, kTypeText, 1500, "设计稿 新", false)));
    CoverRange cover10;
    cover10.m_valid = true;
    cover10.m_lo = 100;
    cover10.m_hi = 200;
    cover10.m_reachedOldest = false;
    REQUIRE(store.SetCoverage(kSingle, 10, cover10));

    //会话 11：区间 [100, 200]，已拉到最早 —— 全部可见
    REQUIRE(store.Upsert(MakeRecord(60, kSingle, 11, kTypeText, 600, "设计稿 a", false)));
    REQUIRE(store.Upsert(MakeRecord(160, kSingle, 11, kTypeText, 1600, "设计稿 b", false)));
    CoverRange cover11 = cover10;
    cover11.m_reachedOldest = true;
    REQUIRE(store.SetCoverage(kSingle, 11, cover11));

    //会话 12：没有区间 —— 全部可见
    REQUIRE(store.Upsert(MakeRecord(70, kSingle, 12, kTypeText, 700, "设计稿 c", false)));
    REQUIRE(store.Upsert(MakeRecord(170, kSingle, 12, kTypeText, 1700, "设计稿 d", false)));

    //会话 13：只有断层以下的命中 —— 整个会话不出现
    REQUIRE(store.Upsert(MakeRecord(80, kSingle, 13, kTypeText, 800, "设计稿 e", false)));
    REQUIRE(store.SetCoverage(kSingle, 13, cover10));

    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversations("设计稿", 10, hits));
    REQUIRE(hits.size() == 3);
    for (size_t i = 0; i < hits.size(); ++i)
    {
        INFO("cid = " << hits[i].m_cid);
        if (hits[i].m_cid == 10)
        {
            CHECK(hits[i].m_hitCount == 1);
        }
        else
        {
            CHECK((hits[i].m_cid == 11 || hits[i].m_cid == 12));
            CHECK(hits[i].m_hitCount == 2);
        }
    }

    //文件查询同样遵守覆盖区间
    REQUIRE(store.Upsert(MakeRecord(90, kSingle, 13, kTypeFile, 900, "设计稿.pdf", false)));
    REQUIRE(store.Upsert(MakeRecord(190, kSingle, 13, kTypeFile, 1900, "设计稿终版.pdf", false)));
    std::vector<HistoryRecord> files;
    REQUIRE(store.SearchFileMessages("设计稿", 10, files));
    REQUIRE(files.size() == 1);
    CHECK(files[0].m_msgid == 190);
}

TEST_CASE("GSM5 会话数超过 limit 时只返回最近命中的 limit 个", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm5.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    for (int i = 1; i <= 5; ++i)
    {
        REQUIRE(store.Upsert(MakeRecord(i, kSingle, 100 + i, kTypeText, 1000 * i, "设计稿", false)));
    }

    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversations("设计稿", 3, hits));
    REQUIRE(hits.size() == 3);
    CHECK(hits[0].m_cid == 105);
    CHECK(hits[1].m_cid == 104);
    CHECK(hits[2].m_cid == 103);
}

TEST_CASE("GSM6 关键字里的 % _ \\ 与单引号按普通字符匹配；ASCII 大小写不敏感；空关键字无结果", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm6.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(1, kSingle, 10, kTypeText, 1000, "完成了 50%", false)));
    REQUIRE(store.Upsert(MakeRecord(2, kSingle, 11, kTypeText, 2000, "完成了 500", false)));
    REQUIRE(store.Upsert(MakeRecord(3, kSingle, 12, kTypeText, 3000, "file_name", false)));
    REQUIRE(store.Upsert(MakeRecord(4, kSingle, 13, kTypeText, 4000, "fileXname", false)));
    REQUIRE(store.Upsert(MakeRecord(5, kSingle, 14, kTypeText, 5000, "C:\\temp", false)));
    REQUIRE(store.Upsert(MakeRecord(6, kSingle, 15, kTypeText, 6000, "it's OK", false)));
    REQUIRE(store.Upsert(MakeRecord(7, kSingle, 16, kTypeText, 7000, "Design Review", false)));

    std::vector<ConversationHit> hits;

    //% 不是通配：只命中真的含「50%」的那条，「500」不算
    REQUIRE(store.SearchConversations("50%", 10, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 10);

    //_ 不是单字符通配：「fileXname」不算
    REQUIRE(store.SearchConversations("file_", 10, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 12);

    //反斜杠按普通字符匹配
    REQUIRE(store.SearchConversations(":\\t", 10, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 14);

    //单引号经参数绑定，不会破坏 SQL
    REQUIRE(store.SearchConversations("it's", 10, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 15);

    //ASCII 大小写不敏感（SQLite LIKE 的默认行为）
    REQUIRE(store.SearchConversations("design", 10, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 16);

    //空关键字：不返回任何结果，也不报错
    REQUIRE(store.SearchConversations("", 10, hits));
    CHECK(hits.empty());
    std::vector<HistoryRecord> files;
    REQUIRE(store.SearchFileMessages("", 10, files));
    CHECK(files.empty());
}

TEST_CASE("GSM7 文件查询只返回文件、文件夹、共享卡片三类，按时间倒序，排除撤回，limit 生效", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm7.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(1, kSingle, 10, kTypeFile, 1000, "设计稿.pdf", false)));
    REQUIRE(store.Upsert(MakeRecord(2, kGroup, 20, kTypeFolder, 2000, "设计稿素材", false)));
    REQUIRE(store.Upsert(MakeRecord(3, kGroup, 20, kTypeFileShare, 3000, "设计稿共享", false)));
    REQUIRE(store.Upsert(MakeRecord(4, kSingle, 10, kTypeText, 4000, "设计稿发你了", false)));
    REQUIRE(store.Upsert(MakeRecord(5, kSingle, 11, kTypeFile, 5000, "设计稿v2.pdf", true)));
    REQUIRE(store.Upsert(MakeRecord(6, kSingle, 11, kTypeFile, 6000, "报价单.xlsx", false)));

    std::vector<HistoryRecord> files;
    REQUIRE(store.SearchFileMessages("设计稿", 10, files));
    REQUIRE(files.size() == 3);
    CHECK(files[0].m_msgid == 3);
    CHECK(files[0].m_msgtype == kTypeFileShare);
    CHECK(files[1].m_msgid == 2);
    CHECK(files[1].m_msgtype == kTypeFolder);
    CHECK(files[2].m_msgid == 1);
    CHECK(files[2].m_msgtype == kTypeFile);
    CHECK(files[2].m_searchText == "设计稿.pdf");
    CHECK(files[2].m_ctype == kSingle);
    CHECK(files[2].m_cid == 10);

    REQUIRE(store.SearchFileMessages("设计稿", 2, files));
    REQUIRE(files.size() == 2);
    CHECK(files[0].m_msgid == 3);
    CHECK(files[1].m_msgid == 2);
}

TEST_CASE("GSM8 MakeLikePattern 前后加 %，并给 \\ % _ 前面加反斜杠", "[globalsearch]")
{
    CHECK(MakeLikePattern("设计稿") == "%设计稿%");
    CHECK(MakeLikePattern("50%") == "%50\\%%");
    CHECK(MakeLikePattern("a_b") == "%a\\_b%");
    CHECK(MakeLikePattern("C:\\t") == "%C:\\\\t%");
    CHECK(MakeLikePattern("") == "%%");

    //归「文件」标签的消息类型
    CHECK(IsFileTabMsgtype(kTypeFile));
    CHECK(IsFileTabMsgtype(kTypeFolder));
    CHECK(IsFileTabMsgtype(kTypeFileShare));
    CHECK_FALSE(IsFileTabMsgtype(kTypeText));
    CHECK_FALSE(IsFileTabMsgtype(kTypeMerged));
}

namespace {

//基准用例的规模：会话数 × 每个会话的消息数（共 20 万条，约合一个重度用户几年的本地记录）
const int kBenchConversations = 400;
const int kBenchPerConversation = 500;
//每多少条消息里放一条文件消息、一条命中关键字的文字消息
const int kBenchFileEvery = 50;
const int kBenchHitEvery = 100;
//每批写入多少条（UpsertBatch 一个事务）
const size_t kBenchBatch = 5000;
//每种查询重复几次取平均
const int kBenchRepeat = 5;
//「消息」「文件」标签每次查询的条数上限（与 SearchDropdown.cpp 的 kMessageHitLimit / kFileHitLimit 一致）
const int kBenchLimit = 50;

//当前时刻（毫秒，带小数），基于高精度计数器
double BenchNowMs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

} // namespace

TEST_CASE("GSM9 本地查询在 20 万条记录下的耗时（基准，不参与常规运行）", "[.][globalsearchbench]")
{
    std::wstring path = FreshTempDb(L"flamingo_gsm9.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //造数据：每个会话交替单聊 / 群聊，正文从几句话里轮换，按固定间隔插入文件消息与命中消息
    const char* const kPhrases[] =
    {
        "明天上午十点开周会，记得带上本周的进度",
        "收到，我晚点把材料整理好发到群里",
        "这个问题我跟一下，下午给你答复",
        "接口文档已经更新到最新版本了",
        "好的，辛苦了",
    };
    const int kPhraseCount = (int)(sizeof(kPhrases) / sizeof(kPhrases[0]));

    const double tInsert = BenchNowMs();
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
                batch.push_back(MakeRecord(msgid, ctype, cid, kTypeFile, ts, "季度报告-附件.pdf", false));
            }
            else if (i % kBenchHitEvery == 1)
            {
                batch.push_back(MakeRecord(msgid, ctype, cid, kTypeText, ts, "设计稿已经放到共享盘了", false));
            }
            else
            {
                batch.push_back(MakeRecord(msgid, ctype, cid, kTypeText, ts, kPhrases[i % kPhraseCount], false));
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
    std::printf("GSM9 insert %d rows: %.0f ms\n", (int)(msgid - 1), BenchNowMs() - tInsert);

    //「消息」标签：常见词、找不到的词（全表扫一遍）、英文词
    const char* const kMsgKeywords[] = { "设计稿", "这个词不存在", "ok" };
    for (size_t k = 0; k < sizeof(kMsgKeywords) / sizeof(kMsgKeywords[0]); ++k)
    {
        std::vector<ConversationHit> hits;
        double total = 0.0;
        for (int r = 0; r < kBenchRepeat; ++r)
        {
            const double t0 = BenchNowMs();
            REQUIRE(store.SearchConversations(kMsgKeywords[k], kBenchLimit, hits));
            total += BenchNowMs() - t0;
        }
        std::printf("GSM9 SearchConversations keyword #%u: %u conversations, avg %.1f ms\n",
                    (unsigned)k, (unsigned)hits.size(), total / kBenchRepeat);
    }

    //「文件」标签
    const char* const kFileKeywords[] = { "报告", "这个词不存在" };
    for (size_t k = 0; k < sizeof(kFileKeywords) / sizeof(kFileKeywords[0]); ++k)
    {
        std::vector<HistoryRecord> files;
        double total = 0.0;
        for (int r = 0; r < kBenchRepeat; ++r)
        {
            const double t0 = BenchNowMs();
            REQUIRE(store.SearchFileMessages(kFileKeywords[k], kBenchLimit, files));
            total += BenchNowMs() - t0;
        }
        std::printf("GSM9 SearchFileMessages keyword #%u: %u records, avg %.1f ms\n",
                    (unsigned)k, (unsigned)files.size(), total / kBenchRepeat);
    }
    std::fflush(stdout);

    store.Close();
    ::DeleteFileW(path.c_str());
}

namespace {

//用原生 SQLite 打开库文件，查 sqlite_master 里有没有某个索引
//  path：库文件路径；indexName：索引名
bool IndexExists(const std::wstring& path, const char* indexName)
{
    sqlite3* db = NULL;
    if (sqlite3_open16(path.c_str(), &db) != SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }
    bool found = false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='index' AND name=?",
                           -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, indexName, -1, SQLITE_TRANSIENT);
        found = (sqlite3_step(stmt) == SQLITE_ROW);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

} // namespace

TEST_CASE("IDX1 打开本地库后有按类型与时间的索引，存量库第一次打开时补建", "[globalsearch]")
{
    //新库
    std::wstring fresh = FreshTempDb(L"flamingo_idx1_new.db");
    {
        CV2MsgStore store;
        REQUIRE(store.Open(fresh.c_str()));
        store.Close();
    }
    CHECK(IndexExists(fresh, "idx_type_time"));

    //存量库：只有表、没有这个索引（模拟加索引之前建出来的库）
    std::wstring legacy = FreshTempDb(L"flamingo_idx1_old.db");
    {
        sqlite3* db = NULL;
        REQUIRE(sqlite3_open16(legacy.c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db,
            "CREATE TABLE t_msg (msgid INTEGER NOT NULL, clientmsgid TEXT, ctype INTEGER NOT NULL,"
            " cid INTEGER NOT NULL, fromid INTEGER NOT NULL, msgtype INTEGER NOT NULL, content TEXT,"
            " search_text TEXT, is_link INTEGER NOT NULL DEFAULT 0, timestamp INTEGER NOT NULL,"
            " recalled INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (ctype,cid,msgid))",
            NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(db);
    }
    CHECK_FALSE(IndexExists(legacy, "idx_type_time"));
    {
        CV2MsgStore store;
        REQUIRE(store.Open(legacy.c_str()));
        store.Close();
    }
    CHECK(IndexExists(legacy, "idx_type_time"));

    ::DeleteFileW(fresh.c_str());
    ::DeleteFileW(legacy.c_str());
}

TEST_CASE("IDX2 「文件」标签的查询走按类型与时间的索引", "[globalsearch]")
{
    std::wstring path = FreshTempDb(L"flamingo_idx2.db");
    {
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        REQUIRE(store.Upsert(MakeRecord(1, kSingle, 10, kTypeFile, 1000, "设计稿.pdf", false)));
        REQUIRE(store.Upsert(MakeRecord(2, kSingle, 10, kTypeText, 2000, "设计稿发你了", false)));
        store.Close();
    }

    //取查询计划：EXPLAIN QUERY PLAN 每行第 4 列是说明文字，找有没有一行用到了这个索引
    sqlite3* db = NULL;
    REQUIRE(sqlite3_open16(path.c_str(), &db) == SQLITE_OK);
    const std::string sql = "EXPLAIN QUERY PLAN " + BuildSearchFileMessagesSql(50);
    sqlite3_stmt* stmt = NULL;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK);
    const std::string pattern = MakeLikePattern("设计稿");
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    bool usesIndex = false;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* detail = (const char*)sqlite3_column_text(stmt, 3);
        if (detail != NULL && std::string(detail).find("idx_type_time") != std::string::npos)
        {
            usesIndex = true;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    CHECK(usesIndex);

    ::DeleteFileW(path.c_str());
}
