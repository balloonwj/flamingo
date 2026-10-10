/**
 *  管理控制台长连接服务的实现，AdminLinkServer.cpp
 *  说明见 AdminLinkServer.h，方案见 docs/FlamingoLink方案.md。
 *  balloonwj@qq.com   2026-10-04
 */
#include "AdminLinkServer.h"

#include <time.h>
#include <functional>

#include "../net/InetAddress.h"
#include "../net/FrameCrypto.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "AdminLinkProtocol.h"
#include "AdminLinkSession.h"
#include "AdminManager.h"
#include "ChatServer.h"
#include "ChatSession.h"
#include "DepartmentManager.h"
#include "DepartmentMsg.h"
#include "UserManager.h"

namespace {

//一毫秒折合的微秒数：EventLoop::runEvery 的周期以微秒计
const int64_t kMicrosecondsPerMillisecond = 1000;

//一秒折合的微秒数
const int64_t kMicrosecondsPerSecond = 1000 * 1000;

} // anonymous namespace

AdminLinkServer::AdminLinkServer() :
    m_pushLoop(NULL),
    m_listening(false),
    m_port(0)
{
}

bool AdminLinkServer::init(const char* ip, short port, EventLoop* loop, int ioThreadCount)
{
    m_port = port;

    //先起推送线程：侦听一开始就可能有连接进来并发订阅，那时推送线程必须已经在了
    m_pushThread.reset(new EventLoopThread(EventLoopThread::ThreadInitCallback(), "adminlink-push"));
    m_pushLoop = m_pushThread->startLoop();

    m_pushLoop->runEvery(kAdminLinkFlushIntervalMs * kMicrosecondsPerMillisecond,
                         std::bind(&AdminLinkServer::flushTick, this));
    m_pushLoop->runEvery(kAdminLinkTimeoutCheckSec * kMicrosecondsPerSecond,
                         std::bind(&AdminLinkServer::timeoutCheckTick, this));
    m_pushLoop->runEvery(kAdminLinkTokenRecheckSec * kMicrosecondsPerSecond,
                         std::bind(&AdminLinkServer::tokenRecheckTick, this));

    InetAddress addr(ip, port);
    m_server.reset(new TcpServer(loop, addr, "FLAMINGO-ADMINLINK", TcpServer::kReusePort));
    m_server->setConnectionCallback(std::bind(&AdminLinkServer::onConnected, this, std::placeholders::_1));
    m_server->start(ioThreadCount);

    m_listening.store(true);
    LOGI("adminlink server listening on %s:%d, io threads: %d, encryption: %s",
         ip, (int)port, ioThreadCount, net::cryptoEncryptionEnabled() ? "on" : "off");
    return true;
}

void AdminLinkServer::uninit()
{
    m_listening.store(false);

    if (m_server)
        m_server->stop();

    //析构 EventLoopThread 会让推送线程的事件循环退出并等待线程结束
    m_pushThread.reset();
    m_pushLoop = NULL;
}

void AdminLinkServer::getLoginLinkInfo(int& outPort, std::string& outPubkey)
{
    std::string pubkey;
    const bool encryption = net::cryptoEncryptionEnabled();
    if (encryption)
        pubkey = net::cryptoServerIdentity().publicKeyBase64();

    AdminLinkProtocol::resolveLoginLinkInfo(m_listening.load(), m_port, encryption, pubkey, outPort, outPubkey);
}

void AdminLinkServer::onUserOnlineChanged(int64_t userid)
{
    if (!m_listening.load() || userid <= 0)
        return;

    std::lock_guard<std::mutex> guard(m_dirtyMutex);
    m_dirtyUsers.insert(userid);
}

void AdminLinkServer::requestSubscribe(const std::shared_ptr<AdminLinkSession>& session, int64_t enterpriseId,
                                       uint32_t seq)
{
    if (m_pushLoop == NULL || !session)
        return;

    m_pushLoop->runInLoop(std::bind(&AdminLinkServer::doSubscribe, this, session, enterpriseId, seq));
}

