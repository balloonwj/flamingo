/**
 *  通话视频引擎实现，CallVideoEngine.cpp
 *  balloonwj@qq.com   2026-06-16
 */
#include "CallVideoEngine.h"

#include <windows.h>

#include "../IULog.h"   //须在 windows.h 之后

namespace call {

//一秒的 100ns 计数（帧时间戳 = 帧序号 × 此值 / fps）。
static const int64_t kHnsPerSecond = 10000000LL;

CallVideoEngine::CallVideoEngine()
    : m_sink(NULL)
    , m_ready(false)
    , m_width(0)
    , m_height(0)
    , m_fps(0)
    , m_bitrate(0)
    , m_frameIndex(0)
    , m_encoderInitTried(false)
    , m_decoderInitTried(false)
{
}

CallVideoEngine::~CallVideoEngine()
{
    Stop();
}

bool CallVideoEngine::Start(CallVideoEngineSink* sink, int fps, int bitrate)
{
    if (sink == NULL || fps <= 0)
        return false;

    m_sink = sink;
    m_fps = fps;
    m_bitrate = bitrate;
    m_frameIndex = 0;
    m_encoderInitTried = false;
    m_decoderInitTried = false;

    //先开摄像头拿分辨率（采集线程内 MFStartup，故其后 MFT 创建时 MF 已就绪）。
    //此刻 m_ready 仍 false，早到的采集帧被丢弃，直到下面置位。
    int w = 0;
    int h = 0;
    if (!m_capture.Start(this, w, h))
    {
        LOG_ERROR("CallVideoEngine: open camera failed.");
        return false;
    }
    m_width = w;
    m_height = h;

    //编 / 解码器不在此创建：MFT 须在使用线程上创建，故延迟到采集线程（编码）/ 喂解码线程
    //（解码）首次用时再创建（见 OnVideoFrame / OnRemoteEncodedVideo）。
    m_ready.store(true);
    LOG_INFO("CallVideoEngine: running, %dx%d @%dfps %dbps.", w, h, fps, bitrate);
    return true;
}

void CallVideoEngine::Stop()
{
    //先关门控：之后到来的采集 / 解码回调直接丢弃
    m_ready.store(false);
    //停采集（join 采集线程后再无 OnVideoFrame，编码器不再被并发访问）。
    //注意：MfCameraCapture::Stop 内部会 MFShutdown 其采集线程的 MF；编解码 MFT 是 COM 对象，
    //其 Release 不依赖 MF 平台，故在此之后 Close 仍安全。
    m_capture.Stop();
    //释放编解码器（前提：调用方已先停掉 OnRemoteEncodedVideo 的来源，此时无线程访问解码器）
    m_encoder.Close();
    m_decoder.Close();
}

void CallVideoEngine::OnVideoFrame(const uint8_t* data, int width, int height, int stride)
{
    //采集线程上下文。未就绪时丢弃。
    if (!m_ready.load())
        return;
    if (data == NULL || width != m_width || height != m_height)
        return;

    //先把原始采集帧交给 sink 做本地预览（编码之前；编码器未就绪也能出本地预览画面）
    if (m_sink != NULL)
        m_sink->OnLocalVideoFrame(data, width, height, stride);

    //懒创建编码器：在采集线程上创建，确保 MFT 创建与使用同线程
    if (!m_encoder.IsReady())
    {
        if (m_encoderInitTried)
            return;   //已试过且失败，不再每帧重试
        m_encoderInitTried = true;
        if (!m_encoder.Init(m_width, m_height, m_fps, m_bitrate))
        {
            LOG_ERROR("CallVideoEngine: H264 encoder init failed (%dx%d).", m_width, m_height);
            return;
        }
    }

    //按帧序号算时间戳，送 H.264 编码（编码输出经 OnEncodedH264 回调）
    int64_t pts = m_frameIndex * kHnsPerSecond / m_fps;
    ++m_frameIndex;
    m_encoder.EncodeNv12(data, stride, pts, this);
}

void CallVideoEngine::OnVideoCaptureStopped(int reason)
{
    //采集线程上下文：摄像头中途没了（多半被其它程序抢走）。引擎的解码侧仍可用（还能显示对端画面），
    //故这里不停引擎，只把原因转报给 sink，由上层提示用户。
    LOG_ERROR("CallVideoEngine: camera capture stopped, reason=%d; no local video from now on.", reason);
    if (m_sink != NULL)
        m_sink->OnLocalVideoUnavailable(reason);
}

void CallVideoEngine::OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns)
{
    //采集线程上下文：编码帧经 sink 送出（上层打视频标记发中转）
    if (m_sink != NULL)
        m_sink->OnEncodedVideo(data, len, keyframe, ptsHns);
}

void CallVideoEngine::OnRemoteEncodedVideo(const uint8_t* data, size_t len, int64_t ptsHns)
{
    //喂解码线程上下文（回环=采集线程；真实通话=中转接收线程）。未就绪时丢弃。
    if (!m_ready.load())
        return;

    //懒创建解码器：在喂解码线程上创建，确保 MFT 创建与使用同线程
    if (!m_decoder.IsReady())
    {
        if (m_decoderInitTried)
            return;
        m_decoderInitTried = true;
        if (!m_decoder.Init(m_width, m_height))
        {
            LOG_ERROR("CallVideoEngine: H264 decoder init failed (%dx%d).", m_width, m_height);
            return;
        }
    }

    //解码（解码出帧经 OnDecodedNv12 回调）
    m_decoder.DecodePacket(data, len, ptsHns, this);
}

void CallVideoEngine::OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t /*ptsHns*/)
{
    //喂解码线程上下文：远端帧经 sink 交付显示
    if (m_sink != NULL)
        m_sink->OnRemoteVideoFrame(nv12, width, height, stride);
}

} // namespace call
