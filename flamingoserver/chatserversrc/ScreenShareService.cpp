/**
 *  屏幕共享信令的服务端处理流程，ScreenShareService.cpp
 *  职责划分与调用约束见 ScreenShareService.h；各请求的字段与推送时机见 docs/协议设计-v2.md 的屏幕共享一节。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ScreenShareService.h"

#include <time.h>
#include <algorithm>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "ChatServer.h"
#include "CallManager.h"
#include "CallProtocol.h"   // call_state_connected（共享密码对通话对方免校验）
#include "GroupManager.h"
#include "GroupCallService.h"   // isUserInGroupCall（与群通话互斥）
#include "MessageManager.h"
#include "MessageProtocol.h"
#include "ScreenShareManager.h"
#include "ScreenShareLinkTable.h"   //分享链接码表（第 25 期）
#include "ScreenShareProtocol.h"
#include "ScreenShareRules.h"
#include "UserManager.h"            //移交主持人时查新主持人的昵称
#include "DepartmentManager.h"      //getUserEnterpriseId：凭链接加入须与发起人同企业（第 25 期）
#include "CloudRecordProtocol.h"    //1754 的解析与应答（第 26 期）
#include "CloudRecordService.h"     //云端录制的检查、建记录与通知 imgserver（第 26 期）

namespace {

//秒到毫秒的换算系数
const int64_t kMsPerSecond = 1000;

//群聊结束提示的发送者 userid：服务器生成的系统消息一律记 0，须与 ChatSession.cpp 的 kSystemMessageSenderId 一致。
//单聊的结束提示则记在发起人名下，这样它落在两人的会话里（单聊会话由收发双方的 userid 确定）
const int32_t kSystemSenderId = 0;

//服务器生成的两种消息的 clientmsgid 前缀，后接房间号。以 "sys-" 开头，避免与真实客户端消息的去重键相撞
const char* kCardClientMsgIdPrefix = "sys-screenshare-card-";
const char* kEndTipClientMsgIdPrefix = "sys-screenshare-end-";

int64_t nowMs()
{
    return static_cast<int64_t>(time(NULL)) * kMsPerSecond;
}

//错误码对应的应答 msg 文本（给排查者看，客户端只认 code）
const char* errorText(int code)
{
    switch (code)
    {
    //请求包体缺字段或取值非法
    case error_code_bad_request:
        return "bad request";
    //房间已结束或房间号无效
    case error_code_screenshare_not_exist:
        return "room not exist";
    //不是房间成员，或没有资格加入
    case error_code_screenshare_not_member:
        return "not a member";
    //该会话已有进行中的共享
    case error_code_screenshare_exists:
        return "conversation already sharing";
    //房间人数已满
    case error_code_screenshare_room_full:
        return "room full";
    //正在一对一通话中
    case error_code_screenshare_in_call:
        return "in call";
    //发起人已收回录制权限
    case error_code_screenshare_no_record_perm:
        return "no record permission";
    //已在另一个共享房间里
    case error_code_screenshare_in_other_room:
        return "already in another room";
    //同时开摄像头的人数已达上限
    case error_code_screenshare_camera_limit:
        return "camera limit reached";
    //只有发起人能执行该操作
    case error_code_screenshare_not_host:
        return "not host";
    //要控制的画面不存在（对方没在共享屏幕或暂时掉线）
    case error_code_screenshare_not_sharing:
        return "not sharing";
    //无权授予、拒绝或收回控制
    case error_code_screenshare_not_owner:
        return "not screen owner";
    //正在群通话里（已加入或振铃中）
    case error_code_groupcall_busy:
        return "in group call";
    //企业没有开通云端录制（第 26 期）
    case error_code_cloudrecord_disabled:
        return "cloud record disabled";
    //已经在云端录制，或没有在云端录制（第 26 期）
    case error_code_cloudrecord_busy:
        return "cloud record busy";
    //云端存储服务不可用（第 26 期）
    case error_code_cloudrecord_unavailable:
        return "cloud storage unavailable";
    //云端录像配额已满（第 26 期）
    case error_code_storage_quota:
        return "quota exceeded";
    //其余错误码：本模块不会产生，兜底
    default:
        return "failed";
    }
}

//构造一条通用应答帧
ScreenShareReply resultReply(uint16_t cmd, int code, const std::string& roomId)
{
    return ScreenShareReply(cmd, ScreenShareProtocol::buildResult(code, code == error_code_ok ? "ok" : errorText(code),
                                                                  roomId));
}

void pushTo(const std::vector<int64_t>& users, uint16_t cmd, const std::string& body)
{
    if (users.empty())
        return;
    Singleton<ChatServer>::Instance().pushToUsers(users, cmd, body);
}

void pushToOne(int64_t userId, uint16_t cmd, const std::string& body)
{
    std::vector<int64_t> users(1, userId);
    pushTo(users, cmd, body);
}

std::vector<int64_t> groupMemberIds(int64_t groupId)
{
    std::vector<GroupMemberRecord> members;
    Singleton<GroupManager>::Instance().getGroupMembers(groupId, members);
    std::vector<int64_t> ids;
    ids.reserve(members.size());
    for (size_t i = 0; i < members.size(); ++i)
        ids.push_back(members[i].userid);
    return ids;
}

std::vector<int64_t> roomMemberIds(const ScreenShareRoom& room)
{
    std::vector<int64_t> ids;
    ids.reserve(room.m_members.size());
    for (size_t i = 0; i < room.m_members.size(); ++i)
        ids.push_back(room.m_members[i].m_userId);
    return ids;
}

//会话里应当收到「开始 / 结束」类消息的全体用户：单聊为双方，群聊为全体群成员
std::vector<int64_t> conversationUsers(const ScreenShareRoom& room)
{
    if (room.m_ctype == conversation_group)
        return groupMemberIds(room.m_convKeyA);
    std::vector<int64_t> ids;
    ids.push_back(room.m_convKeyA);
    ids.push_back(room.m_convKeyB);
    return ids;
}

//向房间全体成员推一条房间状态（1725），附事件发生后的完整快照
void broadcastState(int event, int64_t actor, const ScreenShareRoom& room, int64_t target = 0)
{
    pushTo(roomMemberIds(room), cmd_screenshare_state_notify,
           ScreenShareProtocol::buildStateNotify(event, actor, room, target));
}

//重新取房间快照后推状态；房间已不存在时不推
void broadcastCurrentState(int event, int64_t actor, const std::string& roomId)
{
    ScreenShareRoom room;
    if (Singleton<ScreenShareManager>::Instance().getRoom(roomId, room))
        broadcastState(event, actor, room);
}

//给某成员签发中转令牌并把 1726 追加到 outReplies；此人不在房间里时不追加。p2pSessionId 非空时 1726 带上它
bool appendRelayInfo(const std::string& roomId, int64_t userId, std::vector<ScreenShareReply>& outReplies,
                     const std::string& p2pSessionId = std::string())
{
    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    std::string token;
    int seat = 0;
    if (!mgr.signTokenFor(roomId, userId, nowMs(), token, seat))
        return false;
    ScreenShareConfig config = mgr.getConfig();
    //共享已进行的时长（第 13 期），客户端据此计时
    ScreenShareRoom room;
    const int64_t now = nowMs();
    const int64_t elapsed = mgr.getRoom(roomId, room) ? now - room.m_startTime : 0;
    outReplies.push_back(ScreenShareReply(cmd_screenshare_relay_info,
                                          ScreenShareProtocol::buildRelayInfo(roomId, config.m_relayHost,
                                                                              config.m_relayPort, token, seat,
                                                                              config.m_maxCameras, elapsed,
                                                                              p2pSessionId)));
    return true;
}

/**
 *  往共享所在的会话里写一条服务器生成的消息，并推给 receivers。
 *  单聊时消息记在 from 与对端之间，每个接收者看到的 cid 是「另一个人」；群聊时 to 为群 id。
 *  落库失败时记错误日志、不推送（卡片与提示只是附带的通知，不影响共享本身）。
 *  @return 落库得到的消息 id；失败返回 0（第 15 期起发起共享时据此记下卡片 msgid）
 */
