/**
 *  v2 联系人报文编解码单元测试（Suite N），test_friendprotocol.cpp
 *  覆盖：cmd_search 请求解析与应答（用户/群），cmd_set_friend_remark 请求解析与应答，
 *       cmd_set_blacklist 请求解析。
 *
 *  好友关系子系统（加好友 / 处理申请 / 删好友 / 好友标签 / 消息盒子）已于 2026-08-05
 *  在企业版中删除，对应的 N2 / N3 / N6 / N7 / N8 / N9 / N12 用例随之移除，
 *  编号留空不再复用，免得日后看着编号不连续以为漏跑了。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/FriendProtocol.h"
#include "../chatserversrc/Protocol.h"   //error_code_not_found（应答用例断言错误码）
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

TEST_CASE("N1 parseSearchRequest 解析关键字与类型")
{
    std::string keyword;
    int type = -1;
    REQUIRE(FriendProtocol::parseSearchRequest("{\"keyword\":\"72100\",\"type\":2}", keyword, type));
    CHECK(keyword == "72100");
    CHECK(type == 2);
    //type 缺失默认为 1
    REQUIRE(FriendProtocol::parseSearchRequest("{\"keyword\":\"abc\"}", keyword, type));
    CHECK(type == 1);
    //keyword 缺失/为空被拒绝
    CHECK_FALSE(FriendProtocol::parseSearchRequest("{\"type\":1}", keyword, type));
    CHECK_FALSE(FriendProtocol::parseSearchRequest("{\"keyword\":\"\"}", keyword, type));
}

TEST_CASE("N4 buildSearchResponse 用户结果：64 位 id 为字符串")
{
    SearchResultItem u;
    u.id = 100009;
    u.username = "72100345";
    u.name = "苏沐辰";
    u.facetype = 0;
    u.gender = 1;
    u.signature = "今天也要好好生活";
    u.address = "杭州";
    u.usertype = 0;
    std::vector<SearchResultItem> items;
    items.push_back(u);

    Json::Value root;
    REQUIRE(parseJson(FriendProtocol::buildSearchResponse(0, "ok", 1, items), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["type"].asInt() == 1);
    REQUIRE(root["results"].size() == 1);
    CHECK(root["results"][0]["userid"].asString() == "100009");
    CHECK(root["results"][0]["nickname"].asString() == "苏沐辰");
    //isfriend 字段随好友关系子系统一并去掉，应答里不该再出现
    CHECK_FALSE(root["results"][0].isMember("isfriend"));
}

TEST_CASE("N5 buildSearchResponse 群结果")
{
    SearchResultItem g;
    g.id = 268435457;
    g.name = "设计组 · 周会";
    g.facetype = 0;
    g.membercount = 12;
    g.ownerid = 100007;
    std::vector<SearchResultItem> items;
    items.push_back(g);

    Json::Value root;
    REQUIRE(parseJson(FriendProtocol::buildSearchResponse(0, "ok", 2, items), root));
    CHECK(root["type"].asInt() == 2);
    REQUIRE(root["results"].size() == 1);
    CHECK(root["results"][0]["groupid"].asString() == "268435457");
    CHECK(root["results"][0]["name"].asString() == "设计组 · 周会");
    CHECK(root["results"][0]["membercount"].asInt() == 12);
    CHECK(root["results"][0]["ownerid"].asString() == "100007");
}

TEST_CASE("N10 parseSetFriendRemarkRequest 解析与校验")
{
    SetFriendRemarkRequestData req;
    REQUIRE(FriendProtocol::parseSetFriendRemarkRequest(
        "{\"userid\":\"100009\",\"remark\":\"产品组同事\"}", req));
    CHECK(req.userid == 100009);
    CHECK(req.remark == "产品组同事");
    //remark 缺省或为空串均允许，按"清除备注"处理
    REQUIRE(FriendProtocol::parseSetFriendRemarkRequest("{\"userid\":\"100009\"}", req));
    CHECK(req.remark == "");
    REQUIRE(FriendProtocol::parseSetFriendRemarkRequest("{\"userid\":\"100009\",\"remark\":\"\"}", req));
    CHECK(req.remark == "");
    //userid 缺失被拒绝
    CHECK_FALSE(FriendProtocol::parseSetFriendRemarkRequest("{\"remark\":\"x\"}", req));
}

TEST_CASE("N10b buildSetFriendRemarkResponse 回带 userid 且为字符串")
{
    //客户端改备注走乐观更新（先改界面再发请求），失败要把改动退回去。用户连着
    //改几个人时会有多条请求在途，应答不回带 userid 就分不清是哪一条失败、会退错人。
    //口径与 cmd_set_favorite 原样回带 ctype/cid/favorite 一致。
    Json::Value ok;
    REQUIRE(parseJson(FriendProtocol::buildSetFriendRemarkResponse(0, "ok", 100009), ok));
    CHECK(ok["code"].asInt() == 0);
    CHECK(ok["msg"].asString() == "ok");
    //64 位 ID 一律以字符串编码，不能退化成数字
    REQUIRE(ok["userid"].isString());
    CHECK(ok["userid"].asString() == "100009");

    //失败应答同样要回带，否则失败这条恰恰是最需要对号入座的
    Json::Value err;
    REQUIRE(parseJson(
        FriendProtocol::buildSetFriendRemarkResponse(error_code_not_found, "target user not found", 100009),
        err));
    CHECK(err["code"].asInt() == error_code_not_found);
    CHECK(err["userid"].asString() == "100009");
}

TEST_CASE("N11 parseSetBlacklistRequest 解析与校验")
{
    SetBlacklistRequestData req;
    REQUIRE(FriendProtocol::parseSetBlacklistRequest("{\"userid\":\"100009\",\"action\":1}", req));
    CHECK(req.userid == 100009);
    CHECK(req.action == 1);
    REQUIRE(FriendProtocol::parseSetBlacklistRequest("{\"userid\":\"100009\",\"action\":0}", req));
    CHECK(req.action == 0);
    //userid 缺失、action 缺失均被拒绝
    CHECK_FALSE(FriendProtocol::parseSetBlacklistRequest("{\"action\":1}", req));
    CHECK_FALSE(FriendProtocol::parseSetBlacklistRequest("{\"userid\":\"100009\"}", req));
}
