/**
 *  单声道音频重采样（speexdsp resampler 封装），SpeexResampler.h
 *
 *  把 speexdsp 的 speex_resampler 包成一个面向通话音频管线的 C++ 小类：在「设备采样率」
 *  与「引擎内部统一采样率（48kHz）」之间做单声道 16 位 PCM 重采样，使任意采样率的麦克风 /
 *  扬声器（如 44100Hz）都能接入——采集侧把设备率转成 48kHz 喂编码 / AEC，播放侧把 48kHz
 *  解码音频转成设备率播放。
 *
 *  speex_resampler 内部有连续状态（相位 / 滤波器记忆），故一个实例对应一条单向流（采集一个、
 *  播放一个），须按帧顺序连续喂、不可乱序。输出样本数随相位浮动（非严格按比例），故 Process
 *  以「追加到调用方 vector」的方式产出可变长结果，调用方据实际长度消费。
 *
 *  约定单实例只在单线程使用（采集重采样在采集线程、播放重采样在渲染线程）。依赖 speexdsp，
 *  纯逻辑，可单元测试（SpeexResamplerTests.cpp）。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include <stdint.h>
#include <vector>

//speexdsp 不透明重采样器状态前向声明（定义在 speex 头；本头不引入 speex 头）。
struct SpeexResamplerState_;

namespace call {

//单声道 int16 重采样器。
class SpeexResampler
{
public:
    SpeexResampler();
    ~SpeexResampler();

    SpeexResampler(const SpeexResampler&) = delete;
    SpeexResampler& operator=(const SpeexResampler&) = delete;

    /**
     *  初始化重采样器（单声道）。重复调用先释放上一个（幂等）。
     *  @param inRate  输入采样率(Hz)
     *  @param outRate 输出采样率(Hz)
     *  @param quality 重采样质量 0~10（越高越好越慢；语音取 5 左右即可）
     *  @return 成功返回 true；参数非法或创建失败返回 false
     */
    bool Init(int inRate, int outRate, int quality);

    //释放重采样器（析构会自动调用）。
    void Close();

    //是否已就绪。
    bool IsReady() const
    {
        return m_st != NULL;
    }

    /**
     *  重采样一段输入，把结果追加到 outAppend 尾部（不清空 outAppend 已有内容）。
     *  @param in        输入采样（inCount 个单声道 int16，非空）
     *  @param inCount   输入采样数
     *  @param outAppend [出参] 重采样结果追加到此 vector 尾部；实际追加数量随相位浮动
     */
    void Process(const int16_t* in, int inCount, std::vector<int16_t>& outAppend);

    int InRate() const
    {
        return m_inRate;
    }

    int OutRate() const
    {
        return m_outRate;
    }

private:
    SpeexResamplerState_* m_st;       //重采样器状态（不透明）
    int                   m_inRate;   //输入采样率(Hz)
    int                   m_outRate;  //输出采样率(Hz)
};

} // namespace call
