/**
 *  拉取历史消息时共享卡片带状态的联调用例（Suite SCSY，标签 [.live][fileshare][sharecardsync]），ShareCardSyncLiveTests.cpp
 *
 *  2026-10-03 用户发现：重启客户端后，聊天窗里一张已取消共享的卡片一直显示「正在获取状态」。原因是服务端
 *  ChatSession::onSyncMessages（cmd_sync_messages，客户端打开会话时拉历史用的命令）只给投票、接龙、待办三种卡片
 *  合并状态，没有给文件共享卡片（msgtype 20）合并 sharestate / sharereason；客户端又会用拉到的消息体覆盖本地库，
 *  于是 DLL 手里没有这个共享的状态时（已取消的共享不在登录时拉到的共享列表里），卡片就没有任何状态可显示。
 *  本用例用 LiveChatClient 直接发 cmd_sync_messages，断言拉到的每一张共享卡片都带 sharestate 与 sharereason。
 *
 *  默认不参与常规测试：用例带 [.live] 标签。跑之前先起 tmp/fileshare_env 的测试服务端（start_server.ps1），
 *  并且不要同时开着该环境里以 MY0308 登录的 PC 客户端（同一账号同类终端重复登录会互相顶下线）。然后：
 *
 *      FlamingoUnit.exe "[sharecardsync]"
 *
 *  联调库里群 268435459 有 MY0308（5308）与 MY0309 发起的多张共享卡片，其中有已取消的共享。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../../Source/Tests/LiveChatClient.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;

namespace {

//tmp/fileshare_env 的 chatserver 端口、测试账号与密码
const unsigned short kScsyChatPort = 25000;
const char* const kScsyAccount = "MY0308";
const char* const kScsyPassword = "perf1234";

//有共享卡片的测试群
const char* const kScsyGroupId = "268435459";

//拉历史的条数：联调库里这个群的共享卡片都在最近的这些消息里
const int kScsyHistoryLimit = 50;

//文件共享卡片的消息类型（docs/文件共享方案.md 5.6，与服务端 message_type_fileshare 相同）
const int kScsyMsgTypeFileShare = 20;

//等一条应答的最长时间，单位毫秒
const int kScsyWaitMs = 5000;

bool ParseObject(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    Json::CharReader* reader = builder.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

} // namespace

TEST_CASE("SCSY1 拉取历史消息时共享卡片带状态", "[.live][fileshare][sharecardsync]")
{
    LiveChatClient client;
    std::string reason;
    const bool loggedIn = client.ConnectAndLogin(kScsyChatPort, kTestServerPubKey, kScsyAccount, kScsyPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(loggedIn);

    REQUIRE(client.Send(cmd_sync_messages, std::string("{\"ctype\":2,\"cid\":\"") + kScsyGroupId
                                           + "\",\"beforemsgid\":\"0\",\"limit\":" + std::to_string(kScsyHistoryLimit) + "}"));
    std::string body;
    REQUIRE(client.WaitFor(cmd_sync_messages, body, kScsyWaitMs));
    Json::Value root;
    REQUIRE(ParseObject(body, root));
    REQUIRE(root["code"].asInt() == 0);
    const Json::Value& messages = root["messages"];
    REQUIRE(messages.isArray());

    int cards = 0;
    for (Json::ArrayIndex i = 0; i < messages.size(); ++i)
    {
        if (messages[i]["msgtype"].asInt() != kScsyMsgTypeFileShare)
            continue;
        ++cards;
        const Json::Value& content = messages[i]["content"];
        INFO("msgid " << messages[i]["msgid"].asString());
        REQUIRE(content.isObject());
        CHECK(content.isMember("sharestate"));
        CHECK(content.isMember("sharereason"));
    }
    //联调库里至少有一张共享卡片，否则本用例什么也没验证
    CHECK(cards > 0);
}
