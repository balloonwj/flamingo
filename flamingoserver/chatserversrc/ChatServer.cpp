/**
 *  服务器主服务类，IMServer.cpp
 *  zhangyl 2017.03.09
 **/
#include "ChatServer.h"

#include <set>
#include <time.h>
#include "../net/InetAddress.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "ChatSession.h"
#include "Protocol.h"
#include "LoginProtocol.h"
#include "ProfileProtocol.h"        // buildPresenceChangedNotify（按企业广播离线通知用）
#include "DepartmentMsg.h"
#include "DepartmentManager.h"
#include "EmployeeManager.h"
#include "GroupProtocol.h"
#include "GroupManager.h"
//平台公告推送：报文编解码 + 受众匹配（消息盒子，见 docs/消息盒子接入方案.md）。
#include "AnnouncementProtocol.h"
#include "AnnouncementManager.h"

ChatServer::ChatServer()
{
    m_logPackageBinary = false;
}

bool ChatServer::init(const char* ip, short port, EventLoop* loop, int ioThreadCount)
{   
    InetAddress addr(ip, port);
    m_server.reset(new TcpServer(loop, addr, "FLAMINGO-SERVER", TcpServer::kReusePort));
    m_server->setConnectionCallback(std::bind(&ChatServer::onConnected, this, std::placeholders::_1));
    //启动侦听。线程数由调用方从配置项 iothreadnum 取得，非法取值已在那里回落成
    //kDefaultIoThreadCount，这里直接使用
    m_server->start(ioThreadCount);

    return true;
}

void ChatServer::uninit()
{
    if (m_server)
        m_server->stop();
}

void ChatServer::enableLogPackageBinary(bool enable)
{
    m_logPackageBinary = enable;
}

bool ChatServer::isLogPackageBinaryEnabled()
{
    return m_logPackageBinary;
}

