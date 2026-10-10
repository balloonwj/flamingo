/**
 *  通话音频引擎实现，CallAudioEngine.cpp
 *  balloonwj@qq.com   2026-06-12（2026-10-03 播放端改为多路混音，新增采集开关、电平与无设备模式）
 *
 *  采样率策略：引擎内部统一用 48kHz 做 Opus 编解码、AEC、抖动缓冲；设备采集 / 播放采样率
 *  任意——采集侧把设备率重采样到 48kHz，播放侧把 48kHz 重采样到设备率（设备本就 48kHz 时
 *  重采样器不创建、直通）。这样任何采样率的麦克风 / 扬声器都能接入。
 */
#include "CallAudioEngine.h"

#include <windows.h>
#include <math.h>

#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace call {

//抖动缓冲容量：1 秒(48kHz)的采样，吸收网络/设备节奏抖动（far-end 参考环同样大小）。
static const size_t kJitterCapacity = 48000;
//Opus 码率(bit/s)。
static const int kAudioBitrate = 24000;
//一帧 20ms，每秒 50 帧。
static const int kFramesPerSecond = 50;
//引擎内部统一采样率(Hz)：Opus 原生率，编解码 / AEC / 抖动缓冲都在此率。
static const int kInternalRate = 48000;
//内部每帧采样数：20ms = 48000/50 = 960。
static const int kInternalFrameSamples = kInternalRate / kFramesPerSecond;
//回声消除滤波器尾长（毫秒）：须 ≥ 设备「扬声器→空气→麦克风」回声路径延迟，桌面典型 50~150ms，取 100ms。
static const int kEchoTailMs = 100;
//重采样质量 0~10：语音取 5（速度 / 质量平衡）。
static const int kResampleQuality = 5;
//毫秒每秒
static const int kMsPerSecond = 1000;
//音量百分比的基准：100% 为原样播放
static const double kVolumeUnity = 100.0;
//16 位采样的上下限（播放音量放大后限幅用）
static const double kSampleMaxF = 32767.0;
static const double kSampleMinF = -32768.0;

const int CallAudioEngine::kVolumeDefault;
const int CallAudioEngine::kVolumeMax;

CallAudioEngine::CallAudioEngine()
    : m_mixer(kInternalRate, kInternalFrameSamples)
    , m_echoRef(kJitterCapacity)
    , m_sink(NULL)
    , m_ready(false)
    , m_renderReady(false)
    , m_pcmTap(NULL)
    , m_aecEnabled(false)
    , m_captureEnabled(true)
    , m_captureLevel(0)
    , m_dtx(false)
    , m_playbackVolume(kVolumeDefault)
    , m_encodedPackets(0)
    , m_captureFrameIndex(0)
    , m_withoutDevices(false)
    , m_captureOpen(false)
    , m_captureResampling(false)
    , m_renderResampling(false)
    , m_sampleRate(kInternalRate)
    , m_frameSamples(kInternalFrameSamples)
    , m_captureRate(0)
    , m_renderRate(0)
{
}

CallAudioEngine::~CallAudioEngine()
{
    Stop();
}

bool CallAudioEngine::Start(CallAudioEngineSink* sink, bool enableAec)
{
    return Start(sink, enableAec, true);
}

bool CallAudioEngine::openCapture()
{
    //先关门控再开设备：设备回调在采集重采样器配好之前到来的样本由 m_captureEnabled / m_ready 门控丢弃
    int captureRate = 0;
    if (!m_capture.Start(this, captureRate))
    {
        LOG_ERROR("CallAudioEngine: open capture device failed.");
        return false;
    }
    m_captureRate = captureRate;
    m_captureResampling = (captureRate != kInternalRate);
    if (m_captureResampling && !m_captureResampler.Init(captureRate, kInternalRate, kResampleQuality))
    {
        LOG_ERROR("CallAudioEngine: capture resampler init failed (%d->%d).", captureRate, kInternalRate);
        m_capture.Stop();
        return false;
    }
    m_captureOpen = true;
    return true;
}

