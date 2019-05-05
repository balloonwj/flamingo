#include "EventLoopThreadPool.h"
#include <stdio.h>
#include <assert.h>
#include <sstream>
#include <string>
#include "EventLoop.h"
#include "EventLoopThread.h"
#include "Callbacks.h"

using namespace net;


EventLoopThreadPool::EventLoopThreadPool()
: baseLoop_(NULL),
started_(false),
numThreads_(0),
next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
	// Don't delete loop, it's stack variable
}

void EventLoopThreadPool::Init(EventLoop* baseLoop, int numThreads)
{
	numThreads_ = numThreads;
	baseLoop_ = baseLoop;
}

void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
    //assert(baseLoop_);
    if (baseLoop_ == NULL)
        return;
    
    assert(!started_);
    if (started_)
        return;  
	
	baseLoop_->assertInLoopThread();

	started_ = true;

	for (int i = 0; i < numThreads_; ++i)
	{
		char buf[128];
		snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);

		std::shared_ptr<EventLoopThread> t(new EventLoopThread(cb, buf));
		//EventLoopThread* t = new EventLoopThread(cb, buf);
		threads_.push_back(t);
		loops_.push_back(t->startLoop());
	}
	if (numThreads_ == 0 && cb)
	{
		cb(baseLoop_);
	}
}

void EventLoopThreadPool::stop()
{
    for (auto& iter : threads_)
    {
        iter->stopLoop();
    }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
	baseLoop_->assertInLoopThread();
    //assert(started_);
    if (!started_)
        return NULL;
	
	EventLoop* loop = baseLoop_;

	if (!loops_.empty())
	{
		// round-robin
		loop = loops_[next_];
		++next_;
		if (implicit_cast<size_t>(next_) >= loops_.size())
		{
			next_ = 0;
		}
	}
	return loop;
}

EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode)
{
	baseLoop_->assertInLoopThread();
	EventLoop* loop = baseLoop_;

	if (!loops_.empty())
	{
		loop = loops_[hashCode % loops_.size()];
	}
	return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
	baseLoop_->assertInLoopThread();
	if (loops_.empty())
	{
		return std::vector<EventLoop*>(1, baseLoop_);
	}
	else
	{
		return loops_;
	}
}

const std::string EventLoopThreadPool::info() const
{
	std::stringstream ss;
	ss << "print threads id info " << endl;
	for (size_t i = 0; i < loops_.size(); i++)
	{
		ss << i << ": id = " << loops_[i]->getThreadID() << endl;
	}
	return ss.str();
}