/**
 *  v2 注册 / 找回密码单元测试，test_registerprotocol.cpp
 *  Suite R：RegisterProtocol 报文编解码。
 *  Suite M：SmsCodeManager 短信验证码生成与校验。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/RegisterProtocol.h"
#include "../chatserversrc/SmsCodeManager.h"
#include "../jsoncpp1.9.0/json.h"

//把 JSON 文本解析成 Json::Value，供断言检查
static bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("R1 parseRegisterRequest 解析合法注册请求")
{
    std::string body = "{\"nickname\":\"林知夏\","
                       "\"password\":\"abcdef0123\",\"phone\":\"13917043329\","
                       "\"company\":\"明远科技有限公司\"}";
    RegisterRequest req;
    REQUIRE(RegisterProtocol::parseRegisterRequest(body, req));
    CHECK(req.nickname == "林知夏");
    CHECK(req.password == "abcdef0123");
    CHECK(req.phone == "13917043329");
    CHECK(req.company == "明远科技有限公司");
}

TEST_CASE("R2 parseRegisterRequest 拒绝缺字段 / 非法请求")
{
    RegisterRequest req;
    //缺 company
    CHECK_FALSE(RegisterProtocol::parseRegisterRequest(
        "{\"nickname\":\"a\",\"password\":\"c\",\"phone\":\"d\"}", req));
    //缺 nickname
    CHECK_FALSE(RegisterProtocol::parseRegisterRequest(
        "{\"password\":\"c\",\"phone\":\"d\",\"company\":\"e\"}", req));
    //company 为空串
    CHECK_FALSE(RegisterProtocol::parseRegisterRequest(
        "{\"nickname\":\"a\",\"password\":\"c\",\"phone\":\"d\",\"company\":\"\"}", req));
    //company 只有空白字符：去掉首尾空白后为空，同样拒绝
    CHECK_FALSE(RegisterProtocol::parseRegisterRequest(
        "{\"nickname\":\"a\",\"password\":\"c\",\"phone\":\"d\",\"company\":\"   \"}", req));
    //company 类型不是字符串
    CHECK_FALSE(RegisterProtocol::parseRegisterRequest(
        "{\"nickname\":\"a\",\"password\":\"c\",\"phone\":\"d\",\"company\":123}", req));
    //phone 为空串
    CHECK_FALSE(RegisterProtocol::parseRegisterRequest(
        "{\"nickname\":\"a\",\"password\":\"c\",\"phone\":\"\",\"company\":\"e\"}", req));
    //JSON 非法
    CHECK_FALSE(RegisterProtocol::parseRegisterRequest("not a json", req));
}

TEST_CASE("R7 parseRegisterRequest 去掉公司名首尾空白")
{
    //用户手打公司名很容易多敲空格，而服务端是按它做精确匹配的，必须先归一化。
    RegisterRequest req;
    REQUIRE(RegisterProtocol::parseRegisterRequest(
        "{\"nickname\":\"a\",\"password\":\"c\",\"phone\":\"d\","
        "\"company\":\"  \\t明远科技有限公司\\r\\n \"}", req));
    CHECK(req.company == "明远科技有限公司");
}

TEST_CASE("R8 parseRegisterRequest 忽略老报文里的 username 与 smscode")
{
    //这两个字段都已不再由客户端决定：登录账号由服务端按用户ID自动生成，
    //短信验证码只服务于找回密码。老客户端仍把它们发上来时必须被忽略，
    //而不是把整个请求判为非法。
    RegisterRequest req;
    REQUIRE(RegisterProtocol::parseRegisterRequest(
        "{\"nickname\":\"林知夏\",\"username\":\"iwantthisname\",\"password\":\"abcdef0123\","
        "\"phone\":\"13917043329\",\"company\":\"明远科技有限公司\",\"smscode\":\"729413\"}", req));
    CHECK(req.company == "明远科技有限公司");
    CHECK(req.nickname == "林知夏");
}

TEST_CASE("R3 parseSendSmsCodeRequest 解析与 scene 缺省")
{
    std::string phone;
    int scene = -1;
    REQUIRE(RegisterProtocol::parseSendSmsCodeRequest(
        "{\"phone\":\"13917043329\",\"scene\":2}", phone, scene));
    CHECK(phone == "13917043329");
    CHECK(scene == 2);
    //scene 缺失时为 0
    REQUIRE(RegisterProtocol::parseSendSmsCodeRequest("{\"phone\":\"138\"}", phone, scene));
    CHECK(scene == 0);
    //phone 缺失被拒绝
    CHECK_FALSE(RegisterProtocol::parseSendSmsCodeRequest("{\"scene\":1}", phone, scene));
}

TEST_CASE("R4 parseResetPasswordRequest 解析与校验")
{
    ResetPasswordRequest req;
    REQUIRE(RegisterProtocol::parseResetPasswordRequest(
        "{\"phone\":\"138\",\"smscode\":\"729413\",\"newpassword\":\"newhash\"}", req));
    CHECK(req.phone == "138");
    CHECK(req.smscode == "729413");
    CHECK(req.newpassword == "newhash");
    //缺字段被拒绝
    CHECK_FALSE(RegisterProtocol::parseResetPasswordRequest(
        "{\"phone\":\"138\",\"smscode\":\"x\"}", req));
}

TEST_CASE("R5 buildRegisterResponse 含 userid / username / 企业归属（ID 按字符串编码）")
{
    Json::Value root;
    //username 是服务端按用户ID自动生成的账号（见 AccountName.h）
    REQUIRE(parseJson(RegisterProtocol::buildRegisterResponse(
        0, "ok", 88234589, "EMP88234589", 1, "明远科技有限公司"), root));
    CHECK(root["code"].asInt() == 0);
    //userid 与 enterpriseid 都按 v2 约定编成字符串，避免 JS 侧大整数精度丢失
    CHECK(root["userid"].asString() == "88234589");
    CHECK(root["username"].asString() == "EMP88234589");
    CHECK(root["enterpriseid"].asString() == "1");
    CHECK(root["enterprisename"].asString() == "明远科技有限公司");
}

TEST_CASE("R6 buildSimpleResponse 含 code 与 msg")
{
    Json::Value root;
    REQUIRE(parseJson(RegisterProtocol::buildSimpleResponse(105, "sms code error"), root));
    CHECK(root["code"].asInt() == 105);
    CHECK(root["msg"].asString() == "sms code error");
}

TEST_CASE("M1 generateCode 产出 6 位纯数字验证码")
{
    SmsCodeManager mgr;
    std::string code = mgr.generateCode("13917043329");
    CHECK(code.size() == 6);
    bool allDigit = true;
    for (size_t i = 0; i < code.size(); ++i)
    {
        if (code[i] < '0' || code[i] > '9')
            allDigit = false;
    }
    CHECK(allDigit);
}

TEST_CASE("M2 verifyCode 正确验证码通过")
{
    SmsCodeManager mgr;
    std::string code = mgr.generateCode("138");
    CHECK(mgr.verifyCode("138", code));
}

TEST_CASE("M3 verifyCode 一次性消费：通过后再校验失败")
{
    SmsCodeManager mgr;
    std::string code = mgr.generateCode("139");
    REQUIRE(mgr.verifyCode("139", code));
    //同一验证码已被消费，再次校验失败
    CHECK_FALSE(mgr.verifyCode("139", code));
}

TEST_CASE("M4 verifyCode 错误验证码 / 未知手机号返回 false")
{
    SmsCodeManager mgr;
    std::string code = mgr.generateCode("137");
    //构造一个与真实验证码必然不同的错误码（翻转首位数字）
    std::string wrong = code;
    wrong[0] = (code[0] == '0') ? '1' : '0';
    CHECK_FALSE(mgr.verifyCode("137", wrong));
    CHECK_FALSE(mgr.verifyCode("136", "123456"));   //该手机号未申请过验证码
}
