/**
 *  ReplyQuoteProtocolTests.cpp
 *
 *  引用回复（content.quote）的协议往返测试。
 *
 *  服务端对 quote 是<u>完全不透明</u>的 —— MessageProtocol 只把它当一段 JSON 原样落库、
 *  原样回推，字段怎么定全由客户端说了算。正因为服务端不校验，客户端这一侧的读写对称性
 *  就是唯一防线：写进去什么、读回来必须一模一样，少一个字段、多一层编码都不会有人替
 *  我们发现，只会表现为"引用块显示不出来"。故本组用例逐字段钉死。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言，不建窗口、不发网络请求。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"   // message_type_text / message_type_image

using namespace net;

namespace {

// 本组用例统一用的会话与消息标识。
const int64_t kPeerId    = 5127;
const int64_t kGroupId   = 2001;
const int64_t kQuoteMsgid = 184;
const int64_t kQuoteFrom  = 5308;

// 显式写成 UTF-8 字节，不依赖源文件编码 —— 这个测试工程要在不同机器上编译，
// 源文件编码一旦被工具改掉，字面量里的中文就会变成别的字节。
// "韩晓燕" = E9 9F A9 E6 99 93 E7 87 95
const char* const kNameUtf8 = "\xE9\x9F\xA9\xE6\x99\x93\xE7\x87\x95";
// "你好" = E4 BD A0 E5 A5 BD
const char* const kDigestUtf8 = "\xE4\xBD\xA0\xE5\xA5\xBD";

// 造一条典型的文本引用快照。
ReplyQuote MakeTextQuote()
{
    ReplyQuote q;
    q.m_valid   = true;
    q.m_msgid   = kQuoteMsgid;
    q.m_from    = kQuoteFrom;
    q.m_name    = kNameUtf8;
    q.m_msgtype = message_type_text;
    q.m_digest  = kDigestUtf8;
    return q;
}

// 把一条"发送请求"补上 msgid / from 后当作推送来解析 —— 与 GroupMentionTests 里
// RoundTripGroupText 同一手法：发送与推送的 content 是同一份，故可以这样往返。
bool RoundTrip(const std::string& request, MessageNotify& out)
{
    std::string req = request;
    if (req.empty() || req[0] != '{')
    {
        return false;
    }
    req.insert(1, "\"msgid\":\"9001\",\"from\":\"10002\",");
    return MessageProtocolClient::parseMessageNotify(req, out);
}

} // anonymous namespace

// =====================================================================
// Q1-Q3：基本往返与向后兼容
// =====================================================================

TEST_CASE("Q1 单聊文本带引用往返后逐字段一致", "[quote]")
{
    const std::string req = MessageProtocolClient::buildSendTextRequest(
        kPeerId, "cmid-q1", "ok", TextFont(), MakeTextQuote());

    MessageNotify out;
    REQUIRE(RoundTrip(req, out));

    REQUIRE(out.m_quote.m_valid);
    CHECK(out.m_quote.m_msgid   == kQuoteMsgid);
    CHECK(out.m_quote.m_from    == kQuoteFrom);
    CHECK(out.m_quote.m_name    == std::string(kNameUtf8));
    CHECK(out.m_quote.m_msgtype == message_type_text);
    CHECK(out.m_quote.m_digest  == std::string(kDigestUtf8));
}

TEST_CASE("Q2 群聊文本带引用往返后逐字段一致", "[quote]")
{
    const std::string req = MessageProtocolClient::buildSendGroupTextRequest(
        kGroupId, "cmid-q2", "ok", TextFont(), MakeTextQuote());

    MessageNotify out;
    REQUIRE(RoundTrip(req, out));

    REQUIRE(out.m_quote.m_valid);
    CHECK(out.m_quote.m_msgid  == kQuoteMsgid);
    CHECK(out.m_quote.m_digest == std::string(kDigestUtf8));
}

TEST_CASE("Q3 不带引用时整个 quote 字段都不写", "[quote]")
{
    // 与 at 同一策略：不写就是一条普通消息，老版本客户端天然认得。
    const std::string req = MessageProtocolClient::buildSendTextRequest(
        kPeerId, "cmid-q3", "ok");

    CHECK(req.find("quote") == std::string::npos);

    MessageNotify out;
    REQUIRE(RoundTrip(req, out));
    CHECK_FALSE(out.m_quote.m_valid);
}

// =====================================================================
// Q4-Q6：健壮性 —— 对端少写、写错、写成老格式都不能把整条消息毁掉
// =====================================================================

TEST_CASE("Q4 老版本消息没有 quote 字段时按无引用渲染", "[quote]")
{
    // 直接手写一条不含 quote 的推送，模拟老客户端发来的消息。
    const std::string json =
        "{\"msgid\":\"9001\",\"from\":\"10002\",\"ctype\":1,\"cid\":\"5127\","
        "\"msgtype\":1,\"content\":{\"text\":\"hi\"}}";

    MessageNotify out;
    REQUIRE(MessageProtocolClient::parseMessageNotify(json, out));

    CHECK(out.m_text == "hi");
    CHECK_FALSE(out.m_quote.m_valid);   // 不崩、不误判
}

TEST_CASE("Q5 quote 缺 msgid 时按无引用处理", "[quote]")
{
    // 没有可定位的原消息，这条引用毫无意义 —— 与其画一个点不动的引用块，
    // 不如当普通消息渲染。
    const std::string json =
        "{\"msgid\":\"9001\",\"from\":\"10002\",\"ctype\":1,\"cid\":\"5127\","
        "\"msgtype\":1,\"content\":{\"text\":\"hi\",\"quote\":{\"name\":\"x\"}}}";

    MessageNotify out;
    REQUIRE(MessageProtocolClient::parseMessageNotify(json, out));
    CHECK_FALSE(out.m_quote.m_valid);
}

TEST_CASE("Q6 quote 只有 msgid、其余字段缺失时给安全默认值", "[quote]")
{
    // 对端少写字段不该让整条消息渲染不出来：有 msgid 就认这条引用，
    // 名字 / 摘要为空，界面上退化成"只有一条引用线"，仍然可点可跳。
    const std::string json =
        "{\"msgid\":\"9001\",\"from\":\"10002\",\"ctype\":1,\"cid\":\"5127\","
        "\"msgtype\":1,\"content\":{\"text\":\"hi\",\"quote\":{\"msgid\":\"184\"}}}";

    MessageNotify out;
    REQUIRE(MessageProtocolClient::parseMessageNotify(json, out));

    REQUIRE(out.m_quote.m_valid);
    CHECK(out.m_quote.m_msgid == 184);
    CHECK(out.m_quote.m_from == 0);
    CHECK(out.m_quote.m_name.empty());
    CHECK(out.m_quote.m_digest.empty());
    CHECK(out.m_quote.m_msgtype == message_type_text);   // 缺省按文本
}

// =====================================================================
// Q7-Q8：大整数与图片引用
// =====================================================================

TEST_CASE("Q7 大 msgid 经字符串编码往返不丢精度", "[quote]")
{
    // JSON 的数字在不少实现里是 double，超过 2^53 就会丢精度。msgid / from / fileid
    // 一律字符串编码正是为此 —— 这条用例盯着"哪天有人图省事改成写数字"。
    const int64_t kBigMsgid = 9007199254740993LL;   // 2^53 + 1，double 表示不了

    ReplyQuote q = MakeTextQuote();
    q.m_msgid = kBigMsgid;
    q.m_from  = kBigMsgid - 1;

    const std::string req = MessageProtocolClient::buildSendTextRequest(
        kPeerId, "cmid-q7", "ok", TextFont(), q);

    MessageNotify out;
    REQUIRE(RoundTrip(req, out));

    REQUIRE(out.m_quote.m_valid);
    CHECK(out.m_quote.m_msgid == kBigMsgid);
    CHECK(out.m_quote.m_from  == kBigMsgid - 1);
}

TEST_CASE("Q8 图片引用带回 fileid / md5 / 宽高", "[quote]")
{
    // 引用块里要画缩略图：收方据 fileid + md5 命中本地缓存或发起下载，
    // 宽高用来按引用块高度等比缩放。
    ReplyQuote q = MakeTextQuote();
    q.m_msgtype = message_type_image;
    q.m_fileid  = "9021";
    q.m_md5     = "d41d8cd98f00b204e9800998ecf8427e";
    q.m_width   = 800;
    q.m_height  = 600;

    const std::string req = MessageProtocolClient::buildSendTextRequest(
        kPeerId, "cmid-q8", "ok", TextFont(), q);

    MessageNotify out;
    REQUIRE(RoundTrip(req, out));

    REQUIRE(out.m_quote.m_valid);
    CHECK(out.m_quote.m_msgtype == message_type_image);
    CHECK(out.m_quote.m_fileid  == "9021");
    CHECK(out.m_quote.m_md5     == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(out.m_quote.m_width   == 800);
    CHECK(out.m_quote.m_height  == 600);
}

TEST_CASE("Q9 文本引用不写图片那几项", "[quote]")
{
    // 保持落库内容精简：文本引用没有 fileid，就不该凭空写出空字段。
    const std::string req = MessageProtocolClient::buildSendTextRequest(
        kPeerId, "cmid-q9", "ok", TextFont(), MakeTextQuote());

    CHECK(req.find("fileid") == std::string::npos);
    CHECK(req.find("\"md5\"") == std::string::npos);
}

// =====================================================================
// Q10：引用与 @提醒 并存
// =====================================================================

TEST_CASE("Q10 群聊里引用与 @提醒 可以并存且互不干扰", "[quote]")
{
    std::vector<balloonchatwnd::MentionSpan> ats;
    balloonchatwnd::MentionSpan span;
    span.m_uid = 10001;
    span.m_off = 0;
    span.m_len = 7;
    ats.push_back(span);

    const std::string req = MessageProtocolClient::buildSendGroupTextRequest(
        kGroupId, "cmid-q10", "@abc ok", ats, false, TextFont(), MakeTextQuote());

    MessageNotify out;
    REQUIRE(RoundTrip(req, out));

    // 两者在 content 里是各自独立的子对象，谁都不该把谁挤掉。
    REQUIRE(out.m_ats.size() == 1);
    CHECK(out.m_ats[0].m_uid == 10001);
    REQUIRE(out.m_quote.m_valid);
    CHECK(out.m_quote.m_msgid == kQuoteMsgid);
}
