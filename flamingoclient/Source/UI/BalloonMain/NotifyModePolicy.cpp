/**
 *  NotifyModePolicy.cpp
 *
 *  收到消息时按提醒模式做判定的纯函数实现。不碰窗口、不碰网络，Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "NotifyModePolicy.h"
#include "NotifyModeModel.h"
#include "../../net/Protocol.h"   // enum conversation_notify_mode

namespace balloonmain {

namespace {

// 不考虑锁屏时的判定（各模式的规则），锁屏的影响由 DecideArrival 在此结果上统一去掉
ArrivalDecision DecideArrivalCore(const ArrivalInput& in)
{
    ArrivalDecision d;

    //屏蔽：只是不提醒、不计未读、不碰会话列表，消息本身照常收下（已打开的聊天窗照常显示）。
    //被 @ 也不例外
    if (in.m_mode == notify_mode_block)
        return d;

    d.m_updateRow = true;

    //单聊窗口抖动：在各模式下都照常弹出并晃动聊天窗（2026-10-03 确认保持现状），不闪托盘、不响
    if (in.m_isShake)
    {
        d.m_popupWindow = true;
        d.m_countUnread = in.m_countable && !in.m_watching;
        return d;
    }

    //自己从别的设备同步回来的消息、群系统消息：只更新会话行，不计未读、不提醒
    if (!in.m_countable)
        return d;

    //正看着这个聊天窗：消息已当场被看到
    if (in.m_watching)
        return d;

    d.m_countUnread = true;

    switch (in.m_mode)
    {
    //特别关注：窗口没开就弹出来；开着但没在看就只闪任务栏，不反复提到前台。两种情况都响提示音
    case notify_mode_starred:
        if (in.m_windowOpen)
            d.m_flashTaskbar = true;
        else
            d.m_popupWindow = true;
        d.m_playSound = true;
        break;

    //接收不提醒：不闪不响；群里被 @ 时例外，与允许提醒一样提醒
    case notify_mode_silent:
        if (in.m_isGroup && in.m_atMe && !in.m_windowOpen)
        {
            d.m_flashTray = true;
            d.m_playSound = true;
        }
        break;

    //收进消息盒子：不闪不响，被 @ 也只在摘要前加标注（由调用方处理）
    case notify_mode_msgbox:
        break;

    //只提醒 @我和关注的人（只有群聊能设）：被 @（含 @所有人）或关注的成员发言、且窗口没开时
    //闪托盘并响提示音，其余消息只计未读；不弹聊天窗（docs/消息提醒模式与消息盒子方案.md §12.3）
    case notify_mode_mention_follow:
        if ((in.m_atMe || in.m_fromFollowed) && !in.m_windowOpen)
        {
            d.m_flashTray = true;
            d.m_playSound = true;
        }
        break;

    //允许提醒（以及不认识的值，宁可多提醒）：窗口没开时闪托盘、响提示音；窗口开着时消息已
    //渲染进窗口，不再另外提醒
    case notify_mode_allow:
    default:
        if (!in.m_windowOpen)
        {
            d.m_flashTray = true;
            d.m_playSound = true;
        }
        break;
    }
    return d;
}

} // namespace

ArrivalDecision DecideArrival(const ArrivalInput& in)
{
    //锁屏期间用户看不到任何聊天窗与字幕，一律按「没在看」判定，消息照常计未读
    ArrivalInput effective = in;
    if (in.m_screenLocked)
    {
        effective.m_watching = false;
    }
    ArrivalDecision d = DecideArrivalCore(effective);
    //锁屏期间不弹出聊天窗、不闪任务栏、不响提示音；会话行、未读数与托盘闪烁照常（托盘图标被锁屏盖住，
    //解锁后仍在闪，提示用户锁屏期间来过消息）
    if (in.m_screenLocked)
    {
        d.m_popupWindow = false;
        d.m_flashTaskbar = false;
        d.m_playSound = false;
    }
    //隐藏的会话（帖子群功能不可用时的帖子群）：用户在界面上看不到它，任何提醒都无从解释，一律不提醒；
    //会话数据与未读数照常更新，功能重新可用时原样回来
    if (in.m_hidden)
    {
        d.m_popupWindow = false;
        d.m_flashTray = false;
        d.m_flashTaskbar = false;
        d.m_playSound = false;
    }
    return d;
}

bool IsQuietBadgeMode(int mode)
{
    return mode == notify_mode_silent || mode == notify_mode_msgbox || mode == notify_mode_mention_follow;
}

bool ShowsUnreadBadge(int mode)
{
    return mode != notify_mode_block;
}

bool ShowsUnreadDot(int unread, bool marked, int mode)
{
    return marked && unread <= 0 && ShowsUnreadBadge(mode);
}

bool ShouldClearTrayOnModeChange(int newMode)
{
    return newMode == notify_mode_silent || newMode == notify_mode_msgbox || newMode == notify_mode_block
        || newMode == notify_mode_mention_follow;
}

bool IsLeavingBlock(int oldMode, int newMode)
{
    return oldMode == notify_mode_block && newMode != notify_mode_block;
}

int ComputeAlertingUnread(const std::vector<net::ConversationItem>& items, const NotifyModeModel& modes)
{
    int total = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const net::ConversationItem& c = items[i];
        if (c.m_unread <= 0)
            continue;

        const int mode = modes.GetMode(c.m_ctype, c.m_cid);
        if (mode == notify_mode_allow || mode == notify_mode_starred)
            total += c.m_unread;
    }
    return total;
}

} // namespace balloonmain
