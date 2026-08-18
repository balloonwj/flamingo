/**
 *  ShakeRateLimiter.h
 *
 *  窗口抖动节流器 —— 对"同一目标"维护上次放行的时刻，把发送 / 接收抖动
 *  的频率限制在"最小间隔"之上。纯逻辑、不依赖任何 Win32 类型（时刻用
 *  unsigned long long 传入，调用方通常传 GetTickCount64() 的毫秒值），便于
 *  单元测试。
 *
 *  典型用法：
 *      ShakeRateLimiter limiter;
 *      if (!limiter.TryPass(::GetTickCount64(), kShakeMinIntervalMs))
 *      {
 *          // 距上次放行不足最小间隔 —— 被拦截，给出提示并 return。
 *      }
 *      // 放行：正常发送 / 处理这次抖动。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

namespace balloonchatwnd {

/**
 *  ShakeRateLimiter —— 窗口抖动频率节流器。
 *
 *  语义要点：间隔始终从"上次成功放行"算起；被拦截的请求<u>不</u>刷新计时，
 *  因此连续快速点击不会把放行时刻一直往后推（不会出现"一直点就一直发不
 *  出去、且 5 秒永远重新计时"的情况）。一个限制器对应一个目标。
 */
class ShakeRateLimiter
{
public:
    ShakeRateLimiter()
        : m_lastPassTick(0)
    {
    }

    /**
     *  尝试放行一次抖动。
     *    now：           当前时刻（毫秒，通常为 GetTickCount64() 的返回值）。
     *    minIntervalMs： 同一目标两次放行之间要求的最小间隔（毫秒）。
     *  返回：true=放行（并把内部"上次放行时刻"更新为 now）；
     *        false=被拦截（距上次放行不足 minIntervalMs，<u>不</u>更新计时）。
     */
    bool TryPass(unsigned long long now, unsigned long long minIntervalMs)
    {
        // m_lastPassTick==0 表示从未放行过，首次必放行。否则按"距上次放行
        // 的间隔是否达到 minIntervalMs"判定。
        if (m_lastPassTick != 0 && now - m_lastPassTick < minIntervalMs)
        {
            return false;
        }
        m_lastPassTick = now;
        return true;
    }

    /**
     *  复位为"从未放行"状态（下一次 TryPass 必放行）。
     */
    void Reset()
    {
        m_lastPassTick = 0;
    }

private:
    unsigned long long m_lastPassTick;   // 上次成功放行的时刻（毫秒）；0=从未放行
};

} // namespace balloonchatwnd
