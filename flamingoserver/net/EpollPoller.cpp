#include "EpollPoller.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "../base/Logging.h"
#include "Channel.h"

using namespace net;

// On Linux, the constants of poll(2) and epoll(4)
// are expected to be the same.
static_assert(EPOLLIN == POLLIN, "EPOLLIN == POLLIN");
static_assert(EPOLLPRI == POLLPRI, "EPOLLPRI == POLLPRI");
static_assert(EPOLLOUT == POLLOUT, "EPOLLOUT == POLLOUT");
static_assert(EPOLLRDHUP == POLLRDHUP,"EPOLLRDHUP == POLLRDHUP");
static_assert(EPOLLERR == POLLERR, "EPOLLERR == POLLERR");
static_assert(EPOLLHUP == POLLHUP, "EPOLLHUP == POLLHUP");

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
		LOG_SYSFATAL << "EPollPoller::EPollPoller";
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
		LOG_TRACE << numEvents << " events happended";
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
			LOG_SYSERR << "EPollPoller::poll()";
		}
	}
	return now;
}

void EPollPoller::fillActiveChannels(int numEvents,
	ChannelList* activeChannels) const
{
	assert(static_cast<size_t>(numEvents) <= events_.size());
	for (int i = 0; i < numEvents; ++i)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
#ifndef NDEBUG
		int fd = channel->fd();
		ChannelMap::const_iterator it = channels_.find(fd);
		assert(it != channels_.end());
		assert(it->second == channel);
#endif
		channel->set_revents(events_[i].events);
		activeChannels->push_back(channel);
	}
}

bool EPollPoller::updateChannel(Channel* channel)
{
	assertInLoopThread();
	LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
	const int index = channel->index();
	if (index == kNew || index == kDeleted)
	{
		// a new one, add with EPOLL_CTL_ADD
		int fd = channel->fd();
		if (index == kNew)
		{          
            //assert(channels_.find(fd) == channels_.end())
            if (channels_.find(fd) != channels_.end())
            {
                LOG_ERROR << "fd = " << fd << " must not exist in channels_";
                return false;
            }
                

			channels_[fd] = channel;
		}
		else // index == kDeleted
		{
            //assert(channels_.find(fd) != channels_.end());
            if (channels_.find(fd) == channels_.end())
            {
                LOG_ERROR << "fd = " << fd << " must exist in channels_";
                return false;
            }
            
			//assert(channels_[fd] == channel);
            if (channels_[fd] != channel)
            {
                LOG_ERROR << "current channel is not matched current fd, fd = " << fd;
                return false;
            }
		}
		channel->set_index(kAdded);
		
        return update(EPOLL_CTL_ADD, channel);
	}
	else
	{
		// update existing one with EPOLL_CTL_MOD/DEL
		int fd = channel->fd();
		//assert(channels_.find(fd) != channels_.end());
		//assert(channels_[fd] == channel);
		//assert(index == kAdded);
        if (channels_.find(fd) == channels_.end() || channels_[fd] != channel || index != kAdded)
        {
            LOG_ERROR << "current channel is not matched current fd, fd = " << fd << ", channel = " << channel;
            return false;
        }

		if (channel->isNoneEvent())
		{
            if (update(EPOLL_CTL_DEL, channel))
            {
                channel->set_index(kDeleted);
                return true;
            }
            return false;
		}
		else
		{
            return update(EPOLL_CTL_MOD, channel);
		}
	}
}

void EPollPoller::removeChannel(Channel* channel)
{
	assertInLoopThread();
	int fd = channel->fd();
	LOG_TRACE << "fd = " << fd;
	assert(channels_.find(fd) != channels_.end());
	assert(channels_[fd] == channel);
	assert(channel->isNoneEvent());
	int index = channel->index();
	assert(index == kAdded || index == kDeleted);
	size_t n = channels_.erase(fd);
	(void)n;
	assert(n == 1);

	if (index == kAdded)
	{
		update(EPOLL_CTL_DEL, channel);
	}
	channel->set_index(kNew);
}

bool EPollPoller::update(int operation, Channel* channel)
{
	struct epoll_event event;
	bzero(&event, sizeof event);
	event.events = channel->events();
	event.data.ptr = channel;
	int fd = channel->fd();
	if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
	{
		if (operation == EPOLL_CTL_DEL)
		{
			LOG_SYSERR << "epoll_ctl op=" << operation << " fd=" << fd << ", epollfd=" 
                << epollfd_ << ", errno=" << errno << ", errorInfo: " << strerror(errno);
		}
		else
		{
			//FIXME： epoll_ctl执行失败这里会导致程序退出
            LOG_SYSERR << "epoll_ctl op=" << operation << " fd=" << fd << ", epollfd="
                << epollfd_ << ", errno=" << errno << ", errorInfo: " << strerror(errno);
		}

        return false;
	}

    return true;
}
