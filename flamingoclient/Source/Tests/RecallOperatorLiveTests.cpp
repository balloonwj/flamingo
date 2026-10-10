/**
 *  撤回者随历史与会话列表下发的端到端联调用例（Suite RBYL，标签 [.live][recallby]），RecallOperatorLiveTests.cpp
 *
 *  验证撤回者的记录与下发：群主撤回成员发的群消息后，服务端记下撤回者（t_chatmsg.f_recalled_by），
 *  成员重新拉历史（cmd_sync_messages）与会话列表（cmd_sync_conversations）时都能拿到 recalledby，
 *  PC 客户端据此写「群主撤回了一条消息」，而不是按发送人写成「你撤回了一条消息」。
 *  PC 客户端只在本人发出的消息上提供「撤回」，界面上点不出群主撤回别人消息的操作，所以用
 *  LiveChatClient 直接收发 v2 帧，走的是服务端 ChatSession::onRecallMessage 的真实路径。
 *
 *  默认不参与常规测试：用例带 [.live] 标签，Catch2 会跳过。跑之前先起 tmp/relaytest 的测试服务端
 *  （start_server.ps1），并且不要同时开着该环境的 PC 客户端（同一账号同类终端重复登录会互相顶下线）。然后：
 *
 *      FlamingoUnit.exe "[recallby]"
 *
 *  账号：MY0127（5127，群 268435463 的群主）与 MY0308（5308，普通成员），密码都是 perf1234。
 *  联调库里 MY0308 在该群被禁言（之前群管理联调留下的状态），发不了言；用例先由群主解禁，
 *  结束时再恢复禁言，让环境保持原样。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <time.h>
#include "LiveChatClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;

namespace {

//tmp/relaytest 测试服务端的 chatserver 端口
const unsigned short kRelayTestServerPort = 28000;

//两个测试账号与密码
const char* kOwnerAccount  = "MY0127";
const char* kMemberAccount = "MY0308";
const char* kPassword      = "perf1234";
const int64_t kOwnerId  = 5127;
const int64_t kMemberId = 5308;

//A 任群主、B 是普通成员的测试群
const int64_t kGroupId = 268435463;

//等一条应答的最长时间，单位毫秒
const int kWaitMs = 5000;

//拉历史时取最近几条：刚发的那条一定在最新的几条里
const int kHistoryLimit = 5;

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

//登录一个测试账号；失败时让用例停下并带上原因
void login(LiveChatClient& c, const char* account)
{
    std::string reason;
    bool ok = c.ConnectAndLogin(kRelayTestServerPort, kTestServerPubKey, account, kPassword, reason);
    INFO("login " << account << " failed: " << reason);
    REQUIRE(ok);
}

//发一条请求并等它的应答，返回解析后的 JSON
Json::Value request(LiveChatClient& c, uint16_t cmd, const std::string& body)
{
    REQUIRE(c.Send(cmd, body));
    std::string resp;
    REQUIRE(c.WaitFor(cmd, resp, kWaitMs));
    Json::Value root;
    REQUIRE(parseJson(resp, root));
    return root;
}

//成员在群里是否被禁言：群主拉一次群成员列表，取该成员的 muted
bool isMemberMuted(LiveChatClient& owner, int64_t groupId, int64_t userId)
{
    Json::Value members = request(owner, cmd_get_group_members, "{\"groupid\":\"" + toStr(groupId) + "\"}");
    REQUIRE(members["code"].asInt() == 0);
    const Json::Value& arr = members["members"];
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i]["userid"].asString() == toStr(userId))
            return arr[i]["muted"].asBool();
    }
    return false;
}

//群主设置某成员的禁言状态，只发请求、不等应答（析构里用，不能再触发断言）
void sendMemberMute(LiveChatClient& owner, int64_t groupId, int64_t userId, bool muted)
{
    owner.Send(cmd_mute_member, "{\"groupid\":\"" + toStr(groupId) + "\",\"userid\":\"" + toStr(userId)
                                + "\",\"muted\":" + (muted ? "1" : "0") + "}");
}

/**
 *  用例开头若成员被禁言，先由群主解禁；对象析构时恢复禁言。用例中途 REQUIRE 失败退出时
 *  析构照样执行，联调环境不会因为用例失败而少了这项禁言。
 */
