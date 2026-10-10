/**
 *  文件共享弹窗通知的总控，说明见 ShareNoticeCenter.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareNoticeCenter.h"

#include <vector>

#include "../Engine/ShareWinPath.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/CornerPopupPlacement.h"   //MoveWindowToWorkAreaCorner：贴工作区右下角
#include "../../Source/net/Protocol.h"

namespace fileshare {

CShareNoticeCenter::CShareNoticeCenter()
    : m_host(NULL)
    , m_actions(NULL)
    , m_suppressed(false)
    , m_deferred(false)
    , m_lastSoundMs(0)
    , m_soundGapMs(kShareNoticeSoundGapMs)
{
    m_ownerPopup.SetKind(share_notice_popup_owner, this);
    m_visitorPopup.SetKind(share_notice_popup_visitor, this);
}

CShareNoticeCenter::~CShareNoticeCenter()
{
    Detach();
}

void CShareNoticeCenter::Attach(IFileShareHost* host, IShareNoticeActions* actions)
{
    m_host = host;
    m_actions = actions;
    m_suppressed = false;
}

void CShareNoticeCenter::Detach()
{
    Reset();
    m_host = NULL;
    m_actions = NULL;
}

void CShareNoticeCenter::SetSuppressed(bool suppressed)
{
    m_suppressed = suppressed;
}

void CShareNoticeCenter::Reset()
{
    m_ownerPopup.Hide();
    m_visitorPopup.Hide();
    m_ownerList.Clear();
    m_visitorList.Clear();
    m_ownerPayloads.clear();
    m_visitorPayloads.clear();
    m_deferred = false;
}

void CShareNoticeCenter::SetSoundGapMs(int64_t gapMs)
{
    if (gapMs >= 0)
        m_soundGapMs = gapMs;
}

int CShareNoticeCenter::QueryPolicy() const
{
    if (m_host == NULL)
        return fileshare_notice_drop;
    return m_host->QueryNoticePolicy();
}

void CShareNoticeCenter::OnVisitorTransferFinished(const ShareVisitorTransferResult& result)
{
    if (m_suppressed || m_host == NULL || m_actions == NULL)
        return;
    const bool foreground = m_actions->IsShareBrowserForeground(result.m_shareId);
    if (!ShareNoticeModel::ShouldNotifyVisitorTransfer(result, foreground))
    {
        LOG_INFO("share notice skipped for a transfer, shareid: %lld, local id: %u, code: %d, user canceled: %d, drag-out: %d, "
                 "browser foreground: %d.", static_cast<long long>(result.m_shareId), result.m_localId, result.m_code,
                 result.m_userCanceled ? 1 : 0, result.m_dragOut ? 1 : 0, foreground ? 1 : 0);
        return;
    }
    AddEntry(share_notice_popup_visitor, ShareNoticeModel::MakeVisitorEntry(result), &result, NULL);
}

void CShareNoticeCenter::OnOwnerActivity(const ShareOwnerActivity& activity)
{
    if (m_suppressed || m_host == NULL || !ShareNoticeModel::IsOwnerActionNotified(activity.m_action))
        return;
    AddEntry(share_notice_popup_owner, ShareNoticeModel::MakeOwnerEntry(activity, VisitorName(activity)), NULL, &activity);
}

std::wstring CShareNoticeCenter::VisitorName(const ShareOwnerActivity& activity) const
{
    char buf[kFileShareTextBytes] = { 0 };
    std::wstring name;
    if (m_host != NULL && m_host->GetUserDisplayName(activity.m_visitorId, buf, kFileShareTextBytes))
        ShareUtf8ToWide(buf, name);
    if (name.empty())
        ShareUtf8ToWide(activity.m_visitorName, name);
    return name;
}

void CShareNoticeCenter::AddEntry(int kind, const ShareNoticeEntry& entry, const ShareVisitorTransferResult* visitor,
                                  const ShareOwnerActivity* owner)
{
    const int policy = QueryPolicy();
    //勿扰时段：不弹，记录照常留在「最近操作」与传输列表里
    if (policy == fileshare_notice_drop)
    {
        LOG_INFO("share notice dropped by the host policy, kind: %d.", kind);
        return;
    }
    if (kind == share_notice_popup_owner && owner != NULL)
    {
        const uint32_t id = m_ownerList.Add(entry);
        m_ownerPayloads[id] = *owner;
    }
    else if (kind == share_notice_popup_visitor && visitor != NULL)
    {
        const uint32_t id = m_visitorList.Add(entry);
        m_visitorPayloads[id] = *visitor;
    }
    PrunePayloads();
    //锁屏期间：先记下，解锁后由定时检查补弹
    if (policy == fileshare_notice_defer)
    {
        m_deferred = true;
        LOG_INFO("share notice deferred by the host policy, kind: %d, owner entries: %u, visitor entries: %u.", kind,
                 static_cast<unsigned>(m_ownerList.Size()), static_cast<unsigned>(m_visitorList.Size()));
        return;
    }
    LOG_INFO("share notice shown, kind: %d, owner entries: %u, visitor entries: %u.", kind,
             static_cast<unsigned>(m_ownerList.Size()), static_cast<unsigned>(m_visitorList.Size()));
    ShowPopups(true);
}

void CShareNoticeCenter::OnTimer()
{
    if (!m_deferred)
        return;
    const int policy = QueryPolicy();
    if (policy == fileshare_notice_defer)
        return;
    m_deferred = false;
    //推迟期间进入了勿扰时段：补弹也算打扰，丢弃
    if (policy == fileshare_notice_drop)
    {
        LOG_INFO("share notices deferred during lock dropped by the host policy.");
        Reset();
        return;
    }
    LOG_INFO("share notices deferred during lock shown, owner entries: %u, visitor entries: %u.",
             static_cast<unsigned>(m_ownerList.Size()), static_cast<unsigned>(m_visitorList.Size()));
    ShowPopups(true);
}

void CShareNoticeCenter::ShowPopups(bool playSound)
{
    m_ownerPopup.ShowEntries(m_ownerList.Visible(), m_ownerList.OverflowCount());
    m_visitorPopup.ShowEntries(m_visitorList.Visible(), m_visitorList.OverflowCount());
    LayoutPopups();
    m_ownerPopup.ShowNoActivate();
    m_visitorPopup.ShowNoActivate();
    if (!playSound || m_host == NULL)
        return;
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    if (m_lastSoundMs != 0 && now - m_lastSoundMs < m_soundGapMs)
        return;
    m_lastSoundMs = now;
    m_host->PlayNoticeSound();
}

void CShareNoticeCenter::LayoutPopups()
{
    //分享者的弹窗贴着工作区右下角；访问者的弹窗在它上方，只有访问者的弹窗时它贴着右下角
    int stacked = 0;
    if (m_ownerPopup.IsWindow())
    {
        balloonmain::MoveWindowToWorkAreaCorner(m_ownerPopup.m_hWnd, balloonmain::kCornerPopupMarginX,
                                                balloonmain::kCornerPopupMarginY);
        RECT rc;
        if (::GetWindowRect(m_ownerPopup.m_hWnd, &rc))
            stacked = (rc.bottom - rc.top) + kShareNoticeStackGap;
    }
    if (m_visitorPopup.IsWindow())
        balloonmain::MoveWindowToWorkAreaCorner(m_visitorPopup.m_hWnd, balloonmain::kCornerPopupMarginX,
                                                balloonmain::kCornerPopupMarginY + stacked);
}

void CShareNoticeCenter::PrunePayloads()
{
    //条目表超出保留上限时最早的被挤掉：它们的数据也不再需要
    std::vector<uint32_t> gone;
    for (std::map<uint32_t, ShareOwnerActivity>::iterator it = m_ownerPayloads.begin(); it != m_ownerPayloads.end(); ++it)
    {
        if (m_ownerList.Find(it->first) == NULL)
            gone.push_back(it->first);
    }
    for (size_t i = 0; i < gone.size(); ++i)
        m_ownerPayloads.erase(gone[i]);
    gone.clear();
    for (std::map<uint32_t, ShareVisitorTransferResult>::iterator it = m_visitorPayloads.begin(); it != m_visitorPayloads.end();
         ++it)
    {
        if (m_visitorList.Find(it->first) == NULL)
            gone.push_back(it->first);
    }
    for (size_t i = 0; i < gone.size(); ++i)
        m_visitorPayloads.erase(gone[i]);
}

void CShareNoticeCenter::OnNoticeLink(int kind, uint32_t entryId, int action)
{
    if (m_actions == NULL)
        return;
    //分享者的通知：在资源管理器里显示那一项，或打开「文件共享」窗口选中那个共享
    if (kind == share_notice_popup_owner)
    {
        std::map<uint32_t, ShareOwnerActivity>::const_iterator it = m_ownerPayloads.find(entryId);
        if (it == m_ownerPayloads.end())
            return;
        const ShareOwnerActivity activity = it->second;
        //那一项已不在原处（之后又被改名、移动或删除）：退回打开「文件共享」窗口
        if (action == share_notice_reveal && m_actions->NoticeRevealPath(activity.m_revealPath))
            return;
        m_actions->NoticeOpenShareCenter(activity.m_shareId);
        return;
    }

    std::map<uint32_t, ShareVisitorTransferResult>::const_iterator it = m_visitorPayloads.find(entryId);
    if (it == m_visitorPayloads.end())
        return;
    const ShareVisitorTransferResult result = it->second;
    switch (action)
    {
    //「打开」：打开下载到的那一项
    case share_notice_open_file:
        m_actions->NoticeOpenPath(result.m_localTopPath);
        break;

    //「打开所在文件夹」：选中下载到的第一项；不知道它落地后的名字时打开目标目录
    case share_notice_open_folder:
        if (result.m_localTopPath.empty() || !m_actions->NoticeRevealPath(result.m_localTopPath))
            m_actions->NoticeOpenPath(result.m_targetDir);
        break;

    //「查看」：打开浏览窗口并进入上传到的目录
    case share_notice_browse:
        m_actions->NoticeBrowse(result.m_shareId, result.m_toDir);
        break;

    //「重试」：发起成功后这一条从弹窗里去掉，新一笔结束时另有通知
    case share_notice_retry:
        if (m_actions->NoticeRetry(result))
        {
            m_visitorList.Remove(entryId);
            m_visitorPayloads.erase(entryId);
            ShowPopups(false);
        }
        break;

    //不认识的动作：忽略
    default:
        break;
    }
}

void CShareNoticeCenter::OnNoticeViewAll(int kind)
{
    (void)kind;
    if (m_actions != NULL)
        m_actions->NoticeOpenShareCenter(0);
}

void CShareNoticeCenter::OnNoticePopupClosed(int kind)
{
    //用户关掉了弹窗：里面的条目一并丢弃，下一条通知到达时弹窗只显示新的
    if (kind == share_notice_popup_owner)
    {
        m_ownerList.Clear();
        m_ownerPayloads.clear();
    }
    else
    {
        m_visitorList.Clear();
        m_visitorPayloads.clear();
    }
    //另一个弹窗挪到它该在的位置（分享者的弹窗关了，访问者的落到右下角）
    LayoutPopups();
}

CShareNoticePopup& CShareNoticeCenter::GetPopup(int kind)
{
    return (kind == share_notice_popup_owner) ? m_ownerPopup : m_visitorPopup;
}

size_t CShareNoticeCenter::GetEntryCount(int kind) const
{
    return (kind == share_notice_popup_owner) ? m_ownerList.Size() : m_visitorList.Size();
}

bool CShareNoticeCenter::HasDeferred() const
{
    return m_deferred;
}

} // namespace fileshare
