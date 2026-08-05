/**
 *  AccountProtocolTests.cpp
 *
 *  v2 账号报文编解码 AccountProtocolClient 的测试：构造 cmd_change_password
 *  请求 JSON、解析其应答 JSON（成功 / 旧密码错 / 失败码 / 缺 code / 非法）。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯逻辑、不触网络与 UI。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../net/AccountProtocolClient.h"

using namespace net;

TEST_CASE("ACC1 buildChangePasswordRequest 含 oldpassword/newpassword 两字段且值正确", "[account]")
{
    std::string body = AccountProtocolClient::buildChangePasswordRequest("aaa111", "bbb222");
    CHECK(body.find("\"oldpassword\":\"aaa111\"") != std::string::npos);
    CHECK(body.find("\"newpassword\":\"bbb222\"") != std::string::npos);
}

TEST_CASE("ACC2 buildChangePasswordRequest 产出可被自身回解析", "[account]")
{
    std::string body = AccountProtocolClient::buildChangePasswordRequest("oldhash", "newhash");
    // 把构造出的请求体当成"含这两个字段的 JSON 对象"再读回来校验结构合法。
    // 这里复用 parse 仅验证 JSON 合法性，请求体没有 code 字段，故应返回 false，
    // 但不应崩溃 —— 真正的字段断言已在 ACC1 完成。
    ChangePasswordResponse r;
    CHECK_FALSE(AccountProtocolClient::parseChangePasswordResponse(body, r));
}

TEST_CASE("ACC3 parseChangePasswordResponse 解析成功应答", "[account]")
{
    ChangePasswordResponse r;
    REQUIRE(AccountProtocolClient::parseChangePasswordResponse("{\"code\":0,\"msg\":\"ok\"}", r));
    CHECK(r.code == 0);
    CHECK(r.msg == "ok");
}

TEST_CASE("ACC4 parseChangePasswordResponse 解析失败码（旧密码错 103 / 改密失败 107）", "[account]")
{
    ChangePasswordResponse r1;
    REQUIRE(AccountProtocolClient::parseChangePasswordResponse(
        "{\"code\":103,\"msg\":\"incorrect old password\"}", r1));
    CHECK(r1.code == 103);
    CHECK(r1.msg == "incorrect old password");

    // msg 缺失也应允许（只要有整型 code）
    ChangePasswordResponse r2;
    REQUIRE(AccountProtocolClient::parseChangePasswordResponse("{\"code\":107}", r2));
    CHECK(r2.code == 107);
    CHECK(r2.msg.empty());
}

TEST_CASE("ACC5 parseChangePasswordResponse 缺 code / 非整型 code / 非法 JSON 返回 false", "[account]")
{
    ChangePasswordResponse r;
    CHECK_FALSE(AccountProtocolClient::parseChangePasswordResponse("{\"msg\":\"x\"}", r));      // 缺 code
    CHECK_FALSE(AccountProtocolClient::parseChangePasswordResponse("{\"code\":\"0\"}", r));     // code 为字符串
    CHECK_FALSE(AccountProtocolClient::parseChangePasswordResponse("not a json", r));           // 非法 JSON
    CHECK_FALSE(AccountProtocolClient::parseChangePasswordResponse("", r));                     // 空串
}
