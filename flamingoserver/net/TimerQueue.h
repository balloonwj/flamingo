#pragma once

#include <set>
#include <vector>

#include "../base/Timestamp.h"
#include "../net/Callbacks.h"
#include "../net/Channel.h"

namespace net
{

    class EventLoop;
    class Timer;
    class TimerId;

    ///
    /// A best efforts timer queue.
    /// No guarantee that the callback will be on time.
    ///
    class TimerQueue
    {
    public:
        TimerQueue(EventLoop* loop);
        ~TimerQueue();

        ///
        /// Schedules the callback to be run at given time,
        /// repeats if @c interval > 0.0.
        ///
        /// Must be thread safe. Usually be called from other threads.
        //intervalµ•Œª «Œ¢√Ó
        TimerId addTimer(const TimerCallback& cb, Timestamp when, int64_t interval, int64_t repeatCount);

        TimerId addTimer(TimerCallback&& cb, Timestamp when, int64_t interval, int64_t repeatCount);

        void removeTimer(TimerId timerId);

        void cancel(TimerId timerId, bool off);

        // called when timerfd alarms
        void doTimer();
      
    private:
        //noncopyable
        TimerQueue(const TimerQueue& rhs) = delete;
        TimerQueue& operator=(const TimerQueue& rhs) = delete;

        // FIXME: use unique_ptr<Timer> instead of raw pointers.
        typedef std::pair<Timestamp, Timer*>    Entry;
        typedef std::set<Entry>                 TimerList;
        typedef std::pair<Timer*, int64_t>      ActiveTimer;
        typedef std::set<ActiveTimer>           ActiveTimerSet;

        void addTimerInLoop(Timer* timer);
        void removeTimerInLoop(TimerId timerId);
        void cancelTimerInLoop(TimerId timerId, bool off);
        
        // move out all expired timers
        //std::vector<Entry> getExpired(Timestamp now);
        //void reset(const std::vector<Entry>& expired, Timestamp now);

        void insert(Timer* timer);

    private:
        EventLoop*          loop_;
        //const int           timerfd_;
        //Channel             timerfdChannel_;
        // Timer list sorted by expiration
        TimerList           timers_;

        //for cancel()
        //ActiveTimerSet      activeTimers_;
        //bool                callingExpiredTimers_; /* atomic */
        //ActiveTimerSet      cancelingTimers_;
    };

}