void CallAudioEngine::closeCapture()
{
    //join 采集线程后再无 OnAudioCaptured，可安全清理采集侧状态
    m_capture.Stop();
    m_captureResampler.Close();
    m_captureResampling = false;
    m_captureOpen = false;
    m_accum.clear();
    m_captureLevel.store(0);
}

bool CallAudioEngine::Start(CallAudioEngineSink* sink, bool enableAec, bool captureEnabled)
{
    if (sink == NULL)
        return false;

    m_sink = sink;
    m_withoutDevices = false;
    m_encodedPackets.store(0);
    m_captureFrameIndex = 0;
    m_captureEnabled.store(captureEnabled);

    //Opus 编码固定在内部 48kHz（解码在混音器里，每路一个）
    if (!m_codec.Init(kInternalRate, kInternalFrameSamples, kAudioBitrate))
    {
        LOG_ERROR("CallAudioEngine: Opus init failed at rate %d.", kInternalRate);
        return false;
    }
    if (m_dtx.load() && !m_codec.SetDtx(true))
        LOG_WARNING("CallAudioEngine: enabling Opus DTX failed, every frame will be sent.");

    //采集：一开始就打开时先开设备拿采样率（此刻 m_ready 仍 false，早到的采集样本被丢弃，直到下面就绪）
    if (captureEnabled && !openCapture())
    {
        m_codec.Close();
        return false;
    }

    //回声消除 + 降噪（@48k；仅真实通话启用）。渲染线程把播放帧押 m_echoRef，采集线程取作回声参考。
    m_aecEnabled.store(false);
    m_echoRef.Clear();
    if (enableAec)
    {
        int filterLen = kInternalRate * kEchoTailMs / kMsPerSecond;
        if (m_aec.Init(kInternalRate, kInternalFrameSamples, filterLen))
        {
            m_aecEnabled.store(true);
            LOG_INFO("CallAudioEngine: AEC enabled, tail=%dms (%d samples).", kEchoTailMs, filterLen);
        }
        else
        {
            LOG_ERROR("CallAudioEngine: AEC init failed; continuing without echo cancellation.");
        }
    }

    m_accum.clear();
    m_mixer.Clear();
    m_renderStage.clear();
    //采集就绪：采集回调开始正式处理（重采样→累积→编码）
    m_ready.store(true);

    //开播放拿设备播放率
    int renderRate = 0;
    if (!m_render.Start(this, renderRate))
    {
        LOG_ERROR("CallAudioEngine: open render device failed.");
        Stop();
        return false;
    }
    m_renderRate = renderRate;

    //播放重采样器：设备率 != 48k 才创建。注意渲染线程在 m_render.Start 返回后即可能回调
    //OnAudioNeeded，但 m_renderReady 仍 false，OnAudioNeeded 会先出静音，待此处配好再放行——
    //避开「渲染线程已起、播放重采样器尚未就绪」的竞态。
    m_renderResampling = (renderRate != kInternalRate);
    if (m_renderResampling)
    {
        if (!m_renderResampler.Init(kInternalRate, renderRate, kResampleQuality))
        {
            LOG_ERROR("CallAudioEngine: render resampler init failed (%d->%d).", kInternalRate, renderRate);
            Stop();
            return false;
        }
    }
    m_renderReady.store(true);

    LOG_INFO("CallAudioEngine: running, internal=%dHz frame=%d; capture=%s %dHz%s, render=%dHz%s.",
             kInternalRate, kInternalFrameSamples, captureEnabled ? "on" : "off",
             m_captureRate, m_captureResampling ? "(resampled)" : "",
             renderRate, m_renderResampling ? "(resampled)" : "");
    return true;
}

