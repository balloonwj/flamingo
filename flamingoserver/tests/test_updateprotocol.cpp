/**
 *  v2 检查客户端更新报文编解码单元测试（Suite CU），test_updateprotocol.cpp
 *
 *  覆盖 cmd_check_update(1905) 的四条编解码路径：
 *    - 请求解析：合法请求、platform 缺省回落、各类非法请求；
 *    - 应答构造：有更新（含超过 2GiB 的包大小）、无更新、强制升级、失败。
 *
 *  本套用例是纯逻辑，不连数据库也不起网络。签令牌、选 fileserver、比版本高低
 *  分别由会话层、FileStoreManager、LoginProtocol::isVersionAtLeast 负责，各有
 *  自己的用例，这里只验报文本身。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "doctest.h"
#include "../chatserversrc/UpdateProtocol.h"
#include "../chatserversrc/Protocol.h"
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

//构造一份字段齐备的版本信息，供应答构造的几个用例复用。
//包大小取 3GiB —— 特意超过 2^31，用来验证 64 位下发路径不被截断。
static ClientVersionInfo makeVersionInfo()
{
    ClientVersionInfo info;
    info.version     = "3.3.0";
    info.minVersion  = "3.1.0";
    info.releaseNote = "新增群接龙；修复若干问题";
    info.packageMd5  = "9f2c8ab1d4e5f60718293a4b5c6d7e8f";
    info.packageSize = 3221225472LL;
    info.packageName = "Flamingo-3.3.0.zip";
    return info;
}

TEST_CASE("CU1 parseCheckUpdateRequest 解析合法请求")
{
    //platform 显式带上
    CheckUpdateRequest req;
    REQUIRE(UpdateProtocol::parseCheckUpdateRequest(
        "{\"platform\":1,\"version\":\"3.2.1\"}", req));
    CHECK(req.platform == platform_pc);
    CHECK(req.version == "3.2.1");

    //platform 缺省：回落为 PC，不算解析失败
    CheckUpdateRequest req2;
    REQUIRE(UpdateProtocol::parseCheckUpdateRequest("{\"version\":\"3.2.1\"}", req2));
    CHECK(req2.platform == platform_pc);
    CHECK(req2.version == "3.2.1");

    //platform 取其它合法端也能解析（自动更新目前只做 PC，但报文层不该把别的端挡在外面）
    CheckUpdateRequest req3;
    REQUIRE(UpdateProtocol::parseCheckUpdateRequest(
        "{\"platform\":2,\"version\":\"1.0\"}", req3));
    CHECK(req3.platform == platform_android);
}

TEST_CASE("CU2 parseCheckUpdateRequest 拒绝非法请求")
{
    CheckUpdateRequest req;

    //JSON 本身非法
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest("{\"version\":", req));
    //不是 JSON 对象
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest("[1,2,3]", req));
    //version 缺失
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest("{\"platform\":1}", req));
    //version 为空串：拿它没法比版本，与缺失同等对待
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest("{\"version\":\"\"}", req));
    //version 类型不对（数字而非字符串）
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest("{\"version\":321}", req));
    //platform 类型不对
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest(
        "{\"platform\":\"pc\",\"version\":\"3.2.1\"}", req));
    //platform 越界：0、负数、超出 enum platform_type 上界
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest(
        "{\"platform\":0,\"version\":\"3.2.1\"}", req));
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest(
        "{\"platform\":-1,\"version\":\"3.2.1\"}", req));
    CHECK_FALSE(UpdateProtocol::parseCheckUpdateRequest(
        "{\"platform\":99,\"version\":\"3.2.1\"}", req));
}

TEST_CASE("CU3 buildHasUpdateResponse 有更新时字段齐备且大包不溢出")
{
    const std::string json = UpdateProtocol::buildHasUpdateResponse(
        makeVersionInfo(), false, "2|5|9f2c|3221225472|0|1723363200000.a1b2c3",
        "file.flamingo.io", 20001, false);

    Json::Value root;
    REQUIRE(parseJson(json, root));
    CHECK(root["code"].asInt() == error_code_ok);
    CHECK(root["msg"].asString() == "ok");
    CHECK(root["hasupdate"].asBool());
    CHECK(root["latestversion"].asString() == "3.3.0");
    CHECK(root["minversion"].asString() == "3.1.0");
    CHECK_FALSE(root["forced"].asBool());
    CHECK(root["releasenote"].asString() == "新增群接龙；修复若干问题");
    CHECK(root["md5"].asString() == "9f2c8ab1d4e5f60718293a4b5c6d7e8f");
    CHECK(root["packagename"].asString() == "Flamingo-3.3.0.zip");
    CHECK(root["downloadtoken"].asString() == "2|5|9f2c|3221225472|0|1723363200000.a1b2c3");

    //3GiB 超过 int32 上界，必须原样往返，不能被截断成负数
    CHECK(root["size"].asInt64() == 3221225472LL);

    //server 子对象与 cmd_request_download 应答同构
    REQUIRE(root["server"].isObject());
    CHECK(root["server"]["host"].asString() == "file.flamingo.io");
    CHECK(root["server"]["port"].asInt() == 20001);
    CHECK_FALSE(root["server"]["tls"].asBool());
}

TEST_CASE("CU4 buildNoUpdateResponse 无更新时不带多余字段")
{
    //已发布过版本：带上 latestversion 供关于页显示"当前已是最新版本 3.3.0"
    Json::Value root;
    REQUIRE(parseJson(UpdateProtocol::buildNoUpdateResponse("3.3.0"), root));
    CHECK(root["code"].asInt() == error_code_ok);
    CHECK(root["msg"].asString() == "ok");
    CHECK_FALSE(root["hasupdate"].asBool());
    CHECK(root["latestversion"].asString() == "3.3.0");

    //有更新才有意义的字段一个都不能出现，避免客户端拿到空令牌去连 fileserver
    CHECK_FALSE(root.isMember("downloadtoken"));
    CHECK_FALSE(root.isMember("md5"));
    CHECK_FALSE(root.isMember("size"));
    CHECK_FALSE(root.isMember("packagename"));
    CHECK_FALSE(root.isMember("server"));
    CHECK_FALSE(root.isMember("forced"));

    //一个版本都没发布过：连 latestversion 也不下发
    Json::Value empty;
    REQUIRE(parseJson(UpdateProtocol::buildNoUpdateResponse(""), empty));
    CHECK_FALSE(empty["hasupdate"].asBool());
    CHECK_FALSE(empty.isMember("latestversion"));
}

TEST_CASE("CU5 buildHasUpdateResponse 强制升级")
{
    Json::Value root;
    REQUIRE(parseJson(UpdateProtocol::buildHasUpdateResponse(
        makeVersionInfo(), true, "tok", "127.0.0.1", 20001, true), root));
    CHECK(root["hasupdate"].asBool());
    CHECK(root["forced"].asBool());
    //强制升级时其余字段同样要齐备，否则客户端拦下了登录却不知道该下什么包
    CHECK(root["latestversion"].asString() == "3.3.0");
    CHECK(root["md5"].asString() == "9f2c8ab1d4e5f60718293a4b5c6d7e8f");
    CHECK(root["downloadtoken"].asString() == "tok");
    CHECK(root["server"]["tls"].asBool());
}

TEST_CASE("CU6 buildSimpleResponse 失败应答")
{
    Json::Value root;
    REQUIRE(parseJson(UpdateProtocol::buildSimpleResponse(
        error_code_bad_request, "bad request"), root));
    CHECK(root["code"].asInt() == error_code_bad_request);
    CHECK(root["msg"].asString() == "bad request");
    CHECK_FALSE(root.isMember("hasupdate"));
}
