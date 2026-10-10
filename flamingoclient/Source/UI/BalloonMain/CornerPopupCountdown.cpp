/**
 *  右下角弹窗的倒计时，说明见 CornerPopupCountdown.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj、FlamingoUnit.vcxproj 与
 *  FlamingoMedia.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "CornerPopupCountdown.h"

#include <stdio.h>   // swprintf_s
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

//一秒的毫秒数、一分钟的秒数
const int64_t kMsPerSecond = 1000;
const int kSecondsPerMinute = 60;
//格式化剩余时间的缓冲区长度（宽字符个数），足够放下「9999:59」
const size_t kTimeTextLen = 32;
//标题与倒计时之间的分隔
const wchar_t* const kSeparator = L" · ";

} // namespace

int CornerCountdownSeconds(int64_t remainingMs)
{
    if (remainingMs <= 0)
        return 0;
    return static_cast<int>((remainingMs + kMsPerSecond - 1) / kMsPerSecond);
}

std::wstring MakeCornerCountdownTitle(const std::wstring& title, int64_t remainingMs, int style)
{
    const bool ring = (style == corner_countdown_ring);
    const int seconds = CornerCountdownSeconds(remainingMs);
    if (seconds <= 0)
        return title + kSeparator + (ring ? TR(L"即将自动结束") : TR(L"即将关闭"));
    //不足一分钟写「N 秒」，一分钟及以上写「M:SS 」（与「秒」之后的写法一致，数字与「后」之间空一格）
    wchar_t timeText[kTimeTextLen] = { 0 };
    if (seconds < kSecondsPerMinute)
        swprintf_s(timeText, TR(L"%d 秒"), seconds);
    else
        swprintf_s(timeText, L"%d:%02d ", seconds / kSecondsPerMinute, seconds % kSecondsPerMinute);
    return title + kSeparator + timeText + (ring ? TR(L"后无人接听自动结束") : TR(L"后关闭"));
}

CornerCountdown::CornerCountdown()
    : m_deadlineMs(0)
    , m_pausedRemainMs(0)
    , m_running(false)
    , m_paused(false)
{
}

void CornerCountdown::Start(int64_t totalMs, int64_t nowMs)
{
    m_deadlineMs = nowMs + totalMs;
    m_pausedRemainMs = 0;
    m_running = true;
    m_paused = false;
}

void CornerCountdown::Pause(int64_t nowMs)
{
    if (!m_running || m_paused)
        return;
    m_pausedRemainMs = RemainingMs(nowMs);
    m_paused = true;
}

void CornerCountdown::Stop()
{
    m_running = false;
    m_paused = false;
}

bool CornerCountdown::IsRunning() const
{
    return m_running;
}

bool CornerCountdown::IsPaused() const
{
    return m_paused;
}

int64_t CornerCountdown::RemainingMs(int64_t nowMs) const
{
    if (!m_running)
        return 0;
    if (m_paused)
        return m_pausedRemainMs;
    return m_deadlineMs > nowMs ? m_deadlineMs - nowMs : 0;
}

bool CornerCountdown::Expired(int64_t nowMs) const
{
    return m_running && !m_paused && nowMs >= m_deadlineMs;
}

} // namespace balloonmain
