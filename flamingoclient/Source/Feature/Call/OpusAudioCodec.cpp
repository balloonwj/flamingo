/**
 *  Opus 语音编解码薄封装实现，OpusAudioCodec.cpp
 *  balloonwj@qq.com   2026-06-12
 */
#include "OpusAudioCodec.h"

#include "opus.h"   //Opus C 接口（opus_encoder_* / opus_decoder_* / opus_encode / opus_decode）

namespace call {

//单帧 Opus 包的最大字节数上限缓冲：Opus 单帧理论上限约 1275 字节，取 4000 留足余量。
static const int kMaxPacketBytes = 4000;

OpusAudioCodec::OpusAudioCodec()
    : m_enc(NULL)
    , m_dec(NULL)
    , m_sampleRate(0)
    , m_frameSamples(0)
{
}

OpusAudioCodec::~OpusAudioCodec()
{
    Close();
}

bool OpusAudioCodec::Init(int sampleRate, int frameSamples, int bitrateBps)
{
    //幂等：先清掉上一对编解码器
    Close();

    if (sampleRate <= 0 || frameSamples <= 0)
        return false;

    int err = OPUS_OK;
    //单声道、VOIP 应用（为语音优化）
    m_enc = opus_encoder_create(sampleRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || m_enc == NULL)
    {
        Close();
        return false;
    }

    //设目标码率（<=0 则不设、用 Opus 默认）
    if (bitrateBps > 0)
        opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(bitrateBps));

    m_dec = opus_decoder_create(sampleRate, 1, &err);
    if (err != OPUS_OK || m_dec == NULL)
    {
        Close();
        return false;
    }

    m_sampleRate = sampleRate;
    m_frameSamples = frameSamples;
    return true;
}

void OpusAudioCodec::Close()
{
    if (m_enc != NULL)
    {
        opus_encoder_destroy(m_enc);
        m_enc = NULL;
    }
    if (m_dec != NULL)
    {
        opus_decoder_destroy(m_dec);
        m_dec = NULL;
    }
    m_sampleRate = 0;
    m_frameSamples = 0;
}

bool OpusAudioCodec::Encode(const int16_t* pcm, int numSamples, std::vector<unsigned char>& out)
{
    if (m_enc == NULL || pcm == NULL)
        return false;
    //帧长必须与初始化时一致（Opus 只接受其合法帧长；本封装固定一种）
    if (numSamples != m_frameSamples)
        return false;

    unsigned char buf[kMaxPacketBytes];
    int n = opus_encode(m_enc, pcm, m_frameSamples, buf, kMaxPacketBytes);
    if (n < 0)
        return false;   //Opus 编码出错（n 为负的错误码）

    out.assign(buf, buf + n);
    return true;
}

bool OpusAudioCodec::Decode(const unsigned char* data, size_t len, std::vector<int16_t>& outPcm)
{
    if (m_dec == NULL)
        return false;

    //预留一帧的空间；data==NULL/len==0 时 opus_decode 走丢包隐藏(PLC)填一帧
    outPcm.resize(static_cast<size_t>(m_frameSamples));
    int samples = opus_decode(m_dec, data, static_cast<opus_int32>(len),
                              outPcm.data(), m_frameSamples, 0);
    if (samples < 0)
    {
        outPcm.clear();
        return false;   //Opus 解码出错
    }

    //回填实际解出的采样数（正常等于 frameSamples）
    outPcm.resize(static_cast<size_t>(samples));
    return true;
}

} // namespace call
