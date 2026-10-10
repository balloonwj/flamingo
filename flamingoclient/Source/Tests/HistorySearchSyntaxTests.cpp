/**
 *  聊天记录管理器搜索语法的单元测试（Suite SS），HistorySearchSyntaxTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistorySearchSyntax.h 与各处接入（docs/聊天记录管理器方案.md §12.3）：
 *    SS1 空白切分：半角空格、制表符、全角空格，连续空白不产生空词；
 *    SS2 引号：半角与中文引号括起的部分是一个词，引号本身去掉；
 *    SS3 排除词：-词、-"带空格的词"，单独一个 - 按普通词；
 *    SS4 限定词：from / 发送人、type / 类型、after、before，名不区分大小写，半角与全角冒号；
 *        认不出的 xxx: 与 http://… 按普通词；
 *    SS5 写错的限定值：type 认不出、日期不合法或不存在、from 后面是空的；
 *    SS6 通配符：勾选时只有 '*' 的词丢弃；
 *    SS7 日期文字与类型文字的换算、显示名匹配；
 *    SS8 拼 SQL：每个正向词一个 LIKE、每个排除词一个 NOT LIKE，绑定顺序，termsOnly，from 的 IN 与 AND 0，
 *        全文索引查询只取不少于 3 个字的词；
 *    SS9 高亮：多个词的命中区间合并，排除词与限定词不高亮；
 *    SS10 会话内搜索（真实 SQLite）：多个词、排除词、引号、from、type、after / before、写错的限定值；
 *    SS11 跨会话搜索（真实 SQLite，含全文索引路径）：排除词、from；
 *    SS12 HistoryModel：from 解析结果参与「筛选是否变化」的判断，恢复默认筛选时清空。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistorySearchSyntax.h"
#include "../UI/BalloonHistory/HistoryWildcard.h"
#include "../UI/BalloonHistory/HistoryModel.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/V2MsgStore.h"

using namespace balloonhistory;

namespace {

//会话类型与消息类型（与 net/Protocol.h 一致）
const int kGroup     = 2;
const int kTypeText  = 1;
const int kTypeImage = 2;
//测试群与群里几个人的 userid
const int64_t kGroupId = 300;
const int64_t kSelf    = 100;
const int64_t kAlice   = 301;
const int64_t kBob     = 302;
//一小时的毫秒数：同一天里错开几条消息
const int64_t kHourMs = 3600LL * 1000LL;
//每页条数：放得下全部数据
const int kBigPage = 100;
//跨会话搜索最多返回的会话数
const int kManagerLimit = 50;

//取一个临时目录下的 db 全路径，并先删掉同名旧文件与 WAL 残留（文件名带进程号）
std::wstring FreshDb(const wchar_t* name)
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

//某天本地 0 点（Unix 毫秒）
int64_t DayStart(int year, int month, int day)
{
    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(LocalDayBoundsMs(year, month, day, start, end));
    return start;
}

//某天本地最后一毫秒
int64_t DayEnd(int year, int month, int day)
{
    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(LocalDayBoundsMs(year, month, day, start, end));
    return end;
}

//构造一条群里的文字消息；正文含 http 时置链接标志（与入库时 IsLink 的口径一致）
HistoryRecord GroupText(int64_t msgid, int64_t fromid, int64_t ts, const std::string& text)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = kGroup;
    rec.m_cid = kGroupId;
    rec.m_fromid = fromid;
    rec.m_msgtype = kTypeText;
    rec.m_timestamp = ts;
    rec.m_searchText = text;
    rec.m_isLink = IsLink(text);
    rec.m_content = "{\"text\":\"x\"}";
    return rec;
}

/**
 *  测试数据（群 300）：
 *    1 Alice 10-01 09:00 季度报告初稿          2 本人  10-02 09:00 季度报告 草稿 第二版
 *    3 Bob   10-03 09:00 年度总结              4 Alice 10-03 10:00 参考 https://example.com/a（链接）
 *    5 Bob   10-04 09:00 图片                  6 Alice 10-04 10:00 季度 计划
 */
