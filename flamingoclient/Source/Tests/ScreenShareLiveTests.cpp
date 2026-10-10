/**
 *  屏幕共享信令的端到端联调用例（Suite SSLIVE，标签 [.live][screenshare]），ScreenShareLiveTests.cpp
 *
 *  用真实的 chatserver 验证 2a-2 接入 ChatSession 之后的完整行为（docs/屏幕共享与录制方案.md 第 11.4 节，
 *  2026-10-02 与用户确认的联调用例 1~7）：发起与通知、查询与加入、媒体 / 权限 / 录制、与一对一通话互斥、
 *  结束与系统提示、发起人掉线与宽限期、群聊资格校验。客户端一侧用 LiveChatClient 直接收发 v2 帧，
 *  不经过 FlamingoMedia.dll（DLL 侧的协议客户端在第 2c 步才有）。
 *
 *  默认不参与常规测试：用例带 [.live] 标签，Catch2 会跳过。跑之前先起 tmp/calltest 的测试服务端
 *  （python tmp/calltest/setup_calltest.py，再 start_server.ps1），它的 chatserver.conf 已把发起人掉线宽限期
 *  设为 3 秒（screensharehostgracems=3000），用例 6 依赖这一点。然后：
 *
 *      FlamingoUnit.exe "[screenshare]"
 *
 *  账号：MY0127（5127）与 MY0308（5308）互为单聊对象，二人同在群 268435463；MY0001（5001）不在该群。
 *  三个账号的密码都是 perf1234（性能测试库的统一测试密码）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <time.h>
#include "LiveChatClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../../../flamingoserver/utils/RelayToken.h"

using namespace livetest;

namespace {

//tmp/calltest 测试服务端的 chatserver 端口与 imgserver 房间中转端口
const unsigned short kShareServerPort = 26000;
const int            kShareRelayPort  = 26003;

//测试服务端签 relay 令牌用的密钥（tmp/calltest/server/etc/chatserver.conf 的 relaytokenkey）
const char* kShareRelayTokenKey = "flamingo-relay-token-secret";

//三个测试账号与密码
const char* kAccountA = "MY0127";
const char* kAccountB = "MY0308";
const char* kAccountC = "MY0001";
const char* kPassword = "perf1234";
const int64_t kUserA = 5127;
const int64_t kUserB = 5308;
const int64_t kUserC = 5001;

//A、B 同在、C 不在的测试群
const int64_t kGroupId = 268435463;

//等一条应答或推送的最长时间，单位毫秒
const int kWaitMs = 5000;

//等「宽限期超时解散」的最长时间：宽限期 3 秒 + 清理定时器间隔 + 余量，单位毫秒
const int kGraceExpireWaitMs = 9000;

//秒到毫秒的换算系数（RelayToken::verify 的当前时间以 Unix 毫秒计）
const int64_t kMsPerSecond = 1000;

//会话类型（与 Protocol.h 的 conversation_type 一致）
const int kCtypeSingle = conversation_single;
const int kCtypeGroup = conversation_group;

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
    bool ok = c.ConnectAndLogin(kShareServerPort, kTestServerPubKey, account, kPassword, reason);
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

//等一条指定事件的房间状态推送（1725），跳过其它事件
bool waitStateEvent(LiveChatClient& c, int event, Json::Value& out, int timeoutMs)
{
    const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
    while (::GetTickCount64() < deadline)
    {
        std::string body;
        if (!c.WaitFor(cmd_screenshare_state_notify, body, timeoutMs))
        {
            return false;
        }
        if (parseJson(body, out) && out["event"].asInt() == event)
        {
            return true;
        }
    }
    return false;
}

//等一条指定内容类型的消息推送（1504），跳过其它消息
bool waitMessageOfType(LiveChatClient& c, int msgtype, Json::Value& out, int timeoutMs)
{
    const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
    while (::GetTickCount64() < deadline)
    {
        std::string body;
        if (!c.WaitFor(cmd_message_notify, body, timeoutMs))
        {
            return false;
        }
        if (parseJson(body, out) && out["msgtype"].asInt() == msgtype)
        {
            return true;
        }
    }
    return false;
}

//在成员列表里找某人
const Json::Value* findMember(const Json::Value& state, int64_t uid)
{
    const Json::Value& members = state["members"];
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        if (members[i]["uid"].asString() == toStr(uid))
        {
            return &members[i];
        }
    }
    return NULL;
}

//清理上一次用例可能残留的共享：查询某会话，若有共享，发起人就结束、观看者就离开
void ensureNoShare(LiveChatClient& c, int ctype, int64_t cid)
{
    Json::Value q = request(c, cmd_screenshare_query, "{\"ctype\":" + toStr(ctype) + ",\"cid\":\"" + toStr(cid) + "\"}");
    if (q["exists"].asInt() == 1)
    {
        const std::string roomBody = "{\"roomid\":\"" + q["roomid"].asString() + "\"}";
        if (q["host"].asString() == toStr(c.UserId()))
        {
            request(c, cmd_screenshare_stop, roomBody);
        }
        else
        {
            request(c, cmd_screenshare_leave, roomBody);
        }
    }
    //上一条用例中途失败时留下的房间：发起人宽限期内重新登录会收到旧房间的中转参数与状态推送，清掉免得被本用例误取
    c.ClearBacklog();
}

//A 在单聊里对 B 发起共享，返回房间号；顺带校验应答成功
std::string startSingle(LiveChatClient& host, int64_t peer)
{
    Json::Value r = request(host, cmd_screenshare_start,
                            "{\"ctype\":1,\"cid\":\"" + toStr(peer) + "\",\"mic\":1,\"camera\":0,\"sysaudio\":0}");
    REQUIRE(r["code"].asInt() == 0);
    REQUIRE_FALSE(r["roomid"].asString().empty());
    return r["roomid"].asString();
}

std::string roomBody(const std::string& roomId)
{
    return "{\"roomid\":\"" + roomId + "\"}";
}

//校验中转参数推送（1726）：房间号、端口、座位号，以及令牌能用测试密钥校验、角色与 uid 正确
void checkRelayInfo(const Json::Value& info, const std::string& roomId, int64_t uid, int seat, int role)
{
    CHECK(info["roomid"].asString() == roomId);
    CHECK(info["relayport"].asInt() == kShareRelayPort);
    CHECK(info["seat"].asInt() == seat);
    CHECK(info["maxcameras"].asInt() > 0);
    RelayTokenPayload payload;
    REQUIRE(RelayToken::verify(info["token"].asString(), kShareRelayTokenKey, (int64_t)time(NULL) * kMsPerSecond, payload));
    CHECK(payload.sessionid == roomId);
    CHECK(payload.role == role);
    CHECK(payload.uid == uid);
    CHECK(payload.peeruid == seat);
}

} // namespace

TEST_CASE("SSLIVE1 单聊发起：发起人收到应答与座位 0 的中转参数，对端收到开始推送与共享卡片", "[.live][screenshare]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    ensureNoShare(a, kCtypeSingle, kUserB);

    std::string rid = startSingle(a, kUserB);
    std::string body;
    REQUIRE(a.WaitFor(cmd_screenshare_relay_info, body, kWaitMs));
    Json::Value info;
    REQUIRE(parseJson(body, info));
    checkRelayInfo(info, rid, kUserA, 0, RelayToken::role_screenshare_host);

    REQUIRE(b.WaitFor(cmd_screenshare_started_notify, body, kWaitMs));
    Json::Value started;
    REQUIRE(parseJson(body, started));
    CHECK(started["roomid"].asString() == rid);
    CHECK(started["ctype"].asInt() == kCtypeSingle);
    CHECK(started["cid"].asString() == toStr(kUserA));
    CHECK(started["host"].asString() == toStr(kUserA));

    Json::Value card;
    REQUIRE(waitMessageOfType(b, message_type_screenshare, card, kWaitMs));
    CHECK(card["content"]["roomid"].asString() == rid);
    CHECK(card["content"]["host"].asString() == toStr(kUserA));

    request(a, cmd_screenshare_stop, roomBody(rid));
}

TEST_CASE("SSLIVE2 对端查询到共享并加入：拿到座位 1 的中转参数，双方收到成员加入", "[.live][screenshare]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    ensureNoShare(a, kCtypeSingle, kUserB);

    std::string rid = startSingle(a, kUserB);
    Json::Value q = request(b, cmd_screenshare_query, "{\"ctype\":1,\"cid\":\"" + toStr(kUserA) + "\"}");
    CHECK(q["exists"].asInt() == 1);
    CHECK(q["roomid"].asString() == rid);
    CHECK(q["host"].asString() == toStr(kUserA));
    CHECK(q["membercount"].asInt() == 1);

    a.ClearBacklog();
    Json::Value joined = request(b, cmd_screenshare_join, roomBody(rid));
    REQUIRE(joined["code"].asInt() == 0);
    std::string body;
    REQUIRE(b.WaitFor(cmd_screenshare_relay_info, body, kWaitMs));
    Json::Value info;
    REQUIRE(parseJson(body, info));
    checkRelayInfo(info, rid, kUserB, 1, RelayToken::role_screenshare_member);

    Json::Value stateA;
    Json::Value stateB;
    REQUIRE(waitStateEvent(a, 1, stateA, kWaitMs));
    REQUIRE(waitStateEvent(b, 1, stateB, kWaitMs));
    CHECK(stateA["actor"].asString() == toStr(kUserB));
    CHECK(stateA["members"].size() == 2);
    CHECK(stateB["members"].size() == 2);

    request(a, cmd_screenshare_stop, roomBody(rid));
}

TEST_CASE("SSLIVE3 媒体开关、收回录制权限停止观看者录制、无权限时开始录制返回 713", "[.live][screenshare]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    ensureNoShare(a, kCtypeSingle, kUserB);

    std::string rid = startSingle(a, kUserB);
    REQUIRE(request(b, cmd_screenshare_join, roomBody(rid))["code"].asInt() == 0);
    a.ClearBacklog();
    b.ClearBacklog();

    Json::Value r = request(b, cmd_screenshare_member_media,
                            "{\"roomid\":\"" + rid + "\",\"mic\":1,\"camera\":0,\"sysaudio\":0}");
    REQUIRE(r["code"].asInt() == 0);
    Json::Value state;
    REQUIRE(waitStateEvent(a, 3, state, kWaitMs));
    const Json::Value* mb = findMember(state, kUserB);
    REQUIRE(mb != NULL);
    CHECK((*mb)["mic"].asInt() == 1);

    REQUIRE(request(b, cmd_screenshare_record_state, "{\"roomid\":\"" + rid + "\",\"recording\":1}")["code"].asInt() == 0);
    REQUIRE(waitStateEvent(a, 6, state, kWaitMs));

    b.ClearBacklog();
    REQUIRE(request(a, cmd_screenshare_set_permission, "{\"roomid\":\"" + rid + "\",\"record\":0}")["code"].asInt() == 0);
    REQUIRE(waitStateEvent(b, 4, state, kWaitMs));
    CHECK(state["recordallowed"].asInt() == 0);
    mb = findMember(state, kUserB);
    REQUIRE(mb != NULL);
    CHECK((*mb)["recording"].asInt() == 0);

    CHECK(request(b, cmd_screenshare_record_state, "{\"roomid\":\"" + rid + "\",\"recording\":1}")["code"].asInt() ==
          error_code_screenshare_no_record_perm);

    request(a, cmd_screenshare_stop, roomBody(rid));
}

TEST_CASE("SSLIVE4 与一对一通话互斥：共享中发起通话返回 712，呼叫共享中的人收到对方忙", "[.live][screenshare]")
{
    LiveChatClient a;
    LiveChatClient b;
    LiveChatClient c;
    login(a, kAccountA);
    login(b, kAccountB);
    login(c, kAccountC);
    ensureNoShare(a, kCtypeSingle, kUserB);

    std::string rid = startSingle(a, kUserB);

    Json::Value invite = request(a, cmd_call_invite, "{\"peerid\":\"" + toStr(kUserB) + "\",\"media\":1}");
    CHECK(invite["code"].asInt() == error_code_screenshare_in_call);

    Json::Value busy = request(c, cmd_call_invite, "{\"peerid\":\"" + toStr(kUserA) + "\",\"media\":1}");
    CHECK(busy["code"].asInt() == error_code_peer_busy);

    request(a, cmd_screenshare_stop, roomBody(rid));
}

TEST_CASE("SSLIVE5 发起人结束：双方收到结束推送（原因 1），会话里多一条系统提示，再查询不存在", "[.live][screenshare]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    ensureNoShare(a, kCtypeSingle, kUserB);

    std::string rid = startSingle(a, kUserB);
    REQUIRE(request(b, cmd_screenshare_join, roomBody(rid))["code"].asInt() == 0);
    b.ClearBacklog();

    REQUIRE(request(a, cmd_screenshare_stop, roomBody(rid))["code"].asInt() == 0);
    std::string body;
    Json::Value ended;
    REQUIRE(a.WaitFor(cmd_screenshare_ended_notify, body, kWaitMs));
    REQUIRE(parseJson(body, ended));
    CHECK(ended["roomid"].asString() == rid);
    CHECK(ended["reason"].asInt() == 1);
    CHECK(ended["cid"].asString() == toStr(kUserB));
    REQUIRE(b.WaitFor(cmd_screenshare_ended_notify, body, kWaitMs));
    REQUIRE(parseJson(body, ended));
    CHECK(ended["roomid"].asString() == rid);
    CHECK(ended["cid"].asString() == toStr(kUserA));
    CHECK(ended["duration"].asInt() >= 0);

    Json::Value tip;
    REQUIRE(waitMessageOfType(b, message_type_system, tip, kWaitMs));
    //「屏幕共享已结束」的 UTF-8 编码
    const std::string ended_text = "\xE5\xB1\x8F\xE5\xB9\x95\xE5\x85\xB1\xE4\xBA\xAB\xE5\xB7\xB2\xE7\xBB\x93\xE6\x9D\x9F";
    CHECK(tip["content"]["text"].asString().find(ended_text) != std::string::npos);

    Json::Value q = request(b, cmd_screenshare_query, "{\"ctype\":1,\"cid\":\"" + toStr(kUserA) + "\"}");
    CHECK(q["exists"].asInt() == 0);
}

TEST_CASE("SSLIVE6 发起人掉线：观看者收到离线，宽限期内回来恢复并重发中转参数，超时则解散（原因 2）", "[.live][screenshare]")
{
    LiveChatClient b;
    login(b, kAccountB);
    std::string rid;
    {
        LiveChatClient a;
        login(a, kAccountA);
        ensureNoShare(a, kCtypeSingle, kUserB);
        rid = startSingle(a, kUserB);
        REQUIRE(request(b, cmd_screenshare_join, roomBody(rid))["code"].asInt() == 0);
        b.ClearBacklog();
        a.Close();
    }
    Json::Value state;
    REQUIRE(waitStateEvent(b, 7, state, kWaitMs));
    CHECK(state["hostonline"].asInt() == 0);

    {
        LiveChatClient a2;
        login(a2, kAccountA);
        REQUIRE(waitStateEvent(b, 8, state, kWaitMs));
        CHECK(state["hostonline"].asInt() == 1);
        std::string body;
        REQUIRE(a2.WaitFor(cmd_screenshare_relay_info, body, kWaitMs));
        Json::Value info;
        REQUIRE(parseJson(body, info));
        checkRelayInfo(info, rid, kUserA, 0, RelayToken::role_screenshare_host);
        a2.Close();
    }
    REQUIRE(waitStateEvent(b, 7, state, kWaitMs));

    std::string body;
    REQUIRE(b.WaitFor(cmd_screenshare_ended_notify, body, kGraceExpireWaitMs));
    Json::Value ended;
    REQUIRE(parseJson(body, ended));
    CHECK(ended["roomid"].asString() == rid);
    CHECK(ended["reason"].asInt() == 2);
}

TEST_CASE("SSLIVE7 群聊：群成员收到开始推送并可加入，非群成员加入返回 709", "[.live][screenshare]")
{
    LiveChatClient a;
    LiveChatClient b;
    LiveChatClient c;
    login(a, kAccountA);
    login(b, kAccountB);
    login(c, kAccountC);
    ensureNoShare(a, kCtypeGroup, kGroupId);

    Json::Value r = request(a, cmd_screenshare_start, "{\"ctype\":2,\"cid\":\"" + toStr(kGroupId) + "\"}");
    REQUIRE(r["code"].asInt() == 0);
    std::string rid = r["roomid"].asString();

    std::string body;
    REQUIRE(b.WaitFor(cmd_screenshare_started_notify, body, kWaitMs));
    Json::Value started;
    REQUIRE(parseJson(body, started));
    CHECK(started["roomid"].asString() == rid);
    CHECK(started["ctype"].asInt() == kCtypeGroup);
    CHECK(started["cid"].asString() == toStr(kGroupId));

    CHECK(request(c, cmd_screenshare_join, roomBody(rid))["code"].asInt() == error_code_screenshare_not_member);
    CHECK(request(b, cmd_screenshare_join, roomBody(rid))["code"].asInt() == 0);

    request(a, cmd_screenshare_stop, roomBody(rid));
}
