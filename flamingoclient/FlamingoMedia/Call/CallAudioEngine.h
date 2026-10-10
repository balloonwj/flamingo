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
 *  第 4 期 4a（2026-10-03）起播放端改为多路混音（AudioStreamMixer）：每个发言人一路解码器与抖动缓冲，播放回调里
 *  叠加后限幅；一对一通话就是只有一路（路号 kDefaultStreamId）。同时新增：运行中开关采集（麦克风静音时真正停掉
 *  采集设备）、按路查询电平与本人采集电平（发言指示用）、无设备模式（测试直接喂采集、取播放，不打开声卡）。
 *
 *  2026-10-05 新增播放音量（SetPlaybackVolume）：混音之后、交给声卡之前按音量缩放。录制旁路取缩放之前的声音；
 *  回声消除的参考取缩放之后的声音（那才是扬声器实际放出去的）。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include "WasapiAudioIo.h"
#include "OpusAudioCodec.h"
#include "AudioPcmUtils.h"
#include "SpeexAudioProcessor.h"
#include "SpeexResampler.h"
#include "AudioStreamMixer.h"
#include "CallEchoReference.h"   //回声消除的参考信号：播放混音加共享中的电脑声音

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
     *  @param data       编码包字节起始；仅本次回调内有效，需留存须自行拷贝
     *  @param len        字节数
     *  @param frameIndex 这一帧的采集帧序号：本次启动以来引擎处理过的第几个 20 毫秒采集帧（从 0 起），打开 DTX 后没有
     *                    送出的帧也计在内（2026-10-06，docs/音视频通话方案.md 第 9.9.1 节）。发送方据此打「媒体时间戳」
     *                    （帧序号 × 20 毫秒），接收端的抖动缓冲按它排队；麦克风关闭期间引擎不处理采集，序号不前进
     */
    virtual void OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex) = 0;
};

//录制用的声音旁路（屏幕共享录制，第 7 期 7d）。两个方法分别在采集线程、渲染线程上调用，实现方须线程安全，且不能阻塞。
class CallAudioPcmTap
{
public:
    virtual ~CallAudioPcmTap() {}

    /**
     *  采集线程：本人麦克风的一帧（回声消除之后、送去编码的那一帧），48 kHz 单声道。
     *  @param mono    采样，仅本次回调内有效
     *  @param samples 采样数
     */
    virtual void OnCapturedPcm(const int16_t* mono, int samples) = 0;

    /**
     *  渲染线程：混好的、将要播放的一段声音（即其他人的声音），48 kHz 单声道。
     *  @param mono    采样，仅本次回调内有效
     *  @param samples 采样数
     */
    virtual void OnRenderedPcm(const int16_t* mono, int samples) = 0;
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

    /**
     *  登记录制用的声音旁路（任意线程）；传 NULL 取消。旁路对象须在取消登记、且引擎的采集与渲染线程不再回调之后才能销毁。
     */
    void SetPcmTap(CallAudioPcmTap* tap);

    /**
     *  启动引擎并指定采集是否一开始就打开（屏幕共享房间里麦克风默认关闭，只播放别人的声音）。
     *  @param captureEnabled 为 false 时不打开麦克风，之后可用 SetCaptureEnabled 打开
     *  其余同 Start(sink, enableAec)
     */
    bool Start(CallAudioEngineSink* sink, bool enableAec, bool captureEnabled);

    /**
     *  无设备模式启动：初始化编解码与混音，但不打开麦克风与扬声器，由调用方用 FeedCapture 喂采集、
     *  PullRender 取播放。供单元测试与联调用例在不出声、不占用设备的情况下验证引擎与网络链路。
     *  @param captureEnabled 采集开关的初始状态
     *  @return 成功返回 true
     */
    bool StartWithoutDevices(CallAudioEngineSink* sink, bool enableAec, bool captureEnabled);

    /**
     *  运行中打开或关闭采集。关闭时停掉麦克风设备（系统的麦克风占用提示随之消失），不再编码送出；
     *  打开时重新打开设备。无设备模式下只切换开关。引擎未运行时只记下开关，下次启动生效。
     *  @return 打开失败（设备不可用）返回 false，此时采集保持关闭
     */
    bool SetCaptureEnabled(bool enabled);

    //采集是否打开
    bool IsCaptureEnabled() const;

    //本人最近一帧采集的电平（均方根，0~32767）；采集关闭时为 0
    int CaptureLevel() const;

    //无设备模式：喂入一段设备采样率（即内部 48 kHz）的采集，相当于麦克风回调
    void FeedCapture(const int16_t* mono, int frames);

