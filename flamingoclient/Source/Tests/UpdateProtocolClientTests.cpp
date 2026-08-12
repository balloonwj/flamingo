/**
 *  v2 检查客户端更新报文编解码（客户端侧）单元测试（Suite UPC），
 *  UpdateProtocolClientTests.cpp
 *
 *  覆盖：buildCheckUpdateRequest 的字段与类型；parseCheckUpdateResponse 的有更新
 *  （含超过 2GiB 的包大小不被截断、server 子对象）、无更新、强制升级；三条失败
 *  路径（非法 JSON / 不是对象 / 缺 code）与业务错误码；以及点分版本号比较 ——
 *  版本比较的用例与服务端 tests/test_loginprotocol.cpp 的 C5 逐条对齐，两端规则
 *  不一致会出现"服务端说要强制升级、客户端却认为自己够新"这种谁也说不清的状态。
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-12
 */
#include "catch_amalgamated.hpp"
#include "../net/UpdateProtocolClient.h"
//见 UpdateProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查 buildCheckUpdateRequest 的产物
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("UPC1 buildCheckUpdateRequest 字段与类型", "[update]")
{
    const std::string body = UpdateProtocolClient::buildCheckUpdateRequest(1, "3.2.1");

    Json::Value root;
    REQUIRE(parseJsonText(body, root));
    REQUIRE(root.isObject());
    //platform 必须是数字而不是字符串：服务端 parseCheckUpdateRequest 只认整型，
    //发成字符串会被判成非法请求
    CHECK(root["platform"].isIntegral());
    CHECK(root["platform"].asInt() == 1);
    CHECK(root["version"].isString());
    CHECK(root["version"].asString() == "3.2.1");
}

TEST_CASE("UPC2 parseCheckUpdateResponse 有更新时字段齐备且大包不溢出", "[update]")
{
    const std::string json =
        "{\"code\":0,\"msg\":\"ok\",\"hasupdate\":true,"
        "\"latestversion\":\"3.3.0\",\"minversion\":\"3.1.0\",\"forced\":false,"
        "\"releasenote\":\"\\u65b0\\u589e\\u7fa4\\u63a5\\u9f99\","
        "\"md5\":\"9f2c8ab1d4e5f60718293a4b5c6d7e8f\",\"size\":3221225472,"
        "\"packagename\":\"Flamingo-3.3.0.zip\",\"downloadtoken\":\"2|5|9f2c|3221225472|0|172336.abc\","
        "\"server\":{\"host\":\"192.168.1.8\",\"port\":20001,\"tls\":false}}";

    CheckUpdateResult r;
    REQUIRE(UpdateProtocolClient::parseCheckUpdateResponse(json, r));
    CHECK(r.m_code == 0);
    CHECK(r.m_hasUpdate);
    CHECK(r.m_latestVersion == "3.3.0");
    CHECK(r.m_minVersion == "3.1.0");
    CHECK_FALSE(r.m_forced);
    CHECK(r.m_md5 == "9f2c8ab1d4e5f60718293a4b5c6d7e8f");
    CHECK(r.m_packageName == "Flamingo-3.3.0.zip");
    CHECK(r.m_downloadToken == "2|5|9f2c|3221225472|0|172336.abc");
    //3GiB 超过 int32 上界，必须原样读回而不是溢出成负数
    CHECK(r.m_size == 3221225472LL);
    CHECK(r.m_host == "192.168.1.8");
    CHECK(r.m_port == 20001);
    CHECK_FALSE(r.m_tls);
    //令牌、MD5、大小齐备，可以真正进下载流程
    CHECK(r.HasUsableUpdate());
}

TEST_CASE("UPC3 parseCheckUpdateResponse 无更新", "[update]")
{
    //已发布过版本：带 latestversion，供关于页显示"当前已是最新版本 3.3.0"
    CheckUpdateResult r;
    REQUIRE(UpdateProtocolClient::parseCheckUpdateResponse(
        "{\"code\":0,\"msg\":\"ok\",\"hasupdate\":false,\"latestversion\":\"3.3.0\"}", r));
    CHECK(r.m_code == 0);
    CHECK_FALSE(r.m_hasUpdate);
    CHECK(r.m_latestVersion == "3.3.0");
    CHECK_FALSE(r.HasUsableUpdate());

    //服务器一个版本都没发布过：连 latestversion 也没有，同样不算失败
    CheckUpdateResult r2;
    REQUIRE(UpdateProtocolClient::parseCheckUpdateResponse(
        "{\"code\":0,\"msg\":\"ok\",\"hasupdate\":false}", r2));
    CHECK_FALSE(r2.m_hasUpdate);
    CHECK(r2.m_latestVersion.empty());
}

