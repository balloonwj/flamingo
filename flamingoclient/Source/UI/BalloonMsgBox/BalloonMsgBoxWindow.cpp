/**
 *  BalloonMsgBoxWindow.cpp
 *
 *  「消息盒子」窗口的实现：建窗、标题栏、事件回投主面板、滚动条动画与让位。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"
#include "BalloonMsgBoxWindow.h"
#include "MsgBoxLayout.h"
#include "../../App/BrandText.h"                       // g_strAppTitle（创建窗口时的初始标题）
#include "../BalloonMain/BalloonMainPanel.h"           // 点击、右键、按钮回投主面板
#include "../BalloonMain/MainPanelTreeView.h"
#include "../BalloonMain/MainPanelScrollView.h"        // 列表的悬浮式滚动视口（动画推进、边框让位）
#include "../BalloonMain/OverlayScrollBarGeometry.h"   // ComputeResizeBorderInsetPx
#include "../BalloonMain/TitleBarIcon.h"               // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../net/Protocol.h"                        // conversation_group（「批量管理」先显示的页签）
#include "../../Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTreeView.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace {

const int kTitleIconSize = 16;                              // 标题栏左侧图标边长（逻辑像素）
const COLORREF kCaptionColor = RGB(0x33, 0x33, 0x33);       // 标题文字与最小化 / 最大化 / 关闭字形
const COLORREF kTitleBarBg   = RGB(0xFF, 0xFF, 0xFF);       // 标题栏透明后穿透显示的底色（白）

} // anonymous namespace

CBalloonMsgBoxWindow::CBalloonMsgBoxWindow()
    : m_pHost(NULL)
    , m_hTitleIcon(NULL)
{
}

CBalloonMsgBoxWindow::~CBalloonMsgBoxWindow()
{
    //客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底；标题栏图标位图由本类持有
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

bool CBalloonMsgBoxWindow::Open(CBalloonMainPanel* pHost)
{
    m_pHost = pHost;
    if (IsWindow())
    {
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        ::SetForegroundWindow(m_hWnd);
        return true;
    }

    if (Create(NULL, CWindow::rcDefault, g_strAppTitle.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("message box window Create failed.");
        return false;
    }

    SetTitle(TR(L"消息盒子"));
    SetTitleBarHeight(balloonmsgbox::kMsgBoxTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
    m_taskIcons.ApplyTaskIcons(m_hWnd);
    SetButtons(true, true, true);   //最小化 / 最大化 / 关闭
    SetResizable(true);
    SetMinSize(balloonmsgbox::kMsgBoxMinWidth, balloonmsgbox::kMsgBoxMinHeight);

    SetClientContent(balloonmsgbox::BuildMsgBoxBody());

    //整窗尺寸，含标题栏（开发约定文档 UI 约定第 7 条）
    ResizeClient(balloonmsgbox::kMsgBoxWindowWidth, balloonmsgbox::kMsgBoxWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    //布局已落定，滚动条给缩放边框让出的宽度此时才算得出
    SyncScrollBarEdgeInset();
    LOG_INFO("message box window created and shown.");
    return true;
}

balloonmain::MainPanelTreeView* CBalloonMsgBoxWindow::GetTree() const
{
    if (!IsWindow())
    {
        return NULL;
    }
    balloonwjui::DuiControl* content = const_cast<CBalloonMsgBoxWindow*>(this)->GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(balloonmsgbox::kIdMsgBoxTree));
}

void CBalloonMsgBoxWindow::ApplyState(int convCount, int unreadTotal)
{
    if (!IsWindow())
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonmsgbox::ApplyMsgBoxState(content, convCount, unreadTotal);
    balloonmsgbox::SyncMsgBoxContentHeight(content);
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

LRESULT CBalloonMsgBoxWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    DestroyWindow();
    LOG_INFO("message box window closed.");
    return 0;
}

LRESULT CBalloonMsgBoxWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL || m_pHost == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //顶部「全部标为已读」
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == balloonmsgbox::kIdMsgBoxMarkAllRead)
    {
        m_pHost->MarkAllMsgBoxRead();
        return 0;
    }
    //顶部「批量管理」：被收进的会话单聊群聊都有，先显示群聊页
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == balloonmsgbox::kIdMsgBoxManage)
    {
        m_pHost->OpenNotifyModeManage(conversation_group);
        return 0;
    }
    //列表行单击：打开该会话的聊天窗并清未读，本窗口保持打开
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == balloonmsgbox::kIdMsgBoxTree)
    {
        m_pHost->OnMsgBoxWindowRowClick((int)pNotify->extra);
        return 0;
    }
    //列表行右键：与主面板会话右键菜单相同
    if (pNotify->code == (UINT)balloonwjui::DuiTreeView::DUITVN_RCLICK
        && pNotify->ctrlId == balloonmsgbox::kIdMsgBoxTree)
    {
        m_pHost->OnMsgBoxWindowRowRightClick((int)pNotify->extra);
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonMsgBoxWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM /*lParam*/, BOOL& bHandled)
{
    //只旁听：DuiFrameWindow 还要靠它做客户区重排。DPI 变化（窗口拖到另一块显示器）后会跟着
    //收到 WM_SIZE，让位宽度随之更新
    bHandled = FALSE;
    SyncScrollBarEdgeInset();
    return 0;
}

void CBalloonMsgBoxWindow::SyncScrollBarEdgeInset()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonmain::MainPanelScrollView* sv =
        dynamic_cast<balloonmain::MainPanelScrollView*>(content->FindCtrlById(balloonmsgbox::kIdMsgBoxScroll));
    if (sv == NULL)
    {
        return;
    }

    //与 DuiFrameWindow::OnNcHitTestMsg 同一口径：边框宽度是 96-dpi 逻辑像素，按当前显示器 DPI
    //折算成物理像素。列表视口横向占满客户区（根容器没有左右内边距），右缘就是窗口右缘，
    //所以整条缩放边框都要让出，不依赖视口当前的矩形（WM_SIZE 时它还没按新尺寸重排）
    sv->SetEdgeInsetPx(balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(), GetDpi(), IsResizable()));
}
