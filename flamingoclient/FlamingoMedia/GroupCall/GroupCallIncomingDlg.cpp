/**
 *  群通话的来电窗，GroupCallIncomingDlg.cpp
 *  说明见 GroupCallIncomingDlg.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "GroupCallIncomingDlg.h"

#include <memory>
#include "GroupCallMessages.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/CornerPopupPlacement.h"
#include "../../Source/UI/BalloonMain/NotificationSound.h"   // StopLoopSound（关窗时停来电铃声）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../Common/IncomingCallLayout.h"   // 右下角来电弹窗的共用版式（420×200，左侧头像）

namespace groupcall {

namespace {

//整窗尺寸、标题栏高与版式都与一对一来电窗相同，见 Common/IncomingCallLayout.h（420 宽时标题「群视频通话 · 42 秒后
//无人接听自动结束」完整显示；原先 380×150）
//倒计时定时器 id：避开 balloonui 基类可能占用的小 id
const UINT_PTR kCountdownTimerId = 311;
//没有给出剩余振铃时长时按 45 秒处理（与服务端振铃超时一致）
const int64_t kDefaultRingMs = 45 * 1000;

} // namespace

CGroupCallIncomingDlg::CGroupCallIncomingDlg()
    : m_notifyWnd(NULL)
    , m_detailLabel(NULL)
    , m_avatarCtrl(NULL)
    , m_groupId(0)
    , m_expireAtMs(0)
{
}

void CGroupCallIncomingDlg::SetHost(flamingomedia::IMediaHost* host)
{
    m_avatars.SetHost(host);
}

CGroupCallIncomingDlg::~CGroupCallIncomingDlg()
{
    Hide();
}

void CGroupCallIncomingDlg::Open(int64_t groupId, const std::wstring& prompt, const std::wstring& detail,
                                 const std::wstring& title, int64_t ringMs, HWND notifyWnd)
{
    m_notifyWnd = notifyWnd;
    m_title = title;
    m_groupId = groupId;
    if (!IsWindow())
    {
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        if (Create(NULL, CWindow::rcDefault, title.c_str(), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("groupcall: create incoming dialog failed.");
            return;
        }
        SetTitleBarHeight(flamingomedia::kIncomingTitleBarH);
        //会自动收起的来电窗不加最小化，只留关闭按钮（关闭等同拒绝）
        SetButtons(false, false, true);
        SetResizable(false);
        SetIcon(m_icons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    }
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    const int64_t total = ringMs > 0 ? ringMs : kDefaultRingMs;
    m_countdown.Start(total, now);
    m_expireAtMs = now + total + kIncomingGraceMs;
    refreshTitle();
    ResizeClient(flamingomedia::kIncomingDlgW, flamingomedia::kIncomingDlgH);

    //左边群头像，右边两行文字，底部右侧「拒绝」「接听」（与一对一来电窗同一份版式）
    flamingomedia::IncomingCallBody body = flamingomedia::BuildIncomingCallBody(
        m_avatars.MakeGroupAvatar(groupId, flamingomedia::kIncomingAvatarPx), prompt, detail, kIdDecline, kIdAccept,
        m_buttonArt);
    m_detailLabel = body.m_line2;
    m_avatarCtrl = body.m_avatar;
    SetClientContent(std::move(body.m_root));

    //来电弹在鼠标所在显示器工作区的右下角，置顶但不抢焦点：正在别的窗口打字时不被打断
    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd, balloonmain::kCornerPopupMarginX, balloonmain::kCornerPopupMarginY);
    ShowWindow(SW_SHOWNA);
    SetTimer(kCountdownTimerId, balloonmain::kCornerCountdownTickMs, NULL);
    LOG_INFO("groupcall incoming dialog shown, ring ms=%lld.", (long long)total);
}

void CGroupCallIncomingDlg::SetDetail(const std::wstring& detail)
{
    if (!IsWindow() || m_detailLabel == NULL)
        return;
    m_detailLabel->SetText(detail.c_str());
    m_detailLabel->Invalidate();
}

void CGroupCallIncomingDlg::RefreshAvatar()
{
    if (!IsWindow() || m_avatarCtrl == NULL)
        return;
    //就地换位图；位图按内容缓存，旧位图仍由 m_avatars 持有，不会在重画前失效
    m_avatars.ApplyGroup(m_avatarCtrl, m_groupId, flamingomedia::kIncomingAvatarPx);
    m_avatarCtrl->Invalidate();
}

void CGroupCallIncomingDlg::Hide()
{
    m_countdown.Stop();
    m_notifyWnd = NULL;
    m_detailLabel = NULL;
    m_avatarCtrl = NULL;
    if (IsWindow())
        DestroyWindow();
}

bool CGroupCallIncomingDlg::IsShowing() const
{
    return const_cast<CGroupCallIncomingDlg*>(this)->IsWindow() != FALSE;
}

void CGroupCallIncomingDlg::refreshTitle()
{
    if (!IsWindow())
        return;
    const int64_t remaining = m_countdown.RemainingMs(static_cast<int64_t>(::GetTickCount64()));
    SetTitle(balloonmain::MakeCornerCountdownTitle(m_title, remaining, balloonmain::corner_countdown_ring).c_str());
}

void CGroupCallIncomingDlg::send(int command)
{
    HWND notify = m_notifyWnd;
    Hide();
    if (notify != NULL && ::IsWindow(notify))
        ::PostMessage(notify, kMsgGroupCallIncomingCommand, static_cast<WPARAM>(command), 0);
}

LRESULT CGroupCallIncomingDlg::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        //接听
        if (n->ctrlId == static_cast<UINT>(kIdAccept))
        {
            send(gc_incoming_accept);
            return 0;
        }
        //拒绝
        if (n->ctrlId == static_cast<UINT>(kIdDecline))
        {
            send(gc_incoming_decline);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CGroupCallIncomingDlg::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kCountdownTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    //倒计时走完后等服务端的振铃超时推送；推送迟迟不来时按拒绝处理，免得窗口一直挂着
    if (static_cast<int64_t>(::GetTickCount64()) >= m_expireAtMs)
    {
        LOG_WARNING("groupcall incoming dialog: no ring timeout notify from server, decline locally.");
        balloonmain::StopLoopSound();
        send(gc_incoming_decline);
        return 0;
    }
    refreshTitle();
    return 0;
}

LRESULT CGroupCallIncomingDlg::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    send(gc_incoming_decline);
    return 0;
}

} // namespace groupcall