void ChatServer::onConnected(std::shared_ptr<TcpConnection> conn)
{
    if (conn->connected())
    {
        LOGD("client connected: %s", conn->peerAddress().toIpPort().c_str());
        ++m_sessionId;
        std::shared_ptr<ChatSession> spSession(new ChatSession(conn, m_sessionId));
        conn->setMessageCallback(std::bind(&ChatSession::onRead, spSession.get(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));       

        std::lock_guard<std::mutex> guard(m_sessionMutex);
        m_sessions.push_back(spSession);
    }
    else
    {
        onDisconnected(conn);
    }
}

void ChatServer::onDisconnected(const std::shared_ptr<TcpConnection>& conn)
{
    //是否有用户下线
    //bool bUserOffline = false;

    //本次正常下线的用户（仅当命中有效会话时被赋值），供<u>解锁后</u>按企业广播其离线用。
    //放到锁外广播是因为 broadcastToEnterprise 内部会再取会话表快照（再次加 m_sessionMutex），
    //而 m_sessionMutex 是非递归锁，在持锁期间调用会自我死锁。
    int32_t disconnectedUserId = 0;
    int32_t disconnectedClientType = 0;

    //本次断开的那个会话对象。文件夹传输的收尾（向对端推 cmd_folder_state_notify 并销毁
    //会话）同样要在<u>解锁之后</u>做——它内部经 ChatSession::sendToUser 遍历在线会话表，
    //会再取一次 m_sessionMutex，持锁期间调用即自我死锁。故先在锁内留一份 shared_ptr，
    //把会话对象的存活期延到临界区之外，出锁后再收尾。
    //顺带的效果是 ChatSession 的析构也移出了临界区（此前它随 m_sessions.erase 在锁内发生）。
    std::shared_ptr<ChatSession> offlineSession;

    //TODO: 这样的代码逻辑太混乱，需要优化
    {
        std::lock_guard<std::mutex> guard(m_sessionMutex);
        for (auto iter = m_sessions.begin(); iter != m_sessions.end(); ++iter)
        {
            if ((*iter)->getConnectionPtr() == NULL)
            {
                LOGE("connection is NULL");
                break;
            }

            //通过比对connection对象找到对应的session
            if ((*iter)->getConnectionPtr() == conn)
            {
                //留一份指针供解锁后收尾（文件夹传输作废等），理由见上面的声明处
                offlineSession = *iter;

                //该Session不是之前被踢下线的有效Session，才认为是正常下线，才广播其下线消息
                if ((*iter)->isSessionValid())
                {
                    int32_t offlineUserId = (*iter)->getUserId();
                    //记下供解锁后按企业广播离线（同企业同事的状态点据此变灰）。
                    disconnectedUserId     = offlineUserId;
                    disconnectedClientType = (*iter)->getClientType();

                    //向该账号的其它在线设备推送本设备掉线通知（cmd_device_changed_notify）。
                    //本函数已持有 m_sessionMutex，故此处直接遍历 m_sessions，不另调 broadcastDeviceChanged
                    if (offlineUserId != 0)
                    {
                        int32_t offlineClientType = (*iter)->getClientType();
                        int32_t offlineSessionId  = (*iter)->getSessionId();
                        std::string devBody = LoginProtocol::buildDeviceChangedNotify(
                            device_change_offline, offlineClientType);
                        for (auto& iter3 : m_sessions)
                        {
                            if (iter3->getUserId() == offlineUserId &&
                                iter3->getSessionId() != offlineSessionId)
                            {
                                iter3->send(cmd_device_changed_notify, 0, devBody);
                            }
                        }
                    }
                }
                else
                {
                    LOGI("Session is invalid, userid=%d", (*iter)->getUserId());
                }

                //停掉该Session的掉线检测
                //(*iter)->DisableHeartbaetCheck();
                //用户下线
                m_sessions.erase(iter);
                //bUserOffline = true;
                LOGI("client disconnected: %s", conn->peerAddress().toIpPort().c_str());
                break;
            }
        }

        LOGI("current online user count: %d", (int)m_sessions.size());
    }

    //解锁后：作废本连接名下正在进行的单聊文件夹传输，并向仍在线的对端推一帧状态变更。
    //这一步对被顶号踢下线的旧会话同样要做（它名下那笔传输的续传位点随连接一并消失），
    //故不受上面 isSessionValid 判定的约束，与企业离线广播分开处理。
    if (offlineSession)
    {
        offlineSession->cleanupFolderTransferOnOffline();
    }

    //解锁后：向同企业的在线同事广播本用户离线（presence=0），让其联系人树/会话列表上该用户
    //的状态点实时变灰。与登录上线广播对称。无企业归属（如平台超管）时跳过 —— 企业版里
    //它没有联系人可推。
    //clienttype 取会话里记下的 disconnectedClientType，而不是回头去会话表里查 —— 此刻该
    //会话已从 m_sessions 摘除，查也查不到。
    if (disconnectedUserId != 0)
    {
        int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(disconnectedUserId);
        if (enterpriseId != 0)
        {
            std::string offlineBody = ProfileProtocol::buildPresenceChangedNotify(
                disconnectedUserId, 0, disconnectedClientType);
            broadcastToEnterprise(enterpriseId, cmd_presence_changed_notify, offlineBody, disconnectedUserId);
        }
    }
}

void ChatServer::getSessions(std::list<std::shared_ptr<ChatSession>>& sessions)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    sessions = m_sessions;
}

bool ChatServer::getSessionByUserIdAndClientType(std::shared_ptr<ChatSession>& session, int32_t userid, int32_t clientType)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    std::shared_ptr<ChatSession> tmpSession;
    for (const auto& iter : m_sessions)
    {
        tmpSession = iter;
        if (iter->getUserId() == userid && iter->getClientType() == clientType)
        {
            session = tmpSession;
            return true;
        }
    }

    return false;
}

bool ChatServer::getSessionsByUserId(std::list<std::shared_ptr<ChatSession>>& sessions, int32_t userid)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    std::shared_ptr<ChatSession> tmpSession;
    for (const auto& iter : m_sessions)
    {
        tmpSession = iter;
        if (iter->getUserId() == userid)
        {
            sessions.push_back(tmpSession);
            return true;
        }
    }

    return false;
}

void ChatServer::broadcastOrgStructureChanged(int64_t enterpriseId, int32_t changetype, int64_t deptid)
{
    //企业ID 为 0（如平台超管未指定企业）时无法定位推送范围，直接跳过
    if (enterpriseId == 0)
        return;

    std::string body = dept::BuildOrgStructureNotify(changetype, deptid);

    //拷贝一份在线会话快照后再推送，避免持锁期间调用 send
    std::list<std::shared_ptr<ChatSession>> sessions;
    getSessions(sessions);

    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    int pushed = 0;
    for (auto& s : sessions)
    {
        if (!s || s->getUserId() == 0)
            continue;
        //只推给属于该企业的在线用户
        if (deptMgr.getUserEnterpriseId(s->getUserId()) == enterpriseId)
        {
            s->send(cmd_org_structure_notify, 0, body);
            ++pushed;
        }
    }

    LOGI("broadcast org structure changed, enterpriseid: %lld, changetype: %d, deptid: %lld, pushed sessions: %d",
         (long long)enterpriseId, changetype, (long long)deptid, pushed);
}

