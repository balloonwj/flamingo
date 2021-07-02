#pragma once

#include <atomic>
#include <map>
#include <memory>

#include "TcpConnection.h"

namespace net
{
    class Acceptor;
    class EventLoop;
    class EventLoopThreadPool;

    class TcpServer
    {
    public:
        typedef std::function<void(EventLoop*)> ThreadInitCallback;
        enum Option
        {
            kNoReusePort,
            kReusePort,
        };

        TcpServer(EventLoop* loop,
            const InetAddress& listenAddr,
            const std::string& nameArg,
            Option option = kReusePort);      //TODO: 默认修改成kReusePort
        ~TcpServer();

        const std::string& hostport() const { return m_hostport; }
        const std::string& name() const { return m_name; }
        EventLoop* getLoop() const { return m_loop; }
        ;
        void setThreadInitCallback(const ThreadInitCallback& cb)
        {
            m_threadInitCallback = cb;
        }

        void start(int workerThreadCount = 4);

        void stop();

        /// Set connection callback.
        /// Not thread safe.
        void setConnectionCallback(const ConnectionCallback& cb)
        {
            m_connectionCallback = cb;
        }

        /// Set message callback.
        /// Not thread safe.
        void setMessageCallback(const MessageCallback& cb)
        {
            m_messageCallback = cb;
        }

        /// Set write complete callback.
        /// Not thread safe.
        void setWriteCompleteCallback(const WriteCompleteCallback& cb)
        {
            m_writeCompleteCallback = cb;
        }

        void removeConnection(const TcpConnectionPtr& conn);

    private:
        /// Not thread safe, but in loop
        void newConnection(int sockfd, const InetAddress& peerAddr);
        /// Thread safe.

        /// Not thread safe, but in loop
        void removeConnectionInLoop(const TcpConnectionPtr& conn);

        typedef std::map<string, TcpConnectionPtr> ConnectionMap;

    private:
        EventLoop* m_loop;
        const string                                    m_hostport;
        const string                                    m_name;
        std::unique_ptr<Acceptor>                       m_acceptor;
        std::unique_ptr<EventLoopThreadPool>            m_eventLoopThreadPool;
        ConnectionCallback                              m_connectionCallback;
        MessageCallback                                 m_messageCallback;
        WriteCompleteCallback                           m_writeCompleteCallback;
        ThreadInitCallback                              m_threadInitCallback;
        std::atomic<int>                                m_started;
        int                                             m_nextConnId;
        ConnectionMap                                   m_connections;
    };

}
