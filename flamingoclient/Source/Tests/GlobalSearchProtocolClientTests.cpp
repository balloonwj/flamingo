/**
 *  全局搜索（cmd_global_search 1904）客户端报文编解码的单元测试（Suite GSP），GlobalSearchProtocolClientTests.cpp
 *
 *  覆盖 net::GlobalSearchProtocolClient 与搜索下拉用到的提示文字：
 *    GSP1  buildRequest 带上关键字与两个上限；
 *    GSP2  parseResponse：64 位 id 以字符串解析、会话与群公告逐项读出；失败应答只取 code 与 msg；
 *          不合法的条目跳过；不是 JSON 对象或缺 code 时返回 false；
 *    GSP3  ServerSearchStatusHint：等待、没联网、失败三种状态有提示，其余为空。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../net/GlobalSearchProtocolClient.h"
#include "../UI/BalloonSearch/GroupFileSearchState.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

#include <memory>
#include <string>

TEST_CASE("GSP1 全局搜索请求带上关键字与两个上限", "[globalsearch]")
{
    const std::string body = net::GlobalSearchProtocolClient::buildRequest("设计稿", 20, 10);
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    REQUIRE(reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs));
    CHECK(root["keyword"].asString() == "设计稿");
    CHECK(root["limit"].asInt() == 20);
    CHECK(root["announcementlimit"].asInt() == 10);
}

TEST_CASE("GSP2 全局搜索应答的解析", "[globalsearch]")
{
    const std::string ok =
        "{\"code\":0,\"msg\":\"ok\",\"keyword\":\"设计稿\","
        "\"conversations\":["
        "{\"ctype\":2,\"cid\":\"9007199254740993\",\"hitcount\":3,\"latestmsgid\":\"9007199254741001\","
        "\"latesttime\":1759500000000,\"latesttext\":\"…设计稿发群里\"},"
        "{\"ctype\":1,\"cid\":\"0\",\"hitcount\":1},"
        "\"not an object\"],"
        "\"announcements\":["
        "{\"groupid\":\"268435457\",\"id\":\"55\",\"title\":\"周会\",\"excerpt\":\"带上设计稿\",\"publishtime\":1759400000000},"
        "{\"groupid\":\"0\",\"id\":\"56\"}]}";
    net::GlobalSearchResult r;
    REQUIRE(net::GlobalSearchProtocolClient::parseResponse(ok, r));
    CHECK(r.m_code == 0);
    CHECK(r.m_keyword == "设计稿");
    //会话 id 为 0 的与不是对象的条目跳过
    REQUIRE(r.m_conversations.size() == 1);
    CHECK(r.m_conversations[0].m_ctype == 2);
    CHECK(r.m_conversations[0].m_cid == 9007199254740993LL);
    CHECK(r.m_conversations[0].m_hitCount == 3);
    CHECK(r.m_conversations[0].m_latestMsgid == 9007199254741001LL);
    CHECK(r.m_conversations[0].m_latestTime == 1759500000000LL);
    CHECK(r.m_conversations[0].m_latestText == "…设计稿发群里");
    REQUIRE(r.m_announcements.size() == 1);
    CHECK(r.m_announcements[0].m_groupid == 268435457LL);
    CHECK(r.m_announcements[0].m_id == 55);
    CHECK(r.m_announcements[0].m_title == "周会");
    CHECK(r.m_announcements[0].m_excerpt == "带上设计稿");
    CHECK(r.m_announcements[0].m_publishTime == 1759400000000LL);

    //失败应答：只有 code 与 msg
    net::GlobalSearchResult fail;
    REQUIRE(net::GlobalSearchProtocolClient::parseResponse("{\"code\":3,\"msg\":\"bad request\"}", fail));
    CHECK(fail.m_code == 3);
    CHECK(fail.m_msg == "bad request");
    CHECK(fail.m_conversations.empty());
    CHECK(fail.m_announcements.empty());

    //不是 JSON 对象、缺 code
    net::GlobalSearchResult bad;
    CHECK_FALSE(net::GlobalSearchProtocolClient::parseResponse("not json", bad));
    CHECK_FALSE(net::GlobalSearchProtocolClient::parseResponse("{\"msg\":\"ok\"}", bad));
}

TEST_CASE("GSP3 服务器搜索各状态的提示文字", "[globalsearch]")
{
    CHECK_FALSE(balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFilePending).empty());
    CHECK_FALSE(balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFileOffline).empty());
    CHECK_FALSE(balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFileFailed).empty());
    CHECK(balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFileIdle).empty());
    CHECK(balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFileDone).empty());
    //三种提示互不相同，也不与群文件那一段的提示混用
    CHECK(balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFilePending)
          != balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFileOffline));
    CHECK(balloonsearch::ServerSearchStatusHint(balloonsearch::kGroupFileOffline)
          != balloonsearch::GroupFileStatusHint(balloonsearch::kGroupFileOffline));
}