void SeedGroup(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(GroupText(1, kAlice, DayStart(2026, 10, 1) + 9 * kHourMs, "季度报告初稿")));
    REQUIRE(store.Upsert(GroupText(2, kSelf, DayStart(2026, 10, 2) + 9 * kHourMs, "季度报告 草稿 第二版")));
    REQUIRE(store.Upsert(GroupText(3, kBob, DayStart(2026, 10, 3) + 9 * kHourMs, "年度总结")));
    REQUIRE(store.Upsert(GroupText(4, kAlice, DayStart(2026, 10, 3) + 10 * kHourMs,
                                   "参考 https://example.com/a")));
    HistoryRecord image;
    image.m_msgid = 5;
    image.m_ctype = kGroup;
    image.m_cid = kGroupId;
    image.m_fromid = kBob;
    image.m_msgtype = kTypeImage;
    image.m_timestamp = DayStart(2026, 10, 4) + 9 * kHourMs;
    image.m_content = "{\"image\":{\"fileid\":\"I5\",\"md5\":\"m5\",\"w\":1,\"h\":1}}";
    REQUIRE(store.Upsert(image));
    REQUIRE(store.Upsert(GroupText(6, kAlice, DayStart(2026, 10, 4) + 10 * kHourMs, "季度 计划")));
}

//按关键字在群 300 里查一页，返回命中的 msgid（升序，逗号分隔）；同时核对总数与本页条数一致
std::string SessionHits(CV2MsgStore& store, const std::string& keyword,
                        const std::vector<int64_t>& fromIds = std::vector<int64_t>())
{
    HistoryFilter f;
    f.m_ctype = kGroup;
    f.m_cid = kGroupId;
    f.m_pageSize = kBigPage;
    f.m_keyword = keyword;
    f.m_syntaxFromIds = fromIds;
    std::vector<HistoryRecord> page;
    REQUIRE(store.QueryPage(f, 0, page));
    CHECK(store.Count(f, 0) == (int64_t)page.size());
    std::vector<int64_t> ids;
    for (size_t i = 0; i < page.size(); ++i)
    {
        ids.push_back(page[i].m_msgid);
    }
    std::sort(ids.begin(), ids.end());
    std::string s;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (!s.empty())
        {
            s += ",";
        }
        s += std::to_string((long long)ids[i]);
    }
    return s;
}

//跨会话搜索群 300 的命中条数；没有命中这个群时返回 0
int ManagerHits(CV2MsgStore& store, const std::string& keyword,
                const std::vector<int64_t>& fromIds = std::vector<int64_t>())
{
    std::vector<ConversationHit> hits;
    REQUIRE(store.SearchConversationsForManager(keyword, kManagerLimit, std::vector<HistoryConversationKey>(),
                                                hits, false, fromIds));
    for (size_t i = 0; i < hits.size(); ++i)
    {
        if (hits[i].m_ctype == kGroup && hits[i].m_cid == kGroupId)
        {
            return hits[i].m_hitCount;
        }
    }
    return 0;
}

//数一个子串在字符串里出现了几次
size_t CountOf(const std::string& s, const std::string& sub)
{
    size_t n = 0;
    size_t pos = s.find(sub);
    while (pos != std::string::npos)
    {
        ++n;
        pos = s.find(sub, pos + sub.size());
    }
    return n;
}

} // namespace

TEST_CASE("SS1 空白切分：半角空格、制表符、全角空格", "[searchsyntax]")
{
    HistorySearchQuery q = ParseHistorySearch("季度 报告", false);
    REQUIRE(q.m_terms.size() == 2);
    CHECK(q.m_terms[0] == "季度");
    CHECK(q.m_terms[1] == "报告");
    CHECK(q.m_excludes.empty());
    CHECK(q.m_type == kSearchTypeNone);
    CHECK_FALSE(q.m_badModifier);

    q = ParseHistorySearch("  季度\t报告\xE3\x80\x80总结   ", false);
    REQUIRE(q.m_terms.size() == 3);
    CHECK(q.m_terms[2] == "总结");

    //只有空白：没有任何条件
    q = ParseHistorySearch(" \t\xE3\x80\x80 ", false);
    CHECK(q.m_terms.empty());
    CHECK_FALSE(SearchQueryHasCondition(q));
    CHECK_FALSE(SearchQueryHasCondition(ParseHistorySearch("", false)));
}

