/**
 *  远程桌面中转服务器实现，RelayServer.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RelayServer.h"
#include "../net/InetAddress.h"
#include "../base/AsyncLog.h"

//TcpServer 工作线程数：中转连接数远少于文件传输，取较小值
static const int RELAY_SERVER_THREAD_NUM = 4;

bool RelayServer::init(const char* ip, short port, EventLoop* loop, const char* tokenKey)
{
    m_tokenKey = (tokenKey != NULL) ? tokenKey : "";

    InetAddress addr(ip, port);
    m_server.reset(new TcpServer(loop, addr, "ZYL-MYRelayServer", TcpServer::kReusePort));
    m_server->setConnectionCallback(std::bind(&RelayServer::onConnected, this, std::placeholders::_1));
    m_server->start(RELAY_SERVER_THREAD_NUM);

    return true;
}

void RelayServer::uninit()
{
    if (m_server)
        m_server->stop();
}

void RelayServer::onConnected(std::shared_ptr<TcpConnection> conn)
{
    if (conn->connected())
    {
        LOGI("relay client connected: %s", conn->peerAddress().toIpPort().c_str());
        std::shared_ptr<RelaySession> spSession(new RelaySession(conn, m_tokenKey, &m_hub));
        conn->setMessageCallback(std::bind(&RelaySession::onRead, spSession.get(),
                                           std::placeholders::_1, std::placeholders::_2,
                                           std::placeholders::_3));

        std::lock_guard<std::mutex> guard(m_sessionMutex);
        m_sessions.push_back(spSession);
    }
    else
    {
        onDisconnected(conn);
    }
}

void RelayServer::onDisconnected(const std::shared_ptr<TcpConnection>& conn)
{
    //从配对表摘除本连接；若对端仍在，连带关闭它，使两端一起收尾
    std::shared_ptr<TcpConnection> peer = m_hub.leaveByConn(conn.get());
    if (peer)
        peer->forceClose();

    //移除本连接对应的会话对象
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    for (std::list<std::shared_ptr<RelaySession>>::iterator iter = m_sessions.begin();
         iter != m_sessions.end(); ++iter)
    {
        if ((*iter)->getConnectionPtr() == conn)
        {
            m_sessions.erase(iter);
            LOGI("relay client disconnected: %s", conn->peerAddress().toIpPort().c_str());
            break;
        }
    }
}
