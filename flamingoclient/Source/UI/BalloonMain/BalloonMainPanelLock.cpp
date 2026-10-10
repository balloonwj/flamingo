/**
 *  BalloonMainPanelLock.cpp —— 主面板在锁屏开始与结束时的处理（docs/锁屏方案.md 第 4.9 节）。
 *
 *  锁屏模块（FlamingoLock.dll）在锁屏线程上回调 CLockHost::OnLockStateChanged，宿主再把 kMsgLockStateChanged
 *  投给主面板，本文件在界面线程上处理：
 *    · 锁屏开始：暂停三个全局快捷键（已登录时）、停掉循环提示音、通知 FlamingoMedia（结束受控、结束本人主持的共享、
 *      停止录屏与语音播放，锁屏期间来电与邀请静默）；
 *    · 解锁：通知 FlamingoMedia（补弹仍在响铃的来电、补加强制共享）、恢复快捷键（已登录时），并按到达顺序补弹
 *      锁屏期间延后的平台公告、群公告、备忘提醒，以及会话控制器延后的消息（升级提示、被挤下线、重连后显示主面板）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "BalloonMainPanel.h"

#include "../../Common/Log/IULog.h"
#include "../../App/FlamingoClient.h"                    // CFlamingoClient::m_UserConfig（快捷键设置）
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h" // IMediaModule::SetScreenLocked
#include "../../Feature/Lock/LockSuppressPolicy.h"
#include "../../Feature/Screenshot/ScreenshotEntry.h"   // SuspendAppHotkeys / ResumeAppHotkeys
#include "../BalloonMemo/MemoCenter.h"                  // 解锁后补弹备忘提醒
#include "NotificationSound.h"                          // StopLoopSound

LRESULT CBalloonMainPanel::OnLockStateChangedMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (wParam != 0)
    {
        OnScreenLocked();
    }
    else
    {
        OnScreenUnlocked();
    }
    return 0;
}

void CBalloonMainPanel::OnScreenLocked()
{
    //全局快捷键：锁屏期间全部注销（键盘钩子会先拦住按键，这里再兜一层）。2026-10-07 起截图快捷键在未登录时也注册
    //（docs/截图独立模块方案.md 3.5 节），所以不论是否登录都注销；注销没有注册过的快捷键没有副作用
    m_lockSuspendedHotkeys = true;
    balloonscreenshot::SuspendAppHotkeys(m_hWnd);
    //正在循环的提示音（呼叫回铃、来电铃声）停掉；之后的提示音由全局拦截判定挡住
    balloonmain::StopLoopSound();
    //桌面字幕是置顶窗口、显示消息原文，锁屏期间全部隐藏，期间收到的消息也不进字幕
    m_subtitles.SetAllHidden(true);
    if (m_pMediaModule != NULL)
    {
        m_pMediaModule->SetScreenLocked(true);
    }
    LOG_INFO("main panel: screen locked, hotkeys %s, media module %s.", m_lockSuspendedHotkeys ? "suspended" : "not registered",
             m_pMediaModule != NULL ? "notified" : "absent");
}

void CBalloonMainPanel::OnScreenUnlocked()
{
    if (m_pMediaModule != NULL)
    {
        m_pMediaModule->SetScreenLocked(false);
    }
    //全局快捷键：截图快捷键不论是否登录都恢复；「打开主面板」与录屏两个只在解锁时已登录才恢复
    //（登录前锁屏、解锁时仍未登录的不注册，见 ShouldResumeHotkeysAfterUnlock）
    const bool loggedIn = m_sessionController.IsLoggedIn();
    const bool recordEnabled = (m_lpFMGClient != NULL) && (m_lpFMGClient->m_UserConfig.IsEnableRecordHotkey() != FALSE);
    balloonscreenshot::ResumeAppHotkeys(m_hWnd, balloonlock::ShouldResumeHotkeysAfterUnlock(loggedIn), recordEnabled,
                                        balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey);
    m_lockSuspendedHotkeys = false;
    m_subtitles.SetAllHidden(false);

    //锁屏期间延后的提醒，按到达顺序补弹
    const size_t announcements = m_lockDeferredAnnouncements.size();
    const size_t groupAnnouncements = m_lockDeferredGroupAnnouncements.size();
    std::vector<net::AnnouncementItem> items;
    items.swap(m_lockDeferredAnnouncements);
    for (size_t i = 0; i < items.size(); ++i)
    {
        ShowAnnouncePopup(items[i]);
    }
    std::vector<net::GroupAnnouncementNotify> notifies;
    notifies.swap(m_lockDeferredGroupAnnouncements);
    for (size_t i = 0; i < notifies.size(); ++i)
    {
        PopupGroupAnnouncement(notifies[i]);
    }
    balloonmemo::CMemoCenter::Instance().OnScreenUnlocked();
    m_sessionController.ReplayDeferredAfterUnlock();
    LOG_INFO("main panel: screen unlocked, logged in %d, deferred announcements %u, group announcements %u.",
             loggedIn ? 1 : 0, static_cast<unsigned>(announcements), static_cast<unsigned>(groupAnnouncements));
}
