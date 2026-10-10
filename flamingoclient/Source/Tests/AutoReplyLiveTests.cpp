/**
 *  「离开 / 忙碌时自动回复」的端到端联调用例（ARL1，标签 [.live][autoreplylive]），AutoReplyLiveTests.cpp
 *
 *  与用户确认过的 ARL1（2026-10-03）：B 设为离开，A 连发两条，A 收到两条带前缀的自动回复；A 发来的
 *  如果本身是自动回复，B 不再回复（两边不会互相无限回复）；服务端存下了这两条自动回复。
 *  自动回复的判定与发送在 PC 客户端的主面板里，所以 B 必须是真实的 PC 客户端；A 用 LiveChatClient
 *  直接收发 v2 帧，A 的「自动回复」用它直接发一条带 autoreply 标记的消息来模拟。
 *  「B 自己的消息记录里也有这两条」查的是 B 的本地消息库，不在本用例里，由联调脚本另行核对。
 *
 *  默认不参与常规测试：用例带 [.live] 标签，Catch2 会跳过。跑之前：
 *    1. 起 tmp/relaytest 的测试服务端（start_server.ps1）；
 *    2. 在 tmp/relaytest/clientB 用 MY0308 登录 PC 客户端，系统设置「聊天」页打开「离开时自动回复」，
 *       主面板状态切到「离开」，并且<u>不要</u>把与 MY0127 的聊天窗放在前台（正看着时不回复）；
 *    3. 不要开着 tmp/relaytest/clientA（同一账号同类终端重复登录会互相顶下线）。
 *  然后：
 *
 *      FlamingoUnit.exe "[autoreplylive]"
 *
 *  账号：MY0127（5127，作 A）与 MY0308（5308，作 B），密码都是 perf1234。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <cstring>   // strlen
#include <string>
#include "LiveChatClient.h"
#include "../net/MessageProtocolClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;

namespace {

//tmp/relaytest 测试服务端的 chatserver 端口
const unsigned short kRelayTestServerPort = 28000;

//A 与 B 的账号、密码与 userid
const char* kAccountA = "MY0127";
const char* kPassword = "perf1234";
const int64_t kUserB = 5308;

//等一条应答的最长时间，单位毫秒
const int kWaitMs = 5000;

//等 B 自动回复的最长时间，单位毫秒：B 收到推送、判定、发出、服务端再推给 A，正常不到一秒
const int kAutoReplyWaitMs = 8000;

//确认 B「没有」回复时等待的时间，单位毫秒：比正常的往返时间长得多，等满了还没来就算没回
const int kNoReplyWaitMs = 4000;

//A 连发的条数
const int kBurstCount = 2;

//拉历史时取最近几条：本用例发出与收到的都在最新的十条里
const int kHistoryLimit = 10;

//自动回复正文的前缀（UTF-8 字节）：「[自动回复] 」
const char* const kPrefixUtf8 = "[\xE8\x87\xAA\xE5\x8A\xA8\xE5\x9B\x9E\xE5\xA4\x8D] ";

bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

std::string toStr(int64_t v)
{
    char buf[32] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lld", (long long)v);
    return std::string(buf);
}

//本轮用例的 clientmsgid：带开机以来的毫秒数，重复跑不会与上一轮撞上（服务端按它去重）
std::string makeClientMsgId(const char* tag, int index)
{
    return std::string("arl1-") + tag + "-" + toStr((int64_t)::GetTickCount64()) + "-" + toStr(index);
}

//A 发一条单聊文本给 B 并等回执成功，返回服务器 msgid
int64_t sendToB(LiveChatClient& a, const std::string& body)
{
    REQUIRE(a.Send(cmd_send_message, body));
    std::string resp;
    REQUIRE(a.WaitFor(cmd_send_message, resp, kWaitMs));
    Json::Value root;
    REQUIRE(parseJson(resp, root));
    INFO("send ack: " << resp);
    REQUIRE(root["code"].asInt() == 0);
    return _atoi64(root["msgid"].asString().c_str());
}

//在 timeoutMs 内收 B 发来的推送，数其中的自动回复；收到 want 条即返回。每条都核对前缀
int collectAutoRepliesFromB(LiveChatClient& a, int want, int timeoutMs)
{
    int count = 0;
    const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
    while (count < want && ::GetTickCount64() < deadline)
    {
        const int left = (int)(deadline - ::GetTickCount64());
        std::string body;
        if (!a.WaitFor(cmd_message_notify, body, left > 0 ? left : 1))
        {
            break;
        }
        Json::Value root;
        if (!parseJson(body, root) || root["from"].asString() != toStr(kUserB))
        {
            continue;
        }
        const Json::Value& content = root["content"];
        if (content["autoreply"].isBool() && content["autoreply"].asBool())
        {
            INFO("auto-reply notify: " << body);
            CHECK(content["text"].asString().compare(0, strlen(kPrefixUtf8), kPrefixUtf8) == 0);
            ++count;
        }
    }
    return count;
}

} // anonymous namespace

TEST_CASE("ARL1 B 离开时 A 发几条收到几条自动回复，带标记的消息不回复，服务端存下了回复", "[.live][autoreplylive]")
{
    LiveChatClient a;
    std::string reason;
    const bool ok = a.ConnectAndLogin(kRelayTestServerPort, kTestServerPubKey, kAccountA, kPassword, reason);
    INFO("login " << kAccountA << " failed: " << reason);
    REQUIRE(ok);
    a.ClearBacklog();

    //1. A 连发两条普通消息：B 每条都回
    int64_t firstMsgid = 0;
    for (int i = 0; i < kBurstCount; ++i)
    {
        const std::string text = "ARL1 message " + toStr(i + 1);
        const int64_t msgid = sendToB(a, net::MessageProtocolClient::buildSendTextRequest(
            kUserB, makeClientMsgId("plain", i), text));
        if (i == 0)
        {
            firstMsgid = msgid;
        }
    }
    CHECK(collectAutoRepliesFromB(a, kBurstCount, kAutoReplyWaitMs) == kBurstCount);

    //2. A 发一条本身就是自动回复的消息：B 不回（等满 kNoReplyWaitMs 一条都没有）
    sendToB(a, net::MessageProtocolClient::buildSendAutoReplyRequest(
        kUserB, makeClientMsgId("auto", 0), "ARL1 simulated auto-reply"));
    CHECK(collectAutoRepliesFromB(a, 1, kNoReplyWaitMs) == 0);

    //3. 服务端存下了 B 的两条自动回复：A 拉与 B 的最新历史，第一条消息之后来自 B 且带标记的恰好两条
    Json::Value req;
    req["ctype"] = 1;
    req["cid"] = toStr(kUserB);
    req["beforemsgid"] = "0";
    req["limit"] = kHistoryLimit;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    REQUIRE(a.Send(cmd_sync_messages, Json::writeString(wb, req)));
    std::string resp;
    REQUIRE(a.WaitFor(cmd_sync_messages, resp, kWaitMs));
    Json::Value hist;
    REQUIRE(parseJson(resp, hist));
    REQUIRE(hist["code"].asInt() == 0);
    int stored = 0;
    const Json::Value& msgs = hist["messages"];
    for (Json::ArrayIndex i = 0; i < msgs.size(); ++i)
    {
        const Json::Value& m = msgs[i];
        if (m["from"].asString() == toStr(kUserB) && _atoi64(m["msgid"].asString().c_str()) > firstMsgid
            && m["content"]["autoreply"].isBool() && m["content"]["autoreply"].asBool())
        {
            ++stored;
        }
    }
    CHECK(stored == kBurstCount);
}
