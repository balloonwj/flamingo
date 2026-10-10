/**
 *  语音识别前的声音处理（docs/语音字幕方案.md 5.1、5.2），CaptionAudio.h
 *
 *  - CaptionResampler：48 kHz 单声道 16 位采样转成识别模型要的 16 kHz 浮点采样（先低通滤波再三取一），
 *    跨多次调用保持滤波器状态，连续推入的声音转换结果与一次性转换相同；
 *  - CaptionTimeMap：记下「识别流里第几个 16 kHz 采样」对应「录像（或本机时钟）里的哪个毫秒」。
 *    录制时声音按录制时间轴连续推入；共享时麦克风关掉一阵再打开，时间戳会跳变，而识别流里的采样是连续的，
 *    识别结果的词片时刻要靠它换算回真实时刻。
 *
 *  纯逻辑，不依赖识别引擎，可单测（FlamingoCaption/Tests/CaptionEngineTests.cpp）。
 *
 *  典型用法：
 *      std::vector<float> out;
 *      resampler.Process(pcm48k, n, out);
 *      timeMap.Append(fedSamples, ptsMs, out.size());
 *      int64_t ms = timeMap.ToPts(sampleIndex);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <vector>

namespace caption {

//识别模型的采样率（Hz）
const int kModelSampleRate = 16000;
//推入声音的采样率（Hz）
const int kInputSampleRate = 48000;
//48 kHz 到 16 kHz 的抽取倍数
const int kDecimation = kInputSampleRate / kModelSampleRate;

/**
 *  48 kHz → 16 kHz 重采样器（低通 FIR，截止约 7 kHz，汉明窗）。
 */
class CaptionResampler
{
public:
    CaptionResampler();

    /**
     *  转换一段采样，结果追加到 out 末尾（取值范围 -1～1）。
     *  @param in      48 kHz 单声道 16 位采样
     *  @param samples 采样数
     *  @param out     [出参] 追加 16 kHz 浮点采样
     */
    void Process(const int16_t* in, size_t samples, std::vector<float>& out);

    //清空滤波器状态（开始一段新的、与之前不连续的声音时调用）
    void Reset();

private:
    std::vector<float> m_taps;   //FIR 系数，和为 1
    std::vector<float> m_buf;    //尚未用完的输入：前面是滤波器的历史采样
    size_t             m_pos;    //下一个输出对应的输入下标（m_buf 内）
};

/**
 *  识别流的采样序号到真实时刻（毫秒）的换算。按推入顺序登记每一段声音；首尾相接的段合并成一段。
 */
class CaptionTimeMap
{
public:
    CaptionTimeMap();

    /**
     *  登记一段声音。
     *  @param firstSample 这段声音第一个采样在识别流里的序号（16 kHz）
     *  @param ptsMs       这段声音第一个采样的真实时刻（毫秒）
     *  @param samples     这段声音的采样数（16 kHz）
     */
    void Append(int64_t firstSample, int64_t ptsMs, size_t samples);

    /**
     *  识别流里第 sample 个采样的真实时刻（毫秒）。落在登记过的某段里时按该段换算；
     *  在第一段之前时按第一段往前推算；在最后一段之后时按最后一段往后推算；没有任何登记时返回 0。
     */
    int64_t ToPts(int64_t sample) const;

    //忘掉在 sample 之前已经结束的段（只保留最近用得到的段，避免越积越多）
    void DropBefore(int64_t sample);

    //清空
    void Clear();

    //当前保存的段数（测试用）
    size_t SegmentCount() const;

private:
    //一段连续的声音
    struct Segment
    {
        int64_t m_firstSample;   //第一个采样的序号
        int64_t m_ptsMs;         //第一个采样的真实时刻（毫秒）
        int64_t m_samples;       //采样数
    };

    std::deque<Segment> m_segments;   //按序号排列的各段
};

} // namespace caption
