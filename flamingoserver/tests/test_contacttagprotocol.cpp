/**
 *  v2「联系人标签」报文编解码单元测试（Suite CTP），test_contacttagprotocol.cpp
 *
 *  覆盖 cmd_set_contact_tags 的请求解析（合法与各类畸形输入）与应答构造
 *  （成功与失败两个分支）。
 *
 *  标签在 cmd_get_contacts 应答里的编码（每个 contacts[] 元素上的 tags 数组）
 *  另见 test_contactsprotocol.cpp 的 G8 系列用例；归一与落库行为见
 *  test_contacttagmanager.cpp 的 CTM 系列用例。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/ContactTagProtocol.h"
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
// CTP1 ~ CTP5 —— cmd_set_contact_tags 请求解析
// =====================================================================

TEST_CASE("CTP1 解析合法的设置标签请求：多条标签按原顺序收下")
{
    SetContactTagsRequest req;

    REQUIRE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":[\"设计\",\"外包\"]}", req));
    CHECK(req.userid == 5081);
    REQUIRE(req.tags.size() == 2);
    //顺序即用户期望的显示顺序，解析层不得重排
    CHECK(req.tags[0] == "设计");
    CHECK(req.tags[1] == "外包");
}

TEST_CASE("CTP2 空数组是合法请求，表示清空该联系人的全部标签")
{
    SetContactTagsRequest req;

    REQUIRE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":[]}", req));
    CHECK(req.userid == 5081);
    CHECK(req.tags.empty());
}

TEST_CASE("CTP3 userid 必须是可转成正整数的字符串")
{
    SetContactTagsRequest req;

    //缺字段
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest("{\"tags\":[\"设计\"]}", req));
    //数字而非字符串：64 位 ID 一律以字符串编码进 JSON，写成数字视为非法
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":5081,\"tags\":[\"设计\"]}", req));
    //非数字字符串
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"abc\",\"tags\":[\"设计\"]}", req));
    //0 与负数都不是合法 userid
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"0\",\"tags\":[\"设计\"]}", req));
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"-5\",\"tags\":[\"设计\"]}", req));
}

TEST_CASE("CTP4 tags 必填且必须是字符串数组")
{
    SetContactTagsRequest req;

    //缺字段一律判非法，<u>不</u>按空数组处理：一个漏发字段的客户端会把用户
    //贴好的标签整份清空，这个后果太重，不该靠默认值兜。
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest("{\"userid\":\"5081\"}", req));
    //类型不对：字符串、对象、数字都不是数组
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":\"设计\"}", req));
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":{\"a\":1}}", req));
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":3}", req));
    //null 也不行
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":null}", req));
}

TEST_CASE("CTP5 数组里混有非字符串元素时整个请求判非法，且出参被清空")
{
    SetContactTagsRequest req;

    //混进数字：多半是客户端自己的序列化出了问题，静默丢弃那一条只会让人更晚发现
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":[\"设计\",7]}", req));
    //解析失败时出参不得留下半份数据，免得调用方误用
    CHECK(req.tags.empty());

    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":[\"设计\",{\"name\":\"外包\"}]}", req));
    CHECK(req.tags.empty());
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":[null]}", req));
    CHECK(req.tags.empty());
}

TEST_CASE("CTP6 非法 JSON 与非对象一律拒绝")
{
    SetContactTagsRequest req;

    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest("", req));
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest("not json", req));
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest("{", req));
    //顶层是数组而非对象
    CHECK_FALSE(ContactTagProtocol::parseSetContactTagsRequest("[\"设计\"]", req));
}

TEST_CASE("CTP7 解析层不做归一：首尾空白与重复标签原样收下")
{
    //归一（去空白、丢空串、忽略大小写去重）统一由存储层 ContactTagManager 做。
    //两层各归一一次，日后改规则必定漏改一处。
    SetContactTagsRequest req;

    REQUIRE(ContactTagProtocol::parseSetContactTagsRequest(
        "{\"userid\":\"5081\",\"tags\":[\"  设计  \",\"UI\",\"ui\",\"\"]}", req));
    REQUIRE(req.tags.size() == 4);
    CHECK(req.tags[0] == "  设计  ");
    CHECK(req.tags[1] == "UI");
    CHECK(req.tags[2] == "ui");
    CHECK(req.tags[3].empty());
}

// =====================================================================
// CTP8 ~ CTP10 —— cmd_set_contact_tags 应答构造
// =====================================================================

TEST_CASE("CTP8 成功应答字段齐备，userid 以字符串回带，tags 为数组")
{
    std::vector<std::string> tags;
    tags.push_back("设计");
    tags.push_back("外包");

    const std::string text = ContactTagProtocol::buildSetContactTagsResponse(
        error_code_ok, "ok", 5081, tags);

    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == error_code_ok);
    CHECK(root["msg"].asString() == "ok");
    //64 位 ID 一律以字符串编码，客户端才不会在 JS 侧丢精度
    REQUIRE(root["userid"].isString());
    CHECK(root["userid"].asString() == "5081");

    REQUIRE(root["tags"].isArray());
    REQUIRE(root["tags"].size() == 2);
    CHECK(root["tags"][0].asString() == "设计");
    CHECK(root["tags"][1].asString() == "外包");
}

TEST_CASE("CTP9 空列表写成空数组，字段仍在")
{
    std::vector<std::string> empty;
    const std::string text = ContactTagProtocol::buildSetContactTagsResponse(
        error_code_ok, "ok", 5081, empty);

    Json::Value root;
    REQUIRE(parseJson(text, root));
    //不做"空则省略字段"的优化：客户端只需一条解析路径
    REQUIRE(root.isMember("tags"));
    REQUIRE(root["tags"].isArray());
    CHECK(root["tags"].size() == 0);
}

TEST_CASE("CTP10 失败应答同样回带 userid 与服务端当前存着的列表")
{
    //客户端是乐观更新：界面上已经是用户刚编辑的样子，要靠这份列表回滚
    std::vector<std::string> current;
    current.push_back("设计");

    const std::string text = ContactTagProtocol::buildSetContactTagsResponse(
        error_code_tag_limit, "tag count limit reached", 100009, current);

    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == error_code_tag_limit);
    CHECK(root["userid"].asString() == "100009");
    REQUIRE(root["tags"].size() == 1);
    CHECK(root["tags"][0].asString() == "设计");

    //解析都没过时 userid 取 0、列表为空，同样要能构造出合法应答
    std::vector<std::string> none;
    const std::string text2 = ContactTagProtocol::buildSetContactTagsResponse(
        error_code_bad_request, "bad request", 0, none);
    Json::Value root2;
    REQUIRE(parseJson(text2, root2));
    CHECK(root2["code"].asInt() == error_code_bad_request);
    CHECK(root2["userid"].asString() == "0");
    CHECK(root2["tags"].size() == 0);
}

TEST_CASE("CTP11 标签含引号、反斜杠与换行时应答 JSON 仍合法且内容不变")
{
    std::vector<std::string> tags;
    tags.push_back("他说\"随便\"\\ 换行\n结束 · 中文");

    const std::string text = ContactTagProtocol::buildSetContactTagsResponse(
        error_code_ok, "ok", 5081, tags);

    Json::Value root;
    REQUIRE(parseJson(text, root));
    REQUIRE(root["tags"].size() == 1);
    CHECK(root["tags"][0].asString() == "他说\"随便\"\\ 换行\n结束 · 中文");
}
