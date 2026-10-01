/**
 *  TrayMsgTipPopup.cpp —— 托盘悬停未读消息提示浮窗实现。
 *
 *  窗口创建 / 控件树构建 / 行渲染 / 落点夹取 / 自动隐藏轮询，整体套路对照
 *  CSearchDropdown（主面板搜索结果下拉），区别仅在于：无筛选标签栏、数据来自托盘
 *  闪烁集合、锚点在托盘图标上方。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"
#include "TrayMsgTipPopup.h"

#include <memory>

#include "MainPanelListItem.h"     // 会话行（彩色字母头像 + 名 + 副行 + 悬停 + 点击）
#include "PopupMenuPlacement.h"    // ClampMenuOriginToRect / GetMenuPlacementArea（落点夹取）
#include "../../Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox / Hint
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

// ---- 文件作用域常量 -----------------------------------------------------

static const int kPopupWidth     = 248;  // 浮窗宽（逻辑像素）
static const int kRowHeight      = 52;    // 单条会话行高
static const int kRowStride      = kRowHeight + 1;   // 行高 + 内容 vbox gap(1)
static const int kMaxVisibleRows = 6;     // 超过则滚动
static const int kCardPadV       = 6;     // 卡片上下留白合计的一半（上下各 6）

// 卡片底色 / 描边色 / 行悬停底色。
static const COLORREF kCardBg      = RGB(0xFF, 0xFF, 0xFF);
static const COLORREF kCardBorder  = RGB(0xDD, 0xDD, 0xDD);
static const COLORREF kRowHoverFill = RGB(0xE9, 0xEC, 0xF1);

// 自动隐藏轮询定时器 id 与间隔（毫秒）。与旧 CMsgTipDlg 的 leave 轮询同思路。
static const UINT kHideTimerId       = 991;
static const UINT kHidePollIntervalMs = 160;

// ---- 构造 / 析构 --------------------------------------------------------

CTrayMsgTipPopup::CTrayMsgTipPopup()
    : m_built(false)
    , m_pickCb(NULL)
    , m_pickUd(NULL)
    , m_scroll(NULL)
    , m_hideTimerId(0)
{
    ::SetRectEmpty(&m_trayIconRect);
}

CTrayMsgTipPopup::~CTrayMsgTipPopup()
{
    // 控件树由 DuiHost 基类析构释放；本类只持借用指针。
}

void CTrayMsgTipPopup::SetPickCallback(PickConvCallback cb, void* userdata)
{
    m_pickCb = cb;
    m_pickUd = userdata;
}

// ---- 窗口类注册 / 创建 --------------------------------------------------

// 浮窗窗口类名（带 CS_DROPSHADOW 投影；DuiHost 的 __DuiHost__ 类无投影，故另注册）。
static const TCHAR* kTrayTipClassName = _T("__FlamingoTrayMsgTip__");

// 注册浮窗窗口类（幂等）。CS_DROPSHADOW 系统投影 + CS_SAVEBITS 弹出时存底图。
// 窗口过程用 DefWindowProc 占位，真正处理由随后的 SubclassWindow 接管。
static void EnsureTrayTipClassRegistered()
{
    static bool s_registered = false;
    if (s_registered)
    {
        return;
    }
    WNDCLASSEX wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DROPSHADOW | CS_SAVEBITS;
    wc.lpfnWndProc   = ::DefWindowProc;
    wc.hInstance     = ::GetModuleHandle(NULL);
    wc.hCursor       = ::LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kTrayTipClassName;
    ::RegisterClassEx(&wc);
    s_registered = true;
}

bool CTrayMsgTipPopup::EnsureCreated()
{
    if (IsWindow())
    {
        return true;   // 已创建
    }

    EnsureTrayTipClassRegistered();

    // 顶层无边框浮窗：WS_POPUP；WS_EX_TOOLWINDOW 不进任务栏；WS_EX_NOACTIVATE 弹出 /
    // 点击不抢前台与焦点。owner 传 NULL —— 让 DuiHost::SendNotify 的 GetParent 为空、
    // 通知自路由回本窗口。初始 0 尺寸、不可见，ShowForEntries 时定位。
    HWND hWnd = ::CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kTrayTipClassName, NULL,
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, ::GetModuleHandle(NULL), NULL);
    if (hWnd == NULL)
    {
        LOG_WARNING(_T("托盘消息提示浮窗创建失败。"));
        return false;
    }

    if (!SubclassWindow(hWnd))
    {
        ::DestroyWindow(hWnd);
        LOG_WARNING(_T("托盘消息提示浮窗 SubclassWindow 失败。"));
        return false;
    }

    BuildContentOnce();
    return true;
}

void CTrayMsgTipPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }

    // 根：白底卡片（1px 浅灰描边），内放结果滚动区。
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(0, kCardPadV, 0, kCardPadV);   // 上下留白（l,t,r,b）
    root->SetGap(0);
    root->SetBgColor(kCardBg);
    root->SetBorderColor(kCardBorder);
    root->SetBorderWidth(1.0f);

    std::unique_ptr<balloonwjui::DuiScrollView> scroll(new balloonwjui::DuiScrollView());
    scroll->SetAutoContentHeight(true);
    std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
    content->SetGap(1);
    scroll->SetContent(std::move(content));
    m_scroll = scroll.get();
    root->AddChild(std::move(scroll), balloonwjui::DuiLayout::Hint().Weight(1));

    SetRoot(std::move(root));
    m_built = true;
}

// ---- 展示 / 刷新 --------------------------------------------------------

int CTrayMsgTipPopup::RebuildRows(const std::vector<balloonmain::TrayFlashEntry>& entries)
{
    if (m_scroll == NULL)
    {
        return 0;
    }

    m_rowConv.clear();

    std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
    content->SetGap(1);

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const balloonmain::TrayFlashEntry& e = entries[i];

        std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
        item->SetAvatar(e.m_avatarText.c_str(), e.m_avatarColor);
        item->SetName(e.m_displayName.c_str());
        // 副行：最新一条消息的预览文字。过长由 MainPanelListItem 的 DT_END_ELLIPSIS
        // 自动省略号截断（"消息太长只显示部分内容"）。
        item->SetSubText(e.m_preview.c_str());
        // 未读条数：用右侧红色数字角标显示（与会话列表观感一致）。
        item->SetBadgeCount(e.m_unreadCount);
        item->SetHoverFill(kRowHoverFill);

        balloonmain::MainPanelListItem* raw = item.get();
        m_rowConv[raw] = std::make_pair(e.m_ctype, e.m_cid);

        content->AddChild(std::move(item), balloonwjui::DuiLayout::Hint().Fixed(kRowHeight));
    }

    m_scroll->SetContent(std::move(content));
    return (int)entries.size();
}

int CTrayMsgTipPopup::ComputeHeight(int rowCount) const
{
    int visible = rowCount;
    if (visible > kMaxVisibleRows)
    {
        visible = kMaxVisibleRows;
    }
    if (visible < 1)
    {
        visible = 1;
    }
    return visible * kRowStride + 2 * kCardPadV;
}

void CTrayMsgTipPopup::ShowForEntries(const std::vector<balloonmain::TrayFlashEntry>& entries,
                                      const RECT& trayIconScreenRect)
{
    if (!EnsureCreated())
    {
        return;
    }

    // 无未读：隐藏。
    if (entries.empty())
    {
        HidePopup();
        return;
    }

    m_trayIconRect = trayIconScreenRect;

    int rows = RebuildRows(entries);
    int w = kPopupWidth;
    int h = ComputeHeight(rows);

    // 落点：水平对准托盘图标正上方（以图标中心 x 居中），垂直紧挨任务栏上方弹出
    //（托盘在任务栏，须向上弹）。GetMenuPlacementArea 返回的是托盘所在显示器的工作区
    // （已排除任务栏并四周留一道缝），故把弹窗底边贴到 area.bottom 即"紧挨任务栏上方"，
    // 再把整体夹进 area 保证不越界（开发约定文档「UI 约定」第 3 条）。
    int cx = (trayIconScreenRect.left + trayIconScreenRect.right) / 2;   // 托盘图标中心 x

    POINT anchorForArea;
    anchorForArea.x = cx;
    anchorForArea.y = trayIconScreenRect.top;
    RECT area = balloonmain::GetMenuPlacementArea(anchorForArea);

    POINT pos;
    pos.x = cx - w / 2;                 // 水平居中对准托盘图标
    pos.y = area.bottom - h;            // 底边紧挨任务栏上方

    // 夹进工作区，保证整窗可见（弹窗比工作区还大时退化到贴左 / 上边缘）。
    if (pos.x + w > area.right)
        pos.x = area.right - w;
    if (pos.x < area.left)
        pos.x = area.left;
    if (pos.y + h > area.bottom)
        pos.y = area.bottom - h;
    if (pos.y < area.top)
        pos.y = area.top;

    // HWND_TOPMOST 浮在前台之上；SWP_NOACTIVATE 不抢激活。
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, pos.x, pos.y, w, h,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::InvalidateRect(m_hWnd, NULL, FALSE);

    // 起自动隐藏轮询定时器（幂等：已起则不重复）。
    if (m_hideTimerId == 0)
    {
        m_hideTimerId = (DWORD)::SetTimer(m_hWnd, kHideTimerId, kHidePollIntervalMs, NULL);
    }
}

void CTrayMsgTipPopup::HidePopup()
{
    if (m_hideTimerId != 0 && IsWindow())
    {
        ::KillTimer(m_hWnd, kHideTimerId);
        m_hideTimerId = 0;
    }
    if (m_hWnd != NULL && ::IsWindowVisible(m_hWnd))
    {
        ::ShowWindow(m_hWnd, SW_HIDE);
    }
}

bool CTrayMsgTipPopup::IsShown() const
{
    return m_hWnd != NULL && ::IsWindowVisible(m_hWnd) != FALSE;
}

// ---- 自动隐藏轮询 -------------------------------------------------------

LRESULT CTrayMsgTipPopup::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if ((UINT)wParam != kHideTimerId)
    {
        bHandled = FALSE;
        return 0;
    }

    // 光标既不在浮窗、也不在托盘图标矩形内 → 隐藏（移开就收起，且光标回到托盘上不收）。
    RECT rcWindow;
    ::GetWindowRect(m_hWnd, &rcWindow);

    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);

    if (!::PtInRect(&rcWindow, pt) && !::PtInRect(&m_trayIconRect, pt))
    {
        HidePopup();
    }
    return 0;
}

// ---- 通知处理（自路由：owner 为空，DuiHost::SendNotify 回到本窗口）-------

LRESULT CTrayMsgTipPopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* n =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->pCtrl == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 只处理单击；其余通知放行。
    if (n->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    balloonwjui::DuiControl* pCtrl = static_cast<balloonwjui::DuiControl*>(n->pCtrl);
    std::map<balloonwjui::DuiControl*, std::pair<int, long long> >::iterator it =
        m_rowConv.find(pCtrl);
    if (it != m_rowConv.end())
    {
        int       ctype = it->second.first;
        long long cid   = it->second.second;
        HidePopup();
        if (m_pickCb != NULL)
        {
            m_pickCb(m_pickUd, ctype, cid);
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}