TEST_CASE("SS2 引号：括起的部分是一个词，引号本身去掉", "[searchsyntax]")
{
    HistorySearchQuery q = ParseHistorySearch("\"季度 报告\" 草稿", false);
    REQUIRE(q.m_terms.size() == 2);
    CHECK(q.m_terms[0] == "季度 报告");
    CHECK(q.m_terms[1] == "草稿");

    q = ParseHistorySearch("“季度 报告”", false);
    REQUIRE(q.m_terms.size() == 1);
    CHECK(q.m_terms[0] == "季度 报告");

    //只有一对空引号：没有词
    q = ParseHistorySearch("\"\"", false);
    CHECK(q.m_terms.empty());
}

TEST_CASE("SS3 排除词：-词、-\"带空格的词\"，单独一个 - 按普通词", "[searchsyntax]")
{
    HistorySearchQuery q = ParseHistorySearch("报告 -草稿 -\"第二 版\"", false);
    REQUIRE(q.m_terms.size() == 1);
    CHECK(q.m_terms[0] == "报告");
    REQUIRE(q.m_excludes.size() == 2);
    CHECK(q.m_excludes[0] == "草稿");
    CHECK(q.m_excludes[1] == "第二 版");

    q = ParseHistorySearch("a - b", false);
    REQUIRE(q.m_terms.size() == 3);
    CHECK(q.m_terms[1] == "-");
    CHECK(q.m_excludes.empty());

    //只有排除词也算有条件
    CHECK(SearchQueryHasCondition(ParseHistorySearch("-草稿", false)));
}

TEST_CASE("SS4 限定词：from、type、after、before，大小写与全角冒号", "[searchsyntax]")
{
    HistorySearchQuery q = ParseHistorySearch("from:张三 发送人：李四 TYPE:Image 2026", false);
    REQUIRE(q.m_fromNames.size() == 2);
    CHECK(q.m_fromNames[0] == "张三");
    CHECK(q.m_fromNames[1] == "李四");
    CHECK(q.m_type == kHistoryTypeImage);
    REQUIRE(q.m_terms.size() == 1);
    CHECK(q.m_terms[0] == "2026");
    CHECK_FALSE(q.m_badModifier);

    q = ParseHistorySearch("类型:文件 after:2026-10-01 before:2026/10/05", false);
    CHECK(q.m_type == kHistoryTypeFile);
    CHECK(q.m_afterMs == DayStart(2026, 10, 1));
    CHECK(q.m_beforeMs == DayEnd(2026, 10, 5));
    CHECK(q.m_terms.empty());
    CHECK(SearchQueryHasCondition(q));

    //紧凑日期；type 写多个以最后一个为准
    q = ParseHistorySearch("after:20261001 type:text type:pin", false);
    CHECK(q.m_afterMs == DayStart(2026, 10, 1));
    CHECK(q.m_type == kHistoryTypePinned);

    //from 的值可以加引号，里面可以有空格
    q = ParseHistorySearch("from:\"Zhang San\"", false);
    REQUIRE(q.m_fromNames.size() == 1);
    CHECK(q.m_fromNames[0] == "Zhang San");

    //认不出的 xxx: 与链接按普通词；冒号在引号之后时也按普通词
    q = ParseHistorySearch("http://a.com/x abc:def \"from:x\"", false);
    REQUIRE(q.m_terms.size() == 3);
    CHECK(q.m_terms[0] == "http://a.com/x");
    CHECK(q.m_terms[1] == "abc:def");
    CHECK(q.m_terms[2] == "from:x");
    CHECK(q.m_fromNames.empty());

    //排除词不按限定词解释
    q = ParseHistorySearch("-from:x", false);
    REQUIRE(q.m_excludes.size() == 1);
    CHECK(q.m_excludes[0] == "from:x");
}

