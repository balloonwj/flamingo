/**
 *  通话视频引擎实现，CallVideoEngine.cpp
 *  balloonwj@qq.com   2026-06-16
 */
#include "CallVideoEngine.h"

#include <windows.h>

#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace call {

//一秒的 100ns 计数（帧时间戳 = 帧序号 × 此值 / fps）。
static const int64_t kHnsPerSecond = 10000000LL;

//解码器初始化时给的预设画面尺寸：与本端摄像头无关（本端可以不开摄像头只看对方），真实尺寸以码流 SPS 为准，
//MfH264Decoder 遇到 MF_E_TRANSFORM_STREAM_CHANGE 时会按码流重新协商输出
static const int kDefaultDecodeWidth = 1280;
static const int kDefaultDecodeHeight = 720;

CallVideoEngine::CallVideoEngine()
    : m_sink(NULL)
    , m_recvReady(false)
    , m_sendReady(false)
    , m_width(0)
    , m_height(0)
    , m_fps(0)
    , m_bitrate(0)
    , m_frameIndex(0)
    , m_encoderInitTried(false)
    , m_decoderInitTried(false)
    , m_keyframeRequested(false)
{
}

CallVideoEngine::~CallVideoEngine()
{
    Stop();
}

bool CallVideoEngine::Start(CallVideoEngineSink* sink, int fps, int bitrate)
{
    if (!StartReceive(sink))
        return false;
    return StartCamera(fps, bitrate);
}

void CallVideoEngine::Stop()
{
    StopCamera();
    StopReceive();
}

bool CallVideoEngine::StartReceive(CallVideoEngineSink* sink)
{
    if (sink == NULL)
        return false;
    m_sink = sink;
    if (m_recvReady.load())
        return true;
    //解码器不在此创建：MFT 须在使用线程上创建，故延迟到喂解码线程首次用时（见 OnRemoteEncodedVideo）。
    //先复位「已尝试创建」再打开门控，喂解码线程看到门控打开时一定也看到复位后的值
    m_decoderInitTried = false;
    m_recvReady.store(true);
    LOG_INFO("CallVideoEngine: receive started.");
    return true;
}

void CallVideoEngine::StopReceive()
{
    //先关门控：之后到来的包直接丢弃；再释放解码器（前提：调用方已停掉 OnRemoteEncodedVideo 的来源）
    const bool wasReceiving = m_recvReady.exchange(false);
    m_decoder.Close();
    if (wasReceiving)
        LOG_INFO("CallVideoEngine: receive stopped.");
}

bool CallVideoEngine::StartCamera(int fps, int bitrate)
{
    if (m_sink == NULL || fps <= 0)
        return false;
    if (m_sendReady.load())
        return true;

    m_fps = fps;
    m_bitrate = bitrate;
    m_frameIndex = 0;
    m_encoderInitTried = false;

    //先开摄像头拿分辨率（采集线程内 MFStartup，故其后 MFT 创建时 MF 已就绪）。
    //此刻 m_sendReady 仍 false，早到的采集帧被丢弃，直到下面置位。
    int w = 0;
    int h = 0;
    if (!m_capture.Start(this, w, h))
    {
        LOG_ERROR("CallVideoEngine: open camera failed.");
        return false;
    }
    m_width = w;
    m_height = h;

    //编码器不在此创建：MFT 须在使用线程上创建，故延迟到采集线程首次用时（见 OnVideoFrame）。
    m_sendReady.store(true);
    LOG_INFO("CallVideoEngine: camera running, %dx%d @%dfps %dbps.", w, h, fps, bitrate);
    return true;
}

void CallVideoEngine::StopCamera()
{
    //先关门控，再停采集（join 采集线程后再无 OnVideoFrame，编码器不再被并发访问）。
    //注意：MfCameraCapture::Stop 内部会 MFShutdown 其采集线程的 MF；编码 MFT 是 COM 对象，
    //其 Release 不依赖 MF 平台，故在此之后 Close 仍安全。
    const bool wasOn = m_sendReady.exchange(false);
    m_capture.Stop();
    m_encoder.Close();
    if (wasOn)
        LOG_INFO("CallVideoEngine: camera stopped.");
}

void CallVideoEngine::OnVideoFrame(const uint8_t* data, int width, int height, int stride)
{
    //采集线程上下文。摄像头没有开启（或正在关闭）时丢弃。
    if (!m_sendReady.load())
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

    //对方请求了关键帧：编这一帧之前让编码器强制关键帧（当帧生效）
    if (m_keyframeRequested.exchange(false) && !m_encoder.ForceKeyFrame())
        LOG_WARNING("CallVideoEngine: force key frame failed.");

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
    //喂解码线程上下文（回环=采集线程；真实通话=视频解码线程）。没有在接收时丢弃。
    if (!m_recvReady.load())
        return;

    //懒创建解码器：在喂解码线程上创建，确保 MFT 创建与使用同线程。尺寸用预设值（与本端摄像头无关）
    if (!m_decoder.IsReady())
    {
        if (m_decoderInitTried)
            return;
        m_decoderInitTried = true;
        if (!m_decoder.Init(kDefaultDecodeWidth, kDefaultDecodeHeight))
        {
            LOG_ERROR("CallVideoEngine: H264 decoder init failed (%dx%d).", kDefaultDecodeWidth, kDefaultDecodeHeight);
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
