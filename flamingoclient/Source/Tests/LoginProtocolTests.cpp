/**
 *  v2 登录报文编解码（客户端侧）单元测试（Suite LP），LoginProtocolTests.cpp
 *  覆盖：buildLoginRequest 字段完整性与类型、parseLoginResponse 成功/失败
 *       应答、非法 JSON / 缺 code 字段、64 位 userid 精度。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-05-22
 */
#include "catch_amalgamated.hpp"
#include "../net/LoginProtocolClient.h"
//见 LoginProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查 buildLoginRequest 的产物
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("LP1 buildLoginRequest 含全部字段且类型正确")
{
    LoginRequestParams params;
    params.clientversion = "3.2.1";
    params.account       = "13917043329";
    params.password      = "abcdef0123456789";
    params.clienttype    = 1;
    params.presence      = 1;
    params.deviceid      = "PC-9F2A1C7E";
    params.devicename    = "test-pc";

    std::string json = LoginProtocolClient::buildLoginRequest(params);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["clientversion"].isString());
    CHECK(root["clientversion"].asString() == "3.2.1");
    CHECK(root["account"].asString() == "13917043329");
    CHECK(root["password"].asString() == "abcdef0123456789");
    CHECK(root["clienttype"].isInt());
    CHECK(root["clienttype"].asInt() == 1);
    CHECK(root["presence"].isInt());
    CHECK(root["presence"].asInt() == 1);
    CHECK(root["deviceid"].asString() == "PC-9F2A1C7E");
    CHECK(root["devicename"].asString() == "test-pc");
}

TEST_CASE("LP2 parseLoginResponse 解析成功应答")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"userid\":\"8\",\"servertime\":1716268800000,"
        "\"profile\":{\"usertype\":0,\"username\":\"13917043329\",\"nickname\":\"zhangyl\","
        "\"facetype\":0,\"customface\":\"md5abc\",\"gender\":1,\"birthday\":19891208,"
        "\"signature\":\"hi\",\"address\":\"Shanghai\",\"phonenumber\":\"138\","
        "\"mail\":\"a@b.c\",\"enterpriseid\":\"5001\"}}";
    LoginResponse resp;
    REQUIRE(LoginProtocolClient::parseLoginResponse(body, resp));
    CHECK(resp.code == 0);
    CHECK(resp.msg == "ok");
    CHECK(resp.userid == 8);
    CHECK(resp.servertime == 1716268800000LL);
    CHECK(resp.profile.usertype == 0);
    CHECK(resp.profile.username == "13917043329");
    CHECK(resp.profile.nickname == "zhangyl");
    CHECK(resp.profile.facetype == 0);
    CHECK(resp.profile.customface == "md5abc");
    CHECK(resp.profile.gender == 1);
    CHECK(resp.profile.birthday == 19891208);
    CHECK(resp.profile.signature == "hi");
    CHECK(resp.profile.address == "Shanghai");
    CHECK(resp.profile.phonenumber == "138");
    CHECK(resp.profile.mail == "a@b.c");
    CHECK(resp.profile.enterpriseid == 5001);
}

TEST_CASE("LP3 parseLoginResponse 解析失败应答")
{
    std::string body = "{\"code\":103,\"msg\":\"incorrect password\"}";
    LoginResponse resp;
    REQUIRE(LoginProtocolClient::parseLoginResponse(body, resp));
    CHECK(resp.code == 103);
    CHECK(resp.msg == "incorrect password");
}

TEST_CASE("LP4 parseLoginResponse 拒绝非法 JSON")
{
    LoginResponse resp;
    CHECK_FALSE(LoginProtocolClient::parseLoginResponse("not a json", resp));
    CHECK_FALSE(LoginProtocolClient::parseLoginResponse("", resp));
    //JSON 合法但不是对象
    CHECK_FALSE(LoginProtocolClient::parseLoginResponse("[1,2,3]", resp));
}

TEST_CASE("LP5 parseLoginResponse 拒绝缺 code 字段")
{
    LoginResponse resp;
    //完全没有 code 字段
    CHECK_FALSE(LoginProtocolClient::parseLoginResponse("{\"msg\":\"ok\"}", resp));
    //code 类型错误（字符串而非整数）
    CHECK_FALSE(LoginProtocolClient::parseLoginResponse("{\"code\":\"0\"}", resp));
}

TEST_CASE("LP6 parseLoginResponse 大数值 userid 不丢精度")
{
    //userid 以字符串编码，取一个超过 32 位无符号上限的值
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"userid\":\"4294967300\",\"servertime\":0,"
        "\"profile\":{}}";
    LoginResponse resp;
    REQUIRE(LoginProtocolClient::parseLoginResponse(body, resp));
    CHECK(resp.userid == 4294967300LL);
}

//——以下为 cmd_kickout(1006) 被踢下线推送的解析用例——

TEST_CASE("KICK6 parseKickoutNotify 解析正常包体")
{
    //服务端顶号时下发的原样报文，见 chatserversrc/ChatSession.cpp 的 onLogin
    std::string body = "{\"reason\":1,\"msg\":\"logged in on another device\"}";
    KickoutNotify notify;
    REQUIRE(LoginProtocolClient::parseKickoutNotify(body, notify));
    CHECK(notify.m_reason == 1);
    CHECK(notify.m_msg == "logged in on another device");
}

TEST_CASE("KICK7 parseKickoutNotify 拒绝非法 JSON 且不动出参")
{
    KickoutNotify notify;
    CHECK_FALSE(LoginProtocolClient::parseKickoutNotify("not a json", notify));
    CHECK_FALSE(LoginProtocolClient::parseKickoutNotify("", notify));
    //JSON 合法但不是对象
    CHECK_FALSE(LoginProtocolClient::parseKickoutNotify("[1,2,3]", notify));
    //解析失败时出参必须保持构造后的默认值，调用方照默认的"未知原因"走兜底文案
    CHECK(notify.m_reason == 0);
    CHECK(notify.m_msg.empty());
}

TEST_CASE("KICK8 parseKickoutNotify 缺 reason 字段仍算解析成功")
{
    //被踢下线本身不依赖 reason：服务端哪天改了字段，客户端也必须照常下线，
    //只是提示文案退回兜底那一句。故这里返回 true 而不是 false。
    KickoutNotify notify;
    REQUIRE(LoginProtocolClient::parseKickoutNotify("{\"msg\":\"bye\"}", notify));
    CHECK(notify.m_reason == 0);
    CHECK(notify.m_msg == "bye");

    //reason 类型不对（字符串而非整数）同样按 0 处理
    KickoutNotify notify2;
    REQUIRE(LoginProtocolClient::parseKickoutNotify("{\"reason\":\"1\"}", notify2));
    CHECK(notify2.m_reason == 0);
}