void AdminLinkServer::requestUnsubscribe(const std::shared_ptr<AdminLinkSession>& session, uint32_t seq)
{
    if (m_pushLoop == NULL || !session)
        return;

    m_pushLoop->runInLoop(std::bind(&AdminLinkServer::doUnsubscribe, this, session, seq));
}

void AdminLinkServer::onConnected(std::shared_ptr<TcpConnection> conn)
{
    if (conn->connected())
    {
        std::shared_ptr<AdminLinkSession> session(new AdminLinkSession(conn));
        conn->setMessageCallback(std::bind(&AdminLinkSession::onRead, session.get(),
                                           std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        size_t count = 0;
        {
            std::lock_guard<std::mutex> guard(m_sessionMutex);
            m_sessions.push_back(session);
            count = m_sessions.size();
        }
        LOGI("adminlink client connected: %s, connections: %d",
             conn->peerAddress().toIpPort().c_str(), (int)count);
    }
    else
    {
        onDisconnected(conn);
    }
}

void AdminLinkServer::onDisconnected(const std::shared_ptr<TcpConnection>& conn)
{
    std::shared_ptr<AdminLinkSession> removed;
    size_t count = 0;
    {
        std::lock_guard<std::mutex> guard(m_sessionMutex);
        for (std::list<std::shared_ptr<AdminLinkSession> >::iterator it = m_sessions.begin();
             it != m_sessions.end(); ++it)
        {
            if ((*it)->getConnectionPtr() == conn)
            {
                removed = *it;
                m_sessions.erase(it);
                break;
            }
        }
        count = m_sessions.size();
    }

    LOGI("adminlink client disconnected: %s, connections: %d",
         conn->peerAddress().toIpPort().c_str(), (int)count);

    //订阅表只在推送线程上访问，删除也投递过去
    if (removed && m_pushLoop != NULL)
        m_pushLoop->runInLoop(std::bind(&AdminLinkServer::doRemoveSession, this, removed));
}

void AdminLinkServer::getSessions(std::list<std::shared_ptr<AdminLinkSession> >& out)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    out = m_sessions;
}

void AdminLinkServer::doSubscribe(std::shared_ptr<AdminLinkSession> session, int64_t enterpriseId, uint32_t seq)
{
    //连接已经断了（断开的回调也会投递过来，排在本任务之后，那时会删除订阅），这里直接放弃
    if (!session->getConnectionPtr())
        return;

    m_subscriptions[session] = enterpriseId;
    session->send(cmd_adminlink_subscribe_online, static_cast<int32_t>(seq),
                  AdminLinkProtocol::buildSubscribeResponse(error_code_ok, "ok", enterpriseId));

    //完整名单：该企业全部处于登录态的员工
    std::vector<AdminLinkSessionFact> facts;
    std::vector<int64_t> enterprises;
    collectFacts(facts, enterprises);

    std::vector<AdminLinkSessionFact> entFacts;
    for (size_t i = 0; i < facts.size(); ++i)
    {
        if (enterprises[i] == enterpriseId)
            entFacts.push_back(facts[i]);
    }

    std::vector<int64_t> userids;
    AdminLinkProtocol::collectOnlineUserIds(entFacts, userids);

    std::vector<AdminLinkUser> users;
    buildEnterpriseUsers(enterpriseId, userids, entFacts, users);

    std::vector<std::string> bodies;
    AdminLinkProtocol::buildOnlineNotifyBodies(enterpriseId, true, users, kAdminLinkUsersPerFrame, bodies);
    sendBodies(session, bodies);

    LOGI("adminlink subscribe ok, enterpriseid: %lld, online users: %d, devices: %d, frames: %d, "
         "subscriptions: %d, client: %s",
         (long long)enterpriseId, (int)users.size(), (int)entFacts.size(), (int)bodies.size(),
         (int)m_subscriptions.size(), session->getPeer().c_str());
}

void AdminLinkServer::doUnsubscribe(std::shared_ptr<AdminLinkSession> session, uint32_t seq)
{
    m_subscriptions.erase(session);
    session->send(cmd_adminlink_unsubscribe_online, static_cast<int32_t>(seq),
                  AdminLinkProtocol::buildSimpleResponse(error_code_ok, "ok"));
    LOGI("adminlink unsubscribe ok, subscriptions: %d, client: %s",
         (int)m_subscriptions.size(), session->getPeer().c_str());
}

void AdminLinkServer::doRemoveSession(std::shared_ptr<AdminLinkSession> session)
{
    m_subscriptions.erase(session);
}

void AdminLinkServer::flushTick()
{
    std::set<int64_t> dirty;
    {
        std::lock_guard<std::mutex> guard(m_dirtyMutex);
        dirty.swap(m_dirtyUsers);
    }

    //没人订阅时变化直接丢弃：之后有人订阅会拿到完整名单，不需要补发
    if (dirty.empty() || m_subscriptions.empty())
        return;

    //企业 → 订阅了它的会话
    std::map<int64_t, std::vector<std::shared_ptr<AdminLinkSession> > > subscribersByEnt;
    for (std::map<std::shared_ptr<AdminLinkSession>, int64_t>::iterator it = m_subscriptions.begin();
         it != m_subscriptions.end(); ++it)
    {
        subscribersByEnt[it->second].push_back(it->first);
    }

    //把变化的员工按企业分组，只留下有人订阅的企业。当前归属与最近一次见到他在线时的归属不同
    //（被删除、调离企业）时两个企业都推：原企业那边他的设备列表为空，即显示为离线
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    std::map<int64_t, std::vector<int64_t> > usersByEnt;
    for (std::set<int64_t>::iterator it = dirty.begin(); it != dirty.end(); ++it)
    {
        const int64_t entId = deptMgr.getUserEnterpriseId(*it);
        if (subscribersByEnt.find(entId) != subscribersByEnt.end())
            usersByEnt[entId].push_back(*it);

        std::map<int64_t, int64_t>::iterator last = m_lastEnterpriseOfUser.find(*it);
        if (last != m_lastEnterpriseOfUser.end() && last->second != entId
            && subscribersByEnt.find(last->second) != subscribersByEnt.end())
        {
            usersByEnt[last->second].push_back(*it);
        }
    }

    if (usersByEnt.empty())
        return;

    std::vector<AdminLinkSessionFact> facts;
    std::vector<int64_t> enterprises;
    collectFacts(facts, enterprises);

    for (std::map<int64_t, std::vector<int64_t> >::iterator it = usersByEnt.begin(); it != usersByEnt.end(); ++it)
    {
        const int64_t entId = it->first;

        std::vector<AdminLinkSessionFact> entFacts;
        for (size_t i = 0; i < facts.size(); ++i)
        {
            if (enterprises[i] == entId)
                entFacts.push_back(facts[i]);
        }

        std::vector<AdminLinkUser> users;
        buildEnterpriseUsers(entId, it->second, entFacts, users);

        std::vector<std::string> bodies;
        AdminLinkProtocol::buildOnlineNotifyBodies(entId, false, users, kAdminLinkUsersPerFrame, bodies);

        const std::vector<std::shared_ptr<AdminLinkSession> >& subscribers = subscribersByEnt[entId];
        for (size_t i = 0; i < subscribers.size(); ++i)
            sendBodies(subscribers[i], bodies);

        LOGI("adminlink push online changes, enterpriseid: %lld, users: %d, subscribers: %d",
             (long long)entId, (int)users.size(), (int)subscribers.size());
    }
}

void AdminLinkServer::timeoutCheckTick()
{
    std::list<std::shared_ptr<AdminLinkSession> > sessions;
    getSessions(sessions);

    const int64_t now = static_cast<int64_t>(time(NULL));
    for (std::list<std::shared_ptr<AdminLinkSession> >::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        std::shared_ptr<AdminLinkSession>& s = *it;
        if (AdminLinkProtocol::isAuthTimeout(now, s->getConnectTimeSec(), s->isAuthed(), kAdminLinkAuthTimeoutSec))
            s->close("auth timeout");
        else if (AdminLinkProtocol::isIdleTimeout(now, s->getLastRecvSec(), kAdminLinkIdleTimeoutSec))
            s->close("idle timeout");
    }
}

void AdminLinkServer::tokenRecheckTick()
{
    std::list<std::shared_ptr<AdminLinkSession> > sessions;
    getSessions(sessions);

    AdminManager& adminMgr = Singleton<AdminManager>::Instance();
    for (std::list<std::shared_ptr<AdminLinkSession> >::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        std::shared_ptr<AdminLinkSession>& s = *it;
        if (!s->isAuthed())
            continue;

        std::string token;
        int64_t adminId = 0;
        int64_t adminEnterpriseId = 0;
        s->getIdentity(token, adminId, adminEnterpriseId);

        AdminInfo info;
        if (!adminMgr.checkToken(token, info))
        {
            LOGI("adminlink token no longer valid, adminid: %lld", (long long)adminId);
            s->close("token invalid");
        }
    }
}

void AdminLinkServer::collectFacts(std::vector<AdminLinkSessionFact>& outFacts, std::vector<int64_t>& outEnterprises)
{
    outFacts.clear();
    outEnterprises.clear();

    std::list<std::shared_ptr<ChatSession> > sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);

    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    for (std::list<std::shared_ptr<ChatSession> >::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        std::shared_ptr<ChatSession>& s = *it;
        if (!s)
            continue;

        const int32_t uid = s->getUserId();
        if (uid <= 0 || !s->isLoggedIn())
            continue;

        AdminLinkSessionFact f;
        f.userid     = uid;
        f.loggedIn   = true;
        f.clienttype = s->getClientType();
        f.presence   = s->getUserStatus();
        f.logintime  = s->getLoginTimeMs();

        //来源地址取自连接对象：peerAddress 在连接的整个生命期内不变，可从本线程读取
        std::shared_ptr<TcpConnection> conn = s->getConnectionPtr();
        if (conn)
            f.ip = AdminLinkProtocol::stripPort(conn->peerAddress().toIpPort());

        const int64_t entId = deptMgr.getUserEnterpriseId(uid);
        outFacts.push_back(f);
        outEnterprises.push_back(entId);

        //只在推送线程上调用本函数，可直接更新这份只属于推送线程的记录
        if (entId != 0)
            m_lastEnterpriseOfUser[uid] = entId;
    }
}

