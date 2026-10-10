/**
 *  多路语音的解码、抖动缓冲与混音，AudioStreamMixer.h
 *
 *  播放端按「发言人」分路：每一路有自己的 Opus 解码器与抖动缓冲，播放回调里把各路取出的采样叠加后限幅，
 *  得到最终送往扬声器的一帧；同时记下各路这一帧的电平（均方根），供「谁在说话」判断。一对一通话就是只有一路
 *  （docs/屏幕共享与录制方案.md 5.7、第 4 期 4a），屏幕共享房间里每个座位的麦克风、电脑声音各是一路。
 *
 *  2026-10-06 起（docs/音视频通话方案.md 第 9.9 节方案 B）：编码包不再在收包线程上解码后押入 PCM 缓冲，
 *  而是按「媒体时间戳」放进该路的 CallJitterBuffer（包装 speexdsp 的抖动缓冲），由渲染线程在 Mix 时每 20 毫秒取一个包
 *  解码。缺包时连续 kMaxConcealFrames 帧以内用 Opus 丢包补偿，再多就输出静音；speexdsp 为降延迟跳过帧、或时间线重置
 *  使时间戳不连续时，新帧开头做 kSmoothSamples 个采样的平滑过渡。延迟由 speexdsp 自适应，网络卡顿之后能降回来。
 *  一直只收过 PCM（PushPcm）的路仍按原来的方式：有多少取多少，不足补静音。
 *
 *  线程：PushEncoded / PushPcm 在收包线程上调用（同一路只会被一个线程推入），Mix 在渲染线程上调用，
 *  RemoveStream / Clear / 电平查询可在任意线程调用。路表由内部互斥保护；每路的抖动缓冲自带互斥；
 *  解码器与待播放的采样只在渲染线程上使用。
 *  纯逻辑（不碰设备与网络），用例见 Source/Tests/AudioStreamMixerTests.cpp。
 *
 *  典型用法：
 *      AudioStreamMixer mixer(48000, 960);
 *      mixer.PushEncoded(seat, packet, len, tsSamples);   //收包线程；tsSamples = 发送端采集帧序号 × 960
 *      mixer.Mix(out, frames);                            //渲染线程
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "CallJitterBuffer.h"   //CallJbStats：AudioStreamStats 的成员

namespace call {

class OpusAudioCodec;
class PcmRingBuffer;
class CallJitterBuffer;

//缺包时用 Opus 丢包补偿的最多连续帧数，超过后输出静音（长时间补偿会发出怪声）
const int kMaxConcealFrames = 3;
//时间戳不连续处平滑过渡的采样数（48 kHz 下 2.5 毫秒）
const int kSmoothSamples = 120;

//一路编码包的播放统计（2026-10-06，方案 B 第 B4 步），全部从该路创建起累计，调用方取两次之差
struct AudioStreamStats
{
    CallJbStats m_jb;             //抖动缓冲的计数（含正常播放时的缓冲深度累计）
    uint64_t    m_plcFrames;      //缺包时用 Opus 丢包补偿补出的帧数
    uint64_t    m_silenceFrames;  //缺包时补静音的帧数（连续缺失超过 kMaxConcealFrames、或还没解码过包）

    AudioStreamStats();
};

//多路语音混音器
class AudioStreamMixer
{
public:
    /**
     *  @param sampleRate   内部采样率（Hz），Opus 支持的取值，一般为 48000
     *  @param frameSamples 每帧采样数（20 ms 为 sampleRate / 50）
     */
    AudioStreamMixer(int sampleRate, int frameSamples);
    ~AudioStreamMixer();

    AudioStreamMixer(const AudioStreamMixer&) = delete;
    AudioStreamMixer& operator=(const AudioStreamMixer&) = delete;

    /**
     *  推入某一路的一个 Opus 包：按媒体时间戳放进该路的抖动缓冲，Mix 时再解码。该路不存在时先创建。
     *  丢包不必推入任何东西：到了该播放的时候抖动缓冲会报告缺失，Mix 自己补。
     *  @param streamId  路号（调用方自定，如座位号 × 轨道）
     *  @param data      Opus 包（借用，调用期间有效）
     *  @param len       字节数
     *  @param tsSamples 媒体时间戳：发送端的采集帧序号 × 每帧采样数（48 kHz 下为 960），32 位回绕
     *  @return 放入返回 true；data 为 NULL 或 len 为 0 时返回 false
     */
    bool PushEncoded(int streamId, const unsigned char* data, size_t len, uint32_t tsSamples);

    /**
     *  推入某一路的 PCM（已在内部采样率上）。该路不存在时先创建。
     *  @param pcm   16 位单声道采样
     *  @param count 采样数
     */
    void PushPcm(int streamId, const int16_t* pcm, size_t count);

    /**
     *  混出 frames 个采样：各路各取 frames 个（不足的部分按静音），叠加后限幅到 16 位；
     *  同时记下每一路这一次取到的采样的电平。没有任何一路时输出静音。
     *  @param out    [出参] 输出缓冲，长度至少 frames
     *  @param frames 采样数
     */
    void Mix(int16_t* out, int frames);

    //删除一路（成员离开、关掉电脑声音时）；正在混音的那一帧不受影响
    void RemoveStream(int streamId);

    //删除全部路
    void Clear();

    /**
     *  某一路最近一次混音时的电平（均方根，0~32767）。
     *  @return 该路存在返回 true
     */
    bool GetLevel(int streamId, int& rms) const;

    /**
     *  某一路累计的播放计数（2026-10-05 起，网络状况标识第 1 期的「卡顿率」来源）：混音时向该路请求了多少采样、
     *  其中有多少因抖动缓冲里没有数据而补了静音。两者都从该路创建起累计，调用方取差值算一段时间内的比例。
     *  注意：对方关麦或断流时没有新数据，补静音的比例会接近 100%，判断「卡顿」时须结合这段时间是否收到过音频包。
     *  @param requested [出参] 累计请求的采样数
     *  @param concealed [出参] 累计补静音的采样数
     *  @return 该路存在返回 true
     */
    bool GetPlayoutCounters(int streamId, uint64_t& requested, uint64_t& concealed) const;

    //当前所有路号（升序）
    std::vector<int> StreamIds() const;

    /**
     *  某一路编码包的播放统计（2026-10-06，方案 B 第 B4 步）。任意线程可调用。
     *  @param out [出参] 统计
     *  @return 该路存在返回 true（只收过 PCM 的路也返回 true，各项为 0）
     */
    bool GetStreamStats(int streamId, AudioStreamStats& out) const;

