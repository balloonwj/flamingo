#pragma once

#include <vector>
#include "../base/Timestamp.h"

namespace net
{
    class Channel;

    class Poller
    {
    public:
        Poller();
        ~Poller();

    public:
        typedef std::vector<Channel*> ChannelList;

        virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;
        virtual bool updateChannel(Channel* channel) = 0;
        virtual void removeChannel(Channel* channel) = 0;

        virtual bool hasChannel(Channel* channel) const = 0;
    };
}

