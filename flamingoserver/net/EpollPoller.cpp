#include "EpollPoller.h"

#ifndef WIN32
#include <string.h>
#include "../base/Platform.h"
#include "../base/AsyncLog.h"
#include "EventLoop.h"
#include "Channel.h"



using namespace net;

// On Linux, the constants of poll(2) and epoll(4)
// are expected to be the same.
//static_assert(EXPOLLIN == XPOLLIN, "EXPOLLIN == XPOLLIN");
//static_assert(EXPOLLPRI == XPOLLPRI, "EXPOLLPRI == XPOLLPRI");
//static_assert(EXPOLLOUT == XPOLLOUT, "EXPOLLOUT == XPOLLOUT");
//static_assert(EXPOLLRDHUP == XPOLLRDHUP,"EXPOLLRDHUP == XPOLLRDHUP");
//static_assert(EXPOLLERR == XPOLLERR, "EXPOLLERR == XPOLLERR");
//static_assert(EXPOLLHUP == XPOLLHUP, "EXPOLLHUP == XPOLLHUP");

namespace
{
	const int kNew = -1;
	const int kAdded = 1;
	const int kDeleted = 2;
}

EPollPoller::EPollPoller(EventLoop* loop)
:epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
events_(kInitEventListSize), 
ownerLoop_(loop)
{
	if (epollfd_ < 0)
	{
		LOGF("EPollPoller::EPollPoller");
	}
}

EPollPoller::~EPollPoller()
{
	::close(epollfd_);
}

bool EPollPoller::hasChannel(Channel* channel) const
{
	assertInLoopThread();
	ChannelMap::const_iterator it = channels_.find(channel->fd());
	return it != channels_.end() && it->second == channel;
}

void EPollPoller::assertInLoopThread() const
{
    ownerLoop_->assertInLoopThread();
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
	int numEvents = ::epoll_wait(epollfd_,
		&*events_.begin(),
		static_cast<int>(events_.size()),
		timeoutMs);
	int savedErrno = errno;
	Timestamp now(Timestamp::now());
	if (numEvents > 0)
	{
		//LOG_TRACE << numEvents << " events happended";
		fillActiveChannels(numEvents, activeChannels);
		if (static_cast<size_t>(numEvents) == events_.size())
		{
			events_.resize(events_.size() * 2);
		}
	}
	else if (numEvents == 0)
	{
		//LOG_TRACE << " nothing happended";
	}
	else
	{
		// error happens, log uncommon ones
		if (savedErrno != EINTR)
		{
			errno = savedErrno;
			LOGSYSE("EPollPoller::poll()");
		}
	}
	return now;
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
	//assert(static_cast<size_t>(numEvents) <= events_.size());
	for (int i = 0; i < numEvents; ++i)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
		int fd = channel->fd();
		ChannelMap::const_iterator it = channels_.find(fd);
        if (it == channels_.end() || it->second != channel)
            return;
		channel->set_revents(events_[i].events);
		activeChannels->push_back(channel);
	}
}

bool EPollPoller::updateChannel(Channel* channel)
{
	assertInLoopThread();
	LOGD("fd = %d  events = %d", channel->fd(), channel->events());
	const int index = channel->index();
	if (index == kNew || index == kDeleted)
	{
		// a new one, add with XEPOLL_CTL_ADD
		int fd = channel->fd();
		if (index == kNew)
		{          
            //assert(channels_.find(fd) == channels_.end())
            if (channels_.find(fd) != channels_.end())
            {
                LOGE("fd = %d  must not exist in channels_", fd);
                return false;
            }
                

			channels_[fd] = channel;
		}
		else // index == kDeleted
		{
            //assert(channels_.find(fd) != channels_.end());
            if (channels_.find(fd) == channels_.end())
            {
                LOGE("fd = %d  must exist in channels_", fd);
                return false;
            }
            
			//assert(channels_[fd] == channel);
            if (channels_[fd] != channel)
            {
                LOGE("current channel is not matched current fd, fd = %d", fd);
                return false;
            }
		}
		channel->set_index(kAdded);
		
        return update(XEPOLL_CTL_ADD, channel);
	}
	else
	{
		// update existing one with XEPOLL_CTL_MOD/DEL
		int fd = channel->fd();
		//assert(channels_.find(fd) != channels_.end());
		//assert(channels_[fd] == channel);
		//assert(index == kAdded);
        if (channels_.find(fd) == channels_.end() || channels_[fd] != channel || index != kAdded)
        {
            LOGE("current channel is not matched current fd, fd = %d, channel = 0x%x", fd, channel);
            return false;
        }

		if (channel->isNoneEvent())
		{
            if (update(XEPOLL_CTL_DEL, channel))
            {
                channel->set_index(kDeleted);
                return true;
            }
            return false;
		}
		else
		{
            return update(XEPOLL_CTL_MOD, channel);
		}
	}
}

void EPollPoller::removeChannel(Channel* channel)
{
	assertInLoopThread();
	int fd = channel->fd();
	
    //assert(channels_.find(fd) != channels_.end());
    //assert(channels_[fd] == channel);
    //assert(channel->isNoneEvent());
    if (channels_.find(fd) == channels_.end() || channels_[fd] != channel || !channel->isNoneEvent())
        return;
	
	int index = channel->index();
	//assert(index == kAdded || index == kDeleted);
    if (index != kAdded && index != kDeleted)
        return;

	size_t n = channels_.erase(fd);
	(void)n;
	//assert(n == 1);
    if (n != 1)
        return;

	if (index == kAdded)
	{
		update(XEPOLL_CTL_DEL, channel);
	}
	channel->set_index(kNew);
}

bool EPollPoller::update(int operation, Channel* channel)
{
	struct epoll_event event;
	memset(&event, 0, sizeof event);
	event.events = channel->events();
	event.data.ptr = channel;
	int fd = channel->fd();
	if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
	{
		if (operation == XEPOLL_CTL_DEL)
		{
			LOGE("epoll_ctl op=%d fd=%d, epollfd=%d, errno=%d, errorInfo: %s", operation, fd, epollfd_, errno, strerror(errno));
		}
		else
		{           
            LOGE("epoll_ctl op=%d fd=%d, epollfd=%d, errno=%d, errorInfo: %s", operation, fd, epollfd_, errno, strerror(errno));
		}

        return false;
	}

    return true;
}

#endif