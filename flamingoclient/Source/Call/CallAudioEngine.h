/**
 *  通话音频引擎（采集→编码→送出 / 收包→解码→抖动缓冲→播放），CallAudioEngine.h
 *
 *  把第二期音频管线收口成一个可复用引擎：WASAPI 麦克风采集 → 攒成 20ms 帧 → Opus 编码
 *  → 经 sink 回调把编码包交给上层（上层通常经中转发给对端）；上层把对端来的编码包喂进
 *  OnEncodedPacket → Opus 解码 → 押入抖动缓冲 → WASAPI 扬声器播放。
 *
 *  本地回环（AudioLoopback，把自己编码的包直接喂回 OnEncodedPacket）与真实通话
 *  （CallMediaSession，把包经 imgserver 中转收发）共用本引擎，差异只在「编码包送到哪、
 *  收到的包从哪来」——故回环验证通过即等于验证了引擎本体。
 *
 *  线程与并发：
 *    - 采集线程：OnAudioCaptured 攒帧 + Opus 编码 + 调 sink（编码用 codec 的编码器对象）；
 *    - 解码来源线程（回环为采集线程本身；真实通话为中转接收线程）：OnEncodedPacket 解码
 *      （用 codec 的解码器对象）+ 押抖动缓冲；
 *    - 渲染线程：OnAudioNeeded 从抖动缓冲取、不足补静音。
 *    OpusAudioCodec 的编码器与解码器是相互独立的底层对象，编码与解码分别只碰各自对象，
 *    故采集线程编码与来源线程解码可并发、无需互斥。抖动缓冲 PcmRingBuffer 自带互斥。
 *    **Stop 安全前提**：调用方须先停掉会调用 OnEncodedPacket 的来源（如中转接收）再调本引擎
 *    Stop()，否则解码可能在 codec 释放后被调用。回环因 OnEncodedPacket 同步发生在采集线程上，
 *    采集线程 join 后自然不再有解码，无此问题。
 *
 *  采样率：用采集设备采样率作 Opus 采样率（须为 Opus 支持的 8000/12000/16000/24000/48000，
 *  常见 48kHz）；非支持率 Start 失败（需重采样，留 speexdsp 那期）。播放率与采集率不同时仅
 *  告警（PoC 不重采样）。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include "WasapiAudioIo.h"
#include "OpusAudioCodec.h"
#include "AudioPcmUtils.h"
#include "SpeexAudioProcessor.h"
#include "SpeexResampler.h"

#include <vector>
#include <atomic>

namespace call {

//编码音频去向回调。OnEncodedAudio 在采集线程上下文触发。
class CallAudioEngineSink
{
public:
    virtual ~CallAudioEngineSink() {}

    /**
     *  引擎采集并编码出一个音频包（Opus）。实现方通常把它经中转发给对端。
     *  @param data 编码包字节起始；仅本次回调内有效，需留存须自行拷贝
     *  @param len  字节数
     */
    virtual void OnEncodedAudio(const unsigned char* data, size_t len) = 0;
};

//通话音频引擎：采集+编码经 sink 送出；收到的编码包解码后播放。
class CallAudioEngine : public WasapiCaptureHandler, public WasapiRenderSource
{
public:
    CallAudioEngine();
    ~CallAudioEngine();

    CallAudioEngine(const CallAudioEngine&) = delete;
    CallAudioEngine& operator=(const CallAudioEngine&) = delete;

    /**
     *  启动引擎：打开麦克风/扬声器、按采集率初始化 Opus、起采集与渲染线程。
     *  @param sink      编码包去向（非空；生命周期须长于引擎，至少持续到 Stop 返回）
     *  @param enableAec 是否启用回声消除 + 降噪：真实通话传 true（避免扬声器声音被麦克风
     *                   拾回形成回授），本地回环传 false（回环播放的就是自己的声音，开 AEC
     *                   反而会把它消掉）。启用时渲染线程把播放帧押入 far-end 参考环，采集
     *                   线程取参考帧做回声消除后再编码。
     *  @return 成功（设备就绪且采集率为 Opus 支持率）返回 true；否则 false（见日志）
     */
    bool Start(CallAudioEngineSink* sink, bool enableAec);

