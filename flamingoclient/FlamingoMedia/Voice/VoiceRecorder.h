/**
 *  语音消息的录音器（语音消息第 2b 步，方案见 docs/语音消息方案.md 第 7.1 节）。
 *
 *  麦克风采集到的单声道 16 位 PCM（设备采样率）→ 重采样到 16 kHz → 每 20 毫秒编一帧 Opus（24 kbps）→
 *  写进 Ogg Opus 文件（VoiceOggWriter）。录音过程中每 100 毫秒报告一次音量（录音条上的波形）；录满
 *  kVoiceMaxSeconds 秒后不再录入，通知调用方，等用户确认「发送」（Stop）或「取消」（Cancel），不自动发送。
 *  Stop 时补齐最后一帧并多编静音帧，使解码端能完整输出最后的声音，再按实际录入的采样数裁剪文件时长，
 *  写盘并算出 kVoiceWaveBars 个波形值（放进消息的 wave 字段）。
 *
 *  麦克风抽象成 IVoiceCaptureDevice：正式使用时是 WASAPI 采集（VoiceWasapiCaptureDevice），单元测试换成
 *  不碰真实设备的假设备，由测试直接调用 OnAudioCaptured 喂数据。
 *
 *  第 6d 步起支持暂停与继续：暂停时停止设备、保留已录的内容与编码状态，继续时重新打开设备接着录；暂停中
 *  照常可以 Stop（写出已录的部分）或 Cancel。
 *
 *  线程：Start / Stop / Cancel / Pause / Resume 在调用方线程（模块的 UI 线程）；OnAudioCaptured 与两个通知在
 *  采集线程上，内部用互斥锁保护录音状态。
 *
 *  典型用法：
 *      voicemsg::VoiceRecorder recorder;
 *      recorder.Start(&device, &listener, L"...\\ChatVoice\\rec.ogg");
 *      ...                                    //用户点「发送」
 *      voicemsg::VoiceRecordResult result;
 *      if (recorder.Stop(result)) { ... }     //result.m_path、m_durationMs、m_wave
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

//最长录音时长（秒），见方案「最长 2 分钟」
const int kVoiceMaxSeconds = 120;
//消息里的波形值个数，见方案 V4
const int kVoiceWaveBars = 40;
//Opus 编码码率（bit/s），见方案 V2
const int kVoiceBitrateBps = 24000;
//音量通知的间隔（毫秒）
const int kVoiceLevelIntervalMs = 100;
//音量值的上限（波形值同样取 0~255）
const int kVoiceLevelMax = 255;

/**
 *  麦克风设备。正式使用时打开系统默认麦克风；单元测试用的假设备什么都不做，由测试直接喂数据。
 */
class IVoiceCaptureDevice
{
public:
    virtual ~IVoiceCaptureDevice()
    {
    }

    /**
     *  打开设备并开始采集。
     *  @param handler       采集数据的接收方（录音器本身），须存活到 Stop 返回
     *  @param outSampleRate [出参] 设备采样率（Hz）
     *  @return 成功返回 true
     */
    virtual bool Start(call::WasapiCaptureHandler* handler, int& outSampleRate) = 0;

    //停止采集；返回之后不再回调 handler。可以重复调用
    virtual void Stop() = 0;
};

/**
 *  正式使用的麦克风设备：系统默认麦克风，共享模式（call::WasapiCapture）。
 */
class VoiceWasapiCaptureDevice : public IVoiceCaptureDevice
{
public:
    //打开系统默认麦克风；失败时返回 false
    virtual bool Start(call::WasapiCaptureHandler* handler, int& outSampleRate) override;

    //停止采集，等采集线程结束
    virtual void Stop() override;

private:
    call::WasapiCapture m_capture;   //WASAPI 采集，Start 时打开、Stop 时关闭
};

/**
 *  录音过程中的通知。在采集线程上触发，实现方不得阻塞，需要更新界面时应投递到 UI 线程。
 */
class IVoiceRecorderListener
{
public:
    virtual ~IVoiceRecorderListener()
    {
    }

