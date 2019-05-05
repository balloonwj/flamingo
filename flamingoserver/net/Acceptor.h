#pragma once

#include <functional>

#include "Channel.h"
#include "Sockets.h"
//#include "EventLoop.h"

namespace net
{
    class EventLoop;
    class InetAddress;

    ///
    /// Acceptor of incoming TCP connections.
    ///
    class Acceptor
    {
    public:
        typedef std::function<void(int sockfd, const InetAddress&)> NewConnectionCallback;

        Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
        ~Acceptor();

        //设置新连接到来的回调函数
        void setNewConnectionCallback(const NewConnectionCallback& cb)
        {
            newConnectionCallback_ = cb;
        }

        bool listenning() const { return listenning_; }
        void listen();

    private:
        void handleRead();

    private:
        EventLoop*            loop_;
        Socket                acceptSocket_;
        Channel               acceptChannel_;
        NewConnectionCallback newConnectionCallback_;
        bool                  listenning_;

#ifndef WIN32
        int                   idleFd_;
#endif
    };

}