/**
 *  NotificationSoundTests.cpp
 *
 *  通知提示音纯逻辑（NotificationSound.h / .cpp）的测试：验证
 *    · 三种提示音种类各自映射到正确的 wav 相对路径（容易写错文件名 / 漏 Sound\ 前缀）；
 *    · "好友刚上线"判定 IsBuddyJustCameOnline 的真值表方向正确 —— 只有"不可见态 →
 *      可见在线态"的跳变才算上线，在线态之间互切、或掉线、或无变化都不算。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言、不出声。presence 取值与 net/Protocol.h
 *  的 presence_type 对齐（离线 0 / 在线 1 / 离开 2 / 忙碌 3；4 已作废），此处用字面量
 *  以保持测试自包含、不引 Protocol.h。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cwchar>          // wcscmp —— 比对 wav 相对路径文本

#include "../UI/BalloonMain/NotificationSound.h"

using namespace balloonmain;

// presence_type 取值（对齐 net/Protocol.h），仅本测试内部用：
static const int kOffline   = 0;   // 离线
static const int kOnline    = 1;   // 在线
static const int kAway      = 2;   // 离开
static const int kBusy      = 3;   // 忙碌（勿扰）
static const int kRetired = 4;     // 已作废的取值（曾经是隐身），按不可见态处理

// =====================================================================
// NSND-PATH. 种类 → wav 相对路径
// =====================================================================

TEST_CASE("NSND-PATH1 三种提示音映射到正确的 wav 相对路径", "[notifysound]")
{
    CHECK(::wcscmp(NotifySoundRelativePath(kNotifySoundNewMessage),   L"Sound\\msg.wav") == 0);
    CHECK(::wcscmp(NotifySoundRelativePath(kNotifySoundBuddyOnline),  L"Sound\\global.wav") == 0);
    CHECK(::wcscmp(NotifySoundRelativePath(kNotifySoundAnnouncement), L"Sound\\system.wav") == 0);
}

// =====================================================================
// NSND-ONLINE. 好友"刚上线"判定
// =====================================================================

TEST_CASE("NSND-ONLINE1 不可见态跳变为可见在线态算上线", "[notifysound]")
{
    // 离线 → 在线 / 离开 / 忙碌：都算上线
    CHECK(IsBuddyJustCameOnline(kOffline, kOnline) == true);
    CHECK(IsBuddyJustCameOnline(kOffline, kAway) == true);
    CHECK(IsBuddyJustCameOnline(kOffline, kBusy) == true);
    // 已作废取值 → 在线：也算上线（该取值按不可见态处理）
    CHECK(IsBuddyJustCameOnline(kRetired, kOnline) == true);
}

TEST_CASE("NSND-ONLINE2 可见在线态之间互切不算上线", "[notifysound]")
{
    // 已经在线，只是状态在 在线 / 离开 / 忙碌 之间变 —— 不应再响
    CHECK(IsBuddyJustCameOnline(kOnline, kAway) == false);
    CHECK(IsBuddyJustCameOnline(kAway, kOnline) == false);
    CHECK(IsBuddyJustCameOnline(kOnline, kBusy) == false);
    CHECK(IsBuddyJustCameOnline(kBusy, kOnline) == false);
    // 无变化（在线 → 在线）也不算上线
    CHECK(IsBuddyJustCameOnline(kOnline, kOnline) == false);
}

TEST_CASE("NSND-ONLINE3 掉线 / 转不可见态 / 仍不可见都不算上线", "[notifysound]")
{
    // 在线 → 不可见态：是下线，不响
    CHECK(IsBuddyJustCameOnline(kOnline, kOffline) == false);
    CHECK(IsBuddyJustCameOnline(kOnline, kRetired) == false);
    // 不可见态之间变：始终不可见，不响
    CHECK(IsBuddyJustCameOnline(kOffline, kRetired) == false);
    CHECK(IsBuddyJustCameOnline(kRetired, kOffline) == false);
    CHECK(IsBuddyJustCameOnline(kOffline, kOffline) == false);
}

// =====================================================================
// NS-CALL. 通话音效：种类→路径映射、循环类判定
// =====================================================================

TEST_CASE("NS-CALL1 五种通话音效都映射到非空 wav 路径")
{
    const NotifySoundKind kinds[] = {
        kNotifySoundCallOutgoing, kNotifySoundCallIncoming,
        kNotifySoundCallConnected, kNotifySoundCallEnded, kNotifySoundCallBusy };
    for (int i = 0; i < 5; ++i)
    {
        INFO("kind = " << (int)kinds[i]);
        const wchar_t* p = NotifySoundRelativePath(kinds[i]);
        REQUIRE(p != NULL);
        CHECK(::wcslen(p) > 0);
        //都落在 Sound 子目录下
        CHECK(::wcsncmp(p, L"Sound\\", 6) == 0);
    }
}

TEST_CASE("NS-CALL2 只有呼出回铃与来电铃声是循环类")
{
    CHECK(IsLoopSound(kNotifySoundCallOutgoing));
    CHECK(IsLoopSound(kNotifySoundCallIncoming));

    //一次性音效不得被当成循环 —— 否则响个不停
    CHECK_FALSE(IsLoopSound(kNotifySoundCallConnected));
    CHECK_FALSE(IsLoopSound(kNotifySoundCallEnded));
    CHECK_FALSE(IsLoopSound(kNotifySoundCallBusy));

    //既有的三种通知音同样不是循环类
    CHECK_FALSE(IsLoopSound(kNotifySoundNewMessage));
    CHECK_FALSE(IsLoopSound(kNotifySoundBuddyOnline));
    CHECK_FALSE(IsLoopSound(kNotifySoundAnnouncement));
}

TEST_CASE("NS-CALL3 新增种类未破坏既有三种通知音的映射")
{
    CHECK(::wcscmp(NotifySoundRelativePath(kNotifySoundNewMessage),  L"Sound\\msg.wav") == 0);
    CHECK(::wcscmp(NotifySoundRelativePath(kNotifySoundBuddyOnline), L"Sound\\global.wav") == 0);
    CHECK(::wcscmp(NotifySoundRelativePath(kNotifySoundAnnouncement), L"Sound\\system.wav") == 0);
}

TEST_CASE("NS-CALL4 非法种类仍返回空串")
{
    CHECK(::wcslen(NotifySoundRelativePath((NotifySoundKind)999)) == 0);
    CHECK(::wcslen(NotifySoundRelativePath((NotifySoundKind)-1)) == 0);
}

TEST_CASE("NS-CALL5 五种通话音效各用各的文件，互不复用")
{
    //占位阶段它们曾共用 msg/global/system 三个 wav；专用素材到位后必须各不相同，
    //否则用户听不出呼出、来电、接通、结束、忙音的区别。
    const NotifySoundKind kinds[] = {
        kNotifySoundCallOutgoing, kNotifySoundCallIncoming,
        kNotifySoundCallConnected, kNotifySoundCallEnded, kNotifySoundCallBusy };
    for (int i = 0; i < 5; ++i)
    {
        for (int j = i + 1; j < 5; ++j)
        {
            INFO("kind " << (int)kinds[i] << " vs " << (int)kinds[j]);
            CHECK(::wcscmp(NotifySoundRelativePath(kinds[i]),
                           NotifySoundRelativePath(kinds[j])) != 0);
        }
    }

    //也不得与既有三种通知音撞文件
    const NotifySoundKind others[] = {
        kNotifySoundNewMessage, kNotifySoundBuddyOnline, kNotifySoundAnnouncement };
    for (int i = 0; i < 5; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            CHECK(::wcscmp(NotifySoundRelativePath(kinds[i]),
                           NotifySoundRelativePath(others[k])) != 0);
        }
    }
}
