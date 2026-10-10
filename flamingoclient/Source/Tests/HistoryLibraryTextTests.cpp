/**
 *  聊天记录管理器「链接」页与「与我有关」页的数据层单元测试（Suite HLK / HLR），HistoryLibraryTextTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryLibraryLinks.h 与 HistoryLibraryQuery.h 为这两页新增的部分
 *  （docs/聊天记录管理器方案.md §12.4、§12.5）：
 *    HLK1 识别链接、取域名（去协议、用户信息、端口、www.，转小写）、链接摘要、「复制链接」的文字；
 *    HLK2 链接页查询：只收 is_link=1、方向、会话、按会话统计、关键字按搜索语法匹配正文（含 from、排除词）；
 *    HLR1 与我有关页查询：@我（含 @所有人、布尔写法、数值 uid）、回复我、我发的，各胶囊与「全部」的并集，
 *         排除本人自己 @ 自己、已撤回、原始 JSON 为空或不合法的老数据；关系列（同时 @ 又回复时记为回复）；
 *         本人 userid 未知时没有结果；关键字；
 *    HLR2 关键字是否生效：文件页只认词与排除词，链接页与与我有关页认全部限定词，图片页不生效；
 *    HLR3 浏览模型：切到链接页 / 与我有关页的每页条数、关系胶囊、from 解析结果参与统计条件、恢复默认清空。
 *  用临时 db 文件跑真实 SQLite。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistoryLibraryLinks.h"
#include "../UI/BalloonHistory/HistoryLibraryQuery.h"
#include "../UI/BalloonHistory/HistoryLibraryModel.h"
#include "../UI/BalloonHistory/V2MsgStore.h"

using namespace balloonhistory;

namespace {

//本人 userid 与几位同事
const int64_t kSelf  = 100;
const int64_t kAlice = 301;
const int64_t kBob   = 302;
//会话类型与消息类型（与 net/Protocol.h 一致）
const int kSingle    = 1;
const int kGroup     = 2;
const int kTypeText  = 1;
const int kTypeImage = 2;
//测试用的群与单聊对端
const int64_t kGroupId = 300;
const int64_t kPeer    = 200;
//「近 N 天」换算用的当前时间；本组数据只用全部时间
const int64_t kNowMs = 100000;
//每页条数：放得下全部数据
const int kBigPage = 100;

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

//构造一条文字消息：search_text 为正文，链接标志按入库时的口径（IsLink），raw 为原始 JSON（可空）
HistoryRecord TextMsg(int64_t msgid, int ctype, int64_t cid, int64_t fromid, int64_t ts, const std::string& text,
                      const std::string& raw)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = fromid;
    rec.m_msgtype = kTypeText;
    rec.m_timestamp = ts;
    rec.m_searchText = text;
    rec.m_isLink = IsLink(text);
    rec.m_content = "{\"text\":\"x\"}";
    rec.m_raw = raw;
    return rec;
}

//拼一条群消息的原始 JSON：contentExtra 是 content 对象里 text 之后的部分（如 ,"at":{...}），可空
std::string RawJson(int64_t msgid, int64_t fromid, const std::string& contentExtra)
{
    return "{\"msgid\":\"" + std::to_string((long long)msgid) + "\",\"ctype\":2,\"cid\":\"300\",\"from\":\""
         + std::to_string((long long)fromid) + "\",\"msgtype\":1,\"content\":{\"text\":\"t\"" + contentExtra
         + "},\"timestamp\":1}";
}

//把一页结果写成 msgid 序列（时间倒序），如 "12,11"
std::string Keys(const std::vector<HistoryLibraryItem>& items)
{
    std::string s;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!s.empty())
        {
            s += ",";
        }
        s += std::to_string((long long)items[i].m_msgid);
    }
    return s;
}

//取一页的 msgid 序列；同时核对总数与本页条数一致（每页放得下全部数据）
std::string PageKeys(CV2MsgStore& store, const HistoryLibraryFilter& f)
{
    std::vector<HistoryLibraryItem> items;
    REQUIRE(store.QueryLibraryPage(f, kNowMs, items));
    int64_t total = -1;
    REQUIRE(store.CountLibrary(f, kNowMs, total));
    CHECK(total == (int64_t)items.size());
    return Keys(items);
}

//链接页的默认条件
HistoryLibraryFilter LinksFilter()
{
    HistoryLibraryFilter f;
    f.m_kind = kHistoryLibraryLinks;
    f.m_selfUserId = kSelf;
    f.m_pageSize = kBigPage;
    return f;
}

//与我有关页的默认条件
HistoryLibraryFilter MentionsFilter()
{
    HistoryLibraryFilter f;
    f.m_kind = kHistoryLibraryMentions;
    f.m_selfUserId = kSelf;
    f.m_pageSize = kBigPage;
    return f;
}

/**
 *  链接页的测试数据：
 *    11 单聊 200 对方 1000 「看 https://www.Example.com/a 和 http://b.org」（两个链接）
 *    12 群 300 本人  2000 「文档 https://docs.x.cn/p」
 *    13 群 300 Bob   3000 「季度报告 https://Report.io/q?id=1」
 *    14 群 300 Bob   4000 「www.foo.com 没写协议」（入库时不算链接）
 *    15 群 300 Alice 5000 「普通文字」
 *    16 群 300 Alice 6000 「已撤回 https://gone.com」（撤回）
 */
