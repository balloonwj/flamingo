/**
 *  LockSuppressPolicyTests.cpp
 *
 *  锁屏期间主程序各类「自己冒出来」的东西怎么处理（标签 [lock]），覆盖 docs/锁屏方案.md 第 7 节：
 *    - LS1 未锁屏时一律照常
 *    - LS2 提醒类与必须完成的动作延后（公告、群公告、备忘、升级提示、强制升级退出、被挤下线、重连后显示主面板、
 *          文件共享通知（2026-10-07 起））
 *    - LS3 即时性的弹窗、抢前台、字幕、提示音、气泡与快捷键丢弃
 *    - LS4 不认识的来源在锁屏时保守丢弃
 *    - LS5 新消息到达判定 DecideArrival 在锁屏时：不弹窗、不闪任务栏、不响，照常更新会话行、未读数与托盘闪烁，
 *          「正在看」按没在看处理
 *    - LS6 全局快捷键的恢复：解锁时已登录才重新注册
 *  纯函数，框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Feature/Lock/LockSuppressPolicy.h"
#include "../UI/BalloonMain/NotifyModePolicy.h"
#include "../net/Protocol.h"   // notify_mode_*

using namespace balloonlock;

namespace {

//延后的来源
const LockEventKind kDeferred[] =
{
    kLockEvtAnnouncement,
    kLockEvtGroupAnnouncement,
    kLockEvtMemoReminder,
    kLockEvtUpdatePrompt,
    kLockEvtForcedUpdateExit,
    kLockEvtKickout,
    kLockEvtRaisePanel,
    kLockEvtFileShareNotice,
};

//丢弃的来源
const LockEventKind kDropped[] =
{
    kLockEvtMessagePopup,
    kLockEvtChatWindowRaise,
    kLockEvtSubtitle,
    kLockEvtStarredPresence,
    kLockEvtDockSlideOut,
    kLockEvtTrayBalloon,
    kLockEvtNotifySound,
    kLockEvtGlobalHotkey,
};

const size_t kDeferredCount = sizeof(kDeferred) / sizeof(kDeferred[0]);
const size_t kDroppedCount = sizeof(kDropped) / sizeof(kDropped[0]);

//按提醒模式、单聊群聊、窗口开没开、在不在看拼一个到达输入
balloonmain::ArrivalInput Arrival(int mode, bool group, bool windowOpen, bool watching, bool shake, bool locked)
{
    balloonmain::ArrivalInput in;
    in.m_mode = mode;
    in.m_isGroup = group;
    in.m_atMe = false;
    in.m_countable = true;
    in.m_windowOpen = windowOpen;
    in.m_watching = watching;
    in.m_isShake = shake;
    in.m_screenLocked = locked;
    return in;
}

} // anonymous namespace

TEST_CASE("LS1 未锁屏时每一类来源都照常处理", "[lock]")
{
    for (int k = 0; k < (int)kLockEvtCount; ++k)
    {
        INFO("kind = " << k);
        CHECK(DecideDuringLock((LockEventKind)k, false) == kLockActNormal);
    }
}

TEST_CASE("LS2 锁屏时提醒类与必须完成的动作延后到解锁", "[lock]")
{
    for (size_t i = 0; i < kDeferredCount; ++i)
    {
        INFO("kind = " << (int)kDeferred[i]);
        CHECK(DecideDuringLock(kDeferred[i], true) == kLockActDefer);
    }
}

TEST_CASE("LS3 锁屏时即时性的弹窗、抢前台、字幕、提示音、气泡与快捷键丢弃", "[lock]")
{
    for (size_t i = 0; i < kDroppedCount; ++i)
    {
        INFO("kind = " << (int)kDropped[i]);
        CHECK(DecideDuringLock(kDropped[i], true) == kLockActDrop);
    }
    //两张表合起来正好覆盖全部来源，没有漏掉的
    CHECK(kDeferredCount + kDroppedCount == (size_t)kLockEvtCount);
}

TEST_CASE("LS4 不认识的来源：锁屏时丢弃，未锁屏时照常", "[lock]")
{
    CHECK(DecideDuringLock(kLockEvtCount, true) == kLockActDrop);
    CHECK(DecideDuringLock((LockEventKind)999, true) == kLockActDrop);
    CHECK(DecideDuringLock((LockEventKind)999, false) == kLockActNormal);
}

TEST_CASE("LS5 锁屏时新消息不弹窗、不闪任务栏、不响，会话行、未读数与托盘闪烁照常", "[lock]")
{
    const int modes[5] = { notify_mode_allow, notify_mode_silent, notify_mode_msgbox, notify_mode_block,
                           notify_mode_starred };
    for (int m = 0; m < 5; ++m)
    {
        for (int combo = 0; combo < 8; ++combo)
        {
            const bool group = (combo & 1) != 0;
            const bool windowOpen = (combo & 2) != 0;
            const bool shake = !group && (combo & 4) != 0;
            INFO("mode = " << modes[m] << ", group = " << group << ", windowOpen = " << windowOpen
                           << ", shake = " << shake);
            //锁屏时用户不可能正看着窗口：锁屏 + 正在看 与 未锁屏 + 没在看 的会话行、未读、托盘一致
            const balloonmain::ArrivalDecision normal =
                balloonmain::DecideArrival(Arrival(modes[m], group, windowOpen, false, shake, false));
            const balloonmain::ArrivalDecision locked =
                balloonmain::DecideArrival(Arrival(modes[m], group, windowOpen, true, shake, true));
            CHECK_FALSE(locked.m_popupWindow);
            CHECK_FALSE(locked.m_flashTaskbar);
            CHECK_FALSE(locked.m_playSound);
            CHECK(locked.m_updateRow == normal.m_updateRow);
            CHECK(locked.m_countUnread == normal.m_countUnread);
            CHECK(locked.m_flashTray == normal.m_flashTray);
        }
    }
    //特别关注的单聊、窗口没开：未锁屏会弹窗，锁屏不弹
    CHECK(balloonmain::DecideArrival(Arrival(notify_mode_starred, false, false, false, false, false)).m_popupWindow);
    CHECK_FALSE(balloonmain::DecideArrival(Arrival(notify_mode_starred, false, false, false, false, true)).m_popupWindow);
}

TEST_CASE("LS6 解锁时已登录才重新注册全局快捷键", "[lock]")
{
    CHECK(ShouldResumeHotkeysAfterUnlock(true));
    CHECK_FALSE(ShouldResumeHotkeysAfterUnlock(false));
}