    //无设备模式：取出一段播放（内部 48 kHz），相当于扬声器回调
    void PullRender(int16_t* mono, int frames);

    //停止引擎并释放设备与编解码器。幂等。调用前须先停掉 OnEncodedPacket 的来源。
    void Stop();

    /**
     *  打开或关闭 Opus DTX（不连续发送）：打开后连续静音时编码器产出的 DTX 包（1~2 字节）不交给 sink，接收端按丢包
     *  处理、播放静音。群通话打开它（docs/群音视频通话方案.md 第 5 期）；一对一通话与屏幕共享不调用，保持逐帧发送。
     *  须在 Start / StartWithoutDevices 之前调用，对之后的每次启动都生效。
     *  @param on 是否打开
     */
    void SetDtx(bool on);

    /**
     *  喂入一个收到的编码音频包：按媒体时间戳放进抖动缓冲，播放线程取出时再解码（2026-10-06 起，
     *  docs/音视频通话方案.md 第 9.9 节）。丢包不必喂任何东西，播放端到点发现缺失后自己补。
     *  典型在中转接收线程（真实通话）或采集线程（回环）调用。引擎未运行时丢弃。
     *  @param data      编码包字节起始；为 NULL 或 len 为 0 时忽略
     *  @param len       字节数
     *  @param tsSamples 媒体时间戳：发送端的采集帧序号 × 960（48 kHz 下 20 毫秒一帧），32 位回绕
     */
    void OnEncodedPacket(const unsigned char* data, size_t len, uint32_t tsSamples);

    /**
     *  喂入某一路的编码音频包（多人场景按发言人分路，如座位号 × 轨道）。其余同 OnEncodedPacket。
     *  @param streamId 路号；一对一通话用 kDefaultStreamId
     */
    void OnEncodedPacketFrom(int streamId, const unsigned char* data, size_t len, uint32_t tsSamples);

    /**
     *  某一路编码包的播放统计（2026-10-06，方案 B 第 B4 步）：抖动缓冲计数、正常播放时的缓冲深度累计、丢包补偿与
     *  补静音的帧数。任意线程可调用。
     *  @return 该路存在返回 true
     */
    bool GetStreamStats(int streamId, AudioStreamStats& out) const
    {
        return m_mixer.GetStreamStats(streamId, out);
    }

    //删除一路（成员离开、关掉电脑声音时）
    void RemoveStream(int streamId);

    /**
     *  押入一段回环采到的电脑声音，作为回声消除参考信号的一部分（2026-10-06，docs/音视频通话方案.md 第 10 节）：共享电脑声音
     *  时其它程序的声音也从扬声器放出来，会被麦克风采到。只在启用了回声消除且麦克风打开时押入，否则直接丢弃。
     *  任意线程调用（一般是回环采集线程）。
     *  @param mono    48 kHz 单声道 16 位采样，只在本次调用期间读取
     *  @param samples 采样数
     */
    void PushSystemAudioReference(const int16_t* mono, int samples);

    //丢弃已押入、还没用掉的电脑声音参考（停止共享电脑声音时调用）。任意线程调用。
    void ClearSystemAudioReference();

    /**
     *  某一路最近一次播放时的电平（均方根，0~32767），发言指示用。
     *  @return 该路存在返回 true
     */
    bool GetStreamLevel(int streamId, int& rms) const;

    //一对一通话那一路的路号
    static const int kDefaultStreamId = 0;

    //播放音量的默认值（%）：原样播放
    static const int kVolumeDefault = 100;
    //播放音量的上限（%）：超过 100% 时放大，最多到 1.5 倍振幅
    static const int kVolumeMax = 150;

    /**
     *  设置本端的播放音量（通话窗的音量滑块，2026-10-05 起）。只影响本端听到的声音，不影响对方，也不影响录制旁路。
     *  任意线程调用，下一次播放回调起生效；引擎停止与重新启动都不会重置它。
     *  @param percent 音量百分比，0~kVolumeMax；超出范围按边界处理
     */
    void SetPlaybackVolume(int percent);

    //当前的播放音量（%）
    int PlaybackVolume() const;

    //本次启动以来编码并交给 sink 的音频包数（任意线程读；网络状况标识第 1 期的统计日志用，Start 时清零）
    uint64_t EncodedPacketCount() const;

    //是否以无设备模式运行（不打开麦克风与扬声器；Debug 版的 FLAMINGO_CALL_NO_AUDIO=1 即走这种模式）
    bool IsWithoutDevices() const;

