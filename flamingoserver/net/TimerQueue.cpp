#include "TimerQueue.h"

#include <functional>

#include "../base/Platform.h"
#include "../base/AsyncLog.h"
#include "EventLoop.h"
#include "Timer.h"
#include "TimerId.h"

namespace net
{
    //namespace detail
    //{

    //    int createTimerfd()
    //    {
    //        int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    //        if (timerfd < 0)
    //        {
    //            LOG_SYSFATAL << "Failed in timerfd_create";
    //        }
    //        return timerfd;
    //    }

    //    struct timespec howMuchTimeFromNow(Timestamp when)
    //    {
    //        int64_t microseconds = when.microSecondsSinceEpoch()
    //            - Timestamp::now().microSecondsSinceEpoch();
    //        if (microseconds < 100)
    //        {
    //            microseconds = 100;
    //        }
    //        struct timespec ts;
    //        ts.tv_sec = static_cast<time_t>(
    //            microseconds / Timestamp::kMicroSecondsPerSecond);
    //        ts.tv_nsec = static_cast<long>(
    //            (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
    //        return ts;
    //    }

    //    void readTimerfd(int timerfd, Timestamp now)
    //    {
    //        uint64_t howmany;
    //        ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
    //        LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
    //        if (n != sizeof howmany)
    //        {
    //            LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
    //        }
    //    }

    //    void resetTimerfd(int timerfd, Timestamp expiration)
    //    {
    //        // wake up loop by timerfd_settime()
    //        struct itimerspec newValue;
    //        struct itimerspec oldValue;
    //        bzero(&newValue, sizeof newValue);
    //        bzero(&oldValue, sizeof oldValue);
    //        newValue.it_value = howMuchTimeFromNow(expiration);
    //        int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
    //        if (ret)
    //        {
    //            LOG_SYSERR << "timerfd_settime()";
    //        }
    //    }

    //}
}

using namespace net;
//using namespace net::detail;

TimerQueue::TimerQueue(EventLoop* loop)
    : m_loop(loop),
    /*timerfd_(createTimerfd()),
    timerfdChannel_(loop, timerfd_),*/
    m_timers()
    //callingExpiredTimers_(false)
{
    //timerfdChannel_.setReadCallback(
    //    std::bind(&TimerQueue::handleRead, this));
    // we are always reading the timerfd, we disarm it with timerfd_settime.
    //将timerfd挂到epollfd上
    //timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
    //timerfdChannel_.disableAll();
    //timerfdChannel_.remove();
    //::close(timerfd_);
    // do not remove channel, since we're in EventLoop::dtor();
    for (TimerList::iterator it = m_timers.begin(); it != m_timers.end(); ++it)
    {
        delete it->second;
    }
}

TimerId TimerQueue::addTimer(const TimerCallback& cb, Timestamp when, int64_t interval, int64_t repeatCount)
{
    Timer* timer = new Timer(cb, when, interval);
    m_loop->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}

TimerId TimerQueue::addTimer(TimerCallback&& cb, Timestamp when, int64_t interval, int64_t repeatCount)
{
    Timer* timer = new Timer(std::move(cb), when, interval, repeatCount);
    m_loop->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}

void TimerQueue::removeTimer(TimerId timerId)
{
    m_loop->runInLoop(std::bind(&TimerQueue::removeTimerInLoop, this, timerId));
}

void TimerQueue::cancel(TimerId timerId, bool off)
{
    m_loop->runInLoop(std::bind(&TimerQueue::cancelTimerInLoop, this, timerId, off));
}

