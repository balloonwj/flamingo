/**
 *  AutoReplyProtocolTests.cpp
 *
 *  自动回复在协议层的测试：发送请求体带 autoreply 标记与「[自动回复] 」前缀（ARC1），解析推送时
 *  识别这个标记（ARC2），以及回执成功后本端补的那份回显与服务端推送格式一致（ARC3）。
 *  ARC1、ARC2 是与用户确认过的用例（2026-10-03），ARC3 覆盖实现时新增的回显函数。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不连网络。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"   // conversation_single / message_type_*
#include "../third-party/jsoncpp-1.9.0/json.h"

using net::MessageNotify;
using net::MessageProtocolClient;

namespace {

// 本人与对端的 userid、回执里的 msgid 与时间，取值任意（时间为 2026 年的某个毫秒时刻）
const int64_t kSelfUid  = 10001;
const int64_t kPeerUid  = 10002;
const int64_t kAckMsgid = 900777;
const int64_t kAckTime  = 1790000000000LL;

// 测试用的客户端消息ID
const char* const kClientMsgId = "autoreply-test-0001";

// 回复文字（UTF-8，按字节写出，不依赖源文件编码）：「我在开会」
const char* const kReplyUtf8 = "\xE6\x88\x91\xE5\x9C\xA8\xE5\xBC\x80\xE4\xBC\x9A";

// 前缀的字节形式：「[自动回复] 」，用来核对 kAutoReplyTextPrefix 本身没有写错
const char* const kPrefixUtf8 = "[\xE8\x87\xAA\xE5\x8A\xA8\xE5\x9B\x9E\xE5\xA4\x8D] ";

// 把 JSON 文本解析成 Json::Value，供断言检查构造产物
bool ParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

// 把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的推送报文
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

// 拼一条服务端格式的单聊推送，content 由调用方给出
std::string MakeNotifyJson(int msgtype, const Json::Value& content)
{
    Json::Value root;
    root["msgid"]       = std::to_string(kAckMsgid);
    root["clientmsgid"] = kClientMsgId;
    root["ctype"]       = conversation_single;
    root["cid"]         = std::to_string(kPeerUid);
    root["from"]        = std::to_string(kPeerUid);
    root["to"]          = std::to_string(kSelfUid);
    root["msgtype"]     = msgtype;
    root["content"]     = content;
    root["timestamp"]   = static_cast<Json::Int64>(kAckTime);
    root["recalled"]    = false;
    return WriteCompact(root);
}

} // anonymous namespace

TEST_CASE("ARC1 自动回复的请求体带 autoreply 标记，正文以「[自动回复] 」开头", "[autoreply][autoreplyproto]")
{
    CHECK(std::string(net::kAutoReplyTextPrefix) == kPrefixUtf8);

    const std::string body =
        MessageProtocolClient::buildSendAutoReplyRequest(kPeerUid, kClientMsgId, kReplyUtf8);
    Json::Value root;
    REQUIRE(ParseJsonText(body, root));
    CHECK(root["ctype"].asInt() == conversation_single);
    CHECK(root["cid"].asString() == std::to_string(kPeerUid));
    CHECK(root["clientmsgid"].asString() == kClientMsgId);
    CHECK(root["msgtype"].asInt() == message_type_text);
    REQUIRE(root["content"].isObject());
    CHECK(root["content"]["text"].asString() == std::string(kPrefixUtf8) + kReplyUtf8);
    REQUIRE(root["content"]["autoreply"].isBool());
    CHECK(root["content"]["autoreply"].asBool());

    //普通单聊文本不带这个字段
    Json::Value plain;
    REQUIRE(ParseJsonText(MessageProtocolClient::buildSendTextRequest(kPeerUid, kClientMsgId, kReplyUtf8),
                          plain));
    CHECK_FALSE(plain["content"].isMember("autoreply"));
    CHECK(plain["content"]["text"].asString() == kReplyUtf8);
}

TEST_CASE("ARC2 解析推送时识别 autoreply 标记，不带这个字段的普通消息为否", "[autoreply][autoreplyproto]")
{
    SECTION("带 autoreply=true 的文本：识别为自动回复")
    {
        Json::Value content;
        content["text"] = std::string(kPrefixUtf8) + kReplyUtf8;
        content["autoreply"] = true;
        MessageNotify n;
        REQUIRE(MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK(n.m_autoReply);
        CHECK(n.m_text == std::string(kPrefixUtf8) + kReplyUtf8);
    }

    SECTION("普通文本：不是自动回复")
    {
        Json::Value content;
        content["text"] = kReplyUtf8;
        MessageNotify n;
        REQUIRE(MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK_FALSE(n.m_autoReply);
    }

    SECTION("正文碰巧以前缀开头但没有标记：不是自动回复（以标记为准，不按前缀判断）")
    {
        Json::Value content;
        content["text"] = std::string(kPrefixUtf8) + kReplyUtf8;
        MessageNotify n;
        REQUIRE(MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK_FALSE(n.m_autoReply);
    }

    SECTION("标记不是布尔 true：一律按普通消息处理")
    {
        Json::Value asString;
        asString["text"] = kReplyUtf8;
        asString["autoreply"] = "true";
        Json::Value asNumber;
        asNumber["text"] = kReplyUtf8;
        asNumber["autoreply"] = 1;
        Json::Value asFalse;
        asFalse["text"] = kReplyUtf8;
        asFalse["autoreply"] = false;
        const Json::Value samples[] = { asString, asNumber, asFalse };
        for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
        {
            INFO("样本 index = " << i);
            MessageNotify n;
            REQUIRE(MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, samples[i]), n));
            CHECK_FALSE(n.m_autoReply);
        }
    }

    SECTION("非文本消息带了标记：不认")
    {
        Json::Value content;
        content["fileid"] = "123";
        content["md5"] = "0123456789abcdef0123456789abcdef";
        content["autoreply"] = true;
        MessageNotify n;
        REQUIRE(MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_image, content), n));
        CHECK_FALSE(n.m_autoReply);
    }
}

TEST_CASE("ARC3 本端回显与服务端推送同格式，再解析一遍结果不变", "[autoreply][autoreplyproto]")
{
    MessageNotify echo;
    REQUIRE(MessageProtocolClient::buildAutoReplyEcho(kSelfUid, kPeerUid, kClientMsgId, kAckMsgid,
                                                     kAckTime, kReplyUtf8, echo));
    //发送方视角：from 是本人，会话 ID 与收件人都是对端
    CHECK(echo.m_from == kSelfUid);
    CHECK(echo.m_cid == kPeerUid);
    CHECK(echo.m_to == kPeerUid);
    CHECK(echo.m_ctype == conversation_single);
    CHECK(echo.m_msgtype == message_type_text);
    CHECK(echo.m_msgid == kAckMsgid);
    CHECK(echo.m_timestamp == kAckTime);
    CHECK(echo.m_clientmsgid == kClientMsgId);
    CHECK(echo.m_text == std::string(kPrefixUtf8) + kReplyUtf8);
    CHECK(echo.m_autoReply);
    CHECK_FALSE(echo.m_recalled);

    //原始 JSON 存进本地消息库后，打开聊天窗时再解析一遍，得到的是同一条消息
    REQUIRE_FALSE(echo.m_rawJson.empty());
    MessageNotify again;
    REQUIRE(MessageProtocolClient::parseMessageNotify(echo.m_rawJson, again));
    CHECK(again.m_from == echo.m_from);
    CHECK(again.m_cid == echo.m_cid);
    CHECK(again.m_msgid == echo.m_msgid);
    CHECK(again.m_text == echo.m_text);
    CHECK(again.m_autoReply);
    CHECK(again.m_rawJson == echo.m_rawJson);

    //参数不合法时不拼
    MessageNotify bad;
    CHECK_FALSE(MessageProtocolClient::buildAutoReplyEcho(0, kPeerUid, kClientMsgId, kAckMsgid,
                                                          kAckTime, kReplyUtf8, bad));
    CHECK_FALSE(MessageProtocolClient::buildAutoReplyEcho(kSelfUid, 0, kClientMsgId, kAckMsgid,
                                                          kAckTime, kReplyUtf8, bad));
    CHECK_FALSE(MessageProtocolClient::buildAutoReplyEcho(kSelfUid, kPeerUid, kClientMsgId, 0,
                                                          kAckTime, kReplyUtf8, bad));
}
