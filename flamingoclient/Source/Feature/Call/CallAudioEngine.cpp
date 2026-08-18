/**
 *  通话音频引擎实现，CallAudioEngine.cpp
 *  balloonwj@qq.com   2026-06-12
 *
 *  采样率策略：引擎内部统一用 48kHz 做 Opus 编解码、AEC、抖动缓冲；设备采集 / 播放采样率
 *  任意——采集侧把设备率重采样到 48kHz，播放侧把 48kHz 重采样到设备率（设备本就 48kHz 时
 *  重采样器不创建、直通）。这样任何采样率的麦克风 / 扬声器都能接入。
 */
#include "CallAudioEngine.h"

#include <windows.h>

#include "../../Common/Log/IULog.h"   //须在 windows.h 之后

namespace call {

//抖动缓冲容量：1 秒(48kHz)的采样，吸收网络/设备节奏抖动。
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

CallAudioEngine::CallAudioEngine()
    : m_jitter(kJitterCapacity)
    , m_farRef(kJitterCapacity)
    , m_sink(NULL)
    , m_ready(false)
    , m_renderReady(false)
    , m_aecEnabled(false)
    , m_captureResampling(false)
    , m_renderResampling(false)
    , m_sampleRate(0)
    , m_frameSamples(0)
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
    if (sink == NULL)
        return false;

    m_sink = sink;

    //先开采集拿设备采样率（此刻 m_ready 仍 false，早到的采集样本被丢弃，直到下面就绪）
    int captureRate = 0;
    if (!m_capture.Start(this, captureRate))
    {
        LOG_ERROR("CallAudioEngine: open capture device failed.");
        return false;
    }

    //内部统一 48kHz
    m_sampleRate = kInternalRate;
    m_frameSamples = kInternalFrameSamples;
    m_captureRate = captureRate;

    //采集重采样器：设备率 != 48k 才创建
    m_captureResampling = (captureRate != kInternalRate);
    if (m_captureResampling)
    {
        if (!m_captureResampler.Init(captureRate, kInternalRate, kResampleQuality))
        {
            LOG_ERROR("CallAudioEngine: capture resampler init failed (%d->%d).", captureRate, kInternalRate);
            m_capture.Stop();
            return false;
        }
    }

    //Opus 编解码固定在内部 48kHz
    if (!m_codec.Init(kInternalRate, kInternalFrameSamples, kAudioBitrate))
    {
        LOG_ERROR("CallAudioEngine: Opus init failed at rate %d.", kInternalRate);
        m_capture.Stop();
        m_captureResampler.Close();
        return false;
    }

    //回声消除 + 降噪（@48k；仅真实通话启用）。渲染线程把播放帧押 m_farRef，采集线程取作回声参考。
    m_aecEnabled.store(false);
    m_farRef.Clear();
    if (enableAec)
    {
        int filterLen = kInternalRate * kEchoTailMs / 1000;
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
    m_jitter.Clear();
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

    LOG_INFO("CallAudioEngine: running, internal=%dHz frame=%d; capture=%dHz%s, render=%dHz%s.",
             kInternalRate, kInternalFrameSamples,
             captureRate, m_captureResampling ? "(resampled)" : "",
             renderRate, m_renderResampling ? "(resampled)" : "");
    return true;
}

void CallAudioEngine::Stop()
{
    //先关门控：之后到来的采集 / 解码 / 渲染回调直接丢弃 / 出静音
    m_ready.store(false);
    m_renderReady.store(false);
    //停采集（join 采集线程后再无 OnAudioCaptured）
    m_capture.Stop();
    //停播放（join 渲染线程后再无 OnAudioNeeded）
    m_render.Stop();
    //释放编解码器（前提：调用方已先停掉 OnEncodedPacket 的来源，此时无线程访问 codec）
    m_codec.Close();
    //释放 AEC / 降噪 / 重采样状态（此时采集 / 渲染线程已 join，无并发访问）
    m_aecEnabled.store(false);
    m_aec.Close();
    m_captureResampler.Close();
    m_renderResampler.Close();
    m_accum.clear();
    m_jitter.Clear();
    m_farRef.Clear();
    m_renderStage.clear();
}

void CallAudioEngine::OnAudioCaptured(const int16_t* mono, int frames)
{
    //采集线程上下文。未就绪(启动早期 / 正在停)时丢弃。
    if (!m_ready.load())
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
            //取与本帧对应的 far-end 参考帧（@48k，FIFO；不足补静音）。回声延迟由 AEC 滤波器
            //尾长覆盖，FIFO 顺序即可、不需精确到样本对齐。
            m_refFrame.resize(static_cast<size_t>(fs));
            size_t got = m_farRef.Pop(m_refFrame.data(), static_cast<size_t>(fs));
            for (size_t i = got; i < static_cast<size_t>(fs); ++i)
                m_refFrame[i] = 0;

            m_procFrame.resize(static_cast<size_t>(fs));
            m_aec.ProcessCapture(m_accum.data(), m_refFrame.data(), m_procFrame.data(), fs);
            frameToEncode = m_procFrame.data();
        }