private:
    //一段待播放的采样（解码出的一帧，或补出的丢包补偿 / 静音）
    struct PendingChunk
    {
        std::vector<int16_t> m_pcm;         //采样
        size_t               m_pos;         //已取走的个数
        bool                 m_concealed;   //是否为补出来的（丢包补偿或静音），计入补静音计数

        PendingChunk();
    };

    //一路的状态
    struct Stream
    {
        std::unique_ptr<OpusAudioCodec>   m_decoder;   //该路的 Opus 解码器（2026-10-06 起只在渲染线程上使用）
        std::unique_ptr<PcmRingBuffer>    m_jitter;    //PushPcm 推入的 PCM 缓冲（推入线程写、渲染线程读，自带互斥）
        std::unique_ptr<CallJitterBuffer> m_packets;   //编码包的抖动缓冲（推入线程放、渲染线程取，自带互斥）
        std::atomic<bool>                 m_encoded;   //是否收到过编码包：收到过就按抖动缓冲取，否则按 PCM 缓冲取
        std::deque<PendingChunk>          m_pending;   //已解码或补出、还没取走的采样（只在渲染线程使用）
        int                               m_missingRun;//连续缺包的帧数（只在渲染线程使用）
        bool                              m_decodedAny;//是否解码过至少一个包（解码器有了状态才做丢包补偿；渲染线程）
        bool                              m_hasExpect; //m_expectTs 是否有效（渲染线程）
        uint32_t                          m_expectTs;  //下一个包应有的时间戳，用来发现跳变（渲染线程）
        int16_t                           m_lastSample;//最近产出的最后一个采样，平滑过渡的起点（渲染线程）
        int                               m_level;     //最近一次混音时的电平，受 m_mutex 保护
        uint64_t                          m_requested; //混音时累计向该路请求的采样数，受 m_mutex 保护
        uint64_t                          m_concealed; //其中补静音（含丢包补偿）的采样数，受 m_mutex 保护
        std::atomic<uint64_t>             m_plcFrames; //丢包补偿补出的帧数（渲染线程写，任意线程读）
        std::atomic<uint64_t>             m_silenceFrames; //补静音的帧数（渲染线程写，任意线程读）

        Stream();
        ~Stream();
    };

    //取某一路，不存在时创建；返回的共享指针在路被删除后仍然有效
    std::shared_ptr<Stream> streamFor(int streamId);

    /**
     *  渲染线程：从编码包这一路取满 frames 个采样到 dst（不够时从抖动缓冲取包解码或补出）。
     *  @param concealed  [出参] 其中补出来的采样个数
     *  @param realEnergy [出参] 真正解码出来的那部分采样的平方和（补出来的按静音计，不计入），用于这一路的电平：
     *                    丢包补偿延续的是之前的声音，不应让「正在说话」的指示亮着
     */
    void takeFromPackets(Stream& s, int16_t* dst, int frames, size_t& concealed, double& realEnergy);

    //渲染线程：从抖动缓冲取一段（一个包或一段缺失 / 插入），解码或补出后追加到待播放的采样
    void produceChunk(Stream& s);

    int                                     m_sampleRate;     //内部采样率（Hz）
    int                                     m_frameSamples;   //每帧采样数
    mutable std::mutex                      m_mutex;          //保护路表与各路电平
    std::map<int, std::shared_ptr<Stream> > m_streams;        //路号 → 该路状态
    std::vector<int32_t>                    m_sum;            //混音累加缓冲（只在渲染线程使用）
    std::vector<int16_t>                    m_take;           //从一路取采样的临时缓冲（只在渲染线程使用）
};

/**
 *  由电平判断「是否在说话」：电平超过门限即判为在说话，降到门限以下后再保持一小段时间才判为停止，
 *  避免字与字之间的停顿让指示灯闪烁。纯逻辑，每路一个实例。
 */
class SpeakingDetector
{
public:
    /**
     *  @param threshold 判为在说话的电平门限（均方根，0~32767）
     *  @param holdMs    电平降到门限以下后，仍判为在说话的时长（毫秒）
     */
    SpeakingDetector(int threshold, int holdMs);

    /**
     *  喂入一次电平。
     *  @param rms   电平
     *  @param nowMs 当前时间（毫秒，单调递增即可）
     *  @return 喂入后是否判为在说话
     */
    bool Update(int rms, int64_t nowMs);

    //当前是否判为在说话
    bool IsSpeaking() const;

private:
    int     m_threshold;    //门限
    int     m_holdMs;       //保持时长（毫秒）
    int64_t m_lastLoudMs;   //最近一次电平超过门限的时间；-1 表示从未超过
    bool    m_speaking;     //当前判断
};

} // namespace call