void SeedLinks(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(TextMsg(11, kSingle, kPeer, kPeer, 1000, "看 https://www.Example.com/a 和 http://b.org", "")));
    REQUIRE(store.Upsert(TextMsg(12, kGroup, kGroupId, kSelf, 2000, "文档 https://docs.x.cn/p", "")));
    REQUIRE(store.Upsert(TextMsg(13, kGroup, kGroupId, kBob, 3000, "季度报告 https://Report.io/q?id=1", "")));
    REQUIRE(store.Upsert(TextMsg(14, kGroup, kGroupId, kBob, 4000, "www.foo.com 没写协议", "")));
    REQUIRE(store.Upsert(TextMsg(15, kGroup, kGroupId, kAlice, 5000, "普通文字", "")));
    REQUIRE(store.Upsert(TextMsg(16, kGroup, kGroupId, kAlice, 6000, "已撤回 https://gone.com", "")));
    REQUIRE(store.MarkRecalled(kGroup, kGroupId, 16));
}

/**
 *  与我有关页的测试数据（群 300 除非另注）：
 *    21 Alice 1000 @我                         22 Bob   2000 @所有人（all 写成 true）
 *    23 Alice 3000 @Bob（不是我）               24 Bob   4000 回复我（quote.from = 100）
 *    25 Alice 5000 回复 Alice 自己              26 本人  6000 我发的（顺带 @ 了自己，仍记为我发的）
 *    27 Bob   7000 同时 @我又回复我（记为回复）   28 Alice 8000 原始 JSON 不合法（含 "at" 字样）
 *    29 Alice 9000 原始 JSON 为空（老数据）       30 Bob   9500 @我、uid 写成数值
 *    31 Alice 9600 @我但已撤回                   32 单聊 200 对方 9700 普通消息
 *    33 单聊 200 本人 9800 我发的
 */
