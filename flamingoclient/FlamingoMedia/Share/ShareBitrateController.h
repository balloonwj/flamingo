/**
 *  屏幕共享画面的码率自适应（docs/屏幕共享与录制方案.md 第 9 期 9b），ShareBitrateController.h
 *
 *  发起人按两种拥塞信号调整编码码率：
 *    - 发送阻塞：每帧交给中转连接时 send 阻塞的时长。套接字发送缓冲区满了 send 才会阻塞，说明上行带宽跟不上；
 *    - 关键帧请求：服务端给慢观看者丢帧后会请求关键帧（第 3 期 3b），新成员连上中转时也会请求一次，
 *      所以只有一个评估周期里请求次数较多时才算拥塞。
 *  每 2 秒评估一次：拥塞时目标码率降到当前的 70%（不低于下限）；连续 3 个周期都不拥塞后，之后每个不拥塞的周期
 *  加最高码率的 10%，直到回到最高码率。本类只做计算，不碰编码器与网络，所有方法须在同一线程（推流线程）调用。
 *
 *  典型用法（推流线程）：
 *      ShareBitrateController ctl(kShareBitrateBps, kShareMinBitrateBps);
 *      ...每编一帧之前：
 *      int bps = 0;
 *      if (ctl.Evaluate(nowMs, bps)) encoder.SetBitrate(bps);
 *      ...每帧发完：
 *      ctl.OnFrameSent(sendBlockedMs);
 *      ...收到关键帧请求（经原子计数转到推流线程）：
 *      ctl.OnKeyFrameRequests(count);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>

namespace screenshare {

//屏幕共享画面码率的下限（bit/s）：再低画面已经糊得看不清文字，宁可丢帧
const int kShareMinBitrateBps = 300000;

//码率自适应的评估周期（毫秒）
const int64_t kShareBitrateWindowMs = 2000;
//回执延迟的拥塞门限（毫秒）：一个周期里最大的往返加排队延迟超过它算拥塞（远程桌面用，2026-10-15，据推测，待实测再调）
const int64_t kShareFeedbackCongestedDelayMs = 1000;
//回执中断的门限（毫秒）：启用回执后，距最近一次回执超过它算拥塞
const int64_t kShareFeedbackTimeoutMs = 3000;

//屏幕共享的码率自适应计算
class ShareBitrateController
{
public:
    /**
     *  @param maxBps 最高码率（bit/s），也是初始码率；须大于 0
     *  @param minBps 最低码率（bit/s）；大于 maxBps 时按 maxBps 处理
     */
    ShareBitrateController(int maxBps, int minBps);

    /**
     *  记录一帧发送时 send 阻塞的时长。
     *  @param blockedMs 本帧交给中转连接所用的时间（毫秒）；负数按 0 处理
     */
    void OnFrameSent(int64_t blockedMs);

    /**
     *  记录收到的关键帧请求。
     *  @param count 自上次调用以来收到的请求次数；不大于 0 时忽略
     */
    void OnKeyFrameRequests(int count);

    /**
     *  评估是否需要调整码率；距上次评估不足一个周期时什么都不做。第一次调用只记下周期起点。
     *  @param nowMs  当前时刻（单调时钟，毫秒）
     *  @param outBps [出参] 需要调整时为新的目标码率；不需要时不修改
     *  @return 目标码率变了返回 true（调用方据此设置编码器）
     */
    bool Evaluate(int64_t nowMs, int& outBps);

    //当前的目标码率（bit/s）
    int CurrentBps() const;

    /**
     *  换码率范围（源分辨率变了、编码档位随之变化时调用，docs/远程桌面H264与共享画质提升方案.md 4.2）：
     *  上限、下限与当前码率一起换，连续不拥塞的计数清零，当前评估周期不重置。
     *  @param maxBps     新的最高码率（bit/s），须大于 0
     *  @param minBps     新的最低码率（bit/s）；大于 maxBps 时按 maxBps
     *  @param currentBps 换范围后的当前码率（bit/s，通常由 ScaleBitrateToRange 换算）；夹在新范围之内
     */
    void SetRange(int maxBps, int minBps, int currentBps);

    //当前的最高、最低码率（bit/s）
    int MaxBps() const;
    int MinBps() const;

    /**
     *  记录一次回执延迟（远程桌面用，docs/远程桌面H264与共享画质提升方案.md 4.5）：一对一中转对控制端下行没有积压保护，
     *  send 阻塞只反映被控端的上行，另靠控制端回送的发送时刻算出往返加排队的延迟。第一次调用即启用这一信号：
     *  一个周期里最大延迟超过 kShareFeedbackCongestedDelayMs，或距最近一次回执超过 kShareFeedbackTimeoutMs，都算拥塞。
     *  屏幕共享不调用它，行为与原来相同。
     *  @param delayMs 往返加排队的延迟（毫秒）；负数按 0
     *  @param nowMs   收到回执的时刻（单调时钟，毫秒）
     */
    void OnFeedbackDelay(int64_t delayMs, int64_t nowMs);

private:
    int     m_maxBps;          //最高码率（bit/s）
    int     m_minBps;          //最低码率（bit/s）
    int     m_currentBps;      //当前目标码率（bit/s）
    bool    m_started;         //是否已记下第一个周期的起点
    int64_t m_windowStartMs;   //当前评估周期的起点（毫秒）
    int64_t m_blockedMs;       //当前周期内 send 阻塞的总时长（毫秒）
    int     m_keyRequests;     //当前周期内收到的关键帧请求次数
    int     m_calmWindows;     //连续不拥塞的周期数
    bool    m_feedbackOn;      //是否启用了回执延迟信号（第一次 OnFeedbackDelay 时启用）
    int64_t m_maxFeedbackMs;   //当前周期内最大的回执延迟（毫秒）
    int64_t m_lastFeedbackAt;  //最近一次收到回执的时刻（毫秒）
};

} // namespace screenshare
