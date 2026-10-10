/**
 *  锁屏设置窗口「预览」的状态与判定（纯逻辑，可单测）：预览播哪一种动画、「轮流播放」何时换下一种、
 *  哪些输入结束预览。
 *
 *  预览只在设置窗口所在的那块显示器上全屏播放所选动画，不装钩子、不防休眠、不要求密码；单击、按任意键
 *  （含 Esc）结束。为避免打开预览的那一下点击（例如双击「预览」按钮的第二下）立刻把预览关掉，第一帧画出来后的
 *  kPreviewInputGraceMs 毫秒内不理会单击与按键；窗口失去激活、显示器变化、系统注销则任何时候都结束预览。
 *
 *  典型用法（锁屏线程上的预览会话）：
 *      CLockPreviewState st;
 *      LockAnimation a = st.Start(choice, random, now);      // 播放 a
 *      画完第一帧：st.RestartClock(now);
 *      每帧：if (st.Tick(now)) 换成 st.Current();
 *      收到输入：if (st.OnInput(kPreviewInputKey, now)) 下一帧结束预览;
 *
 *  方案见 docs/锁屏方案.md 第 11 节。
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>

#include "LockAnimationChoice.h"

namespace flamingolock {

const uint32_t kPreviewRotateIntervalMs = 5000;  // 预览「轮流播放」时每种动画播多久（毫秒）；真正锁屏时是 kRotateIntervalMs
const uint32_t kPreviewInputGraceMs = 400;       // 预览开始后多久之内不理会单击与按键（毫秒），约为系统双击间隔的上限

/**
 *  预览窗口收到的输入种类。
 */
enum LockPreviewInput
{
    kPreviewInputMouseMove = 0,     // 鼠标移动：不结束预览（粒子时钟会被推开，正好演示）
    kPreviewInputMouseButton,       // 鼠标左、右、中键按下：结束预览（开始后的保护时间内除外）
    kPreviewInputKey,               // 按键按下（WM_KEYDOWN，含 Esc）：结束预览（保护时间内除外）
    kPreviewInputSysKey,            // 系统键按下（WM_SYSKEYDOWN，如 Alt、F10、Alt+F4）：同普通按键
    kPreviewInputChar,              // 字符输入（WM_CHAR）：不单独处理，对应的按键按下已经结束预览
    kPreviewInputDeactivate,        // 预览窗口失去激活（切到了别的窗口）：立即结束
    kPreviewInputDisplayChange,     // 显示器布局或分辨率变化：立即结束（预览窗口的尺寸已不对）
    kPreviewInputEndSession,        // 系统正在注销或关机：立即结束
};

/**
 *  一次预览的状态。非线程安全，只在锁屏线程上使用；时刻一律由调用方传入（单调毫秒）。
 */
class CLockPreviewState
{
public:
    CLockPreviewState();

    /**
     *  开始一次预览，确定先播哪一种动画。
     *  @param choice      设置窗口下拉框当前的选择
     *  @param randomValue 随机数（任意 32 位值）：「随机」方式下取模决定播哪一种
     *  @param nowMs       当前时刻（单调毫秒）
     *  @return 先播的动画：固定方式为所选的那种；「随机」任取一种；「轮流播放」从八荣八耻开始
     */
    LockAnimation Start(const LockAnimationChoice& choice, uint32_t randomValue, uint64_t nowMs);

    /**
     *  预览的第一帧画出来之后调用：输入保护时间与「轮流播放」的计时改从这一刻算起。
     *  建窗、场景初始化与第一帧可能要几百毫秒（调试版更久），从 Start 算起的话，保护时间可能在用户看到画面之前就已过完。
     *  已在结束中时什么也不做。
     *  @param nowMs 当前时刻（单调毫秒）
     */
    void RestartClock(uint64_t nowMs);

    /**
     *  推进时间。「轮流播放」方式下每满 kPreviewRotateIntervalMs 毫秒换下一种动画（循环）。
     *  @param nowMs 当前时刻（单调毫秒）
     *  @return 本次需要换动画时返回 true（新动画见 Current）；其余情况（含已在结束中）返回 false
     */
    bool Tick(uint64_t nowMs);

    /**
     *  收到一次输入，判断是否结束预览。结束只记下标志，由调用方在下一帧开始时真正关闭窗口
     *  （本函数在预览窗口自己的消息处理里被调用，那里不能销毁窗口）。
     *  @param input 输入种类
     *  @param nowMs 当前时刻（单调毫秒）
     *  @return 这次输入使预览进入结束状态时返回 true；不结束或早已在结束中返回 false
     */
    bool OnInput(LockPreviewInput input, uint64_t nowMs);

    /**
     *  当前播放的动画。
     */
    LockAnimation Current() const;

    /**
     *  是否已经决定结束（等调用方关闭窗口）。
     */
    bool IsEnding() const;

    /**
     *  当前动画已经播放的毫秒数（「轮流播放」换片时重新计时）。
     *  @param nowMs 当前时刻（单调毫秒）
     */
    uint64_t CurrentElapsedMs(uint64_t nowMs) const;

private:
    LockAnimationChoice m_choice;       // 本次预览的选择
    LockAnimation       m_current;      // 当前播放的动画
    uint64_t            m_startMs;      // 预览开始的时刻（单调毫秒），用于输入保护时间
    uint64_t            m_animStartMs;  // 当前动画开始播放的时刻（「轮流播放」换片用）
    bool                m_ending;       // 是否已经决定结束
};

} // namespace flamingolock