void SeedMentions(CV2MsgStore& store)
{
    const std::string atMe = ",\"at\":{\"all\":0,\"users\":[{\"uid\":\"100\",\"off\":0,\"len\":2}]}";
    REQUIRE(store.Upsert(TextMsg(21, kGroup, kGroupId, kAlice, 1000, "@我 看一下", RawJson(21, kAlice, atMe))));
    REQUIRE(store.Upsert(TextMsg(22, kGroup, kGroupId, kBob, 2000, "@所有人 开会",
                                 RawJson(22, kBob, ",\"at\":{\"all\":true,\"users\":[]}"))));
    REQUIRE(store.Upsert(TextMsg(23, kGroup, kGroupId, kAlice, 3000, "@Bob 你好",
                                 RawJson(23, kAlice, ",\"at\":{\"all\":0,\"users\":[{\"uid\":\"302\",\"off\":0,\"len\":4}]}"))));
    REQUIRE(store.Upsert(TextMsg(24, kGroup, kGroupId, kBob, 4000, "好的收到",
                                 RawJson(24, kBob, ",\"quote\":{\"msgid\":\"9\",\"from\":\"100\",\"name\":\"me\"}"))));
    REQUIRE(store.Upsert(TextMsg(25, kGroup, kGroupId, kAlice, 5000, "补充一下",
                                 RawJson(25, kAlice, ",\"quote\":{\"msgid\":\"8\",\"from\":\"301\",\"name\":\"a\"}"))));
    REQUIRE(store.Upsert(TextMsg(26, kGroup, kGroupId, kSelf, 6000, "@我 自己提醒自己", RawJson(26, kSelf, atMe))));
    REQUIRE(store.Upsert(TextMsg(27, kGroup, kGroupId, kBob, 7000, "@我 回复你",
                                 RawJson(27, kBob, atMe + ",\"quote\":{\"msgid\":\"7\",\"from\":\"100\",\"name\":\"me\"}"))));
    REQUIRE(store.Upsert(TextMsg(28, kGroup, kGroupId, kAlice, 8000, "坏数据", "{\"at\": broken")));
    REQUIRE(store.Upsert(TextMsg(29, kGroup, kGroupId, kAlice, 9000, "老数据", "")));
    REQUIRE(store.Upsert(TextMsg(30, kGroup, kGroupId, kBob, 9500, "@我 数值",
                                 RawJson(30, kBob, ",\"at\":{\"all\":0,\"users\":[{\"uid\":100,\"off\":0,\"len\":2}]}"))));
    REQUIRE(store.Upsert(TextMsg(31, kGroup, kGroupId, kAlice, 9600, "@我 撤回了", RawJson(31, kAlice, atMe))));
    REQUIRE(store.MarkRecalled(kGroup, kGroupId, 31));
    REQUIRE(store.Upsert(TextMsg(32, kSingle, kPeer, kPeer, 9700, "单聊普通", "")));
    REQUIRE(store.Upsert(TextMsg(33, kSingle, kPeer, kSelf, 9800, "单聊我发的", "")));
}

} // namespace

TEST_CASE("HLK1 识别链接、取域名、链接摘要与「复制链接」的文字", "[histlinks]")
{
    //域名：去协议、用户信息、端口、路径，转小写，去掉开头的 www.
    CHECK(LinkDomainOf(L"https://User@WWW.Example.com:8080/a?b") == L"example.com");
    CHECK(LinkDomainOf(L"http://b.org") == L"b.org");
    CHECK(LinkDomainOf(L"https://docs.x.cn/p#top") == L"docs.x.cn");
    CHECK(LinkDomainOf(L"https://[::1]:8080/x") == L"[::1]");
    CHECK(LinkDomainOf(L"www.") == L"www.");
    CHECK(LinkDomainOf(L"").empty());

    //识别：与聊天气泡同一口径，www. 开头的补 https://，中文终止链接
    std::vector<std::wstring> links = LibraryLinksOfText("看 https://www.Example.com/a 和 www.b.org/x看看");
    REQUIRE(links.size() == 2);
    CHECK(links[0] == L"https://www.Example.com/a");
    CHECK(links[1] == L"https://www.b.org/x");
    CHECK(LibraryLinksOfText("没有链接").empty());
    CHECK(LibraryLinksOfText("").empty());

    //摘要：第一个链接、域名与总数
    HistoryLinkSummary s;
    REQUIRE(SummarizeLibraryLinks("看 https://www.Example.com/a 和 http://b.org", s));
    CHECK(s.m_url == L"https://www.Example.com/a");
    CHECK(s.m_domain == L"example.com");
    CHECK(s.m_linkCount == 2);
    CHECK_FALSE(SummarizeLibraryLinks("只写了 http://", s));

    //复制链接：一个就是它本身，多个每行一个
    CHECK(JoinLinksForClipboard(std::vector<std::wstring>()).empty());
    CHECK(JoinLinksForClipboard(std::vector<std::wstring>(1, L"https://a.com")) == L"https://a.com");
    CHECK(JoinLinksForClipboard(links) == L"https://www.Example.com/a\r\nhttps://www.b.org/x");
}

