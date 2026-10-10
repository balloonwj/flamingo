/**
 *  屏幕共享房间里的语音，见 ShareAudioSession.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareAudioSession.h"

#include <windows.h>

#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"      // LOG_INFO / LOG_ERROR（须在 windows.h 之后）

namespace screenshare {

namespace {

//每个座位在混音器里占用的路号个数（声音轨编号都小于它）
const int kStreamsPerSeat = 8;
//本人在发言判定表里的键（真实路号都不小于 0）
const int kSelfDetectorKey = -1;
//发言判定：电平门限（均方根）与降到门限以下后的保持时间（毫秒）
const int kSpeakingThreshold = 600;
const int kSpeakingHoldMs = 400;
//声音帧的时间戳按采集帧序号 × 20 毫秒填写，单位微秒，与画面帧一致。2026-10-06 起接收端的抖动缓冲按它排队
//（docs/音视频通话方案.md 第 9.9.1 节）：麦克风取音频引擎给的采集帧序号（DTX 没发出的帧也计在内），电脑声音每处理
//一帧计一次
const uint64_t kAudioFrameUs = 20000;
//帧头时间戳换算成采样：每毫秒 48 个采样（48 kHz）、每毫秒 1000 微秒
const uint64_t kSamplesPerMs = 48;
const uint64_t kUsPerMs = 1000;
//电脑声音：内部采样率与每帧采样数（与麦克风相同的 48 kHz、20 ms）、码率（音乐需要更高的码率）
const int kSysRate = 48000;
const int kSysFrame = 960;
const int kSysBitrate = 64000;
//闪避：不闪避与压低 20 dB 时的增益百分比
const int kGainFull = 100;
const int kGainDucked = 10;
//百分比的基数与 16 位采样的上下限
const int kPercentBase = 100;
const int32_t kSampleMax = 32767;
const int32_t kSampleMin = -32768;

//是否声音轨
bool isAudioTrack(int track)
{
    return track == screenshare_track_mic || track == screenshare_track_system_audio;
}

} // namespace

ShareAudioSession::ShareAudioSession()
    : m_transport(NULL)
    , m_selfSeat(0)
    , m_sentFrames(0)
    , m_withoutDevices(false)
    , m_sysEnabled(false)
    , m_sysExcludingSelf(true)
    , m_sysFrames(0)
    , m_recordTap(NULL)
    , m_captionTap(NULL)
{
}

ShareAudioSession::~ShareAudioSession()
{
    Stop();
}

void ShareAudioSession::SetRecordTap(ShareAudioRecordTap* tap)
{
    m_recordTap.store(tap);
    //录制与字幕任一个需要时才让引擎交出处理后的声音
    const bool any = (tap != NULL) || (m_captionTap.load() != NULL);
    m_engine.SetPcmTap(any ? static_cast<call::CallAudioPcmTap*>(this) : NULL);
}

void ShareAudioSession::SetCaptionTap(ShareAudioRecordTap* tap)
{
    m_captionTap.store(tap);
    const bool any = (tap != NULL) || (m_recordTap.load() != NULL);
    m_engine.SetPcmTap(any ? static_cast<call::CallAudioPcmTap*>(this) : NULL);
}

void ShareAudioSession::OnCapturedPcm(const int16_t* mono, int samples)
{
    ShareAudioRecordTap* tap = m_recordTap.load();
    if (tap != NULL)
        tap->OnRoomAudio(share_audio_tap_mic, mono, samples);
    //实时字幕：本人麦克风送进本人的识别流
    ShareAudioRecordTap* captionTap = m_captionTap.load();
    if (captionTap != NULL)
        captionTap->OnRoomAudio(share_audio_tap_mic, mono, samples);
}

void ShareAudioSession::OnRenderedPcm(const int16_t* mono, int samples)
{
    ShareAudioRecordTap* tap = m_recordTap.load();
    if (tap != NULL)
        tap->OnRoomAudio(share_audio_tap_others, mono, samples);
}

int ShareAudioSession::StreamIdOf(int seat, int track)
{
    return seat * kStreamsPerSeat + track;
}

bool ShareAudioSession::Start(ShareAudioTransport* transport, int selfSeat, bool micOn, bool withoutDevices)
{
    if (transport == NULL)
        return false;
    Stop();
    m_transport = transport;
    m_selfSeat = selfSeat;
    m_sentFrames = 0;
    m_withoutDevices = withoutDevices;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_detectors.clear();
        m_knownSeats.clear();
    }
    //房间里的声音要做回声消除（扬声器放出的别人的声音会被自己的麦克风拾回）；无设备模式没有回声，不开
    const bool ok = withoutDevices ? m_engine.StartWithoutDevices(this, false, micOn)
                                   : m_engine.Start(this, true, micOn);
    if (!ok)
    {
        LOG_ERROR("screenshare audio: start failed, seat=%d, mic=%d.", selfSeat, micOn ? 1 : 0);
        m_transport = NULL;
        return false;
    }
    LOG_INFO("screenshare audio: started, seat=%d, mic=%d, devices=%d.", selfSeat, micOn ? 1 : 0,
             withoutDevices ? 0 : 1);
    return true;
}

void ShareAudioSession::Stop()
{
    stopSystemAudio();
    if (!m_engine.IsRunning())
        return;
    m_engine.Stop();
    m_transport = NULL;
    LOG_INFO("screenshare audio: stopped, seat=%d.", m_selfSeat);
}

bool ShareAudioSession::IsRunning() const
{
    return m_engine.IsRunning();
}

void ShareAudioSession::SetDtx(bool on)
{
    m_engine.SetDtx(on);
}

bool ShareAudioSession::SetMicEnabled(bool on)
{
    const bool ok = m_engine.SetCaptureEnabled(on);
    LOG_INFO("screenshare audio: mic %s, ok=%d.", on ? "on" : "off", ok ? 1 : 0);
    return ok;
}

bool ShareAudioSession::IsMicEnabled() const
{
    return m_engine.IsCaptureEnabled();
}

void ShareAudioSession::OnRemoteFrame(const ShareMediaFrame& frame)
{
    //只要别人的声音；服务端不会把本人的帧回送，这里再防一次
    if (!isAudioTrack(frame.m_track) || frame.m_senderSeat == m_selfSeat || frame.m_data.empty())
        return;
    //帧头时间戳（微秒，采集帧序号 × 20 毫秒）换算成采样，交给播放端的抖动缓冲排队（2026-10-06 起）
    const uint32_t tsSamples = static_cast<uint32_t>(frame.m_timestamp * kSamplesPerMs / kUsPerMs);
    m_engine.OnEncodedPacketFrom(StreamIdOf(frame.m_senderSeat, frame.m_track),
                                 reinterpret_cast<const unsigned char*>(frame.m_data.data()), frame.m_data.size(),
                                 tsSamples);
    std::lock_guard<std::mutex> guard(m_mutex);
    m_knownSeats[frame.m_senderSeat] = true;
}

void ShareAudioSession::RemoveSeat(int seat)
{
    m_engine.RemoveStream(StreamIdOf(seat, screenshare_track_mic));
    m_engine.RemoveStream(StreamIdOf(seat, screenshare_track_system_audio));
    std::lock_guard<std::mutex> guard(m_mutex);
    m_knownSeats.erase(seat);
    m_detectors.erase(StreamIdOf(seat, screenshare_track_mic));
}

std::vector<int> ShareAudioSession::UpdateSpeaking(int64_t nowMs)
{
    std::vector<int> talking;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<int, bool>::const_iterator it = m_knownSeats.begin(); it != m_knownSeats.end(); ++it)
    {
        //只看麦克风那一路：电脑声音（音乐、视频）不算说话
        const int id = StreamIdOf(it->first, screenshare_track_mic);
        int level = 0;
        if (!m_engine.GetStreamLevel(id, level))
            level = 0;
        std::map<int, call::SpeakingDetector>::iterator d = m_detectors.find(id);
        if (d == m_detectors.end())
            d = m_detectors.insert(std::make_pair(id, call::SpeakingDetector(kSpeakingThreshold, kSpeakingHoldMs))).first;
        if (d->second.Update(level, nowMs))
            talking.push_back(it->first);
    }
    std::map<int, call::SpeakingDetector>::iterator self = m_detectors.find(kSelfDetectorKey);
    if (self == m_detectors.end())
        self = m_detectors.insert(std::make_pair(kSelfDetectorKey,
                                                 call::SpeakingDetector(kSpeakingThreshold, kSpeakingHoldMs))).first;
    const int selfLevel = m_engine.IsCaptureEnabled() ? m_engine.CaptureLevel() : 0;
    if (self->second.Update(selfLevel, nowMs))
    {
        //本人座位按升序插进去
        std::vector<int>::iterator pos = talking.begin();
        while (pos != talking.end() && *pos < m_selfSeat)
            ++pos;
        talking.insert(pos, m_selfSeat);
    }
    return talking;
}

bool ShareAudioSession::SetSystemAudioEnabled(bool on)
{
    if (on == m_sysEnabled.load())
        return true;
    if (!on)
    {
        stopSystemAudio();
        LOG_INFO("screenshare audio: system audio off.");
        return true;
    }
    if (!m_engine.IsRunning())
        return false;
    //编码器在采集开始之前准备好；之后只在回环采集线程上使用
    if (!m_sysCodec.Init(kSysRate, kSysFrame, kSysBitrate, true))
        return false;
    m_sysAccum.clear();
    m_sysFrames = 0;
    if (m_withoutDevices)
    {
        m_sysEnabled.store(true);
        return true;
    }
    m_sysEnabled.store(true);
    if (!m_sysCapture.Start(this))
    {
        m_sysEnabled.store(false);
        m_sysCodec.Close();
        LOG_ERROR("screenshare audio: system audio capture unavailable.");
        return false;
    }
    m_sysExcludingSelf.store(m_sysCapture.IsExcludingSelf());
    LOG_INFO("screenshare audio: system audio on, exclude self=%d.", m_sysExcludingSelf.load() ? 1 : 0);
    return true;
}

bool ShareAudioSession::IsSystemAudioEnabled() const
{
    return m_sysEnabled.load();
}

bool ShareAudioSession::IsSystemAudioExcludingSelf() const
{
    return m_sysEnabled.load() && m_sysExcludingSelf.load();
}

void ShareAudioSession::stopSystemAudio()
{
    m_sysEnabled.store(false);
    //先停采集线程（join 之后不再有回调），再释放编码器
    m_sysCapture.Stop();
    m_sysCodec.Close();
    m_sysAccum.clear();
}

void ShareAudioSession::OnSystemAudio(const int16_t* mono, int samples)
{
    FeedSystemAudio(mono, samples);
}

void ShareAudioSession::FeedSystemAudio(const int16_t* pcm, int samples)
{
    ShareAudioTransport* transport = m_transport;
    if (!m_sysEnabled.load() || transport == NULL || pcm == NULL || samples <= 0)
        return;
    m_sysAccum.insert(m_sysAccum.end(), pcm, pcm + samples);
    std::vector<unsigned char> packet;
    while (static_cast<int>(m_sysAccum.size()) >= kSysFrame)
    {
        //退回普通回环时，别人在说话就把共享出去的电脑声音压低，减少把他们的声音再送出去
        const int gain = DuckingGainPercent(m_sysExcludingSelf.load(), othersSpeaking());
        if (gain != kGainFull)
            ApplyGainPercent(&m_sysAccum[0], kSysFrame, gain);
        //录制旁路：录的是送出去的那一份电脑声音
        ShareAudioRecordTap* tap = m_recordTap.load();
        if (tap != NULL)
            tap->OnRoomAudio(share_audio_tap_system, &m_sysAccum[0], kSysFrame);
        if (m_sysCodec.Encode(&m_sysAccum[0], kSysFrame, packet) && !packet.empty())
        {
            ShareFrameHeader header;
            header.m_track = screenshare_track_system_audio;
            header.m_senderSeat = m_selfSeat;
            header.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
            header.m_timestamp = m_sysFrames * kAudioFrameUs;
            transport->SendRoomBody(
                ShareFraming::encodeBody(header, reinterpret_cast<const char*>(&packet[0]), packet.size()));
        }
        //编码失败的帧也计数，时间戳与实际经过的时长保持一致
        ++m_sysFrames;
        m_sysAccum.erase(m_sysAccum.begin(), m_sysAccum.begin() + kSysFrame);
    }
}

void ShareAudioSession::SetSystemAudioModeForTest(bool excludingSelf)
{
    m_sysExcludingSelf.store(excludingSelf);
}

bool ShareAudioSession::othersSpeaking()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<int, bool>::const_iterator it = m_knownSeats.begin(); it != m_knownSeats.end(); ++it)
    {
        int level = 0;
        if (m_engine.GetStreamLevel(StreamIdOf(it->first, screenshare_track_mic), level) &&
            level >= kSpeakingThreshold)
            return true;
    }
    return false;
}

int ShareAudioSession::DuckingGainPercent(bool excludingSelf, bool othersSpeaking)
{
    if (excludingSelf || !othersSpeaking)
        return kGainFull;
    return kGainDucked;
}

void ShareAudioSession::ApplyGainPercent(int16_t* pcm, int samples, int percent)
{
    if (pcm == NULL)
        return;
    for (int i = 0; i < samples; ++i)
    {
        int32_t v = static_cast<int32_t>(pcm[i]) * percent / kPercentBase;
        if (v > kSampleMax)
            v = kSampleMax;
        else if (v < kSampleMin)
            v = kSampleMin;
        pcm[i] = static_cast<int16_t>(v);
    }
}

void ShareAudioSession::FeedMic(const int16_t* pcm, int samples)
{
    m_engine.FeedCapture(pcm, samples);
}

void ShareAudioSession::PullPlayback(int16_t* pcm, int samples)
{
    m_engine.PullRender(pcm, samples);
}

void ShareAudioSession::OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex)
{
    ShareAudioTransport* transport = m_transport;
    if (transport == NULL || data == NULL || len == 0)
        return;
    ShareFrameHeader header;
    header.m_track = screenshare_track_mic;
    header.m_senderSeat = m_selfSeat;
    header.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    header.m_timestamp = frameIndex * kAudioFrameUs;
    ++m_sentFrames;
    transport->SendRoomBody(ShareFraming::encodeBody(header, reinterpret_cast<const char*>(data), len));
}

} // namespace screenshare
