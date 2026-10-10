/**
 *  多路语音的解码、抖动缓冲与混音，见 AudioStreamMixer.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "AudioStreamMixer.h"

#include <math.h>

#include "OpusAudioCodec.h"
#include "AudioPcmUtils.h"
#include "CallJitterBuffer.h"

namespace call {

namespace {

//每路抖动缓冲的容量：1 秒的采样（与一对一通话原来的抖动缓冲相同）
const int kJitterSeconds = 1;
//16 位采样的上下限
const int32_t kSampleMax = 32767;
const int32_t kSampleMin = -32768;

} // namespace

AudioStreamStats::AudioStreamStats()
    : m_plcFrames(0)
    , m_silenceFrames(0)
{
}

AudioStreamMixer::PendingChunk::PendingChunk()
    : m_pos(0)
    , m_concealed(false)
{
}

AudioStreamMixer::Stream::Stream()
    : m_encoded(false)
    , m_missingRun(0)
    , m_decodedAny(false)
    , m_hasExpect(false)
    , m_expectTs(0)
    , m_lastSample(0)
    , m_level(0)
    , m_requested(0)
    , m_concealed(0)
    , m_plcFrames(0)
    , m_silenceFrames(0)
{
}

AudioStreamMixer::Stream::~Stream()
{
}

AudioStreamMixer::AudioStreamMixer(int sampleRate, int frameSamples)
    : m_sampleRate(sampleRate)
    , m_frameSamples(frameSamples)
{
}

AudioStreamMixer::~AudioStreamMixer()
{
}

std::shared_ptr<AudioStreamMixer::Stream> AudioStreamMixer::streamFor(int streamId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::iterator it = m_streams.find(streamId);
    if (it != m_streams.end())
        return it->second;
    std::shared_ptr<Stream> s(new Stream());
    s->m_jitter.reset(new PcmRingBuffer(static_cast<size_t>(m_sampleRate) * kJitterSeconds));
    s->m_packets.reset(new CallJitterBuffer());
    m_streams[streamId] = s;
    return s;
}

bool AudioStreamMixer::PushEncoded(int streamId, const unsigned char* data, size_t len, uint32_t tsSamples)
{
    if (data == NULL || len == 0)
        return false;
    std::shared_ptr<Stream> s = streamFor(streamId);
    s->m_packets->Put(data, len, tsSamples);
    s->m_encoded.store(true);
    return true;
}

void AudioStreamMixer::produceChunk(Stream& s)
{
    CallJbFrame f;
    s.m_packets->GetFrame(f);
    //解码器只在渲染线程上使用，第一次需要时创建（码率参数对解码无影响，传 0 用默认值）
    if (!s.m_decoder)
    {
        std::unique_ptr<OpusAudioCodec> codec(new OpusAudioCodec());
        if (codec->Init(m_sampleRate, m_frameSamples, 0))
            s.m_decoder = std::move(codec);
    }

    PendingChunk chunk;
    if (f.m_result == call_jb_packet)
    {
        std::vector<int16_t> pcm;
        if (s.m_decoder && s.m_decoder->Decode(&f.m_data[0], f.m_data.size(), pcm) && !pcm.empty())
        {
            //时间戳跳变（speexdsp 为降延迟跳过了帧，或时间线重置）：新帧开头从上一个采样平滑过渡过来，避免咔哒声
            if (s.m_hasExpect && f.m_timestamp != s.m_expectTs)
            {
                const size_t n = (pcm.size() < static_cast<size_t>(kSmoothSamples)) ? pcm.size()
                                                                                    : static_cast<size_t>(kSmoothSamples);
                for (size_t i = 0; i < n; ++i)
                {
                    const double w = static_cast<double>(i + 1) / static_cast<double>(n + 1);
                    pcm[i] = static_cast<int16_t>(s.m_lastSample * (1.0 - w) + pcm[i] * w);
                }
            }
            chunk.m_pcm.swap(pcm);
            s.m_decodedAny = true;
        }
        else
        {
            //解码失败：按缺失补静音
            chunk.m_pcm.assign(static_cast<size_t>(f.m_span), 0);
            chunk.m_concealed = true;
        }
        s.m_missingRun = 0;
        s.m_hasExpect = true;
        s.m_expectTs = f.m_timestamp + static_cast<uint32_t>(f.m_span);
    }
    else
    {
        //缺失或插入：按帧补。解码过包、且连续缺失没超过 kMaxConcealFrames 帧时用丢包补偿，否则补静音
        chunk.m_concealed = true;
        int remaining = f.m_span;
        while (remaining > 0)
        {
            std::vector<int16_t> pcm;
            const bool plc = s.m_decodedAny && s.m_decoder && s.m_missingRun < kMaxConcealFrames
                             && s.m_decoder->Decode(NULL, 0, pcm) && !pcm.empty();
            if (plc)
                ++s.m_plcFrames;
            else
            {
                pcm.assign(static_cast<size_t>(m_frameSamples), 0);
                ++s.m_silenceFrames;
            }
            if (static_cast<int>(pcm.size()) > remaining)
                pcm.resize(static_cast<size_t>(remaining));
            chunk.m_pcm.insert(chunk.m_pcm.end(), pcm.begin(), pcm.end());
            remaining -= static_cast<int>(pcm.size());
            ++s.m_missingRun;
        }
        //缺失时播放指针照常前进，下一个包应有的时间戳随之后移；插入时指针不动
        if (f.m_result == call_jb_missing && s.m_hasExpect)
            s.m_expectTs += static_cast<uint32_t>(f.m_span);
    }
    if (!chunk.m_pcm.empty())
        s.m_lastSample = chunk.m_pcm.back();
    s.m_pending.push_back(chunk);
}

void AudioStreamMixer::takeFromPackets(Stream& s, int16_t* dst, int frames, size_t& concealed, double& realEnergy)
{
    concealed = 0;
    realEnergy = 0;
    size_t filled = 0;
    const size_t want = static_cast<size_t>(frames);
    while (filled < want)
    {
        if (s.m_pending.empty())
            produceChunk(s);
        PendingChunk& c = s.m_pending.front();
        const size_t left = c.m_pcm.size() - c.m_pos;
        const size_t n = (want - filled < left) ? (want - filled) : left;
        for (size_t i = 0; i < n; ++i)
        {
            const int16_t v = c.m_pcm[c.m_pos + i];
            dst[filled + i] = v;
            if (!c.m_concealed)
                realEnergy += static_cast<double>(v) * v;
        }
        if (c.m_concealed)
            concealed += n;
        c.m_pos += n;
        filled += n;
        if (c.m_pos >= c.m_pcm.size())
            s.m_pending.pop_front();
    }
}

void AudioStreamMixer::PushPcm(int streamId, const int16_t* pcm, size_t count)
{
    if (pcm == NULL || count == 0)
        return;
    std::shared_ptr<Stream> s = streamFor(streamId);
    s->m_jitter->Push(pcm, count);
}

void AudioStreamMixer::Mix(int16_t* out, int frames)
{
    if (out == NULL || frames <= 0)
        return;
    //先在锁内拷一份路表，锁外逐路取采样，避免渲染线程长时间持锁
    std::vector<std::pair<int, std::shared_ptr<Stream> > > streams;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        streams.assign(m_streams.begin(), m_streams.end());
    }
    m_sum.assign(static_cast<size_t>(frames), 0);
    m_take.resize(static_cast<size_t>(frames));
    std::vector<int> levels(streams.size(), 0);
    std::vector<size_t> gots(streams.size(), 0);
    for (size_t i = 0; i < streams.size(); ++i)
    {
        Stream& s = *streams[i].second;
        size_t got = 0;
        double energy = 0;
        if (s.m_encoded.load())
        {
            //编码包这一路：取满 frames 个（缺的部分已由丢包补偿或静音补上），gots 记「真实取到的」个数；
            //电平只算真正解码出来的部分
            size_t concealedHere = 0;
            takeFromPackets(s, &m_take[0], frames, concealedHere, energy);
            got = static_cast<size_t>(frames);
            gots[i] = static_cast<size_t>(frames) - concealedHere;
            for (size_t k = 0; k < got; ++k)
                m_sum[k] += m_take[k];
        }
        else
        {
            got = s.m_jitter->Pop(&m_take[0], static_cast<size_t>(frames));
            gots[i] = got;
            for (size_t k = 0; k < got; ++k)
            {
                m_sum[k] += m_take[k];
                energy += static_cast<double>(m_take[k]) * m_take[k];
            }
        }
        //电平按整帧算：取不到、补出来的部分按静音计入
        levels[i] = static_cast<int>(sqrt(energy / frames));
    }
    for (int k = 0; k < frames; ++k)
    {
        int32_t v = m_sum[static_cast<size_t>(k)];
        if (v > kSampleMax)
            v = kSampleMax;
        else if (v < kSampleMin)
            v = kSampleMin;
        out[k] = static_cast<int16_t>(v);
    }
    std::lock_guard<std::mutex> guard(m_mutex);
    for (size_t i = 0; i < streams.size(); ++i)
    {
        Stream& s = *streams[i].second;
        s.m_level = levels[i];
        //播放计数：本次请求 frames 个，取不到的部分补了静音
        s.m_requested += static_cast<uint64_t>(frames);
        s.m_concealed += static_cast<uint64_t>(frames) - gots[i];
    }
}

bool AudioStreamMixer::GetPlayoutCounters(int streamId, uint64_t& requested, uint64_t& concealed) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.find(streamId);
    if (it == m_streams.end())
        return false;
    requested = it->second->m_requested;
    concealed = it->second->m_concealed;
    return true;
}

void AudioStreamMixer::RemoveStream(int streamId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_streams.erase(streamId);
}

void AudioStreamMixer::Clear()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_streams.clear();
}

bool AudioStreamMixer::GetLevel(int streamId, int& rms) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.find(streamId);
    if (it == m_streams.end())
        return false;
    rms = it->second->m_level;
    return true;
}

bool AudioStreamMixer::GetStreamStats(int streamId, AudioStreamStats& out) const
{
    std::shared_ptr<Stream> s;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.find(streamId);
        if (it == m_streams.end())
            return false;
        s = it->second;
    }
    out.m_jb = s->m_packets->Stats();
    out.m_plcFrames = s->m_plcFrames.load();
    out.m_silenceFrames = s->m_silenceFrames.load();
    return true;
}

std::vector<int> AudioStreamMixer::StreamIds() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<int> ids;
    for (std::map<int, std::shared_ptr<Stream> >::const_iterator it = m_streams.begin(); it != m_streams.end(); ++it)
        ids.push_back(it->first);
    return ids;
}

SpeakingDetector::SpeakingDetector(int threshold, int holdMs)
    : m_threshold(threshold)
    , m_holdMs(holdMs)
    , m_lastLoudMs(-1)
    , m_speaking(false)
{
}

bool SpeakingDetector::Update(int rms, int64_t nowMs)
{
    if (rms >= m_threshold)
        m_lastLoudMs = nowMs;
    m_speaking = (m_lastLoudMs >= 0 && nowMs - m_lastLoudMs <= m_holdMs);
    return m_speaking;
}

bool SpeakingDetector::IsSpeaking() const
{
    return m_speaking;
}

} // namespace call