TEST_CASE("HLK2 链接页查询：只收含链接的消息，方向、会话、按会话统计、搜索语法", "[histlinks]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshDb(L"hlk2.db").c_str()));
    SeedLinks(store);

    //全部：14 入库时不算链接、15 是普通文字、16 已撤回
    HistoryLibraryFilter f = LinksFilter();
    CHECK(PageKeys(store, f) == "13,12,11");
    //链接页不查群文件；来源筛选不影响链接页
    CHECK(LibraryIncludesChat(f));
    CHECK_FALSE(LibraryIncludesGroupFiles(f));
    f.m_source = kHistoryLibrarySourceGroup;
    CHECK(PageKeys(store, f) == "13,12,11");

    //方向
    f = LinksFilter();
    f.m_direction = kHistoryLibraryDirSent;
    CHECK(PageKeys(store, f) == "12");
    f.m_direction = kHistoryLibraryDirReceived;
    CHECK(PageKeys(store, f) == "13,11");

    //会话与按会话统计
    f = LinksFilter();
    f.m_ctype = kGroup;
    f.m_cid = kGroupId;
    CHECK(PageKeys(store, f) == "13,12");
    std::vector<HistoryLibraryConvCount> counts;
    REQUIRE(store.CountLibraryByConversation(f, kNowMs, counts));
    REQUIRE(counts.size() == 2);
    CHECK(counts[0].m_ctype == kSingle);
    CHECK(counts[0].m_count == 1);
    CHECK(counts[1].m_ctype == kGroup);
    CHECK(counts[1].m_count == 2);

    //关键字：匹配正文，多个词、排除词、from、type 都生效
    f = LinksFilter();
    f.m_keyword = "report";
    CHECK(PageKeys(store, f) == "13");
    f.m_keyword = "https -文档";
    CHECK(PageKeys(store, f) == "13,11");
    f.m_keyword = "from:bob";
    f.m_syntaxFromIds = std::vector<int64_t>(1, kBob);
    CHECK(PageKeys(store, f) == "13");
    f.m_syntaxFromIds.clear();
    CHECK(PageKeys(store, f).empty());
    f.m_keyword = "type:image";
    CHECK(PageKeys(store, f).empty());
    //关键字条件在聊天消息这一部分里只有一份
    int binds = -1;
    f.m_keyword = "a b -c";
    BuildLibraryPageSql(f, kNowMs, binds);
    CHECK(binds == 1);

    //一行的内容：search_text 原样带回，供识别链接
    f = LinksFilter();
    std::vector<HistoryLibraryItem> items;
    REQUIRE(store.QueryLibraryPage(f, kNowMs, items));
    REQUIRE(items.size() == 3);
    CHECK(items[2].m_name == "看 https://www.Example.com/a 和 http://b.org");
    CHECK(items[2].m_relation == kHistoryLibraryRelAll);
}

TEST_CASE("HLR1 与我有关页查询：@我、回复我、我发的，排除老数据与已撤回，关系列", "[histmentions]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshDb(L"hlr1.db").c_str()));
    SeedMentions(store);

    HistoryLibraryFilter f = MentionsFilter();
    f.m_relation = kHistoryLibraryRelMention;
    CHECK(PageKeys(store, f) == "30,27,22,21");
    f.m_relation = kHistoryLibraryRelReply;
    CHECK(PageKeys(store, f) == "27,24");
    f.m_relation = kHistoryLibraryRelMine;
    CHECK(PageKeys(store, f) == "33,26");
    f.m_relation = kHistoryLibraryRelAll;
    CHECK(PageKeys(store, f) == "33,30,27,26,24,22,21");

    //关系列：同时 @ 又回复记为回复，本人发的记为我发的
    std::vector<HistoryLibraryItem> items;
    REQUIRE(store.QueryLibraryPage(f, kNowMs, items));
    REQUIRE(items.size() == 7);
    CHECK(items[0].m_relation == kHistoryLibraryRelMine);      //33
    CHECK(items[1].m_relation == kHistoryLibraryRelMention);   //30
    CHECK(items[2].m_relation == kHistoryLibraryRelReply);     //27
    CHECK(items[3].m_relation == kHistoryLibraryRelMine);      //26
    CHECK(items[4].m_relation == kHistoryLibraryRelReply);     //24
    CHECK(items[5].m_relation == kHistoryLibraryRelMention);   //22
    CHECK(items[6].m_relation == kHistoryLibraryRelMention);   //21

    //方向筛选对与我有关页不生效（由胶囊决定）
    f.m_direction = kHistoryLibraryDirSent;
    CHECK(PageKeys(store, f) == "33,30,27,26,24,22,21");
    f.m_direction = kHistoryLibraryDirAll;

    //会话限定与只看单聊
    f.m_ctypeOnly = kSingle;
    CHECK(PageKeys(store, f) == "33");
    f.m_ctypeOnly = 0;
    f.m_ctype = kGroup;
    f.m_cid = kGroupId;
    CHECK(PageKeys(store, f) == "30,27,26,24,22,21");

    //关键字：匹配正文
    f = MentionsFilter();
    f.m_keyword = "@我 -回复";
    CHECK(PageKeys(store, f) == "30,26,21");

    //本人 userid 未知时没有结果
    f = MentionsFilter();
    f.m_selfUserId = 0;
    CHECK(PageKeys(store, f).empty());
}

