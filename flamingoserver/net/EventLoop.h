#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "../base/Timestamp.h"
#include "../base/Platform.h"
#include "Callbacks.h"
#include "Sockets.h"
#include "TimerId.h"
#include "TimerQueue.h"

namespace net
{
    class EventLoop;
    class Channel;
    class Poller;
    //class TimerQueue;
    class CTimerHeap;

    ///
    /// Reactor, at most one per thread.
    ///
    /// This is an interface class, so don't expose too much details.
    class EventLoop
    {
    public:
        typedef std::function<void()> Functor;

        EventLoop();
        ~EventLoop();  // force out-line dtor, for scoped_ptr members.

        ///
        /// Loops forever.
        ///
        /// Must be called in the same thread as creation of the object.
        ///
        void loop();

        /// Quits loop.
        ///
        /// This is not 100% thread safe, if you call through a raw pointer,
        /// better to call through shared_ptr<EventLoop> for 100% safety.
        void quit();

        ///
        /// Time when poll returns, usually means data arrival.
        ///
        Timestamp pollReturnTime() const { return m_pollReturnTime; }

        int64_t iteration() const { return m_iteration; }

        /// Runs callback immediately in the loop thread.
        /// It wakes up the loop, and run the cb.
        /// If in the same loop thread, cb is run within the function.
        /// Safe to call from other threads.
        void runInLoop(const Functor& cb);
        /// Queues callback in the loop thread.
        /// Runs after finish pooling.
        /// Safe to call from other threads.
        void queueInLoop(const Functor& cb);

        // timers，时间单位均是微秒
        ///
        /// Runs callback at 'time'.
        /// Safe to call from other threads.
        ///
        TimerId runAt(const Timestamp& time, const TimerCallback& cb);
        ///
        /// Runs callback after @c delay seconds.
        /// Safe to call from other threads.
        ///
        TimerId runAfter(int64_t delay, const TimerCallback& cb);
        ///
        /// Runs callback every @c interval seconds.
        /// Safe to call from other threads.
        ///
        TimerId runEvery(int64_t interval, const TimerCallback& cb);
        ///
        /// Cancels the timer.
        /// Safe to call from other threads.
        ///
        void cancel(TimerId timerId, bool off);

        void remove(TimerId timerId);


        TimerId runAt(const Timestamp& time, TimerCallback&& cb);
        TimerId runAfter(int64_t delay, TimerCallback&& cb);
        TimerId runEvery(int64_t interval, TimerCallback&& cb);

        void setFrameFunctor(const Functor& cb);

        // internal usage
        bool updateChannel(Channel* channel);
        void removeChannel(Channel* channel);
        bool hasChannel(Channel* channel);

        // pid_t threadId() const { return threadId_; }
        void assertInLoopThread()
        {
            if (!isInLoopThread())
            {
                abortNotInLoopThread();
            }
        }
        bool isInLoopThread() const { return m_threadId == std::this_thread::get_id(); }
        bool eventHandling() const { return m_eventHandling; }

        const std::thread::id getThreadID() const
        {
            return m_threadId;
        }

    private:
        bool createWakeupfd();
        bool wakeup();
        void abortNotInLoopThread();
        bool handleRead();  // waked up handler
        void doOtherTasks();

        void printActiveChannels() const; // DEBUG

    private:
        typedef std::vector<Channel*> ChannelList;

        bool                                m_looping;
        bool                                m_quit;
        bool                                m_eventHandling;
        bool                                m_doingOtherTasks;
        const std::thread::id               m_threadId;
        Timestamp                           m_pollReturnTime;
        std::unique_ptr<Poller>             m_poller;
        std::unique_ptr<TimerQueue>         m_timerQueue;
        int64_t                             m_iteration;
#ifdef WIN32
        SOCKET                              m_wakeupFdSend;
        SOCKET                              m_wakeupFdListen;
        SOCKET                              m_wakeupFdRecv;

        //int                               fdpipe_[2];
#else
        SOCKET                              m_wakeupFd;          //TODO: 这个fd什么时候释放？
#endif
        // unlike in TimerQueue, which is an internal class,
        // we don't expose Channel to client.
        std::unique_ptr<Channel>            m_wakeupChannel;

        // scratch variables
        ChannelList                         m_activeChannels;
        Channel* currentActiveChannel_;

        std::mutex                          m_mutex;
        std::vector<Functor>                m_pendingFunctors; // Guarded by mutex_

        Functor                             m_frameFunctor;
    };

}
