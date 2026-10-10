/**
 *  屏幕共享房间管理器单元测试（Suite SCM），test_screensharemanager.cpp
 *
 *  覆盖 ScreenShareManager 的全部规则（docs/屏幕共享与录制方案.md 第 11.4 节 2a-1，2026-10-02 与用户确认的
 *  用例 SCM1~SCM15）：建房与按会话查询、同一会话只能一个共享、一人只能在一个房间、单聊配对对称、座位号分配与
 *  复用、人数上限、离开与结束、成员媒体开关与摄像头上限、权限设置（收回录制权限停止观看者录制）、全员静音、
 *  录制权限、按座位签发 relay 令牌、发起人掉线宽限与观看者掉线、isUserInShare。
 *  第 12 期补 SSPW2b（加入密码随房间保存）；第 13 期补 SSMU1（按人静音）、SSFM1b（强制模式与禁止观看者录制随房间保存）、
 *  SSFM2（强制模式下观看者不能离开）、SSFM3（强制模式下观看者掉线后回到观看）、SSFM3c（房间记下发起人昵称）；
 *  第 14 期补 SSRM1（移出观看者、允许或不允许再次加入）、SSRM1b（强制模式下移出，含掉线后待回到观看的人）；
 *  第 15 期补 SSTI2（共享标题与描述随房间保存，只有发起人能设置）、SSTI3c（房间记下共享卡片的消息 id）。
 *  第 18 期补 SSCT1（控制键鼠的申请与撤回）、SSCT2（授予、拒绝与收回）、SSCT3（离开、移出、掉线时清除控制关系）。
 *  每条用例都构造自己的 ScreenShareManager 实例，互不共享状态。
 *
 *  典型用法：Tests.exe --test-case="SCM*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/ScreenShareManager.h"
#include "../chatserversrc/ScreenShareProtocol.h"   // screenshare_control_action（第 18 期控制键鼠）
#include "../chatserversrc/Protocol.h"      // conversation_type、error_code_screenshare_*
#include "../utils/RelayToken.h"

namespace {

//测试用的 relay 令牌密钥与中转地址
const char* kTestKey      = "screenshare-test-key";
const char* kTestRelayIp  = "127.0.0.1";
const int   kTestRelayPort = 20003;

//测试用的时间点，Unix 毫秒
const int64_t kT0 = 1000000;

//宽限期取默认的 30 秒，单位毫秒
const int64_t kGraceMs = 30 * 1000;

//按指定的人数与摄像头上限配置一个管理器
void configureManager(ScreenShareManager& mgr, int maxMembers, int maxCameras)
{
    ScreenShareConfig cfg;
    cfg.m_relayTokenKey = kTestKey;
    cfg.m_relayHost = kTestRelayIp;
    cfg.m_relayPort = kTestRelayPort;
    cfg.m_maxMembers = maxMembers;
    cfg.m_maxCameras = maxCameras;
    cfg.m_hostGraceMs = kGraceMs;
    mgr.configure(cfg);
}

//在群里建一个房间并让若干观看者依次加入；返回房间号
std::string makeGroupRoom(ScreenShareManager& mgr, int64_t hostId, int64_t groupId, const std::vector<int64_t>& viewers)
{
    std::string roomId;
    REQUIRE(mgr.startShare(hostId, conversation_group, groupId, false, false, false, kT0, roomId) == 0);
    for (size_t i = 0; i < viewers.size(); ++i)
    {
        int seat = -1;
        REQUIRE(mgr.joinShare(roomId, viewers[i], kT0 + 1, seat) == 0);
    }
    return roomId;
}

//取房间里某人的成员记录（拷贝）；不在房间里时让用例失败
ScreenShareMember memberOf(ScreenShareManager& mgr, const std::string& roomId, int64_t userId)
{
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    const ScreenShareMember* m = room.findMember(userId);
    REQUIRE(m != NULL);
    return *m;
}

bool contains(const std::vector<int64_t>& v, int64_t x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

} // namespace

TEST_CASE("SCM1 发起共享后建房：发起人座位 0、按会话可查到、房间号不含令牌分隔符")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1001, conversation_single, 1002, true, false, false, kT0, rid) == 0);
    REQUIRE_FALSE(rid.empty());
    //房间号要作 RelayToken 的 sessionid，不得含 '.' 与 '|'
    CHECK(rid.find('.') == std::string::npos);
    CHECK(rid.find('|') == std::string::npos);

    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_roomId == rid);
    CHECK(room.m_hostId == 1001);
    CHECK(room.m_ctype == conversation_single);
    CHECK(room.m_startTime == kT0);
    CHECK(room.m_annotateAllowed);
    CHECK(room.m_recordAllowed);
    CHECK(room.m_hostOfflineSince == 0);
    REQUIRE(room.m_members.size() == 1);
    CHECK(room.m_members[0].m_userId == 1001);
    CHECK(room.m_members[0].m_seat == 0);
    CHECK(room.m_members[0].m_mic);
    CHECK_FALSE(room.m_members[0].m_camera);
    CHECK_FALSE(room.m_members[0].m_systemAudio);

    ScreenShareRoom byConv;
    REQUIRE(mgr.findRoomByConversation(conversation_single, 1001, 1002, byConv));
    CHECK(byConv.m_roomId == rid);

    //群聊：任何人按群 id 都能查到
    std::string gid;
    REQUIRE(mgr.startShare(1003, conversation_group, 600001, false, false, false, kT0, gid) == 0);
    ScreenShareRoom byGroup;
    REQUIRE(mgr.findRoomByConversation(conversation_group, 1999, 600001, byGroup));
    CHECK(byGroup.m_roomId == gid);
    CHECK(gid != rid);

    //参数非法：单聊对端是自己、会话类型不认识、会话 id 非正
    std::string bad;
    CHECK(mgr.startShare(1004, conversation_single, 1004, false, false, false, kT0, bad) == error_code_bad_request);
    CHECK(mgr.startShare(1004, 3, 600002, false, false, false, kT0, bad) == error_code_bad_request);
    CHECK(mgr.startShare(1004, conversation_group, 0, false, false, false, kT0, bad) == error_code_bad_request);
}

TEST_CASE("SCM2 同一会话已有共享时再发起返回 710")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1001, conversation_single, 1002, false, false, false, kT0, rid) == 0);
    //对端从自己的角度发起，仍是同一个单聊会话
    std::string other;
    CHECK(mgr.startShare(1002, conversation_single, 1001, false, false, false, kT0, other) ==
          error_code_screenshare_exists);

    std::string gid;
    REQUIRE(mgr.startShare(1003, conversation_group, 600001, false, false, false, kT0, gid) == 0);
    CHECK(mgr.startShare(1004, conversation_group, 600001, false, false, false, kT0, other) ==
          error_code_screenshare_exists);
}

TEST_CASE("SCM3 已在某个房间里的人再发起或加入别的房间返回 714")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string single;
    REQUIRE(mgr.startShare(1001, conversation_single, 1002, false, false, false, kT0, single) == 0);
    std::string group = makeGroupRoom(mgr, 1003, 600001, std::vector<int64_t>(1, 1004));

    //发起人再发起
    std::string other;
    CHECK(mgr.startShare(1001, conversation_group, 600002, false, false, false, kT0, other) ==
          error_code_screenshare_in_other_room);
    //观看者再发起、再加入别的房间
    CHECK(mgr.startShare(1004, conversation_group, 600003, false, false, false, kT0, other) ==
          error_code_screenshare_in_other_room);
    int seat = -1;
    CHECK(mgr.joinShare(single, 1004, kT0, seat) == error_code_screenshare_in_other_room);
    //发起人加入别人的房间
    CHECK(mgr.joinShare(group, 1001, kT0, seat) == error_code_screenshare_in_other_room);
}

TEST_CASE("SCM4 单聊按双方 userid 配对：两边查到同一个房间，换一对人查不到")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1001, conversation_single, 1002, false, false, false, kT0, rid) == 0);
    ScreenShareRoom fromHost;
    ScreenShareRoom fromPeer;
    REQUIRE(mgr.findRoomByConversation(conversation_single, 1001, 1002, fromHost));
    REQUIRE(mgr.findRoomByConversation(conversation_single, 1002, 1001, fromPeer));
    CHECK(fromHost.m_roomId == rid);
    CHECK(fromPeer.m_roomId == rid);

    ScreenShareRoom none;
    CHECK_FALSE(mgr.findRoomByConversation(conversation_single, 1003, 1001, none));
    CHECK_FALSE(mgr.findRoomByConversation(conversation_group, 1001, 1002, none));
}

TEST_CASE("SCM5 加入分配最小空闲座位号，重复加入幂等，房间不存在返回 708")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1001, conversation_group, 600001, false, false, false, kT0, rid) == 0);
    int seat1 = -1;
    int seat2 = -1;
    REQUIRE(mgr.joinShare(rid, 1002, kT0, seat1) == 0);
    REQUIRE(mgr.joinShare(rid, 1003, kT0, seat2) == 0);
    CHECK(seat1 == 1);
    CHECK(seat2 == 2);

    int again = -1;
    CHECK(mgr.joinShare(rid, 1002, kT0 + 5, again) == 0);
    CHECK(again == 1);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_members.size() == 3);

    int seat = -1;
    CHECK(mgr.joinShare("no-such-room", 1004, kT0, seat) == error_code_screenshare_not_exist);
}

TEST_CASE("SCM6 人数达到上限（含发起人）后再加入返回 711；配置越界按默认值修正")
{
    ScreenShareManager mgr;
    configureManager(mgr, 3, 2);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002, 1003});
    int seat = -1;
    CHECK(mgr.joinShare(rid, 1004, kT0, seat) == error_code_screenshare_room_full);

    ScreenShareManager defaults;
    configureManager(defaults, 0, 0);
    CHECK(defaults.getConfig().m_maxMembers == ScreenShareManager::kDefaultMaxMembers);
    CHECK(defaults.getConfig().m_maxCameras == ScreenShareManager::kDefaultMaxCameras);
}

TEST_CASE("SCM7 观看者离开释放座位并被复用；非成员离开返回 709；发起人离开等同结束")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002, 1003});
    bool dissolved = true;
    ScreenShareRoom before;
    REQUIRE(mgr.leaveShare(rid, 1002, dissolved, before) == 0);
    CHECK_FALSE(dissolved);
    CHECK(before.findMember(1002) != NULL);
    CHECK_FALSE(mgr.isUserInShare(1002));

    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1004, kT0, seat) == 0);
    CHECK(seat == 1);

    ScreenShareRoom ignored;
    CHECK(mgr.leaveShare(rid, 1999, dissolved, ignored) == error_code_screenshare_not_member);
    CHECK(mgr.leaveShare("no-such-room", 1003, dissolved, ignored) == error_code_screenshare_not_exist);

    REQUIRE(mgr.leaveShare(rid, 1001, dissolved, before) == 0);
    CHECK(dissolved);
    ScreenShareRoom gone;
    CHECK_FALSE(mgr.getRoom(rid, gone));
    CHECK_FALSE(mgr.isUserInShare(1003));
    CHECK_FALSE(mgr.isUserInShare(1004));
}

TEST_CASE("SCM8 只有发起人能结束共享；结束后房间消失、所有人都能再发起")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002, 1003});
    ScreenShareRoom snapshot;
    CHECK(mgr.stopShare(rid, 1002, snapshot) == error_code_screenshare_not_host);
    CHECK(mgr.stopShare("no-such-room", 1001, snapshot) == error_code_screenshare_not_exist);

    REQUIRE(mgr.stopShare(rid, 1001, snapshot) == 0);
    CHECK(snapshot.m_roomId == rid);
    CHECK(snapshot.m_startTime == kT0);
    CHECK(snapshot.m_members.size() == 3);

    ScreenShareRoom gone;
    CHECK_FALSE(mgr.getRoom(rid, gone));
    CHECK_FALSE(mgr.findRoomByConversation(conversation_group, 1001, 600001, gone));

    std::string again;
    CHECK(mgr.startShare(1002, conversation_group, 600001, false, false, false, kT0 + 10, again) == 0);
    std::string other;
    CHECK(mgr.startShare(1003, conversation_single, 1001, false, false, false, kT0 + 10, other) == 0);
}

TEST_CASE("SCM9 成员开关媒体；摄像头达到上限后返回 715 且三项都不改，有人关掉后可再开")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 2);

    std::string rid;
    REQUIRE(mgr.startShare(1001, conversation_group, 600001, false, true, false, kT0, rid) == 0);
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1002, kT0, seat) == 0);
    REQUIRE(mgr.joinShare(rid, 1003, kT0, seat) == 0);

    REQUIRE(mgr.setMemberMedia(rid, 1002, true, true, false) == 0);
    CHECK(memberOf(mgr, rid, 1002).m_mic);
    CHECK(memberOf(mgr, rid, 1002).m_camera);

    //发起人与 1002 已占满 2 路摄像头
    CHECK(mgr.setMemberMedia(rid, 1003, true, true, false) == error_code_screenshare_camera_limit);
    CHECK_FALSE(memberOf(mgr, rid, 1003).m_mic);
    CHECK_FALSE(memberOf(mgr, rid, 1003).m_camera);

    REQUIRE(mgr.setMemberMedia(rid, 1002, true, false, false) == 0);
    CHECK(mgr.setMemberMedia(rid, 1003, false, true, false) == 0);
    CHECK(memberOf(mgr, rid, 1003).m_camera);

    CHECK(mgr.setMemberMedia(rid, 1999, true, false, false) == error_code_screenshare_not_member);
    CHECK(mgr.setMemberMedia("no-such-room", 1002, true, false, false) == error_code_screenshare_not_exist);
}

TEST_CASE("SCM10 只有发起人能设置权限；收回录制权限停止观看者录制，发起人自己的录制不受影响")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002, 1003});
    std::vector<int64_t> stopped;
    CHECK(mgr.setPermission(rid, 1002, 0, 0, stopped) == error_code_screenshare_not_host);

    REQUIRE(mgr.setRecording(rid, 1001, true) == 0);
    REQUIRE(mgr.setRecording(rid, 1002, true) == 0);

    REQUIRE(mgr.setPermission(rid, 1001, -1, 0, stopped) == 0);
    CHECK(stopped.size() == 1);
    CHECK(contains(stopped, 1002));
    CHECK_FALSE(memberOf(mgr, rid, 1002).m_recording);
    CHECK(memberOf(mgr, rid, 1001).m_recording);

    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK_FALSE(room.m_recordAllowed);
    CHECK(room.m_annotateAllowed);

    stopped.clear();
    REQUIRE(mgr.setPermission(rid, 1001, 0, -1, stopped) == 0);
    CHECK(stopped.empty());
    REQUIRE(mgr.getRoom(rid, room));
    CHECK_FALSE(room.m_annotateAllowed);
    CHECK_FALSE(room.m_recordAllowed);

    CHECK(mgr.setPermission("no-such-room", 1001, 1, 1, stopped) == error_code_screenshare_not_exist);
}

TEST_CASE("SCM11 全员静音只有发起人能做，关掉其他人的麦克风并返回原本开着的人")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1001, conversation_group, 600001, true, false, false, kT0, rid) == 0);
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1002, kT0, seat) == 0);
    REQUIRE(mgr.joinShare(rid, 1003, kT0, seat) == 0);
    REQUIRE(mgr.setMemberMedia(rid, 1002, true, false, false) == 0);

    std::vector<int64_t> muted;
    CHECK(mgr.muteAll(rid, 1002, muted) == error_code_screenshare_not_host);

    REQUIRE(mgr.muteAll(rid, 1001, muted) == 0);
    CHECK(muted.size() == 1);
    CHECK(contains(muted, 1002));
    CHECK_FALSE(memberOf(mgr, rid, 1002).m_mic);
    CHECK(memberOf(mgr, rid, 1001).m_mic);

    //被静音的人可以自己重新打开
    CHECK(mgr.setMemberMedia(rid, 1002, true, false, false) == 0);
    CHECK(memberOf(mgr, rid, 1002).m_mic);
}

TEST_CASE("SCM12 录制：发起人随时可录，观看者须有权限，停止录制总是允许")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002});
    std::vector<int64_t> stopped;
    REQUIRE(mgr.setPermission(rid, 1001, -1, 0, stopped) == 0);

    CHECK(mgr.setRecording(rid, 1001, true) == 0);
    CHECK(mgr.setRecording(rid, 1002, true) == error_code_screenshare_no_record_perm);
    CHECK_FALSE(memberOf(mgr, rid, 1002).m_recording);
    CHECK(mgr.setRecording(rid, 1002, false) == 0);

    REQUIRE(mgr.setPermission(rid, 1001, -1, 1, stopped) == 0);
    CHECK(mgr.setRecording(rid, 1002, true) == 0);
    CHECK(memberOf(mgr, rid, 1002).m_recording);

    CHECK(mgr.setRecording(rid, 1999, true) == error_code_screenshare_not_member);
    CHECK(mgr.setRecording("no-such-room", 1002, true) == error_code_screenshare_not_exist);
}

TEST_CASE("SCM13 签发中转令牌：发起人与观看者角色不同，peeruid 为座位号，非成员签发失败")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002});
    const int64_t now = kT0 + 100;

    std::string hostToken;
    int hostSeat = -1;
    REQUIRE(mgr.signTokenFor(rid, 1001, now, hostToken, hostSeat));
    CHECK(hostSeat == 0);
    RelayTokenPayload host;
    REQUIRE(RelayToken::verify(hostToken, kTestKey, now, host));
    CHECK(host.sessionid == rid);
    CHECK(host.role == RelayToken::role_screenshare_host);
    CHECK(host.uid == 1001);
    CHECK(host.peeruid == 0);
    CHECK(host.expire == now + ScreenShareManager::kRelayTokenTtlMs);

    std::string viewerToken;
    int viewerSeat = -1;
    REQUIRE(mgr.signTokenFor(rid, 1002, now, viewerToken, viewerSeat));
    CHECK(viewerSeat == 1);
    RelayTokenPayload viewer;
    REQUIRE(RelayToken::verify(viewerToken, kTestKey, now, viewer));
    CHECK(viewer.sessionid == rid);
    CHECK(viewer.role == RelayToken::role_screenshare_member);
    CHECK(viewer.uid == 1002);
    CHECK(viewer.peeruid == 1);

    std::string none;
    int seat = -1;
    CHECK_FALSE(mgr.signTokenFor(rid, 1999, now, none, seat));
    CHECK_FALSE(mgr.signTokenFor("no-such-room", 1001, now, none, seat));
}

TEST_CASE("SCM14 掉线：观看者等同离开；发起人标记离线，宽限期内回来恢复，超时被清理解散")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002, 1003});
    ScreenShareRoom after;
    CHECK(mgr.onUserOffline(1002, kT0 + 10, after) == screenshare_offline_member_left);
    CHECK(after.findMember(1002) == NULL);
    CHECK_FALSE(mgr.isUserInShare(1002));

    CHECK(mgr.onUserOffline(1999, kT0 + 10, after) == screenshare_offline_none);

    const int64_t away = kT0 + 1000;
    CHECK(mgr.onUserOffline(1001, away, after) == screenshare_offline_host_away);
    CHECK(after.m_hostOfflineSince == away);
    CHECK(mgr.isUserInShare(1001));

    ScreenShareRoom back;
    REQUIRE(mgr.onUserOnline(1001, back));
    CHECK(back.m_hostOfflineSince == 0);
    CHECK_FALSE(mgr.onUserOnline(1003, back));

    const int64_t awayAgain = kT0 + 5000;
    CHECK(mgr.onUserOffline(1001, awayAgain, after) == screenshare_offline_host_away);
    std::vector<ScreenShareRoom> expired;
    mgr.reapExpiredHosts(awayAgain + kGraceMs - 1, expired);
    CHECK(expired.empty());
    ScreenShareRoom still;
    CHECK(mgr.getRoom(rid, still));

    mgr.reapExpiredHosts(awayAgain + kGraceMs, expired);
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].m_roomId == rid);
    CHECK_FALSE(mgr.getRoom(rid, still));
    CHECK_FALSE(mgr.isUserInShare(1001));
    CHECK_FALSE(mgr.isUserInShare(1003));
}

TEST_CASE("SCM15 isUserInShare 反映每个人是否在共享中")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002});
    std::string where;
    CHECK(mgr.isUserInShare(1001, &where));
    CHECK(where == rid);
    CHECK(mgr.isUserInShare(1002, &where));
    CHECK(where == rid);
    CHECK_FALSE(mgr.isUserInShare(1003, &where));
    CHECK(mgr.isUserInShare(1001));

    bool dissolved = false;
    ScreenShareRoom snapshot;
    REQUIRE(mgr.leaveShare(rid, 1002, dissolved, snapshot) == 0);
    CHECK_FALSE(mgr.isUserInShare(1002));
    REQUIRE(mgr.stopShare(rid, 1001, snapshot) == 0);
    CHECK_FALSE(mgr.isUserInShare(1001));
}

TEST_CASE("SSPW2b 共享密码：发起时随房间保存，不设时为空")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string withPwd;
    REQUIRE(mgr.startShare(1101, conversation_group, 600101, false, false, false, kT0, withPwd, "482915") == 0);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(withPwd, room));
    CHECK(room.m_password == "482915");

    std::string noPwd;
    REQUIRE(mgr.startShare(1102, conversation_group, 600102, false, false, false, kT0, noPwd) == 0);
    REQUIRE(mgr.getRoom(noPwd, room));
    CHECK(room.m_password.empty());
}

TEST_CASE("SSMU1 按人静音：只有发起人能做，关掉对方的麦克风；对方本来就关着时不算改动")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1001, 600001, std::vector<int64_t>{1002, 1003});
    REQUIRE(mgr.setMemberMedia(rid, 1002, true, false, false) == 0);
    REQUIRE(mgr.setMemberMedia(rid, 1003, true, false, false) == 0);

    //发起人静音 1002：1002 的麦克风关掉，1003 不受影响
    bool changed = false;
    CHECK(mgr.muteMember(rid, 1001, 1002, changed) == 0);
    CHECK(changed);
    CHECK_FALSE(memberOf(mgr, rid, 1002).m_mic);
    CHECK(memberOf(mgr, rid, 1003).m_mic);

    //再静音一次：成功但没有改动，调用方据此不广播
    CHECK(mgr.muteMember(rid, 1001, 1002, changed) == 0);
    CHECK_FALSE(changed);

    //被静音的人可以自己重新打开
    CHECK(mgr.setMemberMedia(rid, 1002, true, false, false) == 0);
    CHECK(memberOf(mgr, rid, 1002).m_mic);

    //不是发起人：716；对方不在房间里：709；不能静音发起人：请求格式错误；房间不存在：708
    CHECK(mgr.muteMember(rid, 1003, 1002, changed) == error_code_screenshare_not_host);
    CHECK(memberOf(mgr, rid, 1002).m_mic);
    CHECK(mgr.muteMember(rid, 1001, 1999, changed) == error_code_screenshare_not_member);
    CHECK(mgr.muteMember(rid, 1001, 1001, changed) == error_code_bad_request);
    CHECK(mgr.muteMember("ss-none", 1001, 1002, changed) == error_code_screenshare_not_exist);
}

TEST_CASE("SSFM1b 强制模式与禁止观看者录制：发起时随房间保存，默认为普通模式、允许录制")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string forced;
    REQUIRE(mgr.startShare(1201, conversation_group, 600201, false, false, false, kT0, forced, std::string(), true,
                           false) == 0);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(forced, room));
    CHECK(room.m_forced);
    CHECK_FALSE(room.m_recordAllowed);

    std::string normal;
    REQUIRE(mgr.startShare(1202, conversation_group, 600202, false, false, false, kT0, normal) == 0);
    REQUIRE(mgr.getRoom(normal, room));
    CHECK_FALSE(room.m_forced);
    CHECK(room.m_recordAllowed);

    //发起时禁止了录制，共享中仍可由发起人改回允许
    std::vector<int64_t> stopped;
    CHECK(mgr.setPermission(forced, 1201, -1, 1, stopped) == 0);
    REQUIRE(mgr.getRoom(forced, room));
    CHECK(room.m_recordAllowed);
}

TEST_CASE("SSFM2 强制模式下观看者不能离开，发起人离开照常结束；普通共享的观看者照常离开")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1301, conversation_group, 600301, false, false, false, kT0, rid, std::string(), true,
                           true) == 0);
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1302, kT0 + 1, seat) == 0);

    bool dissolved = false;
    ScreenShareRoom before;
    CHECK(mgr.leaveShare(rid, 1302, dissolved, before) == error_code_screenshare_forced_leave);
    CHECK(mgr.isUserInShare(1302));
    CHECK(mgr.leaveShare(rid, 1301, dissolved, before) == 0);
    CHECK(dissolved);
    CHECK_FALSE(mgr.isUserInShare(1302));

    std::string normal = makeGroupRoom(mgr, 1311, 600311, std::vector<int64_t>{1312});
    CHECK(mgr.leaveShare(normal, 1312, dissolved, before) == 0);
    CHECK_FALSE(dissolved);
    CHECK_FALSE(mgr.isUserInShare(1312));
}

TEST_CASE("SSFM3 强制模式下观看者掉线：移出房间并记下，重新加入后清除；普通共享不记；房间解散后不再回到观看")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1401, conversation_group, 600401, false, false, false, kT0, rid, std::string(), true,
                           true) == 0);
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1402, kT0 + 1, seat) == 0);

    ScreenShareRoom after;
    CHECK(mgr.onUserOffline(1402, kT0 + 10, after) == screenshare_offline_member_left);
    CHECK(after.findMember(1402) == NULL);
    CHECK(after.isForcedAway(1402));
    CHECK_FALSE(mgr.isUserInShare(1402));

    //重新登录时据此回到观看
    ScreenShareRoom back;
    REQUIRE(mgr.findForcedRejoin(1402, back));
    CHECK(back.m_roomId == rid);
    CHECK_FALSE(mgr.findForcedRejoin(1499, back));

    //重新加入后从名单里清除
    REQUIRE(mgr.joinShare(rid, 1402, kT0 + 20, seat) == 0);
    CHECK_FALSE(mgr.findForcedRejoin(1402, back));
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK_FALSE(room.isForcedAway(1402));

    //普通共享的观看者掉线不记
    std::string normal = makeGroupRoom(mgr, 1411, 600411, std::vector<int64_t>{1412});
    CHECK(mgr.onUserOffline(1412, kT0 + 10, after) == screenshare_offline_member_left);
    CHECK_FALSE(mgr.findForcedRejoin(1412, back));

    //掉线后发起人结束了共享：不再回到观看
    REQUIRE(mgr.onUserOffline(1402, kT0 + 30, after) == screenshare_offline_member_left);
    REQUIRE(mgr.stopShare(rid, 1401, room) == 0);
    CHECK_FALSE(mgr.findForcedRejoin(1402, back));
}

TEST_CASE("SSFM3c 房间记下发起人昵称，强制模式下掉线的观看者回到观看时据此显示")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1501, conversation_group, 600501, false, false, false, kT0, rid, std::string(), true,
                           true) == 0);
    CHECK(mgr.setHostName(rid, "host-1501"));
    CHECK_FALSE(mgr.setHostName("ss-none", "x"));
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1502, kT0 + 1, seat) == 0);
    ScreenShareRoom after;
    REQUIRE(mgr.onUserOffline(1502, kT0 + 10, after) == screenshare_offline_member_left);
    ScreenShareRoom back;
    REQUIRE(mgr.findForcedRejoin(1502, back));
    CHECK(back.m_hostName == "host-1501");
}

TEST_CASE("SSRM1 移出观看者：只有发起人能移出；允许再次加入时可以回来，不允许时再加入回 719，名单只属于这一场")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1601, 600601, std::vector<int64_t>{1602, 1603, 1604});
    //房间不存在、请求者不是发起人、移出发起人自己、对方不在房间里
    CHECK(mgr.removeMember("ss-none", 1601, 1602, true) == error_code_screenshare_not_exist);
    CHECK(mgr.removeMember(rid, 1602, 1603, true) == error_code_screenshare_not_host);
    CHECK(mgr.removeMember(rid, 1601, 1601, true) == error_code_bad_request);
    CHECK(mgr.removeMember(rid, 1601, 1699, true) == error_code_screenshare_not_member);

    //允许再次加入：移出后座位释放，再加入时拿回最小的空闲座位
    const int seat1602 = memberOf(mgr, rid, 1602).m_seat;
    REQUIRE(mgr.removeMember(rid, 1601, 1602, true) == 0);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.findMember(1602) == NULL);
    CHECK(room.findMember(1604) != NULL);
    CHECK_FALSE(room.isBanned(1602));
    CHECK_FALSE(mgr.isUserInShare(1602));
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1602, kT0 + 10, seat) == 0);
    CHECK(seat == seat1602);

    //不允许再次加入：记入名单，再加入回 719；已不在房间里，再移一次回 709
    REQUIRE(mgr.removeMember(rid, 1601, 1603, false) == 0);
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.findMember(1603) == NULL);
    CHECK(room.isBanned(1603));
    CHECK_FALSE(room.isBanned(1604));
    CHECK(mgr.joinShare(rid, 1603, kT0 + 20, seat) == error_code_screenshare_removed);
    CHECK_FALSE(mgr.isUserInShare(1603));
    CHECK(mgr.removeMember(rid, 1601, 1603, false) == error_code_screenshare_not_member);

    //不影响此人加入别的共享
    std::string other = makeGroupRoom(mgr, 1611, 600611, std::vector<int64_t>{1603});
    CHECK(mgr.isUserInShare(1603));
    bool dissolved = false;
    ScreenShareRoom before;
    REQUIRE(mgr.leaveShare(other, 1603, dissolved, before) == 0);

    //名单只属于这一场：结束后在同一个群重新发起，此前不允许再次加入的人可以加入
    REQUIRE(mgr.stopShare(rid, 1601, room) == 0);
    std::string again;
    REQUIRE(mgr.startShare(1601, conversation_group, 600601, false, false, false, kT0 + 30, again) == 0);
    CHECK(mgr.joinShare(again, 1603, kT0 + 31, seat) == 0);
}

TEST_CASE("SSRM1b 强制模式下也能移出观看者；掉线后待回到观看的人同样可以移出，移出后不再回到观看")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid;
    REQUIRE(mgr.startShare(1701, conversation_group, 600701, false, false, false, kT0, rid, std::string(), true,
                           true) == 0);
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 1702, kT0 + 1, seat) == 0);
    REQUIRE(mgr.joinShare(rid, 1703, kT0 + 2, seat) == 0);

    //强制模式下观看者自己不能离开（718），发起人移出不受此限制
    REQUIRE(mgr.removeMember(rid, 1701, 1702, true) == 0);
    CHECK_FALSE(mgr.isUserInShare(1702));
    ScreenShareRoom back;
    CHECK_FALSE(mgr.findForcedRejoin(1702, back));

    //1703 掉线后进入待回到观看的名单；发起人此时移出他，名单里的记录一并清除，不允许再次加入时记入名单
    ScreenShareRoom after;
    REQUIRE(mgr.onUserOffline(1703, kT0 + 10, after) == screenshare_offline_member_left);
    REQUIRE(mgr.findForcedRejoin(1703, back));
    REQUIRE(mgr.removeMember(rid, 1701, 1703, false) == 0);
    CHECK_FALSE(mgr.findForcedRejoin(1703, back));
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK_FALSE(room.isForcedAway(1703));
    CHECK(room.isBanned(1703));
    CHECK(mgr.joinShare(rid, 1703, kT0 + 20, seat) == error_code_screenshare_removed);
}

TEST_CASE("SSTI2 共享标题与描述：只有发起人能设置，随房间快照与按会话查询取出，再次设置覆盖")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1801, 600801, std::vector<int64_t>{1802});
    CHECK(mgr.setInfo("ss-none", 1801, "t", "") == error_code_screenshare_not_exist);
    CHECK(mgr.setInfo(rid, 1802, "t", "") == error_code_screenshare_not_host);
    REQUIRE(mgr.setInfo(rid, 1801, u8"周会演示", u8"第三季度汇报") == 0);

    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_title == u8"周会演示");
    CHECK(room.m_desc == u8"第三季度汇报");
    ScreenShareRoom byConv;
    REQUIRE(mgr.findRoomByConversation(conversation_group, 1802, 600801, byConv));
    CHECK(byConv.m_title == u8"周会演示");

    //再次设置覆盖，描述可以清空
    REQUIRE(mgr.setInfo(rid, 1801, "B", "") == 0);
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_title == "B");
    CHECK(room.m_desc.empty());
}

TEST_CASE("SSP2P2 记下观看者的 P2P 会话号：随房间快照取出；房间或成员不存在时返回 false（P2P 方案第 6 期）")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);
    std::string roomId;
    int seat = -1;
    REQUIRE(mgr.startShare(1001, conversation_single, 1002, false, false, false, kT0, roomId) == 0);
    REQUIRE(mgr.joinShare(roomId, 1002, kT0, seat) == 0);
    CHECK(mgr.setMemberP2PSession(roomId, 1002, "p2p-x"));
    CHECK(memberOf(mgr, roomId, 1002).m_p2pSessionId == "p2p-x");
    CHECK(memberOf(mgr, roomId, 1001).m_p2pSessionId.empty());
    CHECK_FALSE(mgr.setMemberP2PSession(roomId, 1003, "p2p-y"));
    CHECK_FALSE(mgr.setMemberP2PSession("no-such-room", 1002, "p2p-y"));
}

TEST_CASE("SSTI3c 房间记下共享卡片的消息 id，修改标题与描述时据此改写卡片")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);

    std::string rid = makeGroupRoom(mgr, 1811, 600811, std::vector<int64_t>{1812});
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_cardMsgId == 0);
    CHECK_FALSE(mgr.setCardMsgId("ss-none", 9001));
    REQUIRE(mgr.setCardMsgId(rid, 9001));
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_cardMsgId == 9001);
}

TEST_CASE("SSCT1 控制键鼠的申请与撤回：观看者向画面主人申请，转给画面主人；不能申请控制自己，画面主人须在共享")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);
    std::string rid = makeGroupRoom(mgr, 1901, 600901, std::vector<int64_t>{1902, 1903});

    //申请：转给画面主人，不改控制关系
    ScreenShareControlResult r;
    REQUIRE(mgr.applyControl(rid, 1902, screenshare_control_request, 1901, 0, r) == 0);
    CHECK(r.m_notifyUser == 1901);
    CHECK_FALSE(r.m_changed);
    CHECK(memberOf(mgr, rid, 1901).m_controlledBy == 0);
    //撤回：同样转给画面主人
    ScreenShareControlResult c;
    REQUIRE(mgr.applyControl(rid, 1902, screenshare_control_cancel, 1901, 0, c) == 0);
    CHECK(c.m_notifyUser == 1901);

    //房间不存在、申请人不在房间里、画面主人申请控制自己、动作取值非法
    ScreenShareControlResult e;
    CHECK(mgr.applyControl("ss-none", 1902, screenshare_control_request, 1901, 0, e) == error_code_screenshare_not_exist);
    CHECK(mgr.applyControl(rid, 1999, screenshare_control_request, 1901, 0, e) == error_code_screenshare_not_member);
    CHECK(mgr.applyControl(rid, 1901, screenshare_control_request, 1901, 0, e) == error_code_bad_request);
    CHECK(mgr.applyControl(rid, 1902, 0, 1901, 0, e) == error_code_bad_request);
    CHECK(mgr.applyControl(rid, 1902, 6, 1901, 0, e) == error_code_bad_request);
    //申请控制一个没在共享屏幕的人（观看者 1903）：729
    CHECK(mgr.applyControl(rid, 1902, screenshare_control_request, 1903, 0, e) == error_code_screenshare_not_sharing);

    //已在控制这块画面时再申请：成功但不再通知画面主人
    ScreenShareControlResult g;
    REQUIRE(mgr.applyControl(rid, 1901, screenshare_control_grant, 1901, 1902, g) == 0);
    ScreenShareControlResult again;
    REQUIRE(mgr.applyControl(rid, 1902, screenshare_control_request, 1901, 0, again) == 0);
    CHECK(again.m_notifyUser == 0);

    //画面主人掉线（宽限期内）：画面暂时不存在，申请返回 729
    ScreenShareRoom snap;
    REQUIRE(mgr.onUserOffline(1901, kT0 + 5, snap) == screenshare_offline_host_away);
    CHECK(mgr.applyControl(rid, 1903, screenshare_control_request, 1901, 0, e) == error_code_screenshare_not_sharing);
}

TEST_CASE("SSCT2 授予、拒绝与收回：只有画面主人能授予与拒绝，画面主人或控制者能收回；每块画面只有一人控制")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);
    std::string rid = makeGroupRoom(mgr, 2001, 601001, std::vector<int64_t>{2002, 2003});
    ScreenShareControlResult e;

    //无权：观看者授予、拒绝、收回别人的画面
    CHECK(mgr.applyControl(rid, 2002, screenshare_control_grant, 2001, 2003, e) == error_code_screenshare_not_owner);
    CHECK(mgr.applyControl(rid, 2002, screenshare_control_deny, 2001, 2003, e) == error_code_screenshare_not_owner);
    CHECK(mgr.applyControl(rid, 2002, screenshare_control_revoke, 2001, 0, e) == error_code_screenshare_not_owner);
    //对象不在房间里、对象是画面主人自己
    CHECK(mgr.applyControl(rid, 2001, screenshare_control_grant, 2001, 2999, e) == error_code_screenshare_not_member);
    CHECK(mgr.applyControl(rid, 2001, screenshare_control_grant, 2001, 2001, e) == error_code_bad_request);
    CHECK(mgr.applyControl(rid, 2001, screenshare_control_deny, 2001, 2999, e) == error_code_screenshare_not_member);

    //拒绝：转给申请人，不改控制关系
    ScreenShareControlResult d;
    REQUIRE(mgr.applyControl(rid, 2001, screenshare_control_deny, 2001, 2002, d) == 0);
    CHECK(d.m_notifyUser == 2002);
    CHECK_FALSE(d.m_changed);

    //授予：记在画面主人身上，快照里能看到
    ScreenShareControlResult g;
    REQUIRE(mgr.applyControl(rid, 2001, screenshare_control_grant, 2001, 2002, g) == 0);
    CHECK(g.m_changed);
    CHECK(g.m_prevController == 0);
    CHECK(g.m_notifyUser == 0);
    const ScreenShareMember* host = g.m_room.findMember(2001);
    REQUIRE(host != NULL);
    CHECK(host->m_controlledBy == 2002);
    CHECK(memberOf(mgr, rid, 2002).m_controlledBy == 0);
    //重复授予同一人：不算改动
    ScreenShareControlResult same;
    REQUIRE(mgr.applyControl(rid, 2001, screenshare_control_grant, 2001, 2002, same) == 0);
    CHECK_FALSE(same.m_changed);
    //授予另一人：替换原控制者
    ScreenShareControlResult swap;
    REQUIRE(mgr.applyControl(rid, 2001, screenshare_control_grant, 2001, 2003, swap) == 0);
    CHECK(swap.m_changed);
    CHECK(swap.m_prevController == 2002);
    CHECK(memberOf(mgr, rid, 2001).m_controlledBy == 2003);

    //已不在控制的人不能收回；控制者自己结束控制
    CHECK(mgr.applyControl(rid, 2002, screenshare_control_revoke, 2001, 0, e) == error_code_screenshare_not_owner);
    ScreenShareControlResult rel;
    REQUIRE(mgr.applyControl(rid, 2003, screenshare_control_revoke, 2001, 0, rel) == 0);
    CHECK(rel.m_changed);
    CHECK(rel.m_prevController == 2003);
    CHECK(memberOf(mgr, rid, 2001).m_controlledBy == 0);
    //没人控制时收回：成功但不算改动
    ScreenShareControlResult idle;
    REQUIRE(mgr.applyControl(rid, 2001, screenshare_control_revoke, 2001, 0, idle) == 0);
    CHECK_FALSE(idle.m_changed);
    CHECK(idle.m_prevController == 0);
    //画面主人收回
    REQUIRE(mgr.applyControl(rid, 2001, screenshare_control_grant, 2001, 2002, g) == 0);
    ScreenShareControlResult byOwner;
    REQUIRE(mgr.applyControl(rid, 2001, screenshare_control_revoke, 2001, 0, byOwner) == 0);
    CHECK(byOwner.m_changed);
    CHECK(byOwner.m_prevController == 2002);
    CHECK(memberOf(mgr, rid, 2001).m_controlledBy == 0);
}

TEST_CASE("SSCT3 控制者离开、被移出、掉线，以及画面主人掉线时，控制关系随之清除")
{
    ScreenShareManager mgr;
    configureManager(mgr, 50, 9);
    std::string rid = makeGroupRoom(mgr, 2101, 601101, std::vector<int64_t>{2102, 2103, 2104});
    ScreenShareControlResult g;
    bool dissolved = false;
    ScreenShareRoom snap;

    //控制者离开
    REQUIRE(mgr.applyControl(rid, 2101, screenshare_control_grant, 2101, 2102, g) == 0);
    REQUIRE(mgr.leaveShare(rid, 2102, dissolved, snap) == 0);
    CHECK(memberOf(mgr, rid, 2101).m_controlledBy == 0);

    //控制者被移出
    REQUIRE(mgr.applyControl(rid, 2101, screenshare_control_grant, 2101, 2103, g) == 0);
    REQUIRE(mgr.removeMember(rid, 2101, 2103, true) == 0);
    CHECK(memberOf(mgr, rid, 2101).m_controlledBy == 0);

    //控制者掉线
    REQUIRE(mgr.applyControl(rid, 2101, screenshare_control_grant, 2101, 2104, g) == 0);
    REQUIRE(mgr.onUserOffline(2104, kT0 + 10, snap) == screenshare_offline_member_left);
    CHECK(memberOf(mgr, rid, 2101).m_controlledBy == 0);

    //别的观看者离开不影响正在进行的控制
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, 2102, kT0 + 20, seat) == 0);
    REQUIRE(mgr.joinShare(rid, 2103, kT0 + 21, seat) == 0);
    REQUIRE(mgr.applyControl(rid, 2101, screenshare_control_grant, 2101, 2102, g) == 0);
    REQUIRE(mgr.leaveShare(rid, 2103, dissolved, snap) == 0);
    CHECK(memberOf(mgr, rid, 2101).m_controlledBy == 2102);

    //画面主人掉线：控制关系清除，回来后不自动恢复
    REQUIRE(mgr.onUserOffline(2101, kT0 + 30, snap) == screenshare_offline_host_away);
    const ScreenShareMember* host = snap.findMember(2101);
    REQUIRE(host != NULL);
    CHECK(host->m_controlledBy == 0);
    REQUIRE(mgr.onUserOnline(2101, snap));
    CHECK(memberOf(mgr, rid, 2101).m_controlledBy == 0);
}
