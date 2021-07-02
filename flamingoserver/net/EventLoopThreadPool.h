#pragma once

#include <vector>
#include <functional>
#include <memory>
#include <string>

namespace net
{
    class EventLoop;
    class EventLoopThread;

    class EventLoopThreadPool
    {
    public:
        typedef std::function<void(EventLoop*)> ThreadInitCallback;

        EventLoopThreadPool();
        ~EventLoopThreadPool();

        void init(EventLoop* baseLoop, int numThreads);
        void start(const ThreadInitCallback& cb = ThreadInitCallback());

        void stop();

        /// round-robin
        EventLoop* getNextLoop();

        /// with the same hash code, it will always return the same EventLoop
        EventLoop* getLoopForHash(size_t hashCode);

        std::vector<EventLoop*> getAllLoops();

        bool started() const
        {
            return m_started;
        }

        const std::string& name() const
        {
            return m_name;
        }

        const std::string info() const;

    private:

        EventLoop* m_baseLoop;
        std::string                                     m_name;
        bool                                            m_started;
        int                                             m_numThreads;
        int                                             m_next;
        std::vector<std::unique_ptr<EventLoopThread> >  m_threads;
        std::vector<EventLoop*>                         m_loops;
    };

}