TEST_CASE("SS5 写错的限定值：这次搜索不应有结果", "[searchsyntax]")
{
    CHECK(ParseHistorySearch("type:xyz", false).m_badModifier);
    CHECK(ParseHistorySearch("after:2026-13-01", false).m_badModifier);
    CHECK(ParseHistorySearch("after:2026-02-30", false).m_badModifier);
    CHECK(ParseHistorySearch("before:yesterday", false).m_badModifier);
    CHECK(ParseHistorySearch("from:", false).m_badModifier);
    CHECK(ParseHistorySearch("from:\"\"", false).m_badModifier);
    //写错的限定值也算有条件（交给查询得到空结果）
    CHECK(SearchQueryHasCondition(ParseHistorySearch("type:xyz", false)));
}

TEST_CASE("SS6 通配符：只有 '*' 的词丢弃，其余原样保留", "[searchsyntax]")
{
    HistorySearchQuery q = ParseHistorySearch("* 报告 -** 季?", true);
    REQUIRE(q.m_terms.size() == 2);
    CHECK(q.m_terms[0] == "报告");
    CHECK(q.m_terms[1] == "季?");
    CHECK(q.m_excludes.empty());

    //不勾选时 '*' 是普通字符
    q = ParseHistorySearch("* 报告", false);
    REQUIRE(q.m_terms.size() == 2);
    CHECK(q.m_terms[0] == "*");

    CHECK_FALSE(SearchQueryHasCondition(ParseHistorySearch("** *", true)));
}

TEST_CASE("SS7 日期与类型文字、显示名匹配", "[searchsyntax]")
{
    int y = 0;
    int m = 0;
    int d = 0;
    CHECK(ParseSearchDate("2026-1-5", y, m, d));
    CHECK(y == 2026);
    CHECK(m == 1);
    CHECK(d == 5);
    CHECK(ParseSearchDate("2026/10/31", y, m, d));
    CHECK(d == 31);
    CHECK(ParseSearchDate("20261231", y, m, d));
    CHECK(m == 12);
    CHECK_FALSE(ParseSearchDate("26-01-05", y, m, d));
    CHECK_FALSE(ParseSearchDate("2026-01/05", y, m, d));
    CHECK_FALSE(ParseSearchDate("202601050", y, m, d));
    CHECK_FALSE(ParseSearchDate("2026-00-05", y, m, d));
    CHECK_FALSE(ParseSearchDate("2026-01-32", y, m, d));
    CHECK_FALSE(ParseSearchDate("", y, m, d));

    CHECK(SearchTypeFromText("文字") == kHistoryTypeText);
    CHECK(SearchTypeFromText("图片") == kHistoryTypeImage);
    CHECK(SearchTypeFromText("IMG") == kHistoryTypeImage);
    CHECK(SearchTypeFromText("文件") == kHistoryTypeFile);
    CHECK(SearchTypeFromText("Voice") == kHistoryTypeVoice);
    CHECK(SearchTypeFromText("链接") == kHistoryTypeLink);
    CHECK(SearchTypeFromText("PIN") == kHistoryTypePinned);
    CHECK(SearchTypeFromText("视频") == kSearchTypeNone);

    CHECK(SearchNameMatches("张三丰", "张三"));
    CHECK(SearchNameMatches("Alice Wang", "alice"));
    CHECK_FALSE(SearchNameMatches("李四", "张三"));
    CHECK_FALSE(SearchNameMatches("张三", ""));

    //from: 名字换成 userid：任一名字匹配即可，顺序同候选表、不重复；没有 from: 时为空表
    std::vector<int64_t> cands;
    cands.push_back(kAlice);
    cands.push_back(kBob);
    cands.push_back(kSelf);
    cands.push_back(kAlice);
    std::vector<std::string> names;
    names.push_back("Alice Wang");
    names.push_back("Bob");
    names.push_back("张三");
    names.push_back("Alice Wang");
    std::vector<int64_t> ids = MatchSearchFromIds(ParseHistorySearch("from:ALICE from:张", false), cands, names);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == kAlice);
    CHECK(ids[1] == kSelf);
    CHECK(MatchSearchFromIds(ParseHistorySearch("from:nobody", false), cands, names).empty());
    CHECK(MatchSearchFromIds(ParseHistorySearch("alice", false), cands, names).empty());
    //名字比候选人少：多出的候选人不参与
    names.resize(1);
    ids = MatchSearchFromIds(ParseHistorySearch("from:b", false), cands, names);
    CHECK(ids.empty());
}

