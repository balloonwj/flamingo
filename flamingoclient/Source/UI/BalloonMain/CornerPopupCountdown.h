/**
 *  右下角弹窗的倒计时，CornerPopupCountdown.h
 *
 *  屏幕共享第 15 期（15g）：右下角会自动关闭的弹窗（屏幕共享提示 60 秒、录制完成提示 2 分钟、来电 45 秒、
 *  特别关注上下线提示 6 秒）在标题后显示倒计时，如「屏幕共享 · 58 秒后关闭」「屏幕录制 · 1:58 后关闭」
 *  「语音通话 · 42 秒后无人接听自动结束」，倒计时到了弹窗消失。四个窗口分属 exe 与 FlamingoMedia.dll，
 *  标题的写法与倒计时的计算只放一份在这里，两边共用（DLL 直接编译本文件）。
 *
 *  拆成两部分：
 *    · CornerCountdownSeconds / MakeCornerCountdownTitle —— 纯函数，只做格式化；
 *    · CornerCountdown —— 记下截止时刻与暂停状态，时刻一律由调用方传入（GetTickCount64），便于测试。
 *  窗口每秒用一个定时器取 RemainingMs 刷新标题，Expired 为真时关闭。
 *
 *  典型用法：
 *      m_countdown.Start(kAutoCloseMs, ::GetTickCount64());
 *      //每秒：
 *      SetTitle(balloonmain::MakeCornerCountdownTitle(L"屏幕共享", m_countdown.RemainingMs(now),
 *                                                     balloonmain::corner_countdown_close).c_str());
 *      if (m_countdown.Expired(now)) ...关闭...
 *
 *  本文件只依赖标准库，不依赖任何控件库。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

namespace balloonmain {

//倒计时文字的写法
enum CornerCountdownStyle
{
    corner_countdown_close = 0,   //「N 秒后关闭」：屏幕共享提示、录制完成提示、特别关注上下线提示
    corner_countdown_ring  = 1,   //「N 秒后无人接听自动结束」：来电（响铃中的通话窗）
};

//窗口刷新倒计时标题的间隔（毫秒）：每秒一次
const unsigned int kCornerCountdownTickMs = 1000;

/**
 *  剩余的整秒数：向上取整（还剩 0.2 秒时仍算 1 秒，免得标题提前显示 0），不足 0 时为 0。
 *  @param remainingMs 剩余毫秒数
 *  @return 剩余秒数，>= 0
 */
int CornerCountdownSeconds(int64_t remainingMs);

/**
 *  带倒计时的标题：「标题 · 58 秒后关闭」；剩余 60 秒及以上时写成「标题 · 1:58 后关闭」；来电写
 *  「标题 · 42 秒后无人接听自动结束」。剩余时间不足 1 秒（含已到时）时，关闭类写「标题 · 即将关闭」、来电写
 *  「标题 · 即将自动结束」。
 *  @param title       弹窗原来的标题，如「屏幕共享」
 *  @param remainingMs 剩余毫秒数
 *  @param style       写法，取值见 enum CornerCountdownStyle
 *  @return 完整的标题文字
 */
std::wstring MakeCornerCountdownTitle(const std::wstring& title, int64_t remainingMs, int style);

//一个弹窗的倒计时：截止时刻、是否暂停（特别关注提示在鼠标停留时暂停）
class CornerCountdown
{
public:
    CornerCountdown();

    /**
     *  从头开始倒计时（已在倒计时或已暂停时同样从头开始）。
     *  @param totalMs 总时长（毫秒），须 > 0
     *  @param nowMs   当前时刻（毫秒，GetTickCount64）
     */
    void Start(int64_t totalMs, int64_t nowMs);

    /**
     *  暂停：剩余时间冻结在当前值，到继续或重新开始为止都不会到时。未在倒计时时什么也不做。
     *  @param nowMs 当前时刻（毫秒）
     */
    void Pause(int64_t nowMs);

    //停止：之后 RemainingMs 为 0、Expired 为 false，直到再次 Start
    void Stop();

    //是否已开始且未停止（含暂停中）
    bool IsRunning() const;

    //是否暂停中
    bool IsPaused() const;

    /**
     *  剩余毫秒数：暂停中为冻结的值；未开始或已停止为 0；已过截止时刻为 0。
     *  @param nowMs 当前时刻（毫秒）
     */
    int64_t RemainingMs(int64_t nowMs) const;

    /**
     *  是否已到时：在倒计时、没有暂停、且已过截止时刻。
     *  @param nowMs 当前时刻（毫秒）
     */
    bool Expired(int64_t nowMs) const;

private:
    int64_t m_deadlineMs;       //截止时刻（毫秒，GetTickCount64 的时间轴）；暂停中不使用
    int64_t m_pausedRemainMs;   //暂停时冻结的剩余毫秒数；不在暂停中时不使用
    bool    m_running;          //是否已开始且未停止
    bool    m_paused;           //是否暂停中
};

} // namespace balloonmain
