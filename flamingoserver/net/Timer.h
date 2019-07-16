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
        Timer(const TimerCallback& cb, Timestamp when, int64_t interval, int64_t repeatCount = -1)
            : callback_(cb),
            expiration_(when),
            interval_(interval),
            repeatCount_(repeatCount),       
            sequence_(++s_numCreated_),
            canceled_(false)
        { }


        Timer(TimerCallback&& cb, Timestamp when, int64_t interval)
            : callback_(std::move(cb)),
            expiration_(when),
            interval_(interval),
            repeatCount_(-1),
            sequence_(++s_numCreated_),
            canceled_(false)          
        { }

        void run()
        {
            if (canceled_)
                return;

            callback_();

            if (repeatCount_ != -1)
            {
                --repeatCount_;
                if (repeatCount_ == 0)
                {
                    //repeatCount_ = 0;
                    return;
                }                               
            }
           
            expiration_ += interval_;               
        }

        bool isCanceled() const
        {
            return canceled_;
        }

        void cancel(bool off)
        {
            canceled_ = off;
        }

        Timestamp expiration() const { return expiration_; }
        int64_t getRepeatCount() const { return repeatCount_; }
        int64_t sequence() const { return sequence_; }

        //void restart(Timestamp now);

        static int64_t numCreated() { return s_numCreated_; }

    private:
        //noncopyable
        Timer(const Timer& rhs) = delete;
        Timer& operator=(const Timer& rhs) = delete;

    private:
        const TimerCallback         callback_;
        Timestamp                   expiration_;
        const int64_t               interval_;
        int64_t                     repeatCount_;       //重复次数，-1 表示一直重复下去
        const int64_t               sequence_;
        bool                        canceled_;          //是否处于取消状态

        static std::atomic<int64_t> s_numCreated_;
    };
}