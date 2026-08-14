#pragma once

//SOCKET 类型来自这里：Windows 下是 winsock 的 UINT_PTR，POSIX 下是 int
#include "../base/Platform.h"
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
        typedef std::function<void(SOCKET sockfd, const InetAddress&)> NewConnectionCallback;

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
