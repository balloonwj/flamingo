#pragma once

#ifndef WIN32
#include "Poller.h"

#include <vector>
#include <map>

struct pollfd;

namespace net
{
    class Channel;
    class EventLoop;
    ///
    /// IO Multiplexing with poll(2).
    ///
    class PollPoller : public Poller
    {
    public:

        PollPoller(EventLoop* loop);
        virtual ~PollPoller();

        virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels);
        virtual bool updateChannel(Channel* channel);
        virtual void removeChannel(Channel* channel);

        void assertInLoopThread() const;
        

    private:
        void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

    private:
        typedef std::vector<struct pollfd>  PollFdList;
        typedef std::map<int, Channel*>     ChannelMap;

        ChannelMap                          channels_;
        PollFdList                          pollfds_;
        EventLoop*                          ownerLoop_;
    };

}

#endif 