    /**
     *  最近 kVoiceLevelIntervalMs 毫秒的音量。
     *  @param level 0~kVoiceLevelMax，按分贝换算（见 VoiceLevelFromRms）
     */
    virtual void OnVoiceLevel(int level) = 0;

    //已录满 kVoiceMaxSeconds 秒，之后的音频不再录入；只通知一次，等调用方 Stop 或 Cancel
    virtual void OnVoiceLimitReached() = 0;
};

/**
 *  一次录音的结果（Stop 成功时填写）。
 */
struct VoiceRecordResult
{
    std::wstring         m_path;        //录音文件的完整路径（即 Start 时给的路径）
    int64_t              m_durationMs;  //有效时长（毫秒），按实际录入的采样数计算
    int64_t              m_fileBytes;   //文件字节数
    std::vector<uint8_t> m_wave;        //kVoiceWaveBars 个波形值，0~kVoiceLevelMax

    VoiceRecordResult();
};

/**
 *  语音消息的录音器，一次只录一段。用法见文件头。
 */
class VoiceRecorder : public call::WasapiCaptureHandler
{
public:
    VoiceRecorder();
    ~VoiceRecorder();

    VoiceRecorder(const VoiceRecorder&) = delete;
    VoiceRecorder& operator=(const VoiceRecorder&) = delete;

    /**
     *  开始录音。正在录音时返回 false。
     *  @param device   麦克风设备，不能为 NULL；调用方持有，须存活到 Stop 或 Cancel 返回
     *  @param listener 通知对象，可以为 NULL；调用方持有，须存活到 Stop 或 Cancel 返回
     *  @param outPath  录音文件的完整路径；Stop 时才写盘，Cancel 不写
     *  @return 编码器、重采样器初始化成功且设备打开成功时返回 true
     */
    bool Start(IVoiceCaptureDevice* device, IVoiceRecorderListener* listener, const std::wstring& outPath);

    /**
     *  停止录音并写文件。
     *  @param result [出参] 录音结果，返回 true 时有效
     *  @return 写文件成功返回 true；没有在录音、一个采样都没录到、或写文件失败时返回 false，不留下文件
     */
    bool Stop(VoiceRecordResult& result);

    //取消录音：停止设备，丢弃录到的内容，不写文件。没有在录音时什么也不做；暂停中同样可以取消
    void Cancel();

    /**
     *  暂停录音（语音消息第 6d 步）：停止设备（关闭麦克风采集），已录的内容与编码状态保留，之后可以 Resume
     *  接着录，也可以直接 Stop 写出已录的部分或 Cancel 丢弃。暂停期间设备若仍有数据回调，一律不计入。
     *  @return 正在录音（未暂停）时暂停并返回 true；没有在录音、已经暂停时返回 false
     */
    bool Pause();

    /**
     *  继续已暂停的录音：用 Start 时的设备重新开始采集，接着往同一段录音里写。设备采样率与暂停前不同时（如换了
     *  默认麦克风）按新的采样率重建重采样器。
     *  @return 设备重新打开成功返回 true；没有已暂停的录音、或设备打不开时返回 false，失败时保持暂停
     */
    bool Resume();

    //是否已暂停（Pause 成功之后、Resume 成功或 Stop / Cancel 之前）
    bool IsPaused() const;

    /**
     *  把暂停前已录的部分另存成一份完整的 Ogg Opus 文件，供暂停中试听（2026-10-05）。录音状态、编码器与已录内容
     *  都不改，之后照常可以 Resume 接着录、Stop 写出全部内容或 Cancel。最后不足一帧的采样与编码器 lookahead 里
     *  还没输出的一小段（合计不到 30 毫秒）不在另存的文件里。
     *  @param path          另存文件的完整路径；已存在时覆盖
     *  @param outDurationMs [出参] 另存文件的时长（毫秒），返回 true 时有效
     *  @return 已暂停、至少编出了一帧且写文件成功时返回 true；没有暂停、还没编出一帧或写文件失败时返回 false
     */
    bool SnapshotPaused(const std::wstring& path, int64_t& outDurationMs);

    //是否有一段录音在进行（Start 成功之后、Stop 或 Cancel 之前）；暂停中也算，录音会话并未结束
    bool IsRecording() const;

