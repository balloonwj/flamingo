/**
 *  视频通话操作条的自动隐藏时序（纯逻辑），CallBarAutoHide.h
 *
 *  docs/音视频通话-界面美化方案.md 第 3.4 节（2026-10-06 实现）：视频态的操作条浮在画面底部，鼠标静止 kCallBarIdleMs 后
 *  用 kCallBarFadeOutMs 淡出；鼠标一动就从当前不透明度用 kCallBarFadeInMs 淡入；鼠标悬停在操作条上、或音量浮层开着时
 *  一直显示（「保持」），松开保持后重新起算静止时长。时序思路与开发约定文档「UI 约定」第 1 条的滚动条一致，区别在于
 *  触发源是鼠标移动、静止时长更长（方案第 6 节：淡出延时不宜过短，否则用户找不到按钮）。
 *
 *  本类只按调用方给的时刻算不透明度，不碰窗口与计时器：通话窗在鼠标移动、计时器到点时调用，再按
 *  MsUntilNextChange 安排下一次计时器。单元测试见 Source/Tests/CallBarAutoHideTests.cpp（标签 [callbar]）。
 *
 *  典型用法：
 *      fade.Reset(now);                          //操作条建好时：完全显示，从此刻起算静止
 *      fade.OnActivity(now);                     //鼠标移动
 *      fade.SetHold(hovering || popupOpen, now); //计时器到点时刷新保持状态
 *      bar->SetBarAlpha(fade.Alpha(now));
 *      const int64_t wait = fade.MsUntilNextChange(now);   //0：逐帧刷新；>0：过这么久再看；-1：停掉计时器
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

namespace call {

//鼠标静止多久之后开始淡出（毫秒）
const int64_t kCallBarIdleMs = 3000;
//淡入时长（毫秒），与滚动条的淡入相同
const int64_t kCallBarFadeInMs = 200;
//淡出时长（毫秒），与滚动条的淡出相同
const int64_t kCallBarFadeOutMs = 300;
//完全不透明时的不透明度
const int kCallBarAlphaOpaque = 255;

/**
 *  操作条的自动隐藏时序。时刻一律由调用方传入（毫秒，单调递增，通常取 GetTickCount64）。
 *  不加锁，只在 UI 线程上使用。
 */
class CallBarAutoHide
{
public:
    CallBarAutoHide();

    /**
     *  从头开始：完全显示，从 nowMs 起算静止时长，解除保持。
     *  @param nowMs 当前时刻
     */
    void Reset(int64_t nowMs);

    /**
     *  鼠标有活动（移动）：从当前不透明度开始淡入，并从 nowMs 重新起算静止时长。已完全显示时只重新计时。
     *  @param nowMs 当前时刻
     */
    void OnActivity(int64_t nowMs);

    /**
     *  设置是否保持显示（鼠标悬停在操作条上、或音量浮层开着）。由不保持变为保持时与一次活动相同（正在淡出则淡入回来）；
     *  由保持变为不保持时从 nowMs 起算静止时长。状态不变时什么也不做。
     *  @param hold  是否保持显示
     *  @param nowMs 当前时刻
     */
    void SetHold(bool hold, int64_t nowMs);

    //当前是否处于保持显示状态
    bool IsHeld() const { return m_hold; }

    /**
     *  nowMs 时刻的不透明度。
     *  @return 0（完全隐藏）~ kCallBarAlphaOpaque（完全显示）
     */
    int Alpha(int64_t nowMs) const;

    /**
     *  距离不透明度下一次变化还有多久，供调用方安排计时器。
     *  @return 0：正在淡入或淡出，需要逐帧刷新；大于 0：完全显示、在等静止时长，过这么多毫秒后开始淡出；
     *          -1：不会再变化（已完全隐藏，或保持显示且已完全显示），等下一次活动
     */
    int64_t MsUntilNextChange(int64_t nowMs) const;

private:
    int64_t m_lastActivityMs;   //最近一次活动（或解除保持）的时刻，静止时长从这里起算
    int64_t m_fadeInStartMs;    //最近一次开始淡入的时刻
    int     m_fadeInFromAlpha;  //那次淡入开始时的不透明度
    bool    m_hold;             //是否保持显示
};

} // namespace call
