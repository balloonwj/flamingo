#pragma once

#include <atomic>
#include <stdint.h>
#include "../base/Timestamp.h"
#include "../net/Callbacks.h"

namespace net
{
    ///
    /// Internal class for timer event.
    ///
    class Timer
    {
    public:
        Timer(const TimerCallback& cb, Timestamp when, double interval)
            : callback_(cb),
            expiration_(when),
            interval_(interval),
            repeat_(interval > 0.0),
            sequence_(++s_numCreated_)
        { }


        Timer(TimerCallback&& cb, Timestamp when, double interval)
            : callback_(std::move(cb)),
            expiration_(when),
            interval_(interval),
            repeat_(interval > 0.0),
            sequence_(++s_numCreated_)
        { }

        void run() const
        {
            callback_();
        }

        Timestamp expiration() const  { return expiration_; }
        bool repeat() const { return repeat_; }
        int64_t sequence() const { return sequence_; }

        void restart(Timestamp now);

        static int64_t numCreated() { return s_numCreated_; }

    private:
        //noncopyable
        Timer(const Timer& rhs) = delete;
        Timer& operator=(const Timer& rhs) = delete;

    private:
        const TimerCallback         callback_;
        Timestamp                   expiration_;
        const double                interval_;
        const bool                  repeat_;
        const int64_t               sequence_;

        static std::atomic<int64_t> s_numCreated_;
    };
}

