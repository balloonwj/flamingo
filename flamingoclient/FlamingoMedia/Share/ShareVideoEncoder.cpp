/**
 *  屏幕共享的画面编码器，ShareVideoEncoder.cpp
 *  说明见 ShareVideoEncoder.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareVideoEncoder.h"

#include "ShareVideoPolicy.h"
#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

namespace {

//编码器的时间戳单位是 100 纳秒，屏幕共享内部用微秒
const int64_t kHnsPerUs = 10;
//关键帧间隔，单位帧：兜底关键帧由发帧节奏强制，编码器自己的间隔设得比它长，避免多出来的关键帧
const int kEncoderGopFrames = 600;

} // namespace

ShareVideoEncoder::ShareVideoEncoder()
    : m_srcW(0)
    , m_srcH(0)
    , m_encW(0)
    , m_encH(0)
    , m_sink(NULL)
    , m_timestampUs(0)
    , m_lastRebuild(false)
    , m_bitrateBps(kShareBitrateBps)
    , m_useHw(false)
    , m_hwAllowed(true)
    , m_hwFailed(false)
    , m_hwRebuilt(false)
{
}

ShareVideoEncoder::~ShareVideoEncoder()
{
    Close();
}

bool ShareVideoEncoder::Encode(const ShareCaptureFrame& frame, bool keyframe, uint64_t timestampUs,
                               ShareVideoEncoderSink* sink)
{
    if (sink == NULL || frame.m_data == NULL || frame.m_width <= 0 || frame.m_height <= 0)
        return false;

    m_lastRebuild = m_hwRebuilt;
    m_hwRebuilt = false;
    if (!encoderReady() || frame.m_width != m_srcW || frame.m_height != m_srcH)
    {
        int w = 0;
        int h = 0;
        ComputeShareEncodeSize(frame.m_width, frame.m_height, kShareMaxLongSide, w, h);
        if (!openEncoder(w, h))
        {
            LOG_ERROR("screenshare encoder: init %dx%d failed.", w, h);
            return false;
        }
        LOG_INFO("screenshare encoder: source %dx%d, encode %dx%d.", frame.m_width, frame.m_height, w, h);
        m_srcW = frame.m_width;
        m_srcH = frame.m_height;
        m_encW = w;
        m_encH = h;
        m_nv12.resize(static_cast<size_t>(w) * h * 3 / 2);
        m_lastRebuild = true;
        keyframe = true;
    }

    ConvertBgraToNv12(frame.m_data, frame.m_stride, frame.m_width, frame.m_height, &m_nv12[0], m_encW, m_encH);
    if (keyframe)
    {
        if (m_useHw)
            m_hw.ForceKeyFrame();
        else
            m_encoder.ForceKeyFrame();
    }
    m_sink = sink;
    m_timestampUs = timestampUs;
    const int64_t ptsHns = static_cast<int64_t>(timestampUs) * kHnsPerUs;
    if (m_useHw && !m_hw.EncodeNv12(&m_nv12[0], m_encW, ptsHns, this))
    {
        //硬件编码器出错（多为驱动问题或卡住超时）：本次推流之后只用软件编码器，这一帧改用软件编码器编成关键帧
        LOG_WARNING("screenshare encoder: hardware encoder failed, fall back to software.");
        m_hw.Close();
        m_useHw = false;
        m_hwFailed = true;
        if (!m_encoder.Init(m_encW, m_encH, kShareMaxFps, m_bitrateBps, kEncoderGopFrames))
        {
            LOG_ERROR("screenshare encoder: software fallback init %dx%d failed.", m_encW, m_encH);
            m_sink = NULL;
            return false;
        }
        m_lastRebuild = true;
        m_encoder.ForceKeyFrame();
        m_encoder.EncodeNv12(&m_nv12[0], m_encW, ptsHns, this);
    }
    else if (!m_useHw)
    {
        m_encoder.EncodeNv12(&m_nv12[0], m_encW, ptsHns, this);
    }
    m_sink = NULL;
    return true;
}

bool ShareVideoEncoder::openEncoder(int w, int h)
{
    m_hw.Close();
    m_encoder.Close();
    m_useHw = false;
    if (m_hwAllowed && !m_hwFailed)
    {
        if (m_hw.Init(w, h, kShareMaxFps, m_bitrateBps, kEncoderGopFrames))
        {
            m_useHw = true;
            LOG_INFO("screenshare encoder: hardware encoder %S.", m_hw.Name().c_str());
            return true;
        }
        //没有可用的硬件编码器：本次推流之后不再尝试，免得每次换尺寸都枚举一遍
        m_hwFailed = true;
        LOG_INFO("screenshare encoder: no usable hardware encoder, use software.");
    }
    return m_encoder.Init(w, h, kShareMaxFps, m_bitrateBps, kEncoderGopFrames);
}

bool ShareVideoEncoder::encoderReady() const
{
    return m_useHw ? m_hw.IsReady() : m_encoder.IsReady();
}

void ShareVideoEncoder::Close()
{
    m_hw.Close();
    m_useHw = false;
    //下一次推流重新尝试硬件编码器
    m_hwFailed = false;
    m_encoder.Close();
    m_srcW = 0;
    m_srcH = 0;
    m_encW = 0;
    m_encH = 0;
}

int ShareVideoEncoder::EncodeWidth() const
{
    return m_encW;
}

int ShareVideoEncoder::EncodeHeight() const
{
    return m_encH;
}

bool ShareVideoEncoder::LastWasRebuild() const
{
    return m_lastRebuild;
}

void ShareVideoEncoder::SetBitrate(int bitrateBps)
{
    if (bitrateBps <= 0)
        return;
    m_bitrateBps = bitrateBps;
    if (m_useHw)
    {
        //硬件编码器按新码率重建（重建后第一帧是关键帧）；重建失败时下一次 Encode 发现编码器没建好，会重新打开编码器
        m_hwRebuilt = m_hw.SetBitrate(bitrateBps);
    }
    else if (m_encoder.IsReady())
        m_encoder.SetBitrate(bitrateBps);
}

void ShareVideoEncoder::SetHardwareAllowed(bool allowed)
{
    m_hwAllowed = allowed;
}

bool ShareVideoEncoder::IsHardware() const
{
    return m_useHw && m_hw.IsReady();
}

void ShareVideoEncoder::OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns)
{
    if (m_sink == NULL)
        return;
    //硬件编码器的码流可能是上一帧的（在这一次 Encode 等输入时才交出），时间戳按码流自带的换算；
    //软件编码器是同步的，沿用这一帧的时间戳
    const uint64_t ts = m_useHw ? static_cast<uint64_t>(ptsHns / kHnsPerUs) : m_timestampUs;
    m_sink->OnShareEncoded(data, len, keyframe, ts);
}

} // namespace screenshare
