/**
 *  群通话信令的服务端处理流程实现，GroupCallService.cpp
 *  职责划分与调用约束见 GroupCallService.h；各请求的字段与推送时机见 docs/协议设计-v2.md 第 21 节。
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupCallService.h"

#include <algorithm>
#include <set>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "GroupCallProtocol.h"

namespace {

//秒到毫秒的换算系数
const int64_t kMsPerSecond = 1000;

//通话卡片的 clientmsgid 前缀，后接房间号。以 "sys-" 开头，避免与真实客户端消息的去重键相撞
const char* kCardClientMsgIdPrefix = "sys-groupcall-card-";

//错误码对应的应答 msg 文本（给排查者看，客户端只认 code）
const char* errorText(int code)
{
    switch (code)
    {
    //成功
    case error_code_ok:
        return "ok";
    //请求包体缺字段或取值非法
    case error_code_bad_request:
        return "bad request";
    //房间已结束或房间号无效
    case error_code_groupcall_not_exist:
        return "group call not exist";
    //该群已有进行中的群通话
    case error_code_groupcall_exists:
        return "group already in call";
    //本人正在别的通话、屏幕共享或群通话里
    case error_code_groupcall_busy:
        return "busy";
    //人数已满
    case error_code_groupcall_room_full:
        return "room full";
    //不是群成员，或不在这场群通话里
    case error_code_groupcall_not_member:
        return "not a member";
    //摄像头人数已满
    case error_code_groupcall_camera_limit:
        return "camera limit reached";
    //本企业关闭了群通话
    case error_code_groupcall_disabled:
        return "group call disabled";
    //没有管理权限
    case error_code_groupcall_no_permission:
        return "no permission";
    //其余错误码：本模块不会产生，兜底
    default:
        return "failed";
    }
}

//构造一条通用应答帧
GroupCallReply resultReply(uint16_t cmd, int code, const std::string& roomId)
{
    return GroupCallReply(cmd, GroupCallProtocol::buildResult(code, errorText(code), roomId));
}

//在列表末尾追加一个 userid（不去重；推送时由接收端按连接逐个发，重复的人由 unionIds 去掉）
std::vector<int64_t> withOne(const std::vector<int64_t>& ids, int64_t extra)
{
    std::vector<int64_t> out = ids;
    if (extra > 0)
    {
        out.push_back(extra);
    }
    return out;
}

//两个列表的并集，保持出现的先后顺序
std::vector<int64_t> unionIds(const std::vector<int64_t>& a, const std::vector<int64_t>& b)
{
    std::vector<int64_t> out;
    std::set<int64_t> seen;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (seen.insert(a[i]).second)
        {
            out.push_back(a[i]);
        }
    }
    for (size_t i = 0; i < b.size(); ++i)
    {
        if (seen.insert(b[i]).second)
        {
            out.push_back(b[i]);
        }
    }
    return out;
}

//通话时长（秒）；时钟回拨时按 0
int64_t durationSecondsOf(const GroupCallRoom& room, int64_t now)
{
    const int64_t seconds = (now - room.m_startTime) / kMsPerSecond;
    return seconds > 0 ? seconds : 0;
}

} // namespace

GroupCallService::GroupCallService(GroupCallManager& mgr, IGroupCallEnv& env)
    : m_mgr(mgr)
    , m_env(env)
{
}

void GroupCallService::handleRequest(uint16_t cmd, int64_t userId, const std::string& nickname,
                                     const std::string& body, int64_t now, std::vector<GroupCallReply>& outReplies)
{
    switch (cmd)
    {
    //发起群通话：建房间、回中转参数、给被邀请人振铃、写通话卡片
    case cmd_groupcall_start:
        handleStart(userId, nickname, body, now, outReplies);
        break;

    //接听或从横幅加入：分配座位、回中转参数、广播加入
    case cmd_groupcall_join:
        handleJoin(userId, body, now, outReplies);
        break;

    //被邀请人拒绝
    case cmd_groupcall_decline:
        handleDecline(userId, body, outReplies);
        break;

    //离开；最后一个已加入的人离开时结束
    case cmd_groupcall_leave:
        handleLeave(userId, body, now, outReplies);
        break;

    //通话中追加邀请
    case cmd_groupcall_invite_more:
        handleInviteMore(userId, nickname, body, now, outReplies);
        break;

    //成员上报麦克风、摄像头开关
    case cmd_groupcall_member_media:
        handleMemberMedia(userId, body, outReplies);
        break;

    //查询某群有无进行中的群通话
    case cmd_groupcall_query:
        handleQuery(userId, body, outReplies);
        break;

    //发起人、群主、管理员静音或移出别人（第 6 期）
    case cmd_groupcall_manage:
        handleManage(userId, body, now, outReplies);
        break;

    //不是群通话的请求命令（调用方分发有误）：回一条参数错误
    default:
        outReplies.push_back(resultReply(cmd, error_code_bad_request, ""));
        LOGE("groupcall: unexpected cmd %u", (unsigned)cmd);
        break;
    }
}

void GroupCallService::handleStart(int64_t userId, const std::string& nickname, const std::string& body, int64_t now,
                                   std::vector<GroupCallReply>& outReplies)
{
    GroupCallStartRequest req;
    if (!GroupCallProtocol::parseStartRequest(body, req))
    {
        outReplies.push_back(resultReply(cmd_groupcall_start, error_code_bad_request, ""));
        return;
    }

    //资格：企业开关、群成员、本人不在别的实时会话里（群通话房间的占用由 startCall 判断）
    int code = error_code_ok;
    if (!m_env.isFeatureEnabled(userId))
        code = error_code_groupcall_disabled;
    else if (!m_env.isGroupMember(req.m_groupId, userId))
        code = error_code_groupcall_not_member;
    else if (m_env.isBusyOutsideGroupCall(userId))
        code = error_code_groupcall_busy;
    if (code != error_code_ok)
    {
        outReplies.push_back(resultReply(cmd_groupcall_start, code, ""));
        LOGI("groupcall start rejected, user: %lld, group: %lld, code: %d", (long long)userId,
             (long long)req.m_groupId, code);
        return;
    }

    const std::vector<int64_t> groupMembers = m_env.groupMemberIds(req.m_groupId);
    std::vector<int64_t> ring;
    std::vector<int64_t> busy;
    std::vector<int64_t> offline;
    classifyInvitees(userId, req.m_invitees, groupMembers, ring, busy, offline);

    std::string roomId;
    std::string existingId;
    std::vector<int64_t> busyInCall;
    code = m_mgr.startCall(userId, req.m_groupId, req.m_media, ring, req.m_mic, req.m_camera, now, roomId,
                           existingId, busyInCall);
    if (code == error_code_groupcall_exists)
    {
        //该群已有群通话：带上那一场的房间号，客户端改为加入
        outReplies.push_back(resultReply(cmd_groupcall_start, code, existingId));
        LOGI("groupcall start: group already in call, user: %lld, group: %lld, room: %s", (long long)userId,
             (long long)req.m_groupId, existingId.c_str());
        return;
    }
    GroupCallRoom room;
    if (code == error_code_ok && !m_mgr.getRoom(roomId, room))
        code = error_code_groupcall_not_exist;
    if (code != error_code_ok)
    {
        outReplies.push_back(resultReply(cmd_groupcall_start, code, ""));
        LOGI("groupcall start rejected, user: %lld, group: %lld, code: %d", (long long)userId,
             (long long)req.m_groupId, code);
        return;
    }
    busy.insert(busy.end(), busyInCall.begin(), busyInCall.end());
    m_mgr.setInitiatorName(roomId, nickname);

    outReplies.push_back(GroupCallReply(cmd_groupcall_start,
                                        GroupCallProtocol::buildInviteResult(error_code_ok, "ok", roomId, busy,
                                                                             offline)));
    appendRelayInfo(roomId, userId, now, outReplies);

    //振铃：startCall 已把真正振铃的人放进房间，逐个推邀请
    pushInvites(room, ring, userId, nickname, now);
    //房间里的人（发起人与振铃中的人）收到事件 1
    pushState(groupcall_event_member_joined, userId, room, std::vector<int64_t>());

    //通话卡片记在发起人名下，推给全体群成员（含发起人）
    const std::string card = GroupCallProtocol::buildCardContent(roomId, req.m_media, userId, nickname,
                                                                 room.m_startTime);
    const int64_t cardMsgId = m_env.saveCard(req.m_groupId, userId, card, std::string(kCardClientMsgIdPrefix) + roomId,
                                             now, withOne(groupMembers, userId));
    if (cardMsgId > 0)
        m_mgr.setCardMsgId(roomId, cardMsgId);
    else
        LOGE("groupcall: save card failed, room: %s", roomId.c_str());

    //群聊窗横幅：全体群成员与房间里的人
    pushActiveStatus(room);

    LOGI("groupcall started, room: %s, group: %lld, initiator: %lld, media: %d, ringing: %d, busy: %d, offline: %d, "
         "card: %lld", roomId.c_str(), (long long)req.m_groupId, (long long)userId, req.m_media, (int)ring.size(),
         (int)busy.size(), (int)offline.size(), (long long)cardMsgId);
}

void GroupCallService::handleJoin(int64_t userId, const std::string& body, int64_t now,
                                  std::vector<GroupCallReply>& outReplies)
{
    std::string roomId;
    bool mic = true;
    bool camera = false;
    if (!GroupCallProtocol::parseJoinRequest(body, roomId, mic, camera))
    {
        outReplies.push_back(resultReply(cmd_groupcall_join, error_code_bad_request, ""));
        return;
    }

    GroupCallRoom room;
    int code = error_code_ok;
    if (!m_mgr.getRoom(roomId, room))
    {
        code = error_code_groupcall_not_exist;
    }
    else
    {
        const GroupCallMember* self = room.findMember(userId);
        const bool alreadyJoined = (self != NULL && self->m_state == groupcall_member_joined);
        //已经在房间里的人（振铃中或已加入）不再查群成员资格：中途退群的人仍可接听或换一份新令牌
        if (!alreadyJoined)
        {
            if (!m_env.isFeatureEnabled(userId))
                code = error_code_groupcall_disabled;
            else if (self == NULL && !m_env.isGroupMember(room.m_groupId, userId))
                code = error_code_groupcall_not_member;
            else if (m_env.isBusyOutsideGroupCall(userId))
                code = error_code_groupcall_busy;
        }
    }
    int seat = 0;
    bool already = false;
    //全员静音期间没有管理权的人按关麦克风加入（只在房间处于全员静音时才查群角色）
    if (code == error_code_ok)
    {
        const bool canSpeak = !room.m_allMuted || hasManageRight(room, userId);
        code = m_mgr.joinCall(roomId, userId, mic, camera, now, seat, already, canSpeak);
    }
    if (code != error_code_ok)
    {
        outReplies.push_back(resultReply(cmd_groupcall_join, code, roomId));
        LOGI("groupcall join rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId, code);
        return;
    }

    outReplies.push_back(resultReply(cmd_groupcall_join, error_code_ok, roomId));
    appendRelayInfo(roomId, userId, now, outReplies);
    if (!already && m_mgr.getRoom(roomId, room))
    {
        pushState(groupcall_event_member_joined, userId, room, std::vector<int64_t>());
        pushActiveStatus(room);
    }
    LOGI("groupcall joined, room: %s, user: %lld, seat: %d, again: %d", roomId.c_str(), (long long)userId, seat,
         already ? 1 : 0);
}

void GroupCallService::handleDecline(int64_t userId, const std::string& body, std::vector<GroupCallReply>& outReplies)
{
    std::string roomId;
    if (!GroupCallProtocol::parseRoomRequest(body, roomId))
    {
        outReplies.push_back(resultReply(cmd_groupcall_decline, error_code_bad_request, ""));
        return;
    }
    GroupCallRoom room;
    const int code = m_mgr.declineCall(roomId, userId, room);
    outReplies.push_back(resultReply(cmd_groupcall_decline, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("groupcall decline rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId, code);
        return;
    }
    //本人也收一份：他的其它设备据此收起来电
    pushState(groupcall_event_declined, userId, room, std::vector<int64_t>(1, userId));
    LOGI("groupcall declined, room: %s, user: %lld", roomId.c_str(), (long long)userId);
}

void GroupCallService::handleLeave(int64_t userId, const std::string& body, int64_t now,
                                   std::vector<GroupCallReply>& outReplies)
{
    std::string roomId;
    if (!GroupCallProtocol::parseRoomRequest(body, roomId))
    {
        outReplies.push_back(resultReply(cmd_groupcall_leave, error_code_bad_request, ""));
        return;
    }
    bool wasRinging = false;
    bool ended = false;
    GroupCallRoom room;
    const int code = m_mgr.leaveCall(roomId, userId, wasRinging, ended, room);
    outReplies.push_back(resultReply(cmd_groupcall_leave, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("groupcall leave rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId, code);
        return;
    }

    const std::vector<int64_t> self(1, userId);
    pushState(wasRinging ? groupcall_event_declined : groupcall_event_member_left, userId, room, self);
    if (ended)
        finishRoom(room, now, self);
    else if (!wasRinging)
        pushActiveStatus(room);
    LOGI("groupcall left, room: %s, user: %lld, was ringing: %d, ended: %d", roomId.c_str(), (long long)userId,
         wasRinging ? 1 : 0, ended ? 1 : 0);
}

void GroupCallService::handleInviteMore(int64_t userId, const std::string& nickname, const std::string& body,
                                        int64_t now, std::vector<GroupCallReply>& outReplies)
{
    std::string roomId;
    std::vector<int64_t> invitees;
    if (!GroupCallProtocol::parseInviteMoreRequest(body, roomId, invitees))
    {
        outReplies.push_back(resultReply(cmd_groupcall_invite_more, error_code_bad_request, ""));
        return;
    }
    GroupCallRoom room;
    if (!m_mgr.getRoom(roomId, room))
    {
        outReplies.push_back(resultReply(cmd_groupcall_invite_more, error_code_groupcall_not_exist, roomId));
        return;
    }
    if (!m_env.isFeatureEnabled(userId))
    {
        outReplies.push_back(resultReply(cmd_groupcall_invite_more, error_code_groupcall_disabled, roomId));
        return;
    }

    std::vector<int64_t> ring;
    std::vector<int64_t> busy;
    std::vector<int64_t> offline;
    classifyInvitees(userId, invitees, m_env.groupMemberIds(room.m_groupId), ring, busy, offline);
    std::vector<int64_t> added;
    std::vector<int64_t> busyInCall;
    const int code = m_mgr.inviteMore(roomId, userId, ring, now, added, busyInCall);
    if (code != error_code_ok)
    {
        outReplies.push_back(resultReply(cmd_groupcall_invite_more, code, roomId));
        LOGI("groupcall invite more rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId,
             code);
        return;
    }
    busy.insert(busy.end(), busyInCall.begin(), busyInCall.end());
    outReplies.push_back(GroupCallReply(cmd_groupcall_invite_more,
                                        GroupCallProtocol::buildInviteResult(error_code_ok, "ok", roomId, busy,
                                                                             offline)));
    if (!added.empty() && m_mgr.getRoom(roomId, room))
    {
        pushInvites(room, added, userId, nickname, now);
        pushState(groupcall_event_invited, userId, room, std::vector<int64_t>());
    }
    LOGI("groupcall invite more, room: %s, user: %lld, ringing: %d, busy: %d, offline: %d", roomId.c_str(),
         (long long)userId, (int)added.size(), (int)busy.size(), (int)offline.size());
}

void GroupCallService::handleMemberMedia(int64_t userId, const std::string& body,
                                         std::vector<GroupCallReply>& outReplies)
{
    std::string roomId;
    bool mic = false;
    bool camera = false;
    if (!GroupCallProtocol::parseMemberMediaRequest(body, roomId, mic, camera))
    {
        outReplies.push_back(resultReply(cmd_groupcall_member_media, error_code_bad_request, ""));
        return;
    }
    //全员静音期间要打开麦克风时才查群角色：没有管理权的人会被拒绝（728）
    GroupCallRoom room;
    bool canSpeak = true;
    if (mic && m_mgr.getRoom(roomId, room) && room.m_allMuted)
        canSpeak = hasManageRight(room, userId);
    const int code = m_mgr.setMemberMedia(roomId, userId, mic, camera, canSpeak);
    outReplies.push_back(resultReply(cmd_groupcall_member_media, code, roomId));
    if (code == error_code_ok && m_mgr.getRoom(roomId, room))
        pushState(groupcall_event_media_changed, userId, room, std::vector<int64_t>());
    LOGI("groupcall member media, room: %s, user: %lld, mic: %d, camera: %d, code: %d", roomId.c_str(),
         (long long)userId, mic ? 1 : 0, camera ? 1 : 0, code);
}

void GroupCallService::handleQuery(int64_t userId, const std::string& body, std::vector<GroupCallReply>& outReplies)
{
    int64_t groupId = 0;
    if (!GroupCallProtocol::parseQueryRequest(body, groupId))
    {
        outReplies.push_back(GroupCallReply(cmd_groupcall_query,
                                            GroupCallProtocol::buildQueryResponse(error_code_bad_request,
                                                                                  errorText(error_code_bad_request),
                                                                                  NULL)));
        return;
    }
    //群里有没有群通话只告诉群成员
    if (!m_env.isGroupMember(groupId, userId))
    {
        outReplies.push_back(GroupCallReply(cmd_groupcall_query,
                                            GroupCallProtocol::buildQueryResponse(
                                                error_code_groupcall_not_member,
                                                errorText(error_code_groupcall_not_member), NULL)));
        return;
    }
    GroupCallRoom room;
    const bool found = m_mgr.findRoomByGroup(groupId, room);
    outReplies.push_back(GroupCallReply(cmd_groupcall_query,
                                        GroupCallProtocol::buildQueryResponse(error_code_ok, "ok",
                                                                              found ? &room : NULL)));
}

void GroupCallService::handleManage(int64_t userId, const std::string& body, int64_t now,
                                    std::vector<GroupCallReply>& outReplies)
{
    std::string roomId;
    int action = 0;
    int64_t target = 0;
    if (!GroupCallProtocol::parseManageRequest(body, roomId, action, target))
    {
        outReplies.push_back(resultReply(cmd_groupcall_manage, error_code_bad_request, ""));
        return;
    }
    GroupCallRoom room;
    if (!m_mgr.getRoom(roomId, room))
    {
        outReplies.push_back(resultReply(cmd_groupcall_manage, error_code_groupcall_not_exist, roomId));
        return;
    }
    //操作者须已加入；发起人、群主、管理员才有权限
    const GroupCallMember* op = room.findMember(userId);
    if (op == NULL || op->m_state != groupcall_member_joined)
    {
        outReplies.push_back(resultReply(cmd_groupcall_manage, error_code_groupcall_not_member, roomId));
        return;
    }
    if (!hasManageRight(room, userId))
    {
        outReplies.push_back(resultReply(cmd_groupcall_manage, error_code_groupcall_no_permission, roomId));
        LOGI("groupcall manage denied, room: %s, user: %lld", roomId.c_str(), (long long)userId);
        return;
    }
    //不能对自己做静音某人、移出某人（自己用麦克风按钮或离开即可）
    if (target == userId)
    {
        outReplies.push_back(resultReply(cmd_groupcall_manage, error_code_bad_request, roomId));
        return;
    }

    switch (action)
    {
    //静音某人：对方原本开着麦克风才广播
    case groupcall_manage_mute:
    {
        bool changed = false;
        const int code = m_mgr.muteMember(roomId, target, changed);
        outReplies.push_back(resultReply(cmd_groupcall_manage, code, roomId));
        if (code == error_code_ok && changed && m_mgr.getRoom(roomId, room))
            pushState(groupcall_event_member_muted, userId, room, std::vector<int64_t>(), target);
        LOGI("groupcall mute member, room: %s, operator: %lld, target: %lld, changed: %d, code: %d", roomId.c_str(),
             (long long)userId, (long long)target, changed ? 1 : 0, code);
        break;
    }

    //开启全员静音：房间进入全员静音状态，发起人、群主、管理员之外的已加入的人全部静音
    case groupcall_manage_mute_all:
    {
        std::vector<int64_t> exempt;
        for (size_t i = 0; i < room.m_members.size(); ++i)
        {
            const GroupCallMember& m = room.m_members[i];
            if (m.m_state == groupcall_member_joined && hasManageRight(room, m.m_userId))
                exempt.push_back(m.m_userId);
        }
        std::vector<int64_t> muted;
        const int code = m_mgr.muteAll(roomId, exempt, muted);
        outReplies.push_back(resultReply(cmd_groupcall_manage, code, roomId));
        if (code == error_code_ok && m_mgr.getRoom(roomId, room))
            pushState(groupcall_event_muted_all, userId, room, std::vector<int64_t>());
        LOGI("groupcall mute all, room: %s, operator: %lld, exempt: %d, muted: %d", roomId.c_str(), (long long)userId,
             (int)exempt.size(), (int)muted.size());
        break;
    }

    //解除全员静音：只解除开麦限制，不替任何人打开麦克风；原本就没开启时不推送
    case groupcall_manage_unmute_all:
    {
        bool changed = false;
        const int code = m_mgr.unmuteAll(roomId, changed);
        outReplies.push_back(resultReply(cmd_groupcall_manage, code, roomId));
        if (code == error_code_ok && changed && m_mgr.getRoom(roomId, room))
            pushState(groupcall_event_unmuted_all, userId, room, std::vector<int64_t>());
        LOGI("groupcall unmute all, room: %s, operator: %lld, changed: %d", roomId.c_str(), (long long)userId,
             changed ? 1 : 0);
        break;
    }

    //移出某人：被移出的人也收到这一帧（target 是他自己），据此退出通话
    case groupcall_manage_remove:
    {
        const GroupCallMember* t = room.findMember(target);
        const bool wasJoined = (t != NULL && t->m_state == groupcall_member_joined);
        bool ended = false;
        GroupCallRoom after;
        const int code = m_mgr.removeMember(roomId, target, ended, after);
        outReplies.push_back(resultReply(cmd_groupcall_manage, code, roomId));
        if (code == error_code_ok)
        {
            const std::vector<int64_t> removed(1, target);
            pushState(groupcall_event_member_removed, userId, after, removed, target);
            if (ended)
                finishRoom(after, now, removed);
            else if (wasJoined)
                pushActiveStatus(after);
        }
        LOGI("groupcall remove member, room: %s, operator: %lld, target: %lld, code: %d", roomId.c_str(),
             (long long)userId, (long long)target, code);
        break;
    }

    //parseManageRequest 已挡住其它取值，这里只是兜底
    default:
        outReplies.push_back(resultReply(cmd_groupcall_manage, error_code_bad_request, roomId));
        break;
    }
}

void GroupCallService::onUserOffline(int64_t userId, int64_t now)
{
    GroupCallRoom room;
    const int result = m_mgr.onUserOffline(userId, now, room);
    switch (result)
    {
    //振铃中的人掉线：按振铃未接处理，通知房间里的人
    case groupcall_offline_ringing_removed:
        pushState(groupcall_event_ring_timeout, userId, room, std::vector<int64_t>());
        LOGI("groupcall ringing member offline, room: %s, user: %lld", room.m_roomId.c_str(), (long long)userId);
        break;

    //已加入的人掉线：座位保留，等宽限期
    case groupcall_offline_joined_away:
        pushState(groupcall_event_member_offline, userId, room, std::vector<int64_t>());
        LOGI("groupcall member offline, room: %s, user: %lld, grace: %lld ms", room.m_roomId.c_str(),
             (long long)userId, (long long)m_mgr.getConfig().m_offlineGraceMs);
        break;

    //此人不在任何群通话里
    default:
        break;
    }
}

void GroupCallService::onUserLogin(int64_t userId, int64_t now, std::vector<GroupCallReply>& outReplies)
{
    GroupCallRoom room;
    if (!m_mgr.onUserOnline(userId, room))
        return;
    pushState(groupcall_event_member_back, userId, room, std::vector<int64_t>());
    appendRelayInfo(room.m_roomId, userId, now, outReplies);
    LOGI("groupcall member back, room: %s, user: %lld", room.m_roomId.c_str(), (long long)userId);
}

void GroupCallService::reapTick(int64_t now)
{
    std::vector<GroupCallReaped> rings;
    m_mgr.reapRingTimeouts(now, rings);
    for (size_t i = 0; i < rings.size(); ++i)
    {
        //超时的人自己也收一份，他的来电窗据此关闭
        pushState(groupcall_event_ring_timeout, rings[i].m_userId, rings[i].m_room,
                  std::vector<int64_t>(1, rings[i].m_userId));
        LOGI("groupcall ring timeout, room: %s, user: %lld", rings[i].m_room.m_roomId.c_str(),
             (long long)rings[i].m_userId);
    }

    std::vector<GroupCallReaped> offline;
    m_mgr.reapOfflineMembers(now, offline);
    for (size_t i = 0; i < offline.size(); ++i)
    {
        const std::vector<int64_t> self(1, offline[i].m_userId);
        pushState(groupcall_event_member_left, offline[i].m_userId, offline[i].m_room, self);
        if (offline[i].m_ended)
            finishRoom(offline[i].m_room, now, self);
        else
            pushActiveStatus(offline[i].m_room);
        LOGI("groupcall member stayed offline, removed, room: %s, user: %lld, ended: %d",
             offline[i].m_room.m_roomId.c_str(), (long long)offline[i].m_userId, offline[i].m_ended ? 1 : 0);
    }
}

bool GroupCallService::isUserInGroupCall(int64_t userId)
{
    return Singleton<GroupCallManager>::Instance().isUserInCall(userId);
}

void GroupCallService::classifyInvitees(int64_t selfId, const std::vector<int64_t>& raw,
                                        const std::vector<int64_t>& groupMembers, std::vector<int64_t>& outRing,
                                        std::vector<int64_t>& outBusy, std::vector<int64_t>& outOffline)
{
    outRing.clear();
    outBusy.clear();
    outOffline.clear();
    const std::set<int64_t> members(groupMembers.begin(), groupMembers.end());
    std::set<int64_t> seen;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const int64_t uid = raw[i];
        //去掉自己、重复的人与非群成员（非群成员直接忽略，不告诉请求者）
        if (uid <= 0 || uid == selfId || !seen.insert(uid).second || members.find(uid) == members.end())
            continue;
        if (!m_env.isOnline(uid))
            outOffline.push_back(uid);
        else if (m_env.isBusyOutsideGroupCall(uid))
            outBusy.push_back(uid);
        else
            outRing.push_back(uid);
    }
}

bool GroupCallService::hasManageRight(const GroupCallRoom& room, int64_t userId)
{
    if (userId == room.m_initiatorId)
        return true;
    const int role = m_env.groupRole(room.m_groupId, userId);
    return role == group_role_admin || role == group_role_owner;
}

bool GroupCallService::appendRelayInfo(const std::string& roomId, int64_t userId, int64_t now,
                                       std::vector<GroupCallReply>& outReplies)
{
    std::string token;
    int seat = 0;
    if (!m_mgr.signTokenFor(roomId, userId, now, token, seat))
        return false;
    const GroupCallConfig config = m_mgr.getConfig();
    GroupCallRoom room;
    const int64_t elapsed = m_mgr.getRoom(roomId, room) ? now - room.m_startTime : 0;
    outReplies.push_back(GroupCallReply(cmd_groupcall_relay_info,
                                        GroupCallProtocol::buildRelayInfo(roomId, config.m_relayHost,
                                                                          config.m_relayPort, token, seat,
                                                                          config.m_maxCameras, elapsed)));
    return true;
}

void GroupCallService::pushState(int event, int64_t actor, const GroupCallRoom& room,
                                 const std::vector<int64_t>& extra, int64_t target)
{
    m_env.push(unionIds(room.participantIds(), extra), cmd_groupcall_state_notify,
               GroupCallProtocol::buildStateNotify(event, actor, room, target));
}

void GroupCallService::pushActiveStatus(const GroupCallRoom& room)
{
    m_env.push(unionIds(m_env.groupMemberIds(room.m_groupId), room.participantIds()), cmd_groupcall_status_notify,
               GroupCallProtocol::buildStatusNotify(room, true, 0, 0, std::string()));
}

void GroupCallService::pushInvites(const GroupCallRoom& room, const std::vector<int64_t>& newlyInvited,
                                   int64_t inviterId, const std::string& inviterName, int64_t now)
{
    if (newlyInvited.empty())
        return;
    const std::string groupName = m_env.groupName(room.m_groupId);
    for (size_t i = 0; i < newlyInvited.size(); ++i)
    {
        const GroupCallMember* m = room.findMember(newlyInvited[i]);
        if (m == NULL || m->m_state != groupcall_member_ringing)
            continue;
        const int64_t ringMs = m_mgr.ringRemainingMs(room.m_roomId, newlyInvited[i], now);
        m_env.push(std::vector<int64_t>(1, newlyInvited[i]), cmd_groupcall_invite_notify,
                   GroupCallProtocol::buildInviteNotify(room, inviterId, inviterName, groupName, ringMs));
    }
}

void GroupCallService::finishRoom(const GroupCallRoom& room, int64_t now, const std::vector<int64_t>& extra)
{
    const int64_t duration = durationSecondsOf(room, now);
    const std::string base = GroupCallProtocol::buildCardContent(room.m_roomId, room.m_media, room.m_initiatorId,
                                                                 room.m_initiatorName, room.m_startTime);
    const std::string ended = GroupCallProtocol::buildEndedCardContent(base, room, room.m_initiatorName, now,
                                                                       duration);
    int64_t cardMsgId = room.m_cardMsgId;
    if (cardMsgId > 0 && !m_env.updateCard(cardMsgId, ended))
    {
        //库里没改成：推送里不带卡片，客户端只刷新横幅，卡片状态以查询为准
        LOGE("groupcall: update card failed, room: %s, msgid: %lld", room.m_roomId.c_str(), (long long)cardMsgId);
        cardMsgId = 0;
    }
    const std::vector<int64_t> users = unionIds(unionIds(m_env.groupMemberIds(room.m_groupId), room.participantIds()),
                                                extra);
    m_env.push(users, cmd_groupcall_status_notify,
               GroupCallProtocol::buildStatusNotify(room, false, duration, cardMsgId, ended));
    LOGI("groupcall ended, room: %s, group: %lld, initiator: %lld, duration: %lld s, card: %lld",
         room.m_roomId.c_str(), (long long)room.m_groupId, (long long)room.m_initiatorId, (long long)duration,
         (long long)cardMsgId);
}
