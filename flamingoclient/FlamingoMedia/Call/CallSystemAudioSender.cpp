/**
 *  通话中共享电脑声音的发送部分，说明见 CallSystemAudioSender.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CallSystemAudioSender.h"

#include "CallNetStats.h"                 //kCallReportFlagRecvSystemAudio：对方报告里的能力位
#include "../Share/ShareAudioSession.h"   //DuckingGainPercent / ApplyGainPercent：沿用屏幕共享的闪避规则

namespace call {

namespace {

//原样的增益（百分比）：不需要调整音量
const int kGainFull = 100;

} // namespace

int CallSystemAudioGainPercent(bool excludingSelf, int peerLevel)
{
    return screenshare::ShareAudioSession::DuckingGainPercent(excludingSelf, peerLevel >= kCallPeerSpeakingLevel);
}

int CallSystemAudioPrecheck(bool connected, uint32_t peerReports, uint8_t peerFlags)
{
    if (!connected)
        return call_sysaudio_not_connected;
    //对方的报告每秒一份；一份都没收到时无从知道对方的版本，按不支持处理（接通约 1 秒后即可打开）
    if (peerReports == 0 || (peerFlags & kCallReportFlagRecvSystemAudio) == 0)
        return call_sysaudio_peer_unsupported;
    return call_sysaudio_ok;
}

CallSystemAudioSender::CallSystemAudioSender()
    : m_sink(NULL)
    , m_frames(0)
    , m_running(false)
{
}

bool CallSystemAudioSender::Start(CallSystemAudioPacketSink* sink)
{
    std::lock_guard<std::mutex> guard(m_mtx);
    m_codec.Close();
    m_accum.clear();
    m_frames = 0;
    m_running = false;
    m_sink = NULL;
    if (sink == NULL)
        return false;
    //音乐模式：电脑声音多是音乐与视频的声音，语音模式会把它当人声处理、音质明显变差
    if (!m_codec.Init(kSampleRate, kFrameSamples, kBitrateBps, true))
        return false;
    m_sink = sink;
    m_running = true;
    return true;
}

void CallSystemAudioSender::Stop()
{
    std::lock_guard<std::mutex> guard(m_mtx);
    m_running = false;
    m_sink = NULL;
    m_accum.clear();
    m_codec.Close();
}

bool CallSystemAudioSender::IsRunning() const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_running;
}

int CallSystemAudioSender::Feed(const int16_t* pcm, int samples, int gainPercent)
{
    std::lock_guard<std::mutex> guard(m_mtx);
    if (!m_running || m_sink == NULL || pcm == NULL || samples <= 0)
        return 0;
    m_accum.insert(m_accum.end(), pcm, pcm + samples);
    int frames = 0;
    size_t offset = 0;
    while (m_accum.size() - offset >= static_cast<size_t>(kFrameSamples))
    {
        int16_t* frame = &m_accum[offset];
        if (gainPercent != kGainFull)
            screenshare::ShareAudioSession::ApplyGainPercent(frame, kFrameSamples, gainPercent);
        const uint64_t tsMs = m_frames * kFrameMs;
        if (m_codec.Encode(frame, kFrameSamples, m_packet) && !m_packet.empty())
            m_sink->OnSystemAudioPacket(&m_packet[0], m_packet.size(), tsMs);
        //编码失败的帧也计数，时间戳与实际经过的时长保持一致
        ++m_frames;
        ++frames;
        offset += kFrameSamples;
    }
    if (offset > 0)
        m_accum.erase(m_accum.begin(), m_accum.begin() + offset);
    return frames;
}

uint64_t CallSystemAudioSender::FramesSent() const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_frames;
}

} // namespace call
