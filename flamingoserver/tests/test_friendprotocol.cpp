/**
 *  v2 好友关系报文编解码单元测试（Suite N），test_friendprotocol.cpp
 *  覆盖：search/add/handle 请求解析，search 应答（用户/群），三类通知，
 *       get_notifications 应答，以及好友管理（删好友/备注名/黑名单/标签）请求解析。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/FriendProtocol.h"
#include "../chatserversrc/Protocol.h"   //error_code_not_friend（应答用例断言错误码）
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

TEST_CASE("N2 parseAddFriendRequest 解析与校验")
{
    AddFriendRequestData req;
    REQUIRE(FriendProtocol::parseAddFriendRequest(
        "{\"userid\":\"100009\",\"source\":\"search\",\"verifymsg\":\"我是林知夏\"}", req));
    CHECK(req.userid == 100009);
    CHECK(req.source == "search");
    CHECK(req.verifymsg == "我是林知夏");
    //userid 缺失被拒绝；source/verifymsg 可选
    CHECK_FALSE(FriendProtocol::parseAddFriendRequest("{\"verifymsg\":\"x\"}", req));
    REQUIRE(FriendProtocol::parseAddFriendRequest("{\"userid\":\"100010\"}", req));
    CHECK(req.userid == 100010);
    CHECK(req.source == "");
}

TEST_CASE("N3 parseHandleFriendRequest 解析与校验")
{
    HandleFriendRequestData req;
    REQUIRE(FriendProtocol::parseHandleFriendRequest("{\"requestid\":\"3001\",\"action\":1}", req));
    CHECK(req.requestid == 3001);
    CHECK(req.action == 1);
    //requestid 缺失、action 缺失均被拒绝
    CHECK_FALSE(FriendProtocol::parseHandleFriendRequest("{\"action\":0}", req));
    CHECK_FALSE(FriendProtocol::parseHandleFriendRequest("{\"requestid\":\"3001\"}", req));
}

TEST_CASE("N4 buildSearchResponse 用户结果：64 位 id 为字符串、含 isfriend")
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
    u.isfriend = false;
    std::vector<SearchResultItem> items;
    items.push_back(u);

    Json::Value root;
    REQUIRE(parseJson(FriendProtocol::buildSearchResponse(0, "ok", 1, items), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["type"].asInt() == 1);
    REQUIRE(root["results"].size() == 1);
    CHECK(root["results"][0]["userid"].asString() == "100009");
    CHECK(root["results"][0]["nickname"].asString() == "苏沐辰");
    CHECK(root["results"][0]["isfriend"].asBool() == false);
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

TEST_CASE("N6 buildFriendRequestNotify 含发起方信息")
{
    FriendRequestItem item;
    item.requestid = 3001;
    item.fromuserid = 100000;
    item.fromusername = "88234561";
    item.fromnickname = "林知夏";
    item.fromfacetype = 0;
    item.verifymsg = "我是林知夏";
    item.timestamp = 1716268800000LL;

    Json::Value root;
    REQUIRE(parseJson(FriendProtocol::buildFriendRequestNotify(item), root));
    CHECK(root["requestid"].asString() == "3001");
    CHECK(root["fromuserid"].asString() == "100000");
    CHECK(root["fromnickname"].asString() == "林知夏");
    CHECK(root["verifymsg"].asString() == "我是林知夏");
    //notify 不带 status 字段
    CHECK_FALSE(root.isMember("status"));
}

TEST_CASE("N7 buildFriendRequestResultNotify / buildFriendRelationChangedNotify")
{
    Json::Value r1;
    REQUIRE(parseJson(FriendProtocol::buildFriendRequestResultNotify(3001, 100009, 1), r1));
    CHECK(r1["requestid"].asString() == "3001");
    CHECK(r1["fromuserid"].asString() == "100009");
    CHECK(r1["action"].asInt() == 1);

    Json::Value r2;
    REQUIRE(parseJson(FriendProtocol::buildFriendRelationChangedNotify(100009, 1), r2));
    CHECK(r2["userid"].asString() == "100009");
    CHECK(r2["changetype"].asInt() == 1);
}

TEST_CASE("N8 buildGetNotificationsResponse 空数组与多条 pending")
{
    //空
    Json::Value empty;
    REQUIRE(parseJson(FriendProtocol::buildGetNotificationsResponse(0, "ok", {}), empty));
    CHECK(empty["notifications"].isArray());
    CHECK(empty["notifications"].size() == 0);

    //多条
    FriendRequestItem a;
    a.requestid = 3001;
    a.fromuserid = 100000;
    a.fromnickname = "林知夏";
    a.status = 0;
    a.timestamp = 1716268800000LL;
    FriendRequestItem b;
    b.requestid = 3002;
    b.fromuserid = 100002;
    b.fromnickname = "苏沐辰";
    b.status = 0;
    b.timestamp = 1716268900000LL;
    std::vector<FriendRequestItem> items;
    items.push_back(a);
    items.push_back(b);

    Json::Value root;
    REQUIRE(parseJson(FriendProtocol::buildGetNotificationsResponse(0, "ok", items), root));
    REQUIRE(root["notifications"].size() == 2);
    CHECK(root["notifications"][0]["requestid"].asString() == "3001");
    CHECK(root["notifications"][0]["status"].asInt() == 0);
    CHECK(root["notifications"][1]["fromnickname"].asString() == "苏沐辰");
}

TEST_CASE("N9 parseDeleteFriendRequest 解析与校验")
{
    DeleteFriendRequestData req;
    REQUIRE(FriendProtocol::parseDeleteFriendRequest("{\"userid\":\"100009\"}", req));
    CHECK(req.userid == 100009);
    //userid 缺失、为空串、非正、为数字类型（协议要求字符串）均被拒绝
    CHECK_FALSE(FriendProtocol::parseDeleteFriendRequest("{}", req));
    CHECK_FALSE(FriendProtocol::parseDeleteFriendRequest("{\"userid\":\"\"}", req));
    CHECK_FALSE(FriendProtocol::parseDeleteFriendRequest("{\"userid\":\"0\"}", req));
    CHECK_FALSE(FriendProtocol::parseDeleteFriendRequest("{\"userid\":100009}", req));
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
        FriendProtocol::buildSetFriendRemarkResponse(error_code_not_friend, "not a friend", 100009),
        err));
    CHECK(err["code"].asInt() == error_code_not_friend);
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

TEST_CASE("N12 parseSetFriendTagsRequest 解析与校验")
{
    SetFriendTagsRequest req;
    REQUIRE(FriendProtocol::parseSetFriendTagsRequest(
        "{\"userid\":\"100009\",\"tags\":[\"colleague\",\"project\"]}", req));
    CHECK(req.userid == 100009);
    //tagsJson 为标签数组的紧凑JSON文本
    Json::Value tags;
    REQUIRE(parseJson(req.tagsJson, tags));
    REQUIRE(tags.size() == 2);
    CHECK(tags[0].asString() == "colleague");
    CHECK(tags[1].asString() == "project");
    //tags 缺省时 tagsJson 为空数组（表示清除全部标签）
    REQUIRE(FriendProtocol::parseSetFriendTagsRequest("{\"userid\":\"100009\"}", req));
    Json::Value empty;
    REQUIRE(parseJson(req.tagsJson, empty));
    CHECK(empty.size() == 0);
    //userid 缺失被拒绝
    SetFriendTagsRequest req2;
    CHECK_FALSE(FriendProtocol::parseSetFriendTagsRequest("{\"tags\":[\"x\"]}", req2));
}
