#include "Poller.h"
#include "Channel.h"

using namespace net;

Poller::Poller()
{
}

Poller::~Poller()
{
}

//bool Poller::hasChannel(Channel* channel) const
//{
//    //assertInLoopThread();
//    //ChannelMap::const_iterator it = channels_.find(channel->fd());
//    //return it != channels_.end() && it->second == channel;
//
//    return false;
//}