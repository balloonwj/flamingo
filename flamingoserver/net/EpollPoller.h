#pragma once

#ifndef WIN32

#include <vector>
#include <map>

#include "../base/Timestamp.h"
#include "Poller.h"

struct epoll_event;

namespace net
{
    class EventLoop;

    class EPollPoller : public Poller
    {
    public:
        EPollPoller(EventLoop* loop);
        virtual ~EPollPoller();

        virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels);
        virtual bool updateChannel(Channel* channel);
        virtual void removeChannel(Channel* channel);

        virtual bool hasChannel(Channel* channel) const;

        void assertInLoopThread() const;

    private:
        static const int kInitEventListSize = 16;

        void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
        bool update(int operation, Channel* channel);

    private:
        typedef std::vector<struct epoll_event> EventList;

        int                                     m_epollfd;
        EventList                               m_events;

        typedef std::map<int, Channel*>         ChannelMap;

        ChannelMap                              m_channels;
        EventLoop* m_ownerLoop;
    };
}

#endif
