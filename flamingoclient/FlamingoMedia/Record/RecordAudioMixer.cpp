/**
 *  屏幕录制的混音，见 RecordAudioMixer.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "RecordAudioMixer.h"

namespace screenrecord {

namespace {

//每路缓冲的容量：2 秒的 48 kHz 单声道
const size_t kBufferSamples = 48000 * 2;
//16 位采样的上下限
const int kSampleMax = 32767;
const int kSampleMin = -32768;

} // namespace

RecordAudioMixer::RecordAudioMixer()
    : m_tap(NULL)
{
    for (int i = 0; i < record_audio_source_count; ++i)
    {
        m_buffers[i].reset(new call::PcmRingBuffer(kBufferSamples));
        m_enabled[i].store(false);
        m_peaks[i].store(0);
    }
}

bool RecordAudioMixer::valid(int source) const
{
    return source >= 0 && source < record_audio_source_count;
}

void RecordAudioMixer::SetEnabled(int source, bool on)
{
    if (!valid(source))
        return;
    m_enabled[source].store(on);
    if (!on)
        m_buffers[source]->Clear();
}

bool RecordAudioMixer::IsEnabled(int source) const
{
    return valid(source) && m_enabled[source].load();
}

void RecordAudioMixer::Push(int source, const int16_t* pcm, size_t samples)
{
    if (!valid(source) || pcm == NULL || samples == 0 || !m_enabled[source].load())
        return;
    m_buffers[source]->Push(pcm, samples);
    //峰值（第 24 期录制条的电平）：取这一段的最大绝对值，与上次取出以来记下的峰值比较后保留较大者
    int peak = 0;
    for (size_t i = 0; i < samples; ++i)
    {
        const int v = pcm[i] < 0 ? -static_cast<int>(pcm[i]) : static_cast<int>(pcm[i]);
        if (v > peak)
            peak = v;
    }
    //别的线程可能同时在写或取出：比较交换失败时 old 已更新为最新值，再比一次
    int old = m_peaks[source].load();
    while (peak > old)
    {
        if (m_peaks[source].compare_exchange_weak(old, peak))
            break;
    }
}

int RecordAudioMixer::TakePeak(int source)
{
    if (!valid(source))
        return 0;
    return m_peaks[source].exchange(0);
}

void RecordAudioMixer::SetTap(IRecordAudioTap* tap)
{
    m_tap.store(tap);
}

void RecordAudioMixer::Pull(int16_t* out, size_t samples, int64_t ptsMs)
{
    if (out == NULL || samples == 0)
        return;
    std::vector<int> sum(samples, 0);
    m_scratch.resize(samples);
    IRecordAudioTap* tap = (ptsMs >= 0) ? m_tap.load() : NULL;
    for (int i = 0; i < record_audio_source_count; ++i)
    {
        if (!m_enabled[i].load())
            continue;
        const size_t got = m_buffers[i]->Pop(&m_scratch[0], samples);
        for (size_t k = 0; k < got; ++k)
            sum[k] += m_scratch[k];
        //交给分路旁路：不够的部分补静音，旁路看到的时间轴与录像一致
        if (tap != NULL)
        {
            for (size_t k = got; k < samples; ++k)
                m_scratch[k] = 0;
            tap->OnRecordAudio(i, &m_scratch[0], samples, ptsMs);
        }
    }
    for (size_t k = 0; k < samples; ++k)
    {
        int v = sum[k];
        if (v > kSampleMax)
            v = kSampleMax;
        if (v < kSampleMin)
            v = kSampleMin;
        out[k] = static_cast<int16_t>(v);
    }
}

size_t RecordAudioMixer::Available(int source) const
{
    if (!valid(source))
        return 0;
    return m_buffers[source]->Available();
}

void RecordAudioMixer::Clear()
{
    for (int i = 0; i < record_audio_source_count; ++i)
        m_buffers[i]->Clear();
}

} // namespace screenrecord
