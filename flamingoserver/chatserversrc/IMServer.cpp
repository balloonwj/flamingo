/**
 *  服务器主服务类，IMServer.cpp
 *  zhangyl 2017.03.09
 **/
#include "../net/InetAddress.h"
#include "../base/Logging.h"
#include "../base/Singleton.h"
#include "IMServer.h"
#include "ChatSession.h"
#include "UserManager.h"

bool IMServer::Init(const char* ip, short port, EventLoop* loop)
{   
    InetAddress addr(ip, port);
    m_server.reset(new TcpServer(loop, addr, "FLAMINGO-SERVER", TcpServer::kReusePort));
    m_server->setConnectionCallback(std::bind(&IMServer::OnConnection, this, std::placeholders::_1));
    //启动侦听
    m_server->start();

    return true;
}

void IMServer::OnConnection(std::shared_ptr<TcpConnection> conn)
{
    if (conn->connected())
    {
        //LOG_INFO << "client connected:" << conn->peerAddress().toIpPort();
        ++m_sessionId;
        std::shared_ptr<ChatSession> spSession(new ChatSession(conn, m_sessionId));
        conn->setMessageCallback(std::bind(&ChatSession::OnRead, spSession.get(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));       

        std::lock_guard<std::mutex> guard(m_sessionMutex);
        m_sessions.push_back(spSession);
    }
    else
    {
        OnClose(conn);
    }
}

void IMServer::OnClose(const std::shared_ptr<TcpConnection>& conn)
{
    //是否有用户下线
    //bool bUserOffline = false;
    UserManager& userManager = Singleton<UserManager>::Instance();

    //TODO: 这样的代码逻辑太混乱，需要优化
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    for (auto iter = m_sessions.begin(); iter != m_sessions.end(); ++iter)
    {
        if ((*iter)->GetConnectionPtr() == NULL)
        {
            LOG_ERROR << "connection is NULL";
            break;
        }
        
        //通过比对connection对象找到对应的session
        if ((*iter)->GetConnectionPtr() == conn)
        {
            //该Session不是之前被踢下线的有效Session，才认为是正常下线，才给其好友推送其下线消息
            if ((*iter)->IsSessionValid())
            { 
                //遍历其在线好友，给其好友推送其下线消息
                std::list<User> friends;
                int32_t offlineUserId = (*iter)->GetUserId();
                userManager.GetFriendInfoByUserId(offlineUserId, friends);
                for (const auto& iter2 : friends)
                {
                    for (auto& iter3 : m_sessions)
                    {
                        //该好友是否在线（在线会存在session）
                        if (iter2.userid == iter3->GetUserId())
                        {
                            iter3->SendUserStatusChangeMsg(offlineUserId, 2);

                            LOG_INFO << "SendUserStatusChangeMsg to user(userid=" << iter3->GetUserId() << "): user go offline, offline userid = " << offlineUserId;
                        }
                    }
                }
            }
            else
            {
                LOG_INFO << "Session is invalid, userid=" << (*iter)->GetUserId();
            }
            
            //停掉该Session的掉线检测
            //(*iter)->DisableHeartbaetCheck();
            //用户下线
            m_sessions.erase(iter);
            //bUserOffline = true;
            LOG_INFO << "client disconnected: " << conn->peerAddress().toIpPort();
            break;
        }
    }

    LOG_INFO << "current online user count: " << m_sessions.size();
}

void IMServer::GetSessions(std::list<std::shared_ptr<ChatSession>>& sessions)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    sessions = m_sessions;
}

bool IMServer::GetSessionByUserIdAndClientType(std::shared_ptr<ChatSession>& session, int32_t userid, int32_t clientType)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    std::shared_ptr<ChatSession> tmpSession;
    for (const auto& iter : m_sessions)
    {
        tmpSession = iter;
        if (iter->GetUserId() == userid && iter->GetClientType() == clientType)
        {
            session = tmpSession;
            return true;
        }
    }

    return false;
}

bool IMServer::GetSessionsByUserId(std::list<std::shared_ptr<ChatSession>>& sessions, int32_t userid)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    std::shared_ptr<ChatSession> tmpSession;
    for (const auto& iter : m_sessions)
    {
        tmpSession = iter;
        if (iter->GetUserId() == userid)
        {
            sessions.push_back(tmpSession);
            return true;
        }
    }

    return false;
}

int32_t IMServer::GetUserStatusByUserId(int32_t userid)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    for (const auto& iter : m_sessions)
    {
        if (iter->GetUserId() == userid)
        {
            return iter->GetUserStatus();
        }
    }

    return 0;
}

int32_t IMServer::GetUserClientTypeByUserId(int32_t userid)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    bool bMobileOnline = false;
    int clientType = CLIENT_TYPE_UNKOWN;
    for (const auto& iter : m_sessions)
    {
        if (iter->GetUserId() == userid)
        {   
            clientType = iter->GetUserClientType();
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