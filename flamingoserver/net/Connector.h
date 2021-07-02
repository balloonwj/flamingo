#pragma once

#include "InetAddress.h"
#include <functional>
#include <memory>

namespace net
{

    class Channel;
    class EventLoop;

    class Connector : public std::enable_shared_from_this<Connector>
    {
    public:
        typedef std::function<void(int sockfd)> NewConnectionCallback;

        Connector(EventLoop* loop, const InetAddress& serverAddr);
        ~Connector();

        void setNewConnectionCallback(const NewConnectionCallback& cb)
        {
            m_newConnectionCallback = cb;
        }

        void start();
        void restart();
        void stop();

        const InetAddress& serverAddress() const { return m_serverAddr; }

    private:
        enum State { kDisconnected, kConnecting, kConnected };
        static const int kMaxRetryDelayMs = 30 * 1000;
        static const int kInitRetryDelayMs = 500;

        void setState(State s) { m_state = s; }
        void startInLoop();
        void stopInLoop();
        void connect();
        void connecting(int sockfd);
        void handleWrite();
        void handleError();
        void retry(int sockfd);
        int removeAndResetChannel();
        void resetChannel();

    private:
        EventLoop* m_loop;
        InetAddress                     m_serverAddr;
        bool                            m_connect;
        State                           m_state;
        std::unique_ptr<Channel>        m_channel;
        NewConnectionCallback           m_newConnectionCallback;
        int                             m_retryDelayMs;
    };
}
