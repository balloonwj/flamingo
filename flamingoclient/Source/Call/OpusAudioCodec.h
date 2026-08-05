/**
 *  Opus 语音编解码薄封装，OpusAudioCodec.h
 *
 *  把 Opus 的 C 接口（OpusEncoder / OpusDecoder）包成一个面向音视频通话音频管线的
 *  C++ 小类：固定单声道，按构造时给定的采样率与每帧采样数，把一帧 16 位 PCM 编成
 *  一个 Opus 包、或把一个 Opus 包解回一帧 16 位 PCM。第二期音频管线用它：
 *  WASAPI 采集到的 PCM → Encode → 经中转发出；中转收到的包 → Decode → 送扬声器播放。
 *
 *  设计要点：
 *    - 只做单声道（1 对 1 语音通话够用），编码 application 用 VOIP（为语音优化）。
 *    - 帧长固定：每次 Encode 须喂整 frameSamples 个采样（= 采样率×帧时长/1000，
 *      如 48000Hz×20ms/1000 = 960）；这是 Opus 允许的标准帧长之一。
 *    - 头文件只前向声明 Opus 不透明结构体、不引入 opus.h，避免把 Opus 头泄漏给每个
 *      包含者；opus.h 只在 .cpp 内引入。
 *    - 纯逻辑（不碰设备 / 网络），便于单元测试，见 Source/Tests/OpusAudioCodecTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>

//Opus 不透明编解码器结构体前向声明（定义在 opus.h；本头不引入 opus.h）。
struct OpusEncoder;
struct OpusDecoder;

namespace call {

//单声道语音用的 Opus 编解码器封装。一个实例持有一对 编码器 + 解码器，贯穿一次通话。
class OpusAudioCodec
{
public:
    OpusAudioCodec();
    ~OpusAudioCodec();

    OpusAudioCodec(const OpusAudioCodec&) = delete;
    OpusAudioCodec& operator=(const OpusAudioCodec&) = delete;

    /**
     *  创建并初始化编码器与解码器。重复调用会先释放上一对再重建（幂等）。
     *  @param sampleRate   采样率（Hz）；Opus 仅支持 8000/12000/16000/24000/48000
     *  @param frameSamples 每帧采样数（= sampleRate×帧时长ms/1000，须为 Opus 合法帧长，
     *                      如 48000Hz 下 20ms=960、10ms=480）
     *  @param bitrateBps   目标码率（bit/s，如 24000）；传 <=0 则用 Opus 默认码率
     *  @return 成功返回 true；参数非法或 Opus 创建失败返回 false
     */
    bool Init(int sampleRate, int frameSamples, int bitrateBps);

    //释放编解码器（析构会自动调用；也可手动提前释放）。
    void Close();

    /**
     *  编码一帧 PCM。
     *  @param pcm        指向 numSamples 个 16 位单声道采样的缓冲（非空）
     *  @param numSamples 采样数，须恰等于 Init 时的 frameSamples
     *  @param out        [出参] 编码后的 Opus 包字节（成功时被填充；可能仅 1 字节，DTX 静音帧）
     *  @return 成功返回 true；未初始化 / 帧长不符 / Opus 编码出错返回 false
     */
    bool Encode(const int16_t* pcm, int numSamples, std::vector<unsigned char>& out);

    /**
     *  解码一个 Opus 包为一帧 PCM。
     *  @param data   Opus 包字节起始；传 NULL 且 len==0 表示丢包，Opus 走 PLC 隐藏填一帧
     *  @param len    包字节数
     *  @param outPcm [出参] 解出的 16 位单声道 PCM（长度为本帧实际采样数，正常为 frameSamples）
     *  @return 成功返回 true；未初始化 / Opus 解码出错返回 false
     */
    bool Decode(const unsigned char* data, size_t len, std::vector<int16_t>& outPcm);

    //采样率（Hz）；未初始化时为 0。
    int SampleRate() const
    {
        return m_sampleRate;
    }

    //每帧采样数；未初始化时为 0。
    int FrameSamples() const
    {
        return m_frameSamples;
    }

    //是否已成功初始化（编解码器均就绪）。
    bool IsReady() const
    {
        return m_enc != NULL && m_dec != NULL;
    }

private:
    OpusEncoder* m_enc;           //Opus 编码器（不透明；Init 创建、Close 释放）
    OpusDecoder* m_dec;           //Opus 解码器（不透明；Init 创建、Close 释放）
    int          m_sampleRate;    //采样率（Hz）
    int          m_frameSamples;  //每帧采样数
};

} // namespace call
