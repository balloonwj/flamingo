/**
 *  v2 登录与连接会话报文编解码单元测试（Suite C），test_loginprotocol.cpp
 *  覆盖：登录请求解析合法/非法、成功/失败应答构造、版本号比较；
 *       设备管理 kick_device 请求解析、get_devices 应答、device_changed 通知构造。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/LoginProtocol.h"
#include "../jsoncpp1.9.0/json.h"

//把 JSON 文本解析成 Json::Value，供断言检查应答内容
static bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("C1 parseLoginRequest 解析合法登录请求")
{
    std::string body =
        "{\"clientversion\":\"3.2.1\",\"platform\":1,"
        "\"account\":\"13917043329\",\"password\":\"abcdef0123456789\","
        "\"clienttype\":1,\"presence\":1,"
        "\"deviceid\":\"PC-9F2A1C7E\",\"devicename\":\"Lin 的工作电脑\"}";
    LoginRequest req;
    REQUIRE(LoginProtocol::parseLoginRequest(body, req));
    CHECK(req.clientversion == "3.2.1");
    CHECK(req.account == "13917043329");
    CHECK(req.password == "abcdef0123456789");
    CHECK(req.clienttype == 1);
    CHECK(req.presence == 1);
    CHECK(req.deviceid == "PC-9F2A1C7E");
}

TEST_CASE("C2 parseLoginRequest 拒绝非法登录请求")
{
    LoginRequest req;
    //缺少必填字段 account
    CHECK_FALSE(LoginProtocol::parseLoginRequest(
        "{\"clientversion\":\"3.2.1\",\"password\":\"x\",\"clienttype\":1}", req));
    //clienttype 类型错误（应为整数，给了字符串）
    CHECK_FALSE(LoginProtocol::parseLoginRequest(
        "{\"clientversion\":\"3.2.1\",\"account\":\"a\",\"password\":\"x\",\"clienttype\":\"1\"}", req));
    //password 为空串
    CHECK_FALSE(LoginProtocol::parseLoginRequest(
        "{\"clientversion\":\"3.2.1\",\"account\":\"a\",\"password\":\"\",\"clienttype\":1}", req));
    //JSON 本身非法
    CHECK_FALSE(LoginProtocol::parseLoginRequest("not a json", req));
}

TEST_CASE("C3 buildLoginSuccessResponse 含 code/userid/profile")
{
    LoginProfile p;
    p.usertype = 0;
    p.username = "13917043329";
    p.nickname = "zhangyl";
    p.facetype = 0;
    p.customface = "";
    p.gender = 0;
    p.birthday = 19891208;
    p.signature = "hi";
    p.address = "上海";
    p.phonenumber = "138";
    p.mail = "a@b.c";
    p.enterpriseid = 0;

    std::string json = LoginProtocol::buildLoginSuccessResponse(8, p, 1716268800000LL);
    Json::Value root;
    REQUIRE(parseJson(json, root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");
    //64 位 userid 应以字符串形式下发
    CHECK(root["userid"].asString() == "8");
    CHECK(root["servertime"].asInt64() == 1716268800000LL);
    CHECK(root["profile"]["usertype"].asInt() == 0);
    CHECK(root["profile"]["nickname"].asString() == "zhangyl");
    CHECK(root["profile"]["enterpriseid"].asString() == "0");
}

TEST_CASE("C4 buildLoginFailResponse 含 code 与 msg")
{
    std::string json = LoginProtocol::buildLoginFailResponse(103, "wrong password");
    Json::Value root;
    REQUIRE(parseJson(json, root));
    CHECK(root["code"].asInt() == 103);
    CHECK(root["msg"].asString() == "wrong password");
}

TEST_CASE("C5 isVersionAtLeast 客户端版本号比较")
{
    CHECK(LoginProtocol::isVersionAtLeast("3.2.1", "3.0.0"));   //高于
    CHECK(LoginProtocol::isVersionAtLeast("3.0.0", "3.0.0"));   //相等
    CHECK(LoginProtocol::isVersionAtLeast("3.2", "3.2.0"));     //段数不足，补 0 后相等
    CHECK_FALSE(LoginProtocol::isVersionAtLeast("2.9.9", "3.0.0")); //主版本低
    CHECK_FALSE(LoginProtocol::isVersionAtLeast("3.0.0", "3.0.1")); //修订号低
}

TEST_CASE("C6 设备管理报文编解码")
{
    //parseKickDeviceRequest：clienttype 合法 / 缺失 / 非正
    int clienttype = 0;
    REQUIRE(LoginProtocol::parseKickDeviceRequest("{\"clienttype\":2}", clienttype));
    CHECK(clienttype == 2);
    CHECK_FALSE(LoginProtocol::parseKickDeviceRequest("{}", clienttype));
    CHECK_FALSE(LoginProtocol::parseKickDeviceRequest("{\"clienttype\":0}", clienttype));

    //buildGetDevicesResponse：devices 数组逐字段
    DeviceInfo d1;
    d1.clienttype = 1;
    d1.status     = 1;
    d1.iscurrent  = true;
    DeviceInfo d2;
    d2.clienttype = 2;
    d2.status     = 1;
    d2.iscurrent  = false;
    std::vector<DeviceInfo> devices;
    devices.push_back(d1);
    devices.push_back(d2);
    Json::Value root;
    REQUIRE(parseJson(LoginProtocol::buildGetDevicesResponse(0, "ok", devices), root));
    CHECK(root["code"].asInt() == 0);
    REQUIRE(root["devices"].size() == 2);
    CHECK(root["devices"][0]["clienttype"].asInt() == 1);
    CHECK(root["devices"][0]["iscurrent"].asBool() == true);
    CHECK(root["devices"][1]["clienttype"].asInt() == 2);
    CHECK(root["devices"][1]["iscurrent"].asBool() == false);

    //buildDeviceChangedNotify
    Json::Value notify;
    REQUIRE(parseJson(LoginProtocol::buildDeviceChangedNotify(2, 3), notify));
    CHECK(notify["changetype"].asInt() == 2);
    CHECK(notify["clienttype"].asInt() == 3);
}
