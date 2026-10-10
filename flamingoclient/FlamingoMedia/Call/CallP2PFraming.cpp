/**
 *  一对一通话在 P2P 数据报通道上的消息格式与丢包处理的实现，见 CallP2PFraming.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "CallP2PFraming.h"

namespace call {

namespace {

//一个字节的位数，多字节字段按网络字节序拼装
const int kBitsPerByte = 8;

//取一个字节的掩码
const uint64_t kByteMask = 0xFF;

//头部各字段的偏移（字节）
const size_t kOffsetKind = 0;
const size_t kOffsetFlags = 1;
const size_t kOffsetSeq = 2;
const size_t kOffsetTs = 6;

//序号与时间戳的字节数
const int kSeqBytes = 4;
const int kTsBytes = 8;

//按网络字节序写 / 读 n 字节无符号整数
void PutUint(uint64_t value, int bytes, std::vector<uint8_t>& out)
{
    for (int i = bytes - 1; i >= 0; --i)
        out.push_back((uint8_t)((value >> (i * kBitsPerByte)) & kByteMask));
}

uint64_t GetUint(const uint8_t* p, int bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < bytes; ++i)
        value = (value << kBitsPerByte) | p[i];
    return value;
}

//序号差：按 32 位回绕计算，正数表示 a 在 b 之后
int32_t SeqDiff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

} // namespace

void BuildCallP2PMessage(int kind, uint8_t flags, uint32_t seq, uint64_t tsMs, const uint8_t* data, size_t len,
                         std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(kCallP2PHeaderBytes + len);
    out.push_back((uint8_t)kind);
    out.push_back(flags);
    PutUint(seq, kSeqBytes, out);
    PutUint(tsMs, kTsBytes, out);
    if (data != NULL && len > 0)
        out.insert(out.end(), data, data + len);
}

bool ParseCallP2PMessage(const uint8_t* data, size_t len, CallP2PMessage& out)
{
    if (data == NULL || len < kCallP2PHeaderBytes)
        return false;
    const int kind = data[kOffsetKind];
    if (kind != call_p2p_kind_audio && kind != call_p2p_kind_video && kind != call_p2p_kind_keyframe_request
        && kind != call_p2p_kind_stats && kind != call_p2p_kind_system_audio)
        return false;
    out.m_kind = kind;
    out.m_flags = data[kOffsetFlags];
    out.m_seq = (uint32_t)GetUint(data + kOffsetSeq, kSeqBytes);
    out.m_tsMs = GetUint(data + kOffsetTs, kTsBytes);
    out.m_payloadLen = len - kCallP2PHeaderBytes;
    out.m_payload = (out.m_payloadLen > 0) ? data + kCallP2PHeaderBytes : NULL;
    return true;
}

// ======================== CallAudioSeqTracker ========================

CallAudioSeqTracker::CallAudioSeqTracker()
    : m_started(false)
    , m_next(0)
{
}

void CallAudioSeqTracker::Reset()
{
    m_started = false;
    m_next = 0;
}

bool CallAudioSeqTracker::OnPacket(uint32_t seq, int& lostOut)
{
    lostOut = 0;
    if (!m_started)
    {
        m_started = true;
        m_next = seq + 1;
        return true;
    }
    const int32_t diff = SeqDiff(seq, m_next);
    //迟到或重复：它对应的时段已经补偿过（或已经播过），丢弃
    if (diff < 0)
        return false;
    lostOut = (diff > kCallMaxConcealFrames) ? kCallMaxConcealFrames : (int)diff;
    m_next = seq + 1;
    return true;
}

// ======================== CallVideoLossGuard ========================

CallVideoLossGuard::CallVideoLossGuard()
    : m_started(false)
    , m_next(0)
    , m_waitingKey(true)
    , m_requested(false)
    , m_lastRequestMs(0)
    , m_dropped(0)
{
}

void CallVideoLossGuard::Reset()
{
    m_started = false;
    m_next = 0;
    m_waitingKey = true;
    m_requested = false;
    m_lastRequestMs = 0;
    m_dropped = 0;
}

int CallVideoLossGuard::OnFrame(uint32_t seq, bool keyframe, int64_t nowMs, bool& requestKeyframe)
{
    requestKeyframe = false;
    if (m_started)
    {
        const int32_t diff = SeqDiff(seq, m_next);
        //迟到或重复：丢弃，不影响等关键帧的状态
        if (diff < 0)
        {
            ++m_dropped;
            return video_drop;
        }
        //断档：中间丢了帧，后面的非关键帧解不对了
        if (diff > 0)
            m_waitingKey = true;
    }
    m_started = true;
    m_next = seq + 1;

    if (!m_waitingKey)
        return video_decode;
    if (keyframe)
    {
        m_waitingKey = false;
        m_requested = false;
        return video_decode;
    }
    ++m_dropped;
    requestKeyframe = DueRequest(nowMs);
    return video_drop;
}

bool CallVideoLossGuard::PollRequest(int64_t nowMs)
{
    //还没收到过任何帧时不请求：对方可能还没开摄像头（语音通话、或刚切到视频）
    if (!m_started || !m_waitingKey)
        return false;
    return DueRequest(nowMs);
}

bool CallVideoLossGuard::DueRequest(int64_t nowMs)
{
    if (m_requested && nowMs - m_lastRequestMs < kCallKeyframeRequestIntervalMs)
        return false;
    m_requested = true;
    m_lastRequestMs = nowMs;
    return true;
}

// ======================== CallKeyframeForceLimiter ========================

CallKeyframeForceLimiter::CallKeyframeForceLimiter()
    : m_forced(false)
    , m_lastForcedMs(0)
{
}

void CallKeyframeForceLimiter::Reset()
{
    m_forced = false;
    m_lastForcedMs = 0;
}

bool CallKeyframeForceLimiter::Allow(int64_t nowMs)
{
    if (m_forced && nowMs - m_lastForcedMs < kCallKeyframeForceMinIntervalMs)
        return false;
    m_forced = true;
    m_lastForcedMs = nowMs;
    return true;
}

} // namespace call
