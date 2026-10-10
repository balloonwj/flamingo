/**
 *  聊天记录管理器——本地消息库查询的集成测试（Suite HMS），HistoryManagerStoreTests.cpp
 *
 *  覆盖 CV2MsgStore 为聊天记录管理器新增的两个查询：
 *    · QueryConversationList：左栏的本地部分。每个会话取 msgid 最大的一条未撤回消息作为最后一条，
 *      按覆盖区间不可见的会话不列出，按最后时间倒序、同毫秒按 msgid 倒序；
 *    · SearchConversationsForManager：左栏的跨会话搜索。与 SearchConversations 同一套可见口径，
 *      但不排除文件类消息，并且可以限定在若干会话里；
 *    · BuildManagerSearchSql：限定范围时语句的拼法。
 *  另有一条隐藏的性能用例 HMB1（标签 [.][historymgrbench]），量几十万行消息时上述查询的耗时。
 *  用临时 db 文件，跑真实 SQLite。框架 Catch2 v3。方案见 docs/聊天记录管理器方案.md §7。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/HistoryCoverage.h"
#include "sqlite3.h"
#include <string>
#include <vector>
#include <windows.h>

using namespace balloonhistory;

namespace {

//会话类型（与 net::conversation_type 取值一致）
const int kSingle = 1;
const int kGroup  = 2;

//消息类型（与 net/Protocol.h 的 message_type 取值一致）
const int kTypeText  = 1;
const int kTypeImage = 2;
const int kTypeFile  = 3;

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

//宽字符路径转 UTF-8（sqlite3_open 要 UTF-8 路径）
std::string WidePathToUtf8(const std::wstring& path)
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

//构造一条入库记录
//  msgid / ctype / cid / fromid / msgtype / timestamp：对应列的值
//  text：正文，同时写进 content（{"text":...}）与 search_text
//  recalled：是否已撤回
HistoryRecord MakeRecord(int64_t msgid, int ctype, int64_t cid, int64_t fromid, int msgtype,
                         int64_t timestamp, const std::string& text, bool recalled)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = fromid;
    rec.m_msgtype = msgtype;
    rec.m_timestamp = timestamp;
    rec.m_content = "{\"text\":\"" + text + "\"}";
    rec.m_searchText = text;
    rec.m_recalled = recalled;
    return rec;
}

//覆盖区间
//  lo / hi：下界、上界 msgid；reachedOldest：是否已拉到最早
CoverRange MakeCover(int64_t lo, int64_t hi, bool reachedOldest)
{
    CoverRange cover;
    cover.m_valid = true;
    cover.m_lo = lo;
    cover.m_hi = hi;
    cover.m_reachedOldest = reachedOldest;
    return cover;
}

//在结果里找某个会话，找不到返回 -1
int FindBrief(const std::vector<LocalConversationBrief>& briefs, int ctype, int64_t cid)
{
    for (size_t i = 0; i < briefs.size(); ++i)
    {
        if (briefs[i].m_ctype == ctype && briefs[i].m_cid == cid)
        {
            return (int)i;
        }
    }
    return -1;
}

//在搜索结果里找某个会话，找不到返回 -1
int FindHit(const std::vector<ConversationHit>& hits, int ctype, int64_t cid)
{
    for (size_t i = 0; i < hits.size(); ++i)
    {
        if (hits[i].m_ctype == ctype && hits[i].m_cid == cid)
        {
            return (int)i;
        }
    }
    return -1;
}

} // namespace

//==================== QueryConversationList ====================

TEST_CASE("HMS1 空库：返回成功，结果为空", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    std::vector<LocalConversationBrief> briefs;
    briefs.push_back(LocalConversationBrief());   //调用前的内容应被清空
    REQUIRE(store.QueryConversationList(briefs));
    CHECK(briefs.empty());
}

TEST_CASE("HMS2 单聊群聊混合：每个会话一项，字段取自 msgid 最大的那条，按最后时间倒序", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //单聊 10：三条，最后一条 msgid 103（时间 3000，发送人 7）
    REQUIRE(store.Upsert(MakeRecord(101, kSingle, 10, 10, kTypeText, 1000, "a", false)));
    REQUIRE(store.Upsert(MakeRecord(103, kSingle, 10, 7, kTypeImage, 3000, "c", false)));
    REQUIRE(store.Upsert(MakeRecord(102, kSingle, 10, 10, kTypeText, 2000, "b", false)));
    //群聊 600001：两条，最后一条时间 5000
    REQUIRE(store.Upsert(MakeRecord(201, kGroup, 600001, 8, kTypeText, 4000, "g1", false)));
    REQUIRE(store.Upsert(MakeRecord(202, kGroup, 600001, 9, kTypeText, 5000, "g2", false)));
    //单聊 11：一条，时间 2500
    REQUIRE(store.Upsert(MakeRecord(150, kSingle, 11, 11, kTypeText, 2500, "x", false)));

    std::vector<LocalConversationBrief> briefs;
    REQUIRE(store.QueryConversationList(briefs));
    REQUIRE(briefs.size() == 3);

    //按最后时间倒序：群 600001（5000）→ 单聊 10（3000）→ 单聊 11（2500）
    CHECK(briefs[0].m_ctype == kGroup);
    CHECK(briefs[0].m_cid == 600001);
    CHECK(briefs[1].m_cid == 10);
    CHECK(briefs[2].m_cid == 11);

    const LocalConversationBrief& b10 = briefs[1];
    CHECK(b10.m_ctype == kSingle);
    CHECK(b10.m_lastMsgid == 103);
    CHECK(b10.m_lastTimestamp == 3000);
    CHECK(b10.m_lastFromid == 7);
    CHECK(b10.m_lastMsgtype == kTypeImage);
    CHECK(b10.m_lastContent == "{\"text\":\"c\"}");

    CHECK(briefs[0].m_lastMsgid == 202);
    CHECK(briefs[0].m_lastFromid == 9);
}

TEST_CASE("HMS3 同一个 cid 既有单聊记录又有群聊记录时算两个会话", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(301, kSingle, 600001, 600001, kTypeText, 1000, "single", false)));
    REQUIRE(store.Upsert(MakeRecord(302, kGroup, 600001, 5, kTypeText, 2000, "group", false)));

    std::vector<LocalConversationBrief> briefs;
    REQUIRE(store.QueryConversationList(briefs));
    REQUIRE(briefs.size() == 2);
    const int single = FindBrief(briefs, kSingle, 600001);
    const int group = FindBrief(briefs, kGroup, 600001);
    REQUIRE(single >= 0);
    REQUIRE(group >= 0);
    CHECK(briefs[(size_t)single].m_lastMsgid == 301);
    CHECK(briefs[(size_t)group].m_lastMsgid == 302);
}

TEST_CASE("HMS4 最后一条已撤回时取前一条未撤回的；只剩撤回消息的会话不出现", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms4.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //会话 10：最新两条都撤回了，最后一条应是 401
    REQUIRE(store.Upsert(MakeRecord(401, kSingle, 10, 10, kTypeText, 1000, "keep", false)));
    REQUIRE(store.Upsert(MakeRecord(402, kSingle, 10, 10, kTypeText, 2000, "r1", true)));
    REQUIRE(store.Upsert(MakeRecord(403, kSingle, 10, 10, kTypeText, 3000, "r2", false)));
    REQUIRE(store.MarkRecalled(kSingle, 10, 403));
    //会话 11：唯一的一条撤回了
    REQUIRE(store.Upsert(MakeRecord(410, kSingle, 11, 11, kTypeText, 5000, "gone", true)));

    std::vector<LocalConversationBrief> briefs;
    REQUIRE(store.QueryConversationList(briefs));
    REQUIRE(briefs.size() == 1);
    CHECK(briefs[0].m_cid == 10);
    CHECK(briefs[0].m_lastMsgid == 401);
    CHECK(briefs[0].m_lastTimestamp == 1000);
}

TEST_CASE("HMS5 覆盖区间：没有区间、最后一条在区间内、已拉到最早都列出；全部在下界以下不列出", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms5.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //会话 10：没有区间
    REQUIRE(store.Upsert(MakeRecord(50, kSingle, 10, 10, kTypeText, 500, "a", false)));
    //会话 11：区间 [100, 200] 没到最早，最后一条 150 在区间内
    REQUIRE(store.Upsert(MakeRecord(60, kSingle, 11, 11, kTypeText, 600, "b-old", false)));
    REQUIRE(store.Upsert(MakeRecord(150, kSingle, 11, 11, kTypeText, 1500, "b-new", false)));
    REQUIRE(store.SetCoverage(kSingle, 11, MakeCover(100, 200, false)));
    //会话 12：区间 [100, 200] 没到最早，全部行都在下界以下
    REQUIRE(store.Upsert(MakeRecord(70, kSingle, 12, 12, kTypeText, 700, "c1", false)));
    REQUIRE(store.Upsert(MakeRecord(80, kSingle, 12, 12, kTypeText, 800, "c2", false)));
    REQUIRE(store.SetCoverage(kSingle, 12, MakeCover(100, 200, false)));
    //会话 13：区间 [100, 200] 已到最早，即使行在下界以下也可见
    REQUIRE(store.Upsert(MakeRecord(90, kSingle, 13, 13, kTypeText, 900, "d", false)));
    REQUIRE(store.SetCoverage(kSingle, 13, MakeCover(100, 200, true)));

    std::vector<LocalConversationBrief> briefs;
    REQUIRE(store.QueryConversationList(briefs));
    CHECK(briefs.size() == 3);
    CHECK(FindBrief(briefs, kSingle, 10) >= 0);
    CHECK(FindBrief(briefs, kSingle, 12) < 0);
    CHECK(FindBrief(briefs, kSingle, 13) >= 0);
    const int i11 = FindBrief(briefs, kSingle, 11);
    REQUIRE(i11 >= 0);
    CHECK(briefs[(size_t)i11].m_lastMsgid == 150);
}

TEST_CASE("HMS6 msgid 较大的那条时间反而较早时，仍以 msgid 最大的为最后一条", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms6.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //发送方时钟偏差这类极端情况：msgid 502 的时间比 501 早
    REQUIRE(store.Upsert(MakeRecord(501, kSingle, 10, 10, kTypeText, 9000, "earlier-id", false)));
    REQUIRE(store.Upsert(MakeRecord(502, kSingle, 10, 10, kTypeText, 8000, "later-id", false)));

    std::vector<LocalConversationBrief> briefs;
    REQUIRE(store.QueryConversationList(briefs));
    REQUIRE(briefs.size() == 1);
    CHECK(briefs[0].m_lastMsgid == 502);
    CHECK(briefs[0].m_lastTimestamp == 8000);
}

TEST_CASE("HMS7 两个会话最后时间相同时按最后一条的 msgid 倒序，顺序稳定", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms7.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(601, kSingle, 10, 10, kTypeText, 7000, "a", false)));
    REQUIRE(store.Upsert(MakeRecord(605, kSingle, 20, 20, kTypeText, 7000, "b", false)));
    REQUIRE(store.Upsert(MakeRecord(603, kGroup, 600001, 5, kTypeText, 7000, "c", false)));

    std::vector<LocalConversationBrief> briefs;
    REQUIRE(store.QueryConversationList(briefs));
    REQUIRE(briefs.size() == 3);
    CHECK(briefs[0].m_lastMsgid == 605);
    CHECK(briefs[1].m_lastMsgid == 603);
    CHECK(briefs[2].m_lastMsgid == 601);

    //再查一次，顺序不变
    std::vector<LocalConversationBrief> again;
    REQUIRE(store.QueryConversationList(again));
    REQUIRE(again.size() == 3);
    for (size_t i = 0; i < again.size(); ++i)
    {
        CHECK(again[i].m_lastMsgid == briefs[i].m_lastMsgid);
    }
}

TEST_CASE("HMS8 数据库没有打开时返回 false，出参被清空", "[historymgr]")
{
    CV2MsgStore store;
    std::vector<LocalConversationBrief> briefs;
    briefs.push_back(LocalConversationBrief());
    CHECK_FALSE(store.QueryConversationList(briefs));
    CHECK(briefs.empty());

    std::vector<ConversationHit> hits;
    hits.push_back(ConversationHit());
    std::vector<HistoryConversationKey> scope;
    CHECK_FALSE(store.SearchConversationsForManager("x", 10, scope, hits));
    CHECK(hits.empty());
}

//==================== SearchConversationsForManager / BuildManagerSearchSql ====================

TEST_CASE("HMS9 BuildManagerSearchSql：不限范围时没有 IN 子句；限定范围时逐个写进 VALUES", "[historymgr]")
{
    std::vector<HistoryConversationKey> scope;
    const std::string all = BuildManagerSearchSql(50, scope);
    CHECK(all.find("IN (VALUES") == std::string::npos);
    CHECK(all.find("LIKE ?") != std::string::npos);
    CHECK(all.find("LIMIT 50") != std::string::npos);
    //不排除文件类消息
    CHECK(all.find("msgtype NOT IN") == std::string::npos);

    scope.push_back(HistoryConversationKey(kSingle, 10));
    scope.push_back(HistoryConversationKey(kGroup, 600001));
    const std::string scoped = BuildManagerSearchSql(0, scope);
    CHECK(scoped.find("(m.ctype,m.cid) IN (VALUES (1,10),(2,600001))") != std::string::npos);
    //limit 不大于 0 按 1 处理
    CHECK(scoped.find("LIMIT 1") != std::string::npos);
}

TEST_CASE("HMS10 管理器搜索：文件名也能搜到，条数与最近一条正确，按最近命中时间倒序", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms10.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //会话 10：两条正文命中 + 一个文件名命中
    REQUIRE(store.Upsert(MakeRecord(701, kSingle, 10, 10, kTypeText, 1000, "周报 第一版", false)));
    REQUIRE(store.Upsert(MakeRecord(702, kSingle, 10, 10, kTypeText, 2000, "周报 第二版", false)));
    REQUIRE(store.Upsert(MakeRecord(703, kSingle, 10, 10, kTypeFile, 3000, "周报.docx", false)));
    REQUIRE(store.Upsert(MakeRecord(704, kSingle, 10, 10, kTypeText, 3500, "无关", false)));
    //群 600001：一条命中，时间最新
    REQUIRE(store.Upsert(MakeRecord(801, kGroup, 600001, 5, kTypeText, 9000, "本周周报汇总", false)));
    //会话 11：没有命中
    REQUIRE(store.Upsert(MakeRecord(901, kSingle, 11, 11, kTypeText, 9500, "午饭", false)));

    std::vector<HistoryConversationKey> scope;
    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversationsForManager("周报", 10, scope, hits));
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].m_ctype == kGroup);
    CHECK(hits[0].m_cid == 600001);
    CHECK(hits[0].m_hitCount == 1);
    CHECK(hits[0].m_latestText == "本周周报汇总");
    CHECK(hits[1].m_cid == 10);
    CHECK(hits[1].m_hitCount == 3);
    CHECK(hits[1].m_latestMsgid == 703);

    //对照：主面板搜索下拉的 SearchConversations 不计文件，同一关键字在会话 10 只有 2 条
    std::vector<ConversationHit> panelHits;
    REQUIRE(store.SearchConversations("周报", 10, panelHits));
    const int i10 = FindHit(panelHits, kSingle, 10);
    REQUIRE(i10 >= 0);
    CHECK(panelHits[(size_t)i10].m_hitCount == 2);
}

TEST_CASE("HMS11 管理器搜索限定范围：只统计范围里的会话，范围里没有记录的会话不出现", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms11.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    REQUIRE(store.Upsert(MakeRecord(1001, kSingle, 10, 10, kTypeText, 1000, "上线计划", false)));
    REQUIRE(store.Upsert(MakeRecord(1002, kSingle, 11, 11, kTypeText, 2000, "上线计划", false)));
    REQUIRE(store.Upsert(MakeRecord(1003, kGroup, 600001, 5, kTypeText, 3000, "上线计划", false)));
    //同一 cid 的单聊：范围里写的是群 600001，它不应被算进去
    REQUIRE(store.Upsert(MakeRecord(1004, kSingle, 600001, 600001, kTypeText, 4000, "上线计划", false)));

    std::vector<HistoryConversationKey> scope;
    scope.push_back(HistoryConversationKey(kSingle, 11));
    scope.push_back(HistoryConversationKey(kGroup, 600001));
    scope.push_back(HistoryConversationKey(kSingle, 99));   //本地没有记录的会话
    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversationsForManager("上线", 10, scope, hits));
    REQUIRE(hits.size() == 2);
    CHECK(FindHit(hits, kSingle, 11) >= 0);
    CHECK(FindHit(hits, kGroup, 600001) >= 0);
    CHECK(FindHit(hits, kSingle, 10) < 0);
    CHECK(FindHit(hits, kSingle, 600001) < 0);

    //范围为空即不限
    scope.clear();
    REQUIRE(store.SearchConversationsForManager("上线", 10, scope, hits));
    CHECK(hits.size() == 4);
}

TEST_CASE("HMS12 管理器搜索：排除撤回、遵守覆盖区间、空关键字无结果、limit 非法返回 false", "[historymgr]")
{
    std::wstring path = FreshTempDb(L"flamingo_hms12.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //会话 10：一条撤回的命中、一条正常命中
    REQUIRE(store.Upsert(MakeRecord(1101, kSingle, 10, 10, kTypeText, 1000, "报销单", true)));
    REQUIRE(store.Upsert(MakeRecord(1102, kSingle, 10, 10, kTypeText, 2000, "报销单", false)));
    //会话 11：命中在断层以下
    REQUIRE(store.Upsert(MakeRecord(50, kSingle, 11, 11, kTypeText, 500, "报销单", false)));
    REQUIRE(store.Upsert(MakeRecord(150, kSingle, 11, 11, kTypeText, 1500, "别的", false)));
    REQUIRE(store.SetCoverage(kSingle, 11, MakeCover(100, 200, false)));

    std::vector<HistoryConversationKey> scope;
    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversationsForManager("报销", 10, scope, hits));
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 10);
    CHECK(hits[0].m_hitCount == 1);

    hits.push_back(ConversationHit());
    REQUIRE(store.SearchConversationsForManager("", 10, scope, hits));
    CHECK(hits.empty());

    CHECK_FALSE(store.SearchConversationsForManager("报销", 0, scope, hits));
}

//==================== 性能 ====================

namespace {

//用 SQLite 的 C 接口在一个事务里灌数据（灌数据不是被测对象，逐条 Upsert 太慢）。
//  convCount：会话个数；perConv：每个会话的消息条数；每 100 条里有 1 条带关键字「季度复盘」
void FillBenchDb(const std::wstring& path, int convCount, int perConv)
{
    const int kHitEvery = 100;
    sqlite3* db = NULL;
    const std::string pathUtf8 = WidePathToUtf8(path);
    REQUIRE(sqlite3_open(pathUtf8.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_stmt* stmt = NULL;
    REQUIRE(sqlite3_prepare_v2(db,
        "INSERT INTO t_msg(msgid,ctype,cid,fromid,msgtype,content,search_text,timestamp) "
        "VALUES(?,?,?,1,1,'{}',?,?)", -1, &stmt, NULL) == SQLITE_OK);
    const std::string plain = "这一版的改动比较多，主要集中在消息列表和输入区两块，大家回归的时候重点看一下。";
    const std::string withHit = "季度复盘的材料我放共享目录了。" + plain;
    int64_t msgid = 0;
    for (int c = 0; c < convCount; ++c)
    {
        //一半单聊、一半群聊
        const int ctype = (c % 2 == 0) ? kSingle : kGroup;
        const int64_t cid = 100000 + c;
        for (int i = 0; i < perConv; ++i)
        {
            ++msgid;
            const std::string& text = (msgid % kHitEvery == 0) ? withHit : plain;
            sqlite3_bind_int64(stmt, 1, msgid);
            sqlite3_bind_int(stmt, 2, ctype);
            sqlite3_bind_int64(stmt, 3, cid);
            sqlite3_bind_text(stmt, 4, text.c_str(), (int)text.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, msgid * 1000);
            REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_reset(stmt);
        }
    }
    sqlite3_finalize(stmt);
    REQUIRE(sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
}

//重复次数：每种查询跑几次取平均
const int kBenchRepeat = 5;

//跑一组性能测量并打印：会话列表、全部会话搜索、限定 10 个会话的搜索
void RunManagerBench(const wchar_t* dbName, int convCount, int perConv)
{
    std::wstring path = FreshTempDb(dbName);
    {
        CV2MsgStore creator;
        REQUIRE(creator.Open(path.c_str()));
    }
    FillBenchDb(path, convCount, perConv);

    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    const int kScopeSize = 10;
    std::vector<HistoryConversationKey> scope;
    for (int c = 0; c < kScopeSize && c < convCount; ++c)
    {
        scope.push_back(HistoryConversationKey((c % 2 == 0) ? kSingle : kGroup, 100000 + c));
    }
    const std::vector<HistoryConversationKey> noScope;

    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    double listMs = 0;
    double searchAllMs = 0;
    double searchScopedMs = 0;
    size_t listSize = 0;
    size_t hitConvs = 0;
    const int kSearchLimit = 200;
    for (int r = 0; r < kBenchRepeat; ++r)
    {
        LARGE_INTEGER t0;
        LARGE_INTEGER t1;
        LARGE_INTEGER t2;
        LARGE_INTEGER t3;
        std::vector<LocalConversationBrief> briefs;
        std::vector<ConversationHit> hits;
        std::vector<ConversationHit> scopedHits;
        ::QueryPerformanceCounter(&t0);
        REQUIRE(store.QueryConversationList(briefs));
        ::QueryPerformanceCounter(&t1);
        REQUIRE(store.SearchConversationsForManager("季度复盘", kSearchLimit, noScope, hits));
        ::QueryPerformanceCounter(&t2);
        REQUIRE(store.SearchConversationsForManager("季度复盘", kSearchLimit, scope, scopedHits));
        ::QueryPerformanceCounter(&t3);
        listMs += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        searchAllMs += static_cast<double>(t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;
        searchScopedMs += static_cast<double>(t3.QuadPart - t2.QuadPart) * 1000.0 / freq.QuadPart;
        listSize = briefs.size();
        hitConvs = hits.size();
    }
    WARN("HMB1 " << convCount << " conversations x " << perConv << " rows: list "
         << listMs / kBenchRepeat << " ms (" << listSize << " conversations), search all "
         << searchAllMs / kBenchRepeat << " ms (" << hitConvs << " conversations hit), search in "
         << kScopeSize << " conversations " << searchScopedMs / kBenchRepeat << " ms");
    CHECK(listSize == (size_t)convCount);
}

} // namespace

TEST_CASE("HMB1 几十万行消息时会话列表与跨会话搜索的耗时", "[.][historymgrbench]")
{
    RunManagerBench(L"flamingo_hmb1_a.db", 500, 1000);
    RunManagerBench(L"flamingo_hmb1_b.db", 2000, 100);
}