int64_t saveAndPushMessage(const ScreenShareRoom& room, int32_t from, int32_t msgtype, const std::string& content,
                           const std::string& clientMsgId, const std::vector<int64_t>& receivers)
{
    //服务器生成的消息不带 @ 与引用；落库时与其它消息一样套一层 buildStoredContent 的外壳
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    const int64_t now = nowMs();
    const int64_t to = (room.m_ctype == conversation_single) ? ScreenShareRules::conversationIdFor(room, from)
                                                              : room.m_convKeyA;
    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, room.m_ctype, from, static_cast<int32_t>(to), msgtype, stored, now);
    if (msgId == 0)
    {
        LOGE("screenshare: save message failed, room: %s, msgtype: %d", room.m_roomId.c_str(), msgtype);
        return 0;
    }

    MessageRecord rec;
    rec.msgid = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype = room.m_ctype;
    rec.from = from;
    rec.to = to;
    rec.msgtype = msgtype;
    rec.content = content;
    rec.mentionall = false;
    rec.timestamp = now;
    rec.recalled = false;

    if (room.m_ctype == conversation_group)
    {
        pushTo(receivers, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, room.m_convKeyA));
        return msgId;
    }
    for (size_t i = 0; i < receivers.size(); ++i)
    {
        pushToOne(receivers[i], cmd_message_notify,
                  MessageProtocol::buildMessageNotify(rec, ScreenShareRules::conversationIdFor(room, receivers[i])));
    }
    return msgId;
}

//会话成员加上房间里中途退群的观看者：「共享已结束」与「标题与描述变了」都要发给这些人
std::vector<int64_t> conversationAndRoomUsers(const ScreenShareRoom& room)
{
    std::vector<int64_t> users = conversationUsers(room);
    const std::vector<int64_t> members = roomMemberIds(room);
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (std::find(users.begin(), users.end(), members[i]) == users.end())
            users.push_back(members[i]);
    }
    return users;
}

/**
 *  推 1734（共享标题与描述变了，第 15 期；移交主持人后也推）给会话成员与房间里中途退群的观看者。
 *  单聊每人看到的 cid 不同，逐个推；群聊同一份包体一次推完。
 *  @param cardMsgId   改写过的卡片消息 id；不大于 0 表示这次不改写卡片（推送里不带卡片）
 *  @param cardContent 改写后的卡片 content；cardMsgId 不大于 0 时不使用
 *  @param hostId      新主持人 userid；不大于 0 表示主持人没变（推送里不带 hostid 与 hostname）
 *  @param hostName    新主持人昵称（UTF-8）
 *  @return 推送的人数
 */
int pushInfoNotify(const ScreenShareRoom& room, const std::string& title, const std::string& desc, int64_t cardMsgId,
                   const std::string& cardContent, int64_t hostId, const std::string& hostName)
{
    const std::vector<int64_t> users = conversationAndRoomUsers(room);
    if (room.m_ctype == conversation_group)
    {
        pushTo(users, cmd_screenshare_info_notify,
               ScreenShareProtocol::buildInfoNotify(room.m_roomId, room.m_ctype, room.m_convKeyA, title, desc,
                                                    cardMsgId, cardContent, hostId, hostName));
        return static_cast<int>(users.size());
    }
    for (size_t i = 0; i < users.size(); ++i)
    {
        pushToOne(users[i], cmd_screenshare_info_notify,
                  ScreenShareProtocol::buildInfoNotify(room.m_roomId, room.m_ctype,
                                                       ScreenShareRules::conversationIdFor(room, users[i]), title,
                                                       desc, cardMsgId, cardContent, hostId, hostName));
    }
    return static_cast<int>(users.size());
}

/**
 *  房间已解散后的收尾：向会话成员推「共享已结束」（1729），并往会话里写一条结束提示。
 *  @param room   解散前的房间快照
 *  @param reason 结束原因，取值见 enum screenshare_end_reason
 */
void finishRoom(const ScreenShareRoom& room, int reason)
{
    //分享链接（第 25 期）：共享结束后链接码再保留一段时间，这期间点链接提示「共享已结束」
    Singleton<ScreenShareLinkTable>::Instance().onRoomEnded(room.m_roomId, nowMs());
    //云端录制（第 26 期）：房间解散时一并停止；控制连接断开时 stop 发不出去，恢复后由 CloudRecordService::onLinkUp 补发
    if (room.m_cloudRecordId > 0 && CloudRecordService::instance() != NULL)
    {
        CloudRecordService::instance()->sendStop(room.m_cloudRecordId);
        LOGI("screenshare ended with cloud record, room: %s, record: %lld", room.m_roomId.c_str(),
             (long long)room.m_cloudRecordId);
    }
    int64_t durationSeconds = (nowMs() - room.m_startTime) / kMsPerSecond;
    if (durationSeconds < 0)
        durationSeconds = 0;

    //会话成员之外，房间里可能还有中途退群的观看者，也要让他们的客户端关掉观看窗口
    const std::vector<int64_t> users = conversationAndRoomUsers(room);

    if (room.m_ctype == conversation_group)
    {
        pushTo(users, cmd_screenshare_ended_notify,
               ScreenShareProtocol::buildEndedNotify(room.m_roomId, room.m_ctype, room.m_convKeyA, room.m_hostId,
                                                     durationSeconds, reason));
    }
    else
    {
        for (size_t i = 0; i < users.size(); ++i)
        {
            pushToOne(users[i], cmd_screenshare_ended_notify,
                      ScreenShareProtocol::buildEndedNotify(room.m_roomId, room.m_ctype,
                                                            ScreenShareRules::conversationIdFor(room, users[i]),
                                                            room.m_hostId, durationSeconds, reason));
        }
    }

    //结束提示的 content 与文本消息同构（带 text 字段），另带 tipkind / tipargs 供新客户端按界面语言生成文字，
    //与群里其它系统提示一致（多语言第 5 期）
    const std::string content = ScreenShareRules::buildEndTipContent(durationSeconds);
    const int32_t from = (room.m_ctype == conversation_single) ? static_cast<int32_t>(room.m_hostId) : kSystemSenderId;
    saveAndPushMessage(room, from, message_type_system, content, std::string(kEndTipClientMsgIdPrefix) + room.m_roomId,
                       conversationUsers(room));

    LOGI("screenshare ended, room: %s, host: %lld, reason: %d, duration: %lld s, members: %d", room.m_roomId.c_str(),
         (long long)room.m_hostId, reason, (long long)durationSeconds, (int)room.m_members.size());
}

//userId 是否正与 peerId 处于已接通的一对一通话（共享密码对通话对方免校验，第 12 期）
bool isInCallWith(int64_t userId, int64_t peerId)
{
    CallSessionInfo call;
    if (!Singleton<CallManager>::Instance().getUserCall(userId, call) || call.state != call_state_connected)
        return false;
    const int64_t callPeer = (call.callerId == userId) ? call.calleeId : call.callerId;
    return callPeer == peerId;
}

