/**
 *  代码片段消息的协议与数据单测（docs/代码片段方案.md §4、§5.1、§5.2），CodeMessageTests.cpp
 *
 *  [codemsg]
 *    CM1 单聊文字请求：代码片段写 content.format = "code" 与 content.lang，Tab 原样保留，不写 font 与 at；
 *        语言为空时不写 lang；纯文字与 Markdown 的请求不受 codeLang 影响
 *    CM2 群聊文字请求的两个重载都按 textFormat 写 format 与 lang
 *    CM3 解析推送：format 为 "code" 的文字消息是代码片段并读出 lang；lang 缺省或类型不对时为空；
 *        Markdown 消息不读 lang；非文字消息带了 format 也按纯文字
 *    CM4 textContentBytes 计入 format 与 lang：比纯文字恰好多出 ,"format":"code","lang":"cpp" 的字节数
 *    CM5 发送前计量的体积与请求里实际的 content 逐字节一致
 *    CM6 本地库 content 列（BuildContentJson）为代码片段写 format 与 lang，语言为空时只写 format
 *    CM7 ApplyCodeFields：代码片段按语言标识（含别名）填 isCode 与 codeLang，不认识的语言为纯文本，其它格式不是代码片段
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonChatWnd/ChatCodeMessage.h"
#include "../Common/Code/CodeLexer.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace {

//协议用例里的对端 / 群 id 与客户端消息 id，取值任意
const int64_t kPeerUid = 20001;
const int64_t kGroupId = 30001;
const char* const kClientMsgId = "code-test-0001";

//代码原文：两行，第二行以 Tab 缩进（UTF-8）
const char* const kCodeUtf8 = "int main()\n{\n\treturn 0;\n}";

//把 JSON 文本解析成 Json::Value；格式不对时返回 false
bool ParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本（与 MessageProtocolClient 的写法相同：不缩进）
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//拼一条服务端格式的单聊推送，content 由调用方给出
std::string MakeNotifyJson(int msgtype, const Json::Value& content)
{
    Json::Value root;
    root["msgid"] = "900002";
    root["clientmsgid"] = kClientMsgId;
    root["ctype"] = conversation_single;
    root["cid"] = std::to_string(kPeerUid);
    root["from"] = std::to_string(kPeerUid);
    root["to"] = "10001";
    root["msgtype"] = msgtype;
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(1790000000000LL);
    return WriteCompact(root);
}

} // anonymous namespace

TEST_CASE("CM1 单聊文字请求：代码片段写 format 与 lang，其它格式不受影响", "[codemsg]")
{
    Json::Value code;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kCodeUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_code, "cpp"), code));
    CHECK(code["msgtype"].asInt() == message_type_text);
    CHECK(code["content"]["format"].asString() == "code");
    CHECK(code["content"]["lang"].asString() == "cpp");
    CHECK(code["content"]["text"].asString() == kCodeUtf8);
    CHECK_FALSE(code["content"].isMember("font"));
    CHECK_FALSE(code["content"].isMember("at"));

    //语言为空：只写 format，不写 lang
    Json::Value noLang;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kCodeUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_code), noLang));
    CHECK(noLang["content"]["format"].asString() == "code");
    CHECK_FALSE(noLang["content"].isMember("lang"));

    //纯文字与 Markdown：即使传了 codeLang 也不写 lang
    Json::Value plain;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kCodeUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_plain, "cpp"), plain));
    CHECK_FALSE(plain["content"].isMember("format"));
    CHECK_FALSE(plain["content"].isMember("lang"));

    Json::Value md;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kCodeUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_markdown, "cpp"), md));
    CHECK(md["content"]["format"].asString() == "markdown");
    CHECK_FALSE(md["content"].isMember("lang"));
}

TEST_CASE("CM2 群聊文字请求的两个重载都写 format 与 lang", "[codemsg]")
{
    Json::Value a;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendGroupTextRequest(
        kGroupId, kClientMsgId, kCodeUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_code, "python"), a));
    CHECK(a["ctype"].asInt() == conversation_group);
    CHECK(a["content"]["format"].asString() == "code");
    CHECK(a["content"]["lang"].asString() == "python");

    const std::vector<balloonchatwnd::MentionSpan> noAts;
    Json::Value b;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendGroupTextRequest(
        kGroupId, kClientMsgId, kCodeUtf8, noAts, false, net::TextFont(), net::ReplyQuote(),
        net::text_format_code, "sql"), b));
    CHECK(b["content"]["format"].asString() == "code");
    CHECK(b["content"]["lang"].asString() == "sql");
    CHECK_FALSE(b["content"].isMember("at"));
}

TEST_CASE("CM3 解析推送：format 为 code 的文字消息是代码片段并读出 lang", "[codemsg]")
{
    SECTION("format 为 code、带 lang")
    {
        Json::Value content;
        content["text"] = kCodeUtf8;
        content["format"] = "code";
        content["lang"] = "cpp";
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK(n.m_textFormat == net::text_format_code);
        CHECK(n.m_codeLang == "cpp");
        CHECK(n.m_text == kCodeUtf8);
    }

    SECTION("format 为 code、没有 lang 或 lang 类型不对：语言为空")
    {
        Json::Value content;
        content["text"] = kCodeUtf8;
        content["format"] = "code";
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK(n.m_textFormat == net::text_format_code);
        CHECK(n.m_codeLang.empty());

        content["lang"] = 3;
        net::MessageNotify n2;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n2));
        CHECK(n2.m_textFormat == net::text_format_code);
        CHECK(n2.m_codeLang.empty());
    }

    SECTION("Markdown 消息带了 lang 也不读")
    {
        Json::Value content;
        content["text"] = kCodeUtf8;
        content["format"] = "markdown";
        content["lang"] = "cpp";
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK(n.m_textFormat == net::text_format_markdown);
        CHECK(n.m_codeLang.empty());
    }

    SECTION("系统消息带了 format 也按纯文字")
    {
        Json::Value content;
        content["text"] = "tip";
        content["format"] = "code";
        content["lang"] = "cpp";
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_system, content), n));
        CHECK(n.m_textFormat == net::text_format_plain);
        CHECK(n.m_codeLang.empty());
    }
}

TEST_CASE("CM4 textContentBytes 计入 format 与 lang", "[codemsg]")
{
    const std::vector<balloonchatwnd::MentionSpan> noAts;
    const size_t plain = net::MessageProtocolClient::textContentBytes(kCodeUtf8, net::TextFont(), net::ReplyQuote(),
                                                                       noAts, false);
    const size_t code = net::MessageProtocolClient::textContentBytes(kCodeUtf8, net::TextFont(), net::ReplyQuote(),
                                                                      noAts, false, net::text_format_code, "cpp");
    const std::string extra = ",\"format\":\"code\",\"lang\":\"cpp\"";
    CHECK(code == plain + extra.size());
}

TEST_CASE("CM5 发送前计量的体积与请求里的 content 一致", "[codemsg]")
{
    const std::vector<balloonchatwnd::MentionSpan> noAts;
    net::ReplyQuote quote;
    quote.m_valid = true;
    quote.m_msgid = 900001;
    quote.m_from = kPeerUid;
    quote.m_digest = "int main()";
    Json::Value request;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kCodeUtf8, net::TextFont(), quote, net::text_format_code, "cpp"), request));
    const size_t measured = net::MessageProtocolClient::textContentBytes(kCodeUtf8, net::TextFont(), quote, noAts, false,
                                                                          net::text_format_code, "cpp");
    CHECK(WriteCompact(request["content"]).size() == measured);
}

TEST_CASE("CM6 本地库 content 列为代码片段写 format 与 lang", "[codemsg]")
{
    net::MessageNotify n;
    n.m_msgtype = message_type_text;
    n.m_text = kCodeUtf8;
    n.m_textFormat = net::text_format_code;
    n.m_codeLang = "rust";
    Json::Value code;
    REQUIRE(ParseJsonText(balloonhistory::BuildContentJson(n), code));
    CHECK(code["format"].asString() == "code");
    CHECK(code["lang"].asString() == "rust");
    CHECK(code["text"].asString() == kCodeUtf8);

    n.m_codeLang.clear();
    Json::Value noLang;
    REQUIRE(ParseJsonText(balloonhistory::BuildContentJson(n), noLang));
    CHECK(noLang["format"].asString() == "code");
    CHECK_FALSE(noLang.isMember("lang"));

    n.m_textFormat = net::text_format_plain;
    n.m_codeLang = "rust";
    Json::Value plain;
    REQUIRE(ParseJsonText(balloonhistory::BuildContentJson(n), plain));
    CHECK_FALSE(plain.isMember("format"));
    CHECK_FALSE(plain.isMember("lang"));
}

TEST_CASE("CM7 ApplyCodeFields 按格式与语言标识填写代码片段字段", "[codemsg]")
{
    balloonchatwnd::ChatMessage msg;
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_code, "cpp");
    CHECK(msg.isCode);
    CHECK(msg.codeLang == codesnippet::code_lang_cpp);

    //别名与大小写
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_code, "PY");
    CHECK(msg.isCode);
    CHECK(msg.codeLang == codesnippet::code_lang_python);

    //不认识的语言与空语言：仍是代码片段，按纯文本显示
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_code, "cobol");
    CHECK(msg.isCode);
    CHECK(msg.codeLang == codesnippet::code_lang_plain);
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_code, "");
    CHECK(msg.isCode);
    CHECK(msg.codeLang == codesnippet::code_lang_plain);

    //其它格式：不是代码片段
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_markdown, "cpp");
    CHECK_FALSE(msg.isCode);
    CHECK(msg.codeLang == codesnippet::code_lang_plain);
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_plain, "cpp");
    CHECK_FALSE(msg.isCode);
}
