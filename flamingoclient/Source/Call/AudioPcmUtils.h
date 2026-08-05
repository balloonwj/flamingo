/**
 *  音频 PCM 工具：格式转换 + 线程安全环形缓冲，AudioPcmUtils.h
 *
 *  第二期音频管线的纯逻辑底座，供 WASAPI 采集/播放与 Opus 编解码之间做格式适配与
 *  跨线程缓冲：
 *    - 采样格式转换：32 位浮点 ↔ 16 位整型（Opus 吃 int16，WASAPI 共享模式常给 float）；
 *    - 立体声降单声道（1 对 1 语音只需单声道，采集设备常是双声道）；
 *    - PcmRingBuffer：采集线程与播放线程之间的 int16 采样 FIFO（互斥保护，满则丢、空则少给）。
 *
 *  本模块不碰设备 / 网络 / Opus，纯逻辑，便于单元测试，见 Source/Tests/AudioPcmUtilsTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <mutex>

namespace call {

//==== 采样格式转换（纯函数）====

//32 位浮点 [-1,1] 转 16 位整型：超出 [-1,1] 的样本夹取到 int16 边界，避免溢出回绕。
//@param in/out 各 count 个样本；out 可与场景需要不同缓冲（不支持原地）
void FloatToInt16(const float* in, int16_t* out, size_t count);

//16 位整型转 32 位浮点 [-1,1]（除以 32768）。
void Int16ToFloat(const int16_t* in, float* out, size_t count);

//交错立体声 float 降为单声道 float：每帧取左右声道平均。
//@param interleaved 长度 frames*2（L,R,L,R,...）；@param mono 长度 frames
void DownmixStereoToMonoF32(const float* interleaved, float* mono, size_t frames);

//交错立体声 int16 降为单声道 int16：每帧取左右声道平均（用 int32 求和防溢出）。
void DownmixStereoToMonoI16(const int16_t* interleaved, int16_t* mono, size_t frames);

//单声道 int16 升为交错立体声 int16：左右声道同值复制（播放设备是双声道时用）。
void MonoToStereoI16(const int16_t* mono, int16_t* interleaved, size_t frames);

//==== 跨线程 PCM 环形缓冲 ====

//采集线程写、播放线程读的 int16 采样 FIFO。互斥保护，满则只写下能写的部分、空则只读到能读的部分。
//用于把采集→编解码后的音频排队给播放线程，吸收两侧节奏抖动（PoC 阶段即简单抖动缓冲）。
class PcmRingBuffer
{
public:
    //@param capacitySamples 可容纳的最大 int16 采样数（>0）
    explicit PcmRingBuffer(size_t capacitySamples);

    //写入 count 个采样；缓冲剩余空间不足时只写入能写下的部分。
    //@return 实际写入的采样数（0..count）
    size_t Push(const int16_t* data, size_t count);

    //读出最多 count 个采样到 out；可读不足时只读出可读部分，其余不动。
    //@return 实际读出的采样数（0..count）
    size_t Pop(int16_t* out, size_t count);

    //当前可读采样数。
    size_t Available() const;

    //可容纳的最大采样数。
    size_t Capacity() const;

    //清空（丢弃所有未读采样）。
    void Clear();

private:
    std::vector<int16_t> m_buf;     //底层存储，大小为 capacity+1（留一格区分空/满，但本实现用计数法）
    size_t               m_head;    //读位置（下一个被 Pop 的样本下标）
    size_t               m_tail;    //写位置（下一个被 Push 写入的下标）
    size_t               m_count;   //当前已用采样数
    mutable std::mutex   m_mtx;     //保护 head/tail/count，支持采集与播放线程并发访问
};

} // namespace call
