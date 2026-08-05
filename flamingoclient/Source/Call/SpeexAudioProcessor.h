/**
 *  通话采集端音频处理（回声消除 + 降噪），SpeexAudioProcessor.h
 *
 *  封装 speexdsp 的回声消除器（speex_echo）与预处理器（speex_preprocess，做降噪 / 残余
 *  回声抑制）：通话时麦克风采集帧在编码前先经本处理器，去掉从扬声器经空气回到麦克风的
 *  对端声音（回声），再降噪——这样不戴耳机也不会把对端声音又发回去形成回授啸叫。
 *
 *  固定单声道、固定帧长。回声参考（ref）是「刚播放给扬声器的对端音频帧」，由上层
 *  （CallAudioEngine）经 far-end 环形缓冲取来与麦克风帧对齐后传入；speexdsp 的自适应
 *  滤波器据其建模回声路径、从麦克风信号里减掉回声。设备回声路径延迟由滤波器尾长
 *  （filterLengthSamples）覆盖，故 ref 不需精确对齐到样本，FIFO 顺序 + 足够尾长即可。
 *
 *  线程：约定所有方法在同一线程（采集线程）调用——echo / preprocess 状态非线程安全。
 *  本模块依赖 speexdsp，纯逻辑（不碰设备 / 网络），可单元测试（SpeexAudioProcessorTests.cpp）。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

//speexdsp 不透明状态结构体前向声明（定义在 speex 头；本头不引入 speex 头）。
struct SpeexEchoState_;
struct SpeexPreprocessState_;

namespace call {

//通话采集端音频处理器：speexdsp 回声消除(AEC) + 降噪。
class SpeexAudioProcessor
{
public:
    SpeexAudioProcessor();
    ~SpeexAudioProcessor();

    SpeexAudioProcessor(const SpeexAudioProcessor&) = delete;
    SpeexAudioProcessor& operator=(const SpeexAudioProcessor&) = delete;

    /**
     *  初始化回声消除器与降噪器。重复调用先释放上一组（幂等）。
     *  @param sampleRate          采样率(Hz)，须与麦克风 / 参考帧一致
     *  @param frameSamples        每帧采样数（ProcessCapture 须按此帧长喂）
     *  @param filterLengthSamples 回声滤波器尾长（样本数）：须 ≥ 设备回声路径延迟，
     *                             典型取 100~250ms 对应样本数（如 48000Hz 下 4800~12000）
     *  @return 成功返回 true；参数非法或 speexdsp 创建失败返回 false
     */
    bool Init(int sampleRate, int frameSamples, int filterLengthSamples);

    //释放回声 / 降噪状态（析构会自动调用）。
    void Close();

    /**
     *  处理一帧麦克风采集：先用 ref 消回声，再降噪，输出处理后的一帧。
     *  @param mic    麦克风采集帧（frameSamples 个单声道 int16，非空）
     *  @param ref    回声参考帧（刚播放的对端音频，frameSamples 个 int16）；传 NULL 表示
     *                暂无参考（不消回声，仅降噪）
     *  @param out    [出参] 处理后帧（frameSamples 个 int16，非空；可与 mic 不同缓冲）
     *  @param frames 帧采样数，须等于 Init 时的 frameSamples（不符则原样拷贝 mic→out）
     */
    void ProcessCapture(const int16_t* mic, const int16_t* ref, int16_t* out, int frames);

    //是否已成功初始化。
    bool IsReady() const
    {
        return m_echo != NULL && m_preprocess != NULL;
    }

    //每帧采样数；未初始化时为 0。
    int FrameSamples() const
    {
        return m_frameSamples;
    }

private:
    SpeexEchoState_*       m_echo;          //回声消除器（不透明）
    SpeexPreprocessState_* m_preprocess;    //预处理器（降噪 / 残余回声抑制，不透明）
    int                    m_frameSamples;  //每帧采样数
};

} // namespace call
