/**
 *  ChatSmoothScroll.cpp
 *
 *  消息列表平滑滚动的纯逻辑实现，见 ChatSmoothScroll.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （Flamingo.vcxproj 里对本文件把预编译头设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "ChatSmoothScroll.h"

#include <windows.h>   // WHEEL_DELTA
#include <math.h>      // floor
#include "../../../third-party/balloonui/balloonui/DuiAnimation.h"   // DuiEase::EaseOutCubic（缓出曲线）

namespace balloonchatwnd {

namespace {

// 把浮点位置四舍五入成像素。位置恒为非负，用 floor(x + 0.5) 即可。
int RoundPos(double v)
{
    return (int)floor(v + 0.5);
}

// 把 v 夹紧到 [lo, hi]。
double ClampPos(double v, double lo, double hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

} // anonymous namespace

ChatSmoothScroll::ChatSmoothScroll()
    : m_active(false)
    , m_from(0.0)
    , m_target(0.0)
    , m_startMs(0.0)
{
}

double ChatSmoothScroll::WheelDeltaToPixels(int zDelta, int stepPx)
{
    return (double)zDelta * (double)stepPx / (double)WHEEL_DELTA;
}

bool ChatSmoothScroll::OnWheel(int currentPos, int maxScroll, int zDelta, int stepPx, double nowMs)
{
    const double maxPos = (maxScroll > 0) ? (double)maxScroll : 0.0;
    //动画进行中时在目标上累加：连续快速滚动时每一格都算数
    const double base = m_active ? m_target : (double)currentPos;
    //向上滚（delta 为正）是往内容顶部看，滚动位置变小，故取减号
    const double target = ClampPos(base - WheelDeltaToPixels(zDelta, stepPx), 0.0, maxPos);

    //目标与当前位置一致、也没有进行中的动画：已经在边界上，本次滚动没有效果
    if (!m_active && RoundPos(target) == currentPos)
    {
        m_target = target;
        return false;
    }

    //从调用方当前的实际位置出发重新计时。动画进行中时当前位置就是上一帧画出来的位置，
    //从这里出发画面是连续的，不会跳回上一段的起点。
    m_from    = (double)currentPos;
    m_target  = target;
    m_startMs = nowMs;
    m_active  = true;
    return true;
}

int ChatSmoothScroll::Advance(double nowMs)
{
    if (!m_active)
    {
        return RoundPos(m_target);
    }
    double t = (nowMs - m_startMs) / (double)kChatSmoothScrollDurationMs;
    if (t >= 1.0)
    {
        //时长到了：精确落在目标上并结束，避免浮点误差留下差一两个像素的尾巴
        m_active = false;
        return RoundPos(m_target);
    }
    if (t < 0.0)
    {
        t = 0.0;
    }
    const double eased = balloonwjui::DuiEase::EaseOutCubic(t);
    return RoundPos(m_from + (m_target - m_from) * eased);
}

bool ChatSmoothScroll::IsActive() const
{
    return m_active;
}

double ChatSmoothScroll::Target() const
{
    return m_target;
}

void ChatSmoothScroll::Cancel()
{
    m_active = false;
}

void ChatSmoothScroll::Shift(int delta)
{
    m_from   += (double)delta;
    m_target += (double)delta;
}

} // namespace balloonchatwnd
