/**
 *  聊天正文里的"JSON 危险字符"转义单元测试（Suite MP-ESC），MessageEscapeTests.cpp
 *
 *  由来：有人问"聊天内容里出现类似 JSON 的特殊字符，会不会把协议解析搞崩"。
 *  答案是不会 —— 组包全程走 jsoncpp 的 Json::Value + StreamWriterBuilder，
 *  正文是被<u>塞进</u>对象的字段值，而不是拼进字符串，转义由 jsoncpp 负责。
 *  本组用例把这条性质钉住：任何人把组包方式改回手工拼串，这里必然报红。
 *
 *  与既有 MP5 的分工：MP5 覆盖中文 / emoji / 引号 / 换行 / 单个反斜杠，
 *  本组补它没测到的几类 ——
 *    · 正文本身就是一段 JSON 文本（最容易想当然出事的一种）
 *    · 反斜杠结尾、连续反斜杠（拼串写法下会把收尾引号转义掉）
 *    · 制表符 / 回车 / 退格等控制字符
 *    · 群聊路径（MP5 只测了单聊）
 *    · 引用块的 name / digest 字段（同样承载用户输入）
 *
 *  判据统一是 round-trip：build 出来的报文必须是合法 JSON，且反解回来的
 *  字段与原文<u>逐字节相同</u>。不去断言转义后的具体形态（那是 jsoncpp 的
 *  实现细节，允许它选择 \u001B 还是别的等价写法）。
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-16
 */
#include "catch_amalgamated.hpp"
#include "../net/MessageProtocolClient.h"
//见 MessageProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace net;

namespace {

//把 JSON 文本解析成 Json::Value。解析失败返回 false —— 用例正是靠它来确认
//"带特殊字符的正文组出来的仍是合法 JSON"。
bool ParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//单聊：组包 → 解析 → 取回 content.text，供各用例比对
std::string RoundTripSingleText(const std::string& text)
{
    std::string body = MessageProtocolClient::buildSendTextRequest(5002, "cmid-esc", text);
    Json::Value root;
    if (!ParseJsonText(body, root))
    {
        return std::string("<报文不是合法 JSON>");
    }
    return root["content"]["text"].asString();
}

} // namespace

TEST_CASE("MP-ESC1 正文本身是一段 JSON 文本，round-trip 后逐字节还原")
{
    //这是提问者最担心的一种：用户把一段 JSON 当聊天内容发出去。
    //只要组包不是拼串，它就只是一串普通字符，不会与外层报文的结构混淆。
    const std::string tricky = "{\"text\":\"伪造的内层\",\"msgtype\":99,\"arr\":[1,2,{\"k\":null}]}";
    CHECK(RoundTripSingleText(tricky) == tricky);

    //只有半截、根本不成对的括号与引号，同样不能影响外层解析
    CHECK(RoundTripSingleText("}{ \" : , ] [") == "}{ \" : , ] [");
    CHECK(RoundTripSingleText("\"") == "\"");
}

TEST_CASE("MP-ESC2 反斜杠结尾与连续反斜杠")
{
    //拼串写法下最典型的翻车点：正文末尾的反斜杠会把收尾的引号转义掉，
    //整个报文从这里开始错位。
    CHECK(RoundTripSingleText("C:\\path\\") == "C:\\path\\");
    CHECK(RoundTripSingleText("\\") == "\\");
    CHECK(RoundTripSingleText("\\\\\\\\") == "\\\\\\\\");
    //反斜杠紧跟引号：\" 在 JSON 里是转义序列，必须原样还原成两个字符
    CHECK(RoundTripSingleText("\\\"") == "\\\"");
}

TEST_CASE("MP-ESC3 制表符、回车、退格等控制字符")
{
    //JSON 规范要求 0x20 以下的字符必须转义，jsoncpp 会写成 \t \r \b 或 \uXXXX，
    //本用例只关心还原后一致，不关心它选了哪种等价写法。
    CHECK(RoundTripSingleText("a\tb") == "a\tb");
    CHECK(RoundTripSingleText("a\r\nb") == "a\r\nb");
    CHECK(RoundTripSingleText("a\bb\fc") == "a\bb\fc");
    CHECK(RoundTripSingleText(std::string("ctrl:\x01\x1F")) == std::string("ctrl:\x01\x1F"));
}

TEST_CASE("MP-ESC4 群聊路径同样安全")
{
    //MP5 只测了单聊。群聊走的是另一个组包函数，同样不能拼串。
    const std::string tricky = "群里发一段 {\"a\":\"b\"} 和一个反斜杠 \\";
    std::string body = MessageProtocolClient::buildSendGroupTextRequest(700001, "cmid-g", tricky);

    Json::Value root;
    REQUIRE(ParseJsonText(body, root));
    CHECK(root["content"]["text"].asString() == tricky);
    //顺带确认外层字段没有被正文里的字符带偏
    CHECK(root["cid"].asString() == "700001");
    CHECK(root["clientmsgid"].asString() == "cmid-g");
}

TEST_CASE("MP-ESC5 引用块的显示名与摘要也承载用户输入")
{
    //引用（回复）时会把被引用消息的发送者名与正文摘要快照进 content.quote，
    //这两个字段同样是用户可控文本，走的也必须是 jsoncpp 的转义。
    ReplyQuote quote;
    quote.m_valid   = true;
    quote.m_msgid   = 30001;
    quote.m_from    = 5001;
    quote.m_name    = "张\"三\"\\";
    quote.m_msgtype = 1;
    quote.m_digest  = "{\"fake\":\"digest\"}\n第二行";

    std::string body = MessageProtocolClient::buildSendTextRequest(
        5002, "cmid-q", "正文也带引号 \"", TextFont(), quote);

    Json::Value root;
    REQUIRE(ParseJsonText(body, root));
    CHECK(root["content"]["text"].asString() == "正文也带引号 \"");
    CHECK(root["content"]["quote"]["name"].asString() == quote.m_name);
    CHECK(root["content"]["quote"]["digest"].asString() == quote.m_digest);
    //64 位 id 仍按字符串编码，不因为上面那些字符而变形
    CHECK(root["content"]["quote"]["msgid"].asString() == "30001");
}

TEST_CASE("MP-ESC6 接收侧：推送报文里的危险字符原样还原")
{
    //发送侧安全不代表接收侧安全，这里把同一批文本塞进推送报文再解析。
    const std::string tricky = "{\"x\":1} \\ \" \t 结尾反斜杠\\";

    Json::Value n;
    n["msgid"]     = "30002";
    n["from"]      = "5001";
    n["cid"]       = "5001";
    n["to"]        = "5002";
    n["ctype"]     = 1;
    n["msgtype"]   = 1;
    n["timestamp"] = static_cast<Json::Int64>(0);
    Json::Value content;
    content["text"] = tricky;
    n["content"]    = content;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string notifyBody = Json::writeString(wb, n);

    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(notifyBody, notify));
    CHECK(notify.m_text == tricky);
}
