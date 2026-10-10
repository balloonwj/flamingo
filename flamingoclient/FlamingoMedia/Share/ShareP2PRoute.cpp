/**
 *  单聊屏幕共享走 P2P 时发起人一侧要接管的服务端职责的实现，见 ShareP2PRoute.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "ShareP2PRoute.h"

#include "ShareFraming.h"

namespace screenshare {

namespace {

//帧头里发送者座位号的偏移（字节）
const size_t kSenderSeatOffset = 1;

//座位号的最大值（一个字节）
const int kMaxSeat = 255;

} // namespace

bool ShareRewriteSenderSeat(std::string& body, int seat)
{
    if (body.size() < kShareFrameHeaderSize || seat < 0 || seat > kMaxSeat)
        return false;
    body[kSenderSeatOffset] = static_cast<char>(seat);
    return true;
}

ShareP2PVideoGate::ShareP2PVideoGate()
    : m_waitingKey(false)
    , m_requested(false)
    , m_lastRequestMs(0)
    , m_dropped(0)
{
}

void ShareP2PVideoGate::Reset()
{
    m_waitingKey = false;
    m_requested = false;
    m_lastRequestMs = 0;
    m_dropped = 0;
}

bool ShareP2PVideoGate::OnVideoFrame(bool keyframe, uint64_t queuedBytes, int64_t nowMs, bool& requestKey)
{
    requestKey = false;
    //关键帧一律照发，并结束等关键帧
    if (keyframe)
    {
        m_waitingKey = false;
        m_requested = false;
        return true;
    }
    if (!m_waitingKey && queuedBytes > kShareP2PViewerBacklogBytes)
        m_waitingKey = true;
    if (!m_waitingKey)
        return true;
    ++m_dropped;
    if (!m_requested || nowMs - m_lastRequestMs >= kShareP2PKeyRequestIntervalMs)
    {
        m_requested = true;
        m_lastRequestMs = nowMs;
        requestKey = true;
    }
    return false;
}

bool ShareHostP2PAllowed(size_t memberCount, bool cloudRecording)
{
    //发起人加一名观看者，正好两人
    const size_t kHostAndOneViewer = 2;
    return memberCount == kHostAndOneViewer && !cloudRecording;
}

} // namespace screenshare
