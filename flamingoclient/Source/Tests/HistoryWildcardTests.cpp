/**
 *  聊天记录管理器通配符搜索纯逻辑的单元测试（Suite WC），HistoryWildcardTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryWildcard.h（docs/聊天记录管理器方案.md §11.4）：
 *    WC1 通配符转 LIKE 匹配串；WC2 去掉 '*' 后是否还有内容；WC3 全文索引查询串；
 *    WC4 高亮区间（最左最短、不重叠、代理对、大小写）；
 *    WC5 用 SQLite 内存库确认 LIKE 的实际行为（'_' 按字符而不是按字节匹配、ESCAPE 生效）；
 *    WC6 同一组「文字 + 模式」上，SQLite 用 WC1 的结果做 LIKE 与 WC4 找高亮，两者结论一致。
 *  以及接入本地库之后（§11.8 第 5b 步，用临时 db 文件跑真实 SQLite）：
 *    WCS1 会话内的 Count / QueryPage / CountBefore：不勾通配符时 % _ \ * 按普通字符，
 *         勾选时 * ? 生效，只输了 * 等于没有关键字；
 *    WCS2 HistoryModel：切换通配符重置页号并重新统计，HasKeywordFilter / ShouldResetListScroll /
 *         ShouldOfferViewContext 认通配符；
 *    WCS3 管理器跨会话搜索：勾选通配符时用与不用全文索引结果完全相同，并核对几个关键字的命中；
 *    WCS4 后台查询线程的请求把「是否通配符」传给本地库，结果原样带回。
 *  框架 Catch2 v3，标签 [wildcard]。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <string>
#include <vector>
//sqlite3.h 由工程的附加包含目录（third-party/sqlite-src-3530100）提供，不写相对路径。
#include "sqlite3.h"
#include "../UI/BalloonHistory/HistoryWildcard.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonHistory/HistoryModel.h"
#include "../UI/BalloonHistory/V2MsgSearchWorker.h"

using namespace balloonhistory;

namespace {

//UTF-8 转 UTF-16，供 WC6 把同一组样本分别交给 SQLite（UTF-8）与高亮函数（UTF-16）
std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
    {
        return std::wstring();
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

//在 SQLite 内存库里执行一次 text LIKE pattern ESCAPE '\'，返回是否匹配。
//语句或连接出错时让用例失败，返回 false
bool SqliteLike(sqlite3* db, const std::string& text, const std::string& pattern)
{
    sqlite3_stmt* stmt = NULL;
    const int rcPrepare = sqlite3_prepare_v2(db, "SELECT ?1 LIKE ?2 ESCAPE '\\'", -1, &stmt, NULL);
    REQUIRE(rcPrepare == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, text.c_str(), (int)text.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.c_str(), (int)pattern.size(), SQLITE_TRANSIENT);
    const int rcStep = sqlite3_step(stmt);
    REQUIRE(rcStep == SQLITE_ROW);
    const bool matched = sqlite3_column_int(stmt, 0) != 0;
    sqlite3_finalize(stmt);
    return matched;
}

//打开一个 SQLite 内存库，用例结束时关闭
struct MemoryDb
{
    sqlite3* m_db;   //内存库连接，构造时打开、析构时关闭

    MemoryDb()
        : m_db(NULL)
    {
        const int rc = sqlite3_open(":memory:", &m_db);
        REQUIRE(rc == SQLITE_OK);
    }

    ~MemoryDb()
    {
        if (m_db != NULL)
        {
            sqlite3_close(m_db);
        }
    }
};

//一个高亮区间是否为 (begin, len)
bool IsRange(const HighlightRange& r, int begin, int len)
{
    return r.m_begin == begin && r.m_len == len;
}

} // namespace

TEST_CASE("WC1 通配符转 LIKE 匹配串：* 换 %、? 换 _，LIKE 的特殊字符转义，% 不重复", "[wildcard]")
{
    CHECK(MakeWildcardLikePattern("季度*报告") == "%季度%报告%");
    CHECK(MakeWildcardLikePattern("a?c") == "%a_c%");
    //首尾的 * 与外加的 % 合并
    CHECK(MakeWildcardLikePattern("*报告") == "%报告%");
    CHECK(MakeWildcardLikePattern("报告*") == "%报告%");
    CHECK(MakeWildcardLikePattern("*报告*") == "%报告%");
    //连续的 * 合并
    CHECK(MakeWildcardLikePattern("a**b") == "%a%b%");
    //输入里的 % _ \ 按普通字符转义
    CHECK(MakeWildcardLikePattern("50%*") == "%50\\%%");
    CHECK(MakeWildcardLikePattern("a_b?") == "%a\\_b_%");
    CHECK(MakeWildcardLikePattern("c:\\x") == "%c:\\\\x%");
    //转义过的 \% 后面紧跟 * 时，仍要补一个起通配作用的 %
    CHECK(MakeWildcardLikePattern("%*a") == "%\\%%a%");
    //只有 * 时为单个 %（调用方应先用 WildcardHasContent 排除）
    CHECK(MakeWildcardLikePattern("*") == "%");
    CHECK(MakeWildcardLikePattern("") == "%");

    //分派函数：不勾通配符时与 MakeLikePattern 相同，* ? 按普通字符
    CHECK(MakeSearchLikePattern("a*b?", false) == MakeLikePattern("a*b?"));
    CHECK(MakeSearchLikePattern("a*b?", false) == "%a*b?%");
    CHECK(MakeSearchLikePattern("a*b?", true) == "%a%b_%");
}

TEST_CASE("WC2 去掉 * 之后是否还有内容", "[wildcard]")
{
    CHECK_FALSE(WildcardHasContent(""));
    CHECK_FALSE(WildcardHasContent("*"));
    CHECK_FALSE(WildcardHasContent("**"));
    CHECK(WildcardHasContent("?"));
    CHECK(WildcardHasContent("*a*"));
    CHECK(WildcardHasContent("季度"));
}

TEST_CASE("WC3 全文索引查询串：只收不少于 3 个字的段，段间用 AND 连接", "[wildcard]")
{
    std::string q;
    CHECK(BuildWildcardFtsQuery("第三季度*工作报告", q));
    CHECK(q == "\"第三季度\" AND \"工作报告\"");

    //两段都只有两个字：没有可用的段，调用方改走整表 LIKE
    CHECK_FALSE(BuildWildcardFtsQuery("季度*报告", q));
    CHECK(q.empty());

    //? 同样切段；不够 3 个字的段不放进去
    CHECK(BuildWildcardFtsQuery("周报?模板文件", q));
    CHECK(q == "\"模板文件\"");

    //段里的双引号写成两个（MakeFtsPhrase 的规则）
    CHECK(BuildWildcardFtsQuery("say\"hi*", q));
    CHECK(q == "\"say\"\"hi\"");

    //按 Unicode 字符数，不按字节：「😀ab」是 3 个字
    CHECK(BuildWildcardFtsQuery("\xF0\x9F\x98\x80" "ab", q));

    CHECK_FALSE(BuildWildcardFtsQuery("*", q));
    CHECK_FALSE(BuildWildcardFtsQuery("??", q));
    CHECK_FALSE(BuildWildcardFtsQuery("", q));
}

TEST_CASE("WC4 高亮区间：最左最短、不重叠、代理对算一个字、只忽略 ASCII 大小写", "[wildcard]")
{
    //两处命中，各自取最短的那段
    std::vector<HighlightRange> r = FindWildcardHighlightRanges(L"第三季度工作报告和季度报告", L"季度*报告");
    REQUIRE(r.size() == 2);
    CHECK(IsRange(r[0], 2, 6));
    CHECK(IsRange(r[1], 9, 4));

    //最短：不会把「度报告和年度报告」整段标上
    r = FindWildcardHighlightRanges(L"季度报告和年度报告", L"度*报告");
    REQUIRE(r.size() == 2);
    CHECK(IsRange(r[0], 1, 3));
    CHECK(IsRange(r[1], 6, 3));

    //? 恰好一个字符
    r = FindWildcardHighlightRanges(L"abc axc ac", L"a?c");
    REQUIRE(r.size() == 2);
    CHECK(IsRange(r[0], 0, 3));
    CHECK(IsRange(r[1], 4, 3));

    //只忽略 ASCII 大小写
    r = FindWildcardHighlightRanges(L"q3 annual report", L"Q3*REPORT");
    REQUIRE(r.size() == 1);
    CHECK(IsRange(r[0], 0, 16));

    //命中不重叠
    r = FindWildcardHighlightRanges(L"aaa", L"a?");
    REQUIRE(r.size() == 1);
    CHECK(IsRange(r[0], 0, 2));

    //表情在 UTF-16 里是一对代理，? 把它算一个字
    const std::wstring withEmoji = std::wstring(L"A") + L"\xD83D\xDE00" + L"B";
    r = FindWildcardHighlightRanges(withEmoji, L"A?B");
    REQUIRE(r.size() == 1);
    CHECK(IsRange(r[0], 0, 4));
    r = FindWildcardHighlightRanges(withEmoji, L"?");
    REQUIRE(r.size() == 3);
    CHECK(IsRange(r[1], 1, 2));

    //% _ \ 按原样匹配
    r = FindWildcardHighlightRanges(L"增长 50% 了", L"50%");
    REQUIRE(r.size() == 1);
    CHECK(IsRange(r[0], 3, 3));

    //首尾的 * 不影响结果
    r = FindWildcardHighlightRanges(L"季度报告", L"*报告*");
    REQUIRE(r.size() == 1);
    CHECK(IsRange(r[0], 2, 2));

    //顺序不对、为空、只有 * 时都没有命中
    CHECK(FindWildcardHighlightRanges(L"报告季度", L"季度*报告").empty());
    CHECK(FindWildcardHighlightRanges(L"季度报告", L"").empty());
    CHECK(FindWildcardHighlightRanges(L"季度报告", L"**").empty());
    CHECK(FindWildcardHighlightRanges(L"", L"季度").empty());

    //分派函数：不勾通配符时 * 按普通字符
    CHECK(FindSearchHighlightRanges(L"a*b ab", L"a*b", false).size() == 1);
    CHECK(FindSearchHighlightRanges(L"a*b ab", L"a*b", true).size() == 2);
}

TEST_CASE("WC5 SQLite 的 LIKE：_ 按字符而不是按字节匹配，ESCAPE 生效，只忽略 ASCII 大小写", "[wildcard]")
{
    MemoryDb mem;
    //「度」在 UTF-8 里是 3 个字节，一个 _ 就能匹配它；两个 _ 匹配不上
    CHECK(SqliteLike(mem.m_db, "季度报告", "%季_报%"));
    CHECK_FALSE(SqliteLike(mem.m_db, "季度报告", "%季__报%"));
    //4 字节的表情也算一个字符
    CHECK(SqliteLike(mem.m_db, "a\xF0\x9F\x98\x80" "b", "a_b"));
    //转义过的 % 按普通字符匹配
    CHECK_FALSE(SqliteLike(mem.m_db, "500", "%50\\%%"));
    CHECK(SqliteLike(mem.m_db, "50%", "%50\\%%"));
    //ASCII 字母不区分大小写
    CHECK(SqliteLike(mem.m_db, "REPORT", "%report%"));
}

TEST_CASE("WC6 SQLite LIKE 与高亮函数在同一组样本上的结论一致", "[wildcard]")
{
    struct Sample
    {
        const char* m_text;      //被搜索的文字（UTF-8）
        const char* m_pattern;   //通配符输入（UTF-8）
        bool        m_expected;  //是否应当命中
    };
    const Sample samples[] =
    {
        { "第三季度工作报告", "季度*报告", true },
        { "报告季度", "季度*报告", false },
        { "abc", "a?c", true },
        { "ac", "a?c", false },
        { "季度报告", "季?报", true },
        { "季度报告", "季??报", false },
        { "a\xF0\x9F\x98\x80" "b", "a?b", true },
        { "增长 50% 了", "50%", true },
        { "增长 500 了", "50%", false },
        { "a_b", "a_b", true },
        { "axb", "a_b", false },
        { "Q3 Report", "q3*REPORT", true },
        { "c:\\x\\y", "c:\\*y", true },
        { "x", "??", false },
        { "xy", "??", true },
        { "", "a", false },
        { "abc", "*b*", true },
        { "周报模板文件", "周报?模板", false },
        { "周报的模板文件", "周报?模板", true },
    };

    MemoryDb mem;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        const Sample& s = samples[i];
        INFO("sample " << i << ": text=" << s.m_text << " pattern=" << s.m_pattern);
        const bool byLike = SqliteLike(mem.m_db, s.m_text, MakeWildcardLikePattern(s.m_pattern));
        const bool byHighlight = !FindWildcardHighlightRanges(Utf8ToWide(s.m_text), Utf8ToWide(s.m_pattern)).empty();
        CHECK(byLike == s.m_expected);
        CHECK(byHighlight == s.m_expected);
    }
}

// ---------------------------------------------------------------------------
// WCS1～WCS4  接入本地库（§11.8 第 5b 步）
// ---------------------------------------------------------------------------

namespace {

//会话类型（与 net::conversation_type 一致）
const int kWcSingle = 1;
const int kWcGroup  = 2;
//消息类型（与 net/Protocol.h 的 message_type 一致）
const int kWcText  = 1;
const int kWcImage = 2;
const int kWcFile  = 3;
//WCS1、WCS2 用的会话：单聊 cid=10；另有一个单聊 cid=11 放同样的文字，核对会话内查询不串会话
const int64_t kWcConv      = 10;
const int64_t kWcOtherConv = 11;
//跨会话搜索的上限：足够放下全部测试数据
const int kWcLimit = 100;
//「近 N 天」换算用的当前时间；本组数据只用「全部时间」，取值无关紧要
const int64_t kWcNowMs = 100000;

//取一个临时目录下的 db 全路径，并先删掉同名旧文件与 WAL 残留（文件名带进程号）
std::wstring FreshWildcardDb(const wchar_t* name)
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
HistoryRecord MakeWcRecord(int64_t msgid, int ctype, int64_t cid, int msgtype, int64_t timestamp,
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

//WCS1、WCS2 的数据：会话 (1,10) 十条，时间与 msgid 同序；会话 (1,11) 一条同样的文字
void SeedConversationStore(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(MakeWcRecord(1, kWcSingle, kWcConv, kWcText, 1000, "增长 50% 了")));
    REQUIRE(store.Upsert(MakeWcRecord(2, kWcSingle, kWcConv, kWcText, 2000, "增长 500 了")));
    REQUIRE(store.Upsert(MakeWcRecord(3, kWcSingle, kWcConv, kWcText, 3000, "a_b 变量")));
    REQUIRE(store.Upsert(MakeWcRecord(4, kWcSingle, kWcConv, kWcText, 4000, "axb 变量")));
    REQUIRE(store.Upsert(MakeWcRecord(5, kWcSingle, kWcConv, kWcText, 5000, "路径 c:\\x")));
    REQUIRE(store.Upsert(MakeWcRecord(6, kWcSingle, kWcConv, kWcText, 6000, "第三季度工作报告")));
    REQUIRE(store.Upsert(MakeWcRecord(7, kWcSingle, kWcConv, kWcText, 7000, "季度报告")));
    REQUIRE(store.Upsert(MakeWcRecord(8, kWcSingle, kWcConv, kWcText, 8000, "报告季度")));
    REQUIRE(store.Upsert(MakeWcRecord(9, kWcSingle, kWcConv, kWcText, 9000, "Q3 REPORT")));
    //图片没有可搜的文字
    REQUIRE(store.Upsert(MakeWcRecord(10, kWcSingle, kWcConv, kWcImage, 10000, "")));
    REQUIRE(store.Upsert(MakeWcRecord(11, kWcSingle, kWcOtherConv, kWcText, 11000, "季度报告")));
}

//会话 (1,10) 的过滤条件：全部类型、全部时间、每页放得下全部数据
HistoryFilter ConvFilter(const std::string& keyword, bool wildcard)
{
    HistoryFilter f;
    f.m_ctype = kWcSingle;
    f.m_cid = kWcConv;
    f.m_keyword = keyword;
    f.m_wildcard = wildcard;
    f.m_page = 1;
    f.m_pageSize = kWcLimit;
    return f;
}

//按过滤条件取一页，返回各条的 msgid（时间倒序）
std::vector<int64_t> PageMsgids(CV2MsgStore& store, const HistoryFilter& f)
{
    std::vector<HistoryRecord> recs;
    REQUIRE(store.QueryPage(f, kWcNowMs, recs));
    std::vector<int64_t> ids;
    for (size_t i = 0; i < recs.size(); ++i)
    {
        ids.push_back(recs[i].m_msgid);
    }
    return ids;
}

//WCS3、WCS4 的数据：几个会话里各有几条文字与文件
void SeedManagerStore(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(MakeWcRecord(101, kWcSingle, 20, kWcText, 1000, "Design Review 设计稿 v2")));
    REQUIRE(store.Upsert(MakeWcRecord(102, kWcSingle, 20, kWcText, 2000, "周五交设计稿")));
    REQUIRE(store.Upsert(MakeWcRecord(103, kWcGroup, 30, kWcText, 3000, "群里的设计稿")));
    REQUIRE(store.Upsert(MakeWcRecord(104, kWcGroup, 30, kWcFile, 4000, "设计稿.pdf")));
    REQUIRE(store.Upsert(MakeWcRecord(105, kWcGroup, 31, kWcText, 5000, "增长 50% 的报表")));
    REQUIRE(store.Upsert(MakeWcRecord(106, kWcSingle, 21, kWcText, 6000, "a_b_c 变量名")));
    REQUIRE(store.Upsert(MakeWcRecord(107, kWcSingle, 21, kWcText, 7000, "他说\"你好世界\"然后走了")));
    REQUIRE(store.Upsert(MakeWcRecord(108, kWcSingle, 22, kWcText, 8000, "第三季度工作报告")));
    REQUIRE(store.Upsert(MakeWcRecord(109, kWcSingle, 22, kWcText, 9000, "第三季度的工作周报告")));
}

//管理器跨会话搜索，失败时让用例失败
std::vector<ConversationHit> ManagerSearch(CV2MsgStore& store, const std::string& keyword, bool wildcard,
                                           const std::vector<HistoryConversationKey>& scope)
{
    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversationsForManager(keyword, kWcLimit, scope, hits, wildcard));
    return hits;
}

//比较两份会话汇总是否逐项相同
void CheckSameWcHits(const std::vector<ConversationHit>& a, const std::vector<ConversationHit>& b)
{
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK(a[i].m_ctype == b[i].m_ctype);
        CHECK(a[i].m_cid == b[i].m_cid);
        CHECK(a[i].m_hitCount == b[i].m_hitCount);
        CHECK(a[i].m_latestMsgid == b[i].m_latestMsgid);
    }
}

} // namespace

TEST_CASE("WCS1 会话内查询：不勾通配符时特殊字符按原样匹配，勾选时 * ? 生效", "[wildcard]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshWildcardDb(L"wcs1.db").c_str()));
    SeedConversationStore(store);

    //不勾通配符：% _ \ * 都按普通字符（原先没有转义，「50%」会把「500」也搜出来）
    CHECK(store.Count(ConvFilter("50%", false), kWcNowMs) == 1);
    CHECK(PageMsgids(store, ConvFilter("50%", false)) == std::vector<int64_t>{ 1 });
    CHECK(store.Count(ConvFilter("a_b", false), kWcNowMs) == 1);
    CHECK(PageMsgids(store, ConvFilter("a_b", false)) == std::vector<int64_t>{ 3 });
    CHECK(store.Count(ConvFilter("c:\\x", false), kWcNowMs) == 1);
    CHECK(store.Count(ConvFilter("季度*报告", false), kWcNowMs) == 0);
    //只在本会话里统计：会话 (1,11) 的「季度报告」不算
    CHECK(store.Count(ConvFilter("季度", false), kWcNowMs) == 3);

    //勾选通配符
    CHECK(store.Count(ConvFilter("季度*报告", true), kWcNowMs) == 2);
    CHECK(PageMsgids(store, ConvFilter("季度*报告", true)) == std::vector<int64_t>{ 7, 6 });
    CHECK(PageMsgids(store, ConvFilter("季?报告", true)) == std::vector<int64_t>{ 7 });
    CHECK(PageMsgids(store, ConvFilter("q3*report", true)) == std::vector<int64_t>{ 9 });
    //勾选时 % _ 仍按普通字符
    CHECK(store.Count(ConvFilter("50%", true), kWcNowMs) == 1);
    CHECK(store.Count(ConvFilter("a_b", true), kWcNowMs) == 1);
    //只输了 * 等于没有关键字：十条全有（含没有文字的图片）
    CHECK(store.Count(ConvFilter("*", true), kWcNowMs) == 10);
    CHECK(PageMsgids(store, ConvFilter("**", true)).size() == 10);
    //? 要求至少一个字符：没有文字的图片不算
    CHECK(store.Count(ConvFilter("?", true), kWcNowMs) == 9);

    //CountBefore 与 QueryPage 同一套条件：排在 msgid 6 前面的只有 msgid 7
    int64_t before = -1;
    REQUIRE(store.CountBefore(ConvFilter("季度*报告", true), kWcNowMs, 6000, 6, before));
    CHECK(before == 1);
    REQUIRE(store.CountBefore(ConvFilter("50%", false), kWcNowMs, 1000, 1, before));
    CHECK(before == 0);
}

TEST_CASE("WCS2 HistoryModel：切换通配符重置页号并重新统计，几个判断函数认通配符", "[wildcard]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshWildcardDb(L"wcs2.db").c_str()));
    SeedConversationStore(store);

    HistoryModel model;
    model.SetStore(&store);
    model.SetConversation(kWcSingle, kWcConv);
    model.SetPageSize(1);
    model.SetKeyword("季度*报告");
    REQUIRE(model.Refresh(kWcNowMs));
    CHECK(model.Total() == 0);

    model.SetWildcard(true);
    REQUIRE(model.Refresh(kWcNowMs));
    CHECK(model.Total() == 2);
    model.GoToPage(2);
    REQUIRE(model.RefreshPage(kWcNowMs));
    CHECK(model.CurrentPage() == 2);
    REQUIRE(model.Records().size() == 1);
    CHECK(model.Records()[0].m_msgid == 6);

    //取值没变：不重置页号；只换页时也不重新统计
    model.SetWildcard(true);
    CHECK(model.CurrentPage() == 2);
    const int64_t countsBefore = model.CountQueryTimes();
    REQUIRE(model.RefreshPage(kWcNowMs));
    CHECK(model.CountQueryTimes() == countsBefore);

    //取消勾选：回到第 1 页，统计条件变了要重新统计
    model.SetWildcard(false);
    CHECK(model.CurrentPage() == 1);
    REQUIRE(model.RefreshPage(kWcNowMs));
    CHECK(model.CountQueryTimes() == countsBefore + 1);
    CHECK(model.Total() == 0);

    //ResetSearchFilters 不改动复选框的状态
    model.SetWildcard(true);
    model.ResetSearchFilters();
    CHECK(model.Filter().m_wildcard);
    CHECK(model.Filter().m_keyword.empty());

    //关键字为空时切换复选框不影响结果：停在原来那一页
    REQUIRE(model.Refresh(kWcNowMs));
    model.GoToPage(3);
    model.SetWildcard(false);
    CHECK(model.CurrentPage() == 3);

    //HasKeywordFilter：勾选时只输了 * 不算关键字
    HistoryFilter f;
    CHECK_FALSE(HasKeywordFilter(f));
    f.m_keyword = "*";
    CHECK(HasKeywordFilter(f));
    f.m_wildcard = true;
    CHECK_FALSE(HasKeywordFilter(f));
    f.m_keyword = "*a";
    CHECK(HasKeywordFilter(f));

    //ShouldResetListScroll：有关键字时只切换通配符也算换了一批消息；没有关键字时不算
    HistoryFilter shown;
    shown.m_keyword = "季度*报告";
    HistoryFilter now = shown;
    now.m_wildcard = true;
    CHECK(ShouldResetListScroll(shown, now));
    HistoryFilter shownEmpty;
    HistoryFilter nowEmpty = shownEmpty;
    nowEmpty.m_wildcard = true;
    CHECK_FALSE(ShouldResetListScroll(shownEmpty, nowEmpty));

    //ShouldOfferViewContext：只输了 * 的通配符不算筛选条件
    HistoryFilter onlyStar;
    onlyStar.m_keyword = "*";
    onlyStar.m_wildcard = true;
    CHECK_FALSE(ShouldOfferViewContext(onlyStar, 5));
    onlyStar.m_wildcard = false;
    CHECK(ShouldOfferViewContext(onlyStar, 5));
}

TEST_CASE("WCS3 管理器跨会话搜索：勾选通配符时用与不用全文索引结果相同", "[wildcard]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshWildcardDb(L"wcs3.db").c_str()));
    SeedManagerStore(store);
    //新库打开即可用全文索引（FullTextSearchTests 的 FTS2）
    REQUIRE(store.IsFullTextReady());

    const char* const keywords[] =
    {
        "设计*稿", "Design*设计稿", "第三季度*工作", "第三季度*报告", "a?b", "50%", "你好*世界",
        "\"你好*", "*", "??", "不存在*的词", "设计稿.pdf", "工作?报告", "工作??报告", "review*v2",
    };
    std::vector<HistoryConversationKey> scope;
    scope.push_back(HistoryConversationKey(kWcSingle, 20));
    scope.push_back(HistoryConversationKey(kWcSingle, 22));
    const std::vector<HistoryConversationKey> noScope;
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
    {
        INFO("keyword: " << keywords[i]);
        store.SetFullTextAllowed(true);
        const std::vector<ConversationHit> fts = ManagerSearch(store, keywords[i], true, noScope);
        const std::vector<ConversationHit> ftsScoped = ManagerSearch(store, keywords[i], true, scope);
        store.SetFullTextAllowed(false);
        const std::vector<ConversationHit> like = ManagerSearch(store, keywords[i], true, noScope);
        const std::vector<ConversationHit> likeScoped = ManagerSearch(store, keywords[i], true, scope);
        store.SetFullTextAllowed(true);
        CheckSameWcHits(fts, like);
        CheckSameWcHits(ftsScoped, likeScoped);
    }

    //逐个核对几个关键字的命中
    std::vector<ConversationHit> hits = ManagerSearch(store, "第三季度*报告", true, noScope);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 22);
    CHECK(hits[0].m_hitCount == 2);

    //(1,20) 两条，(2,30) 两条（文字与文件都算）
    hits = ManagerSearch(store, "设计*稿", true, noScope);
    CHECK(hits.size() == 2);
    //「工作周报告」的「工作」与「报告」之间恰好一个字；「工作报告」之间没有字，不算
    hits = ManagerSearch(store, "工作?报告", true, noScope);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 22);
    CHECK(hits[0].m_hitCount == 1);
    CHECK(hits[0].m_latestMsgid == 109);
    CHECK(ManagerSearch(store, "工作??报告", true, noScope).empty());
    hits = ManagerSearch(store, "a?b", true, noScope);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].m_cid == 21);

    //只输了 *：按空关键字处理，没有结果
    CHECK(ManagerSearch(store, "*", true, noScope).empty());
    //不勾通配符时 * 按普通字符
    CHECK(ManagerSearch(store, "设计*稿", false, noScope).empty());
}

TEST_CASE("WCS4 后台查询线程的请求把是否通配符传给本地库，结果原样带回", "[wildcard]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshWildcardDb(L"wcs4.db").c_str()));
    SeedManagerStore(store);

    V2SearchRequest req;
    req.m_kind = kV2SearchManager;
    req.m_keywordUtf8 = "第三季度*报告";
    req.m_limit = kWcLimit;
    req.m_wildcard = true;
    V2SearchResponse* resp = CV2MsgSearchWorker::Execute(&store, req);
    REQUIRE(resp != NULL);
    CHECK(resp->m_ok);
    CHECK(resp->m_wildcard);
    REQUIRE(resp->m_conversations.size() == 1);
    CHECK(resp->m_conversations[0].m_hitCount == 2);
    delete resp;

    req.m_wildcard = false;
    resp = CV2MsgSearchWorker::Execute(&store, req);
    REQUIRE(resp != NULL);
    CHECK(resp->m_ok);
    CHECK_FALSE(resp->m_wildcard);
    CHECK(resp->m_conversations.empty());
    delete resp;
}
