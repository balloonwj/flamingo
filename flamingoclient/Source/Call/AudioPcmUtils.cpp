/**
 *  音频 PCM 工具实现，AudioPcmUtils.cpp
 *  balloonwj@qq.com   2026-06-12
 */
#include "AudioPcmUtils.h"

#include <string.h>

namespace call {

//int16 取值边界。
static const int kInt16Max = 32767;
static const int kInt16Min = -32768;
//浮点转整型的缩放系数（满量程 1.0 对应 32768）。
static const float kFloatToIntScale = 32768.0f;

void FloatToInt16(const float* in, int16_t* out, size_t count)
{
    if (in == NULL || out == NULL)
        return;
    for (size_t i = 0; i < count; ++i)
    {
        //先缩放再夹取，避免超幅样本回绕成相反极性的杂音
        int v = static_cast<int>(in[i] * kFloatToIntScale);
        if (v > kInt16Max)
            v = kInt16Max;
        else if (v < kInt16Min)
            v = kInt16Min;
        out[i] = static_cast<int16_t>(v);
    }
}

void Int16ToFloat(const int16_t* in, float* out, size_t count)
{
    if (in == NULL || out == NULL)
        return;
    for (size_t i = 0; i < count; ++i)
        out[i] = static_cast<float>(in[i]) / kFloatToIntScale;
}

void DownmixStereoToMonoF32(const float* interleaved, float* mono, size_t frames)
{
    if (interleaved == NULL || mono == NULL)
        return;
    for (size_t i = 0; i < frames; ++i)
    {
        float l = interleaved[i * 2];
        float r = interleaved[i * 2 + 1];
        mono[i] = (l + r) * 0.5f;
    }
}

void DownmixStereoToMonoI16(const int16_t* interleaved, int16_t* mono, size_t frames)
{
    if (interleaved == NULL || mono == NULL)
        return;
    for (size_t i = 0; i < frames; ++i)
    {
        //用 int32 求和再取半，防两路接近满幅时溢出
        int sum = static_cast<int>(interleaved[i * 2]) + static_cast<int>(interleaved[i * 2 + 1]);
        mono[i] = static_cast<int16_t>(sum / 2);
    }
}

void MonoToStereoI16(const int16_t* mono, int16_t* interleaved, size_t frames)
{
    if (mono == NULL || interleaved == NULL)
        return;
    for (size_t i = 0; i < frames; ++i)
    {
        interleaved[i * 2] = mono[i];
        interleaved[i * 2 + 1] = mono[i];
    }
}

//==== PcmRingBuffer ====

PcmRingBuffer::PcmRingBuffer(size_t capacitySamples)
    : m_head(0)
    , m_tail(0)
    , m_count(0)
{
    //至少留 1 容量，避免 0 容量导致取模除零
    if (capacitySamples == 0)
        capacitySamples = 1;
    m_buf.resize(capacitySamples);
}

size_t PcmRingBuffer::Push(const int16_t* data, size_t count)
{
    if (data == NULL || count == 0)
        return 0;

    std::lock_guard<std::mutex> guard(m_mtx);
    size_t cap = m_buf.size();
    size_t freeSpace = cap - m_count;
    size_t toWrite = (count < freeSpace) ? count : freeSpace;
    for (size_t i = 0; i < toWrite; ++i)
    {
        m_buf[m_tail] = data[i];
        m_tail = (m_tail + 1) % cap;
    }
    m_count += toWrite;
    return toWrite;
}

size_t PcmRingBuffer::Pop(int16_t* out, size_t count)
{
    if (out == NULL || count == 0)
        return 0;

    std::lock_guard<std::mutex> guard(m_mtx);
    size_t cap = m_buf.size();
    size_t toRead = (count < m_count) ? count : m_count;
    for (size_t i = 0; i < toRead; ++i)
    {
        out[i] = m_buf[m_head];
        m_head = (m_head + 1) % cap;
    }
    m_count -= toRead;
    return toRead;
}

size_t PcmRingBuffer::Available() const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_count;
}

size_t PcmRingBuffer::Capacity() const
{
    //m_buf 大小即容量（构造时确定，运行期不变），无需加锁
    return m_buf.size();
}

void PcmRingBuffer::Clear()
{
    std::lock_guard<std::mutex> guard(m_mtx);
    m_head = 0;
    m_tail = 0;
    m_count = 0;
}

} // namespace call