int32_t ChatServer::kickEnterpriseSessions(int64_t enterpriseId, int32_t reason)
{
    //企业ID 为 0 是「无企业归属」的哨兵值，不是真实企业。真按它踢会把所有尚未
    //归属企业的用户一并踢下线，故直接挡掉。
    if (enterpriseId == 0)
    {
        LOGE("kick enterprise sessions ignored: enterpriseid is 0");
        return 0;
    }

    //踢下线通知包体。reason 让客户端能分辨"被服务器踢"与"网络断了"：前者要停掉
    //自动重连、弹提示框、退回登录窗，后者要重连。msg 是面向开发者的英文诊断串，
    //客户端展示给用户的文案由它自己按 reason 选（见客户端 KickoutNotice.cpp）。
    const char* diagMsg = (reason == kickout_reason_enterprise_deleted)
                              ? "enterprise deleted"
                              : "enterprise disabled";
    char kickoutBody[128] = { 0 };
    snprintf(kickoutBody, sizeof(kickoutBody),
             "{\"reason\":%d,\"msg\":\"%s\"}", reason, diagMsg);

    //拷贝一份在线会话快照后再踢，避免持锁期间发包
    std::list<std::shared_ptr<ChatSession>> sessions;
    getSessions(sessions);

    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    int32_t kicked = 0;
    for (auto& s : sessions)
    {
        if (!s || s->getUserId() == 0)
            continue;
        if (deptMgr.getUserEnterpriseId(s->getUserId()) != enterpriseId)
            continue;

        //与顶号踢人（onLogin 里踢同类型设备的旧会话）完全一致：先把 cmd_kickout
        //发出去，再把会话置为失效，<u>不</u>主动 forceClose 底层连接 —— 强制关会
        //把还在发送缓冲区里的这条通知一起丢掉，客户端就只看到一次莫名其妙的断线、
        //转而走自动重连，而不是弹出下线提示并退回登录窗。连接由客户端收到通知后
        //自行断开（其 GoOffline 会关掉 chat/file/img 三条连接）。
        //被踢下线为服务器主动推送，seq 填 0。
        s->send(cmd_kickout, 0, std::string(kickoutBody));
        s->makeSessionInvalid();
        ++kicked;
    }

    LOGI("kick enterprise sessions, enterpriseid: %lld, reason: %d, kicked sessions: %d",
         (long long)enterpriseId, reason, kicked);
    return kicked;
}

int32_t ChatServer::kickUserSessions(int64_t userId, int32_t clientTypeFilter, int32_t reason)
{
    if (userId == 0)
    {
        LOGE("kick user sessions ignored: userid is 0");
        return 0;
    }

    //踢下线通知包体。msg 是面向开发者的英文诊断串，客户端展示给用户的文案由它
    //自己按 reason 选（见客户端 KickoutNotice.cpp）。
    const char* diagMsg = "kicked by admin";
    if (reason == kickout_reason_disabled)
        diagMsg = "account disabled";
    else if (reason == kickout_reason_account_deleted)
        diagMsg = "account deleted";

    char kickoutBody[128] = { 0 };
    snprintf(kickoutBody, sizeof(kickoutBody),
             "{\"reason\":%d,\"msg\":\"%s\"}", reason, diagMsg);

    std::list<std::shared_ptr<ChatSession>> sessions;
    getSessionsByUserId(sessions, static_cast<int32_t>(userId));

    int32_t kicked = 0;
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin();
         it != sessions.end(); ++it)
    {
        std::shared_ptr<ChatSession>& s = *it;
        if (!s)
            continue;
        if (clientTypeFilter > 0 && s->getClientType() != clientTypeFilter)
            continue;

        //先发通知、再置失效，不 forceClose —— 理由见本方法在头文件里的说明。
        //被踢下线为服务器主动推送，seq 填 0。
        s->send(cmd_kickout, 0, std::string(kickoutBody));
        s->makeSessionInvalid();
        ++kicked;
    }

    LOGI("kick user sessions, userid: %lld, clienttype filter: %d, reason: %d, kicked sessions: %d",
         (long long)userId, clientTypeFilter, reason, kicked);
    return kicked;
}

