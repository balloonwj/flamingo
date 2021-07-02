#include "PollPoller.h"

#ifndef WIN32

#include "../base/AsyncLog.h"
#include "Channel.h"
#include "EventLoop.h"

#include <errno.h>
#include <poll.h>

using namespace net;

PollPoller::PollPoller(EventLoop* loop) : m_ownerLoop(loop)
{
}

PollPoller::~PollPoller()
{
}

Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    // XXX pollfds_ shouldn't change
    int numEvents = ::poll(&*m_pollfds.begin(), m_pollfds.size(), timeoutMs);
    int savedErrno = errno;
    Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
        LOGD("%d  events happended", numEvents);
        fillActiveChannels(numEvents, activeChannels);
    }
    else if (numEvents == 0)
    {
        LOGD("nothing happended");
    }
    else
    {
        if (savedErrno != EINTR)
        {
            errno = savedErrno;
            LOGSYSE("PollPoller::poll()");
        }
    }
    return now;
}

void PollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    for (PollFdList::const_iterator pfd = m_pollfds.begin(); pfd != m_pollfds.end() && numEvents > 0; ++pfd)
    {
        if (pfd->revents > 0)
        {
            --numEvents;
            ChannelMap::const_iterator ch = m_channels.find(pfd->fd);
            //assert(ch != channels_.end());
            if (ch == m_channels.end())
                continue;

            Channel* channel = ch->second;
            //assert(channel->fd() == pfd->fd);
            if (channel->fd() != pfd->fd)
                continue;

            channel->set_revents(pfd->revents);
            // pfd->revents = 0;
            activeChannels->push_back(channel);
        }
    }
}

bool PollPoller::updateChannel(Channel* channel)
{
    assertInLoopThread();
    LOGD("fd = %d events = %d", channel->fd(), channel->events());
    if (channel->index() < 0)
    {
        // a new one, add to pollfds_
        //assert(channels_.find(channel->fd()) == channels_.end());
        if (m_channels.find(channel->fd()) != m_channels.end())
            return false;

        struct pollfd pfd;
        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(channel->events());
        pfd.revents = 0;
        m_pollfds.push_back(pfd);
        int idx = static_cast<int>(m_pollfds.size()) - 1;
        channel->set_index(idx);
        m_channels[pfd.fd] = channel;
    }
    else
    {
        // update existing one
        //assert(channels_.find(channel->fd()) != channels_.end());
        //assert(channels_[channel->fd()] == channel);
        if (m_channels.find(channel->fd()) == m_channels.end() || m_channels[channel->fd()] != channel)
            return false;

        int idx = channel->index();
        //assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
        if (0 > idx || idx >= static_cast<int>(m_pollfds.size()))
            return false;

        struct pollfd& pfd = m_pollfds[idx];
        //TODO: 为什么是 -channel->fd() ？
        //assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd() - 1);
        pfd.events = static_cast<short>(channel->events());
        pfd.revents = 0;
        if (channel->isNoneEvent())
        {
            // ignore this pollfd
            pfd.fd = -channel->fd() - 1;
        }
    }

    return true;
}

void PollPoller::removeChannel(Channel* channel)
{
    assertInLoopThread();
    LOGD("fd = %d", channel->fd());

    //assert(channels_.find(channel->fd()) != channels_.end());
    //assert(channels_[channel->fd()] == channel);
    //assert(channel->isNoneEvent());

    if (m_channels.find(channel->fd()) == m_channels.end() || m_channels[channel->fd()] != channel || !channel->isNoneEvent())
        return;

    int idx = channel->index();
    //assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    if (0 > idx && idx >= static_cast<int>(m_pollfds.size()))
        return;

    const struct pollfd& pfd = m_pollfds[idx]; (void)pfd;
    //TODO: 为什么是 -channel->fd()？
    //assert(pfd.fd == -channel->fd() - 1 && pfd.events == channel->events());

    size_t n = m_channels.erase(channel->fd());
    //assert(n == 1); (void)n;
    if (n != 1)
        return;

    if (size_t(idx) == m_pollfds.size() - 1)
    {
        m_pollfds.pop_back();
    }
    else
    {
        int channelAtEnd = m_pollfds.back().fd;
        iter_swap(m_pollfds.begin() + idx, m_pollfds.end() - 1);
        if (channelAtEnd < 0)
        {
            channelAtEnd = -channelAtEnd - 1;
        }
        m_channels[channelAtEnd]->set_index(idx);
        m_pollfds.pop_back();
    }
}

void PollPoller::assertInLoopThread() const
{
    m_ownerLoop->assertInLoopThread();
}

#endif //!WIN32
