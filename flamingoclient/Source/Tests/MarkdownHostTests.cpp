/**
 *  Markdown 消息 exe 侧的单元测试（docs/Markdown消息方案.md 第 8 节）：
 *
 *  宿主（Feature/Markdown/MarkdownModuleHost，标签 [mdhost]）：
 *    - MH1 函数表来源正常时预加载完成后状态为「可用」，m_init 只调用一次且在后台线程上
 *    - MH2 DLL 不存在时状态为「不可用」，原因为找不到文件
 *    - MH3 加载一个不是本模块的系统 DLL（version.dll）时状态为「不可用」
 *    - MH4 函数表 m_cbSize 小于最小值时状态为「不可用」，不调用 m_init
 *    - MH5 m_init 返回失败时状态为「不可用」
 *    - MH6 预加载只执行一次：第二次 StartPreload 不再调用函数表来源
 *
 *  协议（net/MessageProtocolClient 与本地库，标签 [mdproto]）：
 *    - MP1 单聊文字请求：Markdown 时 content.format 为 "markdown"，纯文字时不写 format
 *    - MP2 群聊文字请求的两个重载都按 textFormat 写 format
 *    - MP3 解析推送：format 为 "markdown" 的文字消息是 Markdown；缺省、不认识的取值、非文字消息都按纯文字
 *    - MP4 textContentBytes 计入 format：Markdown 比纯文字恰好多出 ,"format":"markdown" 的字节数
 *    - MP5 本地库 content 列（BuildContentJson）为 Markdown 消息写 format，纯文字不写
 *
 *  宿主用一张假函数表或直接编进来的 DLL 函数表（flamingomd::GetMdApiTable），不依赖 FlamingoMarkdown.dll 文件。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Feature/Markdown/MarkdownModuleHost.h"
#include "../../FlamingoMarkdown/MdApiTable.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace markdown;

namespace {

//等待后台线程的最长时间（毫秒）
const DWORD kWaitMs = 5000;

//协议用例里的对端 / 群 id 与客户端消息 id，取值任意
const int64_t kPeerUid = 20001;
const int64_t kGroupId = 30001;
const char* const kClientMsgId = "markdown-test-0001";

//Markdown 源文本：「## 周报\n- **完成** 登录优化」（UTF-8 字节写出，不依赖源文件编码）
const char* const kMarkdownUtf8 = "## \xE5\x91\xA8\xE6\x8A\xA5\n- **\xE5\xAE\x8C\xE6\x88\x90** \xE7\x99\xBB\xE5\xBD\x95\xE4\xBC\x98\xE5\x8C\x96";

//假函数表的可配置状态与调用记录
struct FakeMdState
{
    volatile long m_providerCalls;   //函数表来源被调用的次数
    volatile long m_initCalls;       //m_init 被调用的次数
    DWORD         m_initThread;      //m_init 所在的线程
    int           m_initResult;      //m_init 的返回值
    uint32_t      m_cbSize;          //函数表来源返回的表的 m_cbSize
};

FakeMdState g_fake;

//假函数表：以真实函数表为底，只换掉 m_init 与 m_cbSize
flamingomd::MdApi g_fakeApi;

int __cdecl FakeInit(const flamingomd::MdHostInfo* host)
{
    (void)host;
    ::InterlockedIncrement(&g_fake.m_initCalls);
    g_fake.m_initThread = ::GetCurrentThreadId();
    return g_fake.m_initResult;
}

//恢复假函数表的默认状态
void ResetFake()
{
    g_fake.m_providerCalls = 0;
    g_fake.m_initCalls = 0;
    g_fake.m_initThread = 0;
    g_fake.m_initResult = flamingomd::md_ok;
    g_fake.m_cbSize = sizeof(flamingomd::MdApi);
}

//函数表来源：返回假函数表
const flamingomd::MdApi* FakeProvider(void* context, const char** failure)
{
    (void)context;
    (void)failure;
    ::InterlockedIncrement(&g_fake.m_providerCalls);
    g_fakeApi = *flamingomd::GetMdApiTable();
    g_fakeApi.m_init = &FakeInit;
    g_fakeApi.m_cbSize = g_fake.m_cbSize;
    return &g_fakeApi;
}

//测试程序所在目录（以反斜杠结尾）
std::wstring ExeDirectory()
{
    wchar_t path[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring dir(path);
    const size_t pos = dir.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? std::wstring() : dir.substr(0, pos + 1);
}

//把 JSON 文本解析成 Json::Value
bool ParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本
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
    root["msgid"] = "900001";
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

} // namespace

TEST_CASE("MH1 函数表来源正常时预加载完成后可用，m_init 只调用一次且在后台线程上", "[mdhost]")
{
    ResetFake();
    CMarkdownModuleHost host;
    CHECK(host.GetState() == md_host_not_started);
    CHECK_FALSE(host.CanUse());
    host.StartPreloadWithProvider(&FakeProvider, NULL);
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == md_host_ready);
    CHECK(host.CanUse());
    CHECK(host.GetApi() != NULL);
    CHECK(host.GetFailure().empty());
    CHECK(g_fake.m_initCalls == 1);
    CHECK(g_fake.m_initThread != ::GetCurrentThreadId());
}

TEST_CASE("MH2 DLL 不存在时不可用，原因为找不到文件", "[mdhost]")
{
    CMarkdownModuleHost host;
    host.StartPreload(ExeDirectory() + L"NoSuchFlamingoMarkdown_test.dll");
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == md_host_unavailable);
    CHECK_FALSE(host.CanUse());
    CHECK(host.GetApi() == NULL);
    CHECK_FALSE(host.GetFailure().empty());
}

TEST_CASE("MH3 加载不是本模块的系统 DLL 时不可用", "[mdhost]")
{
    wchar_t systemDir[MAX_PATH] = { 0 };
    REQUIRE(::GetSystemDirectoryW(systemDir, MAX_PATH) > 0);
    CMarkdownModuleHost host;
    host.StartPreload(std::wstring(systemDir) + L"\\version.dll");
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == md_host_unavailable);
    CHECK(host.GetApi() == NULL);
}

TEST_CASE("MH4 函数表过小时不可用，不调用 m_init", "[mdhost]")
{
    ResetFake();
    g_fake.m_cbSize = flamingomd::kMdApiMinSize - sizeof(void*);
    CMarkdownModuleHost host;
    host.StartPreloadWithProvider(&FakeProvider, NULL);
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == md_host_unavailable);
    CHECK(g_fake.m_initCalls == 0);
    CHECK(host.GetFailure() == "api table too small");
}

TEST_CASE("MH5 m_init 失败时不可用", "[mdhost]")
{
    ResetFake();
    g_fake.m_initResult = flamingomd::md_internal_error;
    CMarkdownModuleHost host;
    host.StartPreloadWithProvider(&FakeProvider, NULL);
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == md_host_unavailable);
    CHECK(g_fake.m_initCalls == 1);
    CHECK(host.GetApi() == NULL);
}

TEST_CASE("MH6 预加载只执行一次", "[mdhost]")
{
    ResetFake();
    CMarkdownModuleHost host;
    host.StartPreloadWithProvider(&FakeProvider, NULL);
    REQUIRE(host.WaitForPreload(kWaitMs));
    host.StartPreloadWithProvider(&FakeProvider, NULL);
    host.StartPreload(L"C:\\never\\used.dll");
    CHECK(host.WaitForPreload(kWaitMs));
    CHECK(g_fake.m_providerCalls == 1);
    CHECK(host.GetState() == md_host_ready);
}

TEST_CASE("MP1 单聊文字请求：Markdown 写 content.format，纯文字不写", "[mdproto]")
{
    Json::Value md;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kMarkdownUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_markdown), md));
    CHECK(md["msgtype"].asInt() == message_type_text);
    CHECK(md["content"]["format"].asString() == "markdown");
    CHECK(md["content"]["text"].asString() == kMarkdownUtf8);

    Json::Value plain;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(kPeerUid, kClientMsgId, kMarkdownUtf8), plain));
    CHECK_FALSE(plain["content"].isMember("format"));
}

TEST_CASE("MP2 群聊文字请求的两个重载都按 textFormat 写 format", "[mdproto]")
{
    Json::Value a;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendGroupTextRequest(
        kGroupId, kClientMsgId, kMarkdownUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_markdown), a));
    CHECK(a["content"]["format"].asString() == "markdown");

    std::vector<balloonchatwnd::MentionSpan> ats;
    Json::Value b;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendGroupTextRequest(
        kGroupId, kClientMsgId, kMarkdownUtf8, ats, true, net::TextFont(), net::ReplyQuote(),
        net::text_format_markdown), b));
    CHECK(b["content"]["format"].asString() == "markdown");
    CHECK(b["content"].isMember("at"));

    Json::Value c;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendGroupTextRequest(
        kGroupId, kClientMsgId, kMarkdownUtf8, ats, true), c));
    CHECK_FALSE(c["content"].isMember("format"));
}

TEST_CASE("MP3 解析推送：只有文字消息的 format=\"markdown\" 才是 Markdown", "[mdproto]")
{
    SECTION("format 为 markdown 的文字消息")
    {
        Json::Value content;
        content["text"] = kMarkdownUtf8;
        content["format"] = "markdown";
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK(n.m_textFormat == net::text_format_markdown);
        CHECK(n.m_text == kMarkdownUtf8);
    }

    SECTION("没有 format：纯文字")
    {
        Json::Value content;
        content["text"] = kMarkdownUtf8;
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK(n.m_textFormat == net::text_format_plain);
    }

    SECTION("不认识的取值或类型不对：纯文字")
    {
        Json::Value content;
        content["text"] = kMarkdownUtf8;
        content["format"] = "html";
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, content), n));
        CHECK(n.m_textFormat == net::text_format_plain);

        Json::Value wrongType;
        wrongType["text"] = kMarkdownUtf8;
        wrongType["format"] = 1;
        net::MessageNotify n2;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_text, wrongType), n2));
        CHECK(n2.m_textFormat == net::text_format_plain);
    }

    SECTION("系统消息带了 format 也按纯文字")
    {
        Json::Value content;
        content["text"] = "tip";
        content["format"] = "markdown";
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(message_type_system, content), n));
        CHECK(n.m_textFormat == net::text_format_plain);
    }
}

TEST_CASE("MP4 textContentBytes 计入 format 字段", "[mdproto]")
{
    const std::vector<balloonchatwnd::MentionSpan> noAts;
    const size_t plain = net::MessageProtocolClient::textContentBytes(kMarkdownUtf8, net::TextFont(),
                                                                       net::ReplyQuote(), noAts, false);
    const size_t md = net::MessageProtocolClient::textContentBytes(kMarkdownUtf8, net::TextFont(), net::ReplyQuote(),
                                                                    noAts, false, net::text_format_markdown);
    const std::string extra = ",\"format\":\"markdown\"";
    CHECK(md == plain + extra.size());
}

TEST_CASE("MP5 本地库 content 列为 Markdown 消息写 format", "[mdproto]")
{
    net::MessageNotify n;
    n.m_msgtype = message_type_text;
    n.m_text = kMarkdownUtf8;
    n.m_textFormat = net::text_format_markdown;
    Json::Value md;
    REQUIRE(ParseJsonText(balloonhistory::BuildContentJson(n), md));
    CHECK(md["format"].asString() == "markdown");
    CHECK(md["text"].asString() == kMarkdownUtf8);

    n.m_textFormat = net::text_format_plain;
    Json::Value plain;
    REQUIRE(ParseJsonText(balloonhistory::BuildContentJson(n), plain));
    CHECK_FALSE(plain.isMember("format"));
}