bool CallAudioEngine::StartWithoutDevices(CallAudioEngineSink* sink, bool enableAec, bool captureEnabled)
{
    if (sink == NULL)
        return false;
    m_sink = sink;
    m_withoutDevices = true;
    m_encodedPackets.store(0);
    m_captureFrameIndex = 0;
    m_captureEnabled.store(captureEnabled);
    if (!m_codec.Init(kInternalRate, kInternalFrameSamples, kAudioBitrate))
        return false;
    if (m_dtx.load() && !m_codec.SetDtx(true))
        LOG_WARNING("CallAudioEngine: enabling Opus DTX failed, every frame will be sent.");
    m_aecEnabled.store(false);
    m_echoRef.Clear();
    if (enableAec)
    {
        int filterLen = kInternalRate * kEchoTailMs / kMsPerSecond;
        if (m_aec.Init(kInternalRate, kInternalFrameSamples, filterLen))
            m_aecEnabled.store(true);
    }
    //无设备模式：采集与播放都按内部 48k 进出，不需要重采样
    m_captureResampling = false;
    m_renderResampling = false;
    m_captureRate = kInternalRate;
    m_renderRate = kInternalRate;
    m_accum.clear();
    m_mixer.Clear();
    m_renderStage.clear();
    m_ready.store(true);
    m_renderReady.store(true);
    LOG_INFO("CallAudioEngine: running without devices, capture=%s.", captureEnabled ? "on" : "off");
    return true;
}

void CallAudioEngine::Stop()
{
    //先关门控：之后到来的采集 / 解码 / 渲染回调直接丢弃 / 出静音
    m_ready.store(false);
    m_renderReady.store(false);
    //停采集（join 采集线程后再无 OnAudioCaptured）
    closeCapture();
    //停播放（join 渲染线程后再无 OnAudioNeeded）
    m_render.Stop();
    //释放编码器（前提：调用方已先停掉 OnEncodedPacket 的来源，此时无线程访问混音器里的解码器）
    m_codec.Close();
    //释放 AEC / 降噪 / 重采样状态（此时采集 / 渲染线程已 join，无并发访问）
    m_aecEnabled.store(false);
    m_aec.Close();
    m_renderResampler.Close();
    m_mixer.Clear();
    m_echoRef.Clear();
    m_renderStage.clear();
    m_withoutDevices = false;
}

bool CallAudioEngine::SetCaptureEnabled(bool enabled)
{
    if (enabled == m_captureEnabled.load())
        return true;
    if (!enabled)
    {
        m_captureEnabled.store(false);
        if (m_captureOpen)
            closeCapture();
        m_captureLevel.store(0);
        LOG_INFO("CallAudioEngine: capture disabled.");
        return true;
    }
    //无设备模式或引擎未运行：只切换开关（未运行时下次 Start 按开关决定是否打开设备）
    if (m_withoutDevices || !m_ready.load())
    {
        m_captureEnabled.store(true);
        return true;
    }
    if (!openCapture())
        return false;
    //关麦期间渲染线程仍在往回声参考环里押播放帧，积下的旧参考与新采集对不上，清掉重新对齐
    m_echoRef.Clear();
    m_captureEnabled.store(true);
    LOG_INFO("CallAudioEngine: capture enabled, %dHz%s.", m_captureRate, m_captureResampling ? "(resampled)" : "");
    return true;
}

bool CallAudioEngine::IsCaptureEnabled() const
{
    return m_captureEnabled.load();
}

int CallAudioEngine::CaptureLevel() const
{
    return m_captureLevel.load();
}

void CallAudioEngine::SetPcmTap(CallAudioPcmTap* tap)
{
    m_pcmTap.store(tap);
}

void CallAudioEngine::FeedCapture(const int16_t* mono, int frames)
{
    OnAudioCaptured(mono, frames);
}

void CallAudioEngine::PullRender(int16_t* mono, int frames)
{
    OnAudioNeeded(mono, frames);
}

