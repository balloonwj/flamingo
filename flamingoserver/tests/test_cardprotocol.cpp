/**
 *  v2 群互动卡片报文编解码单元测试（Suite K），test_cardprotocol.cpp
 *  覆盖：vote_poll / join_relay / update_todo 请求解析，投票数据序列化，
 *       投票卡片元信息读取，待办负责人判定，三类 changed 通知构造，
 *       卡片消息 content 的当前状态合并。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/CardProtocol.h"
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

//构造一条卡片互动记录
static CardInteractionRecord makeRow(int64_t userid, const std::string& data, int64_t t)
{
    CardInteractionRecord r;
    r.userid     = userid;
    r.data       = data;
    r.createtime = t;
    return r;
}

TEST_CASE("K1 parseVotePollRequest 解析与校验")
{
    VotePollRequest req;
    REQUIRE(CardProtocol::parseVotePollRequest(
        "{\"ctype\":2,\"cid\":\"600001\",\"msgid\":\"9007199254741010\",\"options\":[0,2]}", req));
    CHECK(req.ctype == 2);
    CHECK(req.cid == 600001);
    CHECK(req.msgid == 9007199254741010LL);
    REQUIRE(req.options.size() == 2);
    CHECK(req.options[0] == 0);
    CHECK(req.options[1] == 2);
    //msgid 缺失、options 缺失/为空均被拒
    CHECK_FALSE(CardProtocol::parseVotePollRequest("{\"cid\":\"1\",\"options\":[0]}", req));
    CHECK_FALSE(CardProtocol::parseVotePollRequest("{\"cid\":\"1\",\"msgid\":\"2\"}", req));
    CHECK_FALSE(CardProtocol::parseVotePollRequest(
        "{\"cid\":\"1\",\"msgid\":\"2\",\"options\":[]}", req));
}

TEST_CASE("K2 parseJoinRelayRequest 解析与校验")
{
    JoinRelayRequest req;
    REQUIRE(CardProtocol::parseJoinRelayRequest(
        "{\"ctype\":2,\"cid\":\"600001\",\"msgid\":\"5010\",\"text\":\"me plus one\"}", req));
    CHECK(req.cid == 600001);
    CHECK(req.msgid == 5010);
    CHECK(req.text == "me plus one");
    //text 缺失或为空、msgid 缺失均被拒
    CHECK_FALSE(CardProtocol::parseJoinRelayRequest("{\"cid\":\"1\",\"msgid\":\"2\"}", req));
    CHECK_FALSE(CardProtocol::parseJoinRelayRequest(
        "{\"cid\":\"1\",\"msgid\":\"2\",\"text\":\"\"}", req));
    CHECK_FALSE(CardProtocol::parseJoinRelayRequest("{\"cid\":\"1\",\"text\":\"x\"}", req));
}

TEST_CASE("K3 parseUpdateTodoRequest 解析与校验")
{
    UpdateTodoRequest req;
    REQUIRE(CardProtocol::parseUpdateTodoRequest(
        "{\"ctype\":2,\"cid\":\"600001\",\"msgid\":\"5020\",\"done\":1}", req));
    CHECK(req.msgid == 5020);
    CHECK(req.done == 1);
    REQUIRE(CardProtocol::parseUpdateTodoRequest(
        "{\"cid\":\"1\",\"msgid\":\"2\",\"done\":0}", req));
    CHECK(req.done == 0);
    //done 缺失、msgid 缺失均被拒
    CHECK_FALSE(CardProtocol::parseUpdateTodoRequest("{\"cid\":\"1\",\"msgid\":\"2\"}", req));
    CHECK_FALSE(CardProtocol::parseUpdateTodoRequest("{\"cid\":\"1\",\"done\":1}", req));
}

TEST_CASE("K4 buildVoteData 与 getPollMeta")
{
    std::vector<int> opts;
    opts.push_back(0);
    opts.push_back(2);
    Json::Value arr;
    REQUIRE(parseJson(CardProtocol::buildVoteData(opts), arr));
    REQUIRE(arr.size() == 2);
    CHECK(arr[0].asInt() == 0);
    CHECK(arr[1].asInt() == 2);

    //getPollMeta：从投票卡片定义取选项数 / 多选 / 截止时间
    int optioncount = 0;
    int multi = -1;
    int64_t expiretime = -1;
    REQUIRE(CardProtocol::getPollMeta(
        "{\"title\":\"q\",\"options\":[\"a\",\"b\",\"c\"],\"multi\":true,\"expiretime\":1716355200000}",
        optioncount, multi, expiretime));
    CHECK(optioncount == 3);
    CHECK(multi == 1);
    CHECK(expiretime == 1716355200000LL);
    //无 options 数组的非法投票定义被拒
    CHECK_FALSE(CardProtocol::getPollMeta("{\"title\":\"q\"}", optioncount, multi, expiretime));
}

TEST_CASE("K5 buildPollChangedNotify 票数聚合")
{
    std::vector<CardInteractionRecord> rows;
    rows.push_back(makeRow(100, "[0,1]", 1));
    rows.push_back(makeRow(101, "[1]", 2));
    rows.push_back(makeRow(102, "[1,2]", 3));

    Json::Value root;
    REQUIRE(parseJson(CardProtocol::buildPollChangedNotify(9007199254741010LL, rows), root));
    CHECK(root["msgid"].asString() == "9007199254741010");
    //选项0：1票；选项1：3票；选项2：1票
    CHECK(root["votes"]["0"].asInt() == 1);
    CHECK(root["votes"]["1"].asInt() == 3);
    CHECK(root["votes"]["2"].asInt() == 1);
    //totalcount 为去重投票人数
    CHECK(root["totalcount"].asInt() == 3);
}

TEST_CASE("K6 buildRelayChangedNotify 接龙列表")
{
    std::vector<CardInteractionRecord> rows;
    rows.push_back(makeRow(100, "Lin plus one", 1));
    rows.push_back(makeRow(101, "Su solo", 2));

    Json::Value root;
    REQUIRE(parseJson(CardProtocol::buildRelayChangedNotify(5010, rows), root));
    CHECK(root["msgid"].asString() == "5010");
    REQUIRE(root["items"].size() == 2);
    CHECK(root["items"][0]["userid"].asString() == "100");
    CHECK(root["items"][0]["text"].asString() == "Lin plus one");
    CHECK(root["items"][1]["userid"].asString() == "101");
}

TEST_CASE("K7 buildTodoChangedNotify 已完成名单")
{
    std::vector<CardInteractionRecord> rows;
    rows.push_back(makeRow(100, "1", 1));
    rows.push_back(makeRow(102, "1", 2));

    Json::Value root;
    REQUIRE(parseJson(CardProtocol::buildTodoChangedNotify(5020, rows), root));
    CHECK(root["msgid"].asString() == "5020");
    REQUIRE(root["doneusers"].size() == 2);
    CHECK(root["doneusers"][0].asString() == "100");
    CHECK(root["doneusers"][1].asString() == "102");
}

TEST_CASE("K8 augmentCardContent 合并卡片当前状态")
{
    std::vector<CardInteractionRecord> pollRows;
    pollRows.push_back(makeRow(100, "[0]", 1));
    pollRows.push_back(makeRow(101, "[0]", 2));
    //投票卡片：content 并入 votes / totalcount
    Json::Value poll;
    REQUIRE(parseJson(CardProtocol::augmentCardContent(
        10, "{\"title\":\"q\",\"options\":[\"a\",\"b\"]}", pollRows), poll));
    CHECK(poll["title"].asString() == "q");
    CHECK(poll["votes"]["0"].asInt() == 2);
    CHECK(poll["totalcount"].asInt() == 2);

    //接龙卡片：content 并入 items
    std::vector<CardInteractionRecord> relayRows;
    relayRows.push_back(makeRow(100, "entry one", 1));
    Json::Value relay;
    REQUIRE(parseJson(CardProtocol::augmentCardContent(
        11, "{\"title\":\"signup\"}", relayRows), relay));
    REQUIRE(relay["items"].size() == 1);
    CHECK(relay["items"][0]["text"].asString() == "entry one");

    //待办卡片：content 并入 doneusers
    std::vector<CardInteractionRecord> todoRows;
    todoRows.push_back(makeRow(100, "1", 1));
    Json::Value todo;
    REQUIRE(parseJson(CardProtocol::augmentCardContent(
        12, "{\"title\":\"review\"}", todoRows), todo));
    REQUIRE(todo["doneusers"].size() == 1);
    CHECK(todo["doneusers"][0].asString() == "100");
}

TEST_CASE("K9 isTodoAssignee 待办负责人判定")
{
    std::string content = "{\"title\":\"review\",\"assignees\":[\"100000\",\"100002\"]}";
    CHECK(CardProtocol::isTodoAssignee(content, 100000));
    CHECK(CardProtocol::isTodoAssignee(content, 100002));
    CHECK_FALSE(CardProtocol::isTodoAssignee(content, 100009));
    //无 assignees 字段时任何人都不是负责人
    CHECK_FALSE(CardProtocol::isTodoAssignee("{\"title\":\"x\"}", 100000));
}