class MuteRestorer
{
public:
    MuteRestorer(LiveChatClient& owner, int64_t groupId, int64_t userId)
        : m_owner(owner)
        , m_groupId(groupId)
        , m_userId(userId)
        , m_wasMuted(isMemberMuted(owner, groupId, userId))
    {
        if (m_wasMuted)
        {
            Json::Value resp = request(m_owner, cmd_mute_member,
                "{\"groupid\":\"" + toStr(m_groupId) + "\",\"userid\":\"" + toStr(m_userId) + "\",\"muted\":0}");
            REQUIRE(resp["code"].asInt() == 0);
        }
    }

    ~MuteRestorer()
    {
        if (m_wasMuted)
        {
            sendMemberMute(m_owner, m_groupId, m_userId, true);
            //等服务端处理完再让连接随用例结束关闭
            std::string ignored;
            m_owner.WaitFor(cmd_mute_member, ignored, kWaitMs);
        }
    }

private:
    LiveChatClient& m_owner;     //群主的连接，生命周期长于本对象（同一用例内的局部变量）
    int64_t         m_groupId;   //群 id
    int64_t         m_userId;    //被解禁、结束时恢复禁言的成员 userid
    bool            m_wasMuted;  //用例开始时该成员是否被禁言；为真才需要在析构时恢复
};

} // namespace

TEST_CASE("RBYL1 群主撤回成员的群消息：成员拉历史与会话列表都拿到撤回者", "[.live][recallby]")
{
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    MuteRestorer muteGuard(owner, kGroupId, kMemberId);

    //成员在群里发一条文字消息
    const std::string clientMsgId = "rbyl1-" + toStr((int64_t)time(NULL));
    Json::Value sent = request(member, cmd_send_message,
        "{\"clientmsgid\":\"" + clientMsgId + "\",\"ctype\":2,\"cid\":\"" + toStr(kGroupId) + "\","
        "\"msgtype\":1,\"content\":{\"text\":\"recall operator live check\"},\"mentions\":[],\"mentionall\":false}");
    REQUIRE(sent["code"].asInt() == 0);
    const std::string msgid = sent["msgid"].asString();
    REQUIRE_FALSE(msgid.empty());

    //群主撤回这条别人发的消息
    Json::Value recalled = request(owner, cmd_recall_message,
        "{\"ctype\":2,\"cid\":\"" + toStr(kGroupId) + "\",\"msgid\":\"" + msgid + "\"}");
    REQUIRE(recalled["code"].asInt() == 0);

    //成员拉最近的历史：这条带 recalled 与撤回者，发送方仍是成员自己
    Json::Value history = request(member, cmd_sync_messages,
        "{\"ctype\":2,\"cid\":\"" + toStr(kGroupId) + "\",\"beforemsgid\":\"0\",\"limit\":" + toStr(kHistoryLimit) + "}");
    REQUIRE(history["code"].asInt() == 0);
    const Json::Value& messages = history["messages"];
    REQUIRE(messages.isArray());
    bool found = false;
    for (Json::ArrayIndex i = 0; i < messages.size(); ++i)
    {
        if (messages[i]["msgid"].asString() != msgid)
            continue;
        found = true;
        CHECK(messages[i]["recalled"].asBool());
        CHECK(messages[i]["recalledby"].asString() == toStr(kOwnerId));
        CHECK(messages[i]["from"].asString() == toStr(kMemberId));
    }
    CHECK(found);

    //成员拉会话列表：该群的最后一条就是刚撤回的那条，lastmsg 同样带撤回者
    Json::Value conversations = request(member, cmd_sync_conversations, "{}");
    REQUIRE(conversations["code"].asInt() == 0);
    const Json::Value& items = conversations["conversations"];
    REQUIRE(items.isArray());
    bool groupFound = false;
    for (Json::ArrayIndex i = 0; i < items.size(); ++i)
    {
        if (items[i]["ctype"].asInt() != conversation_group || items[i]["cid"].asString() != toStr(kGroupId))
            continue;
        groupFound = true;
        CHECK(items[i]["lastmsgid"].asString() == msgid);
        const Json::Value& last = items[i]["lastmsg"];
        CHECK(last["recalled"].asBool());
        CHECK(last["recalledby"].asString() == toStr(kOwnerId));
        CHECK(last["from"].asString() == toStr(kMemberId));
    }
    CHECK(groupFound);
}
