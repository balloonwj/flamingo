/**
 *  屏幕共享房间中转的慢观看者丢帧策略，RoomForwardPolicy.cpp
 *  规则说明见 RoomForwardPolicy.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "RoomForwardPolicy.h"

#include "../chatserversrc/Protocol.h"

//类内已给初值的静态常量在这里补上定义：doctest 的断言按引用捕获操作数，g++ 下缺了定义会链接失败
const size_t RoomForwardPolicy::kDefaultViewerBufferCapBytes;
const int64_t RoomForwardPolicy::kKeyFrameRequestIntervalMs;

RoomVideoStream::RoomVideoStream(int senderSeat, int track)
    : m_senderSeat(senderSeat)
    , m_track(track)
{
}

bool RoomVideoStream::operator<(const RoomVideoStream& rhs) const
{
    if (m_senderSeat != rhs.m_senderSeat)
        return m_senderSeat < rhs.m_senderSeat;
    return m_track < rhs.m_track;
}

bool RoomVideoStream::operator==(const RoomVideoStream& rhs) const
{
    return m_senderSeat == rhs.m_senderSeat && m_track == rhs.m_track;
}

RoomForwardPolicy::ReceiverState::ReceiverState()
    : m_waitingKeyFrame(false)
    , m_droppingFrame(false)
{
}

RoomForwardPolicy::RoomForwardPolicy(size_t viewerBufferCapBytes)
    : m_bufferCapBytes(viewerBufferCapBytes == 0 ? kDefaultViewerBufferCapBytes : viewerBufferCapBytes)
{
}

int RoomForwardPolicy::decide(int receiverSeat, const RoomRelayFrameHeader& header, size_t receiverPendingBytes,
                              int64_t nowMs, bool& outRequestKeyFrame)
{
    outRequestKeyFrame = false;

    //音频、标注、控制帧一律不丢
    if (!RoomRelayFrame::isVideoTrack(header.m_track))
        return room_forward_send;

    const RoomVideoStream stream(header.m_senderSeat, header.m_track);
    m_knownStreams.insert(stream);

    //订阅过滤（群通话第 5 期）：接收者发过订阅帧之后，没订阅的座位的摄像头画面一律不发给他
    if (header.m_track == screenshare_track_camera && !isSubscribed(receiverSeat, header.m_senderSeat))
        return room_forward_drop;
    ReceiverState& state = m_receiverStates[ReceiverStreamKey(receiverSeat, stream)];

    //非首分片跟随本帧首分片的决定，不会把一帧截成半截
    if ((header.m_flags & screenshare_flag_first_frag) == 0)
        return state.m_droppingFrame ? room_forward_drop : room_forward_send;

    //关键帧一律照发，并结束「等关键帧」状态
    if ((header.m_flags & screenshare_flag_keyframe) != 0)
    {
        state.m_waitingKeyFrame = false;
        state.m_droppingFrame = false;
        return room_forward_send;
    }

    //积压超过上限：进入「等关键帧」状态，并按限频向这路视频的发送者请求关键帧
    if (!state.m_waitingKeyFrame && receiverPendingBytes > m_bufferCapBytes)
    {
        state.m_waitingKeyFrame = true;
        std::map<RoomVideoStream, int64_t>::const_iterator last = m_lastRequestMs.find(stream);
        if (last == m_lastRequestMs.end() || nowMs - last->second >= kKeyFrameRequestIntervalMs)
        {
            m_lastRequestMs[stream] = nowMs;
            outRequestKeyFrame = true;
        }
    }

    state.m_droppingFrame = state.m_waitingKeyFrame;
    return state.m_waitingKeyFrame ? room_forward_drop : room_forward_send;
}

void RoomForwardPolicy::noteFrame(const RoomRelayFrameHeader& header)
{
    if (RoomRelayFrame::isVideoTrack(header.m_track))
        m_knownStreams.insert(RoomVideoStream(header.m_senderSeat, header.m_track));
}

void RoomForwardPolicy::onMemberJoined(int64_t nowMs, std::vector<RoomVideoStream>& outRequests)
{
    //新成员需要一个关键帧才能开始解码：每路已知视频都请求一次，不受限频约束
    outRequests.assign(m_knownStreams.begin(), m_knownStreams.end());
    for (size_t i = 0; i < outRequests.size(); ++i)
        m_lastRequestMs[outRequests[i]] = nowMs;
}

void RoomForwardPolicy::setSubscription(int receiverSeat, const std::vector<int>& seats, int64_t nowMs,
                                        std::vector<RoomVideoStream>& outRequests)
{
    outRequests.clear();
    const std::set<int> wanted(seats.begin(), seats.end());
    std::map<int, std::set<int> >::const_iterator old = m_subscriptions.find(receiverSeat);
    for (std::set<int>::const_iterator it = wanted.begin(); it != wanted.end(); ++it)
    {
        //原来没发过订阅帧的接收者本来就收全部画面，不算新订阅
        const bool wasReceiving = (old == m_subscriptions.end()) || (old->second.find(*it) != old->second.end());
        if (wasReceiving)
            continue;
        //新订阅的画面：先等关键帧，免得发给他缺参考帧的非关键帧；这路画面在发的话立即请求关键帧
        const RoomVideoStream stream(*it, screenshare_track_camera);
        ReceiverState& state = m_receiverStates[ReceiverStreamKey(receiverSeat, stream)];
        state.m_waitingKeyFrame = true;
        state.m_droppingFrame = true;
        if (m_knownStreams.find(stream) != m_knownStreams.end())
        {
            m_lastRequestMs[stream] = nowMs;
            outRequests.push_back(stream);
        }
    }
    m_subscriptions[receiverSeat] = wanted;
}

bool RoomForwardPolicy::isSubscribed(int receiverSeat, int senderSeat) const
{
    std::map<int, std::set<int> >::const_iterator it = m_subscriptions.find(receiverSeat);
    if (it == m_subscriptions.end())
        return true;
    return it->second.find(senderSeat) != it->second.end();
}

void RoomForwardPolicy::onMemberLeft(int seat)
{
    //他的订阅、以及别人订阅里的他都清掉：座位可能被新成员复用，别人收到房间状态后会重新订阅
    m_subscriptions.erase(seat);
    for (std::map<int, std::set<int> >::iterator it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it)
        it->second.erase(seat);

    //以他为接收者、或以他为发送者的状态全部清掉
    for (std::map<ReceiverStreamKey, ReceiverState>::iterator it = m_receiverStates.begin();
         it != m_receiverStates.end();)
    {
        if (it->first.first == seat || it->first.second.m_senderSeat == seat)
            m_receiverStates.erase(it++);
        else
            ++it;
    }
    for (std::set<RoomVideoStream>::iterator it = m_knownStreams.begin(); it != m_knownStreams.end();)
    {
        if (it->m_senderSeat == seat)
            m_knownStreams.erase(it++);
        else
            ++it;
    }
    for (std::map<RoomVideoStream, int64_t>::iterator it = m_lastRequestMs.begin(); it != m_lastRequestMs.end();)
    {
        if (it->first.m_senderSeat == seat)
            m_lastRequestMs.erase(it++);
        else
            ++it;
    }
}
