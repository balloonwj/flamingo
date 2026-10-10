/**
 *  屏幕录制的混音，RecordAudioMixer.h
 *
 *  录制的声音有三路来源（docs/屏幕共享与录制方案.md 6.1）：本人麦克风、房间里其他人的声音（解码后的混音）、电脑声音
 *  （排除 Flamingo 进程的回环采集，因而不含他人说话声）。三路互不重叠，相加后限幅即可，不会录进两遍。
 *  各路由各自的线程按 48 kHz 单声道推入（Push），录制的写入线程按录制时长取出（Pull），某一路数据不够时那一路补静音。
 *  每路一个带互斥的环形缓冲（容量 2 秒），满了就丢弃新来的数据。
 *  语音字幕（docs/语音字幕方案.md 6.1）：可登记一个分路旁路（IRecordAudioTap），写入线程每取出一段时，把各路取到的声音
 *  连同这段声音在录制时间轴上的时刻交给它，字幕的时间因此与录像严格对齐，暂停的时段也不会错位。
 *
 *  典型用法：
 *      mixer.SetEnabled(record_audio_mic, true);
 *      mixer.Push(record_audio_mic, pcm, n);   //采集线程
 *      mixer.Pull(out, 960);                    //写入线程
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <memory>
#include <vector>

#include "../Call/AudioPcmUtils.h"

namespace screenrecord {

//声音来源
enum RecordAudioSource
{
    record_audio_mic    = 0,   //本人麦克风
    record_audio_others = 1,   //房间里其他人的声音（共享录制时才有）
    record_audio_system = 2,   //电脑声音
    record_audio_source_count = 3,   //来源的个数（不是一种来源）
};

/**
 *  分路旁路：写入线程每取出一段混音时，按路交出这一段（语音字幕识别用）。只在写入线程上调用，实现须尽快返回。
 */
class IRecordAudioTap
{
public:
    virtual ~IRecordAudioTap()
    {
    }

    /**
     *  某一路这一段的声音（这一路打开时每段都会调用；数据不够的部分已补成静音，时间轴连续）。
     *  @param source  来源，取值见 RecordAudioSource
     *  @param pcm     48 kHz 单声道 16 位采样，只在本次调用期间有效
     *  @param samples 采样数
     *  @param ptsMs   这一段第一个采样在录制时间轴上的时刻（毫秒，不含暂停）
     */
    virtual void OnRecordAudio(int source, const int16_t* pcm, size_t samples, int64_t ptsMs) = 0;
};

//三路声音的混音
class RecordAudioMixer
{
public:
    RecordAudioMixer();

    RecordAudioMixer(const RecordAudioMixer&) = delete;
    RecordAudioMixer& operator=(const RecordAudioMixer&) = delete;

    //打开或关闭某一路；关闭时丢弃已缓存的数据，之后推入的也丢弃
    void SetEnabled(int source, bool on);
    bool IsEnabled(int source) const;

    /**
     *  推入一段 48 kHz 单声道采样（任意线程）。
     *  @param source  来源，取值见 RecordAudioSource
     */
    void Push(int source, const int16_t* pcm, size_t samples);

    /**
     *  取出 samples 个混好的采样：各路相加后限幅到 16 位；某路不够时不足的部分按静音。
     *  @param out   [出参] 至少 samples 个元素
     *  @param ptsMs 这一段在录制时间轴上的时刻（毫秒）；为负时不交给分路旁路
     */
    void Pull(int16_t* out, size_t samples, int64_t ptsMs = -1);

    /**
     *  登记分路旁路（语音字幕）。登记与取消都只在写入线程不在运行时做（开始录制之前、录制结束之后），
     *  或由旁路自己保证取消后不再使用被取消的对象。
     *  @param tap 不拥有；传 NULL 取消
     */
    void SetTap(IRecordAudioTap* tap);

    /**
     *  取出某一路自上次取出以来推入数据的峰值（采样绝对值的最大值），并清零（第 24 期：录制条的电平指示，任意线程）。
     *  关闭的一路推入的数据不计。
     *  @param source 来源，取值见 RecordAudioSource；越界时返回 0
     *  @return 峰值，0~32768
     */
    int TakePeak(int source);

    //某一路已缓存的采样数
    size_t Available(int source) const;

    //丢弃各路已缓存的数据（如暂停后继续，避免把暂停期间的声音接上去）
    void Clear();

private:
    bool valid(int source) const;

    std::unique_ptr<call::PcmRingBuffer> m_buffers[record_audio_source_count];   //各路的缓冲
    std::atomic<bool>                    m_enabled[record_audio_source_count];   //各路是否打开
    std::atomic<int>                     m_peaks[record_audio_source_count];     //各路自上次 TakePeak 以来的峰值（第 24 期）
    std::vector<int16_t>                 m_scratch;   //取出一路时的临时缓冲（只在写入线程使用）
    std::atomic<IRecordAudioTap*>        m_tap;       //分路旁路（语音字幕，不拥有）；为 NULL 时不旁路
};

} // namespace screenrecord
