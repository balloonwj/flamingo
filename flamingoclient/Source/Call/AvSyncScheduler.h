/**
 *  音视频同步呈现调度器（纯逻辑，头文件实现），AvSyncScheduler.h
 *
 *  接收端视频帧不在「解码即显示」——那样会把网络抖动直接搬到画面上（忽快忽慢），也无法跟
 *  音频对齐。本调度器是一个带时间戳的视频帧呈现缓冲：解码出的帧连同其「会话时间戳」(tsMs，
 *  发送端统一时间轴)入队，呈现侧按一个「呈现时钟」(同一时间轴的毫秒值)取「到点该显示的那一
 *  帧」。时钟源由上层决定——可用「本地墙钟锚定首帧时间戳 + 固定缓冲延迟」做平滑播放，也可用
 *  「音频播放进度时钟」做严格 A/V 同步——本调度器与时钟源解耦，只管调度。
 *
 *  调度策略（标准视频呈现）：给定呈现时钟 clockMs，
 *    - 取队列里 tsMs ≤ clockMs 的**最后一帧**作为当前应显示帧；
 *    - 它之前那些同样到点(tsMs ≤ clockMs)的更旧帧已过期，丢弃并计数（卡顿后追帧，避免积压
 *      一次性猛刷）；
 *    - tsMs > clockMs 的「未来帧」留在队列等待其到点；
 *    - 没有任何帧到点则返回 false（呈现侧维持上一帧）。
 *  队列上限保护：入队超过上限时丢弃最旧帧（防止呈现时钟长时间不前进导致无限堆积）。
 *
 *  模板参数 T 为帧载荷类型（实际用 BGRA/NV12 帧缓冲；单测用 int 帧号即可）。纯逻辑、不碰
 *  设备 / 线程，便于单测（AvSyncSchedulerTests.cpp，Suite AVS）。**非线程安全**：调用方
 *  须自行串行化 Push / PopDue（通话里约定都在视频呈现线程上调用）。
 *
 *  balloonwj@qq.com   2026-06-16
 */
#pragma once

#include <deque>
#include <utility>
#include <stddef.h>
#include <stdint.h>

namespace call {

template <typename T>
class AvSyncScheduler
{
public:
    /**
     *  @param maxQueue 队列帧数上限（>0）；超出时入队丢弃最旧帧。默认 8（720p@30fps 约 0.27s）。
     */
    explicit AvSyncScheduler(size_t maxQueue = 8)
        : m_max(maxQueue > 0 ? maxQueue : 1)
    {
    }

    /**
     *  入队一帧。约定 tsMs 随入队大致非减（视频按序到达）；即便偶有乱序也只影响该帧呈现时机。
     *  超过上限则先丢最旧帧再入队。
     *  @param tsMs    帧的会话时间戳（毫秒）
     *  @param payload 帧载荷（拷贝入队；大缓冲建议用 move 重载）
     */
    void Push(uint64_t tsMs, const T& payload)
    {
        if (m_q.size() >= m_max)
            m_q.pop_front();
        Entry e;
        e.tsMs = tsMs;
        e.payload = payload;
        m_q.push_back(e);
    }

    //入队一帧（move 版，避免大帧缓冲拷贝）。
    void Push(uint64_t tsMs, T&& payload)
    {
        if (m_q.size() >= m_max)
            m_q.pop_front();
        Entry e;
        e.tsMs = tsMs;
        e.payload = std::move(payload);
        m_q.push_back(e);
    }

    /**
     *  取「呈现时钟 clockMs 下应显示的那一帧」。
     *  @param clockMs     当前呈现时钟（与帧 tsMs 同一时间轴，毫秒）
     *  @param out         [出参] 应显示帧的载荷（仅返回 true 时有效；内部 move 出来）
     *  @param outTsMs     [出参] 该帧时间戳（仅返回 true 时有效）
     *  @param outDropped  [出参] 本次因过期被丢弃的更旧帧数（统计/日志用；无则 0）
     *  @return 有帧到点返回 true（out 有效，且该帧及更旧的都已出队）；否则 false（队列不变）
     */
    bool PopDue(uint64_t clockMs, T& out, uint64_t& outTsMs, int& outDropped)
    {
        outDropped = 0;
        //找最后一个 tsMs ≤ clockMs 的帧的下标
        int dueIdx = -1;
        size_t i;
        for (i = 0; i < m_q.size(); ++i)
        {
            if (m_q[i].tsMs <= clockMs)
                dueIdx = static_cast<int>(i);
            else
                break;   //tsMs 非减，遇到未来帧即可停
        }
        if (dueIdx < 0)
            return false;   //无帧到点，维持上一帧

        //dueIdx 之前的都是过期帧，丢弃计数
        outDropped = dueIdx;
        for (i = 0; i < static_cast<size_t>(dueIdx); ++i)
            m_q.pop_front();
        //此时队首即应显示帧
        out = std::move(m_q.front().payload);
        outTsMs = m_q.front().tsMs;
        m_q.pop_front();
        return true;
    }

    //清空队列（会话重置 / 异常时）。
    void Clear()
    {
        m_q.clear();
    }

    //当前缓冲帧数。
    size_t Size() const
    {
        return m_q.size();
    }

    //队首帧时间戳（Size()>0 时有效）；上层用它判断「最早一帧还要等多久」。
    uint64_t FrontTsMs() const
    {
        return m_q.empty() ? 0 : m_q.front().tsMs;
    }

private:
    struct Entry
    {
        uint64_t tsMs;
        T        payload;
    };

    std::deque<Entry> m_q;    //按到达序的帧队列（tsMs 大致非减）
    size_t            m_max;  //队列帧数上限
};

} // namespace call