void ChatServer::broadcastToEnterprise(int64_t enterpriseId, uint16_t cmd,
                                       const std::string& body, int32_t excludeUserId)
{
    //企业ID 为 0（如平台超管未指定企业）时无法定位推送范围，直接跳过
    if (enterpriseId == 0)
        return;

    //拷贝一份在线会话快照后再推送，避免持锁期间调用 send
    std::list<std::shared_ptr<ChatSession>> sessions;
    getSessions(sessions);

    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    int pushed = 0;
    for (auto& s : sessions)
    {
        if (!s || s->getUserId() == 0)
            continue;
        //跳过发起者自己：其客户端联系人树不含自己，收到也无意义
        if (excludeUserId != 0 && s->getUserId() == excludeUserId)
            continue;
        //只推给属于该企业的在线用户
        if (deptMgr.getUserEnterpriseId(s->getUserId()) == enterpriseId)
        {
            s->send(cmd, 0, body);
            ++pushed;
        }
    }

    LOGI("broadcast to enterprise, enterpriseid: %lld, cmd: %u, exclude userid: %d, pushed sessions: %d",
         (long long)enterpriseId, (unsigned int)cmd, excludeUserId, pushed);
}

void ChatServer::pushToUsers(const std::vector<int64_t>& userids, int32_t msgType, const std::string& body)
{
    //名单为空时无需推送
    if (userids.empty())
        return;

    //目标 userid 集合，便于 O(logN) 判定某会话是否在推送名单内
    std::set<int64_t> targets(userids.begin(), userids.end());

    //拷贝一份在线会话快照后再推送，避免持锁期间调用 send
    std::list<std::shared_ptr<ChatSession>> sessions;
    getSessions(sessions);

    int pushed = 0;
    for (auto& s : sessions)
    {
        if (!s || s->getUserId() == 0)
            continue;
        if (targets.count(static_cast<int64_t>(s->getUserId())) > 0)
        {
            s->send(msgType, 0, body);
            ++pushed;
        }
    }

    LOGI("push to users, target count: %u, msgtype: %d, pushed sessions: %d",
         static_cast<unsigned>(targets.size()), msgType, pushed);
}

//读回最新群资料，向该群全体成员推送 cmd_group_info_changed_notify（资料变更）
static void pushGroupInfoUpdated(ChatServer& server, int64_t groupid)
{
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord updated;
    if (!groupMgr.getGroup(groupid, updated))
        return;

    std::vector<GroupMemberRecord> members;
    groupMgr.getGroupMembers(groupid, members);
    std::vector<int64_t> userids;
    for (size_t i = 0; i < members.size(); ++i)
        userids.push_back(members[i].userid);

    server.pushToUsers(userids, cmd_group_info_changed_notify,
                       GroupProtocol::buildGroupInfoChangedNotify(updated, group_info_change_updated));
}

bool ChatServer::refreshAutoGroupNameAndNotify(int64_t groupid)
{
    std::string newName;
    if (!Singleton<GroupManager>::Instance().refreshAutoGroupName(groupid, newName))
        return false;

    pushGroupInfoUpdated(*this, groupid);
    return true;
}

void ChatServer::refreshAutoGroupNamesOfMemberAndNotify(int32_t userid)
{
    std::vector<std::pair<int64_t, std::string> > changes;
    if (!Singleton<GroupManager>::Instance().refreshAutoGroupNamesOfMember(userid, changes))
        return;

    for (size_t i = 0; i < changes.size(); ++i)
        pushGroupInfoUpdated(*this, changes[i].first);
}

void ChatServer::collectOnlineUserIds(std::vector<int32_t>& outUserIds)
{
    outUserIds.clear();

    std::list<std::shared_ptr<ChatSession>> sessions;
    getSessions(sessions);

    //同一账号在多台设备登录会有多个会话，用 set 去重——受众筛选按用户算，不按连接算。
    std::set<int32_t> unique;
    for (const std::shared_ptr<ChatSession>& s : sessions)
    {
        //userid 为 0 表示这条连接还没走完登录，不参与受众筛选。
        if (s && s->getUserId() != 0)
            unique.insert(s->getUserId());
    }

    outUserIds.assign(unique.begin(), unique.end());
}