//查出 userId 当前所在的通话，交给 ScreenShareRules::checkCallConflict 判断与共享是否冲突
int checkCallConflictFor(int64_t userId, int ctype, int64_t sharePeerId)
{
    CallSessionInfo call;
    const bool inCall = Singleton<CallManager>::Instance().getUserCall(userId, call);
    const int64_t callPeer = (call.callerId == userId) ? call.calleeId : call.callerId;
    const int code = ScreenShareRules::checkCallConflict(inCall, call.state, callPeer, ctype, sharePeerId);
    if (inCall && code == error_code_ok)
        LOGI("screenshare allowed during call, user: %lld, call: %s, peer: %lld", (long long)userId,
             call.sessionid.c_str(), (long long)callPeer);
    return code;
}

void handleStart(int64_t userId, const std::string& nickname, const std::string& body,
                 std::vector<ScreenShareReply>& outReplies, IScreenShareSensitiveChecker* checker)
{
    ScreenShareStartRequest req;
    if (!ScreenShareProtocol::parseStartRequest(body, req))
    {
        outReplies.push_back(resultReply(cmd_screenshare_start, error_code_bad_request, ""));
        return;
    }
    const int ctype = req.m_ctype;
    const int64_t cid = req.m_cid;
    const std::string& password = req.m_password;

    //群聊只有群成员能发起；与一对一通话互斥，只有正在与单聊对端通话时例外（通话中共享屏幕）
    int code = error_code_ok;
    if (ctype == conversation_group &&
        !Singleton<GroupManager>::Instance().isGroupMember(cid, static_cast<int32_t>(userId)))
        code = error_code_screenshare_not_member;
    else
        code = checkCallConflictFor(userId, ctype, cid);
    //与群通话互斥：本人在群通话里（已加入或振铃中）不能发起屏幕共享（docs/协议设计-v2.md 21.1）
    if (code == error_code_ok && GroupCallService::isUserInGroupCall(userId))
        code = error_code_groupcall_busy;
    //共享标题与描述（第 15 期）：标题必填、长度、敏感词；资格与通话互斥检查通过之后才查，避免为无权发起的请求记拦截记录
    if (code == error_code_ok)
        code = ScreenShareRules::checkShareInfo(req.m_title, req.m_desc, ctype, cid, checker);

    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    std::string roomId;
    if (code == error_code_ok)
        code = mgr.startShare(userId, ctype, cid, req.m_mic, req.m_camera, req.m_sysAudio, nowMs(), roomId, password,
                              req.m_forced, req.m_recordAllowed);
    ScreenShareRoom room;
    if (code == error_code_ok && !mgr.getRoom(roomId, room))
        code = error_code_screenshare_not_exist;
    if (code != error_code_ok)
    {
        outReplies.push_back(resultReply(cmd_screenshare_start, code, ""));
        LOGI("screenshare start rejected, user: %lld, ctype: %d, cid: %lld, code: %d", (long long)userId, ctype,
             (long long)cid, code);
        return;
    }

    outReplies.push_back(resultReply(cmd_screenshare_start, error_code_ok, roomId));
    appendRelayInfo(roomId, userId, outReplies);
    //记下发起人昵称（第 13 期）：强制模式下掉线的观看者重新登录时，rejoin 推送据此带上发起人名
    mgr.setHostName(roomId, nickname);
    //记下共享标题与描述（第 15 期），之后的 1725、1727 与 rejoin 推送都从房间取
    mgr.setInfo(roomId, userId, req.m_title, req.m_desc);

    //通知会话里的其他人：单聊每人看到的 cid 不同，逐个推；群聊同一份包体一次推完
    std::vector<int64_t> groupMembers;
    if (ctype == conversation_group)
        groupMembers = groupMemberIds(cid);
    const std::vector<int64_t> targets = ScreenShareRules::startNotifyTargets(room, groupMembers);
    if (ctype == conversation_group)
    {
        pushTo(targets, cmd_screenshare_started_notify,
               ScreenShareProtocol::buildStartedNotify(roomId, ctype, cid, userId, nickname, room.m_startTime,
                                                       !password.empty(), req.m_forced, false, req.m_title,
                                                       req.m_desc));
    }
    else
    {
        for (size_t i = 0; i < targets.size(); ++i)
        {
            pushToOne(targets[i], cmd_screenshare_started_notify,
                      ScreenShareProtocol::buildStartedNotify(roomId, ctype,
                                                              ScreenShareRules::conversationIdFor(room, targets[i]),
                                                              userId, nickname, room.m_startTime, !password.empty(),
                                                              req.m_forced, false, req.m_title, req.m_desc));
        }
    }

    //共享卡片记在发起人名下；发起人自己的客户端也要显示这张卡片，所以接收者含发起人
    std::vector<int64_t> receivers = targets;
    receivers.push_back(userId);
    const int64_t cardMsgId = saveAndPushMessage(
        room, static_cast<int32_t>(userId), message_type_screenshare,
        ScreenShareProtocol::buildCardContent(roomId, userId, nickname, room.m_startTime, !password.empty(), req.m_forced,
                                              req.m_title, req.m_desc),
        std::string(kCardClientMsgIdPrefix) + roomId, receivers);
    //记下卡片的消息 id（第 15 期）：共享中修改标题与描述时据此改写这张卡片
    if (cardMsgId > 0)
        mgr.setCardMsgId(roomId, cardMsgId);

    LOGI("screenshare started, room: %s, host: %lld, ctype: %d, cid: %lld, notified: %d, password: %d, forced: %d, "
         "record allowed: %d, title: %s, desc bytes: %d", roomId.c_str(), (long long)userId, ctype, (long long)cid,
         (int)targets.size(), password.empty() ? 0 : 1, req.m_forced ? 1 : 0, req.m_recordAllowed ? 1 : 0,
         req.m_title.c_str(), (int)req.m_desc.size());
}

//单聊共享里一名观看者与发起人之间 P2P 会话的功能会话号：房间号 + 分隔符 + 座位号（房间号里没有这个字符）
const char kShareP2PFeatureSeparator = '#';

