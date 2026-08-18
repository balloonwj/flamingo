/**
 *  NotificationSound.cpp
 *
 *  通知提示音<u>纯逻辑</u>的实现：种类 → wav 相对路径的映射，以及"好友是否刚上线"
 *  的 presence 跳变判定。只装配数据 / 做纯比较，不碰任何窗口、不调声音 API，故可被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用（在 Flamingo.vcxproj
 *  里需标记 PrecompiledHeader=NotUsing，不 include stdafx.h）。
 *
 *  本文件含中文注释；两个工程均已开启 /utf-8 编译选项，源码按 UTF-8 解析，无需 BOM。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "NotificationSound.h"

#include "../../net/Protocol.h"   // presence_type（离线/在线/离开/忙碌的取值，用于"刚上线"判定）

namespace balloonmain {

namespace {

// 判断某个 presence 是否为"可见在线态" —— 在线 / 离开 / 忙碌均算（对外可见且在线）；
// 离线不算。与 v1 OnStatusChangeMsg 里 bOnline 的语义保持一致。
bool IsVisibleOnline(int presence)
{
    return presence == presence_online
        || presence == presence_away
        || presence == presence_busy;
}

} // anonymous namespace

const wchar_t* NotifySoundRelativePath(NotifySoundKind kind)
{
    switch (kind)
    {
    case kNotifySoundNewMessage:
        return L"Sound\\msg.wav";
    case kNotifySoundBuddyOnline:
        return L"Sound\\global.wav";
    case kNotifySoundAnnouncement:
        return L"Sound\\system.wav";

    //通话音效：正弦合成的专用素材。回铃与忙音取 450Hz（中国电信标准音），回铃 1 秒响
    //4 秒停、忙音 0.35 秒通断；来电铃声用三音上行琶音。循环类整段以静音收尾、每段音
    //首尾做淡入淡出，保证 SND_LOOP 每圈接缝处不出爆音。
    case kNotifySoundCallOutgoing:
        return L"Sound\\call_outgoing.wav";
    case kNotifySoundCallIncoming:
        return L"Sound\\call_incoming.wav";
    case kNotifySoundCallConnected:
        return L"Sound\\call_connected.wav";
    case kNotifySoundCallEnded:
        return L"Sound\\call_ended.wav";
    case kNotifySoundCallBusy:
        return L"Sound\\call_busy.wav";
    default:
        return L"";
    }
}

bool IsLoopSound(NotifySoundKind kind)
{
    //只有呼出回铃与来电铃声需要响到状态改变为止；其余都是响一声即止。
    return kind == kNotifySoundCallOutgoing || kind == kNotifySoundCallIncoming;
}

bool IsBuddyJustCameOnline(int oldPresence, int newPresence)
{
    // 仅当"变化前不可见在线、变化后可见在线"才算刚上线，响一次提示音。
    return IsVisibleOnline(newPresence) && !IsVisibleOnline(oldPresence);
}

bool IsBuddyJustWentOffline(int oldPresence, int newPresence)
{
    // 与上面那条严格对称：变化前可见在线、变化后不可见，才算刚下线。
    // 共用 IsVisibleOnline 这一套口径，两处判定不会对同一次跳变各说各话。
    return IsVisibleOnline(oldPresence) && !IsVisibleOnline(newPresence);
}

} // namespace balloonmain
