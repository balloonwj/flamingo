/**
 *  云端录制处理流程的线上环境，见 CloudRecordServerEnv.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordServerEnv.h"

#include <time.h>
#include <functional>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "ChatServer.h"
#include "DepartmentManager.h"
#include "FeatureSwitchManager.h"
#include "FileStoreManager.h"
#include "GroupManager.h"
#include "MessageManager.h"
#include "MessageProtocol.h"
#include "Protocol.h"
#include "ScreenShareService.h"

namespace {

//云端录像卡片的 clientmsgid 前缀，后接录像 id、会话与时刻；以 "sys-" 开头，避免与客户端消息的去重键相撞
const char* const kCardClientMsgIdPrefix = "sys-cloudrecord-";
//秒到毫秒的换算系数
const int64_t kMsPerSecond = 1000;

} // namespace

CloudRecordServerEnv::CloudRecordServerEnv()
{
}

CloudRecordServerEnv::~CloudRecordServerEnv()
{
}

int64_t CloudRecordServerEnv::enterpriseOf(int64_t userId)
{
    return Singleton<DepartmentManager>::Instance().getUserEnterpriseId(userId);
}

bool CloudRecordServerEnv::isEnabled(int64_t enterpriseId)
{
    return Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, kFeatureCloudRecord);
}

int64_t CloudRecordServerEnv::quotaGb(int64_t enterpriseId)
{
    return Singleton<FeatureSwitchManager>::Instance().getNumber(enterpriseId, kSettingCloudRecordQuotaGb);
}

int64_t CloudRecordServerEnv::retainDays(int64_t enterpriseId)
{
    return Singleton<FeatureSwitchManager>::Instance().getNumber(enterpriseId, kSettingCloudRecordRetainDays);
}

bool CloudRecordServerEnv::isGroupMember(int64_t groupId, int64_t userId)
{
    return Singleton<GroupManager>::Instance().isGroupMember(groupId, static_cast<int32_t>(userId));
}

void CloudRecordServerEnv::push(int64_t userId, uint16_t cmd, const std::string& body)
{
    std::vector<int64_t> users(1, userId);
    Singleton<ChatServer>::Instance().pushToUsers(users, cmd, body);
}

void CloudRecordServerEnv::onRoomRecordEnded(const std::string& roomId, int64_t recordId, int code)
{
    ScreenShareService::onCloudRecordEnded(roomId, recordId, code);
}

bool CloudRecordServerEnv::isRoomRecording(const std::string& roomId, int64_t recordId)
{
    return ScreenShareService::isCloudRecording(roomId, recordId);
}

int64_t CloudRecordServerEnv::sendCard(int64_t fromId, int ctype, int64_t cid, const std::string& content, int64_t nowMs)
{
    //服务器生成的卡片不带 @ 与引用；落库时与其它消息一样套一层 buildStoredContent 的外壳
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    const std::string clientMsgId = std::string(kCardClientMsgIdPrefix) + std::to_string(static_cast<long long>(fromId)) +
                                    "-" + std::to_string(static_cast<long long>(ctype)) + "-" +
                                    std::to_string(static_cast<long long>(cid)) + "-" +
                                    std::to_string(static_cast<long long>(nowMs));
    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, ctype, static_cast<int32_t>(fromId), static_cast<int32_t>(cid), message_type_cloudrecord, stored,
        nowMs);
    if (msgId == 0)
    {
        LOGE("cloud record: save card failed, from: %lld, ctype: %d, cid: %lld", (long long)fromId, ctype, (long long)cid);
        return 0;
    }

    MessageRecord rec;
    rec.msgid = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype = ctype;
    rec.from = fromId;
    rec.to = cid;
    rec.msgtype = message_type_cloudrecord;
    rec.content = content;
    rec.mentionall = false;
    rec.timestamp = nowMs;
    rec.recalled = false;

    ChatServer& server = Singleton<ChatServer>::Instance();
    if (ctype == conversation_group)
    {
        std::vector<GroupMemberRecord> members;
        Singleton<GroupManager>::Instance().getGroupMembers(cid, members);
        std::vector<int64_t> ids;
        ids.reserve(members.size());
        for (size_t i = 0; i < members.size(); ++i)
            ids.push_back(members[i].userid);
        server.pushToUsers(ids, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, cid));
        return msgId;
    }
    //单聊：每一方看到的会话 id 是「另一个人」；发送者自己的其它设备也要收到
    std::vector<int64_t> sender(1, fromId);
    server.pushToUsers(sender, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, cid));
    std::vector<int64_t> receiver(1, cid);
    server.pushToUsers(receiver, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, fromId));
    return msgId;
}

void CloudRecordServerEnv::imgServerAddr(std::string& host, int& port)
{
    bool tls = false;
    Singleton<FileStoreManager>::Instance().getServerForScene(file_scene_cloud_record, host, port, tls);
}

CloudRecordLinkBridge::CloudRecordLinkBridge(CloudRecordLink& link, net::EventLoop* loop)
    : m_link(link)
    , m_loop(loop)
    , m_service(NULL)
{
}

CloudRecordLinkBridge::~CloudRecordLinkBridge()
{
}

void CloudRecordLinkBridge::setService(CloudRecordService* service)
{
    m_service = service;
}

bool CloudRecordLinkBridge::isUp() const
{
    return m_link.isUp();
}

bool CloudRecordLinkBridge::send(const std::string& body)
{
    return m_link.send(body);
}

void CloudRecordLinkBridge::onCloudRecordLinkUp()
{
    m_loop->runInLoop(std::bind(&CloudRecordLinkBridge::handleUp, this));
}

void CloudRecordLinkBridge::onCloudRecordLinkDown()
{
    //断开期间的 stop 与删除文件由 handleUp 补齐，这里只记日志
    LOGW("cloud record link down, new cloud recordings are unavailable until it reconnects");
}

void CloudRecordLinkBridge::onCloudRecordLinkMessage(const std::string& body)
{
    m_loop->runInLoop(std::bind(&CloudRecordLinkBridge::handleMessage, this, body));
}

void CloudRecordLinkBridge::handleUp()
{
    if (m_service != NULL)
        m_service->onLinkUp();
}

void CloudRecordLinkBridge::handleMessage(const std::string& body)
{
    if (m_service == NULL)
    {
        LOGW("cloud record: message from imgserver dropped, service not configured: %s", body.c_str());
        return;
    }
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMsPerSecond;
    m_service->onCtlMessage(body, nowMs);
}
