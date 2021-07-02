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

    class TimerQueue
    {
    public:
        TimerQueue(EventLoop* loop);
        ~TimerQueue();

        //interval单位是微妙
        TimerId addTimer(const TimerCallback& cb, Timestamp when, int64_t interval, int64_t repeatCount);

        TimerId addTimer(TimerCallback&& cb, Timestamp when, int64_t interval, int64_t repeatCount);

        void removeTimer(TimerId timerId);

        void cancel(TimerId timerId, bool off);

        // called when timerfd alarms
        void doTimer();

    private:
        TimerQueue(const TimerQueue& rhs) = delete;
        TimerQueue& operator=(const TimerQueue& rhs) = delete;

        typedef std::pair<Timestamp, Timer*>    Entry;
        typedef std::set<Entry>                 TimerList;
        typedef std::pair<Timer*, int64_t>      ActiveTimer;
        typedef std::set<ActiveTimer>           ActiveTimerSet;

        void addTimerInLoop(Timer* timer);
        void removeTimerInLoop(TimerId timerId);
        void cancelTimerInLoop(TimerId timerId, bool off);

        void insert(Timer* timer);

    private:
        EventLoop* m_loop;
        TimerList           m_timers;
    };

}
