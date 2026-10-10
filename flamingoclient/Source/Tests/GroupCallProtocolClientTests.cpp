/**
 *  FlamingoMedia.dll 群通话模块纯逻辑的单元测试（Suite GCC，标签 [groupcall]），GroupCallProtocolClientTests.cpp
 *  覆盖（对应 docs/群音视频通话方案.md 第 6.2 节）：
 *    - GCC1 请求构造：发起（64 位 id 为字符串、开关为 0 / 1）、加入、拒绝 / 离开、追加邀请、成员媒体、查询、管理
 *           （全员静音不带 target）；
 *    - GCC2 应答解析：通用应答、带名单的应答（busy / offline）、缺 code 与非法 JSON 失败；
 *    - GCC3 邀请推送 1741：房间快照、群名、邀请人、剩余振铃；缺 roomid / members 不是数组时失败；没有 uid 的成员项被跳过；
 *    - GCC4 房间状态 1745：事件、actor、target、成员的座位与开关；已加入人数与按人查找；缺 event 失败；
 *    - GCC5 中转参数 1746、查询应答 1749（有 / 没有 / 说有却没有房间号）、群内状态 1750（进行中、结束带改写后的卡片、
 *           结束却没有卡片内容时 msgid 归 0）；
 *    - GCC6 卡片 content：进行中、结束（带 duration）、kind 不对、缺 roomid；
 *    - GCC7 宫格行列数：1 / 2 / 3~4 / 5~6 / 7~9，0 与超上限按边界处理；
 *    - GCC8 宫格排列：本人在首格且按本地设备状态，开摄像头的其他人按座位号在前、没开的在后，振铃中的最后；
 *           超过上限的进小条；本人还在振铃时不出现；
 *    - GCC9 订阅的座位：只取宫格里开着摄像头、已加入的其他人，升序；本人、振铃中、没开摄像头的不取；
 *    - GCC10 宫格结构比较：人、振铃、摄像头一样即相同，麦克风变化不算；
 *    - GCC11 时长与文字：MM:SS / H:MM:SS、负数按 0；窗口标题、来电提示与第二行；
 *    - GCC12 订阅控制帧：帧头为控制轨、本人座位、单片，负载为 [3][N][座位...]，超过上限截断；
 *    - GCC13 全员静音（房间状态，2026-10-05）：快照的 allmuted、缺字段按 false、事件 12、解除全员静音的请求不带 target；
 *    - GCC14 全员静音下本人麦克风：被单独静音、被全员静音、在全员静音期间加入要关；有管理权、快照里仍开着、本地本来就关着
 *           时不动；麦克风按钮只对没有管理权的人锁定；状态行文字。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../../FlamingoMedia/GroupCall/GroupCallProtocolClient.h"
#include "../../FlamingoMedia/GroupCall/GroupCallLayout.h"
#include "../../FlamingoMedia/GroupCall/GroupCallMediaSession.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../net/Protocol.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace groupcall;

namespace {

//测试用的群、房间与人
const int64_t kGroupId = 600001;
const int64_t kSelfId = 5301;
const int64_t kPeerA = 5302;
const int64_t kPeerB = 5303;
const int64_t kPeerC = 5304;
const int64_t kBigId = 9007199254740993LL;   //超过 2^53，验证 64 位 id 以字符串编码不丢精度
const char* const kRoomId = "gc-600001-1";

//订阅控制帧负载里的座位个数上限（与 GroupCallMediaSession.cpp 的 kMaxSubscribeSeats 一致：座位号 1~254）
const size_t kMaxSubscribeSeatsInTest = 254;

//解析 JSON 对象，失败时让用例失败
Json::Value ParseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    REQUIRE(reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs));
    return root;
}

//构造一个房间里的人
GroupCallMember MakeMember(int64_t uid, int seat, int state, bool mic, bool camera)
{
    GroupCallMember m;
    m.m_userId = uid;
    m.m_seat = seat;
    m.m_state = state;
    m.m_mic = mic;
    m.m_camera = camera;
    return m;
}

} // namespace

TEST_CASE("GCC1 群通话请求的构造", "[groupcall]")
{
    std::vector<int64_t> invitees;
    invitees.push_back(kPeerA);
    invitees.push_back(kBigId);
    Json::Value start = ParseJson(GroupCallProtocolClient::buildStart(kGroupId, gc_media_video, invitees, true, false));
    CHECK(start["groupid"].asString() == "600001");
    CHECK(start["media"].asInt() == gc_media_video);
    REQUIRE(start["invitees"].isArray());
    REQUIRE(start["invitees"].size() == 2);
    CHECK(start["invitees"][0].asString() == "5302");
    CHECK(start["invitees"][1].asString() == "9007199254740993");
    CHECK(start["mic"].asInt() == 1);
    CHECK(start["camera"].asInt() == 0);

    Json::Value join = ParseJson(GroupCallProtocolClient::buildJoin(kRoomId, false, true));
    CHECK(join["roomid"].asString() == kRoomId);
    CHECK(join["mic"].asInt() == 0);
    CHECK(join["camera"].asInt() == 1);

    Json::Value room = ParseJson(GroupCallProtocolClient::buildRoomRequest(kRoomId));
    CHECK(room["roomid"].asString() == kRoomId);
    CHECK(room.size() == 1);

    std::vector<int64_t> more;
    more.push_back(kPeerC);
    Json::Value inviteMore = ParseJson(GroupCallProtocolClient::buildInviteMore(kRoomId, more));
    CHECK(inviteMore["roomid"].asString() == kRoomId);
    REQUIRE(inviteMore["invitees"].size() == 1);
    CHECK(inviteMore["invitees"][0].asString() == "5304");

    Json::Value media = ParseJson(GroupCallProtocolClient::buildMemberMedia(kRoomId, true, true));
    CHECK(media["mic"].asInt() == 1);
    CHECK(media["camera"].asInt() == 1);

    Json::Value query = ParseJson(GroupCallProtocolClient::buildQuery(kGroupId));
    CHECK(query["groupid"].asString() == "600001");

    Json::Value mute = ParseJson(GroupCallProtocolClient::buildManage(kRoomId, gc_manage_mute, kPeerA));
    CHECK(mute["action"].asInt() == gc_manage_mute);
    CHECK(mute["target"].asString() == "5302");
    //全员静音不针对某个人，不带 target
    Json::Value muteAll = ParseJson(GroupCallProtocolClient::buildManage(kRoomId, gc_manage_mute_all, 0));
    CHECK(muteAll["action"].asInt() == gc_manage_mute_all);
    CHECK_FALSE(muteAll.isMember("target"));
}

TEST_CASE("GCC2 群通话应答的解析", "[groupcall]")
{
    GroupCallResult r;
    REQUIRE(GroupCallProtocolClient::parseResult("{\"code\":0,\"msg\":\"ok\",\"roomid\":\"gc-600001-1\"}", r));
    CHECK(r.m_code == 0);
    CHECK(r.m_msg == "ok");
    CHECK(r.m_roomId == kRoomId);
    CHECK(r.m_busy.empty());
    CHECK(r.m_offline.empty());

    //带名单：正忙与不在线的人没有振铃
    REQUIRE(GroupCallProtocolClient::parseResult(
        "{\"code\":0,\"roomid\":\"gc-600001-1\",\"busy\":[\"5302\"],\"offline\":[\"5303\",\"9007199254740993\"]}", r));
    REQUIRE(r.m_busy.size() == 1);
    CHECK(r.m_busy[0] == kPeerA);
    REQUIRE(r.m_offline.size() == 2);
    CHECK(r.m_offline[0] == kPeerB);
    CHECK(r.m_offline[1] == kBigId);

    //722：本群已有群通话，带现有房间号
    REQUIRE(GroupCallProtocolClient::parseResult("{\"code\":722,\"roomid\":\"gc-600001-0\"}", r));
    CHECK(r.m_code == 722);
    CHECK(r.m_roomId == "gc-600001-0");

    CHECK_FALSE(GroupCallProtocolClient::parseResult("{\"msg\":\"no code\"}", r));
    CHECK_FALSE(GroupCallProtocolClient::parseResult("not json", r));
    CHECK_FALSE(GroupCallProtocolClient::parseResult("", r));
}

TEST_CASE("GCC3 邀请推送 1741 的解析", "[groupcall]")
{
    const std::string json =
        u8"{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"groupname\":\"研发部\",\"media\":2,"
        u8"\"initiator\":\"5302\",\"starttime\":1760000000000,\"inviter\":\"5302\",\"invitername\":\"张三\","
        u8"\"ringms\":45000,\"members\":[{\"uid\":\"5302\",\"seat\":1,\"state\":2,\"mic\":1,\"camera\":1,\"online\":1},"
        u8"{\"uid\":\"5301\",\"seat\":0,\"state\":1,\"mic\":0,\"camera\":0,\"online\":1},{\"seat\":3,\"state\":2}]}";
    GroupCallInvite inv;
    REQUIRE(GroupCallProtocolClient::parseInvite(json, inv));
    CHECK(inv.m_room.m_roomId == kRoomId);
    CHECK(inv.m_room.m_groupId == kGroupId);
    CHECK(inv.m_room.m_media == gc_media_video);
    CHECK(inv.m_room.m_initiatorId == kPeerA);
    CHECK(inv.m_room.m_startTime == 1760000000000LL);
    CHECK(inv.m_groupName == u8"研发部");
    CHECK(inv.m_inviterId == kPeerA);
    CHECK(inv.m_inviterName == u8"张三");
    CHECK(inv.m_ringMs == 45000);
    //没有 uid 的那一项被跳过
    REQUIRE(inv.m_room.m_members.size() == 2);
    CHECK(inv.m_room.m_members[0].m_seat == 1);
    CHECK(inv.m_room.m_members[0].m_state == gc_member_joined);
    CHECK(inv.m_room.m_members[0].m_camera);
    CHECK(inv.m_room.m_members[1].m_state == gc_member_ringing);
    CHECK(inv.m_room.joinedCount() == 1);

    CHECK_FALSE(GroupCallProtocolClient::parseInvite("{\"groupid\":\"600001\",\"members\":[]}", inv));
    CHECK_FALSE(GroupCallProtocolClient::parseInvite("{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\"}", inv));
    CHECK_FALSE(GroupCallProtocolClient::parseInvite("{\"roomid\":\"gc-600001-1\",\"members\":[]}", inv));
}

TEST_CASE("GCC4 房间状态推送 1745 的解析", "[groupcall]")
{
    const std::string json =
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"media\":1,\"initiator\":\"5301\",\"starttime\":1,"
        "\"event\":9,\"actor\":\"5301\",\"target\":\"5303\",\"members\":["
        "{\"uid\":\"5301\",\"seat\":1,\"state\":2,\"mic\":1,\"camera\":0,\"online\":1},"
        "{\"uid\":\"5303\",\"seat\":2,\"state\":2,\"mic\":0,\"camera\":0,\"online\":0},"
        "{\"uid\":\"5304\",\"seat\":0,\"state\":1}]}";
    GroupCallState st;
    REQUIRE(GroupCallProtocolClient::parseStateNotify(json, st));
    CHECK(st.m_event == gc_event_member_muted);
    CHECK(st.m_actor == kSelfId);
    CHECK(st.m_target == kPeerB);
    CHECK(st.joinedCount() == 2);
    const GroupCallMember* b = st.findMember(kPeerB);
    REQUIRE(b != NULL);
    CHECK(b->m_seat == 2);
    CHECK_FALSE(b->m_mic);
    CHECK_FALSE(b->m_online);
    //缺 online 时按在线处理
    const GroupCallMember* c = st.findMember(kPeerC);
    REQUIRE(c != NULL);
    CHECK(c->m_online);
    CHECK(st.findMember(kPeerA) == NULL);

    //缺 event 不是合法的状态推送
    CHECK_FALSE(GroupCallProtocolClient::parseStateNotify(
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"members\":[]}", st));
}

TEST_CASE("GCC5 中转参数、查询应答与群内状态的解析", "[groupcall]")
{
    GroupCallRelayInfo relay;
    REQUIRE(GroupCallProtocolClient::parseRelayInfo(
        "{\"roomid\":\"gc-600001-1\",\"relayip\":\"10.0.0.2\",\"relayport\":20002,\"token\":\"tk\",\"seat\":3,"
        "\"maxcameras\":9,\"elapsedms\":12000}", relay));
    CHECK(relay.m_relayIp == "10.0.0.2");
    CHECK(relay.m_relayPort == 20002);
    CHECK(relay.m_token == "tk");
    CHECK(relay.m_seat == 3);
    CHECK(relay.m_maxCameras == 9);
    CHECK(relay.m_elapsedMs == 12000);
    CHECK_FALSE(GroupCallProtocolClient::parseRelayInfo("{\"roomid\":\"gc-600001-1\",\"relayport\":20002}", relay));
    CHECK_FALSE(GroupCallProtocolClient::parseRelayInfo("{\"roomid\":\"gc-600001-1\",\"token\":\"tk\"}", relay));

    GroupCallQueryResult q;
    REQUIRE(GroupCallProtocolClient::parseQueryResult(
        "{\"code\":0,\"exists\":1,\"roomid\":\"gc-600001-1\",\"media\":2,\"initiator\":\"5302\",\"starttime\":5,"
        "\"count\":2,\"members\":[\"5302\",\"5303\"]}", q));
    CHECK(q.m_exists);
    CHECK(q.m_roomId == kRoomId);
    CHECK(q.m_media == gc_media_video);
    CHECK(q.m_count == 2);
    REQUIRE(q.m_members.size() == 2);
    REQUIRE(GroupCallProtocolClient::parseQueryResult("{\"code\":0,\"exists\":0}", q));
    CHECK_FALSE(q.m_exists);
    //说有、却没有房间号：按没有处理
    REQUIRE(GroupCallProtocolClient::parseQueryResult("{\"code\":0,\"exists\":1}", q));
    CHECK_FALSE(q.m_exists);

    GroupCallStatus s;
    REQUIRE(GroupCallProtocolClient::parseStatus(
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"active\":1,\"media\":1,\"initiator\":\"5301\","
        "\"starttime\":7,\"count\":3}", s));
    CHECK(s.m_active);
    CHECK(s.m_count == 3);
    CHECK(s.m_cardMsgId == 0);
    //结束：带时长与改写后的卡片
    REQUIRE(GroupCallProtocolClient::parseStatus(
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"active\":0,\"media\":1,\"count\":0,\"duration\":65,"
        "\"msgid\":\"88\",\"content\":\"{\\\"kind\\\":\\\"groupcall\\\",\\\"roomid\\\":\\\"gc-600001-1\\\"}\"}", s));
    CHECK_FALSE(s.m_active);
    CHECK(s.m_durationSeconds == 65);
    CHECK(s.m_cardMsgId == 88);
    CHECK(s.m_cardContent.find("groupcall") != std::string::npos);
    //结束却没有卡片内容：msgid 归 0，调用方据此不改写本地库
    REQUIRE(GroupCallProtocolClient::parseStatus(
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"active\":0,\"msgid\":\"88\"}", s));
    CHECK(s.m_cardMsgId == 0);
    CHECK_FALSE(GroupCallProtocolClient::parseStatus("{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\"}", s));
}

TEST_CASE("GCC6 群通话卡片 content 的解析", "[groupcall]")
{
    GroupCallCard card;
    REQUIRE(GroupCallProtocolClient::parseCard(
        u8"{\"kind\":\"groupcall\",\"roomid\":\"gc-600001-1\",\"media\":2,\"initiator\":\"5302\","
        u8"\"initiatorname\":\"张三\",\"starttime\":1760000000000}", card));
    CHECK(card.m_roomId == kRoomId);
    CHECK(card.m_media == gc_media_video);
    CHECK(card.m_initiatorId == kPeerA);
    CHECK(card.m_initiatorName == u8"张三");
    CHECK_FALSE(card.m_ended);

    REQUIRE(GroupCallProtocolClient::parseCard(
        "{\"kind\":\"groupcall\",\"roomid\":\"gc-600001-1\",\"media\":1,\"endtime\":2,\"duration\":3725}", card));
    CHECK(card.m_ended);
    CHECK(card.m_durationSeconds == 3725);

    CHECK_FALSE(GroupCallProtocolClient::parseCard("{\"kind\":\"screenshare\",\"roomid\":\"ss-1\"}", card));
    CHECK_FALSE(GroupCallProtocolClient::parseCard("{\"kind\":\"groupcall\"}", card));
    CHECK_FALSE(GroupCallProtocolClient::parseCard("", card));
}

TEST_CASE("GCC7 宫格的行列数", "[groupcall]")
{
    //格子个数 → 期望的行、列
    const int cases[][3] = {
        { 0, 1, 1 }, { 1, 1, 1 }, { 2, 1, 2 }, { 3, 2, 2 }, { 4, 2, 2 },
        { 5, 2, 3 }, { 6, 2, 3 }, { 7, 3, 3 }, { 9, 3, 3 }, { 16, 3, 3 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        int rows = 0;
        int cols = 0;
        GridShapeFor(cases[i][0], rows, cols);
        INFO("count=" << cases[i][0]);
        CHECK(rows == cases[i][1]);
        CHECK(cols == cases[i][2]);
    }
}

TEST_CASE("GCC8 宫格的排列顺序与小条", "[groupcall]")
{
    GroupCallState st;
    //快照里本人摄像头关着，但本地已打开：以本地为准
    st.m_members.push_back(MakeMember(kPeerB, 4, gc_member_joined, true, false));
    st.m_members.push_back(MakeMember(kSelfId, 2, gc_member_joined, true, false));
    st.m_members.push_back(MakeMember(kPeerC, 0, gc_member_ringing, false, false));
    st.m_members.push_back(MakeMember(kPeerA, 3, gc_member_joined, false, true));
    st.m_members.push_back(MakeMember(5310, 1, gc_member_joined, false, true));

    std::vector<GroupCallTile> grid;
    std::vector<GroupCallTile> strip;
    BuildGroupCallTiles(st, kSelfId, true, false, kGroupCallMaxGridTiles, grid, strip);
    REQUIRE(grid.size() == 5);
    CHECK(strip.empty());
    //本人 → 开摄像头的按座位号（1、3）→ 没开的（4）→ 振铃中的
    CHECK(grid[0].m_userId == kSelfId);
    CHECK(grid[0].m_self);
    CHECK(grid[0].m_camera);
    CHECK_FALSE(grid[0].m_mic);
    CHECK(grid[1].m_userId == 5310);
    CHECK(grid[2].m_userId == kPeerA);
    CHECK(grid[3].m_userId == kPeerB);
    CHECK(grid[4].m_userId == kPeerC);
    CHECK(grid[4].m_ringing);

    //宫格上限 3：其余进小条，顺序不变
    BuildGroupCallTiles(st, kSelfId, true, false, 3, grid, strip);
    REQUIRE(grid.size() == 3);
    REQUIRE(strip.size() == 2);
    CHECK(strip[0].m_userId == kPeerB);
    CHECK(strip[1].m_userId == kPeerC);

    //本人还在振铃（来电时预览）：不出现在宫格里
    GroupCallState ringingSelf;
    ringingSelf.m_members.push_back(MakeMember(kPeerA, 1, gc_member_joined, true, false));
    ringingSelf.m_members.push_back(MakeMember(kSelfId, 0, gc_member_ringing, false, false));
    BuildGroupCallTiles(ringingSelf, kSelfId, false, true, kGroupCallMaxGridTiles, grid, strip);
    REQUIRE(grid.size() == 1);
    CHECK(grid[0].m_userId == kPeerA);
}

TEST_CASE("GCC9 订阅的座位只取宫格里开摄像头的其他已加入成员", "[groupcall]")
{
    std::vector<GroupCallTile> grid;
    GroupCallTile self;
    self.m_userId = kSelfId;
    self.m_seat = 1;
    self.m_self = true;
    self.m_camera = true;
    grid.push_back(self);
    GroupCallTile a;
    a.m_userId = kPeerA;
    a.m_seat = 7;
    a.m_camera = true;
    grid.push_back(a);
    GroupCallTile b;
    b.m_userId = kPeerB;
    b.m_seat = 3;
    b.m_camera = true;
    grid.push_back(b);
    GroupCallTile noCam;
    noCam.m_userId = kPeerC;
    noCam.m_seat = 2;
    grid.push_back(noCam);
    GroupCallTile ringing;
    ringing.m_userId = 5310;
    ringing.m_ringing = true;
    ringing.m_camera = true;
    grid.push_back(ringing);

    const std::vector<int> seats = SubscribedCameraSeats(grid);
    REQUIRE(seats.size() == 2);
    CHECK(seats[0] == 3);
    CHECK(seats[1] == 7);
    //最小化时传空的宫格：一路画面都不要
    CHECK(SubscribedCameraSeats(std::vector<GroupCallTile>()).empty());
}

TEST_CASE("GCC10 宫格结构比较", "[groupcall]")
{
    std::vector<GroupCallTile> a(2);
    a[0].m_userId = kSelfId;
    a[1].m_userId = kPeerA;
    std::vector<GroupCallTile> b = a;
    CHECK(SameGridStructure(a, b));
    //麦克风变化只刷新图标，不算结构变化
    b[1].m_mic = true;
    CHECK(SameGridStructure(a, b));
    b[1].m_camera = true;
    CHECK_FALSE(SameGridStructure(a, b));
    b = a;
    b[1].m_ringing = true;
    CHECK_FALSE(SameGridStructure(a, b));
    b = a;
    b.pop_back();
    CHECK_FALSE(SameGridStructure(a, b));
}

TEST_CASE("GCC11 时长、标题与来电文字", "[groupcall]")
{
    CHECK(FormatGroupCallDuration(0) == L"00:00");
    CHECK(FormatGroupCallDuration(-5) == L"00:00");
    CHECK(FormatGroupCallDuration(65999) == L"01:05");
    CHECK(FormatGroupCallDuration(3725000) == L"1:02:05");

    CHECK(MakeGroupCallTitle(L"研发部", gc_media_voice) == L"研发部 · 群语音通话");
    CHECK(MakeGroupCallTitle(L"研发部", gc_media_video) == L"研发部 · 群视频通话");
    CHECK(MakeGroupCallTitle(L"", gc_media_voice) == L"群语音通话");

    CHECK(MakeGroupCallInvitePrompt(L"张三", gc_media_voice) == L"张三 邀请你加入群语音通话");
    CHECK(MakeGroupCallInvitePrompt(L"张三", gc_media_video) == L"张三 邀请你加入群视频通话");
    CHECK(MakeGroupCallInviteDetail(L"研发部", 3) == L"研发部 · 3 人正在通话");
    CHECK(MakeGroupCallInviteDetail(L"研发部", 0) == L"研发部");
    CHECK(MakeGroupCallInviteDetail(L"", 2) == L"2 人正在通话");
}

TEST_CASE("GCC12 订阅控制帧的编码", "[groupcall]")
{
    std::vector<int> seats;
    seats.push_back(3);
    seats.push_back(7);
    const std::string body = GroupCallMediaSession::BuildSubscribeBody(5, seats);
    screenshare::ShareFrameHeader header;
    const char* payload = NULL;
    size_t payloadLen = 0;
    REQUIRE(screenshare::ShareFraming::decodeBody(body.data(), body.size(), header, payload, payloadLen));
    CHECK(header.m_track == screenshare_track_control);
    CHECK(header.m_senderSeat == 5);
    CHECK((header.m_flags & screenshare_flag_first_frag) != 0);
    CHECK((header.m_flags & screenshare_flag_last_frag) != 0);
    REQUIRE(payloadLen == 4);
    CHECK((unsigned char)payload[0] == screenshare_control_subscribe);
    CHECK((unsigned char)payload[1] == 2);
    CHECK((unsigned char)payload[2] == 3);
    CHECK((unsigned char)payload[3] == 7);

    //空名单：一路都不要
    const std::string none = GroupCallMediaSession::BuildSubscribeBody(5, std::vector<int>());
    REQUIRE(screenshare::ShareFraming::decodeBody(none.data(), none.size(), header, payload, payloadLen));
    REQUIRE(payloadLen == 2);
    CHECK((unsigned char)payload[1] == 0);

    //超过上限截断：座位号最多 254 个（1~254），个数占 1 字节
    std::vector<int> many;
    for (int s = 1; s <= (int)kMaxSubscribeSeatsInTest + 10; ++s)
    {
        many.push_back(s % 255);
    }
    const std::string capped = GroupCallMediaSession::BuildSubscribeBody(5, many);
    REQUIRE(screenshare::ShareFraming::decodeBody(capped.data(), capped.size(), header, payload, payloadLen));
    CHECK((size_t)(unsigned char)payload[1] == kMaxSubscribeSeatsInTest);
    CHECK(payloadLen == kMaxSubscribeSeatsInTest + 2);
}

TEST_CASE("GCC13 全员静音的房间状态、解除事件与解除请求", "[groupcall]")
{
    const std::string muted =
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"media\":1,\"initiator\":\"5301\",\"starttime\":1,"
        "\"allmuted\":1,\"event\":11,\"actor\":\"5301\",\"members\":["
        "{\"uid\":\"5301\",\"seat\":1,\"state\":2,\"mic\":1,\"camera\":0,\"online\":1}]}";
    GroupCallState st;
    REQUIRE(GroupCallProtocolClient::parseStateNotify(muted, st));
    CHECK(st.m_event == gc_event_muted_all);
    CHECK(st.m_allMuted);

    const std::string unmuted =
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"media\":1,\"initiator\":\"5301\",\"starttime\":1,"
        "\"allmuted\":0,\"event\":12,\"actor\":\"5301\",\"members\":[]}";
    REQUIRE(GroupCallProtocolClient::parseStateNotify(unmuted, st));
    CHECK(st.m_event == gc_event_unmuted_all);
    CHECK_FALSE(st.m_allMuted);

    //老服务端不带 allmuted：按没有开启处理；来电推送 1741 同样带这个字段
    REQUIRE(GroupCallProtocolClient::parseStateNotify(
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"event\":11,\"actor\":\"5301\",\"members\":[]}", st));
    CHECK_FALSE(st.m_allMuted);
    GroupCallInvite invite;
    REQUIRE(GroupCallProtocolClient::parseInvite(
        "{\"roomid\":\"gc-600001-1\",\"groupid\":\"600001\",\"inviter\":\"5301\",\"allmuted\":1,\"members\":[]}",
        invite));
    CHECK(invite.m_room.m_allMuted);

    //解除全员静音不针对某个人，不带 target
    Json::Value unmuteAll = ParseJson(GroupCallProtocolClient::buildManage(kRoomId, gc_manage_unmute_all, 0));
    CHECK(unmuteAll["action"].asInt() == gc_manage_unmute_all);
    CHECK_FALSE(unmuteAll.isMember("target"));
}

TEST_CASE("GCC14 全员静音下本人麦克风的处理、麦克风按钮锁定与状态行", "[groupcall]")
{
    //本人已加入、快照里麦克风是关的
    GroupCallState st;
    st.m_members.push_back(MakeMember(kSelfId, 2, gc_member_joined, false, false));
    st.m_members.push_back(MakeMember(kPeerA, 1, gc_member_joined, true, false));

    //被单独静音
    st.m_event = gc_event_member_muted;
    st.m_actor = kPeerA;
    st.m_target = kSelfId;
    CHECK(DecideSelfMicOnState(st, kSelfId, false, true) == gc_self_mic_muted_by_peer);
    //静音的是别人：不动
    st.m_target = kPeerB;
    CHECK(DecideSelfMicOnState(st, kSelfId, false, true) == gc_self_mic_keep);

    //别人开启全员静音（老服务端不带 allmuted 时也一样）
    st.m_event = gc_event_muted_all;
    st.m_target = 0;
    CHECK(DecideSelfMicOnState(st, kSelfId, false, true) == gc_self_mic_muted_by_all);
    //本人就是操作者：不动
    st.m_actor = kSelfId;
    CHECK(DecideSelfMicOnState(st, kSelfId, true, true) == gc_self_mic_keep);

    //全员静音期间加入（事件 1，快照 allmuted）：没有管理权的要关，有管理权的不动
    st.m_event = gc_event_member_joined;
    st.m_actor = kSelfId;
    st.m_allMuted = true;
    CHECK(DecideSelfMicOnState(st, kSelfId, false, true) == gc_self_mic_muted_by_all);
    CHECK(DecideSelfMicOnState(st, kSelfId, true, true) == gc_self_mic_keep);
    //本地本来就关着：不动
    CHECK(DecideSelfMicOnState(st, kSelfId, false, false) == gc_self_mic_keep);
    //快照里本人的麦克风仍开着（有管理权的人不会被关）：不动
    st.m_members[0].m_mic = true;
    CHECK(DecideSelfMicOnState(st, kSelfId, false, true) == gc_self_mic_keep);
    //本人还在振铃、或不在快照里：不动
    st.m_members[0] = MakeMember(kSelfId, 0, gc_member_ringing, false, false);
    CHECK(DecideSelfMicOnState(st, kSelfId, false, true) == gc_self_mic_keep);
    CHECK(DecideSelfMicOnState(st, kPeerC, false, true) == gc_self_mic_keep);

    //麦克风按钮只对没有管理权的人、且只在全员静音期间锁定
    CHECK(IsGroupCallMicLocked(true, false));
    CHECK_FALSE(IsGroupCallMicLocked(true, true));
    CHECK_FALSE(IsGroupCallMicLocked(false, false));

    //状态行：临时提示优先；锁定时写「已开启全员静音」，有人说话时接在后面；没锁定时只写谁在说话
    CHECK(MakeGroupCallStatusLine(L"提示", true, L"张三") == std::wstring(L"提示"));
    CHECK(MakeGroupCallStatusLine(L"", true, L"") == std::wstring(L"已开启全员静音"));
    CHECK(MakeGroupCallStatusLine(L"", true, L"张三") == std::wstring(L"已开启全员静音 · 张三 正在说话"));
    CHECK(MakeGroupCallStatusLine(L"", false, L"张三") == std::wstring(L"张三 正在说话"));
    CHECK(MakeGroupCallStatusLine(L"", false, L"").empty());
}