void handleJoin(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies,
                IScreenShareP2PLinker* p2pLinker)
{
    std::string roomId;
    std::string password;
    std::string linkCode;
    if (!ScreenShareProtocol::parseJoinRequest(body, roomId, password, linkCode))
    {
        outReplies.push_back(resultReply(cmd_screenshare_join, error_code_bad_request, ""));
        return;
    }

    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    ScreenShareRoom room;
    int code = error_code_ok;
    bool alreadyIn = false;
    //凭分享链接加入（第 25 期）：是否作为链接访客、是否免输密码
    bool viaLink = false;
    bool skipPassword = false;
    if (!mgr.getRoom(roomId, room))
    {
        code = error_code_screenshare_not_exist;
    }
    else
    {
        const bool isGroupMember = (room.m_ctype == conversation_group) &&
            Singleton<GroupManager>::Instance().isGroupMember(room.m_convKeyA, static_cast<int32_t>(userId));
        code = ScreenShareRules::checkJoinEligibility(room, userId, isGroupMember);
        alreadyIn = (room.findMember(userId) != NULL);
        //已在房间里的人重复加入是幂等的，不再按链接判定（已是链接访客的保持原标记）
        if (!linkCode.empty() && !alreadyIn)
        {
            std::string linkRoomId;
            bool linkNoPassword = false;
            const int linkStatus =
                Singleton<ScreenShareLinkTable>::Instance().resolve(linkCode, linkRoomId, linkNoPassword);
            DepartmentManager& dept = Singleton<DepartmentManager>::Instance();
            const int64_t selfEnterprise = dept.getUserEnterpriseId(userId);
            const bool sameEnterprise = selfEnterprise > 0 && selfEnterprise == dept.getUserEnterpriseId(room.m_hostId);
            code = ScreenShareRules::checkLinkJoin(room, userId, code, linkStatus, linkRoomId, linkNoPassword,
                                                   sameEnterprise, viaLink, skipPassword);
        }
        else if (alreadyIn && code == error_code_screenshare_not_member)
        {
            //链接访客重新要一份中转令牌（如 P2P 断开后回到中转）：已在房间里就照常放行
            const ScreenShareMember* me = room.findMember(userId);
            if (me != NULL && me->m_viaLink)
                code = error_code_ok;
        }
        //重复加入是幂等的，已在房间里的人不再做通话互斥判断；正在与发起人通话的人可以加入发起人的单聊共享
        if (code == error_code_ok && !alreadyIn)
            code = checkCallConflictFor(userId, room.m_ctype, room.m_hostId);
        //与群通话互斥：本人在群通话里（已加入或振铃中）不能加入屏幕共享
        if (code == error_code_ok && !alreadyIn && GroupCallService::isUserInGroupCall(userId))
            code = error_code_groupcall_busy;
        //加入密码（第 12 期）：与发起人处于已接通通话的人（通话中共享时自动加入）免校验；
        //打开了「点链接加入免输密码」的分享链接（第 25 期）同样免校验
        if (code == error_code_ok && !skipPassword)
            code = ScreenShareRules::checkJoinPassword(room, userId, password, isInCallWith(userId, room.m_hostId));
    }
    int seat = 0;
    if (code == error_code_ok)
        code = mgr.joinShare(roomId, userId, nowMs(), seat, viaLink);
    if (code != error_code_ok)
    {
        outReplies.push_back(resultReply(cmd_screenshare_join, code, roomId));
        LOGI("screenshare join rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId, code);
        return;
    }

    //P2P 方案第 6 期：单聊共享里新加入的观看者与发起人之间建 P2P 会话，1726 与房间快照里这名观看者一项带会话号。
    //重复加入（P2P 中途断开后观看者回到房间中转、来换一份新令牌）不再建，1726 不带会话号
    std::string p2pSessionId;
    //链接访客（第 25 期）不建 P2P：单聊共享里一旦有了第二名观看者，发起端就改回房间中转，P2P 只服务会话对端一人。
    //云端录制中（第 26 期）也不建：帧要经过中转才能被 imgserver 录下来
    if (!alreadyIn && !viaLink && p2pLinker != NULL && room.m_ctype == conversation_single && room.m_cloudRecordId == 0)
    {
        const std::string featureId = roomId + kShareP2PFeatureSeparator + std::to_string(seat);
        if (p2pLinker->createViewerLink(featureId, room.m_hostId, userId, p2pSessionId))
            mgr.setMemberP2PSession(roomId, userId, p2pSessionId);
        else
            p2pSessionId.clear();
    }

    outReplies.push_back(resultReply(cmd_screenshare_join, error_code_ok, roomId));
    appendRelayInfo(roomId, userId, outReplies, p2pSessionId);
    if (!alreadyIn)
        broadcastCurrentState(screenshare_event_member_joined, userId, roomId);
    LOGI("screenshare joined, room: %s, user: %lld, seat: %d, again: %d, via link: %d", roomId.c_str(),
         (long long)userId, seat, alreadyIn ? 1 : 0, viaLink ? 1 : 0);
}

//构造 1754 的应答帧
ScreenShareReply cloudRecordReply(int code, const std::string& roomId, int action, int64_t recordId)
{
    //成功时 msg 为 "ok"，与 resultReply 一致（errorText 只列错误码，成功码会落到兜底的 "failed"）
    return ScreenShareReply(cmd_cloudrecord_control,
                            CloudRecordProtocol::buildControlResponse(code, code == error_code_ok ? "ok" : errorText(code),
                                                                      roomId, action, recordId));
}

/**
 *  共享中的云端录制（1754，第 26 期）：只有主持人能开始、暂停、继续、停止。
 *  开始的顺序：房间与主持人检查 → 开关、控制连接、配额检查 → 建录像记录 → 记进房间 → 通知 imgserver → 回应答并经 1725 事件 15 广播。
 *  任何一步失败都撤销前面已做的（删记录、清房间状态），房间里的人不会看到半途而废的「正在云端录制」。
 */
void handleCloudRecordControl(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    int action = 0;
    if (!CloudRecordProtocol::parseControlRequest(body, roomId, action))
    {
        outReplies.push_back(cloudRecordReply(error_code_bad_request, "", 0, 0));
        return;
    }
    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    CloudRecordService* service = CloudRecordService::instance();
    ScreenShareRoom room;
    int code = error_code_ok;
    if (!mgr.getRoom(roomId, room))
        code = error_code_screenshare_not_exist;
    else if (room.m_hostId != userId)
        code = error_code_screenshare_not_host;
    else if (service == NULL)
        code = error_code_cloudrecord_unavailable;
    int64_t recordId = 0;
    if (code == error_code_ok)
    {
        switch (action)
        {
        //开始
        case cloudrecord_control_start:
        {
            if (room.m_cloudRecordId != 0)
            {
                code = error_code_cloudrecord_busy;
                break;
            }
            code = service->checkStart(userId);
            if (code != error_code_ok)
                break;
            CloudRecordRoomInfo info;
            info.m_roomId = roomId;
            info.m_ctype = room.m_ctype;
            info.m_cid = (room.m_ctype == conversation_single) ? ScreenShareRules::conversationIdFor(room, userId)
                                                                : room.m_convKeyA;
            info.m_title = room.m_title;
            code = service->createShareRecord(userId, info, nowMs(), recordId);
            if (code != error_code_ok)
                break;
            code = mgr.beginCloudRecord(roomId, userId, recordId, nowMs(), room);
            if (code != error_code_ok)
            {
                service->discardRecord(recordId);
                recordId = 0;
                break;
            }
            if (!service->sendStart(recordId, roomId))
            {
                int64_t ended = 0;
                mgr.endCloudRecord(roomId, 0, recordId, error_code_cloudrecord_unavailable, ended, room);
                service->discardRecord(recordId);
                recordId = 0;
                code = error_code_cloudrecord_unavailable;
            }
            break;
        }

        //暂停、继续
        case cloudrecord_control_pause:
        case cloudrecord_control_resume:
        {
            const bool pause = (action == cloudrecord_control_pause);
            code = mgr.setCloudRecordPaused(roomId, userId, pause, recordId, room);
            if (code == error_code_ok)
            {
                if (pause)
                    service->sendPause(recordId);
                else
                    service->sendResume(recordId);
            }
            break;
        }

        //停止：没发出去的 stop 由控制连接恢复后补发，房间状态照常清掉
        default:
            code = mgr.endCloudRecord(roomId, userId, 0, 0, recordId, room);
            if (code == error_code_ok)
                service->sendStop(recordId);
            break;
        }
    }
    outReplies.push_back(cloudRecordReply(code, roomId, action, recordId));
    if (code != error_code_ok)
    {
        LOGI("screenshare cloud record rejected, room: %s, user: %lld, action: %d, code: %d", roomId.c_str(),
             (long long)userId, action, code);
        return;
    }
    broadcastState(screenshare_event_cloudrecord_changed, userId, room);
    LOGI("screenshare cloud record, room: %s, host: %lld, action: %d, record: %lld", roomId.c_str(), (long long)userId,
         action, (long long)recordId);
}

//分享链接（1752，第 25 期）：只有主持人能取得、重置、停用或设置免输密码
void handleLink(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    int action = 0;
    bool noPassword = false;
    if (!ScreenShareProtocol::parseLinkRequest(body, roomId, action, noPassword))
    {
        outReplies.push_back(ScreenShareReply(cmd_screenshare_link,
                                              ScreenShareProtocol::buildLinkResponse(error_code_bad_request,
                                                                                     "bad request", "", "", false)));
        return;
    }
    ScreenShareRoom room;
    int code = error_code_ok;
    if (!Singleton<ScreenShareManager>::Instance().getRoom(roomId, room))
        code = error_code_screenshare_not_exist;
    else if (room.m_hostId != userId)
        code = error_code_screenshare_not_host;
    if (code != error_code_ok)
    {
        outReplies.push_back(ScreenShareReply(cmd_screenshare_link,
                                              ScreenShareProtocol::buildLinkResponse(code, "", roomId, "", false)));
        LOGI("screenshare link rejected, room: %s, user: %lld, action: %d, code: %d", roomId.c_str(),
             (long long)userId, action, code);
        return;
    }
    ScreenShareLinkTable& links = Singleton<ScreenShareLinkTable>::Instance();
    std::string linkCode;
    bool currentNoPassword = false;
    switch (action)
    {
    //重置：旧码作废，生成新码
    case screenshare_link_reset:
        links.reset(roomId, linkCode, currentNoPassword);
        break;
    //停用：作废后不再生成，应答里链接码为空
    case screenshare_link_disable:
        links.disable(roomId);
        break;
    //设置免输密码：没有链接时先生成
    case screenshare_link_set_nopassword:
        links.getOrCreate(roomId, linkCode, currentNoPassword);
        links.setNoPassword(roomId, noPassword);
        currentNoPassword = noPassword;
        break;
    //取得：没有就生成
    default:
        links.getOrCreate(roomId, linkCode, currentNoPassword);
        break;
    }
    outReplies.push_back(ScreenShareReply(cmd_screenshare_link,
                                          ScreenShareProtocol::buildLinkResponse(error_code_ok, "", roomId, linkCode,
                                                                                 currentNoPassword)));
    //日志不记链接码本身（持有链接码即可加入）
    LOGI("screenshare link, room: %s, host: %lld, action: %d, active: %d, no password: %d", roomId.c_str(),
         (long long)userId, action, linkCode.empty() ? 0 : 1, currentNoPassword ? 1 : 0);
}

//凭分享链接码查共享概要（1753，第 25 期）：频率限制、企业校验，链接访客不下发会话类型与会话 id
void handleLinkResolve(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string linkCode;
    ScreenShareLinkSummary summary;
    if (!ScreenShareProtocol::parseLinkResolveRequest(body, linkCode))
    {
        outReplies.push_back(ScreenShareReply(cmd_screenshare_link_resolve,
                                              ScreenShareProtocol::buildLinkResolveResponse(error_code_bad_request,
                                                                                            "bad request", summary)));
        return;
    }
    ScreenShareLinkTable& links = Singleton<ScreenShareLinkTable>::Instance();
    int code = error_code_ok;
    std::string roomId;
    bool noPassword = false;
    int status = screenshare_link_invalid;
    if (!links.allowResolve(userId, nowMs()))
        code = error_code_screenshare_link_too_frequent;
    else
        status = links.resolve(linkCode, roomId, noPassword);
    if (code == error_code_ok && status == screenshare_link_invalid)
        code = error_code_screenshare_link_invalid;
    ScreenShareRoom room;
    if (code == error_code_ok && status == screenshare_link_active &&
        !Singleton<ScreenShareManager>::Instance().getRoom(roomId, room))
    {
        //链接还在、房间却已解散（解散与清理之间的极短间隙）：按已结束处理
        status = screenshare_link_ended;
    }
    summary.m_status = status;
    if (code == error_code_ok && status == screenshare_link_active)
    {
        const bool isGroupMember = (room.m_ctype == conversation_group) &&
            Singleton<GroupManager>::Instance().isGroupMember(room.m_convKeyA, static_cast<int32_t>(userId));
        const bool member = (ScreenShareRules::checkJoinEligibility(room, userId, isGroupMember) !=
                             error_code_screenshare_not_member);
        DepartmentManager& dept = Singleton<DepartmentManager>::Instance();
        const int64_t selfEnterprise = dept.getUserEnterpriseId(userId);
        const bool sameEnterprise = selfEnterprise > 0 && selfEnterprise == dept.getUserEnterpriseId(room.m_hostId);
        //不是会话成员又不同企业：不透露共享的任何信息
        if (!member && !sameEnterprise)
        {
            code = error_code_screenshare_link_enterprise;
        }
        else
        {
            summary.m_roomId = room.m_roomId;
            summary.m_title = room.m_title;
            summary.m_desc = room.m_desc;
            summary.m_hostId = room.m_hostId;
            summary.m_hostName = room.m_hostName;
            summary.m_locked = !room.m_password.empty() && !noPassword;
            summary.m_forced = room.m_forced;
            summary.m_member = member;
            if (member)
            {
                summary.m_ctype = room.m_ctype;
                summary.m_cid = ScreenShareRules::conversationIdFor(room, userId);
            }
        }
    }
    outReplies.push_back(ScreenShareReply(cmd_screenshare_link_resolve,
                                          ScreenShareProtocol::buildLinkResolveResponse(code, "", summary)));
    LOGI("screenshare link resolve, user: %lld, status: %d, code: %d, room: %s", (long long)userId, status, code,
         summary.m_roomId.c_str());
}

void handleLeave(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    if (!ScreenShareProtocol::parseRoomRequest(body, roomId))
    {
        outReplies.push_back(resultReply(cmd_screenshare_leave, error_code_bad_request, ""));
        return;
    }

    bool dissolved = false;
    ScreenShareRoom before;
    const int code = Singleton<ScreenShareManager>::Instance().leaveShare(roomId, userId, dissolved, before);
    outReplies.push_back(resultReply(cmd_screenshare_leave, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare leave rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId, code);
        return;
    }

    //发起人离开等同结束共享
    if (dissolved)
        finishRoom(before, screenshare_end_host_stopped);
    else
        broadcastCurrentState(screenshare_event_member_left, userId, roomId);
    LOGI("screenshare left, room: %s, user: %lld, dissolved: %d", roomId.c_str(), (long long)userId,
         dissolved ? 1 : 0);
}

void handleStop(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    if (!ScreenShareProtocol::parseRoomRequest(body, roomId))
    {
        outReplies.push_back(resultReply(cmd_screenshare_stop, error_code_bad_request, ""));
        return;
    }

    ScreenShareRoom room;
    const int code = Singleton<ScreenShareManager>::Instance().stopShare(roomId, userId, room);
    outReplies.push_back(resultReply(cmd_screenshare_stop, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare stop rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId, code);
        return;
    }
    finishRoom(room, screenshare_end_host_stopped);
}

void handleQuery(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    int ctype = 0;
    int64_t cid = 0;
    if (!ScreenShareProtocol::parseQueryRequest(body, ctype, cid))
    {
        outReplies.push_back(ScreenShareReply(cmd_screenshare_query,
                                              ScreenShareProtocol::buildQueryResponse(error_code_bad_request,
                                                                                      "bad request", NULL)));
        return;
    }

    //群里有没有共享只告诉群成员
    if (ctype == conversation_group &&
        !Singleton<GroupManager>::Instance().isGroupMember(cid, static_cast<int32_t>(userId)))
    {
        outReplies.push_back(ScreenShareReply(cmd_screenshare_query,
                                              ScreenShareProtocol::buildQueryResponse(
                                                  error_code_screenshare_not_member, "not a member", NULL)));
        return;
    }

    ScreenShareRoom room;
    const bool found = Singleton<ScreenShareManager>::Instance().findRoomByConversation(ctype, userId, cid, room);
    outReplies.push_back(ScreenShareReply(cmd_screenshare_query,
                                          ScreenShareProtocol::buildQueryResponse(error_code_ok, "ok",
                                                                                  found ? &room : NULL)));
}

void handleMemberMedia(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    bool mic = false;
    bool camera = false;
    bool sysAudio = false;
    if (!ScreenShareProtocol::parseMemberMediaRequest(body, roomId, mic, camera, sysAudio))
    {
        outReplies.push_back(resultReply(cmd_screenshare_member_media, error_code_bad_request, ""));
        return;
    }

    const int code = Singleton<ScreenShareManager>::Instance().setMemberMedia(roomId, userId, mic, camera, sysAudio);
    outReplies.push_back(resultReply(cmd_screenshare_member_media, code, roomId));
    if (code == error_code_ok)
        broadcastCurrentState(screenshare_event_media_changed, userId, roomId);
}

void handleSetPermission(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    int annotate = -1;
    int record = -1;
    bool muteAll = false;
    int64_t muteUser = 0;
    if (!ScreenShareProtocol::parsePermissionRequest(body, roomId, annotate, record, muteAll, muteUser))
    {
        outReplies.push_back(resultReply(cmd_screenshare_set_permission, error_code_bad_request, ""));
        return;
    }

    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    if (annotate != -1 || record != -1)
    {
        std::vector<int64_t> stoppedRecords;
        const int code = mgr.setPermission(roomId, userId, annotate, record, stoppedRecords);
        if (code != error_code_ok)
        {
            outReplies.push_back(resultReply(cmd_screenshare_set_permission, code, roomId));
            return;
        }
        broadcastCurrentState(screenshare_event_permission_changed, userId, roomId);
        //收回录制权限导致有人停止录制：再推一帧录制变化，快照里这些人的 recording 已为 0
        if (!stoppedRecords.empty())
            broadcastCurrentState(screenshare_event_record_changed, userId, roomId);
        LOGI("screenshare permission set, room: %s, annotate: %d, record: %d, stopped records: %d", roomId.c_str(),
             annotate, record, (int)stoppedRecords.size());
    }
    if (muteAll)
    {
        std::vector<int64_t> muted;
        const int code = mgr.muteAll(roomId, userId, muted);
        if (code != error_code_ok)
        {
            outReplies.push_back(resultReply(cmd_screenshare_set_permission, code, roomId));
            return;
        }
        broadcastCurrentState(screenshare_event_muted_all, userId, roomId);
        LOGI("screenshare mute all, room: %s, muted: %d", roomId.c_str(), (int)muted.size());
    }
    //按人静音（第 13 期）：对方原本开着麦克风才广播，被静音的人据 target 关麦并提示
    if (muteUser > 0)
    {
        bool changed = false;
        const int code = mgr.muteMember(roomId, userId, muteUser, changed);
        if (code != error_code_ok)
        {
            outReplies.push_back(resultReply(cmd_screenshare_set_permission, code, roomId));
            LOGI("screenshare mute member rejected, room: %s, target: %lld, code: %d", roomId.c_str(),
                 (long long)muteUser, code);
            return;
        }
        if (changed)
        {
            ScreenShareRoom room;
            if (mgr.getRoom(roomId, room))
                broadcastState(screenshare_event_member_muted, userId, room, muteUser);
        }
        LOGI("screenshare mute member, room: %s, target: %lld, changed: %d", roomId.c_str(), (long long)muteUser,
             changed ? 1 : 0);
    }
    outReplies.push_back(resultReply(cmd_screenshare_set_permission, error_code_ok, roomId));
}

void handleRecordState(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    bool recording = false;
    if (!ScreenShareProtocol::parseRecordStateRequest(body, roomId, recording))
    {
        outReplies.push_back(resultReply(cmd_screenshare_record_state, error_code_bad_request, ""));
        return;
    }

    const int code = Singleton<ScreenShareManager>::Instance().setRecording(roomId, userId, recording);
    outReplies.push_back(resultReply(cmd_screenshare_record_state, code, roomId));
    if (code == error_code_ok)
        broadcastCurrentState(screenshare_event_record_changed, userId, roomId);
    LOGI("screenshare record state, room: %s, user: %lld, recording: %d, code: %d", roomId.c_str(),
         (long long)userId, recording ? 1 : 0, code);
}

void handleRemoveMember(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    int64_t target = 0;
    bool allowRejoin = true;
    if (!ScreenShareProtocol::parseRemoveMemberRequest(body, roomId, target, allowRejoin))
    {
        outReplies.push_back(resultReply(cmd_screenshare_remove_member, error_code_bad_request, ""));
        return;
    }

    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    const int code = mgr.removeMember(roomId, userId, target, allowRejoin);
    outReplies.push_back(resultReply(cmd_screenshare_remove_member, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare remove member rejected, room: %s, target: %lld, code: %d", roomId.c_str(),
             (long long)target, code);
        return;
    }

    ScreenShareRoom room;
    if (mgr.getRoom(roomId, room))
    {
        int64_t durationSeconds = (nowMs() - room.m_startTime) / kMsPerSecond;
        if (durationSeconds < 0)
            durationSeconds = 0;
        //被移出的人经 1729 得知（原因 3 可以再加入、4 不能），客户端据此关掉观看窗并弹出提示；共享本身仍在进行，
        //所以不写结束提示、也不通知会话里的其他人
        const int reason = allowRejoin ? screenshare_end_removed : screenshare_end_removed_banned;
        pushToOne(target, cmd_screenshare_ended_notify,
                  ScreenShareProtocol::buildEndedNotify(room.m_roomId, room.m_ctype,
                                                        ScreenShareRules::conversationIdFor(room, target),
                                                        room.m_hostId, durationSeconds, reason));
        //其余成员按快照刷新名单，快照里已没有此人
        broadcastState(screenshare_event_member_removed, userId, room, target);
    }
    LOGI("screenshare member removed, room: %s, target: %lld, allow rejoin: %d", roomId.c_str(), (long long)target,
         allowRejoin ? 1 : 0);
}

void handleSetInfo(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies,
                   IScreenShareSensitiveChecker* checker)
{
    std::string roomId;
    std::string title;
    std::string desc;
    if (!ScreenShareProtocol::parseSetInfoRequest(body, roomId, title, desc))
    {
        outReplies.push_back(resultReply(cmd_screenshare_set_info, error_code_bad_request, ""));
        return;
    }

    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    ScreenShareRoom room;
    int code = error_code_ok;
    if (!mgr.getRoom(roomId, room))
        code = error_code_screenshare_not_exist;
    else if (room.m_hostId != userId)
        code = error_code_screenshare_not_host;
    //格式与敏感词：确认是发起人之后才查，不为无权修改的请求记拦截记录
    if (code == error_code_ok)
        code = ScreenShareRules::checkShareInfo(title, desc, room.m_ctype,
                                                ScreenShareRules::conversationIdFor(room, userId), checker);
    if (code == error_code_ok)
        code = mgr.setInfo(roomId, userId, title, desc);
    outReplies.push_back(resultReply(cmd_screenshare_set_info, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare set info rejected, room: %s, user: %lld, code: %d", roomId.c_str(), (long long)userId, code);
        return;
    }

    //改写会话里的共享卡片：内容与发起时同构，只换标题与描述；发起人仍写最初的发起人
    const std::string cardContent = ScreenShareRules::buildCardContentFor(room, title, desc);
    int64_t cardMsgId = room.m_cardMsgId;
    if (cardMsgId > 0)
    {
        std::vector<int64_t> noMentions;
        const std::string stored = MessageProtocol::buildStoredContent(cardContent, noMentions, false, "");
        if (!Singleton<MessageManager>::Instance().updateMessageContent(cardMsgId, stored))
        {
            //库里没改成：推送里不带卡片，客户端只刷新横幅与观看窗，不改本地的卡片
            LOGE("screenshare: update card failed, room: %s, msgid: %lld", roomId.c_str(), (long long)cardMsgId);
            cardMsgId = 0;
        }
    }

    //推 1734：会话成员（含发起人的其它设备）与房间里中途退群的观看者
    const int notified = pushInfoNotify(room, title, desc, cardMsgId, cardContent, 0, std::string());
    LOGI("screenshare info changed, room: %s, title: %s, desc bytes: %d, card msgid: %lld, notified: %d",
         roomId.c_str(), title.c_str(), (int)desc.size(), (long long)cardMsgId, notified);
}

void handleControl(int64_t userId, const std::string& nickname, const std::string& body,
                   std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    int action = 0;
    int64_t owner = 0;
    int64_t target = 0;
    if (!ScreenShareProtocol::parseControlRequest(body, roomId, action, owner, target))
    {
        outReplies.push_back(resultReply(cmd_screenshare_control, error_code_bad_request, ""));
        return;
    }

    ScreenShareControlResult result;
    const int code = Singleton<ScreenShareManager>::Instance().applyControl(roomId, userId, action, owner, target,
                                                                            result);
    outReplies.push_back(resultReply(cmd_screenshare_control, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare control rejected, room: %s, user: %lld, action: %d, owner: %lld, target: %lld, code: %d",
             roomId.c_str(), (long long)userId, action, (long long)owner, (long long)target, code);
        return;
    }

    //申请、撤回转给画面主人，拒绝转给申请人
    if (result.m_notifyUser > 0)
    {
        pushToOne(result.m_notifyUser, cmd_screenshare_control_notify,
                  ScreenShareProtocol::buildControlNotify(roomId, action, owner, userId, nickname));
    }
    //授予、收回改变了控制关系：经 1725 广播，快照里画面主人的 controlledby 是改动之后的值
    if (result.m_changed)
    {
        if (action == screenshare_control_grant)
            broadcastState(screenshare_event_control_granted, userId, result.m_room, target);
        else
            broadcastState(screenshare_event_control_revoked, userId, result.m_room, result.m_prevController);
    }
    LOGI("screenshare control, room: %s, user: %lld, action: %d, owner: %lld, target: %lld, prev: %lld, "
         "notify: %lld, changed: %d",
         roomId.c_str(), (long long)userId, action, (long long)owner, (long long)target,
         (long long)result.m_prevController, (long long)result.m_notifyUser, result.m_changed ? 1 : 0);
}

//多人共享（第 22 期，1737）：申请、撤回、同意、拒绝、停止共享屏幕
//白板（第 23 期，1737 的 kind 为 2）：申请、撤回、同意、拒绝、关闭，规则见 ScreenShareManager::applyWhiteboard
void handleWhiteboard(int64_t userId, const std::string& nickname, const std::string& roomId, int action, int64_t target,
                      std::vector<ScreenShareReply>& outReplies)
{
    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    ScreenShareSharingResult result;
    const int code = mgr.applyWhiteboard(roomId, userId, action, target, result);
    outReplies.push_back(resultReply(cmd_screenshare_sharing, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare whiteboard rejected, room: %s, user: %lld, action: %d, target: %lld, code: %d", roomId.c_str(),
             (long long)userId, action, (long long)target, code);
        return;
    }
    //新建（主持人自己直接建、或同意别人的申请）时动作写成「同意」并带白板座位，建立者据此打开白板窗口
    if (result.m_notifyUser > 0)
    {
        const int notifyAction = (result.m_boardSeat >= 0) ? screenshare_sharing_approve : action;
        pushToOne(result.m_notifyUser, cmd_screenshare_sharing_notify,
                  ScreenShareProtocol::buildBoardNotify(roomId, notifyAction, userId, nickname, result.m_boardSeat));
    }
    broadcastState(screenshare_event_sharing_changed, userId, result.m_room, result.m_subject);
    LOGI("screenshare whiteboard, room: %s, user: %lld, action: %d, target: %lld, subject: %lld, seat: %d", roomId.c_str(),
         (long long)userId, action, (long long)target, (long long)result.m_subject, result.m_boardSeat);
}

void handleSharing(int64_t userId, const std::string& nickname, const std::string& body,
                   std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    int action = 0;
    int64_t target = 0;
    if (!ScreenShareProtocol::parseSharingRequest(body, roomId, action, target))
    {
        outReplies.push_back(resultReply(cmd_screenshare_sharing, error_code_bad_request, ""));
        return;
    }
    //白板（第 23 期）
    if (ScreenShareProtocol::parseSharingKind(body) == screenshare_kind_whiteboard)
    {
        handleWhiteboard(userId, nickname, roomId, action, target, outReplies);
        return;
    }
    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    ScreenShareSharingResult result;
    const int code = mgr.applySharing(roomId, userId, action, target, result);
    outReplies.push_back(resultReply(cmd_screenshare_sharing, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare sharing rejected, room: %s, user: %lld, action: %d, target: %lld, code: %d", roomId.c_str(),
             (long long)userId, action, (long long)target, code);
        return;
    }
    //同意：给被同意的人签「只推流」令牌，连同中转地址与画面座位经 1738 发给他。经移交成为主持人的人自己申请时也走这里，
    //这时 1738 的动作写成「同意」，客户端按同一流程开始推流
    if (result.m_publishUser > 0)
    {
        std::string token;
        int screenSeat = -1;
        if (mgr.signPublishToken(roomId, result.m_publishUser, nowMs(), token, screenSeat))
        {
            const ScreenShareConfig config = mgr.getConfig();
            const int notifyAction = (result.m_publishUser == userId) ? screenshare_sharing_approve : action;
            pushToOne(result.m_publishUser, cmd_screenshare_sharing_notify,
                      ScreenShareProtocol::buildSharingNotify(roomId, notifyAction, userId, nickname, token,
                                                              config.m_relayHost, config.m_relayPort, screenSeat));
        }
        else
        {
            LOGE("screenshare sharing: sign publish token failed, room: %s, user: %lld", roomId.c_str(),
                 (long long)result.m_publishUser);
        }
    }
    else if (result.m_notifyUser > 0)
    {
        //申请、撤回转给主持人；拒绝、被主持人停止转给当事人
        pushToOne(result.m_notifyUser, cmd_screenshare_sharing_notify,
                  ScreenShareProtocol::buildSharingNotify(roomId, action, userId, nickname));
    }
    //正在共享的人变了：经 1725 事件 13 广播；申请、撤回、拒绝也广播一次，房间里的人据此刷新名单上的「申请中」
    broadcastState(screenshare_event_sharing_changed, userId, result.m_room, result.m_subject);
    LOGI("screenshare sharing, room: %s, user: %lld, action: %d, target: %lld, subject: %lld, notify: %lld, "
         "publish: %lld, changed: %d",
         roomId.c_str(), (long long)userId, action, (long long)target, (long long)result.m_subject,
         (long long)result.m_notifyUser, (long long)result.m_publishUser, result.m_changed ? 1 : 0);
}

//移交主持人（第 22 期，1739）：原主持人离开房间，其余成员经 1725 事件 14 得知新主持人
void handleTransferHost(int64_t userId, const std::string& body, std::vector<ScreenShareReply>& outReplies)
{
    std::string roomId;
    int64_t target = 0;
    if (!ScreenShareProtocol::parseTransferHostRequest(body, roomId, target))
    {
        outReplies.push_back(resultReply(cmd_screenshare_transfer_host, error_code_bad_request, ""));
        return;
    }
    //新主持人的昵称：房间里记的主持人昵称随之换掉；查不到资料时留空，客户端按 userid 取名字
    std::string targetName;
    User targetUser;
    if (Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(target), targetUser))
        targetName = targetUser.nickname;
    ScreenShareRoom room;
    const int code = Singleton<ScreenShareManager>::Instance().transferHost(roomId, userId, target, targetName, room);
    outReplies.push_back(resultReply(cmd_screenshare_transfer_host, code, roomId));
    if (code != error_code_ok)
    {
        LOGI("screenshare transfer host rejected, room: %s, user: %lld, target: %lld, code: %d", roomId.c_str(),
             (long long)userId, (long long)target, code);
        return;
    }
    broadcastState(screenshare_event_host_changed, userId, room, target);
    //1725 只发给房间成员：再经 1734 告诉全体会话成员新主持人是谁，不在房间里的人据此更新横幅。
    //不改写卡片，卡片上的发起人始终是最初的发起人
    const int notified = pushInfoNotify(room, room.m_title, room.m_desc, 0, std::string(), target, targetName);
    LOGI("screenshare host transferred, room: %s, from: %lld, to: %lld, info notified: %d", roomId.c_str(),
         (long long)userId, (long long)target, notified);
}

} // namespace

