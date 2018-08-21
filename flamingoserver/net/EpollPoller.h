#pragma once

#include <vector>
#include <map>

#include "../base/Timestamp.h"
#include "EventLoop.h"

struct epoll_event;

namespace net
{

	///
	/// IO Multiplexing with epoll(4).
	///
	class EPollPoller
	{
	public:
		typedef std::vector<Channel*> ChannelList;

		EPollPoller(EventLoop* loop);
		virtual ~EPollPoller();

		virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels);
		virtual bool updateChannel(Channel* channel);
		virtual void removeChannel(Channel* channel);

		virtual bool hasChannel(Channel* channel) const;

		//static EPollPoller* newDefaultPoller(EventLoop* loop);

		void assertInLoopThread() const
		{
			ownerLoop_->assertInLoopThread();
		}

	private:
		static const int kInitEventListSize = 16;

		void fillActiveChannels(int numEvents,
			ChannelList* activeChannels) const;
		bool update(int operation, Channel* channel);		

	private:
		typedef std::vector<struct epoll_event> EventList;

		int epollfd_;
		EventList events_;

		typedef std::map<int, Channel*> ChannelMap;

		ChannelMap channels_;
		EventLoop* ownerLoop_;
	};

}