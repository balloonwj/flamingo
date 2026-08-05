/**
 *  单声道音频重采样实现，SpeexResampler.cpp
 *  balloonwj@qq.com   2026-06-12
 */
#include "SpeexResampler.h"

#include <speex/speex_resampler.h>

namespace call {

//输出缓冲容量余量（采样）：按比例估算输出后再多留一点，确保一次调用能容下全部输出。
static const int kOutCapMargin = 16;

SpeexResampler::SpeexResampler()
    : m_st(NULL)
    , m_inRate(0)
    , m_outRate(0)
{
}

SpeexResampler::~SpeexResampler()
{
    Close();
}

bool SpeexResampler::Init(int inRate, int outRate, int quality)
{
    Close();

    if (inRate <= 0 || outRate <= 0)
        return false;

    int err = 0;
    m_st = speex_resampler_init(1, static_cast<spx_uint32_t>(inRate),
                               static_cast<spx_uint32_t>(outRate), quality, &err);
    if (m_st == NULL || err != RESAMPLER_ERR_SUCCESS)
    {
        Close();
        return false;
    }

    m_inRate = inRate;
    m_outRate = outRate;
    return true;
}

void SpeexResampler::Close()
{
    if (m_st != NULL)
    {
        speex_resampler_destroy(m_st);
        m_st = NULL;
    }
    m_inRate = 0;
    m_outRate = 0;
}

void SpeexResampler::Process(const int16_t* in, int inCount, std::vector<int16_t>& outAppend)
{
    if (m_st == NULL || in == NULL || inCount <= 0)
        return;

    //输出容量估算：inCount × outRate / inRate，再加余量，确保一次 process 容下全部输出
    size_t cap = static_cast<size_t>(inCount) * static_cast<size_t>(m_outRate) / static_cast<size_t>(m_inRate)
               + kOutCapMargin;
    size_t base = outAppend.size();
    outAppend.resize(base + cap);

    spx_uint32_t inLen = static_cast<spx_uint32_t>(inCount);
    spx_uint32_t outLen = static_cast<spx_uint32_t>(cap);
    speex_resampler_process_int(m_st, 0, in, &inLen, &outAppend[base], &outLen);

    //回缩到实际产出的样本数
    outAppend.resize(base + outLen);
}

} // namespace call
