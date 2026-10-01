/**
 *  BalloonGroupManageWindow.cpp
 *
 *  新版群管理窗口的实现：组装 Header + 左导航 + 右滚动内容区，处理左侧
 *  导航点击切页。数据由 GroupManageWindowData.cpp 向服务端拉取；开窗时模型
 *  为空，各页先显示"正在加载"。调试入口 --groupmanage 经 LoadMockData 使用
 *  示例数据。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"
#include "BalloonGroupManageWindow.h"

#include <memory>

#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_ERROR
#include "GroupManageLayout.h"        // BuildHeader / BuildNav / BuildPage / 各 id / 窗口尺寸
#include "GroupManagePaint.h"         // GmDpi / GmScale：尺寸按 DPI 换算
#include "GroupManageModel.h"         // FillMockData（仅调试入口经 LoadMockData 使用）
#include "GroupManageMapping.h"       // MemberIndexFromCardId / ApplyGroupContext

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "GroupManageNavItem.h"

using namespace balloonwjui;
using namespace balloongroupmanage;

namespace {

// 整页重建或切页时要保留输入内容的输入框（控件编号见 GroupManageLayout.h）：
// 公告标题、公告正文、新增敏感词、群名称、群简介、成员页搜索框
const int kDraftInputIds[] =
{
    kIdEditAnnounceTitle, kIdEditAnnounceBody, kIdEditSensitiveNew, kIdEditGroupName, kIdEditIntro,
    kIdSearchMembers,
};
const size_t kDraftInputCount = sizeof(kDraftInputIds) / sizeof(kDraftInputIds[0]);

} // anonymous namespace

// ---- 文件作用域常量 -----------------------------------------------------

// 本文件的尺寸常量都按 96 DPI（100% 缩放）设计，使用前经 GmScale 换算成当前 DPI
// 下的物理像素（DuiFrameWindow 的标题栏高、最小尺寸都按物理像素使用，库内不换算）。
// 窗口初始尺寸与最小尺寸在 GroupManageLayout 的 ComputeInitialClientSize /
// ComputeMinClientSize 里，便于单元测试。

// 标题栏高度（96 DPI 下的像素）—— 纤细透明，承载图标 / 标题文字与三按钮。
static const int kGmTitleBarHeight = 32;

// 客户区顶部 Header 高度（群头像 + 群名 + 角色副标题）。
static const int kGmHeaderHeight = 46;

// 左侧导航栏宽度。
static const int kGmNavWidth = 188;

// 右侧内容区滚动条宽度（96 DPI 下的像素）。
static const int kGmScrollBarWidth = 10;

// 标题栏左侧图标边长（96 DPI 下的像素）。
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
    // 模型保持为空：成员与加群申请的加载状态默认为 kLoadPending，界面据此显示
    // "正在加载"。不能在这里填示例数据 —— 窗口显示出来之后请求才发出，示例数据
    // 会在应答到达前先被画出来，表现为打开窗口时先闪一下一个无关的群。
}

void CBalloonGroupManageWindow::LoadMockData()
{
    FillMockData(m_model);
}

void CBalloonGroupManageWindow::SetGroupContext(int64_t groupId, LPCTSTR name, int myRole)
{
    m_groupId = groupId;
    // 群号、群名、本人角色与占位头像的写入规则见 ApplyGroupContext（纯函数，有单测）。
    ApplyGroupContext(groupId, name, myRole, m_model);
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
        LOG_ERROR("Group manage window: Create failed.");
        return false;
    }

    SetTitle(_T("群管理"));
    SetTitleBarHeight(GmScale(kGmTitleBarHeight));
    SetTitleBarTransparent(true);
    SetTitleTextColor(kGmCaptionColor);
    SetCaptionGlyphColor(kGmCaptionColor);

    // 标题栏左侧图标：用程序主图标，合成到白底（标题栏背景）后挂上。
    m_hTitleIcon = balloonmain::CreateProgramIconBitmap(GmScale(kGmTitleIconSize), kGmTitleBarBg);
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }

    SetButtons(true, true, true);     // 最小化 / 最大化 / 关闭
    SetResizable(true);
    const SIZE minSize = ComputeMinClientSize(GmDpi());
    SetMinSize(minSize.cx, minSize.cy);

    InitUi();

    // 初始尺寸按 DPI 放大，但不超出窗口所在显示器的工作区。DuiFrameWindow 没有系统
    // 非客户区（客户区即整个窗口矩形），工作区可以直接交给尺寸计算。
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    RECT workArea = { 0, 0, 0, 0 };
    if (::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
    {
        workArea = mi.rcWork;
    }
    else
    {
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    }
    const SIZE initSize = ComputeInitialClientSize(GmDpi(), workArea);
    ResizeClient(initSize.cx, initSize.cy);
    LOG_INFO("Group manage window: dpi=%d, client=%dx%d, workArea=%dx%d.",
             GmDpi(), (int)initSize.cx, (int)initSize.cy,
             (int)(workArea.right - workArea.left), (int)(workArea.bottom - workArea.top));
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CBalloonGroupManageWindow::CreateAndShow(HWND hMainDlg)
{
    // 调试入口（命令行 --groupmanage）：无父窗口、非模态。
    m_hMainDlg = hMainDlg;
    if (!BuildWindow(NULL))
    {
        return;
    }
    LOG_INFO("Group manage window created and shown.");
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
    root->AddChild(BuildHeader(m_model), Hint().Fixed(GmScale(kGmHeaderHeight)));

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
    body->AddChild(std::move(nav), Hint().Fixed(GmScale(kGmNavWidth)));

    std::unique_ptr<DuiSeparator> midSep(new DuiSeparator());
    midSep->SetOrientation(DuiSeparator::Vertical);
    midSep->SetColor(kGmSeparatorColor);
    body->AddChild(std::move(midSep), Hint().Fixed(1));

    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetAutoContentHeight(true);
    scroll->SetScrollBarWidth(GmScale(kGmScrollBarWidth));
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

    // 页面建好后登记各输入框建页时的文字，有草稿的填回。成员管理页目前没有这类输入框，
    // 与 SwitchToPage 保持同一做法，日后初始页换成别的页也不会漏掉
    RestoreInputDrafts();

    // 每次整体重建都记一行：开窗时的第一行应为 members=0、membersState=0（未加载），
    // 据此可以确认首帧画的不是示例数据。状态取值见 balloongroupmanage::LoadState。
    LOG_INFO("Group manage window UI built: groupid=%lld, members=%d, membersState=%d, "
             "requests=%d, requestsState=%d.",
             (long long)m_groupId, m_model.MemberCount(), (int)m_model.m_membersState,
             m_model.PendingRequestCount(), (int)m_model.m_requestsState);
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

    // 旧页面上的输入框马上要随旧页面销毁，先把用户尚未提交的输入记下来
    CaptureInputDrafts();

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

    RestoreInputDrafts();
}

void CBalloonGroupManageWindow::OnMemberSearchChanged()
{
    DuiControl* content = GetClientContent();
    if (content == NULL || m_pContentScroll == NULL || m_currentPage != kPageMembers)
    {
        return;
    }
    DuiEdit* pSearch = dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdSearchMembers));
    if (pSearch == NULL)
    {
        return;
    }
    CString keyword = pSearch->GetText();
    keyword.Trim();
    //关键词没变（只多了首尾空白，或整页重建后填回草稿时发出的同值通知）就不必重建
    if (keyword == m_model.m_memberKeyword)
    {
        return;
    }
    m_model.m_memberKeyword = keyword;

    //只重建搜索栏以下的部分，搜索框不重建，输入焦点与光标保持原样
    DuiVBox* page = dynamic_cast<DuiVBox*>(m_pContentScroll->GetContent());
    if (page == NULL)
    {
        return;
    }
    RefilterMembersPage(page, m_model);

    //页面高度变了，让滚动视口按当前矩形重新排版以更新滚动范围，并回到顶部，让命中的成员
    //从第一行开始显示。必须用 ForceLayout：矩形没变时 SetRect 会直接返回、不重新排版
    m_pContentScroll->SetScrollPos(0);
    m_pContentScroll->ForceLayout(m_pContentScroll->GetRect());
    m_pContentScroll->Invalidate();

    LOG_INFO("Group manage window: member search keyword changed, chars=%d, groupid=%lld.",
             keyword.GetLength(), (long long)m_groupId);
}

void CBalloonGroupManageWindow::CaptureInputDrafts()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    for (size_t i = 0; i < kDraftInputCount; ++i)
    {
        DuiEdit* pEdit = dynamic_cast<DuiEdit*>(content->FindCtrlById(kDraftInputIds[i]));
        if (pEdit != NULL)
        {
            m_inputDrafts.OnInputLeaving(kDraftInputIds[i], std::wstring((LPCTSTR)pEdit->GetText()));
        }
    }
}

void CBalloonGroupManageWindow::RestoreInputDrafts()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    for (size_t i = 0; i < kDraftInputCount; ++i)
    {
        DuiEdit* pEdit = dynamic_cast<DuiEdit*>(content->FindCtrlById(kDraftInputIds[i]));
        if (pEdit == NULL)
        {
            continue;
        }
        std::wstring draft;
        if (m_inputDrafts.OnInputBuilt(kDraftInputIds[i], std::wstring((LPCTSTR)pEdit->GetText()),
                                       &draft))
        {
            pEdit->SetText(draft.c_str());
            LOG_INFO("Group manage window: restored input draft, ctrlId=%d, chars=%u.",
                     kDraftInputIds[i], (unsigned)draft.size());
        }
    }
}

bool CBalloonGroupManageWindow::IsDraftInput(int ctrlId)
{
    for (size_t i = 0; i < kDraftInputCount; ++i)
    {
        if (kDraftInputIds[i] == ctrlId)
        {
            return true;
        }
    }
    return false;
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
    // 调试入口下，本窗口是唯一可见窗口，关闭即请求
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
        //成员页搜索框的文字变了：本地过滤，不涉及协议，放在 HandlePageButton 之前 ——
        //后者在调试入口（群 id 为 0）下一律不响应
        if (pNotify->ctrlId == (UINT)kIdSearchMembers)
        {
            OnMemberSearchChanged();
            return 0;
        }
        if (HandlePageButton(pNotify->ctrlId, pNotify->extra))
        {
            return 0;
        }
    }

    // 其余通知（标题栏按钮等）交回基类链。
    bHandled = FALSE;
    return 0;
}