    //已录入的有效时长（毫秒），按重采样到 16 kHz 后的采样数计算，录满后停在 kVoiceMaxSeconds 秒
    int64_t RecordedMs() const;

    /**
     *  接收一段采集到的音频（WasapiCaptureHandler 的实现，在采集线程上调用；单元测试直接调用它喂数据）。
     *  @param mono   单声道 16 位 PCM，采样率为设备采样率；只在本次调用内有效
     *  @param frames 采样数
     */
    virtual void OnAudioCaptured(const int16_t* mono, int frames) override;

private:
    /**
     *  编码一帧 16 kHz 的 PCM（kVoiceSamplesPerFrame 个采样）并写进文件。调用方须持有 m_mutex。
     *  @param pcm        一帧采样
     *  @param countLevel 是否把这一帧的音量计入波形；Stop 时为让解码端输出最后的声音而补的静音帧传 false
     */
    void encodeFrameLocked(const int16_t* pcm, bool countLevel);

    /**
     *  停止设备。调用方不得持有 m_mutex：设备的 Stop 会等采集线程结束，而采集线程回调时要取这把锁。
     *  @return 停止前正在录音返回 true；没有在录音返回 false（什么也不做）
     */
    bool stopDevice();

    //释放编码器与重采样器、清空录音状态（不写文件）。调用方须持有 m_mutex
    void resetLocked();

private:
    mutable std::mutex      m_mutex;            //保护以下录音状态（采集线程与调用方线程共用）
    IVoiceCaptureDevice*    m_device;           //麦克风设备，Start 时传入，不持有
    IVoiceRecorderListener* m_listener;         //通知对象，Start 时传入，不持有，可以为 NULL
    std::wstring            m_path;             //录音文件路径
    bool                    m_recording;        //是否正在接收采集数据（设备打开且未暂停）
    bool                    m_paused;           //是否已暂停：设备已停止，录音状态保留，等 Resume / Stop / Cancel
    int                     m_deviceRate;       //设备当前的采样率（Hz），Resume 时据此判断要不要重建重采样器
    bool                    m_limitReached;     //是否已录满 kVoiceMaxSeconds 秒（之后的音频丢弃）
    bool                    m_resampling;       //设备采样率不是 16 kHz 时为 true，需要重采样
    call::SpeexResampler    m_resampler;        //设备采样率 → 16 kHz
    call::OpusAudioCodec    m_codec;            //16 kHz、20 毫秒一帧的 Opus 编码器
    VoiceOggWriter          m_writer;           //Ogg Opus 文件内容
    std::vector<int16_t>    m_pending16k;       //重采样后还不够一帧的 16 kHz 采样
    std::vector<int16_t>    m_resampled;        //重采样输出的临时缓冲，避免每次回调都重新分配
    int64_t                 m_samples16k;       //已录入的 16 kHz 采样数（有效时长的依据）
    int                     m_preSkip48k;       //pre-skip（48 kHz 计），由编码器 lookahead 换算
    std::vector<uint8_t>    m_frameLevels;      //每一帧的音量值，Stop 时归并成波形
    double                  m_levelSquareSum;   //当前音量通知区间内采样平方和
    int                     m_levelSamples;     //当前音量通知区间内的采样数
};

/**
 *  把一段音频的均方根换算成音量值：按分贝线性映射，kVoiceLevelFloorDb 及以下为 0，0 dB（满幅）为 kVoiceLevelMax。
 *  @param rms 16 位采样的均方根，0~32767
 *  @return 0~kVoiceLevelMax
 */
uint8_t VoiceLevelFromRms(double rms);

/**
 *  把每一帧的音量值归并成 count 个波形值：整段等分成 count 个区间，每个区间取最大值；帧数少于 count 时
 *  按比例重复取帧。
 *  @param frameLevels 每一帧的音量值
 *  @param count       波形值个数
 *  @return count 个波形值；frameLevels 为空时全部为 0
 */
std::vector<uint8_t> BuildVoiceWave(const std::vector<uint8_t>& frameLevels, int count);

} // namespace voicemsg
