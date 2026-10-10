/**
 *  LockSuppressPolicy.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "LockSuppressPolicy.h"

namespace balloonlock {

LockEventAction DecideDuringLock(LockEventKind kind, bool locked)
{
    if (!locked)
    {
        return kLockActNormal;
    }
    //以下八类是提醒或必须完成的动作：解锁后补弹（公告、备忘、升级提示、文件共享通知）或补做（被挤下线的提示框、
    //强制升级的退出、重连后显示主面板），锁屏期间先记下来
    switch (kind)
    {
    //平台公告：用户需要看到，解锁后补弹
    case kLockEvtAnnouncement:
    //群公告：同上
    case kLockEvtGroupAnnouncement:
    //备忘提醒：用户设的提醒时间已到，解锁后补弹，不能丢
    case kLockEvtMemoReminder:
    //自动升级的提示窗口：解锁后再弹
    case kLockEvtUpdatePrompt:
    //强制升级下载完毕：退出会让锁屏随进程一起消失，必须等解锁后再启动更新程序并退出
    case kLockEvtForcedUpdateExit:
    //被挤下线：连接照常断开，提示框解锁后再弹
    case kLockEvtKickout:
    //重连成功后显示主面板：它同时刷新主面板顶部的本人资料，不能丢，解锁后再做
    case kLockEvtRaisePanel:
    //文件共享的通知：别人改动了本人的文件、本人的传输结束了，解锁后补弹
    case kLockEvtFileShareNotice:
        return kLockActDefer;

    //以下八类是即时性的弹窗、抢前台、提示音、气泡与快捷键：解锁后再出现已无意义，直接丢弃
    //（新消息本身照常进会话列表与未读数）
    //新消息自动打开聊天窗、闪任务栏
    case kLockEvtMessagePopup:
    //已打开的聊天窗抖动、提到前台
    case kLockEvtChatWindowRaise:
    //桌面字幕显示新消息
    case kLockEvtSubtitle:
    //特别关注的上线提醒
    case kLockEvtStarredPresence:
    //停靠主面板因光标靠边而滑出
    case kLockEvtDockSlideOut:
    //托盘气泡提示
    case kLockEvtTrayBalloon:
    //各类提示音
    case kLockEvtNotifySound:
    //全局快捷键
    case kLockEvtGlobalHotkey:
        return kLockActDrop;

    //不认识的来源：保守起见丢弃，锁屏期间宁可少弹
    default:
        return kLockActDrop;
    }
}

bool ShouldResumeHotkeysAfterUnlock(bool loggedIn)
{
    return loggedIn;
}

} // namespace balloonlock
