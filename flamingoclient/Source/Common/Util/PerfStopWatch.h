/**
 *  PerfStopWatch.h
 *
 *  高精度耗时计量：基于性能计数器（QueryPerformanceCounter），分辨率在微秒级。
 *  用于给「打开聊天窗」「滚动一格」这类只有几毫秒到几十毫秒的操作计时 —— 系统毫秒
 *  计数（GetTickCount）约 15.6 毫秒才跳一次，量这类操作会被量化成 0 / 16 / 31 这样的
 *  台阶，看不出真实耗时。
 *
 *  只有头文件、不依赖 stdafx.h，客户端各模块与单元测试工程都可以直接包含。
 *
 *  典型用法：
 *      PerfStopWatch sw;                 // 构造即开始计时
 *      DoSomething();
 *      double ms = sw.ElapsedMs();       // 取经过的毫秒数（带小数）
 *      sw.Restart();                     // 从现在起重新计时
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <windows.h>

/**
 *  取当前时刻（毫秒，带小数，精度到微秒）。时间原点是系统启动时刻，只用来算两个时刻
 *  之间的差（动画进度、耗时），不要当作墙钟时间。
 *  与系统毫秒计数（GetTickCount）不同，它不会每约 15.6 毫秒才跳一次，逐帧驱动动画时
 *  每一帧拿到的时刻都是准确的，画面不会因为时刻停在同一个值上而原地顿一帧。
 *  @return 当前时刻（毫秒）；性能计数器不可用时返回 0。
 */
inline double PerfNowMs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    if (freq.QuadPart == 0)
    {
        return 0.0;
    }
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

/**
 *  PerfStopWatch —— 从构造（或最近一次 Restart）起计时的秒表。
 *  对象很轻（两个 64 位整数），可以放在栈上随用随建。线程无关，但同一个对象不应
 *  在多个线程之间共享。
 */
class PerfStopWatch
{
public:
    // 构造即开始计时。
    PerfStopWatch()
    {
        ::QueryPerformanceFrequency(&m_freq);
        ::QueryPerformanceCounter(&m_start);
    }

    // 从起点到现在经过的毫秒数（带小数，精度到微秒）。不改变起点。
    double ElapsedMs() const
    {
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        if (m_freq.QuadPart == 0)
        {
            return 0.0;   // 性能计数器不可用（极老的硬件），按零处理
        }
        return (double)(now.QuadPart - m_start.QuadPart) * 1000.0 / (double)m_freq.QuadPart;
    }

    // 把起点重置为现在。
    void Restart()
    {
        ::QueryPerformanceCounter(&m_start);
    }

private:
    LARGE_INTEGER m_freq;    // 性能计数器频率（每秒计数次数），构造时取一次
    LARGE_INTEGER m_start;   // 计时起点的计数值
};
