/**
 *  群通话处理流程的线上环境实现，GroupCallServerEnv.cpp
 *  职责与调用约束见 GroupCallServerEnv.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupCallServerEnv.h"

#include <list>
#include <memory>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "ChatServer.h"
#include "ChatSession.h"
#include "CallManager.h"
#include "DepartmentManager.h"
#include "FeatureSwitchManager.h"
#include "GroupManager.h"
#include "GroupProtocol.h"
#include "MessageManager.h"
#include "MessageProtocol.h"
#include "ScreenShareManager.h"

GroupCallServerEnv::GroupCallServerEnv()
{
}

GroupCallServerEnv::~GroupCallServerEnv()
{
}

bool GroupCallServerEnv::isGroupMember(int64_t groupId, int64_t userId)
{
    return Singleton<GroupManager>::Instance().isGroupMember(groupId, static_cast<int32_t>(userId));
}

std::vector<int64_t> GroupCallServerEnv::groupMemberIds(int64_t groupId)
{
    std::vector<GroupMemberRecord> members;
    Singleton<GroupManager>::Instance().getGroupMembers(groupId, members);
    std::vector<int64_t> ids;
    ids.reserve(members.size());
    for (size_t i = 0; i < members.size(); ++i)
        ids.push_back(members[i].userid);
    return ids;
}

std::string GroupCallServerEnv::groupName(int64_t groupId)
{
    GroupRecord rec;
    if (!Singleton<GroupManager>::Instance().getGroup(groupId, rec))
        return std::string();
    return rec.name;
}

int GroupCallServerEnv::groupRole(int64_t groupId, int64_t userId)
{
    return Singleton<GroupManager>::Instance().getMemberRole(groupId, static_cast<int32_t>(userId));
}

bool GroupCallServerEnv::isOnline(int64_t userId)
{
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(sessions, static_cast<int32_t>(userId));
    return !sessions.empty();
}

bool GroupCallServerEnv::isBusyOutsideGroupCall(int64_t userId)
{
    return Singleton<CallManager>::Instance().isUserBusy(userId) ||
           Singleton<ScreenShareManager>::Instance().isUserInShare(userId);
}

bool GroupCallServerEnv::isFeatureEnabled(int64_t userId)
{
    const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(userId);
    return Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, kFeatureGroupCall);
}

int64_t GroupCallServerEnv::saveCard(int64_t groupId, int64_t fromId, const std::string& content,
                                     const std::string& clientMsgId, int64_t now,
                                     const std::vector<int64_t>& receivers)
{
    //服务器生成的卡片不带 @ 与引用；落库时与其它消息一样套一层 buildStoredContent 的外壳
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_group, static_cast<int32_t>(fromId), static_cast<int32_t>(groupId),
        message_type_rtc, stored, now);
    if (msgId == 0)
    {
        LOGE("groupcall: save card failed, group: %lld, from: %lld", (long long)groupId, (long long)fromId);
        return 0;
    }

    MessageRecord rec;
    rec.msgid = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype = conversation_group;
    rec.from = fromId;
    rec.to = groupId;
    rec.msgtype = message_type_rtc;
    rec.content = content;
    rec.mentionall = false;
    rec.timestamp = now;
    rec.recalled = false;
    push(receivers, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, groupId));
    return msgId;
}

bool GroupCallServerEnv::updateCard(int64_t msgId, const std::string& content)
{
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    return Singleton<MessageManager>::Instance().updateMessageContent(msgId, stored);
}

void GroupCallServerEnv::push(const std::vector<int64_t>& users, uint16_t cmd, const std::string& body)
{
    if (users.empty())
        return;
    Singleton<ChatServer>::Instance().pushToUsers(users, cmd, body);
}
