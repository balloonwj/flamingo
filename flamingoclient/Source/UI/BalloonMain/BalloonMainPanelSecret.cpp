/**
 *  BalloonMainPanelSecret.cpp —— 主面板一侧的单聊密聊接入（docs/密聊方案.md 4.8、6.2）。
 *
 *  密聊宿主 CSecretChatHost 把 FlamingoSecretChat.dll 的事件在界面线程上转给本面板（ISecretChatSink），本文件：
 *    · 报到应答到达、1013 改了开关后刷新各单聊窗的「密聊」按钮；
 *    · 本人发起的邀请有了应答：转给发起时所在的单聊窗；窗口已经关了时视同撤回；
 *    · 收到别人的邀请：窗口开着就插入邀请卡片；按提醒模式决定闪托盘、闪任务栏、响提示音或弹出窗口，
 *      会话行不改摘要、不计未读（docs/密聊方案.md 4.5）。窗口没开时由单聊窗打开后向宿主查回这条邀请补出卡片；
 *    · 会话状态变化、密聊文字与图片的收发进度和结果：转给对方的单聊窗；收到文字或图片时按提醒模式提醒，
 *      托盘只显示「[密聊消息]」，不写本地库、不改会话行、不进字幕、不自动回复。
 *  会话号与对方的对应、待应答邀请的登记都在宿主里，本文件不另存。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "BalloonMainPanel.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"                  // TR8 / TR_NOOP：界面文字的多语言查找
#include "../../App/FlamingoClient.h"                    // CFlamingoClient::m_UserConfig（全局静音）
#include "../../Feature/Lock/LockHost.h"                 // IsScreenLocked：锁屏期间不弹窗、不响
#include "../../Feature/SecretChat/SecretChatHost.h"
#include "../BalloonChatWnd/BalloonChatWindow.h"
#include "../BalloonChatWnd/WindowAttention.h"           // Win32WindowRaiseOps::FlashUntilForeground
#include "ConversationListOps.h"                         // ShouldTreatAsReadOnArrival：用户是否正看着窗口
#include "NotifyModePolicy.h"                            // DecideArrival：按提醒模式决定怎么提醒
#include "NotificationSound.h"                           // PlayNotifySound

namespace {

// 邀请、密聊消息到达时托盘浮窗显示的提示：不含任何聊天内容（docs/密聊方案.md 4.5）
const char* const kSecretInviteDigest = TR_NOOP("[密聊邀请]");
const char* const kSecretMessageDigest = TR_NOOP("[密聊消息]");

} // namespace

CBalloonChatWindow* CBalloonMainPanel::FindOpenChatWindow(UINT peerUserId)
{
    std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find(peerUserId);
    if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        return it->second;
    }
    return NULL;
}

void CBalloonMainPanel::RefreshSecretEntries()
{
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin(); it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->ApplySecretEntryVisibility();
        }
    }
}

void CBalloonMainPanel::NotifySecretArrival(UINT peerUserId, CBalloonChatWindow* pWin, const std::string& digestUtf8,
                                            bool allowPopup)
{
    const bool bMuted = (m_lpFMGClient != NULL) ? (m_lpFMGClient->m_UserConfig.IsEnableMute() != FALSE) : false;

    //用户是否正看着这个窗口。桌面字幕不显示密聊内容，所以不按字幕算「在看」
    HWND hWnd = (pWin != NULL) ? pWin->m_hWnd : NULL;
    const bool isWindow = (hWnd != NULL) && (::IsWindow(hWnd) != FALSE);
    const bool bWatching = balloonmain::ShouldTreatAsReadOnArrival(
        hWnd != NULL, isWindow, isWindow && (::IsWindowVisible(hWnd) != FALSE),
        isWindow && (::IsIconic(hWnd) != FALSE), isWindow && (::GetForegroundWindow() == hWnd), false);

    //与普通单聊消息同一套判定：输入按一条「别人发给我的单聊消息」构造，锁屏期间不弹窗、不闪任务栏、不响
    balloonmain::ArrivalInput arrival;
    arrival.m_mode         = m_notifyModes.GetMode(conversation_single, (int64_t)peerUserId);
    arrival.m_isGroup      = false;
    arrival.m_atMe         = false;
    arrival.m_countable    = true;
    arrival.m_windowOpen   = (pWin != NULL);
    arrival.m_watching     = bWatching;
    arrival.m_isShake      = false;
    arrival.m_screenLocked = balloonlock::IsScreenLocked();
    const balloonmain::ArrivalDecision decision = balloonmain::DecideArrival(arrival);

    //特别关注且窗口没开：建窗弹出，新窗口打开时向宿主查回这条邀请补出卡片
    if (allowPopup && decision.m_popupWindow)
    {
        CBalloonChatWindow* pPopup = EnsureChatWindow(peerUserId);
        if (pPopup != NULL)
        {
            pWin = pPopup;
        }
    }

    LOG_INFO("secret arrival: peer=%u, mode=%d, window open=%d, watching=%d, popup=%d, flashTray=%d, "
             "flashTaskbar=%d, sound=%d.",
             peerUserId, arrival.m_mode, arrival.m_windowOpen ? 1 : 0, bWatching ? 1 : 0,
             decision.m_popupWindow ? 1 : 0, decision.m_flashTray ? 1 : 0, decision.m_flashTaskbar ? 1 : 0,
             decision.m_playSound ? 1 : 0);

    //会话行不改摘要、不计未读：不调 BumpConversation
    if (decision.m_flashTray)
    {
        PostTrayFlash(conversation_single, (int64_t)peerUserId, digestUtf8);
    }
    if (decision.m_flashTaskbar && pWin != NULL)
    {
        balloonchatwnd::Win32WindowRaiseOps().FlashUntilForeground(pWin->m_hWnd);
    }
    if (decision.m_playSound)
    {
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundNewMessage, bMuted);
    }
}

//======================== balloonsecret::ISecretChatSink ========================

void CBalloonMainPanel::OnSecretRegistered(bool enabled)
{
    LOG_INFO("secret chat registered, enabled=%d.", enabled ? 1 : 0);
    RefreshSecretEntries();
}

void CBalloonMainPanel::OnSecretInviteSent(uint64_t sessionId, int64_t peerId, uint32_t remainMs, int code)
{
    CBalloonChatWindow* pWin = FindOpenChatWindow((UINT)peerId);
    if (pWin == NULL)
    {
        //发起之后窗口被关掉了：关窗视同撤回（docs/密聊方案.md 4.2）
        if (code == 0)
        {
            balloonsecret::CSecretChatHost::GetInstance().Cancel(sessionId);
        }
        LOG_INFO("secret invite answered after the window closed, session=%llu, peer=%lld, code=%d.",
                 (unsigned long long)sessionId, (long long)peerId, code);
        return;
    }
    pWin->OnSecretInviteSent(sessionId, remainMs, code);
}

void CBalloonMainPanel::OnSecretInviteArrived(uint64_t sessionId, int64_t fromId, uint32_t remainMs, int peerProtect)
{
    const UINT peerUserId = (UINT)fromId;
    CBalloonChatWindow* pWin = FindOpenChatWindow(peerUserId);
    //窗口开着：插入卡片；没开不建窗，提醒之后由用户打开窗口时补出（做法同文件夹邀请）
    if (pWin != NULL)
    {
        pWin->ShowSecretInviteCard(sessionId, remainMs, peerProtect);
    }
    NotifySecretArrival(peerUserId, pWin, TR8(kSecretInviteDigest), true);
}

void CBalloonMainPanel::OnSecretSessionState(uint64_t sessionId, int64_t peerId, int state, int reason, int peerProtect)
{
    CBalloonChatWindow* pWin = FindOpenChatWindow((UINT)peerId);
    if (pWin != NULL)
    {
        pWin->OnSecretSessionState(sessionId, state, reason, peerProtect);
        return;
    }
    //窗口没开却建立了会话（不应出现：同意要在窗口里点，发起方关窗即撤回）：直接退出，不留下没有窗口的密聊
    if (state == secretchat::session_state_active)
    {
        balloonsecret::CSecretChatHost::GetInstance().Close(sessionId);
        LOG_WARNING("secret session established without a chat window, closed, session=%llu, peer=%lld.",
                    (unsigned long long)sessionId, (long long)peerId);
        return;
    }
    LOG_INFO("secret session state without a chat window, session=%llu, peer=%lld, state=%d, reason=%d.",
             (unsigned long long)sessionId, (long long)peerId, state, reason);
}

void CBalloonMainPanel::OnSecretText(uint64_t sessionId, int64_t peerId, uint32_t msgId, const wchar_t* text,
                                     int64_t serverTimeMs)
{
    const UINT peerUserId = (UINT)peerId;
    CBalloonChatWindow* pWin = FindOpenChatWindow(peerUserId);
    //会话绑定在发起或同意时所在的窗口上，窗口一关会话即结束，所以正常情况下窗口一定开着；日志不记正文
    if (pWin == NULL)
    {
        LOG_WARNING("secret text dropped, no chat window, session=%llu, peer=%lld, msg=%u.",
                    (unsigned long long)sessionId, (long long)peerId, msgId);
        return;
    }
    pWin->OnSecretText(sessionId, msgId, text, serverTimeMs);
    //不写本地库、不改会话行、不进字幕、不自动回复；用户没在看时按提醒模式提醒，托盘只显示不含内容的提示
    NotifySecretArrival(peerUserId, pWin, TR8(kSecretMessageDigest), false);
}

void CBalloonMainPanel::OnSecretImageBegin(uint64_t sessionId, int64_t peerId, uint32_t imageId, uint32_t totalBytes,
                                           uint32_t width, uint32_t height, int64_t serverTimeMs)
{
    const UINT peerUserId = (UINT)peerId;
    CBalloonChatWindow* pWin = FindOpenChatWindow(peerUserId);
    if (pWin == NULL)
    {
        LOG_WARNING("secret image dropped, no chat window, session=%llu, peer=%lld, image=%u.",
                    (unsigned long long)sessionId, (long long)peerId, imageId);
        return;
    }
    pWin->OnSecretImageBegin(sessionId, imageId, totalBytes, width, height, serverTimeMs);
    //一张图片只在开始接收时提醒一次
    NotifySecretArrival(peerUserId, pWin, TR8(kSecretMessageDigest), false);
}

void CBalloonMainPanel::OnSecretImageProgress(uint64_t sessionId, int64_t peerId, uint32_t imageId,
                                              uint32_t receivedBytes)
{
    //进度回调频繁，不记日志
    CBalloonChatWindow* pWin = FindOpenChatWindow((UINT)peerId);
    if (pWin != NULL)
    {
        pWin->OnSecretImageProgress(sessionId, imageId, receivedBytes);
    }
}

void CBalloonMainPanel::OnSecretImage(uint64_t sessionId, int64_t peerId, uint32_t imageId, const void* bytes,
                                      uint32_t len, bool ok)
{
    CBalloonChatWindow* pWin = FindOpenChatWindow((UINT)peerId);
    if (pWin == NULL)
    {
        LOG_WARNING("secret image dropped, no chat window, session=%llu, peer=%lld, image=%u, ok=%d.",
                    (unsigned long long)sessionId, (long long)peerId, imageId, ok ? 1 : 0);
        return;
    }
    pWin->OnSecretImage(sessionId, imageId, bytes, len, ok);
}

void CBalloonMainPanel::OnSecretSendResult(uint64_t sessionId, int64_t peerId, uint32_t msgId, int code)
{
    CBalloonChatWindow* pWin = FindOpenChatWindow((UINT)peerId);
    if (pWin != NULL)
    {
        pWin->OnSecretSendResult(sessionId, msgId, code);
    }
}

void CBalloonMainPanel::OnSecretImageSendProgress(uint64_t sessionId, int64_t peerId, uint32_t imageId,
                                                  uint32_t sentBytes, uint32_t totalBytes, int code)
{
    //进度回调频繁，不记日志（结果由窗口记）
    CBalloonChatWindow* pWin = FindOpenChatWindow((UINT)peerId);
    if (pWin != NULL)
    {
        pWin->OnSecretImageSendProgress(sessionId, imageId, sentBytes, totalBytes, code);
    }
}