void ChatServer::broadcastAnnouncementPublished(int64_t announcementId)
{
    if (announcementId <= 0)
        return;

    //取公告内容用于组包。取不到（比如刚发完就被删了）就别推了。
    AnnouncementInfo info;
    if (!Singleton<AnnouncementManager>::Instance().getAnnouncement(announcementId, info))
    {
        LOGE("broadcast announcement failed: announcement not found, announcementid: %lld",
             (long long)announcementId);
        return;
    }

    std::vector<int32_t> onlineUsers;
    collectOnlineUserIds(onlineUsers);
    if (onlineUsers.empty())
    {
        LOGI("broadcast announcement skipped: nobody online, announcementid: %lld",
             (long long)announcementId);
        return;
    }

    //从在线用户里筛出这条公告的受众。受众类型的判定、以及「平台内部公告不投」都在
    //filterAudienceAmong 内部完成，这里不重复判断。
    std::vector<int32_t> hits;
    Singleton<AnnouncementManager>::Instance().filterAudienceAmong(
        announcementId, onlineUsers, hits);

    //记下在线数与命中数：受众为「全部企业」时这一遍筛选的开销随在线人数增长，
    //日志里留着这两个数字，便于日后判断要不要优化。
    LOGI("broadcast announcement, announcementid: %lld, online: %u, matched: %u",
         (long long)announcementId, static_cast<unsigned>(onlineUsers.size()),
         static_cast<unsigned>(hits.size()));

    if (hits.empty())
        return;

    AnnouncementPushItem item;
    item.announcementId = info.announcementId;
    item.title          = info.title;
    item.body           = info.body;
    item.severity       = info.severity;
    item.senderName     = info.senderName;
    item.pinned         = info.pinned;
    item.sentTime       = AnnouncementProtocol::dateTimeToMs(info.sentTime);
    //刚发布的公告，收到推送的人必然还没读过。
    item.read           = 0;

    std::string notifyBody = AnnouncementProtocol::buildAnnouncementNotify(
        announcement_change_published, item);

    std::vector<int64_t> targets(hits.begin(), hits.end());
    pushToUsers(targets, cmd_announcement_notify, notifyBody);
}

int32_t ChatServer::recallAnnouncementAndNotify(int64_t announcementId)
{
    //与 AdminApiCodes.h 的 error_code_unknown 同值。本文件属 Protocol.h 世界、引不进 AdminApiCodes.h
    //（两者的 error_code 枚举有冲突），而返回值要交给 HttpSession 按 AdminApiCodes.h 的语义
    //解释后回给管理控制台，故在此显式对齐取值。
    const int32_t ANNOUNCEMENT_ERR_UNKNOWN = 1;

    if (announcementId <= 0)
        return ANNOUNCEMENT_ERR_UNKNOWN;

    //先筛受众，再撤回 —— 顺序不能反。受众筛选只认「已发布」的公告，而撤回会把状态
    //改成「已撤回」，先撤后筛的话一个受众都筛不到，客户端那边就永远收不到撤回通知、
    //消息盒子里留着一条已经作废的公告。
    std::vector<int32_t> onlineUsers;
    collectOnlineUserIds(onlineUsers);

    std::vector<int32_t> hits;
    if (!onlineUsers.empty())
    {
        Singleton<AnnouncementManager>::Instance().filterAudienceAmong(
            announcementId, onlineUsers, hits);
    }

    int32_t code = Singleton<AnnouncementManager>::Instance().recallAnnouncement(announcementId);
    if (code != error_code_ok)
    {
        //撤回本身没成功就什么都不推，客户端保持原样。
        LOGE("recall announcement failed, announcementid: %lld, code: %d",
             (long long)announcementId, code);
        return code;
    }

    LOGI("recall announcement ok, announcementid: %lld, online: %u, matched: %u",
         (long long)announcementId, static_cast<unsigned>(onlineUsers.size()),
         static_cast<unsigned>(hits.size()));

    if (!hits.empty())
    {
        //撤回通知只带公告ID，其余字段 buildAnnouncementNotify 不会编进去。
        AnnouncementPushItem item;
        item.announcementId = announcementId;

        std::string notifyBody = AnnouncementProtocol::buildAnnouncementNotify(
            announcement_change_recalled, item);

        std::vector<int64_t> targets(hits.begin(), hits.end());
        pushToUsers(targets, cmd_announcement_notify, notifyBody);
    }

    return error_code_ok;
}

