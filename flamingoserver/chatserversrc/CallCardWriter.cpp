/**
 *  一对一通话卡片的落库、改写与推送，说明见 CallCardWriter.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "CallCardWriter.h"

#include <vector>

#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "CallManager.h"
#include "CallProtocol.h"
#include "ChatServer.h"
#include "MessageManager.h"
#include "MessageProtocol.h"
#include "Protocol.h"
#include "UserManager.h"

namespace {

//卡片消息的客户端消息 id 前缀：服务端生成的消息没有客户端 id，用前缀加会话 id 占位，保证去重键唯一
const char* const kCardClientMsgIdPrefix = "sys-call-";

//给一个用户推一帧
void pushToOne(int64_t userId, uint16_t cmd, const std::string& body)
{
    std::vector<int64_t> users(1, userId);
    Singleton<ChatServer>::Instance().pushToUsers(users, cmd, body);
}

} // namespace

void CallCardWriter::writeStartCard(const std::string& sessionId, int64_t callerId, const std::string& callerName,
                                    int64_t calleeId, int media, int64_t nowMs)
{
    const std::string content =
        CallProtocol::buildCardContent(sessionId, media, callerId, callerName, calleeId, nowMs);
    //服务器生成的卡片不带 @ 与引用；落库时与其它消息一样套一层 buildStoredContent 的外壳
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    const std::string clientMsgId = std::string(kCardClientMsgIdPrefix) + sessionId;
    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_single, static_cast<int32_t>(callerId), static_cast<int32_t>(calleeId),
        message_type_rtc, stored, nowMs);
    if (msgId == 0)
    {
        LOGE("call card: save failed, session: %s, caller: %lld, callee: %lld", sessionId.c_str(),
             (long long)callerId, (long long)calleeId);
        return;
    }
    Singleton<CallManager>::Instance().setCard(sessionId, msgId, content);

    MessageRecord rec;
    rec.msgid = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype = conversation_single;
    rec.from = callerId;
    rec.to = calleeId;
    rec.msgtype = message_type_rtc;
    rec.content = content;
    rec.mentionall = false;
    rec.timestamp = nowMs;
    rec.recalled = false;
    //双方都要收到（双方的其它在线设备也一样）。两帧的差别只在会话 id：各自视角下都是对方的 userid
    pushToOne(calleeId, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, callerId));
    pushToOne(callerId, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, calleeId));
    LOGI("call card written, session: %s, msgid: %lld, caller: %lld, callee: %lld, media: %d", sessionId.c_str(),
         (long long)msgId, (long long)callerId, (long long)calleeId, media);
}

void CallCardWriter::finishCard(const CallSessionInfo& s, int result, int64_t nowMs)
{
    if (s.cardMsgId <= 0)
    {
        LOGI("call card: no card to finish, session: %s, result: %d", s.sessionid.c_str(), result);
        return;
    }
    //原 content 一般可用；万一不可用需要重建，才去取主叫昵称
    std::string callerName;
    if (CallProtocol::cardMedia(s.cardContent) == 0)
    {
        User caller;
        caller.userid = 0;
        if (Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(s.callerId), caller))
            callerName = caller.nickname;
    }
    const std::string content = CallProtocol::buildEndedCardContent(s.cardContent, s.sessionid, s.media, s.callerId,
                                                                    callerName, s.calleeId, s.createTime,
                                                                    s.connectTime, result, nowMs);
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    if (!Singleton<MessageManager>::Instance().updateMessageContent(s.cardMsgId, stored))
    {
        LOGE("call card: update failed, session: %s, msgid: %lld", s.sessionid.c_str(), (long long)s.cardMsgId);
        return;
    }
    //各自视角下的会话 id 是对方的 userid
    pushToOne(s.callerId, cmd_call_card_notify,
              CallProtocol::buildCardNotify(s.sessionid, s.cardMsgId, s.calleeId, content));
    pushToOne(s.calleeId, cmd_call_card_notify,
              CallProtocol::buildCardNotify(s.sessionid, s.cardMsgId, s.callerId, content));
    LOGI("call card finished, session: %s, msgid: %lld, result: %d, connected: %d", s.sessionid.c_str(),
         (long long)s.cardMsgId, result, s.connectTime > 0 ? 1 : 0);
}