void TimerQueue::doTimer()
{
    m_loop->assertInLoopThread();

    Timestamp now(Timestamp::now());

    for (auto iter = m_timers.begin(); iter != m_timers.end(); )
    {
        if (iter->second->expiration() <= now)
        {
            //LOGD("time: %lld", iter->second->expiration().microSecondsSinceEpoch());
            iter->second->run();
            if (iter->second->getRepeatCount() == 0)
            {
                iter = m_timers.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
        else
        {
            break;
        }
    }


    //readTimerfd(timerfd_, now);

    //std::vector<Entry> expired = getExpired(now);

    //callingExpiredTimers_ = true;
    //cancelingTimers_.clear();
    //// safe to callback outside critical section
    //for (std::vector<Entry>::iterator it = expired.begin();
    //    it != expired.end(); ++it)
    //{
    //    it->second->run();
    //}
    //callingExpiredTimers_ = false;

    //reset(expired, now);
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
    m_loop->assertInLoopThread();
    /*bool earliestChanged = */insert(timer);

    //if (earliestChanged)
    //{
    //    resetTimerfd(timerfd_, timer->expiration());
    //}
}

void TimerQueue::removeTimerInLoop(TimerId timerId)
{
    m_loop->assertInLoopThread();
    //assert(timers_.size() == activeTimers_.size());
    //ActiveTimer timer(timerId.timer_, timerId.sequence_);
    //ActiveTimerSet::iterator it = activeTimers_.find(timer);

    Timer* timer = timerId.m_timer;
    for (auto iter = m_timers.begin(); iter != m_timers.end(); ++iter)
    {
        if (iter->second == timer)
        {
            m_timers.erase(iter);
            break;
        }
    }
}

void TimerQueue::cancelTimerInLoop(TimerId timerId, bool off)
{
    m_loop->assertInLoopThread();

    Timer* timer = timerId.m_timer;
    for (auto iter = m_timers.begin(); iter != m_timers.end(); ++iter)
    {
        if (iter->second == timer)
        {
            iter->second->cancel(off);
            break;
        }
    }

    ////assert(timers_.size() == activeTimers_.size());
    //ActiveTimer timer(timerId.timer_, timerId.sequence_);
    //ActiveTimerSet::iterator it = activeTimers_.find(timer);
    //if (it != activeTimers_.end())
    //{
    //    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    //    //assert(n == 1); (void)n;
    //    delete it->first; // FIXME: no delete please
    //    activeTimers_.erase(it);
    //}
    //else if (callingExpiredTimers_)
    //{
    //    cancelingTimers_.insert(timer);
    //}
    ////assert(timers_.size() == activeTimers_.size());
}

//std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
//{
//    assert(timers_.size() == activeTimers_.size());
//    std::vector<Entry> expired;
//    Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
//    TimerList::iterator end = timers_.lower_bound(sentry);
//    assert(end == timers_.end() || now < end->first);
//    std::copy(timers_.begin(), end, back_inserter(expired));
//    timers_.erase(timers_.begin(), end);
//
//    for (std::vector<Entry>::iterator it = expired.begin();
//        it != expired.end(); ++it)
//    {
//        ActiveTimer timer(it->second, it->second->sequence());
//        size_t n = activeTimers_.erase(timer);
//        assert(n == 1); (void)n;
//    }
//
//    assert(timers_.size() == activeTimers_.size());
//    return expired;
//}

//void TimerQueue::reset(const std::vector<Entry> & expired, Timestamp now)
//{
//    Timestamp nextExpire;
//
//    for (std::vector<Entry>::const_iterator it = expired.begin();
//        it != expired.end(); ++it)
//    {
//        ActiveTimer timer(it->second, it->second->sequence());
//        if (it->second->getRepeatCount()
//            && cancelingTimers_.find(timer) == cancelingTimers_.end())
//        {
//            it->second->restart(now);
//            insert(it->second);
//        }
//        else
//        {
//            // FIXME move to a free list
//            delete it->second; // FIXME: no delete please
//        }
//    }
//
//    if (!timers_.empty())
//    {
//        nextExpire = timers_.begin()->second->expiration();
//    }
//
//    if (nextExpire.valid())
//    {
//        resetTimerfd(timerfd_, nextExpire);
//    }
//}

void TimerQueue::insert(Timer* timer)
{
    m_loop->assertInLoopThread();
    //assert(timers_.size() == activeTimers_.size());
    bool earliestChanged = false;
    Timestamp when = timer->expiration();
    //TimerList::iterator it = timers_.begin();
    //if (it == timers_.end() || when < it->first)
    //{
    //    earliestChanged = true;
    //}
    //{
    /*std::pair<TimerList::iterator, bool> result = */m_timers.insert(Entry(when, timer));
    //assert(result.second); (void)result;
//}
//{
//    std::pair<ActiveTimerSet::iterator, bool> result = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    //assert(result.second); (void)result;
//}

//assert(timers_.size() == activeTimers_.size());
//return earliestChanged;
}