AdminCreateGroupOutcome ChatServer::adminCreateGroup(int64_t enterpriseId, const std::string& jsonBody,
                                                     int64_t& outGroupId)
{
    outGroupId = 0;

    //1) 解析建群包体（群名 / 群主 / 管理员 / 普通成员）
    AdminCreateGroupRequest req;
    if (!GroupProtocol::parseAdminCreateGroupRequest(jsonBody, req))
        return AdminCreateGroupOutcome::BadParams;

    //2) 校验并规整角色：群主必填、管理员须为已选成员、各列表去重且互斥
    std::vector<int64_t> adminids;
    std::vector<int64_t> memberids;
    if (!GroupProtocol::validateAdminCreateGroup(req, adminids, memberids))
        return AdminCreateGroupOutcome::BadParams;

    //3) 逐一校验群主 / 管理员 / 普通成员都属于目标企业，杜绝跨租户拉人
    EmployeeManager& empMgr = Singleton<EmployeeManager>::Instance();
    if (empMgr.getEmployeeEnterpriseId(req.ownerid) != enterpriseId)
        return AdminCreateGroupOutcome::OwnerNotInEnterprise;
    for (size_t i = 0; i < adminids.size(); ++i)
    {
        if (empMgr.getEmployeeEnterpriseId(adminids[i]) != enterpriseId)
            return AdminCreateGroupOutcome::AdminNotInEnterprise;
    }
    for (size_t i = 0; i < memberids.size(); ++i)
    {
        if (empMgr.getEmployeeEnterpriseId(memberids[i]) != enterpriseId)
            return AdminCreateGroupOutcome::MemberNotInEnterprise;
    }

    //4) 落库建群
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    int64_t groupid = groupMgr.createGroup(req.name, static_cast<int32_t>(req.ownerid),
                                           adminids, memberids, now);
    if (groupid == 0)
        return AdminCreateGroupOutcome::DbFail;

    //5) 给群主 + 管理员 + 普通成员中在线者推送"成员加入"通知，使其立即看到新群
    std::vector<int64_t> roster;
    roster.push_back(req.ownerid);
    for (size_t i = 0; i < adminids.size(); ++i)
        roster.push_back(adminids[i]);
    for (size_t i = 0; i < memberids.size(); ++i)
        roster.push_back(memberids[i]);
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        groupid, group_member_change_join, roster, req.ownerid);
    pushToUsers(roster, cmd_group_member_changed_notify, notify);

    outGroupId = groupid;
    LOGI("admin create group ok, groupid: %lld, enterpriseid: %lld, owner: %lld, admins: %u, members: %u",
         (long long)groupid, (long long)enterpriseId, (long long)req.ownerid,
         static_cast<unsigned>(adminids.size()), static_cast<unsigned>(memberids.size()));
    return AdminCreateGroupOutcome::Ok;
}

void ChatServer::broadcastDeviceChanged(int32_t userid, int32_t changetype,
                                        int32_t clienttype, int32_t excludeSessionId)
{
    if (userid == 0)
        return;

    std::string body = LoginProtocol::buildDeviceChangedNotify(changetype, clienttype);

    //拷贝一份在线会话快照后再推送，避免持锁期间调用 send
    std::list<std::shared_ptr<ChatSession>> sessions;
    getSessions(sessions);

    for (auto& s : sessions)
    {
        if (!s || s->getUserId() != userid)
            continue;
        //不推给引发本次变更的会话本身
        if (s->getSessionId() == excludeSessionId)
            continue;
        s->send(cmd_device_changed_notify, 0, body);
    }
}

int32_t ChatServer::getUserStatusByUserId(int32_t userid)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    for (const auto& iter : m_sessions)
    {
        if (iter->getUserId() == userid)
        {
            return iter->getUserStatus();
        }
    }

    return 0;
}

int32_t ChatServer::getUserClientTypeByUserId(int32_t userid)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    bool bMobileOnline = false;
    int clientType = CLIENT_TYPE_UNKOWN;
    for (const auto& iter : m_sessions)
    {
        if (iter->getUserId() == userid)
        {   
            clientType = iter->getUserClientType();
            //电脑在线直接返回电脑在线状态
            if (clientType == CLIENT_TYPE_PC)
                return clientType;
            else if (clientType == CLIENT_TYPE_ANDROID || clientType == CLIENT_TYPE_IOS)
                bMobileOnline = true;
        }
    }

    //只有手机在线才返回手机在线状态
    if (bMobileOnline)
        return clientType;

    return CLIENT_TYPE_UNKOWN;
}