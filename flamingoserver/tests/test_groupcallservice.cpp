/**
 *  群通话服务端处理流程单元测试（Suite GCS），test_groupcallservice.cpp
 *
 *  用假环境（FakeGroupCallEnv：内存里的群成员、角色、在线与忙线名单，记录每一次推送与卡片落库）驱动 GroupCallService，
 *  核对每个请求的应答、紧随其后的 1746、以及「谁收到哪条推送」（docs/协议设计-v2.md 第 21 节）：
 *    GCS1 发起：应答带忙线 / 离线名单，随后 1746；被邀请人收到 1741；房间里的人收到 1745 事件 1；卡片推给全体群成员；
 *         全体群成员收到进行中的 1750；非群成员的被邀请人被忽略
 *    GCS2 发起被拒：企业关闭 727、不是群成员 725、本人忙 723、群里已有通话 722（带现有房间号）、格式错误 3
 *    GCS3 加入：接听后 1745 事件 1 与人数变了的 1750；直接加入要群成员资格；重复加入只回应答与 1746、不广播；正忙 723
 *    GCS4 拒绝：房间里的人与拒绝者本人收到 1745 事件 3
 *    GCS5 离开：中途离开推 1745 事件 2 与 1750；最后一个已加入的人离开时改写卡片、向群成员与仍在振铃的人推结束的 1750
 *    GCS6 追加邀请：应答带名单，新被邀请的人收到 1741，房间里的人收到 1745 事件 5
 *    GCS7 成员媒体与查询：1745 事件 6；查询只回答群成员，有 / 没有群通话
 *    GCS8 掉线、登录与定时清理：事件 4 / 7 / 8，回来时补发 1746，宽限期满且最后一人时结束
 *    GCS9 管理：普通成员 728；管理员可静音某人（事件 9）、全员静音（事件 11）；移出（事件 10，被移出的人也收到）；不能对自己
 *
 *  典型用法：Tests.exe --test-case="GCS*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/GroupCallService.h"
#include "../chatserversrc/GroupCallProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//测试用的群与用户
const int64_t kGroup = 600001;
const int64_t kOtherGroup = 600002;
const int64_t kAlice = 5127;   //群主
const int64_t kBob   = 5308;
const int64_t kCarol = 5309;
const int64_t kDave  = 5310;   //管理员
const int64_t kErin  = 5311;
const int64_t kStranger = 7001;   //不是群成员

//测试用的时间点，Unix 毫秒
const int64_t kT0 = 1790943882000LL;

//一次推送的记录
struct PushRecord
{
    std::vector<int64_t> m_users;   //接收者
    uint16_t             m_cmd;     //命令号
    std::string          m_body;    //包体
};

//一次卡片落库的记录
struct CardRecord
{
    int64_t              m_groupId;
    int64_t              m_fromId;
    std::string          m_content;
    std::string          m_clientMsgId;
    std::vector<int64_t> m_receivers;
};

//假环境：群成员、角色、在线与忙线全在内存里，推送与卡片落库只做记录
class FakeGroupCallEnv : public IGroupCallEnv
{
public:
    FakeGroupCallEnv()
        : m_nextMsgId(9100)
        , m_updateOk(true)
    {
        //群里五个人：kAlice 群主、kDave 管理员，其余普通成员
        const int64_t members[] = { kAlice, kBob, kCarol, kDave, kErin };
        for (size_t i = 0; i < sizeof(members) / sizeof(members[0]); ++i)
        {
            m_groupMembers[kGroup].push_back(members[i]);
            m_online.insert(members[i]);
            m_roles[members[i]] = group_role_member;
        }
        m_roles[kAlice] = group_role_owner;
        m_roles[kDave] = group_role_admin;
        m_online.insert(kStranger);
    }

    virtual bool isGroupMember(int64_t groupId, int64_t userId) override
    {
        const std::vector<int64_t>& ids = m_groupMembers[groupId];
        return std::find(ids.begin(), ids.end(), userId) != ids.end();
    }

    virtual std::vector<int64_t> groupMemberIds(int64_t groupId) override
    {
        return m_groupMembers[groupId];
    }

    virtual std::string groupName(int64_t groupId) override
    {
        return groupId == kGroup ? "Product" : "";
    }

    virtual int groupRole(int64_t groupId, int64_t userId) override
    {
        if (!isGroupMember(groupId, userId))
            return -1;
        return m_roles[userId];
    }

    virtual bool isOnline(int64_t userId) override
    {
        return m_online.find(userId) != m_online.end();
    }

    virtual bool isBusyOutsideGroupCall(int64_t userId) override
    {
        return m_busy.find(userId) != m_busy.end();
    }

    virtual bool isFeatureEnabled(int64_t userId) override
    {
        return m_featureOff.find(userId) == m_featureOff.end();
    }

    virtual int64_t saveCard(int64_t groupId, int64_t fromId, const std::string& content,
                             const std::string& clientMsgId, int64_t now,
                             const std::vector<int64_t>& receivers) override
    {
        (void)now;
        CardRecord rec;
        rec.m_groupId = groupId;
        rec.m_fromId = fromId;
        rec.m_content = content;
        rec.m_clientMsgId = clientMsgId;
        rec.m_receivers = receivers;
        m_cards.push_back(rec);
        return ++m_nextMsgId;
    }

    virtual bool updateCard(int64_t msgId, const std::string& content) override
    {
        m_updates[msgId] = content;
        return m_updateOk;
    }

    virtual void push(const std::vector<int64_t>& users, uint16_t cmd, const std::string& body) override
    {
        PushRecord rec;
        rec.m_users = users;
        rec.m_cmd = cmd;
        rec.m_body = body;
        m_pushes.push_back(rec);
    }

    //取某个命令号的全部推送
    std::vector<PushRecord> pushesOf(uint16_t cmd) const
    {
        std::vector<PushRecord> out;
        for (size_t i = 0; i < m_pushes.size(); ++i)
        {
            if (m_pushes[i].m_cmd == cmd)
                out.push_back(m_pushes[i]);
        }
        return out;
    }

    std::map<int64_t, std::vector<int64_t> > m_groupMembers;   //群 id → 成员
    std::map<int64_t, int>                   m_roles;          //userid → 群角色
    std::set<int64_t>                        m_online;         //在线的人
    std::set<int64_t>                        m_busy;           //在一对一通话或屏幕共享里的人
    std::set<int64_t>                        m_featureOff;     //所在企业关闭群通话的人
    std::vector<PushRecord>                  m_pushes;         //全部推送
    std::vector<CardRecord>                  m_cards;          //全部卡片落库
    std::map<int64_t, std::string>           m_updates;        //卡片改写：msgid → 新 content
    int64_t                                  m_nextMsgId;      //下一个卡片消息 id
    bool                                     m_updateOk;       //updateCard 的返回值
};

//把 JSON 文本解析为对象；失败时让用例失败
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

//接收者列表里是否有某人
bool hasUser(const PushRecord& rec, int64_t userId)
{
    return std::find(rec.m_users.begin(), rec.m_users.end(), userId) != rec.m_users.end();
}

//配好上限的管理器
void configureManager(GroupCallManager& mgr)
{
    GroupCallConfig cfg;
    cfg.m_relayTokenKey = "gcs-key";
    cfg.m_relayHost = "127.0.0.1";
    cfg.m_relayPort = 20003;
    cfg.m_maxMembers = 16;
    cfg.m_maxCameras = 9;
    mgr.configure(cfg);
}

//kAlice 发起，邀请 kBob 与 kCarol；返回房间号
std::string startByAlice(GroupCallService& svc, FakeGroupCallEnv& env)
{
    std::vector<GroupCallReply> replies;
    svc.handleRequest(cmd_groupcall_start, kAlice, "Alice",
                      "{\"groupid\":\"600001\",\"media\":1,\"invitees\":[\"5308\",\"5309\"]}", kT0, replies);
    REQUIRE(replies.size() == 2);
    const Json::Value r = parseJson(replies[0].m_body);
    REQUIRE(r["code"].asInt() == 0);
    env.m_pushes.clear();
    return r["roomid"].asString();
}

//发一个只带房间号的请求，返回应答的 code
int roomRequest(GroupCallService& svc, uint16_t cmd, int64_t userId, const std::string& roomId,
                std::vector<GroupCallReply>& replies)
{
    replies.clear();
    svc.handleRequest(cmd, userId, "", "{\"roomid\":\"" + roomId + "\"}", kT0 + 1000, replies);
    REQUIRE(!replies.empty());
    return parseJson(replies[0].m_body)["code"].asInt();
}

} // namespace

TEST_CASE("GCS1 发起：应答名单、1746、邀请、房间状态、卡片与群内状态")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    env.m_online.erase(kErin);   //kErin 不在线
    env.m_busy.insert(kDave);    //kDave 在一对一通话里
    GroupCallService svc(mgr, env);

    std::vector<GroupCallReply> replies;
    svc.handleRequest(cmd_groupcall_start, kAlice, "Alice",
                      "{\"groupid\":\"600001\",\"media\":2,\"invitees\":[\"5308\",\"5310\",\"5311\",\"7001\"]}", kT0,
                      replies);
    REQUIRE(replies.size() == 2);
    CHECK(replies[0].m_cmd == cmd_groupcall_start);
    Json::Value r = parseJson(replies[0].m_body);
    CHECK(r["code"].asInt() == 0);
    const std::string roomId = r["roomid"].asString();
    REQUIRE(r["busy"].size() == 1);
    CHECK(r["busy"][0].asString() == "5310");
    REQUIRE(r["offline"].size() == 1);
    CHECK(r["offline"][0].asString() == "5311");

    CHECK(replies[1].m_cmd == cmd_groupcall_relay_info);
    Json::Value relay = parseJson(replies[1].m_body);
    CHECK(relay["roomid"].asString() == roomId);
    CHECK(relay["seat"].asInt() == 1);
    CHECK(relay["relayport"].asInt() == 20003);

    //只有 kBob 振铃（kStranger 不是群成员，被忽略）
    std::vector<PushRecord> invites = env.pushesOf(cmd_groupcall_invite_notify);
    REQUIRE(invites.size() == 1);
    REQUIRE(invites[0].m_users.size() == 1);
    CHECK(invites[0].m_users[0] == kBob);
    Json::Value inv = parseJson(invites[0].m_body);
    CHECK(inv["groupname"].asString() == "Product");
    CHECK(inv["invitername"].asString() == "Alice");
    CHECK(inv["ringms"].asInt64() == GroupCallManager::kRingTimeoutMs);
    CHECK(inv["media"].asInt() == groupcall_media_video);

    //房间状态：发起人与振铃中的 kBob
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(hasUser(states[0], kAlice));
    CHECK(hasUser(states[0], kBob));
    CHECK(!hasUser(states[0], kCarol));
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_member_joined);

    //卡片：记在发起人名下，推给全体群成员
    REQUIRE(env.m_cards.size() == 1);
    CHECK(env.m_cards[0].m_groupId == kGroup);
    CHECK(env.m_cards[0].m_fromId == kAlice);
    CHECK(env.m_cards[0].m_clientMsgId == "sys-groupcall-card-" + roomId);
    CHECK(env.m_cards[0].m_receivers.size() >= 5);
    Json::Value card = parseJson(env.m_cards[0].m_content);
    CHECK(card["kind"].asString() == "groupcall");
    CHECK(card["initiatorname"].asString() == "Alice");
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.m_cardMsgId == env.m_nextMsgId);

    //群内状态：全体群成员
    std::vector<PushRecord> status = env.pushesOf(cmd_groupcall_status_notify);
    REQUIRE(status.size() == 1);
    CHECK(hasUser(status[0], kErin));
    CHECK(parseJson(status[0].m_body)["active"].asInt() == 1);
    CHECK(parseJson(status[0].m_body)["count"].asInt() == 1);
}

TEST_CASE("GCS2 发起被拒：727、725、723、722、格式错误")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    std::vector<GroupCallReply> replies;
    const std::string body = "{\"groupid\":\"600001\",\"media\":1}";

    env.m_featureOff.insert(kBob);
    svc.handleRequest(cmd_groupcall_start, kBob, "", body, kT0, replies);
    REQUIRE(replies.size() == 1);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_disabled);

    replies.clear();
    svc.handleRequest(cmd_groupcall_start, kStranger, "", body, kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_not_member);

    replies.clear();
    env.m_busy.insert(kCarol);
    svc.handleRequest(cmd_groupcall_start, kCarol, "", body, kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_busy);

    replies.clear();
    svc.handleRequest(cmd_groupcall_start, kAlice, "", "{\"groupid\":\"600001\"}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_bad_request);

    //已有通话：722，带现有房间号；不推送任何东西
    const std::string roomId = startByAlice(svc, env);
    replies.clear();
    svc.handleRequest(cmd_groupcall_start, kDave, "", body, kT0, replies);
    REQUIRE(replies.size() == 1);
    Json::Value r = parseJson(replies[0].m_body);
    CHECK(r["code"].asInt() == error_code_groupcall_exists);
    CHECK(r["roomid"].asString() == roomId);
    CHECK(env.m_pushes.empty());
    CHECK(env.m_cards.size() == 1);
}

TEST_CASE("GCS3 加入：接听、直接加入的资格、重复加入、正忙")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    const std::string roomId = startByAlice(svc, env);

    //振铃中的 kBob 接听
    std::vector<GroupCallReply> replies;
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kBob, roomId, replies) == 0);
    REQUIRE(replies.size() == 2);
    CHECK(replies[1].m_cmd == cmd_groupcall_relay_info);
    CHECK(parseJson(replies[1].m_body)["seat"].asInt() == 2);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_member_joined);
    CHECK(parseJson(states[0].m_body)["actor"].asString() == "5308");
    //房间里的人：kAlice、kBob 与仍在振铃的 kCarol
    CHECK(hasUser(states[0], kCarol));
    std::vector<PushRecord> status = env.pushesOf(cmd_groupcall_status_notify);
    REQUIRE(status.size() == 1);
    CHECK(parseJson(status[0].m_body)["count"].asInt() == 2);

    //重复加入：应答 + 1746，不再广播
    env.m_pushes.clear();
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kBob, roomId, replies) == 0);
    CHECK(replies.size() == 2);
    CHECK(env.m_pushes.empty());

    //不是群成员的人直接加入
    CHECK(roomRequest(svc, cmd_groupcall_join, kStranger, roomId, replies) == error_code_groupcall_not_member);
    //正忙的群成员
    env.m_busy.insert(kErin);
    CHECK(roomRequest(svc, cmd_groupcall_join, kErin, roomId, replies) == error_code_groupcall_busy);
    //企业关闭
    env.m_featureOff.insert(kDave);
    CHECK(roomRequest(svc, cmd_groupcall_join, kDave, roomId, replies) == error_code_groupcall_disabled);
    //房间不存在
    CHECK(roomRequest(svc, cmd_groupcall_join, kDave, "gc-nope", replies) == error_code_groupcall_not_exist);
    //群成员从横幅直接加入
    env.m_featureOff.clear();
    CHECK(roomRequest(svc, cmd_groupcall_join, kDave, roomId, replies) == 0);
}

TEST_CASE("GCS4 拒绝：房间里的人与拒绝者本人收到事件 3")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    const std::string roomId = startByAlice(svc, env);

    std::vector<GroupCallReply> replies;
    REQUIRE(roomRequest(svc, cmd_groupcall_decline, kBob, roomId, replies) == 0);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(hasUser(states[0], kBob));
    CHECK(hasUser(states[0], kAlice));
    CHECK(hasUser(states[0], kCarol));
    Json::Value v = parseJson(states[0].m_body);
    CHECK(v["event"].asInt() == groupcall_event_declined);
    CHECK(v["members"].size() == 2);
    //拒绝不改变已加入人数，不推 1750
    CHECK(env.pushesOf(cmd_groupcall_status_notify).empty());
    //再拒绝一次：已不在振铃中
    CHECK(roomRequest(svc, cmd_groupcall_decline, kBob, roomId, replies) == error_code_groupcall_not_member);
}

TEST_CASE("GCS5 离开：中途离开与最后一个已加入的人离开（改写卡片、结束的 1750）")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    const std::string roomId = startByAlice(svc, env);
    std::vector<GroupCallReply> replies;
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kBob, roomId, replies) == 0);
    env.m_pushes.clear();

    //发起人中途离开：事件 2，人数变成 1
    REQUIRE(roomRequest(svc, cmd_groupcall_leave, kAlice, roomId, replies) == 0);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_member_left);
    CHECK(hasUser(states[0], kAlice));
    std::vector<PushRecord> status = env.pushesOf(cmd_groupcall_status_notify);
    REQUIRE(status.size() == 1);
    CHECK(parseJson(status[0].m_body)["active"].asInt() == 1);
    CHECK(parseJson(status[0].m_body)["count"].asInt() == 1);

    //最后一个已加入的 kBob 离开：结束；仍在振铃的 kCarol 与群成员收到结束的 1750，卡片被改写
    env.m_pushes.clear();
    REQUIRE(roomRequest(svc, cmd_groupcall_leave, kBob, roomId, replies) == 0);
    status = env.pushesOf(cmd_groupcall_status_notify);
    REQUIRE(status.size() == 1);
    CHECK(hasUser(status[0], kCarol));
    CHECK(hasUser(status[0], kBob));
    CHECK(hasUser(status[0], kErin));
    Json::Value v = parseJson(status[0].m_body);
    CHECK(v["active"].asInt() == 0);
    CHECK(v["duration"].asInt64() == 1);
    const std::string cardId = v["msgid"].asString();
    CHECK(cardId == std::to_string(env.m_nextMsgId));
    REQUIRE(env.m_updates.size() == 1);
    Json::Value ended = parseJson(env.m_updates.begin()->second);
    CHECK(ended["duration"].asInt64() == 1);
    CHECK(ended["initiatorname"].asString() == "Alice");
    CHECK(v["content"].asString() == env.m_updates.begin()->second);
    CHECK(mgr.roomCount() == 0);
    //不在房间里了
    CHECK(roomRequest(svc, cmd_groupcall_leave, kBob, roomId, replies) == error_code_groupcall_not_exist);
}

TEST_CASE("GCS5b 卡片改写失败时结束的 1750 不带卡片")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    env.m_updateOk = false;
    GroupCallService svc(mgr, env);
    const std::string roomId = startByAlice(svc, env);
    std::vector<GroupCallReply> replies;
    REQUIRE(roomRequest(svc, cmd_groupcall_leave, kAlice, roomId, replies) == 0);
    std::vector<PushRecord> status = env.pushesOf(cmd_groupcall_status_notify);
    REQUIRE(status.size() == 1);
    Json::Value v = parseJson(status[0].m_body);
    CHECK(v["active"].asInt() == 0);
    CHECK(!v.isMember("msgid"));
}

TEST_CASE("GCS6 追加邀请：应答名单、新被邀请的人收到 1741、房间里的人收到事件 5")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    env.m_online.erase(kErin);
    GroupCallService svc(mgr, env);
    const std::string roomId = startByAlice(svc, env);

    std::vector<GroupCallReply> replies;
    svc.handleRequest(cmd_groupcall_invite_more, kAlice, "Alice",
                      "{\"roomid\":\"" + roomId + "\",\"invitees\":[\"5310\",\"5311\",\"5308\"]}", kT0 + 500, replies);
    REQUIRE(replies.size() == 1);
    Json::Value r = parseJson(replies[0].m_body);
    CHECK(r["code"].asInt() == 0);
    REQUIRE(r["offline"].size() == 1);
    CHECK(r["offline"][0].asString() == "5311");
    CHECK(r["busy"].size() == 0);
    std::vector<PushRecord> invites = env.pushesOf(cmd_groupcall_invite_notify);
    REQUIRE(invites.size() == 1);
    CHECK(invites[0].m_users[0] == kDave);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_invited);
    CHECK(hasUser(states[0], kDave));

    //振铃中的人不能追加邀请
    replies.clear();
    svc.handleRequest(cmd_groupcall_invite_more, kBob, "",
                      "{\"roomid\":\"" + roomId + "\",\"invitees\":[\"5311\"]}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_not_member);
}

TEST_CASE("GCS7 成员媒体与查询")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);

    //没有群通话时查询
    std::vector<GroupCallReply> replies;
    svc.handleRequest(cmd_groupcall_query, kBob, "", "{\"groupid\":\"600001\"}", kT0, replies);
    REQUIRE(replies.size() == 1);
    CHECK(replies[0].m_cmd == cmd_groupcall_query);
    CHECK(parseJson(replies[0].m_body)["exists"].asInt() == 0);

    const std::string roomId = startByAlice(svc, env);
    replies.clear();
    svc.handleRequest(cmd_groupcall_member_media, kAlice, "", "{\"roomid\":\"" + roomId + "\",\"mic\":0,\"camera\":1}",
                      kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    Json::Value v = parseJson(states[0].m_body);
    CHECK(v["event"].asInt() == groupcall_event_media_changed);
    CHECK(v["members"][0]["camera"].asInt() == 1);
    CHECK(v["members"][0]["mic"].asInt() == 0);

    replies.clear();
    svc.handleRequest(cmd_groupcall_query, kErin, "", "{\"groupid\":\"600001\"}", kT0, replies);
    v = parseJson(replies[0].m_body);
    CHECK(v["exists"].asInt() == 1);
    CHECK(v["roomid"].asString() == roomId);
    CHECK(v["count"].asInt() == 1);
    //不是群成员不回答
    replies.clear();
    svc.handleRequest(cmd_groupcall_query, kStranger, "", "{\"groupid\":\"600001\"}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_not_member);
    CHECK(!parseJson(replies[0].m_body).isMember("roomid"));
    //别的群
    replies.clear();
    svc.handleRequest(cmd_groupcall_query, kStranger, "", "{\"groupid\":\"600002\"}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_not_member);
    (void)kOtherGroup;
}

TEST_CASE("GCS8 掉线、登录与定时清理")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    const std::string roomId = startByAlice(svc, env);
    std::vector<GroupCallReply> replies;
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kBob, roomId, replies) == 0);
    env.m_pushes.clear();

    //振铃中的 kCarol 掉线：事件 4
    svc.onUserOffline(kCarol, kT0 + 2000);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_ring_timeout);
    //已加入的 kBob 掉线：事件 7
    env.m_pushes.clear();
    svc.onUserOffline(kBob, kT0 + 2000);
    states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_member_offline);
    //不在通话里的人掉线：什么都不推
    env.m_pushes.clear();
    svc.onUserOffline(kErin, kT0 + 2000);
    CHECK(env.m_pushes.empty());

    //kBob 回来：事件 8，并补发 1746
    replies.clear();
    svc.onUserLogin(kBob, kT0 + 3000, replies);
    REQUIRE(replies.size() == 1);
    CHECK(replies[0].m_cmd == cmd_groupcall_relay_info);
    states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_member_back);
    //不在宽限期里的人登录：什么都不做
    replies.clear();
    svc.onUserLogin(kErin, kT0 + 3000, replies);
    CHECK(replies.empty());

    //追加邀请 kDave 后让他振铃超时：事件 4，他本人也收到
    replies.clear();
    svc.handleRequest(cmd_groupcall_invite_more, kAlice, "Alice",
                      "{\"roomid\":\"" + roomId + "\",\"invitees\":[\"5310\"]}", kT0 + 4000, replies);
    env.m_pushes.clear();
    svc.reapTick(kT0 + 4000 + GroupCallManager::kRingTimeoutMs);
    states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_ring_timeout);
    CHECK(hasUser(states[0], kDave));

    //两个已加入的人都掉线、超过宽限期：先走的按离开，最后一个结束房间
    svc.onUserOffline(kAlice, kT0 + 100000);
    svc.onUserOffline(kBob, kT0 + 100000);
    env.m_pushes.clear();
    svc.reapTick(kT0 + 100000 + GroupCallManager::kDefaultOfflineGraceMs);
    std::vector<PushRecord> status = env.pushesOf(cmd_groupcall_status_notify);
    REQUIRE(!status.empty());
    CHECK(parseJson(status.back().m_body)["active"].asInt() == 0);
    CHECK(mgr.roomCount() == 0);
}

TEST_CASE("GCS9 管理：权限、静音某人、全员静音、移出、不能对自己")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    //kBob（普通成员）发起，让管理员 kDave 与群主 kAlice 都加入
    std::vector<GroupCallReply> replies;
    svc.handleRequest(cmd_groupcall_start, kBob, "Bob", "{\"groupid\":\"600001\",\"media\":1,\"invitees\":[\"5309\"]}",
                      kT0, replies);
    const std::string roomId = parseJson(replies[0].m_body)["roomid"].asString();
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kCarol, roomId, replies) == 0);
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kDave, roomId, replies) == 0);
    env.m_pushes.clear();

    const std::string prefix = "{\"roomid\":\"" + roomId + "\",";
    //普通成员 kCarol（不是发起人）无权
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kCarol, "", prefix + "\"action\":1,\"target\":\"5308\"}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_no_permission);
    //没有加入的群主 kAlice 也不行（操作者须已加入）
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kAlice, "", prefix + "\"action\":2}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_not_member);
    //不能对自己
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kDave, "", prefix + "\"action\":1,\"target\":\"5310\"}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_bad_request);

    //管理员静音 kCarol：事件 9，带 target
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kDave, "", prefix + "\"action\":1,\"target\":\"5309\"}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    Json::Value v = parseJson(states[0].m_body);
    CHECK(v["event"].asInt() == groupcall_event_member_muted);
    CHECK(v["target"].asString() == "5309");

    //发起人 kBob 全员静音：事件 11
    env.m_pushes.clear();
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kBob, "", prefix + "\"action\":2}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(parseJson(states[0].m_body)["event"].asInt() == groupcall_event_muted_all);

    //移出 kCarol：事件 10，kCarol 本人也收到；人数变了推 1750
    env.m_pushes.clear();
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kDave, "", prefix + "\"action\":3,\"target\":\"5309\"}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    CHECK(hasUser(states[0], kCarol));
    v = parseJson(states[0].m_body);
    CHECK(v["event"].asInt() == groupcall_event_member_removed);
    CHECK(v["target"].asString() == "5309");
    CHECK(env.pushesOf(cmd_groupcall_status_notify).size() == 1);
    //被移出的人可以再加入
    CHECK(roomRequest(svc, cmd_groupcall_join, kCarol, roomId, replies) == 0);
}

TEST_CASE("GCS11 全员静音房间状态：快照带 allmuted，发起人与管理员豁免，普通成员开麦回 728，后加入默认静音，解除推事件 12")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    //kBob（普通成员）发起；kCarol（普通成员）与 kDave（管理员）加入，都开着麦克风
    std::vector<GroupCallReply> replies;
    svc.handleRequest(cmd_groupcall_start, kBob, "Bob", "{\"groupid\":\"600001\",\"media\":1,\"invitees\":[\"5309\"]}",
                      kT0, replies);
    const std::string roomId = parseJson(replies[0].m_body)["roomid"].asString();
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kCarol, roomId, replies) == 0);
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kDave, roomId, replies) == 0);
    const std::string prefix = "{\"roomid\":\"" + roomId + "\",";

    //普通成员无权开启
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kCarol, "", prefix + "\"action\":2}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_no_permission);

    //管理员开启：事件 11，快照 allmuted 为 1，发起人 kBob 与管理员 kDave 仍开着麦克风，kCarol 被静音
    env.m_pushes.clear();
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kDave, "", prefix + "\"action\":2}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    std::vector<PushRecord> states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    Json::Value v = parseJson(states[0].m_body);
    CHECK(v["event"].asInt() == groupcall_event_muted_all);
    CHECK(v["allmuted"].asInt() == 1);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.findMember(kBob)->m_mic);
    CHECK(room.findMember(kDave)->m_mic);
    CHECK(!room.findMember(kCarol)->m_mic);

    //kCarol 要开麦：728；发起人 kBob 关了再开不受限
    replies.clear();
    svc.handleRequest(cmd_groupcall_member_media, kCarol, "", prefix + "\"mic\":1,\"camera\":0}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_no_permission);
    replies.clear();
    svc.handleRequest(cmd_groupcall_member_media, kBob, "", prefix + "\"mic\":0,\"camera\":0}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    replies.clear();
    svc.handleRequest(cmd_groupcall_member_media, kBob, "", prefix + "\"mic\":1,\"camera\":0}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);

    //后加入的普通成员 kErin 默认静音；后加入的群主 kAlice 照常开着麦克风
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kErin, roomId, replies) == 0);
    REQUIRE(roomRequest(svc, cmd_groupcall_join, kAlice, roomId, replies) == 0);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.findMember(kErin)->m_mic);
    CHECK(room.findMember(kAlice)->m_mic);

    //普通成员也无权解除
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kErin, "", prefix + "\"action\":4}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_groupcall_no_permission);

    //群主解除：事件 12，allmuted 为 0，被静音的人仍关着麦克风，之后可以自己打开
    env.m_pushes.clear();
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kAlice, "", prefix + "\"action\":4}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    states = env.pushesOf(cmd_groupcall_state_notify);
    REQUIRE(states.size() == 1);
    v = parseJson(states[0].m_body);
    CHECK(v["event"].asInt() == groupcall_event_unmuted_all);
    CHECK(v["actor"].asString() == "5127");
    CHECK(v["allmuted"].asInt() == 0);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.findMember(kCarol)->m_mic);
    CHECK(!room.findMember(kErin)->m_mic);
    replies.clear();
    svc.handleRequest(cmd_groupcall_member_media, kCarol, "", prefix + "\"mic\":1,\"camera\":0}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);

    //没有开启时再解除：应答成功、不推送
    env.m_pushes.clear();
    replies.clear();
    svc.handleRequest(cmd_groupcall_manage, kAlice, "", prefix + "\"action\":4}", kT0, replies);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == 0);
    CHECK(env.pushesOf(cmd_groupcall_state_notify).empty());
}

TEST_CASE("GCS10 不是群通话的命令号回参数错误")
{
    GroupCallManager mgr;
    configureManager(mgr);
    FakeGroupCallEnv env;
    GroupCallService svc(mgr, env);
    std::vector<GroupCallReply> replies;
    svc.handleRequest(cmd_screenshare_start, kAlice, "", "{}", kT0, replies);
    REQUIRE(replies.size() == 1);
    CHECK(parseJson(replies[0].m_body)["code"].asInt() == error_code_bad_request);
}