void CallAudioEngine::OnAudioCaptured(const int16_t* mono, int frames)
{
    //采集线程上下文。未就绪(启动早期 / 正在停)或采集已关闭时丢弃。
    if (!m_ready.load() || !m_captureEnabled.load())
        return;
    if (mono == NULL || frames <= 0)
        return;

    //把设备率采集帧重采样到内部 48k 后累积；设备本就 48k 时直通
    if (m_captureResampling)
        m_captureResampler.Process(mono, frames, m_accum);
    else
        m_accum.insert(m_accum.end(), mono, mono + frames);

    int fs = m_frameSamples;
    std::vector<unsigned char> packet;
    while (static_cast<int>(m_accum.size()) >= fs)
    {
        //待编码帧默认是累积的原始帧；启用 AEC 时先消回声 + 降噪。
        const int16_t* frameToEncode = m_accum.data();
        if (m_aecEnabled.load() && m_aec.IsReady())
        {
            //取与本帧对应的参考帧（@48k，FIFO；播放混音加共享中的电脑声音，不足补静音）。回声延迟由 AEC 滤波器
            //尾长覆盖，FIFO 顺序即可、不需精确到样本对齐。
            m_refFrame.resize(static_cast<size_t>(fs));
            m_echoRef.Pop(m_refFrame.data(), static_cast<size_t>(fs));

            m_procFrame.resize(static_cast<size_t>(fs));
            m_aec.ProcessCapture(m_accum.data(), m_refFrame.data(), m_procFrame.data(), fs);
            frameToEncode = m_procFrame.data();
        }

        //本人电平（发言指示用）：按送出的那一帧算
        double energy = 0;
        for (int i = 0; i < fs; ++i)
            energy += static_cast<double>(frameToEncode[i]) * frameToEncode[i];
        m_captureLevel.store(static_cast<int>(sqrt(energy / fs)));

        //录制旁路：送出的这一帧
        CallAudioPcmTap* tap = m_pcmTap.load();
        if (tap != NULL)
            tap->OnCapturedPcm(frameToEncode, fs);

        //一帧：Opus 编码 → 经 sink 送出（上层发中转）。打开 DTX 时静音帧编出的 DTX 包不送出，但帧序号照样前进，
        //接收端看到的时间戳因此能越过这段静音
        const uint64_t frameIndex = m_captureFrameIndex++;
        if (m_codec.Encode(frameToEncode, fs, packet))
        {
            const bool skip = m_dtx.load() && OpusAudioCodec::IsDtxPacket(packet.size());
            if (m_sink != NULL && !packet.empty() && !skip)
            {
                m_sink->OnEncodedAudio(packet.data(), packet.size(), frameIndex);
                ++m_encodedPackets;
            }
        }
        m_accum.erase(m_accum.begin(), m_accum.begin() + fs);
    }
}

void CallAudioEngine::SetDtx(bool on)
{
    m_dtx.store(on);
}

void CallAudioEngine::SetPlaybackVolume(int percent)
{
    if (percent < 0)
        percent = 0;
    else if (percent > kVolumeMax)
        percent = kVolumeMax;
    //拖动滑块时每一步都会调到这里，不在此记日志；最终值由通话窗在音量浮层关闭时记录
    m_playbackVolume.store(percent);
}

int CallAudioEngine::PlaybackVolume() const
{
    return m_playbackVolume.load();
}

uint64_t CallAudioEngine::EncodedPacketCount() const
{
    return m_encodedPackets.load();
}

bool CallAudioEngine::IsWithoutDevices() const
{
    return m_withoutDevices.load();
}

bool CallAudioEngine::GetPlayoutCounters(int streamId, uint64_t& requested, uint64_t& concealed) const
{
    return m_mixer.GetPlayoutCounters(streamId, requested, concealed);
}

double CallAudioEngine::GainForVolume(int percent)
{
    if (percent <= 0)
        return 0.0;
    if (percent > kVolumeMax)
        percent = kVolumeMax;
    const double ratio = percent / kVolumeUnity;
    //100% 以下取平方，以上线性：两段在 100% 处都等于 1，曲线连续
    if (ratio <= 1.0)
        return ratio * ratio;
    return ratio;
}

void CallAudioEngine::applyPlaybackGain(int16_t* pcm, int frames)
{
    const int volume = m_playbackVolume.load();
    if (volume == kVolumeDefault || pcm == NULL || frames <= 0)
        return;
    const double gain = GainForVolume(volume);
    for (int i = 0; i < frames; ++i)
    {
        double v = pcm[i] * gain;
        if (v > kSampleMaxF)
            v = kSampleMaxF;
        else if (v < kSampleMinF)
            v = kSampleMinF;
        pcm[i] = static_cast<int16_t>(v);
    }
}