        //一帧：Opus 编码 → 经 sink 送出（上层发中转）
        if (m_codec.Encode(frameToEncode, fs, packet))
        {
            if (m_sink != NULL && !packet.empty())
                m_sink->OnEncodedAudio(packet.data(), packet.size());
        }
        m_accum.erase(m_accum.begin(), m_accum.begin() + fs);
    }
}

void CallAudioEngine::OnEncodedPacket(const unsigned char* data, size_t len)
{
    //解码来源线程上下文（回环=采集线程；真实通话=中转接收线程）。引擎未运行时丢弃。
    if (!m_ready.load())
        return;

    //解码为内部 48k PCM（data==NULL/len==0 时由 OpusAudioCodec 走 PLC 隐藏一帧）
    std::vector<int16_t> decoded;
    if (m_codec.Decode(data, len, decoded) && !decoded.empty())
        m_jitter.Push(decoded.data(), decoded.size());
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
        //设备本就 48k：直接从抖动缓冲取、不足补静音
        size_t got = m_jitter.Pop(mono, static_cast<size_t>(frames));
        for (size_t i = got; i < static_cast<size_t>(frames); ++i)
            mono[i] = 0;
        //AEC：把实际播放出去的这帧（@48k）押入 far-end 参考环
        if (m_aecEnabled.load())
            m_farRef.Push(mono, static_cast<size_t>(frames));
        return;
    }

    //需重采样：不断拉一个内部 48k 帧、重采样成设备率，攒够 frames 个再交给设备
    m_renderInBuf.resize(static_cast<size_t>(kInternalFrameSamples));
    while (static_cast<int>(m_renderStage.size()) < frames)
    {
        size_t got = m_jitter.Pop(m_renderInBuf.data(), static_cast<size_t>(kInternalFrameSamples));
        for (size_t i = got; i < static_cast<size_t>(kInternalFrameSamples); ++i)
            m_renderInBuf[i] = 0;
        //AEC 参考：押的是「将被播放的内容」@48k（重采样前），与采集端 48k 对齐
        if (m_aecEnabled.load())
            m_farRef.Push(m_renderInBuf.data(), static_cast<size_t>(kInternalFrameSamples));
        //重采样 48k → 设备率，追加到暂存
        m_renderResampler.Process(m_renderInBuf.data(), kInternalFrameSamples, m_renderStage);
    }

    //从暂存前段取 frames 个交给设备，余量留到下次
    memcpy(mono, m_renderStage.data(), static_cast<size_t>(frames) * sizeof(int16_t));
    m_renderStage.erase(m_renderStage.begin(), m_renderStage.begin() + frames);
}

} // namespace call
