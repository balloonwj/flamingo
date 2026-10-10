/**
 *  通话中共享电脑声音的发送部分（2026-10-06，docs/音视频通话方案.md 第 10 节），CallSystemAudioSender.h
 *
 *  回环采集交来的电脑声音（48 kHz 单声道，长度不定）在这里攒成 20 毫秒一帧，按闪避增益调整音量后，用单独的 Opus
 *  编码器（音乐模式、64 kbps，与屏幕共享的电脑声音相同）编码，连同这一路自己的时间戳（帧序号 × 20 毫秒，打开共享时
 *  从 0 开始）交给 sink；sink（CallMediaSession）再按当前路径发往中转或 P2P。说话声那一路不经过这里。
 *  从会话里抽出来单独成类，使攒帧、闪避与时间戳可以在 FlamingoUnit 里直接验证（CSA5、CSA7），不需要网络与声音设备。
 *
 *  线程：Start / Stop 在 UI 线程调用；Feed 在回环采集线程（无设备模式下为测试线程）调用，sink 回调也在该线程上。
 *  内部用一把锁把三者串行化。调用方须先停掉回环采集再调用 Stop，之后不会再有 sink 回调。
 *
 *  典型用法：
 *      m_sender.Start(this);
 *      //回环采集线程：
 *      m_sender.Feed(pcm, samples, CallSystemAudioGainPercent(excludingSelf, peerLevel));
 *      ...
 *      m_sender.Stop();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <mutex>
#include <vector>

#include "OpusAudioCodec.h"

namespace call {

//电脑声音编码包的去向（在调用 Feed 的线程上回调）
class CallSystemAudioPacketSink
{
public:
    virtual ~CallSystemAudioPacketSink()
    {
    }

    /**
     *  编好了一个电脑声音的 Opus 包。
     *  @param data 包的字节，只在本次回调期间有效
     *  @param len  字节数
     *  @param tsMs 这一路自己的时间戳（毫秒）：第 n 帧为 n × 20
     */
    virtual void OnSystemAudioPacket(const uint8_t* data, size_t len, uint64_t tsMs) = 0;
};

//判断对方「正在说话」的电平门限（均方根），与屏幕共享闪避用的门限相同（ShareAudioSession.cpp 的 kSpeakingThreshold）
const int kCallPeerSpeakingLevel = 600;

//打开共享电脑声音的结果（CallMediaSession::SetSystemAudioEnabled 的返回值）
enum CallSystemAudioResult
{
    call_sysaudio_ok               = 0,   //已按要求打开或关闭
    call_sysaudio_not_connected    = 1,   //通话还没接通（媒体通道没建好、音频引擎没启动），不能打开
    call_sysaudio_peer_unsupported = 2,   //还没收到过对方的质量报告，或对方报告里没有「能接收电脑声音」（旧版本）
    call_sysaudio_capture_failed   = 3,   //回环采集打不开（两种回环方式都失败）
};

/**
 *  打开共享电脑声音之前的检查（纯函数）：通话要已接通，且对方的报告表明能接收电脑声音。
 *  @param connected     媒体通道是否已建好、音频引擎是否在运行
 *  @param peerReports   至今收到的对方质量报告份数
 *  @param peerFlags     最近一份对方报告的标志字节（kCallReportFlagRecvSystemAudio 等）
 *  @return call_sysaudio_ok 表示可以打开；否则为不能打开的原因
 */
int CallSystemAudioPrecheck(bool connected, uint32_t peerReports, uint8_t peerFlags);

/**
 *  电脑声音这一路的闪避增益，沿用屏幕共享的规则（ShareAudioSession::DuckingGainPercent）：用的是「排除本进程树」的
 *  回环时我们播放的对方说话声不会被采进去，不需要闪避；退回普通回环时，对方在说话就压到 10%，减少把对方的声音再送
 *  回去。
 *  @param excludingSelf 回环采集是否排除了本进程树（ShareSystemAudioCapture::IsExcludingSelf）
 *  @param peerLevel     对方说话声那一路最近的播放电平（均方根，0~32767）
 *  @return 增益百分比（100 或 10）
 */
int CallSystemAudioGainPercent(bool excludingSelf, int peerLevel);

//通话中共享电脑声音的发送部分：攒帧、闪避、编码、打时间戳
class CallSystemAudioSender
{
public:
    //内部采样率（Hz）与每帧采样数（20 毫秒），与说话声相同
    static const int kSampleRate = 48000;
    static const int kFrameSamples = 960;
    //每帧的时长（毫秒）：时间戳按帧序号乘以它
    static const int kFrameMs = 20;
    //编码码率（bps）：与屏幕共享的电脑声音相同（ShareAudioSession.cpp 的 kSysBitrate）
    static const int kBitrateBps = 64000;

    CallSystemAudioSender();

    CallSystemAudioSender(const CallSystemAudioSender&) = delete;
    CallSystemAudioSender& operator=(const CallSystemAudioSender&) = delete;

    /**
     *  开始一次共享：创建音乐模式的编码器，帧序号从 0 开始。已在运行时先停掉上一次。
     *  @param sink 编码包去向，不拥有，须在 Stop 返回之前一直有效；为 NULL 时返回 false
     *  @return 编码器创建成功返回 true
     */
    bool Start(CallSystemAudioPacketSink* sink);

    //结束共享：释放编码器、丢掉没攒满一帧的采样。幂等。
    void Stop();

    //是否正在共享（Start 成功之后、Stop 之前）
    bool IsRunning() const;

    /**
     *  喂入一段电脑声音。攒满一帧就按 gainPercent 调整音量、编码并回调 sink；编码失败的帧也计入帧序号，时间戳与实际
     *  经过的时长保持一致。没有在共享时直接丢弃。
     *  @param pcm         48 kHz 单声道 16 位采样，只在本次调用期间读取
     *  @param samples     采样数
     *  @param gainPercent 闪避增益百分比（100 为原样），见 CallSystemAudioGainPercent
     *  @return 本次凑成并处理的帧数
     */
    int Feed(const int16_t* pcm, int samples, int gainPercent);

    //本次共享以来处理过的帧数（编码失败的也算）
    uint64_t FramesSent() const;

private:
    mutable std::mutex         m_mtx;      //串行化 Start / Stop / Feed（UI 线程与回环采集线程）
    OpusAudioCodec             m_codec;    //电脑声音的编码器（音乐模式）；Start 时创建、Stop 时释放
    std::vector<int16_t>       m_accum;    //还没攒满一帧的采样
    std::vector<unsigned char> m_packet;   //编码输出的暂存
    CallSystemAudioPacketSink* m_sink;     //编码包去向，不拥有；没有在共享时为 NULL
    uint64_t                   m_frames;   //本次共享以来处理过的帧数（下一帧的帧序号）
    bool                       m_running;  //是否正在共享
};

} // namespace call