TEST_CASE("HLR2 关键字是否生效：各页认的条件不同", "[histmentions],[histlinks]")
{
    HistoryLibraryFilter f;
    f.m_keyword = "from:x";
    f.m_kind = kHistoryLibraryFiles;
    CHECK_FALSE(LibraryHasKeyword(f));
    f.m_kind = kHistoryLibraryLinks;
    CHECK(LibraryHasKeyword(f));
    f.m_kind = kHistoryLibraryMentions;
    CHECK(LibraryHasKeyword(f));
    f.m_kind = kHistoryLibraryImages;
    CHECK_FALSE(LibraryHasKeyword(f));

    f.m_keyword = "季度 -草稿";
    f.m_kind = kHistoryLibraryFiles;
    CHECK(LibraryHasKeyword(f));
    f.m_keyword = "**";
    f.m_wildcard = true;
    CHECK_FALSE(LibraryHasKeyword(f));
    f.m_kind = kHistoryLibraryLinks;
    CHECK_FALSE(LibraryHasKeyword(f));

    //文件页的文件名搜索：两个词各一个 LIKE，群文件部分同样两个
    f = HistoryLibraryFilter();
    f.m_kind = kHistoryLibraryFiles;
    f.m_keyword = "季度 报告";
    int binds = -1;
    const std::string sql = BuildLibraryPageSql(f, kNowMs, binds);
    CHECK(binds == 2);
    size_t likes = 0;
    size_t pos = sql.find("LIKE ?");
    while (pos != std::string::npos)
    {
        ++likes;
        pos = sql.find("LIKE ?", pos + 1);
    }
    CHECK(likes == 4);
}

TEST_CASE("HLR3 浏览模型：链接页与与我有关页的每页条数、关系胶囊、from 解析结果、恢复默认", "[histmentions],[histlinks]")
{
    HistoryLibraryModel model;
    model.SetKind(kHistoryLibraryLinks);
    CHECK(model.Filter().m_kind == kHistoryLibraryLinks);
    CHECK(model.Filter().m_pageSize == kHistoryLibraryModelFilePageSize);
    model.SetKind(kHistoryLibraryMentions);
    CHECK(model.Filter().m_kind == kHistoryLibraryMentions);
    CHECK(model.Filter().m_pageSize == kHistoryLibraryModelFilePageSize);

    //关系胶囊：越界按「全部」
    model.SetRelation(kHistoryLibraryRelReply);
    CHECK(model.Filter().m_relation == kHistoryLibraryRelReply);
    const int kBadRelation = 7;
    model.SetRelation(kBadRelation);
    CHECK(model.Filter().m_relation == kHistoryLibraryRelAll);

    //有数据时：改关系胶囊或 from 解析结果都要重新统计
    CV2MsgStore store;
    REQUIRE(store.Open(FreshDb(L"hlr3.db").c_str()));
    SeedMentions(store);
    model.SetStore(&store);
    model.SetSelfUserId(kSelf);
    REQUIRE(model.Refresh(kNowMs));
    CHECK(model.Total() == 7);
    const int64_t queries = model.CountQueryTimes();
    model.SetRelation(kHistoryLibraryRelMention);
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.CountQueryTimes() == queries + 1);
    CHECK(model.Total() == 4);

    model.SetKeyword("from:bob");
    model.SetSyntaxFromIds(std::vector<int64_t>(1, kBob));
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.Total() == 3);   //22、27、30（27 同时 @ 又回复，「@我的」里也有它）
    model.SetSyntaxFromIds(std::vector<int64_t>(1, kAlice));
    REQUIRE(model.RefreshPage(kNowMs));
    CHECK(model.Total() == 1);   //21

    //恢复默认：关系、关键字与 from 解析结果都清空，汇总哪一类不变
    model.ResetFilters();
    CHECK(model.Filter().m_relation == kHistoryLibraryRelAll);
    CHECK(model.Filter().m_keyword.empty());
    CHECK(model.Filter().m_syntaxFromIds.empty());
    CHECK(model.Filter().m_kind == kHistoryLibraryMentions);
}
