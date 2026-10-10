/**
 *  ProfileProtocolClientTests.cpp
 *
 *  v2 账号资料报文编解码（客户端侧，ProfileProtocolClient）的测试：
 *    · buildUpdateSignatureRequest 产出只含 signature 字段的紧凑 JSON（含空串）。
 *    · parseUpdateProfileResponse 解析通用 {code,msg} 成功 / 失败码 / 非法输入。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖网络。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../net/ProfileProtocolClient.h"

using namespace net;

// =====================================================================
// PPC. cmd_update_profile 请求构造 / 应答解析契约
// =====================================================================

TEST_CASE("PPC1 build 只含 signature 字段", "[profileproto]")
{
    std::string j = ProfileProtocolClient::buildUpdateSignatureRequest("hi");
    // 不强求整串相等（容忍 jsoncpp 的紧凑格式细节），只校验 signature 键值正确，
    // 且不含其它资料字段（nickname/gender 等不出现 = 只改签名）。
    CHECK(j.find("\"signature\":\"hi\"") != std::string::npos);
    CHECK(j.find("nickname") == std::string::npos);
    CHECK(j.find("gender")   == std::string::npos);
}

TEST_CASE("PPC2 build 空签名 → signature 为空串（表示清空）", "[profileproto]")
{
    std::string j = ProfileProtocolClient::buildUpdateSignatureRequest("");
    CHECK(j.find("\"signature\":\"\"") != std::string::npos);
}

TEST_CASE("PPC3 parse 成功应答 {code:0,msg:ok}", "[profileproto]")
{
    UpdateProfileResponse r;
    bool ok = ProfileProtocolClient::parseUpdateProfileResponse("{\"code\":0,\"msg\":\"ok\"}", r);
    REQUIRE(ok);
    CHECK(r.code == 0);
    CHECK(r.msg == "ok");
}

TEST_CASE("PPC4 parse 失败应答 {code:106}", "[profileproto]")
{
    UpdateProfileResponse r;
    bool ok = ProfileProtocolClient::parseUpdateProfileResponse(
        "{\"code\":106,\"msg\":\"update profile failed\"}", r);
    REQUIRE(ok);
    CHECK(r.code == 106);
    CHECK(r.msg == "update profile failed");
}

TEST_CASE("PPC5 parse 缺 msg 字段：合法，msg 为空", "[profileproto]")
{
    UpdateProfileResponse r;
    bool ok = ProfileProtocolClient::parseUpdateProfileResponse("{\"code\":0}", r);
    REQUIRE(ok);
    CHECK(r.code == 0);
    CHECK(r.msg.empty());
}

TEST_CASE("PPC6 parse 非法 JSON / 缺 code → false", "[profileproto]")
{
    UpdateProfileResponse r1;
    CHECK_FALSE(ProfileProtocolClient::parseUpdateProfileResponse("not a json", r1));

    UpdateProfileResponse r2;
    CHECK_FALSE(ProfileProtocolClient::parseUpdateProfileResponse("{\"msg\":\"x\"}", r2));

    UpdateProfileResponse r3;
    CHECK_FALSE(ProfileProtocolClient::parseUpdateProfileResponse("", r3));
}

TEST_CASE("PPC6b buildUpdateProfile 标记提交的 6 个字段都在、值正确、邮箱键名是 mail",
          "[profileproto]")
{
    // 用 ASCII 值断言契约：jsoncpp 默认把非 ASCII 转义成 \uXXXX，用中文字面量查子串
    // 会假阴性；中文整段往返留运行期目检。这里只验"键名 / 类型 / 值"结构正确。
    ProfileUpdateFields fields;
    fields.m_values.m_nickname  = "Tom";
    fields.m_values.m_signature = "hello";
    fields.m_values.m_gender    = 1;
    fields.m_values.m_birthday  = 19950402;
    fields.m_values.m_address   = "Shanghai";
    fields.m_values.m_mail      = "tom@a.com";
    fields.m_hasNickname  = true;
    fields.m_hasSignature = true;
    fields.m_hasGender    = true;
    fields.m_hasBirthday  = true;
    fields.m_hasAddress   = true;
    fields.m_hasMail      = true;
    std::string j = ProfileProtocolClient::buildUpdateProfileRequest(fields);
    CHECK(j.find("\"nickname\":\"Tom\"")       != std::string::npos);
    CHECK(j.find("\"signature\":\"hello\"")    != std::string::npos);
    CHECK(j.find("\"gender\":1")               != std::string::npos);
    CHECK(j.find("\"birthday\":19950402")      != std::string::npos);
    CHECK(j.find("\"address\":\"Shanghai\"")   != std::string::npos);
    CHECK(j.find("\"mail\":\"tom@a.com\"")      != std::string::npos);
    // 邮箱键名必须是 "mail"，不能误写成 "email"（否则服务端不认）。
    CHECK(j.find("\"email\"") == std::string::npos);
}

TEST_CASE("PPC6c buildUpdateProfile 标记提交的空串字段与 birthday=0（未设）也照常出键",
          "[profileproto]")
{
    // 值全部是默认的空串与 0，只把要提交的几项标记上
    ProfileUpdateFields fields;
    fields.m_hasNickname = true;
    fields.m_hasGender   = true;
    fields.m_hasBirthday = true;
    fields.m_hasMail     = true;
    std::string j = ProfileProtocolClient::buildUpdateProfileRequest(fields);
    CHECK(j.find("\"nickname\":\"\"")  != std::string::npos);
    CHECK(j.find("\"gender\":0")       != std::string::npos);
    CHECK(j.find("\"birthday\":0")     != std::string::npos);
    CHECK(j.find("\"mail\":\"\"")      != std::string::npos);
}

// =====================================================================
// PPC.PRESENCE. cmd_change_presence 请求构造 / 应答解析契约
// =====================================================================

TEST_CASE("PPC7 buildChangePresence 对 0/1/2/3 各产出 {\"presence\":N}", "[profileproto][presence]")
{
    // 离线(0)/在线(1)/离开(2)/勿扰(3) —— 逐个核对紧凑 JSON 里 presence 值正确，
    // 且不含其它字段（只改在线状态）。
    CHECK(ProfileProtocolClient::buildChangePresenceRequest(0).find("\"presence\":0") != std::string::npos);
    CHECK(ProfileProtocolClient::buildChangePresenceRequest(1).find("\"presence\":1") != std::string::npos);
    CHECK(ProfileProtocolClient::buildChangePresenceRequest(2).find("\"presence\":2") != std::string::npos);

    std::string j = ProfileProtocolClient::buildChangePresenceRequest(3);
    CHECK(j.find("\"presence\":3") != std::string::npos);
    CHECK(j.find("signature") == std::string::npos);
    CHECK(j.find("nickname")  == std::string::npos);
}

TEST_CASE("PPC8 parseChangePresence 成功应答 {code:0,msg:ok}", "[profileproto][presence]")
{
    ChangePresenceResponse r;
    bool ok = ProfileProtocolClient::parseChangePresenceResponse("{\"code\":0,\"msg\":\"ok\"}", r);
    REQUIRE(ok);
    CHECK(r.code == 0);
    CHECK(r.msg == "ok");
}

TEST_CASE("PPC9 parseChangePresence 失败应答 {code:400}", "[profileproto][presence]")
{
    ChangePresenceResponse r;
    bool ok = ProfileProtocolClient::parseChangePresenceResponse(
        "{\"code\":400,\"msg\":\"bad request\"}", r);
    REQUIRE(ok);
    CHECK(r.code == 400);
    CHECK(r.msg == "bad request");
}

TEST_CASE("PPC10 parseChangePresence 非法 JSON / 缺 code → false", "[profileproto][presence]")
{
    ChangePresenceResponse r1;
    CHECK_FALSE(ProfileProtocolClient::parseChangePresenceResponse("not a json", r1));

    ChangePresenceResponse r2;
    CHECK_FALSE(ProfileProtocolClient::parseChangePresenceResponse("{\"msg\":\"x\"}", r2));
}

// =====================================================================
// PPC.AVATAR. cmd_change_avatar 请求构造契约（应答复用 parseUpdateProfileResponse）
// =====================================================================

TEST_CASE("PPC11 buildChangeAvatar 字形头像 → {facetype:1,customface:\"L|0\"}", "[profileproto][avatar]")
{
    std::string j = ProfileProtocolClient::buildChangeAvatarRequest(1, "L|0");
    CHECK(j.find("\"facetype\":1")        != std::string::npos);
    CHECK(j.find("\"customface\":\"L|0\"") != std::string::npos);
    // 只带这两项，不夹带签名 / 昵称等资料字段。
    CHECK(j.find("signature") == std::string::npos);
    CHECK(j.find("nickname")  == std::string::npos);
}

TEST_CASE("PPC12 buildChangeAvatar 自定义图 → facetype=2 + fileid", "[profileproto][avatar]")
{
    std::string j = ProfileProtocolClient::buildChangeAvatarRequest(2, "abc123fileid");
    CHECK(j.find("\"facetype\":2")                   != std::string::npos);
    CHECK(j.find("\"customface\":\"abc123fileid\"")  != std::string::npos);
}

TEST_CASE("PPC13 buildChangeAvatar 自动头像 → facetype=0 + 空 customface", "[profileproto][avatar]")
{
    std::string j = ProfileProtocolClient::buildChangeAvatarRequest(0, "");
    CHECK(j.find("\"facetype\":0")       != std::string::npos);
    CHECK(j.find("\"customface\":\"\"")  != std::string::npos);
}