void ScreenShareService::handleRequest(uint16_t cmd, int64_t userId, const std::string& nickname,
                                       const std::string& body, std::vector<ScreenShareReply>& outReplies,
                                       IScreenShareSensitiveChecker* checker, IScreenShareP2PLinker* p2pLinker)
{
    switch (cmd)
    {
    //发起人发起共享：建房间、回中转参数、通知会话里其他人并写共享卡片
    case cmd_screenshare_start:
        handleStart(userId, nickname, body, outReplies, checker);
        break;

    //观看者加入：校验资格后分配座位、回中转参数、广播成员加入
    case cmd_screenshare_join:
        handleJoin(userId, body, outReplies, p2pLinker);
        break;

    //成员离开；发起人离开等同结束
    case cmd_screenshare_leave:
        handleLeave(userId, body, outReplies);
        break;

    //发起人结束共享
    case cmd_screenshare_stop:
        handleStop(userId, body, outReplies);
        break;

    //查询某会话有无进行中的共享
    case cmd_screenshare_query:
        handleQuery(userId, body, outReplies);
        break;

    //成员上报麦克风、摄像头、电脑声音开关
    case cmd_screenshare_member_media:
        handleMemberMedia(userId, body, outReplies);
        break;

    //发起人设置标注 / 录制权限，或全员静音
    case cmd_screenshare_set_permission:
        handleSetPermission(userId, body, outReplies);
        break;

    //成员开始 / 停止录制
    case cmd_screenshare_record_state:
        handleRecordState(userId, body, outReplies);
        break;

    //发起人移出某个观看者，可选是否允许再次加入（第 14 期）
    case cmd_screenshare_remove_member:
        handleRemoveMember(userId, body, outReplies);
        break;

    //发起人修改共享标题与描述（第 15 期）：过敏感词，改写卡片并推 1734
    case cmd_screenshare_set_info:
        handleSetInfo(userId, body, outReplies, checker);
        break;

    //控制键鼠的申请、撤回、授予、拒绝、收回（第 18 期）
    case cmd_screenshare_control:
        handleControl(userId, nickname, body, outReplies);
        break;

    //多人共享：申请、撤回、同意、拒绝、停止共享屏幕（第 22 期）
    case cmd_screenshare_sharing:
        handleSharing(userId, nickname, body, outReplies);
        break;

    //移交主持人（第 22 期）
    case cmd_screenshare_transfer_host:
        handleTransferHost(userId, body, outReplies);
        break;

    //分享链接：主持人取得、重置、停用或设置免输密码（第 25 期）
    case cmd_screenshare_link:
        handleLink(userId, body, outReplies);
        break;

    //云端录制的开始 / 暂停 / 继续 / 停止（第 26 期）
    case cmd_cloudrecord_control:
        handleCloudRecordControl(userId, body, outReplies);
        break;

    //凭分享链接码查共享概要（第 25 期）
    case cmd_screenshare_link_resolve:
        handleLinkResolve(userId, body, outReplies);
        break;

    //不是屏幕共享的请求命令（调用方分发有误）：回一条参数错误
    default:
        outReplies.push_back(resultReply(cmd, error_code_bad_request, ""));
        LOGE("screenshare: unexpected cmd %u", (unsigned)cmd);
        break;
    }
}

