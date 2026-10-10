/**
 *  语音消息的播放器（语音消息第 2c 步，方案见 docs/语音消息方案.md 第 7.1 节）。
 *
 *  Play 时读出整个 Ogg Opus 文件并检查格式（VoiceOggReader）；之后扬声器的渲染线程每要一次数据，播放器就按需
 *  解码下一个 Opus 包（16 kHz），丢掉开头 pre-skip 个采样、按文件记录的有效采样数裁掉结尾补的静音，再重采样到
 *  设备采样率填进去。每播放 kVoicePlayProgressMs 毫秒报告一次进度，全部播完报告一次结束，之后只填静音。
 *
 *  扬声器抽象成 IVoiceRenderDevice：正式使用时是 WASAPI 播放（VoiceWasapiRenderDevice），单元测试换成不出声的
 *  假设备，由测试直接调用 OnAudioNeeded 取数据。
 *
 *  线程：Play / Stop 在调用方线程（模块的 UI 线程）；OnAudioNeeded 与通知在渲染线程上。播放器不在渲染线程里
 *  停止设备（设备的 Stop 会等渲染线程结束），收到「播完」通知的一方应回到 UI 线程再调用 Stop。
 *
 *  典型用法：
 *      voicemsg::VoicePlayer player;
 *      if (player.Play(&device, &listener, L"...\\ChatVoice\\<md5>.ogg")) { ... }
 *      ...                                  //listener 收到 OnVoicePlayFinished 后投递到 UI 线程
 *      player.Stop();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>

#include "VoiceOggFile.h"
#include "../Call/OpusAudioCodec.h"
#include "../Call/SpeexResampler.h"
#include "../Call/WasapiAudioIo.h"

namespace voicemsg {

//进度通知的间隔（毫秒）
const int kVoicePlayProgressMs = 100;
//播放时允许读取的最大文件字节数（与服务端的语音文件上限相同）
const size_t kVoiceMaxFileBytes = 1024 * 1024;

/**
 *  扬声器设备。正式使用时打开系统默认扬声器；单元测试用的假设备不出声，由测试直接取数据。
 */
class IVoiceRenderDevice
{
public:
    virtual ~IVoiceRenderDevice()
    {
    }

    /**
     *  打开设备并开始播放。
     *  @param source        数据来源（播放器本身），须存活到 Stop 返回
     *  @param outSampleRate [出参] 设备采样率（Hz）
     *  @return 成功返回 true
     */
    virtual bool Start(call::WasapiRenderSource* source, int& outSampleRate) = 0;

    //停止播放；返回之后不再回调 source。可以重复调用
    virtual void Stop() = 0;
};

/**
 *  正式使用的扬声器设备：系统默认扬声器，共享模式（call::WasapiRender）。
 */
class VoiceWasapiRenderDevice : public IVoiceRenderDevice
{
public:
    //打开系统默认扬声器；失败时返回 false
    virtual bool Start(call::WasapiRenderSource* source, int& outSampleRate) override;

    //停止播放，等渲染线程结束
    virtual void Stop() override;

private:
    call::WasapiRender m_render;   //WASAPI 播放，Start 时打开、Stop 时关闭
};

/**
 *  播放过程中的通知。在渲染线程上触发，实现方不得阻塞，需要更新界面或停止播放时应投递到 UI 线程。
 */
class IVoicePlayerListener
{
public:
    virtual ~IVoicePlayerListener()
    {
    }

    /**
     *  播放进度，每 kVoicePlayProgressMs 毫秒一次。
     *  @param playedMs 已播放的时长（毫秒），逐次递增
     *  @param totalMs  总时长（毫秒）
     */
    virtual void OnVoicePlayProgress(int64_t playedMs, int64_t totalMs) = 0;

    //全部播完（只通知一次）；之后播放器只填静音，等调用方 Stop
    virtual void OnVoicePlayFinished() = 0;
};

/**
 *  语音消息的播放器，一次只播一个文件。用法见文件头。
 */
class VoicePlayer : public call::WasapiRenderSource
{
public:
    VoicePlayer();
    ~VoicePlayer();

    VoicePlayer(const VoicePlayer&) = delete;
    VoicePlayer& operator=(const VoicePlayer&) = delete;

    /**
     *  开始播放一个文件。正在播放时先停止前一个。
     *  @param device   扬声器设备，不能为 NULL；调用方持有，须存活到 Stop 返回
     *  @param listener 通知对象，可以为 NULL；调用方持有，须存活到 Stop 返回
     *  @param path     语音文件的完整路径
     *  @return 文件能读出且格式正确、解码器与设备都就绪时返回 true
     */
    bool Play(IVoiceRenderDevice* device, IVoicePlayerListener* listener, const std::wstring& path);

    //停止播放（播完之后、或中途停止都调用它）。没有在播放时什么也不做
    void Stop();

    //是否处于播放中（Play 成功之后、Stop 之前，包括已播完等待 Stop 的这段时间）
    bool IsPlaying() const;

    //当前文件的总时长（毫秒）；没有在播放时为 0
    int64_t DurationMs() const;

    /**
     *  渲染线程要数据（WasapiRenderSource 的实现；单元测试直接调用它取数据）。
     *  @param mono   [出参] frames 个单声道 16 位采样，采样率为设备采样率；播完或没有在播放时填静音
     *  @param frames 需要的采样数
     */
    virtual void OnAudioNeeded(int16_t* mono, int frames) override;

private:
    //解码下一个包，把有效采样（去掉 pre-skip、裁掉结尾）重采样后追加到 m_output。没有包可解时返回 false。
    //调用方须持有 m_mutex
    bool decodeNextLocked();

    /**
     *  停止设备。调用方不得持有 m_mutex：设备的 Stop 会等渲染线程结束，而渲染线程回调时要取这把锁。
     *  @return 停止前正在播放返回 true
     */
    bool stopDevice();

    //释放解码器与重采样器、清空播放状态。调用方须持有 m_mutex
    void resetLocked();

private:
    mutable std::mutex                 m_mutex;         //保护以下播放状态（渲染线程与调用方线程共用）
    IVoiceRenderDevice*                m_device;        //扬声器设备，Play 时传入，不持有
    IVoicePlayerListener*              m_listener;      //通知对象，Play 时传入，不持有，可以为 NULL
    bool                               m_playing;       //是否处于播放中（设备打开之后置位）
    bool                               m_finished;      //是否已全部播完并通知过
    bool                               m_resampling;    //设备采样率不是 16 kHz 时为 true
    call::SpeexResampler               m_resampler;     //16 kHz → 设备采样率
    call::OpusAudioCodec               m_codec;         //16 kHz、20 毫秒一帧的 Opus 解码器
    std::vector<std::vector<uint8_t> > m_packets;       //文件里的 Opus 包
    size_t                             m_nextPacket;    //下一个要解码的包的下标
    int64_t                            m_skip16k;       //开头还要丢弃的 16 kHz 采样数（pre-skip 换算）
    int64_t                            m_remain16k;     //还可以输出的有效 16 kHz 采样数（裁掉结尾补的静音）
    int64_t                            m_totalMs;       //文件总时长（毫秒）
    int                                m_deviceRate;    //设备采样率（Hz）
    std::vector<int16_t>               m_output;        //已解码、已重采样、还没交给设备的采样（设备采样率）
    size_t                             m_outputPos;     //m_output 中下一个要交出的位置
    int64_t                            m_playedSamples; //已交给设备的有效采样数（设备采样率），算进度用
    int64_t                            m_lastProgressMs;//上一次进度通知时的已播放毫秒数
};

} // namespace voicemsg
