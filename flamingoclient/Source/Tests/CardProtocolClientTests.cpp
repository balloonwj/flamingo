/**
 *  v2 群互动卡片报文编解码（客户端侧）单元测试（Suite CPC），CardProtocolClientTests.cpp
 *
 *  覆盖 net/CardProtocolClient 的群接龙部分（docs/群接龙方案.md 第 2 期 2a）：
 *    - 发送前的校验与拼装：buildRelayCardContent、normalizeRelayText、utf8CharCount，
 *      规则与服务端 CardProtocol 逐条一致（去首尾空白、拒绝中间换行、按 UTF-8 码点计长度）；
 *    - 请求构造：buildJoinRelayRequest、buildRelayTargetRequest 的字段与 64 位 ID 的字符串编码，
 *      入参不合契约时返回空串；
 *    - 应答与推送解析：parseRelayOpResponse、parseRelayChangedNotify 的字段还原、大整数往返、
 *      格式不对的名单条目跳过、畸形输入安全失败；
 *    - 四个长度 / 人数常量与服务端取值一致。
 *
 *  解析用例的输入照着服务端 chatserversrc/CardProtocol.cpp 的 buildRelayOpResponse 与
 *  buildRelayChangedNotify <u>等价重写</u>（见 makeServerXxx 一组函数），锁住的是两端真正的
 *  互通口径，而不只是本模块自己跟自己往返。
 *
 *  标签 [grouprelay]（[relay] 已被远程桌面 / 音视频中转通道的联调用例占用）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"
#include "../net/CardProtocolClient.h"
#include "../net/Protocol.h"   //conversation_type、relay_state 与 error_code_* 的具体取值
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;

//本组用例反复用到的群、卡片与参与者，取协议文档 10.22 里的样例值
static const int64_t kGroupId = 600001;
static const int64_t kCardMsgId = 9007199254741011LL;
static const int64_t kUserA = 100008;
static const int64_t kUserB = 100009;

//超过 2^53 的 64 位 ID：按双精度解析会丢精度，用来验证字符串编码往返无损
static const int64_t kHugeId = 9007199254740993LL;

//一个 4 字节的 UTF-8 字符（U+1F600，笑脸表情），按字节写出，不依赖源文件编码
static const char* const kEmojiUtf8 = "\xF0\x9F\x98\x80";

//把 JSON 文本解析成 Json::Value，供断言检查构造产物
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的应答 / 推送报文
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把同一段文本重复 count 次拼起来，用于构造长度边界
static std::string repeatText(const std::string& unit, size_t count)
{
    std::string s;
    for (size_t i = 0; i < count; ++i)
        s += unit;
    return s;
}

//==================== 服务端报文的等价重写 ====================

//等价重写服务端 CardProtocol::buildRelayOpResponse：{ code, msg, ctype, cid, msgid }，
//cid 与 msgid 按十进制字符串编码
static std::string makeServerRelayOpResponse(int code, const std::string& msg, int ctype,
                                             int64_t cid, int64_t msgid)
{
    Json::Value root;
    root["code"]  = code;
    root["msg"]   = msg;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    return writeCompact(root);
}

//等价重写服务端 CardProtocol::buildRelayChangedNotify（含其 writeRelayStatus）：
//{ ctype, cid, msgid, items:[{userid,text}], state, closedby, closetime }，
//userid / cid / msgid / closedby 按字符串编码，closetime 为 64 位整数
static std::string makeServerRelayChangedNotify(int ctype, int64_t cid, int64_t msgid,
                                                const Json::Value& items, int state,
                                                int64_t closedby, int64_t closetime)
{
    Json::Value root;
    root["ctype"]     = ctype;
    root["cid"]       = std::to_string(cid);
    root["msgid"]     = std::to_string(msgid);
    root["items"]     = items;
    root["state"]     = state;
    root["closedby"]  = std::to_string(closedby);
    root["closetime"] = static_cast<Json::Int64>(closetime);
    return writeCompact(root);
}

//构造服务端 aggregateRelay 产出的一条名单条目
static Json::Value makeServerItem(int64_t userid, const std::string& text)
{
    Json::Value item;
    item["userid"] = std::to_string(userid);
    item["text"]   = text;
    return item;
}

//==================== 发送前的校验与拼装 ====================

TEST_CASE("CPC1 buildRelayCardContent 合法内容只输出三个字段且去掉首尾空白", "[grouprelay]")
{
    const int64_t deadline = 1759485600000LL;
    std::string out;
    REQUIRE(CardProtocolClient::buildRelayCardContent(u8" 周五团建报名　", u8"\n格式：姓名+是否带家属 ",
                                                     deadline, out) == error_code_ok);
    Json::Value v;
    REQUIRE(parseJsonText(out, v));
    CHECK(v.size() == 3);
    CHECK(v["title"].asString() == u8"周五团建报名");
    CHECK(v["note"].asString() == u8"格式：姓名+是否带家属");
    CHECK(v["deadline"].asInt64() == deadline);

    //说明为空、截止时间为 0（不限）
    out.clear();
    REQUIRE(CardProtocolClient::buildRelayCardContent("t", "", 0, out) == error_code_ok);
    REQUIRE(parseJsonText(out, v));
    CHECK(v["note"].asString() == "");
    CHECK(v["deadline"].asInt64() == 0);
}

TEST_CASE("CPC2 buildRelayCardContent 标题校验", "[grouprelay]")
{
    std::string out = "untouched";
    CHECK(CardProtocolClient::buildRelayCardContent("", "", 0, out) == error_code_bad_request);
    CHECK(CardProtocolClient::buildRelayCardContent(u8" \t　 ", "", 0, out) == error_code_bad_request);
    CHECK(CardProtocolClient::buildRelayCardContent("a\nb", "", 0, out) == error_code_bad_request);
    CHECK(CardProtocolClient::buildRelayCardContent("a\r\nb", "", 0, out) == error_code_bad_request);
    //失败时出参不被改写
    CHECK(out == "untouched");

    CHECK(CardProtocolClient::buildRelayCardContent(repeatText(u8"字", kRelayTitleMaxChars), "", 0, out)
          == error_code_ok);
    CHECK(CardProtocolClient::buildRelayCardContent(repeatText(u8"字", kRelayTitleMaxChars + 1), "", 0, out)
          == error_code_msg_too_large);
}

TEST_CASE("CPC3 buildRelayCardContent 说明校验", "[grouprelay]")
{
    std::string out;
    Json::Value v;

    CHECK(CardProtocolClient::buildRelayCardContent("t", repeatText(u8"字", kRelayNoteMaxChars), 0, out)
          == error_code_ok);
    CHECK(CardProtocolClient::buildRelayCardContent("t", repeatText(u8"字", kRelayNoteMaxChars + 1), 0, out)
          == error_code_msg_too_large);

    //说明本来就是多行的，中间的换行保留
    REQUIRE(CardProtocolClient::buildRelayCardContent("t", "line1\nline2", 0, out) == error_code_ok);
    REQUIRE(parseJsonText(out, v));
    CHECK(v["note"].asString() == "line1\nline2");

    //只有空白时存为空串
    REQUIRE(CardProtocolClient::buildRelayCardContent("t", " \t\n ", 0, out) == error_code_ok);
    REQUIRE(parseJsonText(out, v));
    CHECK(v["note"].asString() == "");
}

TEST_CASE("CPC4 buildRelayCardContent 截止时间不能为负，是否已过由服务端判断", "[grouprelay]")
{
    std::string out;
    CHECK(CardProtocolClient::buildRelayCardContent("t", "", -1, out) == error_code_bad_request);
    //很早以前的时间客户端照样放行：本机时钟可能不准，以服务端的判定为准
    CHECK(CardProtocolClient::buildRelayCardContent("t", "", 1, out) == error_code_ok);
}

TEST_CASE("CPC5 normalizeRelayText 规则与服务端一致", "[grouprelay]")
{
    std::string out;

    //首尾空白（含全角空格、首尾换行）去掉，中间空格保留
    REQUIRE(CardProtocolClient::normalizeRelayText(u8"　 张三 本人+1\r\n", out) == error_code_ok);
    CHECK(out == u8"张三 本人+1");

    //只有空白、空串
    CHECK(CardProtocolClient::normalizeRelayText(u8" \t　\r\n", out) == error_code_bad_request);
    CHECK(CardProtocolClient::normalizeRelayText("", out) == error_code_bad_request);

    //中间含换行一律拒绝
    CHECK(CardProtocolClient::normalizeRelayText("a\nb", out) == error_code_bad_request);
    CHECK(CardProtocolClient::normalizeRelayText("a\r\nb", out) == error_code_bad_request);
    CHECK(CardProtocolClient::normalizeRelayText("a\rb", out) == error_code_bad_request);

    //长度：汉字、ASCII、4 字节表情都按一个字符计
    CHECK(CardProtocolClient::normalizeRelayText(repeatText(u8"字", kRelayTextMaxChars), out) == error_code_ok);
    CHECK(CardProtocolClient::normalizeRelayText(repeatText(u8"字", kRelayTextMaxChars + 1), out)
          == error_code_msg_too_large);
    CHECK(CardProtocolClient::normalizeRelayText(repeatText("a", kRelayTextMaxChars + 1), out)
          == error_code_msg_too_large);
    CHECK(CardProtocolClient::normalizeRelayText(repeatText(kEmojiUtf8, kRelayTextMaxChars), out)
          == error_code_ok);

    //先去空白再计数
    CHECK(CardProtocolClient::normalizeRelayText("  " + repeatText(u8"字", kRelayTextMaxChars) + "  ", out)
          == error_code_ok);

    CHECK(CardProtocolClient::utf8CharCount("") == 0);
    CHECK(CardProtocolClient::utf8CharCount(u8"张三abc") == 5);
}

//==================== 请求构造 ====================

TEST_CASE("CPC6 buildJoinRelayRequest 字段齐全，ID 按字符串编码", "[grouprelay]")
{
    Json::Value v;
    REQUIRE(parseJsonText(CardProtocolClient::buildJoinRelayRequest(conversation_group, kGroupId,
                                                                    kHugeId, u8"本人+1"), v));
    CHECK(v.size() == 4);
    CHECK(v["ctype"].asInt() == conversation_group);
    REQUIRE(v["cid"].isString());
    CHECK(v["cid"].asString() == "600001");
    REQUIRE(v["msgid"].isString());
    CHECK(v["msgid"].asString() == "9007199254740993");
    CHECK(v["text"].asString() == u8"本人+1");

    //入参不合契约时返回空串，调用方据此放弃发送
    CHECK(CardProtocolClient::buildJoinRelayRequest(conversation_group, 0, kCardMsgId, "x").empty());
    CHECK(CardProtocolClient::buildJoinRelayRequest(conversation_group, kGroupId, 0, "x").empty());
    CHECK(CardProtocolClient::buildJoinRelayRequest(conversation_group, kGroupId, kCardMsgId, "").empty());
}

TEST_CASE("CPC7 buildRelayTargetRequest 只带三个定位字段", "[grouprelay]")
{
    Json::Value v;
    REQUIRE(parseJsonText(CardProtocolClient::buildRelayTargetRequest(conversation_group, kGroupId,
                                                                      kCardMsgId), v));
    CHECK(v.size() == 3);
    CHECK(v["ctype"].asInt() == conversation_group);
    CHECK(v["cid"].asString() == "600001");
    CHECK(v["msgid"].asString() == "9007199254741011");

    CHECK(CardProtocolClient::buildRelayTargetRequest(conversation_group, -1, kCardMsgId).empty());
    CHECK(CardProtocolClient::buildRelayTargetRequest(conversation_group, kGroupId, -1).empty());
}

//==================== 应答与推送解析 ====================

TEST_CASE("CPC8 parseRelayOpResponse 还原服务端应答", "[grouprelay]")
{
    //成功应答
    RelayOpResponse ok;
    REQUIRE(CardProtocolClient::parseRelayOpResponse(
        makeServerRelayOpResponse(error_code_ok, "ok", conversation_group, kGroupId, kHugeId), ok));
    CHECK(ok.m_code == error_code_ok);
    CHECK(ok.m_msg == "ok");
    CHECK(ok.m_ctype == conversation_group);
    CHECK(ok.m_cid == kGroupId);
    CHECK(ok.m_msgid == kHugeId);

    //错误应答同样带回定位字段
    RelayOpResponse closed;
    REQUIRE(CardProtocolClient::parseRelayOpResponse(
        makeServerRelayOpResponse(error_code_card_closed, "relay closed", conversation_group, kGroupId,
                                  kCardMsgId), closed));
    CHECK(closed.m_code == error_code_card_closed);
    CHECK(closed.m_msgid == kCardMsgId);

    //请求格式错误时服务端只回 { code, msg }：解析成功，三个定位字段为 0
    RelayOpResponse bad;
    REQUIRE(CardProtocolClient::parseRelayOpResponse("{\"code\":3,\"msg\":\"bad request\"}", bad));
    CHECK(bad.m_code == error_code_bad_request);
    CHECK(bad.m_ctype == 0);
    CHECK(bad.m_cid == 0);
    CHECK(bad.m_msgid == 0);

    //JSON 非法、缺 code、code 不是整数
    RelayOpResponse r;
    CHECK_FALSE(CardProtocolClient::parseRelayOpResponse("not json", r));
    CHECK_FALSE(CardProtocolClient::parseRelayOpResponse("{\"msg\":\"ok\"}", r));
    CHECK_FALSE(CardProtocolClient::parseRelayOpResponse("{\"code\":\"0\"}", r));
}

TEST_CASE("CPC9 parseRelayChangedNotify 还原服务端推送", "[grouprelay]")
{
    Json::Value items(Json::arrayValue);
    items.append(makeServerItem(kUserA, u8"本人+1"));
    items.append(makeServerItem(kHugeId, u8"不带家属"));
    items.append(makeServerItem(kUserB, "third"));

    //进行中：字段还原、名单顺序保持、大整数往返无损
    RelayChangedNotify open;
    REQUIRE(CardProtocolClient::parseRelayChangedNotify(
        makeServerRelayChangedNotify(conversation_group, kGroupId, kCardMsgId, items, relay_state_open, 0, 0),
        open));
    CHECK(open.m_ctype == conversation_group);
    CHECK(open.m_cid == kGroupId);
    CHECK(open.m_msgid == kCardMsgId);
    REQUIRE(open.m_items.size() == 3);
    CHECK(open.m_items[0].m_userid == kUserA);
    CHECK(open.m_items[0].m_text == u8"本人+1");
    CHECK(open.m_items[1].m_userid == kHugeId);
    CHECK(open.m_items[2].m_userid == kUserB);
    CHECK(open.m_state == relay_state_open);
    CHECK(open.m_closedBy == 0);
    CHECK(open.m_closeTime == 0);

    //已提前结束：带上操作者与结束时间
    const int64_t closeTime = 1759401234567LL;
    RelayChangedNotify closed;
    REQUIRE(CardProtocolClient::parseRelayChangedNotify(
        makeServerRelayChangedNotify(conversation_group, kGroupId, kCardMsgId, items, relay_state_closed,
                                     kUserB, closeTime), closed));
    CHECK(closed.m_state == relay_state_closed);
    CHECK(closed.m_closedBy == kUserB);
    CHECK(closed.m_closeTime == closeTime);

    //名单为空
    RelayChangedNotify empty;
    REQUIRE(CardProtocolClient::parseRelayChangedNotify(
        makeServerRelayChangedNotify(conversation_group, kGroupId, kCardMsgId, Json::Value(Json::arrayValue),
                                     relay_state_expired, 0, closeTime), empty));
    CHECK(empty.m_items.empty());
    CHECK(empty.m_state == relay_state_expired);

    //名单里格式不对的条目跳过，其余照常
    Json::Value mixed(Json::arrayValue);
    mixed.append(makeServerItem(kUserA, "good"));
    Json::Value noUser;
    noUser["text"] = "missing userid";
    mixed.append(noUser);
    Json::Value badText;
    badText["userid"] = std::to_string(kUserB);
    badText["text"]   = 42;
    mixed.append(badText);
    mixed.append(Json::Value("not an object"));
    mixed.append(makeServerItem(kUserB, "also good"));
    RelayChangedNotify partial;
    REQUIRE(CardProtocolClient::parseRelayChangedNotify(
        makeServerRelayChangedNotify(conversation_group, kGroupId, kCardMsgId, mixed, relay_state_open, 0, 0),
        partial));
    REQUIRE(partial.m_items.size() == 2);
    CHECK(partial.m_items[0].m_text == "good");
    CHECK(partial.m_items[1].m_text == "also good");

    //畸形输入：JSON 非法、缺 cid 或 msgid、items 不是数组
    RelayChangedNotify r;
    CHECK_FALSE(CardProtocolClient::parseRelayChangedNotify("", r));
    CHECK_FALSE(CardProtocolClient::parseRelayChangedNotify("{not json", r));
    CHECK_FALSE(CardProtocolClient::parseRelayChangedNotify(
        "{\"ctype\":2,\"msgid\":\"9007199254741011\",\"items\":[]}", r));
    CHECK_FALSE(CardProtocolClient::parseRelayChangedNotify(
        "{\"ctype\":2,\"cid\":\"600001\",\"items\":[]}", r));
    CHECK_FALSE(CardProtocolClient::parseRelayChangedNotify(
        "{\"ctype\":2,\"cid\":\"600001\",\"msgid\":\"9007199254741011\",\"items\":{}}", r));

    //状态字段缺失（更早的服务端）：按进行中处理
    RelayChangedNotify legacy;
    REQUIRE(CardProtocolClient::parseRelayChangedNotify(
        "{\"ctype\":2,\"cid\":\"600001\",\"msgid\":\"9007199254741011\",\"items\":[]}", legacy));
    CHECK(legacy.m_state == relay_state_open);
    CHECK(legacy.m_closedBy == 0);
}

TEST_CASE("CPC10 长度与人数常量与服务端取值一致", "[grouprelay]")
{
    //服务端 chatserversrc/CardProtocol.h 的 RELAY_TITLE_MAX_CHARS / RELAY_NOTE_MAX_CHARS /
    //RELAY_TEXT_MAX_CHARS / RELAY_MAX_PARTICIPANTS，取值见 docs/群接龙方案.md §4.8 与决策表
    CHECK(kRelayTitleMaxChars == 50);
    CHECK(kRelayNoteMaxChars == 200);
    CHECK(kRelayTextMaxChars == 100);
    CHECK(kRelayMaxParticipants == 200);
}

//等价重写服务端 CardProtocol::augmentRelayContent 对一张规范化卡片的输出：
//在 { title, note, deadline } 上合并 items 与 state / closedby / closetime
static std::string makeServerAugmentedContent(const std::string& title, const std::string& note,
                                              int64_t deadline, const Json::Value& items, int state,
                                              int64_t closedby, int64_t closetime)
{
    Json::Value root;
    root["title"]     = title;
    root["note"]      = note;
    root["deadline"]  = static_cast<Json::Int64>(deadline);
    root["items"]     = items;
    root["state"]     = state;
    root["closedby"]  = std::to_string(closedby);
    root["closetime"] = static_cast<Json::Int64>(closetime);
    return writeCompact(root);
}

TEST_CASE("CPC11 parseRelayCardContent 解析同步下发与实时推送两种卡片内容", "[grouprelay]")
{
    const int64_t deadline = 1759485600000LL;
    Json::Value items(Json::arrayValue);
    items.append(makeServerItem(kUserA, u8"本人+1"));
    items.append(makeServerItem(kHugeId, u8"不带家属"));

    //同步下发的完整内容：七个字段都还原
    RelayCardContent full;
    REQUIRE(CardProtocolClient::parseRelayCardContent(
        makeServerAugmentedContent(u8"周五团建报名", u8"格式：姓名", deadline, items, relay_state_closed,
                                   kUserB, deadline - 1), full));
    CHECK(full.m_title == u8"周五团建报名");
    CHECK(full.m_note == u8"格式：姓名");
    CHECK(full.m_deadline == deadline);
    REQUIRE(full.m_items.size() == 2);
    CHECK(full.m_items[1].m_userid == kHugeId);
    CHECK(full.m_state == relay_state_closed);
    CHECK(full.m_closedBy == kUserB);
    CHECK(full.m_closeTime == deadline - 1);

    //实时推送的新卡片只有三个字段：名单为空、进行中
    RelayCardContent fresh;
    REQUIRE(CardProtocolClient::parseRelayCardContent(
        u8"{\"title\":\"周五团建报名\",\"note\":\"\",\"deadline\":0}", fresh));
    CHECK(fresh.m_items.empty());
    CHECK(fresh.m_state == relay_state_open);
    CHECK(fresh.m_deadline == 0);
    CHECK(fresh.m_closedBy == 0);

    //缺截止时间与说明时取缺省值
    RelayCardContent legacy;
    REQUIRE(CardProtocolClient::parseRelayCardContent("{\"title\":\"t\"}", legacy));
    CHECK(legacy.m_note == "");
    CHECK(legacy.m_deadline == 0);

    //名单里格式不对的条目跳过
    Json::Value mixed(Json::arrayValue);
    mixed.append(makeServerItem(kUserA, "good"));
    mixed.append(Json::Value(42));
    RelayCardContent partial;
    REQUIRE(CardProtocolClient::parseRelayCardContent(
        makeServerAugmentedContent("t", "", 0, mixed, relay_state_open, 0, 0), partial));
    REQUIRE(partial.m_items.size() == 1);
    CHECK(partial.m_items[0].m_text == "good");

    //不是 JSON 对象
    RelayCardContent bad;
    CHECK_FALSE(CardProtocolClient::parseRelayCardContent("", bad));
    CHECK_FALSE(CardProtocolClient::parseRelayCardContent("[]", bad));
    CHECK_FALSE(CardProtocolClient::parseRelayCardContent("not json", bad));
}

TEST_CASE("CPC12 serializeRelayCardContent 写回的内容与服务端同构、往返不变", "[grouprelay]")
{
    RelayCardContent card;
    card.m_title     = u8"周五团建报名";
    card.m_note      = "line1\nline2";
    card.m_deadline  = 1759485600000LL;
    card.m_state     = relay_state_expired;
    card.m_closedBy  = 0;
    card.m_closeTime = card.m_deadline;
    RelayItem a;
    a.m_userid = kHugeId;
    a.m_text   = u8"本人+1";
    card.m_items.push_back(a);
    RelayItem b;
    b.m_userid = kUserA;
    b.m_text   = "second";
    card.m_items.push_back(b);

    const std::string json = CardProtocolClient::serializeRelayCardContent(card);

    //七个字段齐全，64 位 ID 按字符串编码
    Json::Value v;
    REQUIRE(parseJsonText(json, v));
    CHECK(v.size() == 7);
    REQUIRE(v["items"][0]["userid"].isString());
    CHECK(v["items"][0]["userid"].asString() == "9007199254740993");
    REQUIRE(v["closedby"].isString());

    //解析回来与原内容一致，名单顺序不变
    RelayCardContent back;
    REQUIRE(CardProtocolClient::parseRelayCardContent(json, back));
    CHECK(back.m_title == card.m_title);
    CHECK(back.m_note == card.m_note);
    CHECK(back.m_deadline == card.m_deadline);
    CHECK(back.m_state == card.m_state);
    CHECK(back.m_closeTime == card.m_closeTime);
    REQUIRE(back.m_items.size() == 2);
    CHECK(back.m_items[0].m_userid == kHugeId);
    CHECK(back.m_items[0].m_text == u8"本人+1");
    CHECK(back.m_items[1].m_text == "second");
}
