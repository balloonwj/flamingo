/**
 *  远程桌面控制端的 H.264 收帧队列，RdVideoFrameQueue.h
 *
 *  2026-10-15（docs/远程桌面H264与共享画质提升方案.md 4.7）：控制端的接收线程把 4 号视频帧放进有上限的队列，解码线程从队列里取。
 *  H.264 的增量帧依赖前一帧，不能随意丢：队列满了（解码跟不上）时清空，丢到下一个关键帧，并要求调用方请求一次关键帧
 *  （经 RdKeyRequestLimiter 限频）；到的恰好是关键帧时只留它，不用请求。
 *  Push 在接收线程、Pop 在解码线程上调用，内部以互斥量保护。单元测试见 Source/Tests/RdVideoNegotiationTests.cpp（RDV6）。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#pragma once

#include <stddef.h>
#include <deque>
#include <mutex>
#include <vector>

namespace remotedesktop {

//控制端收帧队列的默认上限（帧）：30 fps 下约半秒的积压
const size_t kRdVideoQueueMaxFrames = 15;

//控制端的 H.264 收帧队列
class RdVideoFrameQueue
{
public:
    /**
     *  @param maxFrames 队列上限（帧），不小于 1
     */
    explicit RdVideoFrameQueue(size_t maxFrames);

    /**
     *  放入一帧（接收线程）。正在等关键帧时增量帧直接丢弃；队列满时清空：本帧是关键帧就只留它，否则开始等关键帧。
     *  @param frame    整个 4 号引擎帧（拷贝保存）
     *  @param keyframe 是否关键帧
     *  @return 这一次开始等关键帧、调用方应当请求一次关键帧时返回 true
     */
    bool Push(const std::vector<unsigned char>& frame, bool keyframe);

    /**
     *  取出最早的一帧（解码线程）。
     *  @param frame [出参] 引擎帧
     *  @return 队列为空返回 false
     */
    bool Pop(std::vector<unsigned char>& frame);

    //当前帧数
    size_t Size() const;

    //清空并不再等关键帧（会话结束、换了格式）
    void Clear();

    //是否在等关键帧（溢出之后、关键帧到达之前）：调用方据此在关键帧请求被限频挡下后再补发
    bool WaitingForKeyframe() const;

private:
    size_t                                  m_maxFrames;   //上限（帧）
    mutable std::mutex                      m_mutex;       //保护以下两项
    std::deque<std::vector<unsigned char> > m_frames;      //排队的帧，最早的在前
    bool                                    m_waitKey;     //是否在等关键帧（期间丢弃增量帧）
};

} // namespace remotedesktop
