/**
 *  BalloonGroupManageWindow.cpp
 *
 *  新版群管理窗口的实现：组装 Header + 左导航 + 右滚动内容区，处理左侧
 *  导航点击切页。本轮数据为本地假数据。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"
#include "BalloonGroupManageWindow.h"

#include <memory>

#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_ERROR
#include "GroupManageLayout.h"        // BuildHeader / BuildNav / BuildPage / 各 id
#include "GroupManageModel.h"         // FillMockData
#include "GroupManageMapping.h"       // MemberIndexFromCardId：成员卡右键时由卡片 id 反算成员下标

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "GroupManageNavItem.h"

using namespace balloonwjui;
using namespace balloongroupmanage;

// ---- 文件作用域常量 -----------------------------------------------------

// 窗口初始客户区尺寸（逻辑像素）。
static const int kGmWindowWidth  = 960;
static const int kGmWindowHeight = 660;

// 窗口最小尺寸（逻辑像素）—— 保证 3 列成员卡与左导航不至于挤坏。
static const int kGmMinWidth  = 820;
static const int kGmMinHeight = 560;

// 标题栏高度（逻辑像素）—— 纤细透明，承载图标 / 标题文字与三按钮。
static const int kGmTitleBarHeight = 32;

// 客户区顶部 Header 高度（群头像 + 群名 + 角色副标题）。
static const int kGmHeaderHeight = 46;

// 左侧导航栏宽度。
static const int kGmNavWidth = 188;

// 右侧内容区滚动条宽度（逻辑像素）。
static const int kGmScrollBarWidth = 10;

// 标题栏左侧图标边长（逻辑像素）。
static const int kGmTitleIconSize = 16;

// 分隔线颜色。
static const COLORREF kGmSeparatorColor = RGB(0xEC, 0xEA, 0xE6);

// 标题栏文字 / 三按钮 glyph 颜色（深灰，配白色透明标题栏）。
static const COLORREF kGmCaptionColor = RGB(0x55, 0x55, 0x55);

// 标题栏背景（透明标题栏下穿透显示的 host 底色，即白）。
static const COLORREF kGmTitleBarBg = RGB(0xFF, 0xFF, 0xFF);

//注：标题栏图标的合成逻辑原先在本文件里，现已抽到 BalloonMain/TitleBarIcon.h
//共用（群公告弹窗、消息记录窗、新建群对话框都要用同一份），此处直接调用。

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonGroupManageWindow::CBalloonGroupManageWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_hHostPanel(NULL)
    , m_pContentScroll(NULL)
    , m_pNav(NULL)
    , m_currentPage(kPageMembers)
    //-1 表示"还没发过拉公告的请求"，此时任何应答都不认领
    , m_announceReqOffset(-1)
    , m_announceReqCount(-1)
    , m_groupId(0)
    , m_hTitleIcon(NULL)
{
    FillMockData(m_model);
}

void CBalloonGroupManageWindow::SetGroupContext(int64_t groupId, LPCTSTR name, int myRole)
{
    // 群角色取值（与 Protocol.h 的 group_member_role 对齐：1 管理员、2 群主）。
    const int kRoleAdminValue = 1;
    const int kRoleOwnerValue = 2;

    m_groupId = groupId;
    // 群号在开窗这一刻就已经知道了，先填上，不必等群资料应答（1303）到达 —— 应答
    // 到了 ApplyGroupInfo 会用同一个 groupid 再写一遍，值一样。这样即便应答迟到或
    // 失败，「基本信息」页的群号也是准的，排查问题时随时能看能复制。
    if (groupId > 0)
    {
        m_model.m_groupNumber.Format(_T("%lld"), static_cast<long long>(groupId));
    }
    if (name != NULL && name[0] != _T('\0'))
    {
        m_model.m_groupName = name;
    }
    if (myRole == kRoleOwnerValue)
    {
        m_model.m_myRole = balloongroupmanage::kRoleOwner;
    }
    else if (myRole == kRoleAdminValue)
    {
        m_model.m_myRole = balloongroupmanage::kRoleAdmin;
    }
    else
    {
        m_model.m_myRole = balloongroupmanage::kRoleMember;
    }
}

CBalloonGroupManageWindow::~CBalloonGroupManageWindow()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

// =======================================================================
// 创建 / 显示
// =======================================================================

bool CBalloonGroupManageWindow::BuildWindow(HWND hWndParent)
{
    if (Create(hWndParent, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("群管理窗口 Create 失败。"));
        return false;
    }

    SetTitle(_T("群管理"));
    SetTitleBarHeight(kGmTitleBarHeight);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kGmCaptionColor);
    SetCaptionGlyphColor(kGmCaptionColor);

    // 标题栏左侧图标：用程序主图标，合成到白底（标题栏背景）后挂上。
    m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kGmTitleIconSize, kGmTitleBarBg);
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }

    SetButtons(true, true, true);     // 最小化 / 最大化 / 关闭
    SetResizable(true);
    SetMinSize(kGmMinWidth, kGmMinHeight);

    InitUi();

    ResizeClient(kGmWindowWidth, kGmWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CBalloonGroupManageWindow::CreateAndShow(HWND hMainDlg)
{
    // 调试入口（CMainDlg --groupmanage）：无父窗口、非模态。
    m_hMainDlg = hMainDlg;
    if (!BuildWindow(NULL))
    {
        return;
    }
    LOG_INFO(_T("群管理窗口已创建并显示。"));
}

// =======================================================================
// 客户区
// =======================================================================

int CBalloonGroupManageWindow::ResizeBorderInsetPx() const
{
    // 窗口不可缩放时没有缩放边框，不必让路。
    if (!IsResizable())
    {
        return 0;
    }

    // 与 DuiFrameWindow::OnNcHitTestMsg 同一口径：边框宽度按 GetDpi()/96 缩放。
    // 布局矩形用的是物理像素，故这里也要缩放，否则高 DPI 下让出来的宽度不够、
    // 滚动条又被边框压住一截。
    int dpi = GetDpi();
    if (dpi <= 0)
    {
        dpi = 96;
    }
    return ::MulDiv(GetBorderPx(), dpi, 96);
}

void CBalloonGroupManageWindow::InitUi()
{
    typedef DuiLayout::Hint Hint;

    // 根：白底纵向容器。
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(RGB(0xFF, 0xFF, 0xFF));
    root->SetPadding(0, 0, 0, 0);
    root->SetGap(0);

    // Header。
    root->AddChild(BuildHeader(m_model), Hint().Fixed(kGmHeaderHeight));

    // Header 与正文之间的横向分隔线。
    std::unique_ptr<DuiSeparator> topSep(new DuiSeparator());
    topSep->SetOrientation(DuiSeparator::Horizontal);
    topSep->SetColor(kGmSeparatorColor);
    root->AddChild(std::move(topSep), Hint().Fixed(1));

    // 正文：左导航 + 竖分隔线 + 右滚动内容。
    std::unique_ptr<DuiHBox> body(new DuiHBox());
    body->SetGap(0);

    std::unique_ptr<DuiControl> nav = BuildNav(m_model);
    m_pNav = nav.get();
    body->AddChild(std::move(nav), Hint().Fixed(kGmNavWidth));

    std::unique_ptr<DuiSeparator> midSep(new DuiSeparator());
    midSep->SetOrientation(DuiSeparator::Vertical);
    midSep->SetColor(kGmSeparatorColor);
    body->AddChild(std::move(midSep), Hint().Fixed(1));

    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetAutoContentHeight(true);
    scroll->SetScrollBarWidth(kGmScrollBarWidth);
    scroll->SetContent(BuildPage(kPageMembers, m_model));
    m_pContentScroll = scroll.get();

    // 右边留出窗口缩放边框那条带子的宽度，别让滚动条压在上面。
    //
    // DuiFrameWindow 在 WM_NCCALCSIZE 里让"客户区 = 整个窗口矩形"（没有系统
    // 非客户区），又在 WM_NCHITTEST 里把最外侧 GetBorderPx() 个逻辑像素判成
    // 缩放边框。本窗口可缩放，滚动条若贴着客户区右缘，它 10 像素里外侧 8 像素
    // 就被缩放边框吃掉 —— 鼠标靠近先变成双向箭头，滑块根本抓不住。
    // 群聊窗的群文件面板没这毛病，是因为它外面那层容器本来就有 12 像素 padding。
    body->AddChild(std::move(scroll),
                   Hint().Weight(1).Margin(0, 0, ResizeBorderInsetPx(), 0));

    root->AddChild(std::move(body), Hint().Weight(1));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));

    // 初始页：成员管理。刷新导航选中态。
    m_currentPage = kPageMembers;
    UpdateNavActive(kPageMembers);

    LOG_INFO(_T("群管理窗客户区 UI 初始化完成。"));
}

void CBalloonGroupManageWindow::SwitchToPage(int pageId)
{
    if (pageId == m_currentPage)
    {
        return;
    }
    if (m_pContentScroll == NULL)
    {
        return;
    }

    m_currentPage = pageId;

    // 重建内容并交给滚动视口。
    m_pContentScroll->SetContent(BuildPage(pageId, m_model));
    m_pContentScroll->SetScrollPos(0);

    // 强制滚动视口按当前矩形重新布局（SetRect 即使 rc 不变也会重排），
    // 再重绘。
    const RECT rc = m_pContentScroll->GetRect();
    m_pContentScroll->SetRect(rc);
    m_pContentScroll->Invalidate();

    UpdateNavActive(pageId);
}

void CBalloonGroupManageWindow::UpdateNavActive(int pageId)
{
    if (m_pNav == NULL)
    {
        return;
    }
    const int pageIds[] =
    {
        kPageMembers, kPageApproval, kPageMute,
        kPageAnnouncement, kPageBasicInfo, kPagePerms, kPageDanger,
    };
    const int count = (int)(sizeof(pageIds) / sizeof(pageIds[0]));
    for (int i = 0; i < count; ++i)
    {
        GroupManageNavItem* item = dynamic_cast<GroupManageNavItem*>(
            m_pNav->FindCtrlById(pageIds[i]));
        if (item != NULL)
        {
            item->SetActive(pageIds[i] == pageId);
        }
    }
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonGroupManageWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 调试入口下，本窗口是唯一可见窗口，关闭即退出程序，交由 CMainDlg
    // 统一收尾。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonGroupManageWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 成员卡右键 → 弹该成员的操作菜单。卡片 ctrl id = kIdMemberCardBase + 成员下标，
    // 反算收在 MemberIndexFromCardId 里（纯函数，边界有单测）；右键点在别的控件上
    // 时它返回 -1，不会被误算成某个成员。
    if (pNotify->code == DUIN_RCLICK)
    {
        const int memberIndex =
            balloongroupmanage::MemberIndexFromCardId(pNotify->ctrlId);
        if (memberIndex >= 0)
        {
            ShowMemberContextMenu(memberIndex);
            return 0;
        }
    }

    // 左侧导航项点击 → 切页；其余点击交 HandlePageButton 认领。
    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kPageMembers:
        case kPageApproval:
        case kPageMute:
        case kPageAnnouncement:
        case kPageBasicInfo:
        case kPagePerms:
        case kPageDanger:
            SwitchToPage((int)pNotify->ctrlId);
            return 0;
        default:
            break;
        }

        if (HandlePageButton(pNotify->ctrlId, pNotify->extra))
        {
            return 0;
        }
    }

    // 开关 / 下拉框的值变化也走 HandlePageButton（全员禁言开关、群权限各项）。
    if (pNotify->code == DUIN_VALUECHANGED)
    {
        if (HandlePageButton(pNotify->ctrlId, pNotify->extra))
        {
            return 0;
        }
    }

    // 其余通知（标题栏按钮等）交回基类链。
    bHandled = FALSE;
    return 0;
}
