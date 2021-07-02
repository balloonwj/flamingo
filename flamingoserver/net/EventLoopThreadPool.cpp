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
    : m_baseLoop(NULL),
    m_started(false),
    m_numThreads(0),
    m_next(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's stack variable
}

void EventLoopThreadPool::init(EventLoop* baseLoop, int numThreads)
{
    m_numThreads = numThreads;
    m_baseLoop = baseLoop;
}

void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
    //assert(baseLoop_);
    if (m_baseLoop == NULL)
        return;

    //assert(!started_);
    if (m_started)
        return;

    m_baseLoop->assertInLoopThread();

    m_started = true;

    for (int i = 0; i < m_numThreads; ++i)
    {
        char buf[128];
        snprintf(buf, sizeof buf, "%s%d", m_name.c_str(), i);

        std::unique_ptr<EventLoopThread> t(new EventLoopThread(cb, buf));
        //EventLoopThread* t = new EventLoopThread(cb, buf);        
        m_loops.push_back(t->startLoop());
        m_threads.push_back(std::move(t));
    }
    if (m_numThreads == 0 && cb)
    {
        cb(m_baseLoop);
    }
}

void EventLoopThreadPool::stop()
{
    for (auto& iter : m_threads)
    {
        iter->stopLoop();
    }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
    m_baseLoop->assertInLoopThread();
    //assert(started_);
    if (!m_started)
        return NULL;

    EventLoop* loop = m_baseLoop;

    if (!m_loops.empty())
    {
        // round-robin
        loop = m_loops[m_next];
        ++m_next;
        if (size_t(m_next) >= m_loops.size())
        {
            m_next = 0;
        }
    }
    return loop;
}

EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode)
{
    m_baseLoop->assertInLoopThread();
    EventLoop* loop = m_baseLoop;

    if (!m_loops.empty())
    {
        loop = m_loops[hashCode % m_loops.size()];
    }
    return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
    m_baseLoop->assertInLoopThread();
    if (m_loops.empty())
    {
        return std::vector<EventLoop*>(1, m_baseLoop);
    }
    else
    {
        return m_loops;
    }
}

const std::string EventLoopThreadPool::info() const
{
    std::stringstream ss;
    ss << "print threads id info " << endl;
    for (size_t i = 0; i < m_loops.size(); i++)
    {
        ss << i << ": id = " << m_loops[i]->getThreadID() << endl;
    }
    return ss.str();
}
