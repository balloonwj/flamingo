/**
 *  v2「特别关注」报文编解码单元测试，test_starredprotocol.cpp
 *  覆盖：cmd_set_starred 请求解析（合法与各类非法输入）、应答构造（成功与失败分支）。
 *
 *  特别关注名单在 cmd_get_contacts 应答里的编码（starred 数组）另见
 *  test_contactsprotocol.cpp 的 STA 系列用例。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include "doctest.h"
#include "../chatserversrc/StarredProtocol.h"
#include "../chatserversrc/Protocol.h"   //error_code
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

// =====================================================================
// STP1 ~ STP4 —— cmd_set_starred 请求解析
// =====================================================================

TEST_CASE("STP1 解析合法的特别关注请求：标记与取消两个方向")
{
    SetStarredRequest req;

    REQUIRE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"5081\",\"starred\":1}", req));
    CHECK(req.target == 5081);
    CHECK(req.starred == 1);

    REQUIRE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"100009\",\"starred\":0}", req));
    CHECK(req.target == 100009);
    CHECK(req.starred == 0);
}

TEST_CASE("STP2 target 必须是可转成正整数的字符串")
{
    SetStarredRequest req;

    //缺字段
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest("{\"starred\":1}", req));
    //数字而非字符串：64 位 ID 一律以字符串编码进 JSON，写成数字视为非法
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":5081,\"starred\":1}", req));
    //非数字字符串
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"abc\",\"starred\":1}", req));
    //0 与负数都不是合法 userid
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"0\",\"starred\":1}", req));
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"-5\",\"starred\":1}", req));
}

TEST_CASE("STP3 starred 只认 0 与 1")
{
    SetStarredRequest req;

    //缺字段
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest("{\"target\":\"5081\"}", req));
    //越界取值不得被稀里糊涂当成"标记"处理
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"5081\",\"starred\":2}", req));
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"5081\",\"starred\":-1}", req));
    //类型不对
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest(
        "{\"target\":\"5081\",\"starred\":\"1\"}", req));
}

TEST_CASE("STP4 非法 JSON 与非对象一律拒绝")
{
    SetStarredRequest req;

    CHECK_FALSE(StarredProtocol::parseSetStarredRequest("", req));
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest("not json", req));
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest("{", req));
    //顶层是数组而非对象
    CHECK_FALSE(StarredProtocol::parseSetStarredRequest("[1,2,3]", req));
}

// =====================================================================
// STP5 ~ STP6 —— cmd_set_starred 应答构造
// =====================================================================

TEST_CASE("STP5 成功应答字段齐备，target 以字符串回带")
{
    const std::string text =
        StarredProtocol::buildSetStarredResponse(error_code_ok, "ok", 5081, 1);

    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == error_code_ok);
    CHECK(root["msg"].asString() == "ok");
    //64 位 ID 一律以字符串编码，客户端才不会在 JS 侧丢精度
    REQUIRE(root["target"].isString());
    CHECK(root["target"].asString() == "5081");
    CHECK(root["starred"].asInt() == 1);
}

TEST_CASE("STP6 失败应答同样回带 target 与 starred")
{
    //客户端是乐观更新：多条请求在途时，不回带这两项就分不清是哪一条失败、回滚会退错人
    const std::string text = StarredProtocol::buildSetStarredResponse(
        error_code_starred_limit, "starred count limit reached", 100009, 1);

    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == error_code_starred_limit);
    CHECK(root["target"].asString() == "100009");
    CHECK(root["starred"].asInt() == 1);

    //取消方向失败时同样带回执
    const std::string text2 = StarredProtocol::buildSetStarredResponse(
        error_code_bad_request, "bad request", 0, 0);
    Json::Value root2;
    REQUIRE(parseJson(text2, root2));
    CHECK(root2["code"].asInt() == error_code_bad_request);
    CHECK(root2["target"].asString() == "0");
    CHECK(root2["starred"].asInt() == 0);
}

TEST_CASE("STP7 关注人数上限是具名常量且取值合理")
{
    //上限写成裸数字会散落在校验、日志、文档三处，改一处漏两处
    CHECK(kMaxStarredCount == 50);
    //特别关注的意义就是"少数几个人"：上限不该大到形同虚设
    CHECK(kMaxStarredCount > 0);
    CHECK(kMaxStarredCount <= 100);
}
