/**
 *  回声消除的参考信号（2026-10-06，docs/音视频通话方案.md 第 10 节「通话中共享电脑声音」），CallEchoReference.h
 *
 *  回声消除需要知道「扬声器此刻在放什么」，才能从麦克风采到的声音里减掉它。原先参考信号只有本端播放的混音（对方的
 *  说话声等）；共享电脑声音时，其它程序的声音同样从扬声器放出来、会被麦克风采到，对方就会听到两遍（一遍直接发过去的，
 *  一遍经麦克风延迟到达的）。因此共享期间把回环采到的电脑声音也加进参考信号。
 *
 *  两路各用一个 FIFO：渲染线程押入本端播放的混音（PushFar），回环采集线程押入电脑声音（PushSystem），采集线程每处理
 *  一帧取一次（Pop），两路逐采样相加并限幅到 16 位，作为这一帧的参考。不足的部分按静音处理。
 *  电脑声音那一路最多积压 kMaxSystemSamples 个采样：回环与麦克风是两个设备、时钟并不同步，积压超过上限时丢掉最旧的
 *  部分，避免参考信号越来越滞后于麦克风。
 *
 *  线程：PushFar 在渲染线程，PushSystem / ClearSystem 在回环采集线程或 UI 线程，Pop 在采集线程；内部两个 PcmRingBuffer
 *  各自带锁，两个暂存缓冲分属 PushSystem 与 Pop，互不共用。
 *
 *  典型用法（CallAudioEngine 内）：
 *      m_echoRef.PushFar(played, n);      //渲染线程：刚播放的混音
 *      m_echoRef.PushSystem(loopback, n); //回环采集线程：共享中的电脑声音
 *      m_echoRef.Pop(ref, frameSamples);  //采集线程：取一帧参考交给回声消除
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <mutex>
#include <vector>

#include "AudioPcmUtils.h"   //PcmRingBuffer

namespace call {

//回声消除的参考信号：本端播放的混音加上共享中的电脑声音
class CallEchoReference
{
public:
    //电脑声音那一路最多积压的采样数（48 kHz 下 200 毫秒）：超过时丢掉最旧的部分
    static const size_t kMaxSystemSamples = 9600;

    /**
     *  @param farCapacity 播放混音那一路 FIFO 的容量（采样数，> 0）；写满之后新写入的部分被丢弃，与原先的 m_farRef 相同
     */
    explicit CallEchoReference(size_t farCapacity);

    /**
     *  押入一段刚播放的混音（渲染线程）。
     *  @param pcm     48 kHz 单声道采样，只在本次调用期间读取；为 NULL 时忽略
     *  @param samples 采样数
     */
    void PushFar(const int16_t* pcm, size_t samples);

    /**
     *  押入一段回环采到的电脑声音（回环采集线程）。积压超过 kMaxSystemSamples 时先丢掉最旧的部分。
     *  @param pcm     48 kHz 单声道采样，只在本次调用期间读取；为 NULL 时忽略
     *  @param samples 采样数
     */
    void PushSystem(const int16_t* pcm, size_t samples);

    /**
     *  取一帧参考（采集线程）：两路各取 samples 个采样逐个相加并限幅；哪一路不够，不够的部分按 0 计。
     *  @param out     [出参] 至少 samples 个采样
     *  @param samples 采样数
     */
    void Pop(int16_t* out, size_t samples);

    //丢弃电脑声音那一路的全部积压（停止共享电脑声音时调用）
    void ClearSystem();

    //两路都清空（音频引擎启动、停止或开关采集时调用）
    void Clear();

    //电脑声音那一路当前积压的采样数（测试与日志用）
    size_t SystemAvailable() const;

private:
    PcmRingBuffer        m_far;           //本端播放的混音（渲染线程写、采集线程读）
    PcmRingBuffer        m_system;        //回环采到的电脑声音（回环采集线程写、采集线程读），最多 kMaxSystemSamples
    std::vector<int16_t> m_popScratch;    //Pop 取电脑声音用的暂存（只在采集线程使用）
    std::vector<int16_t> m_dropScratch;   //PushSystem 丢弃最旧部分用的暂存（只在押入电脑声音的线程使用）
    std::mutex           m_systemPushMtx; //串行化 PushSystem 与 ClearSystem：两者可能来自回环采集线程与 UI 线程
};

} // namespace call
