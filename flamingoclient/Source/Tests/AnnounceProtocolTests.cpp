/**
 *  v2 平台公告报文编解码（客户端侧）单元测试（Suite PAC），AnnounceProtocolTests.cpp
 *  覆盖：两条请求的构造，cmd_get_announcements 应答解析（正常/失败码/字段缺失/
 *       非法条目跳过），cmd_announcement_notify 推送解析（新发布/撤回），
 *       以及通用 {code,msg} 应答解析。
 *  框架 Catch2 v3。
 *
 *  「PAC」= Platform Announcement Client，与服务端 doctest 的 Suite PA 呼应。
 *  注意本文件测的是<u>平台公告</u>（公告中心），与群公告的测试无关。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "catch_amalgamated.hpp"
#include "../net/AnnounceProtocolClient.h"

using namespace net;

TEST_CASE("PAC1 buildGetAnnouncementsRequest 把 sinceid 编成字符串")
{
    //64 位 ID 必须以字符串编码，否则大整数在 JSON 里会丢精度。
    CHECK(AnnounceProtocolClient::buildGetAnnouncementsRequest(0, 50)
          == "{\"limit\":50,\"sinceid\":\"0\"}");
    CHECK(AnnounceProtocolClient::buildGetAnnouncementsRequest(10023, 20)
          == "{\"limit\":20,\"sinceid\":\"10023\"}");
}

TEST_CASE("PAC2 buildMarkReadRequest 把公告ID编成字符串")
{
    CHECK(AnnounceProtocolClient::buildMarkReadRequest(10023)
          == "{\"announcementid\":\"10023\"}");
}

TEST_CASE("PAC3 parseGetAnnouncementsResponse 解析正常应答")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"hasmore\":true,\"announcements\":["
        "{\"announcementid\":\"10023\",\"title\":\"\xE7\xB3\xBB\xE7\xBB\x9F\xE7\xBB\xB4\xE6\x8A\xA4\","
        "\"body\":\"\xE5\x81\x9C\xE6\x9C\xBA\xE4\xB8\xA4\xE5\xB0\x8F\xE6\x97\xB6\",\"severity\":2,"
        "\"sendername\":\"\xE5\xB9\xB3\xE5\x8F\xB0\xE8\xBF\x90\xE8\x90\xA5\",\"pinned\":1,"
        "\"senttime\":1754300000000,\"read\":0},"
        "{\"announcementid\":\"10024\",\"title\":\"t2\",\"body\":\"b2\",\"severity\":0,"
        "\"sendername\":\"s2\",\"pinned\":0,\"senttime\":1754300100000,\"read\":1}"
        "]}";

    AnnouncementsResult out;
    REQUIRE(AnnounceProtocolClient::parseGetAnnouncementsResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_msg == "ok");
    CHECK(out.m_hasMore == true);
    REQUIRE(out.m_items.size() == 2);

    CHECK(out.m_items[0].m_announcementId == 10023);
    CHECK(out.m_items[0].m_severity == 2);
    CHECK(out.m_items[0].m_pinned == 1);
    CHECK(out.m_items[0].m_sentTimeMs == 1754300000000LL);
    CHECK(out.m_items[0].m_read == 0);

    CHECK(out.m_items[1].m_announcementId == 10024);
    CHECK(out.m_items[1].m_pinned == 0);
    CHECK(out.m_items[1].m_read == 1);
}

TEST_CASE("PAC4 parseGetAnnouncementsResponse 失败应答与空列表")
{
    //失败应答：解析本身成功（调用方要读 code 才知道失败原因），只是没有条目。
    AnnouncementsResult err;
    REQUIRE(AnnounceProtocolClient::parseGetAnnouncementsResponse(
        "{\"code\":1,\"msg\":\"failed\",\"hasmore\":false,\"announcements\":[]}", err));
    CHECK(err.m_code == 1);
    CHECK(err.m_msg == "failed");
    CHECK(err.m_items.empty());

    //announcements 字段整个缺失也不算解析失败，只是一条都没有。
    AnnouncementsResult none;
    REQUIRE(AnnounceProtocolClient::parseGetAnnouncementsResponse("{\"code\":0,\"msg\":\"ok\"}", none));
    CHECK(none.m_code == 0);
    CHECK(none.m_items.empty());
    //hasmore 缺失时按「没有更多」处理——宁可少拉一轮，也不要因为它恒为 true 而死循环。
    CHECK(none.m_hasMore == false);
}

TEST_CASE("PAC5 parseGetAnnouncementsResponse 畸形输入安全失败")
{
    AnnouncementsResult out;
    CHECK_FALSE(AnnounceProtocolClient::parseGetAnnouncementsResponse("", out));
    CHECK_FALSE(AnnounceProtocolClient::parseGetAnnouncementsResponse("not json", out));
    CHECK_FALSE(AnnounceProtocolClient::parseGetAnnouncementsResponse("[1,2,3]", out));
    //缺 code 就没法判定成败，整条丢弃。
    CHECK_FALSE(AnnounceProtocolClient::parseGetAnnouncementsResponse(
        "{\"msg\":\"ok\",\"announcements\":[]}", out));
}

TEST_CASE("PAC6 parseGetAnnouncementsResponse 单条缺字段不影响整批")
{
    //中间那条没有 announcementid：本端拿它没用（去重与增量拉取都靠它），跳过该条，
    //但前后两条要照常收下——不能因为一条数据不全就把整批公告都丢了。
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"hasmore\":false,\"announcements\":["
        "{\"announcementid\":\"1\",\"title\":\"a\"},"
        "{\"title\":\"no id\"},"
        "{\"announcementid\":\"3\"}"
        "]}";

    AnnouncementsResult out;
    REQUIRE(AnnounceProtocolClient::parseGetAnnouncementsResponse(body, out));
    REQUIRE(out.m_items.size() == 2);
    CHECK(out.m_items[0].m_announcementId == 1);
    CHECK(out.m_items[0].m_title == "a");
    CHECK(out.m_items[1].m_announcementId == 3);
    //缺失的字段按默认值填充，不是未初始化的垃圾值。
    CHECK(out.m_items[1].m_title.empty());
    CHECK(out.m_items[1].m_severity == 0);
    CHECK(out.m_items[1].m_sentTimeMs == 0);
}

TEST_CASE("PAC7 parseAnnouncementNotify 新发布带完整内容")
{
    std::string body =
        "{\"changetype\":1,\"announcement\":{\"announcementid\":\"10025\","
        "\"title\":\"t\",\"body\":\"b\",\"severity\":1,\"sendername\":\"s\","
        "\"pinned\":0,\"senttime\":1754300200000,\"read\":0}}";

    AnnouncementNotify out;
    REQUIRE(AnnounceProtocolClient::parseAnnouncementNotify(body, out));
    CHECK(out.m_changeType == 1);
    CHECK(out.m_item.m_announcementId == 10025);
    CHECK(out.m_item.m_title == "t");
    CHECK(out.m_item.m_severity == 1);
    CHECK(out.m_item.m_sentTimeMs == 1754300200000LL);
}

TEST_CASE("PAC8 parseAnnouncementNotify 撤回只带公告ID")
{
    //服务端撤回时不重复传正文，本端凭ID把本地那条移除即可。
    AnnouncementNotify out;
    REQUIRE(AnnounceProtocolClient::parseAnnouncementNotify(
        "{\"changetype\":2,\"announcement\":{\"announcementid\":\"10025\"}}", out));
    CHECK(out.m_changeType == 2);
    CHECK(out.m_item.m_announcementId == 10025);
    //其余字段是默认值，调用方不该去读，这里断言一下确保不是垃圾值。
    CHECK(out.m_item.m_title.empty());
    CHECK(out.m_item.m_sentTimeMs == 0);
}

TEST_CASE("PAC9 parseAnnouncementNotify 畸形输入安全失败")
{
    AnnouncementNotify out;
    CHECK_FALSE(AnnounceProtocolClient::parseAnnouncementNotify("", out));
    CHECK_FALSE(AnnounceProtocolClient::parseAnnouncementNotify("bad json", out));
    //缺 changetype：不知道该新增还是该删除，没法处理。
    CHECK_FALSE(AnnounceProtocolClient::parseAnnouncementNotify(
        "{\"announcement\":{\"announcementid\":\"1\"}}", out));
    //缺 announcement 对象。
    CHECK_FALSE(AnnounceProtocolClient::parseAnnouncementNotify("{\"changetype\":1}", out));
    //公告ID非正：两种 changetype 都必须有有效ID才处理得了。
    CHECK_FALSE(AnnounceProtocolClient::parseAnnouncementNotify(
        "{\"changetype\":2,\"announcement\":{\"announcementid\":\"0\"}}", out));
}

TEST_CASE("PAC10 parseMarkReadResponse 读出结果码与回带的公告ID")
{
    AnnounceMarkReadResult ok;
    REQUIRE(AnnounceProtocolClient::parseMarkReadResponse(
        "{\"code\":0,\"msg\":\"ok\",\"announcementid\":\"10023\"}", ok));
    CHECK(ok.m_code == 0);
    CHECK(ok.m_msg == "ok");
    //服务端原样回带公告ID，用户连点几条时靠它把应答对上号。
    CHECK(ok.m_announcementId == 10023);

    //msg 与 announcementid 缺失时给默认值，不是垃圾值，也不判整条应答失败。
    AnnounceMarkReadResult bare;
    REQUIRE(AnnounceProtocolClient::parseMarkReadResponse("{\"code\":1}", bare));
    CHECK(bare.m_code == 1);
    CHECK(bare.m_msg.empty());
    CHECK(bare.m_announcementId == 0);

    AnnounceMarkReadResult bad;
    CHECK_FALSE(AnnounceProtocolClient::parseMarkReadResponse("{\"msg\":\"ok\"}", bad));
    CHECK_FALSE(AnnounceProtocolClient::parseMarkReadResponse("garbage", bad));
}
