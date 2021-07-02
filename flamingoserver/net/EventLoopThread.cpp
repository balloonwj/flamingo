#include "EventLoopThread.h"
#include <functional>
#include "EventLoop.h"

using namespace net;

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
    const std::string& name/* = ""*/)
    : m_loop(NULL),
    m_exiting(false),
    m_callback(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    m_exiting = true;
    if (m_loop != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
    {
        // still a tiny chance to call destructed object, if threadFunc exits just now.
        // but when EventLoopThread destructs, usually programming is exiting anyway.
        m_loop->quit();
        m_thread->join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    //assert(!thread_.started());
    //thread_.start();

    m_thread.reset(new std::thread(std::bind(&EventLoopThread::threadFunc, this)));

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_loop == NULL)
        {
            m_cond.wait(lock);
        }
    }

    return m_loop;
}

void EventLoopThread::stopLoop()
{
    if (m_loop != NULL)
        m_loop->quit();

    m_thread->join();
}

void EventLoopThread::threadFunc()
{
    EventLoop loop;

    if (m_callback)
    {
        m_callback(&loop);
    }

    {
        //一个一个的线程创建
        std::unique_lock<std::mutex> lock(m_mutex);
        m_loop = &loop;
        m_cond.notify_all();
    }

    loop.loop();
    //assert(exiting_);
    m_loop = NULL;
}