void CallAudioEngine::OnEncodedPacket(const unsigned char* data, size_t len, uint32_t tsSamples)
{
    OnEncodedPacketFrom(kDefaultStreamId, data, len, tsSamples);
}

void CallAudioEngine::OnEncodedPacketFrom(int streamId, const unsigned char* data, size_t len, uint32_t tsSamples)
{
    //解码来源线程上下文（回环=采集线程；真实通话=中转接收线程）。引擎未运行时丢弃。
    if (!m_ready.load())
        return;
    //解码为内部 48k PCM 并押入该路的抖动缓冲（data==NULL/len==0 时解码器走 PLC 隐藏一帧）
    m_mixer.PushEncoded(streamId, data, len, tsSamples);
}

void CallAudioEngine::RemoveStream(int streamId)
{
    m_mixer.RemoveStream(streamId);
}

void CallAudioEngine::PushSystemAudioReference(const int16_t* mono, int samples)
{
    //没有回声消除、或麦克风关着时不需要参考；麦克风关着时押进去的旧参考在重新打开时也已对不上
    if (mono == NULL || samples <= 0 || !m_aecEnabled.load() || !m_captureEnabled.load())
        return;
    m_echoRef.PushSystem(mono, static_cast<size_t>(samples));
}

void CallAudioEngine::ClearSystemAudioReference()
{
    m_echoRef.ClearSystem();
}

bool CallAudioEngine::GetStreamLevel(int streamId, int& rms) const
{
    return m_mixer.GetLevel(streamId, rms);
}

void CallAudioEngine::OnAudioNeeded(int16_t* mono, int frames)
{
    //渲染线程上下文。mono 是设备播放率、长度 frames。
    if (mono == NULL || frames <= 0)
        return;

    //渲染尚未就绪（播放重采样器配好之前）：出静音，避开启动竞态
    if (!m_renderReady.load())
    {
        for (int i = 0; i < frames; ++i)
            mono[i] = 0;
        return;
    }

    if (!m_renderResampling)
    {
        //设备本就 48k：各路混音直接写进设备缓冲（不足的部分为静音）
        m_mixer.Mix(mono, frames);
        CallAudioPcmTap* tap = m_pcmTap.load();
        if (tap != NULL)
            tap->OnRenderedPcm(mono, frames);
        //播放音量：录制旁路取的是缩放之前的声音，回声参考取的是缩放之后、实际放出去的声音
        applyPlaybackGain(mono, frames);
        //AEC：把实际播放出去的这帧（@48k）押入 far-end 参考环
        if (m_aecEnabled.load())
            m_echoRef.PushFar(mono, static_cast<size_t>(frames));
        return;
    }

    //需重采样：不断混出一个内部 48k 帧、重采样成设备率，攒够 frames 个再交给设备
    m_renderInBuf.resize(static_cast<size_t>(kInternalFrameSamples));
    while (static_cast<int>(m_renderStage.size()) < frames)
    {
        m_mixer.Mix(m_renderInBuf.data(), kInternalFrameSamples);
        CallAudioPcmTap* tap = m_pcmTap.load();
        if (tap != NULL)
            tap->OnRenderedPcm(m_renderInBuf.data(), kInternalFrameSamples);
        //播放音量：同上，旁路取缩放前、回声参考取缩放后
        applyPlaybackGain(m_renderInBuf.data(), kInternalFrameSamples);
        //AEC 参考：押的是「将被播放的内容」@48k（重采样前），与采集端 48k 对齐
        if (m_aecEnabled.load())
            m_echoRef.PushFar(m_renderInBuf.data(), static_cast<size_t>(kInternalFrameSamples));
        //重采样 48k → 设备率，追加到暂存
        m_renderResampler.Process(m_renderInBuf.data(), kInternalFrameSamples, m_renderStage);
    }

    //从暂存前段取 frames 个交给设备，余量留到下次
    memcpy(mono, m_renderStage.data(), static_cast<size_t>(frames) * sizeof(int16_t));
    m_renderStage.erase(m_renderStage.begin(), m_renderStage.begin() + frames);
}

} // namespace call
