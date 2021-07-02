#include <stdio.h>  // snprintf
#include <functional>

#include "TcpClient.h"
#include "../base/AsyncLog.h"
#include "../base/Platform.h"
#include "EventLoop.h"
#include "Sockets.h"
#include "Connector.h"

using namespace net;

// TcpClient::TcpClient(EventLoop* loop)
//   : loop_(loop)
// {
// }

// TcpClient::TcpClient(EventLoop* loop, const string& host, uint16_t port)
//   : loop_(CHECK_NOTNULL(loop)),
//     serverAddr_(host, port)
// {
// }

namespace net
{
    namespace detail
    {

        void removeConnection(EventLoop* loop, const TcpConnectionPtr& conn)
        {
            loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
        }

        void removeConnector(const ConnectorPtr& connector)
        {
            //connector->
        }

    }
}


TcpClient::TcpClient(EventLoop* loop,
    const InetAddress& serverAddr,
    const std::string& nameArg)
    : m_loop(loop),
    m_connector(new Connector(loop, serverAddr)),
    m_name(nameArg),
    m_connectionCallback(defaultConnectionCallback),
    m_messageCallback(defaultMessageCallback),
    m_retry(false),
    m_connect(true),
    m_nextConnId(1)
{
    m_connector->setNewConnectionCallback(
        std::bind(&TcpClient::newConnection, this, std::placeholders::_1));
    // FIXME setConnectFailedCallback
    LOGD("TcpClient::TcpClient[%s] - connector 0x%x", m_name.c_str(), m_connector.get());
}

TcpClient::~TcpClient()
{
    LOGD("TcpClient::~TcpClient[%s] - connector 0x%x", m_name.c_str(), m_connector.get());
    TcpConnectionPtr conn;
    bool unique = false;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        unique = m_connection.unique();
        conn = m_connection;
    }
    if (conn)
    {
        //assert(loop_ == conn->getLoop());
        if (m_loop != conn->getLoop())
            return;

        // FIXME: not 100% safe, if we are in different thread
        CloseCallback cb = std::bind(&detail::removeConnection, m_loop, std::placeholders::_1);
        m_loop->runInLoop(std::bind(&TcpConnection::setCloseCallback, conn, cb));
        if (unique)
        {
            conn->forceClose();
        }
    }
    else
    {
        m_connector->stop();
        // FIXME: HACK
        // loop_->runAfter(1, boost::bind(&detail::removeConnector, connector_));
    }
}

void TcpClient::connect()
{
    // FIXME: check state
    LOGD("TcpClient::connect[%s] - connecting to %s", m_name.c_str(), m_connector->serverAddress().toIpPort().c_str());
    m_connect = true;
    m_connector->start();
}

void TcpClient::disconnect()
{
    m_connect = false;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_connection)
        {
            m_connection->shutdown();
        }
    }
}

void TcpClient::stop()
{
    m_connect = false;
    m_connector->stop();
}

void TcpClient::newConnection(int sockfd)
{
    m_loop->assertInLoopThread();
    InetAddress peerAddr(sockets::getPeerAddr(sockfd));
    char buf[32];
    snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), m_nextConnId);
    ++m_nextConnId;
    string connName = m_name + buf;

    InetAddress localAddr(sockets::getLocalAddr(sockfd));
    // FIXME poll with zero timeout to double confirm the new connection
    // FIXME use make_shared if necessary
    TcpConnectionPtr conn(new TcpConnection(m_loop, connName, sockfd, localAddr, peerAddr));
    conn->setConnectionCallback(m_connectionCallback);
    conn->setMessageCallback(m_messageCallback);
    conn->setWriteCompleteCallback(m_writeCompleteCallback);
    conn->setCloseCallback(std::bind(&TcpClient::removeConnection, this, std::placeholders::_1)); // FIXME: unsafe

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_connection = conn;
    }

    conn->connectEstablished();
}

void TcpClient::removeConnection(const TcpConnectionPtr& conn)
{
    m_loop->assertInLoopThread();
    //assert(loop_ == conn->getLoop());
    if (m_loop != conn->getLoop())
        return;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        //assert(connection_ == conn);
        if (m_connection != conn)
            return;

        m_connection.reset();
    }

    m_loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    if (m_retry && m_connect)
    {
        LOGD("TcpClient::connect[%s] - Reconnecting to %s", m_name.c_str(), m_connector->serverAddress().toIpPort().c_str());
        m_connector->restart();
    }
}
