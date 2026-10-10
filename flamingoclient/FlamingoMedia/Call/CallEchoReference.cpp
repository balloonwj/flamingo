/**
 *  回声消除的参考信号，说明见 CallEchoReference.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CallEchoReference.h"

#include <string.h>   //memset

namespace call {

namespace {

//16 位采样的上下限：两路相加后限幅
const int32_t kSampleMax = 32767;
const int32_t kSampleMin = -32768;

} // namespace

CallEchoReference::CallEchoReference(size_t farCapacity)
    : m_far(farCapacity)
    , m_system(kMaxSystemSamples)
{
}

void CallEchoReference::PushFar(const int16_t* pcm, size_t samples)
{
    if (pcm == NULL || samples == 0)
        return;
    m_far.Push(pcm, samples);
}

void CallEchoReference::PushSystem(const int16_t* pcm, size_t samples)
{
    if (pcm == NULL || samples == 0)
        return;
    std::lock_guard<std::mutex> guard(m_systemPushMtx);
    //一次给的比上限还多：只留最新的 kMaxSystemSamples 个
    if (samples > kMaxSystemSamples)
    {
        pcm += samples - kMaxSystemSamples;
        samples = kMaxSystemSamples;
    }
    //放进去会超过上限：先丢掉最旧的那部分，让参考信号跟上麦克风
    const size_t available = m_system.Available();
    if (available + samples > kMaxSystemSamples)
    {
        const size_t drop = available + samples - kMaxSystemSamples;
        m_dropScratch.resize(drop);
        m_system.Pop(&m_dropScratch[0], drop);
    }
    m_system.Push(pcm, samples);
}

void CallEchoReference::Pop(int16_t* out, size_t samples)
{
    if (out == NULL || samples == 0)
        return;
    //播放混音：不够的部分补 0
    const size_t got = m_far.Pop(out, samples);
    if (got < samples)
        memset(out + got, 0, (samples - got) * sizeof(int16_t));

    //电脑声音：有多少加多少，逐采样相加后限幅
    if (m_system.Available() == 0)
        return;
    m_popScratch.resize(samples);
    const size_t sys = m_system.Pop(&m_popScratch[0], samples);
    for (size_t i = 0; i < sys; ++i)
    {
        int32_t v = static_cast<int32_t>(out[i]) + m_popScratch[i];
        if (v > kSampleMax)
            v = kSampleMax;
        else if (v < kSampleMin)
            v = kSampleMin;
        out[i] = static_cast<int16_t>(v);
    }
}

void CallEchoReference::ClearSystem()
{
    std::lock_guard<std::mutex> guard(m_systemPushMtx);
    m_system.Clear();
}

void CallEchoReference::Clear()
{
    m_far.Clear();
    ClearSystem();
}

size_t CallEchoReference::SystemAvailable() const
{
    return m_system.Available();
}

} // namespace call