TEST_CASE("SS8 拼 SQL：占位个数与顺序、termsOnly、from、全文索引", "[searchsyntax]")
{
    std::vector<int64_t> ids;
    ids.push_back(7);
    ids.push_back(8);
    const std::string kw = "季度 报告 -草稿 from:x type:image after:2026-10-01";
    const std::string clause = BuildSearchSyntaxClause(kw, false, ids, "search_text", false);
    CHECK(CountOf(clause, "search_text LIKE ? ESCAPE '\\'") == 2);
    CHECK(CountOf(clause, "search_text NOT LIKE ? ESCAPE '\\'") == 1);
    //type:image 的条件与消息记录窗口「图片」分类相同（含带图片的帖子卡片，docs/帖子群方案.md 14.5），外面再套一层括号
    CHECK(clause.find("AND (" + TypeFilterToSqlFragment(kHistoryTypeImage) + ")") != std::string::npos);
    CHECK(clause.find("AND fromid IN (7,8)") != std::string::npos);
    CHECK(clause.find("AND timestamp>=" + std::to_string((long long)DayStart(2026, 10, 1))) != std::string::npos);
    CHECK(clause.find("AND 0") == std::string::npos);

    //绑定顺序：先正向词、后排除词
    const std::vector<std::string> binds = SearchSyntaxLikeBinds(kw, false);
    REQUIRE(binds.size() == 3);
    CHECK(binds[0] == "%季度%");
    CHECK(binds[1] == "%报告%");
    CHECK(binds[2] == "%草稿%");

    //termsOnly：只有词与排除词
    const std::string termsOnly = BuildSearchSyntaxClause(kw, false, ids, "g.name", true);
    CHECK(termsOnly == " AND g.name LIKE ? ESCAPE '\\' AND g.name LIKE ? ESCAPE '\\'"
                       " AND g.name NOT LIKE ? ESCAPE '\\'");

    //from 一个都没匹配上、或有写错的限定值：没有结果
    CHECK(BuildSearchSyntaxClause("from:x", false, std::vector<int64_t>(), "search_text", false) == " AND 0");
    CHECK(BuildSearchSyntaxClause("type:xyz", false, ids, "search_text", false) == " AND 0");

    //全文索引：只取不少于 3 个字的正向词，用 AND 连接；一个都没有时不走全文索引
    std::string fts;
    CHECK(BuildSearchSyntaxFtsQuery("季度报告 总结一下 -草稿稿稿 ab", false, fts));
    CHECK(fts == "\"季度报告\" AND \"总结一下\"");
    CHECK_FALSE(BuildSearchSyntaxFtsQuery("季度 报告", false, fts));
}

TEST_CASE("SS9 高亮：多个词的命中区间合并，排除词与限定词不高亮", "[searchsyntax]")
{
    std::vector<HighlightRange> r = FindSearchHighlightRanges(L"季度报告季度", L"季度 报告", false);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_begin == 0);
    CHECK(r[0].m_len == 6);

    r = FindSearchHighlightRanges(L"季度工作报告", L"季度 报告 -工作 from:工作", false);
    REQUIRE(r.size() == 2);
    CHECK(r[0].m_begin == 0);
    CHECK(r[0].m_len == 2);
    CHECK(r[1].m_begin == 4);
    CHECK(r[1].m_len == 2);

    //合并：重叠、首尾相接的并成一段，长度不为正的丢弃
    std::vector<HighlightRange> raw;
    HighlightRange a;
    a.m_begin = 5;
    a.m_len = 3;
    raw.push_back(a);
    HighlightRange b;
    b.m_begin = 0;
    b.m_len = 2;
    raw.push_back(b);
    HighlightRange c;
    c.m_begin = 2;
    c.m_len = 1;
    raw.push_back(c);
    HighlightRange z;
    z.m_begin = 9;
    z.m_len = 0;
    raw.push_back(z);
    HighlightRange o;
    o.m_begin = 6;
    o.m_len = 4;
    raw.push_back(o);
    const std::vector<HighlightRange> merged = MergeHighlightRanges(raw);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0].m_begin == 0);
    CHECK(merged[0].m_len == 3);
    CHECK(merged[1].m_begin == 5);
    CHECK(merged[1].m_len == 5);
}