TEST_CASE("UPC4 parseCheckUpdateResponse 强制升级", "[update]")
{
    const std::string json =
        "{\"code\":0,\"msg\":\"ok\",\"hasupdate\":true,\"latestversion\":\"3.3.0\","
        "\"minversion\":\"3.3.0\",\"forced\":true,\"releasenote\":\"x\","
        "\"md5\":\"9f2c8ab1d4e5f60718293a4b5c6d7e8f\",\"size\":100,"
        "\"packagename\":\"a.zip\",\"downloadtoken\":\"tok\","
        "\"server\":{\"host\":\"127.0.0.1\",\"port\":20001,\"tls\":true}}";

    CheckUpdateResult r;
    REQUIRE(UpdateProtocolClient::parseCheckUpdateResponse(json, r));
    CHECK(r.m_forced);
    CHECK(r.m_tls);
    //强制升级时其余字段同样要齐备，否则界面拦下了登录却不知道该下什么包
    CHECK(r.HasUsableUpdate());
}

TEST_CASE("UPC5 parseCheckUpdateResponse 失败路径", "[update]")
{
    CheckUpdateResult r;

    //报文本身不可解析 → 返回 false
    CHECK_FALSE(UpdateProtocolClient::parseCheckUpdateResponse("{\"code\":", r));
    CHECK_FALSE(UpdateProtocolClient::parseCheckUpdateResponse("[1,2,3]", r));
    CHECK_FALSE(UpdateProtocolClient::parseCheckUpdateResponse("", r));
    //缺 code：与其当成"成功且无更新"静悄悄放过，不如判成报文异常
    CHECK_FALSE(UpdateProtocolClient::parseCheckUpdateResponse("{\"hasupdate\":true}", r));

    //业务错误码：解析成功，错误码带回，且不会被误判成有更新
    CheckUpdateResult err;
    REQUIRE(UpdateProtocolClient::parseCheckUpdateResponse(
        "{\"code\":3,\"msg\":\"bad request\"}", err));
    CHECK(err.m_code == 3);
    CHECK(err.m_msg == "bad request");
    CHECK_FALSE(err.m_hasUpdate);
    CHECK_FALSE(err.HasUsableUpdate());

    //有更新但令牌缺失（服务端记录不完整）：不能进下载流程
    CheckUpdateResult broken;
    REQUIRE(UpdateProtocolClient::parseCheckUpdateResponse(
        "{\"code\":0,\"hasupdate\":true,\"md5\":\"abc\",\"size\":10}", broken));
    CHECK(broken.m_hasUpdate);
    CHECK_FALSE(broken.HasUsableUpdate());
}

TEST_CASE("UPC6 isVersionAtLeast 与服务端规则逐条对齐", "[update]")
{
    CHECK(UpdateProtocolClient::isVersionAtLeast("3.2.1", "3.0.0"));   //高于
    CHECK(UpdateProtocolClient::isVersionAtLeast("3.0.0", "3.0.0"));   //相等
    CHECK(UpdateProtocolClient::isVersionAtLeast("3.2", "3.2.0"));     //段数不足，补 0 后相等
    CHECK_FALSE(UpdateProtocolClient::isVersionAtLeast("2.9.9", "3.0.0")); //主版本低
    CHECK_FALSE(UpdateProtocolClient::isVersionAtLeast("3.0.0", "3.0.1")); //修订号低

    //多位数版本段：逐段按数值比较而非字典序，否则 "3.10.0" 会被判成小于 "3.9.0"。
    //自动更新上线后版本号迟早会跨过 9，这两条是防回归。
    CHECK(UpdateProtocolClient::isVersionAtLeast("3.10.0", "3.9.0"));
    CHECK_FALSE(UpdateProtocolClient::isVersionAtLeast("3.9.0", "3.10.0"));
}
