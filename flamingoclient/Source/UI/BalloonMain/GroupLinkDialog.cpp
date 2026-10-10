/**
 *  GroupLinkDialog.cpp —— 「生成加群链接」窗口，说明见 GroupLinkDialog.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "GroupLinkDialog.h"

#include "GroupLinkLayout.h"                  // BuildGroupLinkBody 与尺寸常量
#include "GroupLinkFlow.h"                    // 有效期、到期时间与失败原因的文字
#include "../../net/IUSocket.h"               // CIUSocket::SendCreateGroupLink / SendGetGroupLinks / SendRevokeGroupLink
#include "../../net/Protocol.h"               // error_code_server_internal
#include "../../Common/Util/ClipboardText.h"  // flamingo::CopyTextToClipboard
#include "../../Common/Util/EncodeUtil.h"     // EncodeUtil::Utf8ToUnicode
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"           // TR：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;

CGroupLinkDialog::CGroupLinkDialog()
    : m_groupId(0)
    , m_selfUserId(0)
    , m_creating(false)
    , m_pendingRevokeId(0)
{
}

CGroupLinkDialog::~CGroupLinkDialog()
{
    if (IsWindow())
    {
        DestroyWindow();
    }
}

int64_t CGroupLinkDialog::GroupId() const
{
    return const_cast<CGroupLinkDialog*>(this)->IsWindow() ? m_groupId : 0;
}

void CGroupLinkDialog::Show(HWND owner, int64_t groupId, const CString& groupName, int64_t selfUserId)
{
    const LPCTSTR title = TR(L"生成加群链接");
    if (!IsWindow())
    {
        //WS_OVERLAPPEDWINDOW：带 owner 时控件通知仍回到本窗口（开发约定文档 UI 约定 11）
        if (Create(owner, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("group link dialog: Create failed.");
            return;
        }
        SetTitle(title);
        SetTitleBarHeight(balloonmain::kGroupLinkTitleBarH);
        SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        SetButtons(false, false, true);
        SetResizable(false);
        SetClientContent(balloonmain::BuildGroupLinkBody());
        SetMinSize(balloonmain::kGroupLinkWindowWidth, balloonmain::kGroupLinkWindowHeight);
        ResizeClient(balloonmain::kGroupLinkWindowWidth, balloonmain::kGroupLinkWindowHeight);
        CenterWindow(owner);
    }

    //换了一个群：上一个群生成的链接与「我生成的链接」不再显示，上一个群还没收到结果的作废也不再归本窗口
    if (groupId != m_groupId)
    {
        m_url.clear();
        SetUrlText(TR(L"还没有生成链接"));
        SetStatusText(_T(""));
        SetButtonEnabled(balloonmain::kGroupLinkIdCopy, false);
        m_myLinks = balloonmain::MyGroupLinkList();
        m_revokeTracker.Cancel();
        m_pendingRevokeId = 0;
        balloonmain::FillMyGroupLinkList(GetClientContent(), m_myLinks);
    }
    m_groupId = groupId;
    m_selfUserId = selfUserId;
    m_creating = false;
    SetButtonEnabled(balloonmain::kGroupLinkIdCreate, true);
    SetButtonEnabled(balloonmain::kGroupLinkIdCopy, !m_url.empty());

    DuiControl* root = GetClientContent();
    DuiLabel* name = (root != NULL) ? dynamic_cast<DuiLabel*>(root->FindCtrlById(balloonmain::kGroupLinkIdGroupName)) : NULL;
    if (name != NULL)
    {
        name->SetText(groupName);
    }
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    //每次打开都重拉一次：上次打开之后链接可能已过期，或已被群主、管理员作废
    RequestMyLinks();
    LOG_INFO("group link dialog: shown, groupid=%lld.", (long long)groupId);
}

void CGroupLinkDialog::RequestMyLinks()
{
    if (m_groupId <= 0)
    {
        return;
    }
    CIUSocket::GetInstance().SendGetGroupLinks(m_groupId);
}

void CGroupLinkDialog::OnLinksArrived(const net::GroupLinksResult& result)
{
    if (!IsWindow())
    {
        return;
    }
    if (!balloonmain::ApplyMyGroupLinks(result, m_groupId, m_selfUserId, m_myLinks))
    {
        return;
    }
    balloonmain::FillMyGroupLinkList(GetClientContent(), m_myLinks);
    LOG_INFO("group link dialog: my links arrived, groupid=%lld, code=%d, total=%u, mine=%u.", (long long)m_groupId,
             result.m_code, (unsigned)result.m_links.size(), (unsigned)m_myLinks.m_rows.size());
}

void CGroupLinkDialog::OnCopyRow(size_t index)
{
    if (index >= m_myLinks.m_rows.size())
    {
        return;
    }
    if (flamingo::CopyTextToClipboard(m_myLinks.m_rows[index].m_url.c_str()))
    {
        SetStatusText(TR(L"链接已复制，可以粘贴给同事。"));
    }
    else
    {
        SetStatusText(TR(L"复制失败，请稍后重试。"));
        LOG_WARNING("group link dialog: copy row to clipboard failed.");
    }
}

void CGroupLinkDialog::OnRevokeRowClicked(size_t index)
{
    if (index >= m_myLinks.m_rows.size())
    {
        return;
    }
    //链接ID 在点击时记下：等到消息处理时列表可能已按新的应答重建，按行号就会找错链接
    m_pendingRevokeId = m_myLinks.m_rows[index].m_id;
    PostMessage(WM_GROUP_LINK_DEFERRED, (WPARAM)kGroupLinkDeferredRevoke, 0);
}

void CGroupLinkDialog::RevokePendingLink()
{
    const int64_t linkId = m_pendingRevokeId;
    m_pendingRevokeId = 0;
    if (linkId <= 0 || m_groupId <= 0)
    {
        return;
    }
    //上一条作废还没有结果：1342 应答里没有链接ID，同时等两条就分不清哪条是哪条的结果
    if (m_revokeTracker.IsWaiting())
    {
        SetStatusText(TR(L"正在作废上一个链接，请稍候。"));
        return;
    }
    if (::MessageBox(m_hWnd, TR(L"确定作废这个加群链接吗？\n\n作废后再点这个链接将无法加群，已经通过它加入的成员不受影响。"),
                     TR(L"作废链接"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return;
    }
    m_revokeTracker.Begin(linkId);
    SetStatusText(TR(L"正在作废链接…"));
    CIUSocket::GetInstance().SendRevokeGroupLink(m_groupId, linkId);
    LOG_INFO("group link dialog: revoke requested, groupid=%lld, id=%lld.", (long long)m_groupId, (long long)linkId);
}

bool CGroupLinkDialog::TakeRevokeResult(int code)
{
    const int64_t linkId = m_revokeTracker.LinkId();
    if (!m_revokeTracker.TakeResult())
    {
        return false;
    }
    if (IsWindow())
    {
        SetStatusText(balloonmain::RevokeResultStatusText(code).c_str());
    }
    //成功与失败都重拉：失败多半是链接已被别人作废或已过期，列表也该去掉它
    RequestMyLinks();
    LOG_INFO("group link dialog: revoke result, groupid=%lld, id=%lld, code=%d.", (long long)m_groupId,
             (long long)linkId, code);
    return true;
}

LRESULT CGroupLinkDialog::OnDeferred(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    switch ((int)wParam)
    {
    //「我生成的链接」某一行点了「作废」
    case kGroupLinkDeferredRevoke:
        RevokePendingLink();
        break;

    //未知动作：忽略
    default:
        break;
    }
    return 0;
}

void CGroupLinkDialog::SetUrlText(LPCTSTR text)
{
    DuiControl* root = GetClientContent();
    DuiLabel* label = (root != NULL) ? dynamic_cast<DuiLabel*>(root->FindCtrlById(balloonmain::kGroupLinkIdUrl)) : NULL;
    if (label != NULL)
    {
        label->SetText(text);
    }
}

void CGroupLinkDialog::SetStatusText(LPCTSTR text)
{
    DuiControl* root = GetClientContent();
    DuiLabel* label = (root != NULL) ? dynamic_cast<DuiLabel*>(root->FindCtrlById(balloonmain::kGroupLinkIdStatus)) : NULL;
    if (label != NULL)
    {
        label->SetText(text);
    }
}

void CGroupLinkDialog::SetButtonEnabled(UINT ctrlId, bool enabled)
{
    DuiControl* root = GetClientContent();
    DuiControl* button = (root != NULL) ? root->FindCtrlById(ctrlId) : NULL;
    if (button != NULL)
    {
        button->SetEnabled(enabled);
        button->Invalidate();
    }
}

void CGroupLinkDialog::OnCreate()
{
    if (m_groupId <= 0 || m_creating)
    {
        return;
    }
    DuiControl* root = GetClientContent();
    DuiComboBox* combo = (root != NULL) ? dynamic_cast<DuiComboBox*>(root->FindCtrlById(balloonmain::kGroupLinkIdValidity)) : NULL;
    const int index = (combo != NULL) ? combo->GetCurSel() : balloonmain::kGroupLinkValidityDefaultIndex;
    const int days = balloonmain::GroupLinkDaysByChoice(index);
    m_creating = true;
    SetButtonEnabled(balloonmain::kGroupLinkIdCreate, false);
    SetStatusText(TR(L"正在生成链接…"));
    CIUSocket::GetInstance().SendCreateGroupLink(m_groupId, days);
    LOG_INFO("group link dialog: create requested, groupid=%lld, days=%d.", (long long)m_groupId, days);
}

void CGroupLinkDialog::OnLinkCreated(const net::CreateGroupLinkResult& result)
{
    if (!IsWindow() || !m_creating)
    {
        return;
    }
    m_creating = false;
    SetButtonEnabled(balloonmain::kGroupLinkIdCreate, true);
    if (result.m_code != 0 || !result.m_hasLink || result.m_link.m_groupid != m_groupId)
    {
        const int code = (result.m_code != 0) ? result.m_code : error_code_server_internal;
        SetStatusText(balloonmain::GroupLinkErrorText(code).c_str());
        LOG_WARNING("group link dialog: create failed, groupid=%lld, code=%d.", (long long)m_groupId, result.m_code);
        return;
    }
    m_url = EncodeUtil::Utf8ToUnicode(result.m_link.m_url);
    SetUrlText(m_url.c_str());
    SetStatusText(balloonmain::GroupLinkExpireText(result.m_link.m_expiretime).c_str());
    SetButtonEnabled(balloonmain::kGroupLinkIdCopy, true);
    //新生成的链接也要出现在「我生成的链接」里
    RequestMyLinks();
    LOG_INFO("group link dialog: link created, groupid=%lld, id=%lld.", (long long)m_groupId,
             (long long)result.m_link.m_id);
}

void CGroupLinkDialog::OnCopy()
{
    if (m_url.empty())
    {
        return;
    }
    if (flamingo::CopyTextToClipboard(m_url.c_str()))
    {
        SetStatusText(TR(L"链接已复制，可以粘贴给同事。"));
    }
    else
    {
        SetStatusText(TR(L"复制失败，请稍后重试。"));
        LOG_WARNING("group link dialog: copy to clipboard failed.");
    }
}

LRESULT CGroupLinkDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //只隐藏不销毁：下次打开直接换内容；实例归主面板管
    ShowWindow(SW_HIDE);
    return 0;
}

LRESULT CGroupLinkDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    //「我生成的链接」逐行的「复制」与「作废」
    const UINT ctrlId = pNotify->ctrlId;
    if (ctrlId >= (UINT)balloonmain::kGroupLinkIdRowCopyBase
        && ctrlId < (UINT)(balloonmain::kGroupLinkIdRowCopyBase + balloonmain::kMyGroupLinkRowMax))
    {
        OnCopyRow(ctrlId - (UINT)balloonmain::kGroupLinkIdRowCopyBase);
        return 0;
    }
    if (ctrlId >= (UINT)balloonmain::kGroupLinkIdRowRevokeBase
        && ctrlId < (UINT)(balloonmain::kGroupLinkIdRowRevokeBase + balloonmain::kMyGroupLinkRowMax))
    {
        OnRevokeRowClicked(ctrlId - (UINT)balloonmain::kGroupLinkIdRowRevokeBase);
        return 0;
    }
    switch (ctrlId)
    {
    //「生成链接」
    case balloonmain::kGroupLinkIdCreate:
        OnCreate();
        return 0;

    //「复制链接」
    case balloonmain::kGroupLinkIdCopy:
        OnCopy();
        return 0;

    //「关闭」：隐藏窗口
    case balloonmain::kGroupLinkIdClose:
        ShowWindow(SW_HIDE);
        return 0;

    //其它控件的点击：交给基类
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}
