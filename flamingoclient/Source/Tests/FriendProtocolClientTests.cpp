/**
 *  FriendProtocolClientTests.cpp
 *
 *  好友管理报文（客户端侧）单元测试（Suite FP）。当前覆盖「设置好友备注名」
 *  cmd_set_friend_remark(1209)：
 *    · 请求构造 —— userid 走字符串编码、备注原样写入、空串表示清除备注、
 *      备注含引号 / 反斜杠 / 中文时 JSON 仍合法。
 *    · 应答解析 —— 取出 code / msg 与服务端原样回带的 userid；畸形输入安全失败。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰 socket。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "catch_amalgamated.hpp"
#include "../net/FriendProtocolClient.h"

#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;

namespace {

//把请求报文解析回 Json::Value，用来断言字段值。
//注意不能改成"在报文文本里找子串"：jsoncpp 默认把非 ASCII 转义成 \uXXXX，
//中文备注不会以 UTF-8 原字节出现在报文里，按子串找会误判成"没写进去"。
bool ParseBody(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

} // anonymous namespace

TEST_CASE("FP1 请求构造：userid 走字符串，备注原样写入", "[friendprotocol]")
{
    // userid 必须是字符串——服务端 parseSetFriendRemarkRequest 用 getStringId 取它，
    // 写成数字会被判为格式非法、整条请求被拒。
    const char* kRemarkUtf8 = "\xE4\xBA\xA7\xE5\x93\x81\xE7\xBB\x84";
    const std::string body =
        FriendProtocolClient::buildSetFriendRemarkRequest(100009, kRemarkUtf8);

    Json::Value root;
    REQUIRE(ParseBody(body, root));
    REQUIRE(root["userid"].isString());
    CHECK(root["userid"].asString() == "100009");
    CHECK(root["remark"].asString() == kRemarkUtf8);
}

TEST_CASE("FP2 请求构造：备注为空串表示清除，字段仍要写出", "[friendprotocol]")
{
    // "把备注清空" 是一次正常的提交，不是"不提交"。字段漏写会让服务端按
    // remark 缺省处理 —— 虽然结果同样是清除，但客户端不该依赖这种巧合。
    const std::string body = FriendProtocolClient::buildSetFriendRemarkRequest(100009, "");

    Json::Value root;
    REQUIRE(ParseBody(body, root));
    CHECK(root["userid"].asString() == "100009");
    REQUIRE(root.isMember("remark"));
    REQUIRE(root["remark"].isString());
    CHECK(root["remark"].asString().empty());
}

TEST_CASE("FP3 请求构造：备注含引号与反斜杠时 JSON 仍合法", "[friendprotocol]")
{
    // 备注是用户自由输入的，双引号、反斜杠、换行都可能出现。只要能原样解析回来，
    // 就说明转义正确、没把报文拼坏。
    const char* kMessy = "he said \"hi\"\\ok\n\xE4\xB8\xAD\xE6\x96\x87";
    const std::string body = FriendProtocolClient::buildSetFriendRemarkRequest(100009, kMessy);

    Json::Value root;
    REQUIRE(ParseBody(body, root));
    CHECK(root["remark"].asString() == kMessy);
}

TEST_CASE("FP4 应答解析：取出 code / msg 与回带的 userid", "[friendprotocol]")
{
    SetFriendRemarkResult out;
    REQUIRE(FriendProtocolClient::parseSetFriendRemarkResponse(
        "{\"code\":0,\"msg\":\"ok\",\"userid\":\"100009\"}", out));
    CHECK(out.m_code == 0);
    CHECK(out.m_msg == "ok");
    // 回带的 userid 是失败时精确回滚的依据：用户连着改几个人时会有多条请求在途
    CHECK(out.m_userid == 100009);
}

TEST_CASE("FP5 应答解析：失败应答同样能对上号", "[friendprotocol]")
{
    // 失败这条恰恰是最需要 userid 的 —— 要靠它把那一个人的乐观更新退回去。
    SetFriendRemarkResult out;
    REQUIRE(FriendProtocolClient::parseSetFriendRemarkResponse(
        "{\"code\":211,\"msg\":\"not a friend\",\"userid\":\"100009\"}", out));
    CHECK(out.m_code == 211);
    CHECK(out.m_msg == "not a friend");
    CHECK(out.m_userid == 100009);
}

TEST_CASE("FP6 应答解析：不回带 userid 的老服务端不整条失败", "[friendprotocol]")
{
    // code 已经拿到了，调用方至少知道成没成，只是没法精确回滚。这种情况下
    // m_userid 为 0，调用方据此放弃回滚而不是退错人。
    SetFriendRemarkResult out;
    REQUIRE(FriendProtocolClient::parseSetFriendRemarkResponse(
        "{\"code\":0,\"msg\":\"ok\"}", out));
    CHECK(out.m_code == 0);
    CHECK(out.m_userid == 0);
}

TEST_CASE("FP7 应答解析：畸形输入安全失败", "[friendprotocol]")
{
    SetFriendRemarkResult out;
    // 空串 / 非 JSON / 顶层不是对象 / 缺 code / code 不是整型，一律返回 false，
    // 调用方丢弃该应答而不是拿着未初始化的结果去改界面。
    CHECK_FALSE(FriendProtocolClient::parseSetFriendRemarkResponse("", out));
    CHECK_FALSE(FriendProtocolClient::parseSetFriendRemarkResponse("not json", out));
    CHECK_FALSE(FriendProtocolClient::parseSetFriendRemarkResponse("[1,2,3]", out));
    CHECK_FALSE(FriendProtocolClient::parseSetFriendRemarkResponse("{\"msg\":\"ok\"}", out));
    CHECK_FALSE(FriendProtocolClient::parseSetFriendRemarkResponse("{\"code\":\"0\"}", out));
}

TEST_CASE("FP8 应答解析：userid 大整数不丢精度", "[friendprotocol]")
{
    // 与本协议其余 64 位 ID 同一口径：字符串编码，解析回 int64 不能退化成 double。
    SetFriendRemarkResult out;
    REQUIRE(FriendProtocolClient::parseSetFriendRemarkResponse(
        "{\"code\":0,\"msg\":\"ok\",\"userid\":\"9007199254740993\"}", out));
    CHECK(out.m_userid == 9007199254740993LL);   // 2^53 + 1
}
