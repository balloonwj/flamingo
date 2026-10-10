/**
 *  全局搜索（cmd_global_search 1904）的单元测试（Suite GLS），test_globalsearch.cpp
 *
 *  覆盖 docs/全局搜索增强方案.md 第 3b 步：
 *    - GLS1 GlobalSearchProtocol::parseRequest：关键字去首尾空白、为空或过长判非法；两个上限的缺省值
 *           与夹取；announcementlimit 为 0 表示不搜群公告；
 *    - GLS2 buildResponse：64 位 id 编码为字符串；失败时只带 code 与 msg；
 *    - GLS3 MakeSearchExcerpt：关键字靠前时不加前省略号、靠后时截去开头并加省略号、过长时截尾并加
 *           省略号且不超过上限；ASCII 字母不区分大小写；找不到关键字时取开头；换行换成空格；
 *    - GLS4 globalSearchConversations（真的打开临时库跑 SQL）：单聊两个方向合成一个会话；只搜范围里
 *           的群；群的时间下界；已撤回与本人已删除的不算；最近一条命中的 msgid、时间与摘录；按最近
 *           命中倒序；limit；
 *    - GLS5 searchAnnouncements：标题或正文命中、只搜给定的群、按发布时间倒序、limit。
 *
 *  典型用法：Tests.exe --test-case="GLS*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GlobalSearchProtocol.h"
#include "../chatserversrc/MessageSearchText.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名
const char* const kGlsDbPath = "flamingo_sqlite_globalsearch_test.db";
const char* const kGlsMysqlDb = "flamingo_test_gls";

//本人 A，单聊对象 B、C；群 G1、G2 本人在，G3 本人不在
const int32_t kUserA = 300001;
const int32_t kUserB = 300002;
const int32_t kUserC = 300003;
const int64_t kGroup1 = 920001;
const int64_t kGroup2 = 920002;
const int64_t kGroup3 = 920003;

//消息时间轴
const int64_t kT0 = 1700000000000LL;
const int64_t kStepMs = 1000;

//摘录上限（与线上一致）
const size_t kExcerptMax = GLOBAL_SEARCH_EXCERPT_MAX_BYTES;

int g_glsSeq = 0;

std::string NextClientId()
{
    ++g_glsSeq;
    char buf[32] = { 0 };
    ::snprintf(buf, sizeof(buf), "gls-%d", g_glsSeq);
    return buf;
}

//存一条文字消息，返回 msgid
int64_t SaveText(int ctype, int32_t from, int64_t to, const std::string& text, int index)
{
    Json::Value content(Json::objectValue);
    content["text"] = text;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string stored = MessageProtocol::buildStoredContent(Json::writeString(wb, content),
                                                                   std::vector<int64_t>(), false, "");
    const int64_t msgid = Singleton<MessageManager>::Instance().saveMessage(
        NextClientId(), ctype, from, static_cast<int32_t>(to), message_type_text, stored, kT0 + index * kStepMs);
    REQUIRE(msgid > 0);
    return msgid;
}

//以本人 A 的身份全局搜索
std::vector<GlobalSearchConversationHit> SearchAsA(const std::string& keyword,
                                                   const std::vector<GroupSearchScope>& groups, int limit)
{
    std::vector<GlobalSearchConversationHit> out;
    REQUIRE(Singleton<MessageManager>::Instance().globalSearchConversations(kUserA, keyword, groups, limit, out));
    return out;
}

GroupSearchScope Scope(int64_t groupid, int64_t minCreateTime)
{
    GroupSearchScope s;
    s.groupid = groupid;
    s.minCreateTime = minCreateTime;
    return s;
}

//插一条群公告
void InsertAnnouncement(int64_t groupid, const char* title, const char* content, int64_t publishTime)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    char sql[512] = { 0 };
    ::snprintf(sql, sizeof(sql),
               "INSERT INTO t_group_announcement(f_groupid, f_title, f_content, f_severity, f_publisher, f_publish_time) "
               "VALUES(%lld, '%s', '%s', 0, %d, %lld)",
               (long long)groupid, title, content, kUserA, (long long)publishTime);
    REQUIRE(pConn->execute(sql));
}

} // namespace

TEST_CASE("GLS1 全局搜索请求的解析与夹取")
{
    GlobalSearchRequest req;
    REQUIRE(GlobalSearchProtocol::parseRequest("{\"keyword\":\"  设计稿 \"}", req));
    CHECK(req.keyword == "设计稿");
    CHECK(req.limit == GLOBAL_SEARCH_DEFAULT_LIMIT);
    CHECK(req.announcementlimit == GLOBAL_SEARCH_ANNOUNCEMENT_DEFAULT_LIMIT);

    REQUIRE(GlobalSearchProtocol::parseRequest("{\"keyword\":\"a\",\"limit\":1000,\"announcementlimit\":-5}", req));
    CHECK(req.limit == GLOBAL_SEARCH_LIMIT_MAX);
    CHECK(req.announcementlimit == 0);
    REQUIRE(GlobalSearchProtocol::parseRequest("{\"keyword\":\"a\",\"limit\":0,\"announcementlimit\":99}", req));
    CHECK(req.limit == 1);
    CHECK(req.announcementlimit == GLOBAL_SEARCH_ANNOUNCEMENT_LIMIT_MAX);

    CHECK_FALSE(GlobalSearchProtocol::parseRequest("{\"keyword\":\"   \"}", req));
    CHECK_FALSE(GlobalSearchProtocol::parseRequest("{\"limit\":5}", req));
    CHECK_FALSE(GlobalSearchProtocol::parseRequest("not json", req));
    CHECK_FALSE(GlobalSearchProtocol::parseRequest("{\"keyword\":\"" + std::string(GLOBAL_SEARCH_KEYWORD_MAX_BYTES + 1, 'x') + "\"}", req));
    CHECK(GlobalSearchProtocol::parseRequest("{\"keyword\":\"" + std::string(GLOBAL_SEARCH_KEYWORD_MAX_BYTES, 'x') + "\"}", req));
}

TEST_CASE("GLS2 全局搜索应答的构造")
{
    std::vector<GlobalSearchConversationHit> convs(1);
    convs[0].ctype = conversation_group;
    convs[0].cid = 9007199254740993LL;
    convs[0].hitcount = 3;
    convs[0].latestmsgid = 9007199254741001LL;
    convs[0].latesttime = kT0;
    convs[0].latesttext = "设计稿";
    std::vector<GlobalSearchAnnouncementHit> anns(1);
    anns[0].groupid = kGroup1;
    anns[0].id = 55;
    anns[0].title = "周会";
    anns[0].excerpt = "带上设计稿";
    anns[0].publishtime = kT0;

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    const std::string ok = GlobalSearchProtocol::buildResponse(0, "ok", "设计稿", convs, anns);
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    REQUIRE(reader->parse(ok.c_str(), ok.c_str() + ok.size(), &root, &errs));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["keyword"].asString() == "设计稿");
    REQUIRE(root["conversations"].size() == 1);
    CHECK(root["conversations"][0]["cid"].asString() == "9007199254740993");
    CHECK(root["conversations"][0]["latestmsgid"].asString() == "9007199254741001");
    CHECK(root["conversations"][0]["hitcount"].asInt() == 3);
    CHECK(root["conversations"][0]["latesttext"].asString() == "设计稿");
    REQUIRE(root["announcements"].size() == 1);
    CHECK(root["announcements"][0]["groupid"].asString() == "920001");
    CHECK(root["announcements"][0]["id"].asString() == "55");
    CHECK(root["announcements"][0]["excerpt"].asString() == "带上设计稿");

    const std::string fail = GlobalSearchProtocol::buildResponse(error_code_bad_request, "bad request", "x", convs, anns);
    Json::Value froot;
    REQUIRE(reader->parse(fail.c_str(), fail.c_str() + fail.size(), &froot, &errs));
    CHECK(froot["code"].asInt() == error_code_bad_request);
    CHECK_FALSE(froot.isMember("conversations"));
    CHECK_FALSE(froot.isMember("keyword"));
}

TEST_CASE("GLS3 关键字附近的摘录")
{
    const std::string kEllipsis = "\xE2\x80\xA6";
    //关键字靠前：不加前省略号，整段不长时原样
    CHECK(chatserver::MakeSearchExcerpt("设计稿已经放到共享盘了", "设计稿", kExcerptMax) == "设计稿已经放到共享盘了");
    //关键字很靠后：截去开头、加前省略号，关键字仍在摘录里
    std::string longHead;
    for (int i = 0; i < 100; ++i)
        longHead += "前";
    const std::string text = longHead + "设计稿" + "后面";
    const std::string ex = chatserver::MakeSearchExcerpt(text, "设计稿", kExcerptMax);
    CHECK(ex.compare(0, kEllipsis.size(), kEllipsis) == 0);
    CHECK(ex.find("设计稿后面") != std::string::npos);
    CHECK(ex.size() <= kExcerptMax);
    //过长：截尾加省略号，不超过上限
    std::string longTail = "设计稿";
    for (int i = 0; i < 200; ++i)
        longTail += "尾";
    const std::string ex2 = chatserver::MakeSearchExcerpt(longTail, "设计稿", kExcerptMax);
    CHECK(ex2.compare(0, 9, "设计稿") == 0);
    CHECK(ex2.size() <= kExcerptMax);
    CHECK(ex2.compare(ex2.size() - kEllipsis.size(), kEllipsis.size(), kEllipsis) == 0);
    //ASCII 不区分大小写；找不到时取开头；换行换成空格
    CHECK(chatserver::MakeSearchExcerpt("Weekly REPORT", "report", kExcerptMax) == "Weekly REPORT");
    CHECK(chatserver::MakeSearchExcerpt("第一行\n第二行", "不存在", kExcerptMax) == "第一行 第二行");
    //上限过小时按下限处理
    CHECK(chatserver::MakeSearchExcerpt(longTail, "设计稿", 1).size() <= chatserver::kExcerptMinBytes);
}

TEST_CASE("GLS4 全局搜索按会话汇总本人能看到的命中")
{
    testdb::resetDb(kGlsDbPath, kGlsMysqlDb);
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //单聊 A↔B：两个方向各一条命中，另有一条被撤回、一条被 A 删除
    const int64_t ab1 = SaveText(conversation_single, kUserA, kUserB, "我把设计稿发你", 1);
    const int64_t ab2 = SaveText(conversation_single, kUserB, kUserA, "设计稿收到了，晚点看", 2);
    const int64_t abRecalled = SaveText(conversation_single, kUserA, kUserB, "设计稿撤回", 3);
    REQUIRE(mgr.recallMessage(abRecalled, kUserA));
    const int64_t abDeleted = SaveText(conversation_single, kUserB, kUserA, "设计稿删除", 4);
    REQUIRE(mgr.deleteMessageForUser(kUserA, abDeleted));
    //单聊 B↔C：与 A 无关
    SaveText(conversation_single, kUserB, kUserC, "设计稿别人的", 5);
    //群 G1 两条命中，G2 一条早一条晚，G3（本人不在）一条
    const int64_t g1a = SaveText(conversation_group, kUserB, kGroup1, "群里讨论设计稿", 6);
    const int64_t g1b = SaveText(conversation_group, kUserC, kGroup1, "设计稿定稿", 7);
    const int64_t g2Early = SaveText(conversation_group, kUserB, kGroup2, "入群前的设计稿", 8);
    const int64_t g2Late = SaveText(conversation_group, kUserB, kGroup2, "入群后的设计稿", 9);
    SaveText(conversation_group, kUserB, kGroup3, "别的群的设计稿", 10);
    //不命中的消息
    SaveText(conversation_single, kUserA, kUserC, "午饭吃什么", 11);

    std::vector<GroupSearchScope> groups;
    groups.push_back(Scope(kGroup1, 0));
    groups.push_back(Scope(kGroup2, kT0 + 9 * kStepMs));   //G2 只看第 9 条及之后

    const std::vector<GlobalSearchConversationHit> hits = SearchAsA("设计稿", groups, 10);
    REQUIRE(hits.size() == 3);
    //按最近一条命中倒序：G2（第 9 条）、G1（第 7 条）、单聊 B（第 2 条）
    CHECK(hits[0].ctype == conversation_group);
    CHECK(hits[0].cid == kGroup2);
    CHECK(hits[0].hitcount == 1);
    CHECK(hits[0].latestmsgid == g2Late);
    CHECK(hits[0].latesttime == kT0 + 9 * kStepMs);
    CHECK(hits[0].latesttext == "入群后的设计稿");
    CHECK(hits[1].cid == kGroup1);
    CHECK(hits[1].hitcount == 2);
    CHECK(hits[1].latestmsgid == g1b);
    CHECK(hits[2].ctype == conversation_single);
    CHECK(hits[2].cid == kUserB);
    CHECK(hits[2].hitcount == 2);
    CHECK(hits[2].latestmsgid == ab2);
    CHECK(hits[2].latesttext == "设计稿收到了，晚点看");
    (void)ab1;
    (void)g1a;
    (void)g2Early;

    //limit：只要最近的一个会话
    CHECK(SearchAsA("设计稿", groups, 1).size() == 1);
    //不在任何群里：只剩单聊
    const std::vector<GlobalSearchConversationHit> singleOnly = SearchAsA("设计稿", std::vector<GroupSearchScope>(), 10);
    REQUIRE(singleOnly.size() == 1);
    CHECK(singleOnly[0].cid == kUserB);
    //没有命中
    CHECK(SearchAsA("不存在的词", groups, 10).empty());

    testdb::cleanupDb(kGlsDbPath);
}

TEST_CASE("GLS5 在给定的群里搜群公告")
{
    testdb::resetDb(kGlsDbPath, kGlsMysqlDb);
    InsertAnnouncement(kGroup1, "周会安排", "请带上设计稿", kT0 + 1);
    InsertAnnouncement(kGroup1, "设计稿评审", "周五下午", kT0 + 3);
    InsertAnnouncement(kGroup2, "放假通知", "国庆放假", kT0 + 2);
    InsertAnnouncement(kGroup3, "设计稿", "别的群", kT0 + 4);

    std::vector<int64_t> groupids;
    groupids.push_back(kGroup1);
    groupids.push_back(kGroup2);
    std::vector<GroupAnnouncementRecord> out;
    GroupManager& gm = Singleton<GroupManager>::Instance();
    REQUIRE(gm.searchAnnouncements(groupids, "设计稿", 10, out));
    REQUIRE(out.size() == 2);
    CHECK(out[0].title == "设计稿评审");
    CHECK(out[1].title == "周会安排");
    REQUIRE(gm.searchAnnouncements(groupids, "设计稿", 1, out));
    CHECK(out.size() == 1);
    REQUIRE(gm.searchAnnouncements(groupids, "放假", 10, out));
    CHECK(out.size() == 1);
    REQUIRE(gm.searchAnnouncements(std::vector<int64_t>(), "设计稿", 10, out));
    CHECK(out.empty());

    testdb::cleanupDb(kGlsDbPath);
}
