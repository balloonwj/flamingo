/**
 *  语音识别前的声音处理，实现见 CaptionAudio.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionAudio.h"

#include <math.h>

namespace caption {

namespace {

//FIR 的阶数（系数个数），奇数使滤波器对称、群延迟为整数个采样
const int kTapCount = 47;
//截止频率（Hz），留出到 8 kHz 奈奎斯特频率的过渡带
const double kCutoffHz = 7000.0;
//16 位采样转浮点的除数
const float kPcmScale = 32768.0f;
//圆周率
const double kPi = 3.14159265358979323846;
//汉明窗的两个系数
const double kHammingA = 0.54;
const double kHammingB = 0.46;
//判断两段是否首尾相接时允许的误差（毫秒）
const int64_t kContinuityToleranceMs = 2;
//每秒的毫秒数
const int64_t kMsPerSecond = 1000;

} // namespace

CaptionResampler::CaptionResampler()
    : m_pos(0)
{
    m_taps.resize(kTapCount);
    const double fc = kCutoffHz / kInputSampleRate;
    const int mid = kTapCount / 2;
    double sum = 0.0;
    for (int i = 0; i < kTapCount; ++i)
    {
        const int k = i - mid;
        const double sinc = (k == 0) ? 2.0 * fc : sin(2.0 * kPi * fc * k) / (kPi * k);
        const double window = kHammingA - kHammingB * cos(2.0 * kPi * i / (kTapCount - 1));
        m_taps[i] = static_cast<float>(sinc * window);
        sum += m_taps[i];
    }
    for (int i = 0; i < kTapCount; ++i)
        m_taps[i] = static_cast<float>(m_taps[i] / sum);
    Reset();
}

void CaptionResampler::Reset()
{
    //先放 kTapCount - 1 个 0 作历史，使第一个输入采样就能产出输出，输出个数 = ceil(输入个数 / 3)
    m_buf.assign(kTapCount - 1, 0.0f);
    m_pos = kTapCount - 1;
}

void CaptionResampler::Process(const int16_t* in, size_t samples, std::vector<float>& out)
{
    for (size_t i = 0; i < samples; ++i)
        m_buf.push_back(in[i] / kPcmScale);
    while (m_pos < m_buf.size())
    {
        float acc = 0.0f;
        for (int j = 0; j < kTapCount; ++j)
            acc += m_taps[j] * m_buf[m_pos - j];
        out.push_back(acc);
        m_pos += kDecimation;
    }
    //只留滤波器需要的历史
    const size_t keepFrom = m_pos - (kTapCount - 1);
    if (keepFrom > 0)
    {
        m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<ptrdiff_t>(keepFrom));
        m_pos -= keepFrom;
    }
}

CaptionTimeMap::CaptionTimeMap()
{
}

void CaptionTimeMap::Append(int64_t firstSample, int64_t ptsMs, size_t samples)
{
    if (samples == 0)
        return;
    if (!m_segments.empty())
    {
        Segment& last = m_segments.back();
        const int64_t expectedSample = last.m_firstSample + last.m_samples;
        const int64_t expectedPts = last.m_ptsMs + last.m_samples * kMsPerSecond / kModelSampleRate;
        const int64_t diff = ptsMs - expectedPts;
        if (firstSample == expectedSample && diff <= kContinuityToleranceMs && diff >= -kContinuityToleranceMs)
        {
            last.m_samples += static_cast<int64_t>(samples);
            return;
        }
    }
    Segment s;
    s.m_firstSample = firstSample;
    s.m_ptsMs = ptsMs;
    s.m_samples = static_cast<int64_t>(samples);
    m_segments.push_back(s);
}

int64_t CaptionTimeMap::ToPts(int64_t sample) const
{
    if (m_segments.empty())
        return 0;
    //从后往前找第一个起点不晚于 sample 的段
    for (size_t i = m_segments.size(); i > 0; --i)
    {
        const Segment& s = m_segments[i - 1];
        if (s.m_firstSample <= sample)
            return s.m_ptsMs + (sample - s.m_firstSample) * kMsPerSecond / kModelSampleRate;
    }
    const Segment& first = m_segments.front();
    return first.m_ptsMs - (first.m_firstSample - sample) * kMsPerSecond / kModelSampleRate;
}

void CaptionTimeMap::DropBefore(int64_t sample)
{
    while (m_segments.size() > 1)
    {
        const Segment& s = m_segments.front();
        if (s.m_firstSample + s.m_samples > sample)
            break;
        m_segments.pop_front();
    }
}

void CaptionTimeMap::Clear()
{
    m_segments.clear();
}

size_t CaptionTimeMap::SegmentCount() const
{
    return m_segments.size();
}

} // namespace caption
