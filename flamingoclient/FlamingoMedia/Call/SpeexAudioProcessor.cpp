/**
 *  通话采集端音频处理（回声消除 + 降噪）实现，SpeexAudioProcessor.cpp
 *  balloonwj@qq.com   2026-06-12
 */
#include "SpeexAudioProcessor.h"

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <string.h>

namespace call {

SpeexAudioProcessor::SpeexAudioProcessor()
    : m_echo(NULL)
    , m_preprocess(NULL)
    , m_frameSamples(0)
{
}

SpeexAudioProcessor::~SpeexAudioProcessor()
{
    Close();
}

bool SpeexAudioProcessor::Init(int sampleRate, int frameSamples, int filterLengthSamples)
{
    Close();

    if (sampleRate <= 0 || frameSamples <= 0 || filterLengthSamples <= 0)
        return false;

    m_echo = speex_echo_state_init(frameSamples, filterLengthSamples);
    if (m_echo == NULL)
    {
        Close();
        return false;
    }
    //告诉回声消除器采样率（影响其内部滤波器自适应）
    int sr = sampleRate;
    speex_echo_ctl(m_echo, SPEEX_ECHO_SET_SAMPLING_RATE, &sr);

    m_preprocess = speex_preprocess_state_init(frameSamples, sampleRate);
    if (m_preprocess == NULL)
    {
        Close();
        return false;
    }
    //把回声状态交给预处理器，使其做残余回声抑制
    speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_ECHO_STATE, m_echo);
    //显式开启降噪（speexdsp 默认即开，这里确保）
    int denoiseOn = 1;
    speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_DENOISE, &denoiseOn);

    m_frameSamples = frameSamples;
    return true;
}

void SpeexAudioProcessor::Close()
{
    if (m_preprocess != NULL)
    {
        speex_preprocess_state_destroy(m_preprocess);
        m_preprocess = NULL;
    }
    if (m_echo != NULL)
    {
        speex_echo_state_destroy(m_echo);
        m_echo = NULL;
    }
    m_frameSamples = 0;
}

void SpeexAudioProcessor::ProcessCapture(const int16_t* mic, const int16_t* ref, int16_t* out, int frames)
{
    //未就绪 / 帧长不符：尽量不丢音，原样拷贝麦克风帧
    if (m_echo == NULL || m_preprocess == NULL || mic == NULL || out == NULL || frames != m_frameSamples)
    {
        if (mic != NULL && out != NULL && frames > 0)
            memcpy(out, mic, static_cast<size_t>(frames) * sizeof(int16_t));
        return;
    }

    if (ref != NULL)
    {
        //有参考：消回声（rec=mic 近端，play=ref 远端，out=去回声后）
        speex_echo_cancellation(m_echo, mic, ref, out);
    }
    else
    {
        //无参考：不消回声，仅把麦克风帧拷给降噪
        memcpy(out, mic, static_cast<size_t>(frames) * sizeof(int16_t));
    }

    //降噪 / 残余回声抑制（就地处理 out）
    speex_preprocess_run(m_preprocess, out);
}

} // namespace call
