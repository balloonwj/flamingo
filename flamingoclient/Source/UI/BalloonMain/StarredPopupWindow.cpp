/**
 *  「特别关注」上下线提示窗实现，StarredPopupWindow.cpp
 *  设计与约定见 StarredPopupWindow.h。
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"
#include "StarredPopupWindow.h"

#include <memory>

#include "TitleBarIcon.h"          // CreateProgramIconBitmap（标题栏左侧图标）
#include "CornerPopupPlacement.h"  // 贴工作区右下角的落点计算（与新公告提示窗共用同一份）
#include "../../Common/Log/IULog.h"              // LOG_INFO / LOG_ERROR
#include "../../Session/UserSessionData.h"    // FMG_MSG_OPEN_CHAT

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiListBox;

namespace {

// ---- 控件 id ----
const UINT kIdStarredList = 911;   // 上下线事件列表

// ---- 计时器 ----
const UINT_PTR kAutoCloseTimerId = 1;      // 自动关闭计时器 id（本窗口只有这一个计时器）
const UINT     kAutoCloseMs      = 6000;   // 无人理会时多久自己收起（毫秒）。太短来不及看清名字，太长挡着屏幕右下角

// ---- 尺寸（像素）----
const int kDlgWidth      = 300;   // 客户区宽。够放「某某某某某 已上线」一行而不省略号
const int kDlgTitleBarH  = 32;    // 标题栏高
const int kTitleIconSize = 16;    // 标题栏图标边长
const int kRootPad       = 12;    // 客户区四周内边距
const int kRowH          = 30;    // 每行行高

// ---- 配色 ----
const COLORREF kBgColor      = RGB(0xFF, 0xFF, 0xFF);   // 客户区白底
const COLORREF kRowText      = RGB(0x2A, 0x2A, 0x2A);   // 行文字
const COLORREF kRowHoverBg   = RGB(0xF2, 0xF6, 0xFD);   // 鼠标悬停行的底色（浅蓝，与列表选中同色系）
const COLORREF kRowSelBg     = RGB(0xE4, 0xEE, 0xFB);   // 选中行底色
const COLORREF kTitleBarBg   = RGB(0xFF, 0xFF, 0xFF);   // 标题栏底色

} // anonymous namespace

CStarredPopupWindow::CStarredPopupWindow()
    : m_panelHwnd(NULL)
    , m_built(false)
    , m_hovering(false)
    , m_list(NULL)
    , m_titleIconBmp(NULL)
{
}

CStarredPopupWindow::~CStarredPopupWindow()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
}

void CStarredPopupWindow::AddPresenceEvent(HWND ownerPanel, int64_t userid,
                                           LPCTSTR name, bool online)
{
    if (userid <= 0)
    {
        return;
    }
    m_panelHwnd = ownerPanel;

    //先把事件攒进来 —— 去重、封顶、「等 N 人」由行模型统一处理，这里不判。
    balloonmain::StarredPresenceEvent e;
    e.m_userid = userid;
    e.m_name   = (name != NULL) ? name : _T("");
    e.m_online = online;
    m_events.push_back(e);

    if (!IsWindow())
    {
        //owner 传 NULL：让子控件通知自路由回本窗口（与新公告提示窗同处理）。
        if (Create(NULL, CWindow::rcDefault, _T("特别关注"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("特别关注提示窗 Create 失败。"));
            return;
        }
        SetTitle(_T("特别关注"));
        SetTitleBarHeight(kDlgTitleBarH);
        SetButtons(false, false, true);   //只留关闭按钮
        SetResizable(false);

        if (m_titleIconBmp == NULL)
        {
            m_titleIconBmp = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
        }
        if (m_titleIconBmp != NULL)
        {
            SetIcon(m_titleIconBmp);
        }

        BuildUiOnce();
    }

    //重排内容并把窗口高度调成刚好装下这些行。
    RefreshRows();

    //每次弹出都重新贴一次右下角：显示器分辨率 / 任务栏位置可能已经变了。
    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd,
                                            balloonmain::kCornerPopupMarginX,
                                            balloonmain::kCornerPopupMarginY);

    ShowWindow(SW_SHOWNOACTIVATE);   //不抢前台焦点，别打断用户正在做的事
    //置顶，否则会被别的窗口盖住、等于没弹。
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Invalidate();

    //新事件到达一律重新计时：刚并进来的这条用户还没看过。
    RestartAutoCloseTimer();

    LOG_INFO("弹出特别关注提示: userid=%lld, online=%d, 当前共 %u 条。",
             (long long)userid, (int)online, (uint32_t)m_events.size());
}

void CStarredPopupWindow::HideAndClear()
{
    m_events.clear();
    if (!IsWindow())
    {
        return;
    }
    StopAutoCloseTimer();
    m_hovering = false;
    ShowWindow(SW_HIDE);
}

void CStarredPopupWindow::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(kRootPad, kRootPad, kRootPad, kRootPad);
    root->SetBgColor(kBgColor);

    //行列表用 balloonui 现成的 DuiListBox：悬停高亮、单击选中、内容多了自动出滚动条
    //这些它都自带，不必再手写一个自绘列表（开发约定文档 UI 约定第 2 条）。
    std::unique_ptr<DuiListBox> list(new DuiListBox());
    list->SetCtrlId(kIdStarredList);
    list->SetItemHeight(kRowH);
    list->SetBgColor(kBgColor);
    list->SetTextColor(kRowText);
    list->SetHoverBgColor(kRowHoverBg);
    list->SetSelBgColor(kRowSelBg);
    list->SetSelTextColor(kRowText);
    m_list = list.get();
    root->AddChild(std::unique_ptr<DuiControl>(list.release()), Hint().Weight(1));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    m_built = true;
}

void CStarredPopupWindow::RefreshRows()
{
    if (m_list == NULL)
    {
        return;
    }

    const std::vector<balloonmain::StarredPopupRow> rows =
        balloonmain::BuildStarredPopupRows(m_events, balloonmain::kStarredPopupMaxRows);

    m_list->DeleteAllItems();
    for (std::vector<balloonmain::StarredPopupRow>::size_type i = 0; i < rows.size(); ++i)
    {
        //每行的 lParam 存该行对应的 userid，点行时按它开单聊；汇总行存 0。
        m_list->AddItem(rows[i].m_text, (LPARAM)rows[i].m_userid);
    }
    //不预选任何一行：选中态在这里只是"鼠标点过谁"的痕迹，一进来就高亮一行会让人
    //以为那一行有什么特别。
    m_list->ClearSelection();

    //窗口高度按行数算，恰好装下 —— 只有一条提示时不该留一大片空白。
    const int clientH = kRootPad * 2 + (int)rows.size() * kRowH;
    SetMinSize(kDlgWidth, clientH + kDlgTitleBarH);
    ResizeClient(kDlgWidth, clientH);
}

void CStarredPopupWindow::RestartAutoCloseTimer()
{
    if (!IsWindow())
    {
        return;
    }
    //SetTimer 用同一个 id 重复调用即为"重新计时"，不必先 KillTimer。
    ::SetTimer(m_hWnd, kAutoCloseTimerId, kAutoCloseMs, NULL);
}

void CStarredPopupWindow::StopAutoCloseTimer()
{
    if (!IsWindow())
    {
        return;
    }
    ::KillTimer(m_hWnd, kAutoCloseTimerId);
}

LRESULT CStarredPopupWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //本窗口由主面板持有并复用，关闭只隐藏不销毁。顺带清空已攒的事件 —— 用户既然
    //亲手关掉了，这批提示就算看过了，下次上线不该把它们又端出来。
    HideAndClear();
    return 0;
}

LRESULT CStarredPopupWindow::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam,
                                        LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kAutoCloseTimerId)
    {
        bHandled = FALSE;   //不是本窗口的计时器，交回给基类
        return 0;
    }

    //鼠标正停在窗口上说明用户在看，这一轮不收 —— 等他移开后 OnMouseLeave 会重新计时。
    if (m_hovering)
    {
        return 0;
    }

    HideAndClear();
    return 0;
}

LRESULT CStarredPopupWindow::OnMouseMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& bHandled)
{
    if (!m_hovering)
    {
        m_hovering = true;
        //登记一次离开追踪，否则鼠标移出窗口时收不到 WM_MOUSELEAVE，计时会一直停着、
        //窗口永远不自己收起来。TrackMouseEvent 是一次性的，每次进入都要重登记。
        TRACKMOUSEEVENT tme;
        ::ZeroMemory(&tme, sizeof(tme));
        tme.cbSize    = sizeof(tme);
        tme.dwFlags   = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        ::TrackMouseEvent(&tme);

        //用户正在看，暂停自动关闭。
        StopAutoCloseTimer();
    }

    bHandled = FALSE;   //交回给基类，控件的悬停高亮还要靠它
    return 0;
}

LRESULT CStarredPopupWindow::OnMouseLeaveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    m_hovering = false;
    //鼠标移开：从头开始计时，而不是接着之前剩下的那点时间 —— 用户刚看过一眼，
    //给足一整轮时间再收。
    RestartAutoCloseTimer();

    bHandled = FALSE;   //交回给基类，控件的悬停高亮还要靠它清掉
    return 0;
}

LRESULT CStarredPopupWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM lParam, BOOL& bHandled)
{
    balloonwjui::DuiNotify* pNotify = (balloonwjui::DuiNotify*)lParam;
    //单击某一行会把选中行换成它，据此打开单聊。用 DUIN_VALUECHANGED 而不是等双击：
    //这是个一次性提示窗，用户点一下就该有反应。
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == kIdStarredList && m_list != NULL)
    {
        //DuiNotify::extra 声明为 LPARAM（64 位下 8 字节）；DUIN_VALUECHANGED 时列表控件
        //在这里回带的是选中行的下标，与 GetItemCount() 同为 int 值域，故收窄为 int 安全。
        const int idx = static_cast<int>(pNotify->extra);
        if (idx >= 0 && idx < m_list->GetItemCount())
        {
            const int64_t userid = (int64_t)m_list->GetItemParam(idx);
            //汇总行（「等 N 人」）没有对应的人，lParam 为 0，点它不开窗。
            if (userid > 0 && m_panelHwnd != NULL)
            {
                //请主面板打开单聊。用 PostMessage 而不是直接调：本处仍在控件的事件
                //回调栈上，而开窗会走一串建窗 / 布局，投递出去更稳妥。
                ::PostMessage(m_panelHwnd, FMG_MSG_OPEN_CHAT, (WPARAM)userid, 0);
                //点过就算看完了，收起窗口。
                HideAndClear();
            }
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}
