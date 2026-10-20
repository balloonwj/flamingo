/**
 *  顶部浮动条的公共基类，ShareTopBar.cpp
 *  说明见 ShareTopBar.h。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"

#include "ShareTopBar.h"

#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace screenshare {

namespace {

//「隐藏」「收起 / 固定」两个按钮
const int kOwnButtonCount = 2;

} // namespace

CShareTopBar::CShareTopBar(UINT hideId, UINT toggleId)
    : m_hideId(hideId)
    , m_toggleId(toggleId)
    , m_topmost(false)
    , m_barWidth(0)
    , m_bgBitmap(NULL)
    , m_hideBtn(NULL)
    , m_toggleBtn(NULL)
{
    memset(&m_expandedRect, 0, sizeof(m_expandedRect));
    memset(&m_edgeArea, 0, sizeof(m_edgeArea));
}

CShareTopBar::~CShareTopBar()
{
    //子类在自己的析构里已销毁窗口；背景图在按钮与窗口都不再引用之后才释放
    if (m_bgBitmap != NULL)
    {
        ::DeleteObject(m_bgBitmap);
        m_bgBitmap = NULL;
    }
}

bool CShareTopBar::CreateTopBar(HWND owner, const wchar_t* title, DWORD style, DWORD exStyle)
{
    if (IsWindow())
        return true;
    if (Create(owner, CWindow::rcDefault, title, style, exStyle) == NULL)
    {
        LOG_ERROR("share top bar: create window failed, error=%lu.", ::GetLastError());
        return false;
    }
    SetTitle(title);
    SetTitleBarHeight(kShareTopBarTitleH);
    SetButtons(false, false, false);
    SetResizable(false);
    SetBorderPx(0);
    if (m_bgBitmap == NULL)
        m_bgBitmap = MakeShareBarBackgroundBitmap(kShareTopBarBgColor);
    if (m_bgBitmap != NULL)
    {
        RECT insets = { 0, 0, 0, 0 };
        SetBgImage(m_bgBitmap, insets);
        SetTitleBarTransparent(true);
        SetTitleTextColor(kShareTopBarTextColor);
    }
    return true;
}

void CShareTopBar::SetTopBarTopmost(bool topmost)
{
    m_topmost = topmost;
}

bool CShareTopBar::IsTopBarTopmost() const
{
    return m_topmost;
}

void CShareTopBar::SetTopBarTitles(const std::wstring& expanded, const std::wstring& handle)
{
    m_expandedTitle = expanded;
    m_handleTitle = handle;
    if (!IsWindow())
        return;
    //收起成小把手时写短标题；隐藏时窗口不可见，标题不用改
    const int state = m_collapse.State();
    if (state == top_bar_collapsed)
        SetTitle((std::wstring(kTopBarHandlePrefix) + m_handleTitle).c_str());
    else if (state != top_bar_hidden)
        SetTitle(m_expandedTitle.c_str());
}

void CShareTopBar::SetTopBarWidth(int width)
{
    m_barWidth = width;
}

void CShareTopBar::PlaceTopBar(const RECT& placeArea, const RECT& edgeArea)
{
    computeExpandedRect(placeArea, edgeArea);
    ApplyCollapse();
}

void CShareTopBar::MoveTopBar(const RECT& placeArea, const RECT& edgeArea)
{
    computeExpandedRect(placeArea, edgeArea);
    if (IsWindow())
        applyWindowRect();
}

void CShareTopBar::computeExpandedRect(const RECT& placeArea, const RECT& edgeArea)
{
    m_edgeArea = edgeArea;
    //整条宽不超过摆放区域的宽：放不下时由子类左侧的状态文字让出宽度（它以省略号结尾）
    int width = m_barWidth;
    const int areaW = placeArea.right - placeArea.left;
    if (areaW > 0 && width > areaW)
        width = areaW;
    const int x = placeArea.left + (areaW - width) / 2;
    const int y = placeArea.top + kShareTopBarTopMarginPx;
    m_expandedRect.left = x;
    m_expandedRect.top = y;
    m_expandedRect.right = x + width;
    m_expandedRect.bottom = y + kShareTopBarTitleH + kShareTopBarContentH;
}

void CShareTopBar::applyWindowRect()
{
    const int state = m_collapse.State();
    if (state == top_bar_hidden)
    {
        //隐藏：什么都不留，定时器照常检查鼠标是否推到了感应区域的顶边
        ShowWindow(SW_HIDE);
        return;
    }
    //收起时窗口缩成顶边正中的小把手，只剩标题条；其余状态显示整条
    const RECT rc = (state == top_bar_collapsed) ? ComputeTopBarHandleRect(m_expandedRect) : m_expandedRect;
    //置顶的条每次摆放都放回最上层；不置顶的条（有所有者，本来就在所有者之上）不改 Z 序
    const UINT posFlags = (m_topmost ? 0 : SWP_NOZORDER) | SWP_NOACTIVATE | SWP_SHOWWINDOW;
    const HWND insertAfter = m_topmost ? HWND_TOPMOST : NULL;
    ::SetWindowPos(m_hWnd, insertAfter, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, posFlags);
}

void CShareTopBar::applyToggleLook()
{
    //「收起 / 固定」：一直展开时画「收起」，临时展开与从隐藏临时显示时画「固定」
    const int state = m_collapse.State();
    const bool pinIcon = (state == top_bar_peek || state == top_bar_hidden_peek);
    m_art.Apply(m_toggleBtn, ShareBarLookFor(share_bar_collapse, pinIcon),
                ShareBarTooltipFor(share_bar_collapse, pinIcon, false));
}

void CShareTopBar::ApplyCollapse()
{
    if (!IsWindow())
        return;
    const int state = m_collapse.State();
    //收起成小把手时标题前加向下的箭头；隐藏时窗口不可见，标题不用改
    if (state == top_bar_collapsed)
        SetTitle((std::wstring(kTopBarHandlePrefix) + m_handleTitle).c_str());
    else if (state != top_bar_hidden)
        SetTitle(m_expandedTitle.c_str());
    applyWindowRect();
    applyToggleLook();
    //一直展开时不需要检查鼠标
    if (state == top_bar_pinned)
        KillTimer(kTopBarTimerId);
    else
        SetTimer(kTopBarTimerId, kTopBarTickMs, NULL);
}

void CShareTopBar::ResetCollapse()
{
    m_collapse.Reset();
}

bool CShareTopBar::HandleTopBarClick(UINT ctrlId)
{
    //「收起 / 固定」：本条自己处理
    if (ctrlId == m_toggleId)
    {
        if (m_collapse.OnToggleClicked())
            ApplyCollapse();
        return true;
    }
    //「隐藏」：同上
    if (ctrlId == m_hideId)
    {
        if (m_collapse.OnHideClicked())
            ApplyCollapse();
        return true;
    }
    return false;
}

std::unique_ptr<balloonwjui::DuiHBox> CShareTopBar::MakeTopBarRow() const
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(kShareTopBarRowPadLeft, 0, kShareTopBarRowPadRight, 0);
    row->SetGap(kShareBarBtnGap);
    return row;
}

std::unique_ptr<balloonwjui::DuiVBox> CShareTopBar::MakeTopBarRoot(std::unique_ptr<balloonwjui::DuiHBox> row) const
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kShareTopBarBgColor);
    root->SetPadding(0);
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Weight(1));
    return root;
}

balloonwjui::DuiButton* CShareTopBar::AddTopBarIconButton(balloonwjui::DuiHBox* row, UINT id)
{
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetCtrlId(id);
    balloonwjui::DuiButton* raw = btn.get();
    row->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint()
                                      .Fixed(kShareBarBtnPx)
                                      .Margin(0, kShareTopBarBtnVMargin, 0, kShareTopBarBtnVMargin));
    return raw;
}

void CShareTopBar::AddTopBarOwnButtons(balloonwjui::DuiHBox* row)
{
    //「隐藏」：工具条与把手都不显示，鼠标推到感应区域顶边时重新出现；在「收起 / 固定」左边
    m_hideBtn = AddTopBarIconButton(row, m_hideId);
    //「收起 / 固定」：最右端，收起后缩成顶部正中的小把手
    m_toggleBtn = AddTopBarIconButton(row, m_toggleId);
}

void CShareTopBar::ApplyOwnButtonLooks()
{
    m_art.Apply(m_hideBtn, ShareBarLookFor(share_bar_hide, false), ShareBarTooltipFor(share_bar_hide, false, false));
    applyToggleLook();
}

int CShareTopBar::OwnButtonsWidth()
{
    return kOwnButtonCount * (kShareBarBtnPx + kShareBarBtnGap);
}

void CShareTopBar::StopTopBar()
{
    m_hideBtn = NULL;
    m_toggleBtn = NULL;
    m_collapse.Reset();
    if (IsWindow())
        KillTimer(kTopBarTimerId);
}

ShareBarButtonArtCache& CShareTopBar::Art()
{
    return m_art;
}

const RECT& CShareTopBar::ExpandedRect() const
{
    return m_expandedRect;
}

int CShareTopBar::CollapseState() const
{
    return m_collapse.State();
}

int CShareTopBar::BarWidthForTest() const
{
    return m_barWidth;
}

void CShareTopBar::TickCollapseAt(int64_t nowMs, POINT cursor)
{
    if (!IsWindow())
        return;
    //窗口矩形在收起时就是小把手、显示整条时就是工具条，鼠标在不在上面用同一个判断；隐藏时窗口不可见，不算在上面
    RECT rc = { 0, 0, 0, 0 };
    GetWindowRect(&rc);
    const bool inside = IsWindowVisible() && ::PtInRect(&rc, cursor) != FALSE;
    const RECT edge = ComputeHiddenEdgeZone(m_expandedRect, m_edgeArea);
    const bool overEdge = ::PtInRect(&edge, cursor) != FALSE;
    if (m_collapse.OnTick(nowMs, inside, inside, overEdge))
        ApplyCollapse();
}

LRESULT CShareTopBar::OnTopBarTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kTopBarTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    TickCollapseAt(static_cast<int64_t>(::GetTickCount64()), pt);
    return 0;
}

} // namespace screenshare
