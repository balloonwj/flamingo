/**
 *  文件服务器 / 图片服务器主服务类实现，FileServer.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "FileServer.h"
#include "../net/InetAddress.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "FileSession.h"

//TcpServer 工作线程数：沿用 v1 取值
static const int FILE_SERVER_THREAD_NUM = 6;

bool FileServer::init(const char* ip, short port, EventLoop* loop,
                      const char* fileBaseDir, const char* tokenKey, int serverKind)
{
    m_strFileBaseDir = fileBaseDir;
    m_tokenKey       = (tokenKey != NULL) ? tokenKey : "";
    m_serverKind     = serverKind;

    InetAddress addr(ip, port);
    m_server.reset(new TcpServer(loop, addr, "ZYL-MYImgAndFileServer", TcpServer::kReusePort));
    m_server->setConnectionCallback(std::bind(&FileServer::onConnected, this, std::placeholders::_1));
    //启动侦听
    m_server->start(FILE_SERVER_THREAD_NUM);

    return true;
}

void FileServer::uninit()
{
    if (m_server)
        m_server->stop();
}

void FileServer::onConnected(std::shared_ptr<TcpConnection> conn)
{
    if (conn->connected())
    {
        LOGI("client connected: %s", conn->peerAddress().toIpPort().c_str());
        std::shared_ptr<FileSession> spSession(
            new FileSession(conn, m_strFileBaseDir.c_str(), m_tokenKey, m_serverKind));
        conn->setMessageCallback(std::bind(&FileSession::onRead, spSession.get(),
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

void FileServer::onDisconnected(const std::shared_ptr<TcpConnection>& conn)
{
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    for (auto iter = m_sessions.begin(); iter != m_sessions.end(); ++iter)
    {
        if ((*iter)->getConnectionPtr() == NULL)
        {
            LOGE("connection is NULL");
            break;
        }

        //用户下线，移除其会话
        m_sessions.erase(iter);
        LOGI("client disconnected: %s", conn->peerAddress().toIpPort().c_str());
        break;
    }
}
