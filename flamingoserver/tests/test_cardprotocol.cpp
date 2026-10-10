/**
 *  v2 群互动卡片报文编解码单元测试（Suite K），test_cardprotocol.cpp
 *  覆盖：vote_poll / join_relay / update_todo 请求解析，投票数据序列化，
 *       投票卡片元信息读取，待办负责人判定，三类 changed 通知构造，
 *       卡片消息 content 的当前状态合并。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/CardProtocol.h"
#include "../chatserversrc/Protocol.h"   //K10~K12 用到的 message_type_* 与 error_code_*
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
    REQUIRE(parseJson(CardProtocol::buildRelayChangedNotify(conversation_group, 600001, 5010, rows, RelayStatus()),
                      root));
    CHECK(root["msgid"].asString() == "5010");
    REQUIRE(root["items"].size() == 2);
    CHECK(root["items"][0]["userid"].asString() == "100");
    CHECK(root["items"][0]["text"].asString() == "Lin plus one");
    CHECK(root["items"][1]["userid"].asString() == "101");

    //推送带上卡片所在会话，客户端据此直接定位窗口；64 位 ID 按字符串编码
    CHECK(root["ctype"].asInt() == conversation_group);
    REQUIRE(root["cid"].isString());
    CHECK(root["cid"].asString() == "600001");
    CHECK(root["msgid"].isString());

    //没有任何互动时 items 是空数组而不是 null，客户端可以直接遍历
    std::vector<CardInteractionRecord> none;
    REQUIRE(parseJson(CardProtocol::buildRelayChangedNotify(conversation_group, 600001, 5010, none, RelayStatus()),
                      root));
    CHECK(root["items"].isArray());
    CHECK(root["items"].size() == 0);
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

//把同一段文本重复 count 次拼起来，用于构造长度边界的接龙文本
static std::string repeatText(const std::string& unit, size_t count)
{
    std::string s;
    for (size_t i = 0; i < count; ++i)
        s += unit;
    return s;
}

//一个 4 字节的 UTF-8 字符（U+1F600，笑脸表情），按字节写出，不依赖源文件编码
static const char* const kEmojiUtf8 = "\xF0\x9F\x98\x80";

TEST_CASE("K10 checkCardTarget 判断卡片能否用于本次互动")
{
    //a. 接龙卡片、未撤回、本次互动要求接龙
    CHECK(CardProtocol::checkCardTarget(message_type_relay, false, message_type_relay)
          == error_code_ok);
    //b. 卡片已撤回
    CHECK(CardProtocol::checkCardTarget(message_type_relay, true, message_type_relay)
          == error_code_message_not_exist);
    //c. 类型不符：拿投票卡片来参与接龙
    CHECK(CardProtocol::checkCardTarget(message_type_poll, false, message_type_relay)
          == error_code_bad_request);
    //d. 已撤回且类型也不符：撤回的判断优先
    CHECK(CardProtocol::checkCardTarget(message_type_poll, true, message_type_relay)
          == error_code_message_not_exist);
    //e. 投票与待办两种互动同样适用
    CHECK(CardProtocol::checkCardTarget(message_type_poll, false, message_type_poll)
          == error_code_ok);
    CHECK(CardProtocol::checkCardTarget(message_type_poll, true, message_type_poll)
          == error_code_message_not_exist);
    CHECK(CardProtocol::checkCardTarget(message_type_todo, false, message_type_todo)
          == error_code_ok);
    CHECK(CardProtocol::checkCardTarget(message_type_todo, true, message_type_todo)
          == error_code_message_not_exist);
}

TEST_CASE("K11 normalizeRelayText 空白与换行的处理")
{
    std::string out;

    //a. 普通文本原样通过
    CHECK(CardProtocol::normalizeRelayText(u8"本人+1", out) == error_code_ok);
    CHECK(out == u8"本人+1");

    //b. 首尾的半角空格、制表符、全角空格都去掉
    out.clear();
    CHECK(CardProtocol::normalizeRelayText(u8"　 本人+1 \t", out) == error_code_ok);
    CHECK(out == u8"本人+1");

    //b. 首尾的回车、换行与空格一样去掉：从别处复制来的文本常带一个末尾换行
    out.clear();
    CHECK(CardProtocol::normalizeRelayText(u8"\r\n本人+1\r\n", out) == error_code_ok);
    CHECK(out == u8"本人+1");

    //c. 中间的空格保留
    out.clear();
    CHECK(CardProtocol::normalizeRelayText(u8"张三 本人", out) == error_code_ok);
    CHECK(out == u8"张三 本人");

    //d. 只有空白字符，或空串
    CHECK(CardProtocol::normalizeRelayText(u8" \t　\r\n ", out) == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayText("", out) == error_code_bad_request);

    //e. 中间含换行一律拒绝，回车换行与单个换行、单个回车都算
    CHECK(CardProtocol::normalizeRelayText(u8"张三\n本人", out) == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayText(u8"张三\r\n本人", out) == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayText(u8"张三\r本人", out) == error_code_bad_request);

    //失败时出参保持调用前的值，调用方不会拿到半成品
    out = "untouched";
    CHECK(CardProtocol::normalizeRelayText(u8"张三\n本人", out) == error_code_bad_request);
    CHECK(out == "untouched");
}

TEST_CASE("K12 normalizeRelayText 按字符计的长度上限")
{
    std::string out;

    //a. 汉字：恰好上限通过，多一个拒绝
    CHECK(CardProtocol::normalizeRelayText(repeatText(u8"字", RELAY_TEXT_MAX_CHARS), out)
          == error_code_ok);
    CHECK(CardProtocol::normalizeRelayText(repeatText(u8"字", RELAY_TEXT_MAX_CHARS + 1), out)
          == error_code_msg_too_large);

    //b. ASCII：恰好上限通过，多一个拒绝
    CHECK(CardProtocol::normalizeRelayText(repeatText("a", RELAY_TEXT_MAX_CHARS), out)
          == error_code_ok);
    CHECK(CardProtocol::normalizeRelayText(repeatText("a", RELAY_TEXT_MAX_CHARS + 1), out)
          == error_code_msg_too_large);

    //c. 4 字节的表情每个算一个字符
    CHECK(CardProtocol::normalizeRelayText(repeatText(kEmojiUtf8, RELAY_TEXT_MAX_CHARS), out)
          == error_code_ok);
    CHECK(CardProtocol::utf8CharCount(repeatText(kEmojiUtf8, RELAY_TEXT_MAX_CHARS))
          == RELAY_TEXT_MAX_CHARS);

    //d. 先去空白再计数：98 个汉字加首尾各 5 个空格，去完是 98 个字符
    const size_t kPaddedChars = RELAY_TEXT_MAX_CHARS - 2;
    const size_t kPadSpaces = 5;
    const std::string padded = repeatText(" ", kPadSpaces)
                             + repeatText(u8"字", kPaddedChars)
                             + repeatText(" ", kPadSpaces);
    out.clear();
    CHECK(CardProtocol::normalizeRelayText(padded, out) == error_code_ok);
    CHECK(CardProtocol::utf8CharCount(out) == kPaddedChars);

    //utf8CharCount 的基本取值
    CHECK(CardProtocol::utf8CharCount("") == 0);
    CHECK(CardProtocol::utf8CharCount("abc") == 3);
    CHECK(CardProtocol::utf8CharCount(u8"张三") == 2);
}

//K13~K17 共用的"服务端当前时间"，Unix 毫秒；取一个固定值，截止时间的判断才能写成确定的断言
static const int64_t kRelayNowMs = 1759400000000LL;

//一小时的毫秒数，用来构造"当前时间之后一小时"的截止时间
static const int64_t kOneHourMs = 3600LL * 1000LL;

//合法的截止时间（当前时间之后一小时）的 JSON 写法。2026-10-03 起发起接龙必须设置截止时间，
//只关心标题、说明的用例都带上它，免得被截止时间的检查挡下
static const std::string kValidDeadlineJson = std::to_string(kRelayNowMs + kOneHourMs);

//构造一份接龙卡片 content：只放调用方给出的字段。deadline 用字符串传入，
//便于构造"字符串形式的数字""小数"这类非法取值；传空串表示不带该字段
static std::string makeRelayContent(const std::string& titleJson, const std::string& noteJson,
                                    const std::string& deadlineJson)
{
    std::string s = "{";
    bool first = true;
    if (!titleJson.empty())
    {
        s += "\"title\":" + titleJson;
        first = false;
    }
    if (!noteJson.empty())
    {
        s += (first ? "" : ",");
        s += "\"note\":" + noteJson;
        first = false;
    }
    if (!deadlineJson.empty())
    {
        s += (first ? "" : ",");
        s += "\"deadline\":" + deadlineJson;
    }
    s += "}";
    return s;
}

TEST_CASE("K13 normalizeRelayContent 合法内容的规范化与字段过滤")
{
    const int64_t deadline = kRelayNowMs + kOneHourMs;
    std::string out;

    //a. 三个字段都合法：输出只有这三个字段，取值与输入一致
    REQUIRE(CardProtocol::normalizeRelayContent(
        makeRelayContent(u8"\"周五团建报名\"", u8"\"格式：姓名+是否带家属\"",
                         std::to_string(deadline)),
        kRelayNowMs, out) == error_code_ok);
    Json::Value v;
    REQUIRE(parseJson(out, v));
    CHECK(v.size() == 3);
    CHECK(v["title"].asString() == u8"周五团建报名");
    CHECK(v["note"].asString() == u8"格式：姓名+是否带家属");
    CHECK(v["deadline"].asInt64() == deadline);

    //b. 客户端自带的 items、state、closedby 与未知字段一律丢弃，防止伪造接龙名单与结束状态
    out.clear();
    REQUIRE(CardProtocol::normalizeRelayContent(
        "{\"title\":\"t\",\"items\":[{\"userid\":\"100008\",\"text\":\"fake\"}],"
        "\"state\":1,\"closedby\":\"100002\",\"whatever\":true,\"deadline\":" + kValidDeadlineJson + "}",
        kRelayNowMs, out) == error_code_ok);
    REQUIRE(parseJson(out, v));
    CHECK(v.size() == 3);
    CHECK_FALSE(v.isMember("items"));
    CHECK_FALSE(v.isMember("state"));
    CHECK_FALSE(v.isMember("closedby"));
    CHECK_FALSE(v.isMember("whatever"));

    //c. note 缺省存为空串，为 null 时同样处理（截止时间缺省时被拒，见 CRD1）
    out.clear();
    REQUIRE(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "", kValidDeadlineJson),
                                                kRelayNowMs, out) == error_code_ok);
    REQUIRE(parseJson(out, v));
    CHECK(v["note"].asString() == "");
    out.clear();
    REQUIRE(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "null", kValidDeadlineJson),
                                                kRelayNowMs, out) == error_code_ok);
    REQUIRE(parseJson(out, v));
    CHECK(v["note"].asString() == "");

    //d. 标题与说明的首尾空白都去掉（含全角空格、首尾换行）
    out.clear();
    REQUIRE(CardProtocol::normalizeRelayContent(
        makeRelayContent(u8"\"\u3000 周五团建 \"", "\"\\n  bring your family \\t\"", kValidDeadlineJson),
        kRelayNowMs, out) == error_code_ok);
    REQUIRE(parseJson(out, v));
    CHECK(v["title"].asString() == u8"周五团建");
    CHECK(v["note"].asString() == "bring your family");
}

TEST_CASE("K14 normalizeRelayContent 标题校验")
{
    std::string out;

    //a. 缺失、或不是字符串
    CHECK(CardProtocol::normalizeRelayContent("{\"note\":\"n\"}", kRelayNowMs, out)
          == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayContent("{\"title\":123}", kRelayNowMs, out)
          == error_code_bad_request);

    //b. 只有空白
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent(u8"\" \u3000\\t \"", "", kValidDeadlineJson),
                                              kRelayNowMs, out) == error_code_bad_request);

    //c. 恰好上限通过，多一个字符拒绝
    const std::string okTitle = "\"" + repeatText(u8"字", RELAY_TITLE_MAX_CHARS) + "\"";
    const std::string longTitle = "\"" + repeatText(u8"字", RELAY_TITLE_MAX_CHARS + 1) + "\"";
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent(okTitle, "", kValidDeadlineJson), kRelayNowMs, out)
          == error_code_ok);
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent(longTitle, "", kValidDeadlineJson), kRelayNowMs, out)
          == error_code_msg_too_large);

    //d. 中间含换行一律拒绝：标题在卡片、提示与摘要里都只占一行
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"a\\nb\"", "", kValidDeadlineJson),
                                              kRelayNowMs, out) == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"a\\r\\nb\"", "", kValidDeadlineJson),
                                              kRelayNowMs, out) == error_code_bad_request);
}

TEST_CASE("K15 normalizeRelayContent 说明校验")
{
    std::string out;
    Json::Value v;

    //a. 恰好上限通过，多一个字符拒绝
    const std::string okNote = "\"" + repeatText(u8"字", RELAY_NOTE_MAX_CHARS) + "\"";
    const std::string longNote = "\"" + repeatText(u8"字", RELAY_NOTE_MAX_CHARS + 1) + "\"";
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", okNote, kValidDeadlineJson), kRelayNowMs, out)
          == error_code_ok);
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", longNote, kValidDeadlineJson), kRelayNowMs, out)
          == error_code_msg_too_large);

    //b. 中间的换行保留：说明本来就是多行的
    out.clear();
    REQUIRE(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "\"line1\\nline2\"", kValidDeadlineJson),
                                                kRelayNowMs, out) == error_code_ok);
    REQUIRE(parseJson(out, v));
    CHECK(v["note"].asString() == "line1\nline2");

    //c. 不是字符串
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "42", kValidDeadlineJson), kRelayNowMs, out)
          == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "[\"x\"]", kValidDeadlineJson), kRelayNowMs, out)
          == error_code_bad_request);

    //d. 只有空白：存为空串，照常通过
    out.clear();
    REQUIRE(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "\" \\t \\n \"", kValidDeadlineJson),
                                                kRelayNowMs, out) == error_code_ok);
    REQUIRE(parseJson(out, v));
    CHECK(v["note"].asString() == "");
}

TEST_CASE("K16 normalizeRelayContent 截止时间的类型校验")
{
    //取值是否够晚由 CRD1~CRD3 覆盖，这里只看 JSON 类型不对的情形
    std::string out;

    //d. 负数
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "", "-1"), kRelayNowMs, out)
          == error_code_bad_request);

    //e. 字符串形式的数字、小数、超出 int64 范围的无符号整数
    CHECK(CardProtocol::normalizeRelayContent(
        makeRelayContent("\"t\"", "", "\"" + std::to_string(kRelayNowMs + kOneHourMs) + "\""),
        kRelayNowMs, out) == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "", "1.5"), kRelayNowMs, out)
          == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayContent(
        makeRelayContent("\"t\"", "", "18446744073709551615"), kRelayNowMs, out)
        == error_code_bad_request);
}

TEST_CASE("K17 normalizeRelayContent 内容不是 JSON 对象")
{
    std::string out = "untouched";
    CHECK(CardProtocol::normalizeRelayContent("", kRelayNowMs, out) == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayContent("[]", kRelayNowMs, out) == error_code_bad_request);
    CHECK(CardProtocol::normalizeRelayContent("{not json", kRelayNowMs, out) == error_code_bad_request);
    //失败时出参保持调用前的值
    CHECK(out == "untouched");
}

TEST_CASE("K18 canForwardSeparately 接龙卡片不能逐条转发")
{
    CHECK_FALSE(CardProtocol::canForwardSeparately(message_type_relay));

    CHECK(CardProtocol::canForwardSeparately(message_type_text));
    CHECK(CardProtocol::canForwardSeparately(message_type_image));
    CHECK(CardProtocol::canForwardSeparately(message_type_file));
    CHECK(CardProtocol::canForwardSeparately(message_type_merged));
    //投票、待办等其客户端接入时再按同一规则改为不能逐条转发
    CHECK(CardProtocol::canForwardSeparately(message_type_poll));
    CHECK(CardProtocol::canForwardSeparately(message_type_todo));
}

TEST_CASE("K19 parseRelayTargetRequest 解析只指明一张卡片的请求")
{
    RelayTargetRequest req;
    REQUIRE(CardProtocol::parseRelayTargetRequest(
        "{\"ctype\":2,\"cid\":\"600001\",\"msgid\":\"9007199254741011\"}", req));
    CHECK(req.ctype == 2);
    CHECK(req.cid == 600001);
    CHECK(req.msgid == 9007199254741011LL);

    //缺 ctype 时取 1，与 parseJoinRelayRequest 一致
    RelayTargetRequest noType;
    REQUIRE(CardProtocol::parseRelayTargetRequest("{\"cid\":\"600001\",\"msgid\":\"5010\"}", noType));
    CHECK(noType.ctype == 1);

    //cid、msgid 缺失或不是正数
    CHECK_FALSE(CardProtocol::parseRelayTargetRequest("{\"ctype\":2,\"msgid\":\"5010\"}", req));
    CHECK_FALSE(CardProtocol::parseRelayTargetRequest("{\"ctype\":2,\"cid\":\"600001\"}", req));
    CHECK_FALSE(CardProtocol::parseRelayTargetRequest(
        "{\"ctype\":2,\"cid\":\"0\",\"msgid\":\"5010\"}", req));
    CHECK_FALSE(CardProtocol::parseRelayTargetRequest(
        "{\"ctype\":2,\"cid\":\"600001\",\"msgid\":\"-3\"}", req));
    //不是 JSON 对象
    CHECK_FALSE(CardProtocol::parseRelayTargetRequest("[]", req));
}

TEST_CASE("K20 buildRelayOpResponse 接龙操作的应答带回卡片定位字段")
{
    Json::Value root;

    //成功
    REQUIRE(parseJson(CardProtocol::buildRelayOpResponse(error_code_ok, "ok", conversation_group,
                                                         600001, 9007199254741011LL), root));
    CHECK(root["code"].asInt() == error_code_ok);
    CHECK(root["msg"].asString() == "ok");
    CHECK(root["ctype"].asInt() == conversation_group);
    REQUIRE(root["cid"].isString());
    CHECK(root["cid"].asString() == "600001");
    REQUIRE(root["msgid"].isString());
    CHECK(root["msgid"].asString() == "9007199254741011");

    //错误应答同样带回三个字段，客户端据 msgid 把提示送到对应的详情窗
    REQUIRE(parseJson(CardProtocol::buildRelayOpResponse(error_code_member_muted, "muted",
                                                         conversation_group, 600001, 5010), root));
    CHECK(root["code"].asInt() == error_code_member_muted);
    CHECK(root["ctype"].asInt() == conversation_group);
    CHECK(root["cid"].asString() == "600001");
    CHECK(root["msgid"].asString() == "5010");
}

TEST_CASE("K21 cardConversationIdFor 按收件人算出卡片所在会话的会话ID")
{
    const int64_t kGroupId = 600001;
    const int64_t kSender = 100001;
    const int64_t kReceiver = 100002;
    const int64_t kOtherMember = 100003;

    //群卡片：对任何成员都是群id
    CHECK(CardProtocol::cardConversationIdFor(conversation_group, kSender, kGroupId, kSender) == kGroupId);
    CHECK(CardProtocol::cardConversationIdFor(conversation_group, kSender, kGroupId, kOtherMember)
          == kGroupId);

    //单聊卡片：发送方看到的是接收方，接收方看到的是发送方
    CHECK(CardProtocol::cardConversationIdFor(conversation_single, kSender, kReceiver, kSender)
          == kReceiver);
    CHECK(CardProtocol::cardConversationIdFor(conversation_single, kSender, kReceiver, kReceiver)
          == kSender);
}

//K22~K27 共用：一张卡片的发起人、提前结束的操作者与结束时间
static const int64_t kRelayCreator = 100001;
static const int64_t kRelayCloser = 100002;
static const int64_t kRelayCloseTime = kRelayNowMs - kOneHourMs;

//构造一条"已被提前结束"的结束记录
static CardStateRecord makeClosedRow()
{
    CardStateRecord row;
    row.closed    = true;
    row.closer    = kRelayCloser;
    row.closetime = kRelayCloseTime;
    return row;
}

TEST_CASE("K22 computeRelayStatus 由结束记录与截止时间算出接龙状态")
{
    const CardStateRecord noRow;

    //没有结束记录、也不限时：进行中
    RelayStatus s = CardProtocol::computeRelayStatus(noRow, 0, kRelayNowMs);
    CHECK(s.state == relay_state_open);
    CHECK(s.closedby == 0);
    CHECK(s.closetime == 0);

    //截止时间还没到：进行中
    s = CardProtocol::computeRelayStatus(noRow, kRelayNowMs + 1, kRelayNowMs);
    CHECK(s.state == relay_state_open);

    //当前时间等于截止时间即算截止；晚于截止时间同样是已截止。closetime 取截止时间
    s = CardProtocol::computeRelayStatus(noRow, kRelayNowMs, kRelayNowMs);
    CHECK(s.state == relay_state_expired);
    CHECK(s.closedby == 0);
    CHECK(s.closetime == kRelayNowMs);
    s = CardProtocol::computeRelayStatus(noRow, kRelayNowMs - kOneHourMs, kRelayNowMs);
    CHECK(s.state == relay_state_expired);
    CHECK(s.closetime == kRelayNowMs - kOneHourMs);

    //有结束记录：已提前结束，带上操作者与结束时间
    s = CardProtocol::computeRelayStatus(makeClosedRow(), 0, kRelayNowMs);
    CHECK(s.state == relay_state_closed);
    CHECK(s.closedby == kRelayCloser);
    CHECK(s.closetime == kRelayCloseTime);

    //手动结束优先：截止时间也已经过了，仍按已提前结束算
    s = CardProtocol::computeRelayStatus(makeClosedRow(), kRelayNowMs - 1, kRelayNowMs);
    CHECK(s.state == relay_state_closed);
    CHECK(s.closedby == kRelayCloser);
}

TEST_CASE("K23 getRelayDeadline 从卡片内容里取截止时间")
{
    const int64_t deadline = kRelayNowMs + kOneHourMs;
    CHECK(CardProtocol::getRelayDeadline(
              "{\"title\":\"t\",\"note\":\"\",\"deadline\":" + std::to_string(deadline) + "}")
          == deadline);
    CHECK(CardProtocol::getRelayDeadline("{\"title\":\"t\",\"deadline\":0}") == 0);

    //字段缺失、内容不是 JSON 对象、类型不是整数、负数：都按不限返回 0
    CHECK(CardProtocol::getRelayDeadline("{\"title\":\"t\"}") == 0);
    CHECK(CardProtocol::getRelayDeadline("") == 0);
    CHECK(CardProtocol::getRelayDeadline("[]") == 0);
    CHECK(CardProtocol::getRelayDeadline("{\"deadline\":\"1759485600000\"}") == 0);
    CHECK(CardProtocol::getRelayDeadline("{\"deadline\":1.5}") == 0);
    CHECK(CardProtocol::getRelayDeadline("{\"deadline\":-1}") == 0);
}

TEST_CASE("K24 canCloseRelay 判断谁能提前结束接龙")
{
    const int64_t kOther = 100009;

    //群卡片：发起人（哪怕只是普通成员）、群主、管理员可以，其他普通成员不行
    CHECK(CardProtocol::canCloseRelay(true, kRelayCreator, kRelayCreator, group_role_member));
    CHECK(CardProtocol::canCloseRelay(true, kRelayCreator, kOther, group_role_owner));
    CHECK(CardProtocol::canCloseRelay(true, kRelayCreator, kOther, group_role_admin));
    CHECK_FALSE(CardProtocol::canCloseRelay(true, kRelayCreator, kOther, group_role_member));

    //单聊卡片：只有发起人可以，角色参数被忽略
    CHECK(CardProtocol::canCloseRelay(false, kRelayCreator, kRelayCreator, group_role_member));
    CHECK_FALSE(CardProtocol::canCloseRelay(false, kRelayCreator, kOther, group_role_owner));
}

TEST_CASE("K25 checkRelayJoinCapacity 人数上限")
{
    CHECK(RELAY_MAX_PARTICIPANTS == 200);

    //还差一个名额时新人可以加入；名额已满时新人被拒
    CHECK(CardProtocol::checkRelayJoinCapacity(false, RELAY_MAX_PARTICIPANTS - 1) == error_code_ok);
    CHECK(CardProtocol::checkRelayJoinCapacity(false, RELAY_MAX_PARTICIPANTS) == error_code_card_full);

    //已在名单里的人是修改，不占新名额
    CHECK(CardProtocol::checkRelayJoinCapacity(true, RELAY_MAX_PARTICIPANTS) == error_code_ok);
    CHECK(CardProtocol::checkRelayJoinCapacity(true, RELAY_MAX_PARTICIPANTS + 1) == error_code_ok);
}

TEST_CASE("K26 buildRelayChangedNotify 推送带上接龙状态")
{
    std::vector<CardInteractionRecord> rows;
    rows.push_back(makeRow(100, "Lin plus one", 1));
    Json::Value root;

    //进行中
    REQUIRE(parseJson(CardProtocol::buildRelayChangedNotify(conversation_group, 600001, 5010, rows,
                                                            RelayStatus()), root));
    CHECK(root["state"].asInt() == relay_state_open);
    REQUIRE(root["closedby"].isString());
    CHECK(root["closedby"].asString() == "0");
    CHECK(root["closetime"].asInt64() == 0);
    //原有字段不变
    CHECK(root["msgid"].asString() == "5010");
    CHECK(root["items"].size() == 1);

    //已提前结束：closedby 按字符串编码
    const RelayStatus closed = CardProtocol::computeRelayStatus(makeClosedRow(), 0, kRelayNowMs);
    REQUIRE(parseJson(CardProtocol::buildRelayChangedNotify(conversation_group, 600001, 5010, rows,
                                                            closed), root));
    CHECK(root["state"].asInt() == relay_state_closed);
    CHECK(root["closedby"].asString() == std::to_string(kRelayCloser));
    CHECK(root["closetime"].asInt64() == kRelayCloseTime);

    //已截止
    const RelayStatus expired = CardProtocol::computeRelayStatus(CardStateRecord(), kRelayNowMs,
                                                                 kRelayNowMs);
    REQUIRE(parseJson(CardProtocol::buildRelayChangedNotify(conversation_group, 600001, 5010, rows,
                                                            expired), root));
    CHECK(root["state"].asInt() == relay_state_expired);
    CHECK(root["closetime"].asInt64() == kRelayNowMs);
}

TEST_CASE("K27 augmentRelayContent 下发卡片时合并名单与状态")
{
    std::vector<CardInteractionRecord> rows;
    rows.push_back(makeRow(100, "real entry", 1));
    const RelayStatus closed = CardProtocol::computeRelayStatus(makeClosedRow(), 0, kRelayNowMs);
    Json::Value root;

    //标题、说明、截止时间保留；名单以真实互动为准，覆盖内容里残留的旧 items
    const std::string content =
        "{\"title\":\"t\",\"note\":\"n\",\"deadline\":0,"
        "\"items\":[{\"userid\":\"999\",\"text\":\"stale\"}]}";
    REQUIRE(parseJson(CardProtocol::augmentRelayContent(content, rows, closed), root));
    CHECK(root["title"].asString() == "t");
    CHECK(root["note"].asString() == "n");
    CHECK(root["deadline"].asInt64() == 0);
    REQUIRE(root["items"].size() == 1);
    CHECK(root["items"][0]["userid"].asString() == "100");
    CHECK(root["items"][0]["text"].asString() == "real entry");
    CHECK(root["state"].asInt() == relay_state_closed);
    CHECK(root["closedby"].asString() == std::to_string(kRelayCloser));
    CHECK(root["closetime"].asInt64() == kRelayCloseTime);

    //没有任何互动：items 为空数组
    std::vector<CardInteractionRecord> none;
    REQUIRE(parseJson(CardProtocol::augmentRelayContent(content, none, RelayStatus()), root));
    CHECK(root["items"].isArray());
    CHECK(root["items"].size() == 0);
    CHECK(root["state"].asInt() == relay_state_open);

    //内容不是 JSON 对象时原样返回
    CHECK(CardProtocol::augmentRelayContent("not json", rows, closed) == "not json");
}

TEST_CASE("K28 buildRelayTip 四种系统提示的文字")
{
    const std::string kNick = u8"张三";
    const std::string kTitle = u8"周五团建报名";
    const size_t kCount = 12;

    //与 docs/群接龙方案.md §3.4 的提示文字逐字一致
    CHECK(CardProtocol::buildRelayTip(relay_tip_join, kNick, kTitle, 0)
          == u8"「张三」参与了接龙「周五团建报名」");
    CHECK(CardProtocol::buildRelayTip(relay_tip_modify, kNick, kTitle, 0)
          == u8"「张三」修改了在接龙「周五团建报名」中的内容");
    CHECK(CardProtocol::buildRelayTip(relay_tip_withdraw, kNick, kTitle, 0)
          == u8"「张三」撤回了在接龙「周五团建报名」中的内容");
    CHECK(CardProtocol::buildRelayTip(relay_tip_close, u8"李四", kTitle, kCount)
          == u8"「李四」结束了接龙「周五团建报名」，共 12 人参与");

    //参与人数只在提前结束时写进文字
    CHECK(CardProtocol::buildRelayTip(relay_tip_join, kNick, kTitle, kCount)
          == u8"「张三」参与了接龙「周五团建报名」");

    //兜底：昵称为空写成「有成员」，标题为空只写「接龙」
    CHECK(CardProtocol::buildRelayTip(relay_tip_join, "", kTitle, 0)
          == u8"有成员参与了接龙「周五团建报名」");
    CHECK(CardProtocol::buildRelayTip(relay_tip_join, kNick, "", 0) == u8"「张三」参与了接龙");
    CHECK(CardProtocol::buildRelayTip(relay_tip_close, kNick, "", 0)
          == u8"「张三」结束了接龙，共 0 人参与");
}

TEST_CASE("K29 getRelayTitle 从卡片内容里取标题")
{
    CHECK(CardProtocol::getRelayTitle(u8"{\"title\":\"周五团建报名\",\"note\":\"n\",\"deadline\":0}")
          == u8"周五团建报名");
    CHECK(CardProtocol::getRelayTitle("{\"note\":\"n\"}") == "");
    CHECK(CardProtocol::getRelayTitle("{\"title\":123}") == "");
    CHECK(CardProtocol::getRelayTitle("not json") == "");
    CHECK(CardProtocol::getRelayTitle("") == "");
}

TEST_CASE("K30 buildRelayTipContent 系统提示内容带上卡片所在消息id与结构化字段")
{
    Json::Value root;
    REQUIRE(parseJson(CardProtocol::buildRelayTipContent(relay_tip_join, u8"张三", u8"t", 0, 9007199254741011LL),
                      root));
    CHECK(root["text"].asString() == u8"「张三」参与了接龙「t」");
    REQUIRE(root["cardmsgid"].isString());
    CHECK(root["cardmsgid"].asString() == "9007199254741011");
    //多语言第 5 期：tipkind 与 tipargs，参与人数只在提前结束时写
    CHECK(root["tipkind"].asString() == "relay_join");
    REQUIRE(root["tipargs"].isObject());
    CHECK(root["tipargs"]["nickname"].asString() == u8"张三");
    CHECK(root["tipargs"]["title"].asString() == u8"t");
    CHECK_FALSE(root["tipargs"].isMember("count"));
    CHECK(root.size() == 4);

    //提前结束带参与人数；昵称、标题为空时不写进 tipargs，中文 text 按兜底写法
    Json::Value closed;
    REQUIRE(parseJson(CardProtocol::buildRelayTipContent(relay_tip_close, "", "", 12, 1), closed));
    CHECK(closed["text"].asString() == u8"有成员结束了接龙，共 12 人参与");
    CHECK(closed["tipkind"].asString() == "relay_close");
    CHECK(closed["tipargs"]["count"].asUInt64() == 12);
    CHECK_FALSE(closed["tipargs"].isMember("nickname"));
    CHECK_FALSE(closed["tipargs"].isMember("title"));
}

TEST_CASE("K32 relayTipKindName 接龙提示种类到系统提示类型")
{
    CHECK(std::string(CardProtocol::relayTipKindName(relay_tip_join)) == "relay_join");
    CHECK(std::string(CardProtocol::relayTipKindName(relay_tip_modify)) == "relay_modify");
    CHECK(std::string(CardProtocol::relayTipKindName(relay_tip_withdraw)) == "relay_withdraw");
    CHECK(std::string(CardProtocol::relayTipKindName(relay_tip_close)) == "relay_close");
    //未知种类与 buildRelayTip 的「更新了接龙」兜底对应
    const int kUnknownKind = 99;
    CHECK(std::string(CardProtocol::relayTipKindName(kUnknownKind)) == "relay_update");
}

TEST_CASE("K31 buildRelayTipClientMsgId 占位客户端消息ID各不相同")
{
    const int64_t kCard = 9007199254741011LL;
    const std::string base = CardProtocol::buildRelayTipClientMsgId(relay_tip_join, kCard, 100001, kRelayNowMs);

    //以 sys- 开头（系统消息的约定），并带上卡片id
    CHECK(base.compare(0, 10, "sys-relay-") == 0);
    CHECK(base.find(std::to_string(kCard)) != std::string::npos);

    //提示种类、操作者、时间任何一项不同，生成的ID都不同
    CHECK(CardProtocol::buildRelayTipClientMsgId(relay_tip_modify, kCard, 100001, kRelayNowMs) != base);
    CHECK(CardProtocol::buildRelayTipClientMsgId(relay_tip_join, kCard, 100002, kRelayNowMs) != base);
    CHECK(CardProtocol::buildRelayTipClientMsgId(relay_tip_join, kCard, 100001, kRelayNowMs + 1) != base);
    //同一组输入得到同一个ID
    CHECK(CardProtocol::buildRelayTipClientMsgId(relay_tip_join, kCard, 100001, kRelayNowMs) == base);
}

TEST_CASE("K32 classifyRelayJoin 区分首次参与、修改与重复提交")
{
    std::vector<CardInteractionRecord> rows;
    rows.push_back(makeRow(100001, u8"本人+1", 1));
    rows.push_back(makeRow(100002, u8"不带家属", 2));

    //名单里没有本人：首次参与
    CHECK(CardProtocol::classifyRelayJoin(rows, 100003, u8"本人") == relay_join_new);
    //名单里有本人、内容不同：修改
    CHECK(CardProtocol::classifyRelayJoin(rows, 100001, u8"本人+2") == relay_join_modify);
    //名单里有本人、内容相同：重复提交
    CHECK(CardProtocol::classifyRelayJoin(rows, 100002, u8"不带家属") == relay_join_unchanged);
    //名单为空：首次参与
    std::vector<CardInteractionRecord> none;
    CHECK(CardProtocol::classifyRelayJoin(none, 100001, u8"本人") == relay_join_new);
}

TEST_CASE("K33 shouldAugmentCard 只对未撤回的卡片合并互动状态")
{
    //三种卡片，未撤回：合并
    CHECK(CardProtocol::shouldAugmentCard(message_type_relay, false));
    CHECK(CardProtocol::shouldAugmentCard(message_type_poll, false));
    CHECK(CardProtocol::shouldAugmentCard(message_type_todo, false));

    //三种卡片，已撤回：不合并，名单与投票结果不随已撤回的卡片下发
    CHECK_FALSE(CardProtocol::shouldAugmentCard(message_type_relay, true));
    CHECK_FALSE(CardProtocol::shouldAugmentCard(message_type_poll, true));
    CHECK_FALSE(CardProtocol::shouldAugmentCard(message_type_todo, true));

    //非卡片消息：不合并
    CHECK_FALSE(CardProtocol::shouldAugmentCard(message_type_text, false));
    CHECK_FALSE(CardProtocol::shouldAugmentCard(message_type_image, false));
    CHECK_FALSE(CardProtocol::shouldAugmentCard(message_type_merged, false));
}

TEST_CASE("CRD1 发起接龙必须设置截止时间：缺省、null、0 都被拒")
{
    std::string out = "untouched";
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "", ""), kRelayNowMs, out)
          == error_code_relay_deadline_invalid);
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "", "null"), kRelayNowMs, out)
          == error_code_relay_deadline_invalid);
    CHECK(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "", "0"), kRelayNowMs, out)
          == error_code_relay_deadline_invalid);
    //被拒时不写出参
    CHECK(out == "untouched");
}

TEST_CASE("CRD2 截止时间恰好是当前时间 + 5 分钟时通过，原值保存")
{
    const int64_t earliest = kRelayNowMs + RELAY_DEADLINE_MIN_LEAD_MS;
    std::string out;
    REQUIRE(CardProtocol::normalizeRelayContent(
        makeRelayContent("\"t\"", "", std::to_string(earliest)), kRelayNowMs, out) == error_code_ok);
    Json::Value v;
    REQUIRE(parseJson(out, v));
    CHECK(v["deadline"].asInt64() == earliest);

    //更晚的同样通过
    out.clear();
    REQUIRE(CardProtocol::normalizeRelayContent(makeRelayContent("\"t\"", "", kValidDeadlineJson),
                                                kRelayNowMs, out) == error_code_ok);
    REQUIRE(parseJson(out, v));
    CHECK(v["deadline"].asInt64() == kRelayNowMs + kOneHourMs);
}

TEST_CASE("CRD3 截止时间早于当前时间 + 5 分钟时被拒，回 510")
{
    std::string out;
    //早 1 毫秒
    CHECK(CardProtocol::normalizeRelayContent(
        makeRelayContent("\"t\"", "", std::to_string(kRelayNowMs + RELAY_DEADLINE_MIN_LEAD_MS - 1)),
        kRelayNowMs, out) == error_code_relay_deadline_invalid);
    //当前时间之后 1 毫秒、恰好等于当前时间、已经过去
    CHECK(CardProtocol::normalizeRelayContent(
        makeRelayContent("\"t\"", "", std::to_string(kRelayNowMs + 1)), kRelayNowMs, out)
        == error_code_relay_deadline_invalid);
    CHECK(CardProtocol::normalizeRelayContent(
        makeRelayContent("\"t\"", "", std::to_string(kRelayNowMs)), kRelayNowMs, out)
        == error_code_relay_deadline_invalid);
    CHECK(CardProtocol::normalizeRelayContent(
        makeRelayContent("\"t\"", "", std::to_string(kRelayNowMs - kOneHourMs)), kRelayNowMs, out)
        == error_code_relay_deadline_invalid);
    //最短提前量是 5 分钟
    CHECK(RELAY_DEADLINE_MIN_LEAD_MS == 5LL * 60LL * 1000LL);
}