bool ScreenShareService::isUserInShare(int64_t userId)
{
    return Singleton<ScreenShareManager>::Instance().isUserInShare(userId);
}

void ScreenShareService::onUserOffline(int64_t userId)
{
    ScreenShareRoom room;
    const int result = Singleton<ScreenShareManager>::Instance().onUserOffline(userId, nowMs(), room);
    switch (result)
    {
    //观看者掉线：已按离开移出房间，通知其余成员
    case screenshare_offline_member_left:
        broadcastState(screenshare_event_member_left, userId, room);
        LOGI("screenshare member offline, room: %s, user: %lld", room.m_roomId.c_str(), (long long)userId);
        break;

    //发起人掉线：房间保留等宽限期，通知成员发起人离线
    case screenshare_offline_host_away:
        broadcastState(screenshare_event_host_offline, userId, room);
        LOGI("screenshare host offline, room: %s, host: %lld", room.m_roomId.c_str(), (long long)userId);
        break;

    //此人不在任何房间里
    default:
        break;
    }
}

void ScreenShareService::onUserLogin(int64_t userId, std::vector<ScreenShareReply>& outReplies)
{
    ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
    ScreenShareRoom room;
    if (mgr.onUserOnline(userId, room))
    {
        broadcastState(screenshare_event_host_back, userId, room);
        appendRelayInfo(room.m_roomId, userId, outReplies);
        LOGI("screenshare host back, room: %s, host: %lld", room.m_roomId.c_str(), (long long)userId);
        return;
    }
    //强制模式下掉线的观看者重新登录（第 13 期）：推一条带 rejoin 的开始推送，客户端据此直接回到观看
    if (mgr.findForcedRejoin(userId, room))
    {
        outReplies.push_back(ScreenShareReply(
            cmd_screenshare_started_notify,
            ScreenShareProtocol::buildStartedNotify(room.m_roomId, room.m_ctype,
                                                    ScreenShareRules::conversationIdFor(room, userId), room.m_hostId,
                                                    room.m_hostName, room.m_startTime, !room.m_password.empty(), true,
                                                    true, room.m_title, room.m_desc)));
        LOGI("screenshare forced viewer back, room: %s, user: %lld", room.m_roomId.c_str(), (long long)userId);
    }
}

