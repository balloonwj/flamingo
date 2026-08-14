/**
 *  ContactTagProtocolClientTests.cpp
 *
 *  v2「联系人标签」报文客户端侧编解码的测试（Suite CTP）：
 *    · buildSetContactTagsRequest —— userid 走字符串、tags 恒为数组、空数组也要写出。
 *    · parseSetContactTagsResponse —— 正常应答、各字段缺失、畸形输入的容错。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰网络。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../net/ContactTagProtocolClient.h"

using namespace net;

// =======================================================================
// buildSetContactTagsRequest —— 请求构造
// =======================================================================

TEST_CASE("CTP1 请求里 userid 走字符串、tags 走数组", "[contacttagproto]")
{
    //断言用纯 ASCII 标签：非 ASCII 会被 jsoncpp 转义成 \uXXXX，按原始 UTF-8 字节
    //去 find 必然落空（那条行为另见 CTP1b）。
    std::vector<std::string> tags;
    tags.push_back("design");
    tags.push_back("outsourced");

    const std::string req = ContactTagProtocolClient::buildSetContactTagsRequest(5081, tags);

    //userid 必须是字符串形态：大整数直接写成 JSON number 会在某些解析器上丢精度。
    CHECK(req.find("\"userid\":\"5081\"") != std::string::npos);
    CHECK(req.find("\"tags\":[\"design\",\"outsourced\"]") != std::string::npos);
}

TEST_CASE("CTP1b 中文标签被转义成 \\uXXXX，这是既有链路的一贯行为", "[contacttagproto]")
{
    //jsoncpp 的 StreamWriterBuilder 默认不开 emitUTF8，非 ASCII 一律写成 \uXXXX
    //转义序列。服务端同样用 jsoncpp 解析，会把它还原成 UTF-8，所以功能上没有影响。
    //本仓库既有的备注名请求（FriendProtocolClient::buildSetFriendRemarkRequest）用的
    //是逐字相同的序列化写法，带中文的备注实测能正常落库，可作旁证。
    //这条用例的意义是把这个容易看岔的行为钉住：将来若有人改用 emitUTF8，报文形态
    //会变，本用例会立刻失败并把改动者引到这段说明上。
    std::vector<std::string> tags;
    tags.push_back("\xE8\xAE\xBE\xE8\xAE\xA1");          // "设计"（U+8BBE U+8BA1）

    const std::string req = ContactTagProtocolClient::buildSetContactTagsRequest(5081, tags);

    CHECK(req.find("\\u8bbe\\u8ba1") != std::string::npos);
    //原始 UTF-8 字节不会出现在报文里
    CHECK(req.find("\xE8\xAE\xBE\xE8\xAE\xA1") == std::string::npos);
}

TEST_CASE("CTP2 空列表也要写出 tags 字段，且是数组不是 null", "[contacttagproto]")
{
    //服务端把"tags 字段缺失"判为报文非法而不是按空数组处理 —— 漏发字段会把用户
    //贴好的标签整份清空，两者语义差得太远。所以清空操作必须发一个显式的空数组。
    const std::vector<std::string> empty;
    const std::string req = ContactTagProtocolClient::buildSetContactTagsRequest(5081, empty);

    CHECK(req.find("\"tags\":[]") != std::string::npos);
    CHECK(req.find("null") == std::string::npos);
}

TEST_CASE("CTP3 请求构造不做归一，原样发出", "[contacttagproto]")
{
    //归一（trim / 去重 / 判长）是服务端的事，客户端原样发、再采信应答回带的结果。
    //本端自己先归一一遍只会多出一处可能与服务端走岔的规则。
    std::vector<std::string> tags;
    tags.push_back("  design  ");
    tags.push_back("design");        // 与上一条 trim 后重复

    const std::string req = ContactTagProtocolClient::buildSetContactTagsRequest(5081, tags);

    //首尾空白原样保留、重复的那条也没被去掉，说明客户端确实没有擅自归一
    CHECK(req.find("\"tags\":[\"  design  \",\"design\"]") != std::string::npos);
}

// =======================================================================
// parseSetContactTagsResponse —— 应答解析
// =======================================================================

TEST_CASE("CTP4 成功应答：取到 code / msg / userid / tags", "[contacttagproto]")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"userid\":\"5081\","
        "\"tags\":[\"\xE8\xAE\xBE\xE8\xAE\xA1\",\"\xE5\xA4\x96\xE5\x8C\x85\"]}";

    SetContactTagsResult r;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(body, r));
    CHECK(r.m_code == 0);
    CHECK(r.m_msg == "ok");
    CHECK(r.m_userid == 5081);
    REQUIRE(r.m_tags.size() == 2);
    CHECK(r.m_tags[0] == "\xE8\xAE\xBE\xE8\xAE\xA1");
    CHECK(r.m_tags[1] == "\xE5\xA4\x96\xE5\x8C\x85");
}

TEST_CASE("CTP5 失败应答同样回带 userid 与服务端当前的列表", "[contacttagproto]")
{
    //条数超上限（error_code_tag_limit=211）时，服务端把它当前实际存着的那一份发回来，
    //客户端直接拿它覆盖本地缓存即可完成回滚，不必自己维护回滚快照。
    const std::string body =
        "{\"code\":211,\"msg\":\"too many tags\",\"userid\":\"5081\","
        "\"tags\":[\"\xE8\xAE\xBE\xE8\xAE\xA1\"]}";

    SetContactTagsResult r;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(body, r));
    CHECK(r.m_code == 211);
    CHECK(r.m_userid == 5081);
    REQUIRE(r.m_tags.size() == 1);
}

TEST_CASE("CTP6 清空成功时 tags 是空数组", "[contacttagproto]")
{
    const std::string body = "{\"code\":0,\"msg\":\"ok\",\"userid\":\"5081\",\"tags\":[]}";

    SetContactTagsResult r;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(body, r));
    CHECK(r.m_code == 0);
    CHECK(r.m_tags.empty());
}

TEST_CASE("CTP7 userid 缺失时置 0，调用方据此放弃回滚", "[contacttagproto]")
{
    //老服务端可能不回带 userid。拿到 0 就无从判断这条应答对应哪一次请求，
    //此时宁可不回滚，也不要退错人的标签。
    const std::string body = "{\"code\":0,\"msg\":\"ok\",\"tags\":[]}";

    SetContactTagsResult r;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(body, r));
    CHECK(r.m_userid == 0);
}

TEST_CASE("CTP8 tags 缺失或不是数组时按空列表处理，不判失败", "[contacttagproto]")
{
    SetContactTagsResult r1;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(
        "{\"code\":0,\"userid\":\"5081\"}", r1));
    CHECK(r1.m_tags.empty());

    SetContactTagsResult r2;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(
        "{\"code\":0,\"userid\":\"5081\",\"tags\":\"oops\"}", r2));
    CHECK(r2.m_tags.empty());
}

TEST_CASE("CTP9 数组里混进非字符串元素只跳过那一个，不整条作废", "[contacttagproto]")
{
    //code 已经拿到了，为一条脏数据把整个应答判为解析失败得不偿失。
    const std::string body =
        "{\"code\":0,\"userid\":\"5081\",\"tags\":[\"\xE8\xAE\xBE\xE8\xAE\xA1\",123,null,"
        "\"\xE5\xA4\x96\xE5\x8C\x85\"]}";

    SetContactTagsResult r;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(body, r));
    REQUIRE(r.m_tags.size() == 2);
    CHECK(r.m_tags[0] == "\xE8\xAE\xBE\xE8\xAE\xA1");
    CHECK(r.m_tags[1] == "\xE5\xA4\x96\xE5\x8C\x85");
}

TEST_CASE("CTP10 缺 code、非法 JSON、顶层非对象一律判解析失败", "[contacttagproto]")
{
    SetContactTagsResult r;
    //code 是判定成败的唯一依据，缺了就没法用
    CHECK_FALSE(ContactTagProtocolClient::parseSetContactTagsResponse(
        "{\"msg\":\"ok\",\"userid\":\"5081\"}", r));
    CHECK_FALSE(ContactTagProtocolClient::parseSetContactTagsResponse("not json", r));
    CHECK_FALSE(ContactTagProtocolClient::parseSetContactTagsResponse("[1,2,3]", r));
    CHECK_FALSE(ContactTagProtocolClient::parseSetContactTagsResponse("", r));
}

TEST_CASE("CTP11 解析失败时出参被复位，不残留上一次的值", "[contacttagproto]")
{
    SetContactTagsResult r;
    REQUIRE(ContactTagProtocolClient::parseSetContactTagsResponse(
        "{\"code\":0,\"userid\":\"5081\",\"tags\":[\"\xE8\xAE\xBE\xE8\xAE\xA1\"]}", r));
    REQUIRE(r.m_tags.size() == 1);

    //同一个出参再喂一段非法输入：不能留着上一次的 userid 与标签，否则调用方
    //忽略返回值时会拿旧数据当新结果用。
    CHECK_FALSE(ContactTagProtocolClient::parseSetContactTagsResponse("not json", r));
    CHECK(r.m_userid == 0);
    CHECK(r.m_tags.empty());
    CHECK(r.m_code == -1);
}
