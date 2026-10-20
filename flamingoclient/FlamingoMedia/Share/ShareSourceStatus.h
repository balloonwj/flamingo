/**
 *  屏幕共享「共享内容状态」的计算（纯逻辑），ShareSourceStatus.h
 *
 *  推流管线要把共享内容的状态（正在共享、已最小化、画面暂停、已关闭，见 Protocol.h 的 screenshare_source_state）告诉观看端。
 *  2026-10-14 之前这个状态由推流循环里的六处分支各自改写，有两处缺陷（docs/远程桌面控制权限与观看工具条方案.md 8.2 末尾）：
 *    - 共享的窗口关闭（「已关闭」）之后重新选择内容，换内容的分支不清掉「已关闭」，而取帧要求状态不是「已关闭」，画面一直停住；
 *    - 暂停与继续直接按「暂停 / 采集挂起」改写状态，把「已关闭」「已最小化」覆盖掉。
 *  现在推流循环只把事件交给本类，本类记下各个条件，由纯函数 ComputeShareSourceState 统一算出状态；
 *  推流循环每轮末尾取一次 State()，与上次发出的不同才发送。
 *
 *  条件的来源与优先级：
 *    - 已关闭：采集器报告共享的窗口已关闭时置位，换了共享内容时清除；优先级最高；
 *    - 画面暂停：用户暂停，或采集丢失后第一次重试失败（采集挂起），二者任一成立；
 *    - 已最小化：采集器正常时观察到的窗口最小化状态，换了共享内容时清除；
 *    - 以上都不成立时为正在共享。
 *  单元测试见 Source/Tests/ShareSourceStatusTests.cpp（SST1、SST2，标签 [sharestate]）。
 *
 *  典型用法（推流管线的工作线程）：
 *      m_status.OnCaptureClosed();                 //采集器报告窗口已关闭
 *      m_status.OnSourceSwitched(capturerOk);      //换了共享内容
 *      ...每轮末尾：
 *      const int state = m_status.State();
 *      if (state != m_sourceState) publishSourceState(state);
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

namespace screenshare {

/**
 *  由各个条件算出共享内容的状态。
 *  @param closed           共享的窗口是否已关闭（且还没有换内容）
 *  @param paused           用户是否暂停了共享
 *  @param captureSuspended 采集是否挂起（丢失后第一次重试失败，还没有恢复）
 *  @param minimized        共享的窗口是否已最小化
 *  @return screenshare_source_state 的取值：已关闭优先；其次暂停或采集挂起为画面暂停；其次最小化为已最小化；否则为正在共享
 */
int ComputeShareSourceState(bool closed, bool paused, bool captureSuspended, bool minimized);

/**
 *  共享内容状态的条件记录。推流循环在各个事件发生时调用对应的方法，需要状态时调用 State()。
 *  只在推流管线的工作线程上使用，不加锁。
 */
class ShareSourceStatus
{
public:
    ShareSourceStatus();

    /**
     *  换了共享内容：清除「已关闭」与「已最小化」（新内容的状态要重新观察）。
     *  @param capturerOk 新内容的采集器是否已建好；建好时同时清除「采集挂起」
     */
    void OnSourceSwitched(bool capturerOk);

    /**
     *  采集丢失后的一次重试报告（ShareCaptureRetry::OnAttempt 的返回值）。
     *  @param report share_capture_retry_report：报告暂停时置位「采集挂起」，报告恢复时清除；其余取值不改变条件
     */
    void OnRetryReport(int report);

    /**
     *  用户暂停或继续了共享。
     *  @param paused true 表示暂停
     */
    void OnPauseChanged(bool paused);

    //采集器报告共享的窗口已关闭
    void OnCaptureClosed();

    /**
     *  采集器正常取帧时观察到的窗口最小化状态。
     *  @param minimized 共享的窗口是否已最小化
     */
    void OnMinimizedObserved(bool minimized);

    //当前应向观看端报告的状态，取值见 screenshare_source_state
    int State() const;

    //共享的窗口是否已关闭（推流循环据此停止取帧）
    bool IsClosed() const;

private:
    bool m_closed;       //共享的窗口已关闭、还没有换内容
    bool m_paused;       //用户暂停了共享
    bool m_suspended;    //采集挂起：丢失后第一次重试失败，还没有恢复
    bool m_minimized;    //共享的窗口已最小化（最近一次观察的结果）
};

} // namespace screenshare