void ScreenShareService::onCloudRecordEnded(const std::string& roomId, int64_t recordId, int code)
{
    ScreenShareRoom room;
    int64_t ended = 0;
    if (Singleton<ScreenShareManager>::Instance().endCloudRecord(roomId, 0, recordId, code, ended, room) != error_code_ok)
        return;
    broadcastState(screenshare_event_cloudrecord_changed, 0, room);
    LOGI("screenshare cloud record ended by server, room: %s, record: %lld, code: %d", roomId.c_str(),
         (long long)recordId, code);
}

bool ScreenShareService::isCloudRecording(const std::string& roomId, int64_t recordId)
{
    ScreenShareRoom room;
    return Singleton<ScreenShareManager>::Instance().getRoom(roomId, room) && room.m_cloudRecordId == recordId;
}

void ScreenShareService::reapExpiredHosts()
{
    const int64_t now = nowMs();
    std::vector<ScreenShareRoom> expired;
    Singleton<ScreenShareManager>::Instance().reapExpiredHosts(now, expired);
    for (size_t i = 0; i < expired.size(); ++i)
        finishRoom(expired[i], screenshare_end_host_timeout);
    //分享链接（第 25 期）：清除共享结束已超过保留期的链接码与过期的频率计数
    Singleton<ScreenShareLinkTable>::Instance().reap(now);
}