    //停止引擎并释放设备与编解码器。幂等。调用前须先停掉 OnEncodedPacket 的来源。
    void Stop();

    /**
     *  喂入一个收到的编码音频包：解码后押入抖动缓冲待播放。
     *  典型在中转接收线程（真实通话）或采集线程（回环）调用。引擎未运行时丢弃。
     *  @param data 编码包字节起始
     *  @param len  字节数
     */
    void OnEncodedPacket(const unsigned char* data, size_t len);

    bool IsRunning() const
    {
        return m_ready.load();
    }

private:
    //采集线程上下文：攒帧→Opus 编码→sink。
    virtual void OnAudioCaptured(const int16_t* mono, int frames) override;
    //渲染线程上下文：从抖动缓冲取 frames 个采样，不足补静音。
    virtual void OnAudioNeeded(int16_t* mono, int frames) override;

private:
    WasapiCapture        m_capture;            //麦克风采集
    WasapiRender         m_render;             //扬声器播放
    OpusAudioCodec       m_codec;              //Opus 编解码（编码器:采集线程；解码器:来源线程；两者独立可并发）
    PcmRingBuffer        m_jitter;             //解码后 PCM 抖动缓冲（@内部48k；来源线程押、渲染线程取；自带互斥）
    SpeexAudioProcessor  m_aec;                //回声消除 + 降噪（@内部48k；仅采集线程用；enableAec 时启用）
    PcmRingBuffer        m_farRef;             //far-end 参考环（@内部48k）：渲染线程押刚播放帧、采集线程取作回声参考（自带互斥）
    SpeexResampler       m_captureResampler;   //采集重采样器：设备采集率→内部48k（仅采集线程用；设备本就48k 时不创建）
    SpeexResampler       m_renderResampler;    //播放重采样器：内部48k→设备播放率（仅渲染线程用；设备本就48k 时不创建）
    std::vector<int16_t> m_accum;              //采集累积缓冲（@内部48k，攒够一帧再编码；仅采集线程用）
    std::vector<int16_t> m_refFrame;           //采集线程取参考帧的临时缓冲（仅采集线程用）
    std::vector<int16_t> m_procFrame;          //采集线程 AEC/降噪 输出的临时缓冲（仅采集线程用）
    std::vector<int16_t> m_renderStage;        //渲染重采样输出暂存（@设备播放率，跨回调留存余量；仅渲染线程用）
    std::vector<int16_t> m_renderInBuf;        //渲染拉取的一帧内部48k输入临时缓冲（仅渲染线程用）
    CallAudioEngineSink* m_sink;               //编码包去向（不拥有）
    std::atomic<bool>    m_ready;              //采集/解码是否就绪（采集 / 解码回调据此门控）
    std::atomic<bool>    m_renderReady;        //渲染是否就绪（渲染重采样器配好前 OnAudioNeeded 出静音，避开启动竞态）
    std::atomic<bool>    m_aecEnabled;         //是否启用 AEC（采集 / 渲染线程据此决定是否走回声消除 / 押参考）
    bool                 m_captureResampling;  //采集是否需重采样（设备采集率 != 内部48k；Start 设，采集线程读）
    bool                 m_renderResampling;   //播放是否需重采样（设备播放率 != 内部48k；Start 设，渲染线程读）
    int                  m_sampleRate;         //内部统一采样率(Hz，固定48000)
    int                  m_frameSamples;       //内部每帧采样数(20ms=960)
    int                  m_captureRate;        //设备采集采样率(Hz，仅记录/日志)
    int                  m_renderRate;         //设备播放采样率(Hz，仅记录/日志)
};

} // namespace call
