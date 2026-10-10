/**
 *  群通话房间管理器单元测试（Suite GCM），test_groupcallmanager.cpp
 *
 *  覆盖 GroupCallManager 的全部规则（docs/群音视频通话方案.md 第 5.1 节、docs/协议设计-v2.md 第 21 节）：
 *    GCM1  发起后建房：发起人坐 1 号座位并已加入、被邀请人振铃中、按群可查到、房间号以 gc- 开头且不含令牌分隔符
 *    GCM2  一个群只能有一场：再发起返回 722 并给出现有房间号
 *    GCM3  一人只能在一处：发起人已在房间里（含振铃中）返回 723；被邀请人已在别的房间里记为忙线、不振铃
 *    GCM4  人数上限：发起人加振铃的人超过上限返回 724 且不建房；直接加入超过上限返回 724；振铃中的人接听不受限
 *    GCM5  加入分配最小空闲座位、重复加入幂等、离开后座位复用；房间不存在返回 721
 *    GCM6  摄像头上限：加入时超限按关闭处理；上报打开超限返回 726 且两项都不改；关掉后别人可以打开
 *    GCM7  拒绝：只有振铃中的人能拒绝（725），拒绝后从房间移除、可被再次邀请
 *    GCM8  离开：最后一个已加入的人离开时房间结束（仍在振铃的人留在结束前的快照里）；只剩一人时不结束；振铃中的人离开按拒绝
 *    GCM9  追加邀请：请求者须已加入（725）；已在本房间的人忽略；别的房间里的人记忙线；超过上限一个都不加（724）
 *    GCM10 振铃超时：满 45 秒移除，不影响房间存续；剩余毫秒数随时间递减
 *    GCM11 掉线：振铃中的人掉线直接移除；已加入的人保留座位，宽限期内回来清除标记，超过宽限期按离开处理，最后一人则结束
 *    GCM12 relay 令牌：已加入的人才能签发，角色 13、sessionid 为房间号、peeruid 为座位号；振铃中的人签不出
 *    GCM13 管理：静音某人（原本关着时 changed 为 false）、全员静音（不含操作者）、移出（已加入或振铃中，之后可以再加入）
 *    GCM14 配置越界回落默认值；isUserInCall 覆盖已加入与振铃中
 *  每条用例都构造自己的 GroupCallManager 实例，互不共享状态。
 *
 *  典型用法：Tests.exe --test-case="GCM*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/GroupCallManager.h"
#include "../chatserversrc/GroupCallProtocol.h"   // groupcall_media
#include "../chatserversrc/Protocol.h"            // error_code_groupcall_*
#include "../utils/RelayToken.h"

namespace {

//测试用的 relay 令牌密钥与中转地址
const char* kTestKey       = "groupcall-test-key";
const char* kTestRelayIp   = "127.0.0.1";
const int   kTestRelayPort = 20003;

//测试用的时间点，Unix 毫秒
const int64_t kT0 = 1000000;

//宽限期取默认的 30 秒，单位毫秒
const int64_t kGraceMs = 30 * 1000;

//测试用的群与用户
const int64_t kGroup  = 600001;
const int64_t kGroup2 = 600002;
const int64_t kAlice  = 5127;
const int64_t kBob    = 5308;
const int64_t kCarol  = 5309;
const int64_t kDave   = 5310;
const int64_t kErin   = 5311;

//按指定的人数与摄像头上限配置一个管理器
void configureManager(GroupCallManager& mgr, int maxMembers, int maxCameras)
{
    GroupCallConfig cfg;
    cfg.m_relayTokenKey = kTestKey;
    cfg.m_relayHost = kTestRelayIp;
    cfg.m_relayPort = kTestRelayPort;
    cfg.m_maxMembers = maxMembers;
    cfg.m_maxCameras = maxCameras;
    cfg.m_offlineGraceMs = kGraceMs;
    mgr.configure(cfg);
}

//发起一场群通话（kAlice 发起，邀请 invitees），返回房间号；要求成功
std::string startRoom(GroupCallManager& mgr, const std::vector<int64_t>& invitees, int64_t groupId = kGroup,
                      int64_t initiator = kAlice)
{
    std::string roomId;
    std::string existing;
    std::vector<int64_t> busy;
    REQUIRE(mgr.startCall(initiator, groupId, groupcall_media_voice, invitees, true, false, kT0, roomId, existing,
                          busy) == error_code_ok);
    return roomId;
}

//某人在快照里的状态；不在房间里返回 0
int stateOf(const GroupCallRoom& room, int64_t userId)
{
    const GroupCallMember* m = room.findMember(userId);
    return m == NULL ? 0 : m->m_state;
}

} // namespace

TEST_CASE("GCM1 发起后建房：发起人已加入坐 1 号、被邀请人振铃中、按群可查到、房间号格式")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    std::vector<int64_t> invitees;
    invitees.push_back(kBob);
    invitees.push_back(kCarol);
    invitees.push_back(kBob);     //重复的人只算一次
    invitees.push_back(kAlice);   //发起人自己被忽略
    std::string roomId;
    std::string existing;
    std::vector<int64_t> busy;
    REQUIRE(mgr.startCall(kAlice, kGroup, groupcall_media_video, invitees, true, true, kT0, roomId, existing, busy) ==
            error_code_ok);
    CHECK(busy.empty());
    CHECK(roomId.compare(0, 3, "gc-") == 0);
    CHECK(roomId.find('.') == std::string::npos);
    CHECK(roomId.find('|') == std::string::npos);

    GroupCallRoom room;
    REQUIRE(mgr.findRoomByGroup(kGroup, room));
    CHECK(room.m_roomId == roomId);
    CHECK(room.m_groupId == kGroup);
    CHECK(room.m_initiatorId == kAlice);
    CHECK(room.m_media == groupcall_media_video);
    CHECK(room.m_startTime == kT0);
    REQUIRE(room.m_members.size() == 3);
    //已加入的在前
    CHECK(room.m_members[0].m_userId == kAlice);
    CHECK(room.m_members[0].m_state == groupcall_member_joined);
    CHECK(room.m_members[0].m_seat == GroupCallManager::kFirstSeat);
    CHECK(room.m_members[0].m_mic);
    CHECK(room.m_members[0].m_camera);
    //振铃中的人按被邀请的先后、不占座位
    CHECK(room.m_members[1].m_userId == kBob);
    CHECK(room.m_members[1].m_state == groupcall_member_ringing);
    CHECK(room.m_members[1].m_seat == 0);
    CHECK(room.m_members[1].m_inviterId == kAlice);
    CHECK(room.m_members[2].m_userId == kCarol);
    CHECK(room.joinedCount() == 1);
    CHECK(room.cameraCount() == 1);
    CHECK(!mgr.findRoomByGroup(kGroup2, room));
    CHECK(mgr.roomCount() == 1);

    //参数非法
    CHECK(mgr.startCall(0, kGroup2, groupcall_media_voice, std::vector<int64_t>(), true, false, kT0, roomId, existing,
                        busy) == error_code_bad_request);
    CHECK(mgr.startCall(kDave, kGroup2, 3, std::vector<int64_t>(), true, false, kT0, roomId, existing, busy) ==
          error_code_bad_request);
}

TEST_CASE("GCM2 一个群只能有一场：再发起返回 722 并给出现有房间号")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    const std::string roomId = startRoom(mgr, std::vector<int64_t>());
    std::string other;
    std::string existing;
    std::vector<int64_t> busy;
    CHECK(mgr.startCall(kBob, kGroup, groupcall_media_voice, std::vector<int64_t>(), true, false, kT0, other, existing,
                        busy) == error_code_groupcall_exists);
    CHECK(existing == roomId);
    CHECK(mgr.roomCount() == 1);
}

TEST_CASE("GCM3 一人只能在一处：发起人已在房间里返回 723，被邀请人在别的房间里记为忙线")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    //kBob 在群 1 的通话里振铃
    startRoom(mgr, std::vector<int64_t>(1, kBob));
    std::string roomId;
    std::string existing;
    std::vector<int64_t> busy;
    //振铃中的人去别的群发起：忙
    CHECK(mgr.startCall(kBob, kGroup2, groupcall_media_voice, std::vector<int64_t>(), true, false, kT0, roomId,
                        existing, busy) == error_code_groupcall_busy);
    //已加入的人去别的群发起：忙
    CHECK(mgr.startCall(kAlice, kGroup2, groupcall_media_voice, std::vector<int64_t>(), true, false, kT0, roomId,
                        existing, busy) == error_code_groupcall_busy);

    //kCarol 在群 2 发起，邀请 kBob（在群 1 振铃）与 kDave：kBob 忙线不振铃
    std::vector<int64_t> invitees;
    invitees.push_back(kBob);
    invitees.push_back(kDave);
    REQUIRE(mgr.startCall(kCarol, kGroup2, groupcall_media_voice, invitees, true, false, kT0, roomId, existing, busy) ==
            error_code_ok);
    REQUIRE(busy.size() == 1);
    CHECK(busy[0] == kBob);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(stateOf(room, kBob) == 0);
    CHECK(stateOf(room, kDave) == groupcall_member_ringing);

    //别的房间里的人直接加入：忙
    int seat = 0;
    bool already = false;
    CHECK(mgr.joinCall(roomId, kAlice, true, false, kT0, seat, already) == error_code_groupcall_busy);
}

TEST_CASE("GCM4 人数上限：发起超限不建房、直接加入超限 724、振铃中的人接听不受限")
{
    GroupCallManager mgr;
    configureManager(mgr, 3, 2);
    std::vector<int64_t> tooMany;
    tooMany.push_back(kBob);
    tooMany.push_back(kCarol);
    tooMany.push_back(kDave);
    std::string roomId;
    std::string existing;
    std::vector<int64_t> busy;
    CHECK(mgr.startCall(kAlice, kGroup, groupcall_media_voice, tooMany, true, false, kT0, roomId, existing, busy) ==
          error_code_groupcall_room_full);
    CHECK(mgr.roomCount() == 0);
    CHECK(!mgr.isUserInCall(kAlice));
    CHECK(!mgr.isUserInCall(kBob));

    //发起人 + 两人振铃，正好 3 人
    std::vector<int64_t> two;
    two.push_back(kBob);
    two.push_back(kCarol);
    roomId = startRoom(mgr, two);
    int seat = 0;
    bool already = false;
    //第四个人直接加入：满了
    CHECK(mgr.joinCall(roomId, kDave, true, false, kT0, seat, already) == error_code_groupcall_room_full);
    //振铃中的人接听：不占新名额
    CHECK(mgr.joinCall(roomId, kBob, true, false, kT0, seat, already) == error_code_ok);
    CHECK(mgr.joinCall(roomId, kCarol, true, false, kT0, seat, already) == error_code_ok);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.joinedCount() == 3);
}

TEST_CASE("GCM5 加入分配最小空闲座位、重复加入幂等、离开后座位复用、房间不存在 721")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    const std::string roomId = startRoom(mgr, std::vector<int64_t>(1, kBob));
    int seat = 0;
    bool already = false;
    //振铃中的人接听：取 2 号
    REQUIRE(mgr.joinCall(roomId, kBob, true, false, kT0 + 1, seat, already) == error_code_ok);
    CHECK(seat == 2);
    CHECK(!already);
    //从横幅直接加入：取 3 号
    REQUIRE(mgr.joinCall(roomId, kCarol, false, false, kT0 + 2, seat, already) == error_code_ok);
    CHECK(seat == 3);
    //重复加入：座位不变、already 为 true、麦克风不按新请求改
    REQUIRE(mgr.joinCall(roomId, kCarol, true, true, kT0 + 3, seat, already) == error_code_ok);
    CHECK(seat == 3);
    CHECK(already);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.findMember(kCarol)->m_mic);
    CHECK(room.findMember(kCarol)->m_joinTime == kT0 + 2);

    //2 号离开后，新来的人复用 2 号
    bool wasRinging = false;
    bool ended = false;
    REQUIRE(mgr.leaveCall(roomId, kBob, wasRinging, ended, room) == error_code_ok);
    CHECK(!wasRinging);
    CHECK(!ended);
    REQUIRE(mgr.joinCall(roomId, kDave, true, false, kT0 + 4, seat, already) == error_code_ok);
    CHECK(seat == 2);
    //快照按座位号升序
    REQUIRE(mgr.getRoom(roomId, room));
    REQUIRE(room.m_members.size() == 3);
    CHECK(room.m_members[0].m_seat == 1);
    CHECK(room.m_members[1].m_seat == 2);
    CHECK(room.m_members[2].m_seat == 3);

    CHECK(mgr.joinCall("gc-nope", kErin, true, false, kT0, seat, already) == error_code_groupcall_not_exist);
}

TEST_CASE("GCM6 摄像头上限：加入时超限按关闭处理，上报打开超限 726 且两项不改")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 2);
    std::string roomId;
    std::string existing;
    std::vector<int64_t> busy;
    REQUIRE(mgr.startCall(kAlice, kGroup, groupcall_media_video, std::vector<int64_t>(), true, true, kT0, roomId,
                          existing, busy) == error_code_ok);
    int seat = 0;
    bool already = false;
    REQUIRE(mgr.joinCall(roomId, kBob, true, true, kT0, seat, already) == error_code_ok);
    //第三个人要开摄像头：已满 2 路，按关闭加入
    REQUIRE(mgr.joinCall(roomId, kCarol, true, true, kT0, seat, already) == error_code_ok);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.cameraCount() == 2);
    CHECK(!room.findMember(kCarol)->m_camera);

    //上报打开：超限，两项都不改（想同时关麦克风也不生效）
    CHECK(mgr.setMemberMedia(roomId, kCarol, false, true) == error_code_groupcall_camera_limit);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.findMember(kCarol)->m_mic);
    CHECK(!room.findMember(kCarol)->m_camera);

    //本来开着的人再报一次打开：不算新增
    CHECK(mgr.setMemberMedia(roomId, kBob, false, true) == error_code_ok);
    //有人关掉后，kCarol 可以打开
    CHECK(mgr.setMemberMedia(roomId, kAlice, true, false) == error_code_ok);
    CHECK(mgr.setMemberMedia(roomId, kCarol, true, true) == error_code_ok);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.findMember(kCarol)->m_camera);
    CHECK(!room.findMember(kBob)->m_mic);

    //振铃中与不在房间里的人不能上报
    CHECK(mgr.setMemberMedia(roomId, kErin, true, false) == error_code_groupcall_not_member);
    CHECK(mgr.setMemberMedia("gc-nope", kAlice, true, false) == error_code_groupcall_not_exist);
}

TEST_CASE("GCM7 拒绝：只有振铃中的人能拒绝，拒绝后移除、可被再次邀请")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    std::vector<int64_t> invitees;
    invitees.push_back(kBob);
    invitees.push_back(kCarol);
    const std::string roomId = startRoom(mgr, invitees);
    GroupCallRoom room;
    //已加入的人不能「拒绝」，不在房间里的人也不能
    CHECK(mgr.declineCall(roomId, kAlice, room) == error_code_groupcall_not_member);
    CHECK(mgr.declineCall(roomId, kDave, room) == error_code_groupcall_not_member);
    CHECK(mgr.declineCall("gc-nope", kBob, room) == error_code_groupcall_not_exist);

    REQUIRE(mgr.declineCall(roomId, kBob, room) == error_code_ok);
    CHECK(stateOf(room, kBob) == 0);
    CHECK(stateOf(room, kCarol) == groupcall_member_ringing);
    CHECK(!mgr.isUserInCall(kBob));

    //再次邀请
    std::vector<int64_t> added;
    std::vector<int64_t> busy;
    REQUIRE(mgr.inviteMore(roomId, kAlice, std::vector<int64_t>(1, kBob), kT0 + 5, added, busy) == error_code_ok);
    REQUIRE(added.size() == 1);
    CHECK(mgr.isUserInCall(kBob));
}

TEST_CASE("GCM8 离开：最后一个已加入的人离开时结束，只剩一人不结束，振铃中的人离开按拒绝")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    std::vector<int64_t> invitees;
    invitees.push_back(kBob);
    invitees.push_back(kCarol);
    const std::string roomId = startRoom(mgr, invitees);
    int seat = 0;
    bool already = false;
    REQUIRE(mgr.joinCall(roomId, kBob, true, false, kT0, seat, already) == error_code_ok);

    bool wasRinging = false;
    bool ended = false;
    GroupCallRoom room;
    //振铃中的人离开：按拒绝
    REQUIRE(mgr.leaveCall(roomId, kCarol, wasRinging, ended, room) == error_code_ok);
    CHECK(wasRinging);
    CHECK(!ended);
    //重新邀请 kCarol，让她在结束时仍在振铃
    std::vector<int64_t> added;
    std::vector<int64_t> busy;
    REQUIRE(mgr.inviteMore(roomId, kBob, std::vector<int64_t>(1, kCarol), kT0, added, busy) == error_code_ok);

    //发起人离开：只剩 kBob 一人，不结束
    REQUIRE(mgr.leaveCall(roomId, kAlice, wasRinging, ended, room) == error_code_ok);
    CHECK(!ended);
    CHECK(room.joinedCount() == 1);
    CHECK(mgr.roomCount() == 1);
    //不在房间里的人离开
    CHECK(mgr.leaveCall(roomId, kAlice, wasRinging, ended, room) == error_code_groupcall_not_member);

    //最后一个已加入的人离开：结束；结束前的快照里仍有振铃中的 kCarol
    REQUIRE(mgr.leaveCall(roomId, kBob, wasRinging, ended, room) == error_code_ok);
    CHECK(ended);
    CHECK(stateOf(room, kCarol) == groupcall_member_ringing);
    CHECK(stateOf(room, kBob) == 0);
    CHECK(mgr.roomCount() == 0);
    CHECK(!mgr.isUserInCall(kCarol));
    GroupCallRoom gone;
    CHECK(!mgr.findRoomByGroup(kGroup, gone));
    //同一个群可以再发起
    startRoom(mgr, std::vector<int64_t>());
}

TEST_CASE("GCM9 追加邀请：请求者须已加入，忽略已在本房间的人，别的房间里的人记忙线，超限一个都不加")
{
    GroupCallManager mgr;
    configureManager(mgr, 4, 4);
    const std::string roomId = startRoom(mgr, std::vector<int64_t>(1, kBob));
    //另一个群里 kErin 已加入
    startRoom(mgr, std::vector<int64_t>(), kGroup2, kErin);

    std::vector<int64_t> added;
    std::vector<int64_t> busy;
    //振铃中的人不能追加邀请
    CHECK(mgr.inviteMore(roomId, kBob, std::vector<int64_t>(1, kCarol), kT0, added, busy) ==
          error_code_groupcall_not_member);
    CHECK(mgr.inviteMore("gc-nope", kAlice, std::vector<int64_t>(1, kCarol), kT0, added, busy) ==
          error_code_groupcall_not_exist);

    std::vector<int64_t> invitees;
    invitees.push_back(kBob);     //已在振铃，忽略
    invitees.push_back(kAlice);   //已加入，忽略
    invitees.push_back(kErin);    //在别的房间，忙线
    invitees.push_back(kCarol);
    REQUIRE(mgr.inviteMore(roomId, kAlice, invitees, kT0 + 10, added, busy) == error_code_ok);
    REQUIRE(added.size() == 1);
    CHECK(added[0] == kCarol);
    REQUIRE(busy.size() == 1);
    CHECK(busy[0] == kErin);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.findMember(kCarol)->m_inviteTime == kT0 + 10);

    //已有 3 人，上限 4，再邀请 2 人：超限一个都不加
    std::vector<int64_t> two;
    two.push_back(kDave);
    two.push_back(5312);
    CHECK(mgr.inviteMore(roomId, kAlice, two, kT0, added, busy) == error_code_groupcall_room_full);
    CHECK(added.empty());
    CHECK(!mgr.isUserInCall(kDave));
}

TEST_CASE("GCM10 振铃超时：满 45 秒移除、不影响房间，剩余毫秒数递减")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    const std::string roomId = startRoom(mgr, std::vector<int64_t>(1, kBob));
    CHECK(mgr.ringRemainingMs(roomId, kBob, kT0) == GroupCallManager::kRingTimeoutMs);
    CHECK(mgr.ringRemainingMs(roomId, kBob, kT0 + 5000) == GroupCallManager::kRingTimeoutMs - 5000);
    CHECK(mgr.ringRemainingMs(roomId, kBob, kT0 + GroupCallManager::kRingTimeoutMs + 1) == 0);
    CHECK(mgr.ringRemainingMs(roomId, kAlice, kT0) == -1);
    CHECK(mgr.ringRemainingMs("gc-nope", kBob, kT0) == -1);

    std::vector<GroupCallReaped> reaped;
    mgr.reapRingTimeouts(kT0 + GroupCallManager::kRingTimeoutMs - 1, reaped);
    CHECK(reaped.empty());
    mgr.reapRingTimeouts(kT0 + GroupCallManager::kRingTimeoutMs, reaped);
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0].m_userId == kBob);
    CHECK(!reaped[0].m_ended);
    CHECK(stateOf(reaped[0].m_room, kBob) == 0);
    CHECK(!mgr.isUserInCall(kBob));
    CHECK(mgr.roomCount() == 1);
}

TEST_CASE("GCM11 掉线：振铃中移除，已加入保留座位，宽限期内回来恢复，超时按离开，最后一人则结束")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    std::vector<int64_t> invitees;
    invitees.push_back(kBob);
    invitees.push_back(kCarol);
    const std::string roomId = startRoom(mgr, invitees);
    int seat = 0;
    bool already = false;
    REQUIRE(mgr.joinCall(roomId, kBob, true, false, kT0, seat, already) == error_code_ok);

    GroupCallRoom room;
    //不在任何房间里的人
    CHECK(mgr.onUserOffline(kDave, kT0, room) == groupcall_offline_none);
    //振铃中的人掉线：直接移除
    CHECK(mgr.onUserOffline(kCarol, kT0, room) == groupcall_offline_ringing_removed);
    CHECK(stateOf(room, kCarol) == 0);
    //已加入的人掉线：保留座位、标记离线；重复调用以第一次为准
    CHECK(mgr.onUserOffline(kBob, kT0 + 100, room) == groupcall_offline_joined_away);
    CHECK(mgr.onUserOffline(kBob, kT0 + 900, room) == groupcall_offline_joined_away);
    CHECK(room.findMember(kBob)->m_offlineSince == kT0 + 100);
    //宽限期内回来
    REQUIRE(mgr.onUserOnline(kBob, room));
    CHECK(room.findMember(kBob)->m_offlineSince == 0);
    CHECK(!mgr.onUserOnline(kBob, room));

    //kBob 再掉线、超过宽限期：按离开，房间还有 kAlice，不结束
    mgr.onUserOffline(kBob, kT0 + 1000, room);
    std::vector<GroupCallReaped> reaped;
    mgr.reapOfflineMembers(kT0 + 1000 + kGraceMs - 1, reaped);
    CHECK(reaped.empty());
    mgr.reapOfflineMembers(kT0 + 1000 + kGraceMs, reaped);
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0].m_userId == kBob);
    CHECK(!reaped[0].m_ended);
    CHECK(!mgr.isUserInCall(kBob));

    //kAlice 掉线超时：最后一个已加入的人，房间结束
    mgr.onUserOffline(kAlice, kT0 + 2000, room);
    reaped.clear();
    mgr.reapOfflineMembers(kT0 + 2000 + kGraceMs, reaped);
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0].m_ended);
    CHECK(reaped[0].m_room.m_roomId == roomId);
    CHECK(mgr.roomCount() == 0);
}

TEST_CASE("GCM12 relay 令牌：已加入的人才能签发，角色 13、sessionid 为房间号、peeruid 为座位号")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    const std::string roomId = startRoom(mgr, std::vector<int64_t>(1, kBob));
    std::string token;
    int seat = 0;
    //振铃中的人签不出
    CHECK(!mgr.signTokenFor(roomId, kBob, kT0, token, seat));
    CHECK(!mgr.signTokenFor("gc-nope", kAlice, kT0, token, seat));

    REQUIRE(mgr.signTokenFor(roomId, kAlice, kT0, token, seat));
    CHECK(seat == 1);
    RelayTokenPayload payload;
    REQUIRE(RelayToken::verify(token, kTestKey, kT0 + 1, payload));
    CHECK(payload.sessionid == roomId);
    CHECK(payload.role == RelayToken::role_groupcall_member);
    CHECK(payload.uid == kAlice);
    CHECK(payload.peeruid == 1);
    CHECK(payload.expire == kT0 + GroupCallManager::kRelayTokenTtlMs);
    //换了密钥校验不过
    CHECK(!RelayToken::verify(token, "other-key", kT0 + 1, payload));
}

TEST_CASE("GCM13 管理：静音某人、全员静音（不含操作者）、移出（已加入或振铃中，之后可再加入）")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    std::vector<int64_t> invitees;
    invitees.push_back(kBob);
    invitees.push_back(kCarol);
    invitees.push_back(kDave);
    const std::string roomId = startRoom(mgr, invitees);
    int seat = 0;
    bool already = false;
    REQUIRE(mgr.joinCall(roomId, kBob, true, false, kT0, seat, already) == error_code_ok);
    REQUIRE(mgr.joinCall(roomId, kCarol, false, false, kT0, seat, already) == error_code_ok);

    bool changed = false;
    REQUIRE(mgr.muteMember(roomId, kBob, changed) == error_code_ok);
    CHECK(changed);
    REQUIRE(mgr.muteMember(roomId, kBob, changed) == error_code_ok);
    CHECK(!changed);
    CHECK(mgr.muteMember(roomId, kDave, changed) == error_code_groupcall_not_member);
    CHECK(mgr.muteMember("gc-nope", kBob, changed) == error_code_groupcall_not_exist);

    //全员静音：kBob 与 kCarol 本来就关着，只剩 kAlice 开着但她是操作者
    std::vector<int64_t> muted;
    REQUIRE(mgr.muteAll(roomId, std::vector<int64_t>(1, kAlice), muted) == error_code_ok);
    CHECK(muted.empty());
    REQUIRE(mgr.setMemberMedia(roomId, kBob, true, false) == error_code_ok);
    REQUIRE(mgr.muteAll(roomId, std::vector<int64_t>(1, kCarol), muted) == error_code_ok);
    REQUIRE(muted.size() == 2);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.findMember(kAlice)->m_mic);
    CHECK(!room.findMember(kBob)->m_mic);

    //移出振铃中的人与已加入的人
    bool ended = false;
    REQUIRE(mgr.removeMember(roomId, kDave, ended, room) == error_code_ok);
    CHECK(!ended);
    CHECK(stateOf(room, kDave) == 0);
    REQUIRE(mgr.removeMember(roomId, kBob, ended, room) == error_code_ok);
    CHECK(!ended);
    CHECK(!mgr.isUserInCall(kBob));
    CHECK(mgr.removeMember(roomId, kBob, ended, room) == error_code_groupcall_not_member);
    //被移出的人可以再加入
    CHECK(mgr.joinCall(roomId, kBob, true, false, kT0, seat, already) == error_code_ok);
}

TEST_CASE("GCM15 全员静音是房间状态：豁免的人不受影响，后加入的人默认静音，开麦被拒，解除后不替人开麦")
{
    GroupCallManager mgr;
    configureManager(mgr, 16, 9);
    std::vector<int64_t> invitees;
    invitees.push_back(kBob);
    invitees.push_back(kCarol);
    invitees.push_back(kDave);
    const std::string roomId = startRoom(mgr, invitees);
    int seat = 0;
    bool already = false;
    REQUIRE(mgr.joinCall(roomId, kBob, true, false, kT0, seat, already) == error_code_ok);
    REQUIRE(mgr.joinCall(roomId, kDave, true, false, kT0, seat, already) == error_code_ok);
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.m_allMuted);

    //开启：kAlice（发起人）与 kDave（调用方算出的管理员）豁免，只有 kBob 被静音
    std::vector<int64_t> exempt;
    exempt.push_back(kAlice);
    exempt.push_back(kDave);
    std::vector<int64_t> muted;
    REQUIRE(mgr.muteAll(roomId, exempt, muted) == error_code_ok);
    REQUIRE(muted.size() == 1);
    CHECK(muted[0] == kBob);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.m_allMuted);
    CHECK(room.findMember(kAlice)->m_mic);
    CHECK(room.findMember(kDave)->m_mic);
    CHECK(!room.findMember(kBob)->m_mic);

    //没有豁免的人要开麦：728，两项都不改；关着麦克风开摄像头照常
    CHECK(mgr.setMemberMedia(roomId, kBob, true, true, false) == error_code_groupcall_no_permission);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.findMember(kBob)->m_camera);
    CHECK(mgr.setMemberMedia(roomId, kBob, false, true, false) == error_code_ok);
    //有豁免的人可以关了再开
    CHECK(mgr.setMemberMedia(roomId, kDave, false, false, true) == error_code_ok);
    CHECK(mgr.setMemberMedia(roomId, kDave, true, false, true) == error_code_ok);

    //后加入的人：没有豁免的按关麦克风加入，有豁免的照传入的值
    REQUIRE(mgr.joinCall(roomId, kCarol, true, false, kT0, seat, already, false) == error_code_ok);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.findMember(kCarol)->m_mic);
    std::vector<int64_t> added;
    std::vector<int64_t> busy;
    REQUIRE(mgr.inviteMore(roomId, kAlice, std::vector<int64_t>(1, kErin), kT0, added, busy) == error_code_ok);
    REQUIRE(mgr.joinCall(roomId, kErin, true, false, kT0, seat, already, true) == error_code_ok);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.findMember(kErin)->m_mic);

    //再次开启是幂等的
    REQUIRE(mgr.muteAll(roomId, exempt, muted) == error_code_ok);
    CHECK(std::find(muted.begin(), muted.end(), kErin) != muted.end());

    //解除：状态清掉，但被静音的人仍关着麦克风，之后可以自己打开；再解除一次没有变化
    bool changed = false;
    REQUIRE(mgr.unmuteAll(roomId, changed) == error_code_ok);
    CHECK(changed);
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(!room.m_allMuted);
    CHECK(!room.findMember(kBob)->m_mic);
    CHECK(!room.findMember(kCarol)->m_mic);
    CHECK(mgr.setMemberMedia(roomId, kBob, true, true, false) == error_code_ok);
    REQUIRE(mgr.unmuteAll(roomId, changed) == error_code_ok);
    CHECK(!changed);
    CHECK(mgr.unmuteAll("gc-nope", changed) == error_code_groupcall_not_exist);
    CHECK(mgr.muteAll("gc-nope", exempt, muted) == error_code_groupcall_not_exist);
}

TEST_CASE("GCM14 配置越界回落默认值，isUserInCall 覆盖已加入与振铃中")
{
    GroupCallManager mgr;
    configureManager(mgr, 1, 0);
    GroupCallConfig cfg = mgr.getConfig();
    CHECK(cfg.m_maxMembers == GroupCallManager::kDefaultMaxMembers);
    CHECK(cfg.m_maxCameras == GroupCallManager::kDefaultMaxCameras);
    //人数上限越界回落默认值 16；摄像头上限 400 超过人数上限，压到人数上限
    configureManager(mgr, 300, 400);
    cfg = mgr.getConfig();
    CHECK(cfg.m_maxMembers == GroupCallManager::kDefaultMaxMembers);
    CHECK(cfg.m_maxCameras == GroupCallManager::kDefaultMaxMembers);
    configureManager(mgr, 4, 9);
    cfg = mgr.getConfig();
    CHECK(cfg.m_maxMembers == 4);
    CHECK(cfg.m_maxCameras == 4);
    GroupCallConfig bad;
    bad.m_offlineGraceMs = 0;
    mgr.configure(bad);
    CHECK(mgr.getConfig().m_offlineGraceMs == GroupCallManager::kDefaultOfflineGraceMs);

    const std::string roomId = startRoom(mgr, std::vector<int64_t>(1, kBob));
    std::string where;
    CHECK(mgr.isUserInCall(kAlice, &where));
    CHECK(where == roomId);
    CHECK(mgr.isUserInCall(kBob));
    CHECK(!mgr.isUserInCall(kCarol));
    CHECK(mgr.setInitiatorName(roomId, "Alice"));
    CHECK(mgr.setCardMsgId(roomId, 9001));
    CHECK(!mgr.setCardMsgId("gc-nope", 1));
    GroupCallRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    CHECK(room.m_initiatorName == "Alice");
    CHECK(room.m_cardMsgId == 9001);
}