TEST_CASE("SS10 会话内搜索：多个词、排除词、引号、from、type、日期", "[searchsyntax]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshDb(L"ss10.db").c_str()));
    SeedGroup(store);

    CHECK(SessionHits(store, "季度 报告") == "1,2");
    CHECK(SessionHits(store, "季度 -草稿") == "1,6");
    CHECK(SessionHits(store, "\"季度 计划\"") == "6");
    //以前整串匹配的写法：两个词同时包含，结果是原来的超集
    CHECK(SessionHits(store, "季度 计划") == "6");

    //from：只看解析出的这些人；一个都没匹配上时没有结果
    std::vector<int64_t> alice;
    alice.push_back(kAlice);
    CHECK(SessionHits(store, "季度 from:alice", alice) == "1,6");
    CHECK(SessionHits(store, "from:nobody") == "");

    //type
    CHECK(SessionHits(store, "type:链接") == "4");
    CHECK(SessionHits(store, "type:image") == "5");
    CHECK(SessionHits(store, "type:文字 -季度") == "3");

    //after 含当天 0 点起，before 含当天
    CHECK(SessionHits(store, "after:2026-10-03") == "3,4,5,6");
    CHECK(SessionHits(store, "before:2026-10-02") == "1,2");
    CHECK(SessionHits(store, "after:2026-10-02 before:2026-10-03") == "2,3,4");

    //写错的限定值：没有结果
    CHECK(SessionHits(store, "季度 type:xyz") == "");
    CHECK(SessionHits(store, "after:2026-02-30") == "");
}

TEST_CASE("SS11 跨会话搜索：排除词、from，含全文索引路径", "[searchsyntax]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshDb(L"ss11.db").c_str()));
    SeedGroup(store);

    //两个字的词走 LIKE
    CHECK(ManagerHits(store, "季度 -草稿") == 2);
    //不少于 3 个字的词走全文索引，再用 LIKE 复核排除词
    CHECK(ManagerHits(store, "季度报告 -草稿") == 1);
    CHECK(ManagerHits(store, "季度报告") == 2);

    //只有 from：图片没有搜索文本也算
    std::vector<int64_t> bob;
    bob.push_back(kBob);
    CHECK(ManagerHits(store, "from:bob", bob) == 2);
    CHECK(ManagerHits(store, "from:bob") == 0);
    CHECK(ManagerHits(store, "type:link") == 1);

    //跨会话 from: 的候选人：本地库里发过言的所有人，升序、不重复
    std::vector<int64_t> senders;
    REQUIRE(store.QueryAllSenders(senders));
    REQUIRE(senders.size() == 3);
    CHECK(senders[0] == kSelf);
    CHECK(senders[1] == kAlice);
    CHECK(senders[2] == kBob);
}

TEST_CASE("SS12 HistoryModel：from 解析结果参与筛选比较，恢复默认时清空", "[searchsyntax]")
{
    HistoryFilter a;
    a.m_keyword = "from:x";
    HistoryFilter b = a;
    CHECK_FALSE(ShouldResetListScroll(a, b));
    b.m_syntaxFromIds.push_back(kAlice);
    CHECK(ShouldResetListScroll(a, b));

    HistoryModel model;
    model.SetKeyword("from:x");
    model.GoToPage(1);
    std::vector<int64_t> ids;
    ids.push_back(kAlice);
    model.SetSyntaxFromIds(ids);
    CHECK(model.Filter().m_syntaxFromIds == ids);
    model.ResetSearchFilters();
    CHECK(model.Filter().m_syntaxFromIds.empty());
    CHECK(model.Filter().m_keyword.empty());
}
