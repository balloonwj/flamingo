#pragma once

namespace net
{
    class Timer;

    class TimerId
    {
    public:
        TimerId() : m_timer(NULL), m_sequence(0)
        {
        }

        TimerId(Timer* timer, int64_t seq) : m_timer(timer), m_sequence(seq)
        {
        }

        Timer* getTimer()
        {
            return m_timer;
        }

        // default copy-ctor, dtor and assignment are okay

        friend class TimerQueue;

    private:
        Timer* m_timer;
        int64_t     m_sequence;
    };

}