void AdminLinkServer::buildEnterpriseUsers(int64_t enterpriseId, const std::vector<int64_t>& userids,
                                           const std::vector<AdminLinkSessionFact>& facts,
                                           std::vector<AdminLinkUser>& outUsers)
{
    std::vector<dept::DepartmentInfo> depts;
    Singleton<DepartmentManager>::Instance().getOrgStructure(enterpriseId, depts);

    std::map<int64_t, AdminLinkUserProfile> profiles;
    AdminLinkProtocol::buildProfilesFromOrg(depts, profiles);

    //不在组织架构里的人（还没分配部门）从用户缓存取账号与昵称，部门留空
    UserManager& userMgr = Singleton<UserManager>::Instance();
    for (size_t i = 0; i < userids.size(); ++i)
    {
        if (profiles.find(userids[i]) != profiles.end())
            continue;

        User u;
        if (userMgr.getUserInfoByUserId(static_cast<int32_t>(userids[i]), u))
        {
            AdminLinkUserProfile p;
            p.account  = u.username;
            p.nickname = u.nickname;
            profiles[userids[i]] = p;
        }
    }

    AdminLinkProtocol::buildUsers(userids, facts, profiles, outUsers);
}

void AdminLinkServer::sendBodies(const std::shared_ptr<AdminLinkSession>& session,
                                 const std::vector<std::string>& bodies)
{
    //推送帧 seq 填 0
    for (size_t i = 0; i < bodies.size(); ++i)
        session->send(cmd_adminlink_online_notify, 0, bodies[i]);
}
