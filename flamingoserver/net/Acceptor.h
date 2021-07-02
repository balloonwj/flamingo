#pragma once

#include <functional>

#include "Channel.h"
#include "Sockets.h"

namespace net
{
    class EventLoop;
    class InetAddress;

    class Acceptor
    {
    public:
        typedef std::function<void(int sockfd, const InetAddress&)> NewConnectionCallback;

        Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
        ~Acceptor();

        //设置新连接到来的回调函数
        void setNewConnectionCallback(const NewConnectionCallback& cb)
        {
            m_newConnectionCallback = cb;
        }

        bool listenning() const { return m_listenning; }
        void listen();

    private:
        void handleRead();

    private:
        EventLoop* m_loop;
        Socket                m_acceptSocket;
        Channel               m_acceptChannel;
        NewConnectionCallback m_newConnectionCallback;
        bool                  m_listenning;

#ifndef WIN32
        int                   m_idleFd;
#endif
    };
}
