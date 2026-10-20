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
    , m_params(ShareVideoParamsForQuality(kShareQualityDefault))
    , m_paramsChanged(false)
    , m_maxBps(0)
    , m_minBps(0)
    , m_rangeChanged(false)
    , m_capLongSide(0)
    , m_openLimitForTest(0)
{
    m_bitrateBps = m_params.m_baseBps;
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
    //编码器还没建、源尺寸变了、或编码参数变了（共享中改了清晰度）：按新的源尺寸与参数重建，这一帧编成关键帧
    if (!encoderReady() || m_paramsChanged || frame.m_width != m_srcW || frame.m_height != m_srcH)
    {
        if (!rebuildFor(frame.m_width, frame.m_height))
            return false;
        m_srcW = frame.m_width;
        m_srcH = frame.m_height;
        m_lastRebuild = true;
        keyframe = true;
    }

    //缩放缓冲在转换器里按尺寸复用（2026-10-15 起，原来每帧临时分配；4K 源缩到 2K 时每帧约 14 MB）
    m_converter.Convert(frame.m_data, frame.m_stride, frame.m_width, frame.m_height, &m_nv12[0], m_encW, m_encH);
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
        if (!m_encoder.Init(m_encW, m_encH, m_params.m_fps, m_bitrateBps, kEncoderGopFrames))
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
    //单元测试模拟编码器不接受大尺寸
    const int longSide = (w > h) ? w : h;
    if (m_openLimitForTest > 0 && longSide > m_openLimitForTest)
        return false;
    if (m_hwAllowed && !m_hwFailed)
    {
        if (m_hw.Init(w, h, m_params.m_fps, m_bitrateBps, kEncoderGopFrames))
        {
            m_useHw = true;
            LOG_INFO("screenshare encoder: hardware encoder %S.", m_hw.Name().c_str());
            return true;
        }
        //没有可用的硬件编码器：本次推流之后不再尝试，免得每次换尺寸都枚举一遍。长边超过 1920 时建不起来可能只是
        //尺寸太大（驱动不接受），不记为失败，退到小尺寸时还会再试硬件编码器
        if (longSide <= kShareMaxLongSide)
            m_hwFailed = true;
        LOG_INFO("screenshare encoder: no usable hardware encoder for %dx%d, use software.", w, h);
    }
    return m_encoder.Init(w, h, m_params.m_fps, m_bitrateBps, kEncoderGopFrames);
}

bool ShareVideoEncoder::rebuildFor(int srcW, int srcH)
{
    m_paramsChanged = false;
    ScreenVideoParams params = m_params;
    //本次推流里已经退过档：不再尝试更大的尺寸
    if (m_capLongSide > 0 && (params.m_maxLongSide <= 0 || params.m_maxLongSide > m_capLongSide))
        params.m_maxLongSide = m_capLongSide;
    for (;;)
    {
        ScreenVideoProfile profile;
        ComputeScreenVideoProfile(srcW, srcH, params, profile);
        if (profile.m_encW <= 0 || profile.m_encH <= 0)
            return false;
        //码率范围变了：当前码率按「当前 / 旧上限」的比例换到新范围，避免网络正拥塞时一换尺寸就跳回最高码率
        if (profile.m_maxBps != m_maxBps || profile.m_minBps != m_minBps)
        {
            m_bitrateBps = ScaleBitrateToRange(m_bitrateBps, m_maxBps, profile.m_maxBps, profile.m_minBps);
            m_maxBps = profile.m_maxBps;
            m_minBps = profile.m_minBps;
            m_rangeChanged = true;
        }
        if (openEncoder(profile.m_encW, profile.m_encH))
        {
            LOG_INFO("screenshare encoder: source %dx%d, encode %dx%d, max long side %d, %d fps, bitrate %d (range %d-%d).",
                     srcW, srcH, profile.m_encW, profile.m_encH, params.m_maxLongSide, params.m_fps, m_bitrateBps,
                     m_minBps, m_maxBps);
            m_encW = profile.m_encW;
            m_encH = profile.m_encH;
            m_nv12.resize(static_cast<size_t>(m_encW) * m_encH * 3 / 2);
            return true;
        }
        const int longSide = (profile.m_encW > profile.m_encH) ? profile.m_encW : profile.m_encH;
        if (longSide <= kShareMaxLongSide)
        {
            LOG_ERROR("screenshare encoder: init %dx%d failed.", profile.m_encW, profile.m_encH);
            return false;
        }
        //按这个尺寸建不起来（系统或驱动不接受大尺寸）：退一档（3840 → 2560 → 1920），本次推流里记住
        params.m_maxLongSide = (longSide > kShare2kLongSide) ? kShare2kLongSide : kShareMaxLongSide;
        m_capLongSide = params.m_maxLongSide;
        LOG_WARNING("screenshare encoder: init %dx%d failed, step down to max long side %d.", profile.m_encW,
                    profile.m_encH, params.m_maxLongSide);
    }
}

void ShareVideoEncoder::SetParams(const ScreenVideoParams& params)
{
    if (params.m_maxLongSide == m_params.m_maxLongSide && params.m_fps == m_params.m_fps &&
        params.m_baseBps == m_params.m_baseBps && params.m_floorBps == m_params.m_floorBps)
        return;
    LOG_INFO("screenshare encoder: params changed, max long side %d -> %d, %d fps, base bitrate %d.",
             m_params.m_maxLongSide, params.m_maxLongSide, params.m_fps, params.m_baseBps);
    m_params = params;
    m_paramsChanged = true;
}

const ScreenVideoParams& ShareVideoEncoder::Params() const
{
    return m_params;
}

int ShareVideoEncoder::MaxBps() const
{
    return m_maxBps;
}

int ShareVideoEncoder::MinBps() const
{
    return m_minBps;
}

int ShareVideoEncoder::BitrateBps() const
{
    return m_bitrateBps;
}

bool ShareVideoEncoder::TakeRangeChanged()
{
    const bool changed = m_rangeChanged;
    m_rangeChanged = false;
    return changed;
}

void ShareVideoEncoder::SetOpenLimitForTest(int limit)
{
    m_openLimitForTest = limit;
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
    //下一次推流重新按参数定码率范围，也重新尝试大尺寸
    m_maxBps = 0;
    m_minBps = 0;
    m_rangeChanged = false;
    m_paramsChanged = false;
    m_capLongSide = 0;
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