    /**
     *  某一路累计的播放计数，转自 AudioStreamMixer::GetPlayoutCounters（卡顿率的来源）。任意线程调用。
     *  @return 该路存在返回 true
     */
    bool GetPlayoutCounters(int streamId, uint64_t& requested, uint64_t& concealed) const;

    /**
     *  音量百分比换算成振幅增益：0~100% 取平方（低音量段调节更细，符合听感），100%~150% 线性放大到 1.5 倍。
     *  @param percent 音量百分比；小于 0 按 0，大于 kVolumeMax 按 kVolumeMax
     *  @return 振幅增益，0.0~1.5
     */
    static double GainForVolume(int percent);

    bool IsRunning() const
    {
        return m_ready.load();
    }

private:
    //渲染线程：把一段将要播放的声音按播放音量缩放（音量为 100% 时不做任何事），超出 16 位范围的采样限幅
    void applyPlaybackGain(int16_t* pcm, int frames);

    //打开采集设备与采集重采样器（Start 与 SetCaptureEnabled 共用）
    bool openCapture();
    //关闭采集设备并清空采集侧状态
    void closeCapture();

    //采集线程上下文：攒帧→Opus 编码→sink。
    virtual void OnAudioCaptured(const int16_t* mono, int frames) override;
    //渲染线程上下文：从抖动缓冲取 frames 个采样，不足补静音。
    virtual void OnAudioNeeded(int16_t* mono, int frames) override;

private:
    WasapiCapture        m_capture;            //麦克风采集
    WasapiRender         m_render;             //扬声器播放
    OpusAudioCodec       m_codec;              //Opus 编解码（编码器:采集线程；解码器:来源线程；两者独立可并发）
    AudioStreamMixer     m_mixer;              //各路解码器与抖动缓冲（@内部48k；来源线程押、渲染线程混音）
    SpeexAudioProcessor  m_aec;                //回声消除 + 降噪（@内部48k；仅采集线程用；enableAec 时启用）
    CallEchoReference    m_echoRef;            //回声消除的参考信号（@内部48k）：渲染线程押刚播放的混音、共享电脑声音时回环采集线程
                                               //押电脑声音，采集线程每帧取一次（两路相加）作回声参考；自带互斥（2026-10-06 起，原 m_farRef）
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
    std::atomic<CallAudioPcmTap*> m_pcmTap;    //录制用的声音旁路（不拥有）；没有录制时为 NULL
    std::atomic<bool>    m_aecEnabled;         //是否启用 AEC（采集 / 渲染线程据此决定是否走回声消除 / 押参考）
    std::atomic<bool>    m_captureEnabled;     //采集开关（关闭时采集回调直接丢弃；有设备时同时停掉设备）
    std::atomic<int>     m_captureLevel;       //本人最近一帧采集的电平（采集线程写，任意线程读）
    std::atomic<bool>    m_dtx;                //是否打开 Opus DTX（SetDtx 写；启动时交给编码器，采集线程据此跳过 DTX 包）
    std::atomic<int>     m_playbackVolume;     //播放音量（%，0~kVolumeMax；UI 线程写、渲染线程读；引擎启停不重置）
    std::atomic<uint64_t> m_encodedPackets;    //本次启动以来交给 sink 的音频包数（采集线程写，任意线程读；Start 时清零）
    uint64_t             m_captureFrameIndex;  //下一个采集帧的序号（2026-10-06）：每处理一帧 20 毫秒采集加 1，DTX 没送出的帧也计；
                                               //只在采集线程（无设备模式下为 FeedCapture 的调用线程）读写，Start 时清零
    std::atomic<bool>    m_withoutDevices;     //是否以无设备模式运行（不打开 WASAPI；启动线程写，统计线程也会读）
    bool                 m_captureOpen;        //采集设备是否已打开（只在调用 Start / Stop / SetCaptureEnabled 的线程使用）
    bool                 m_captureResampling;  //采集是否需重采样（设备采集率 != 内部48k；Start 设，采集线程读）
    bool                 m_renderResampling;   //播放是否需重采样（设备播放率 != 内部48k；Start 设，渲染线程读）
    int                  m_sampleRate;         //内部统一采样率(Hz，固定48000)
    int                  m_frameSamples;       //内部每帧采样数(20ms=960)
    int                  m_captureRate;        //设备采集采样率(Hz，仅记录/日志)
    int                  m_renderRate;         //设备播放采样率(Hz，仅记录/日志)
};

} // namespace call
