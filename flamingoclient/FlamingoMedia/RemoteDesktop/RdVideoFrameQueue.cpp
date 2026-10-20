/**
 *  远程桌面控制端的 H.264 收帧队列，见 RdVideoFrameQueue.h。
 *  balloonwj@qq.com   2026-10-15
 */
#include "RdVideoFrameQueue.h"

namespace remotedesktop {

RdVideoFrameQueue::RdVideoFrameQueue(size_t maxFrames)
    : m_maxFrames(maxFrames < 1 ? 1 : maxFrames)
    , m_waitKey(false)
{
}

bool RdVideoFrameQueue::Push(const std::vector<unsigned char>& frame, bool keyframe)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //等关键帧期间：增量帧丢弃，关键帧到了就恢复
    if (m_waitKey && !keyframe)
        return false;
    m_waitKey = false;
    if (m_frames.size() >= m_maxFrames)
    {
        //解码跟不上：清空。本帧是关键帧就从它重新开始，否则丢到下一个关键帧并请求一次
        m_frames.clear();
        if (!keyframe)
        {
            m_waitKey = true;
            return true;
        }
    }
    m_frames.push_back(frame);
    return false;
}

bool RdVideoFrameQueue::Pop(std::vector<unsigned char>& frame)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_frames.empty())
        return false;
    frame.swap(m_frames.front());
    m_frames.pop_front();
    return true;
}

size_t RdVideoFrameQueue::Size() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_frames.size();
}

void RdVideoFrameQueue::Clear()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_frames.clear();
    m_waitKey = false;
}

bool RdVideoFrameQueue::WaitingForKeyframe() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_waitKey;
}

} // namespace remotedesktop
