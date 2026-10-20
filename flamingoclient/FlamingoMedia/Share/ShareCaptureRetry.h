/**
 *  屏幕共享采集丢失后的重试策略（纯逻辑），ShareCaptureRetry.h
 *
 *  锁屏、分辨率变化、显卡驱动重置时采集器会报告丢失。2026-10-12 之前推流管线停下采集器、等 500 毫秒重建一次，失败就再也不试，
 *  观看端一直停在最后一帧（docs/远程桌面控制权限与观看工具条方案.md 第 3.2 节）。现在持续重试，直到成功或停止推流：
 *    - 丢失后等起始间隔（500 毫秒）再试；每次失败后间隔翻倍，最长 2 秒（2026-10-14 起，原为固定 500 毫秒：
 *      锁屏时间长时不必每秒重建两次，同一文档 8.2 第 4 条）；重建成功、Reset 之后再次丢失时从起始间隔重新开始；
 *    - 第一次重试失败时向观看端报告「画面暂停」（短暂的丢失在第一次重试就恢复，观看端察觉不到）；
 *    - 报告过暂停之后重试成功，向观看端报告恢复。
 *  单元测试见 Source/Tests/ShareCaptureRetryTests.cpp（RES6、RES7）。
 *
 *  典型用法（推流管线的工作线程）：
 *      retry.OnLost(now);
 *      ...每一轮：if (retry.Due(now)) { ok = capturer->Start(); report = retry.OnAttempt(ok, now); }
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <stdint.h>

namespace screenshare {

//采集重试的起始间隔（毫秒）：丢失后第一次重试前等这么久
const int64_t kShareCaptureRetryIntervalMs = 500;

//采集重试的最长间隔（毫秒）：每次失败后间隔翻倍，到这个值为止
const int64_t kShareCaptureRetryMaxIntervalMs = 2000;

//一次重试之后要不要改变向观看端报告的状态（ShareCaptureRetry::OnAttempt 的返回值）
enum share_capture_retry_report
{
    share_retry_report_none    = 0,   //不用改
    share_retry_report_paused  = 1,   //第一次重试失败：报告「画面暂停」
    share_retry_report_resumed = 2,   //报告过暂停之后重试成功：报告恢复共享
};

//采集丢失后的重试状态
class ShareCaptureRetry
{
public:
    /**
     *  @param intervalMs    起始间隔（毫秒），不大于 0 时按 kShareCaptureRetryIntervalMs
     *  @param maxIntervalMs 最长间隔（毫秒），小于起始间隔时按起始间隔（即间隔不再增长）
     */
    explicit ShareCaptureRetry(int64_t intervalMs = kShareCaptureRetryIntervalMs,
                               int64_t maxIntervalMs = kShareCaptureRetryMaxIntervalMs);

    /**
     *  采集器报告丢失：开始重试，起始间隔之后第一次重试。已在重试中时不重新计时。
     *  @param nowMs 当前时刻（毫秒，单调时钟）
     */
    void OnLost(int64_t nowMs);

    //是否在重试中（丢失之后、重试成功之前）
    bool Active() const;

    //该重试了：在重试中且到了下一次重试的时刻
    bool Due(int64_t nowMs) const;

    /**
     *  一次重试的结果。失败时下一次的间隔翻倍（不超过最长间隔）；成功时结束这一轮重试。
     *  @param ok    重建采集器是否成功
     *  @param nowMs 当前时刻（毫秒）
     *  @return 要不要改变向观看端报告的状态，取值见 share_capture_retry_report
     */
    int OnAttempt(bool ok, int64_t nowMs);

    //这一轮丢失以来已经重试的次数
    int Attempts() const;

    //停止推流或换了共享内容：不再重试，下一次丢失时从起始间隔开始
    void Reset();

private:
    int64_t m_firstIntervalMs;   //起始间隔（毫秒）
    int64_t m_maxIntervalMs;     //最长间隔（毫秒），不小于起始间隔
    int64_t m_intervalMs;        //当前这一次重试前要等的间隔（毫秒）；丢失时置为起始间隔，每次失败后翻倍
    bool    m_active;            //是否在重试中
    bool    m_reported;          //是否已向观看端报告过「画面暂停」
    int64_t m_nextMs;            //下一次重试的时刻（毫秒）
    int     m_attempts;          //这一轮丢失以来已经重试的次数
};

} // namespace screenshare
