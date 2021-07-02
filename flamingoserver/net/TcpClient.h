#pragma once

#include <string>
#include <mutex>
#include "TcpConnection.h"

namespace net
{
    class EventLoop;
    class Connector;
    typedef std::shared_ptr<Connector> ConnectorPtr;

    class TcpClient
    {
    public:
        TcpClient(EventLoop* loop, const InetAddress& serverAddr, const string& nameArg);
        ~TcpClient();

        void connect();
        void disconnect();
        void stop();

        TcpConnectionPtr connection() const
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_connection;
        }

        EventLoop* getLoop() const { return m_loop; }
        void enableRetry() { m_retry = true; }

        const std::string& name() const
        {
            return m_name;
        }

        void setConnectionCallback(const ConnectionCallback& cb)
        {
            m_connectionCallback = cb;
        }

        void setMessageCallback(const MessageCallback& cb)
        {
            m_messageCallback = cb;
        }

        void setWriteCompleteCallback(const WriteCompleteCallback& cb)
        {
            m_writeCompleteCallback = cb;
        }

    private:
        void newConnection(int sockfd);
        void removeConnection(const TcpConnectionPtr& conn);

    private:
        EventLoop* m_loop;
        ConnectorPtr            m_connector;
        const std::string       m_name;
        ConnectionCallback      m_connectionCallback;
        MessageCallback         m_messageCallback;
        WriteCompleteCallback   m_writeCompleteCallback;
        bool                    m_retry;
        bool                    m_connect;

        int                     m_nextConnId;
        mutable std::mutex      m_mutex;
        TcpConnectionPtr        m_connection;
    };

}
