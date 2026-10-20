/**
 *  观看端的浮动工具条，ViewerFloatBar.cpp
 *  说明见 ViewerFloatBar.h。2026-10-14 起建窗、摆位与收起 / 隐藏由公共基类 CShareTopBar（ShareTopBar.h）负责。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"

#include "ViewerFloatBar.h"

#include <memory>
#include "ShareSources.h"   // SetWindowCaptureAllowed：排除在屏幕采集之外（2026-10-12）
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"         // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText：带文字的按钮按译文加宽
#include "../../Source/UI/BalloonMain/P2PBadge.h"    // MakeP2PBadgeControl：「P2P」徽章（2026-10-12）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace screenshare {

const int CViewerFloatBar::kTopMarginPx;

namespace {

//状态文字区的宽（像素）：「仅查看」「控制中」之类的短文字放得下，译文较长时按文字加宽
const int kStatusMinW = 72;
//状态文字量出的宽度之外再留的空白（像素）：标签不画边距，留一点免得文字贴着下一个控件
const int kStatusTextPadPx = 8;
//带文字按钮的最小宽（像素，与控制条的「N 人观看 ▾」一致）
const int kTextBtnMinW = 104;
//「N 人观看」按钮前人形图标的边长与图标后的间距（像素）
const int kPeopleIconPx = 16;
const int kPeopleIconGap = 4;

//状态区的宽（像素）：按当前界面字体量出文字的实际宽度，不小于 kStatusMinW；没有状态文字时为 0。
//不用 DefaultButtonWidthForText：它在简体中文界面下恒为设计宽度，较长的提示（如「对方已允许控制，现在可以操作对方的电脑」）
//会被截断（2026-10-12 实机联调发现）
int statusWidth(const std::wstring& text)
{
    if (text.empty())
        return 0;
    const int measured = i18n::MeasureUiTextWidth(text.c_str(), i18n::DefaultFontPixelHeight(), false) + kStatusTextPadPx;
    return measured > kStatusMinW ? measured : kStatusMinW;
}

//一个按钮的悬停提示：使用方给了就用，否则按种类与三态取默认文字
std::wstring tooltipOf(const ViewerBarButtonSpec& spec)
{
    if (!spec.m_tooltip.empty())
        return spec.m_tooltip;
    return ShareBarTooltipForTriState(spec.m_action, spec.m_state);
}

} // namespace

ViewerBarButtonSpec::ViewerBarButtonSpec()
    : m_id(0)
    , m_action(0)
    , m_state(share_bar_tri_off)
{
}

ViewerBarButtonSpec::ViewerBarButtonSpec(UINT id, int action, int state, const std::wstring& tooltip)
    : m_id(id)
    , m_action(action)
    , m_state(state)
    , m_tooltip(tooltip)
{
}

RECT ComputeViewerBarAnchor(const balloonwjui::DuiFrameWindow& viewer)
{
    RECT rc = { 0, 0, 0, 0 };
    if (!viewer.IsWindow())
        return rc;
    //全屏：以所在显示器为参照，工具条贴在显示器顶部
    if (viewer.IsFullscreen())
    {
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfo(::MonitorFromWindow(viewer.m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
            return mi.rcMonitor;
    }
    //窗口模式：标题栏以下的画面区域
    viewer.GetWindowRect(&rc);
    rc.top += viewer.GetTitleBarHeight();
    return rc;
}

CViewerFloatBar::CViewerFloatBar()
    : CShareTopBar(kIdHide, kIdToggle)
    , m_owner(NULL)
    , m_notifyWnd(NULL)
    , m_notifyMsg(0)
    , m_captureExcluded(false)
    , m_badgeVisible(false)
    , m_peopleIcon(NULL)
    , m_statusLabel(NULL)
{
    memset(&m_anchor, 0, sizeof(m_anchor));
}

CViewerFloatBar::~CViewerFloatBar()
{
    Hide();
    //人形图标：按钮随窗口销毁之后才释放
    if (m_peopleIcon != NULL)
    {
        ::DeleteObject(m_peopleIcon);
        m_peopleIcon = NULL;
    }
}

void CViewerFloatBar::ensureWindow()
{
    if (IsWindow())
        return;
    //以观看窗为所有者的 WS_POPUP 窗口：没有标题栏与系统菜单，也不受系统最小窗口尺寸约束，收起时能缩成小把手；
    //按钮通知发回本窗口（balloonui 只把 WS_CHILD 宿主的通知发给父窗口，2026-10-14 起）
    DWORD exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    //没有所有者的工具条（远程桌面被控端的顶部条）置顶
    if (IsTopBarTopmost())
        exStyle |= WS_EX_TOPMOST;
    if (!CreateTopBar(m_owner, m_caption.c_str(), WS_POPUP, exStyle))
        return;
    //观看窗排除在屏幕采集之外时工具条跟着排除（2026-10-12）
    if (m_captureExcluded)
        SetWindowCaptureAllowed(m_hWnd, false);
    SetTopBarTitles(m_caption, m_caption);
}

int CViewerFloatBar::buttonWidth(const ViewerBarButtonSpec& spec) const
{
    if (spec.m_text.empty())
        return kShareBarBtnPx;
    int w = i18n::DefaultButtonWidthForText(spec.m_text.c_str(), kTextBtnMinW);
    if (spec.m_action == share_bar_viewers)
        w += kPeopleIconPx + kPeopleIconGap;
    return w;
}

void CViewerFloatBar::buildUi()
{
    m_statusLabel = NULL;
    //整条宽：内边距 + 状态区（有状态文字时）+ 各按钮 + 工具条自己的两个按钮，每个之间隔一个间距
    int width = kShareTopBarRowPadLeft + kShareTopBarRowPadRight;
    if (!m_status.empty())
        width += statusWidth(m_status) + kShareBarBtnGap;
    if (m_badgeVisible)
        width += balloonmain::kP2PBadgeControlWidth + kShareBarBtnGap;
    for (size_t i = 0; i < m_buttons.size(); ++i)
        width += buttonWidth(m_buttons[i]) + kShareBarBtnGap;
    width += OwnButtonsWidth() - kShareBarBtnGap;
    SetTopBarWidth(width);

    std::unique_ptr<balloonwjui::DuiHBox> row = MakeTopBarRow();

    //状态文字：有时才放，占去按钮以外的宽度，放不下时以省略号结尾
    if (!m_status.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> status(new balloonwjui::DuiLabel());
        status->SetText(m_status.c_str());
        status->SetTextColor(kShareTopBarTextColor);
        status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_statusLabel = status.get();
        row->AddChild(std::move(status), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    //「P2P」徽章（2026-10-12）：画面经直连链路传输时放在状态文字后面
    if (m_badgeVisible)
    {
        std::unique_ptr<balloonwjui::DuiBadge> badge = balloonmain::MakeP2PBadgeControl();
        row->AddChild(std::move(badge), balloonwjui::DuiLayout::Hint()
                                            .Fixed(balloonmain::kP2PBadgeControlWidth,
                                                   balloonmain::kP2PBadgeControlHeight)
                                            .AlignC(balloonwjui::DuiLayout::AlignCenter));
    }

    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        const ViewerBarButtonSpec& spec = m_buttons[i];
        std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
        btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        btn->SetCtrlId(spec.m_id);
        //「N 人观看 ▾」：文字前加人形图标
        if (spec.m_action == share_bar_viewers && !spec.m_text.empty())
        {
            if (m_peopleIcon == NULL)
                m_peopleIcon = MakeShareBarIconBitmap(share_bar_icon_people, kPeopleIconPx, kShareTopBarTextColor);
            btn->SetLeadingIcon(m_peopleIcon);
            btn->SetLeadingIconSize(kPeopleIconPx);
            btn->SetLeadingIconGap(kPeopleIconGap);
        }
        row->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint()
                                          .Fixed(buttonWidth(spec))
                                          .Margin(0, kShareTopBarBtnVMargin, 0, kShareTopBarBtnVMargin));
    }

    //工具条自己的两个按钮：「隐藏」在「收起 / 固定」左边，与共享者的控制条一致
    AddTopBarOwnButtons(row.get());
    SetClientContent(MakeTopBarRoot(std::move(row)));
    applyButtons();
}

void CViewerFloatBar::applyButtons()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
        return;
    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        const ViewerBarButtonSpec& spec = m_buttons[i];
        balloonwjui::DuiButton* btn = dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(spec.m_id));
        if (btn == NULL)
            continue;
        //带文字的按钮保留文字（底图与图标按钮同一样式），图标按钮清空文字、原文字作悬停提示
        const bool keepText = !spec.m_text.empty();
        //三态按钮按状态取外观（申请中为橙底），其余按开关取
        Art().Apply(btn, ShareBarLookForTriState(spec.m_action, spec.m_state), tooltipOf(spec), keepText);
        if (keepText)
            btn->SetText(spec.m_text.c_str());
    }
    ApplyOwnButtonLooks();
    if (m_statusLabel != NULL)
        m_statusLabel->SetText(m_status.c_str());
}

void CViewerFloatBar::Show(HWND owner, HWND notifyWnd, UINT notifyMsg, const RECT& anchor)
{
    //换了所有者：旧窗口的所有者关系无法改，销毁后按新所有者重建
    if (IsWindow() && owner != m_owner)
        Hide();
    m_owner = owner;
    m_notifyWnd = notifyWnd;
    m_notifyMsg = notifyMsg;
    m_anchor = anchor;
    ensureWindow();
    if (!IsWindow())
        return;
    ResetCollapse();
    buildUi();
    place();
    LOG_INFO("viewer float bar: shown, buttons=%u, width=%d.", static_cast<unsigned>(m_buttons.size()),
             BarWidthForTest());
}

void CViewerFloatBar::MoveTo(const RECT& anchor)
{
    m_anchor = anchor;
    //只是参照矩形变了（拖动、缩放观看窗时每秒可达上百次）：只挪窗口，标题、按钮外观与定时器都不变
    MoveTopBar(m_anchor, m_anchor);
}

void CViewerFloatBar::Hide()
{
    m_statusLabel = NULL;
    StopTopBar();
    if (!IsWindow())
        return;
    DestroyWindow();
}

void CViewerFloatBar::SetCaptureExcluded(bool excluded)
{
    if (excluded == m_captureExcluded)
        return;
    m_captureExcluded = excluded;
    if (IsWindow())
        SetWindowCaptureAllowed(m_hWnd, !excluded);
}

void CViewerFloatBar::SetTopmost(bool topmost)
{
    if (topmost == IsTopBarTopmost())
        return;
    SetTopBarTopmost(topmost);
    if (IsWindow())
        ::SetWindowPos(m_hWnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CViewerFloatBar::SetLinkBadgeVisible(bool visible)
{
    if (visible == m_badgeVisible)
        return;
    m_badgeVisible = visible;
    if (!IsWindow())
        return;
    //徽章出现或消失要改整条宽：重建并重新摆位
    buildUi();
    place();
}

bool CViewerFloatBar::IsLinkBadgeVisible() const
{
    return m_badgeVisible;
}

void CViewerFloatBar::SetCaptionText(const std::wstring& text)
{
    m_caption = text;
    SetTopBarTitles(m_caption, m_caption);
}

void CViewerFloatBar::SetStatusText(const std::wstring& text)
{
    if (text == m_status)
        return;
    //状态区出现、消失或所需宽度变了（如「仅查看」换成一句较长的提示，2026-10-12 联调发现提示被截断）：整条宽度变了
    const bool layoutChanged = (statusWidth(m_status) != statusWidth(text));
    m_status = text;
    if (!IsWindow())
        return;
    //整条宽度变了：重建并重新摆位
    if (layoutChanged)
    {
        buildUi();
        place();
        return;
    }
    if (m_statusLabel != NULL)
        m_statusLabel->SetText(m_status.c_str());
}

bool CViewerFloatBar::sameLayout(const std::vector<ViewerBarButtonSpec>& a, const std::vector<ViewerBarButtonSpec>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_id != b[i].m_id || a[i].m_text.empty() != b[i].m_text.empty() || a[i].m_action != b[i].m_action)
            return false;
    }
    return true;
}

void CViewerFloatBar::SetBarButtons(const std::vector<ViewerBarButtonSpec>& buttons)
{
    const bool same = sameLayout(buttons, m_buttons);
    m_buttons = buttons;
    if (!IsWindow())
        return;
    if (same)
    {
        //只有状态、提示或文字变了：原地刷新。带文字的按钮文字变长时宽度不变，以省略号结尾
        applyButtons();
        return;
    }
    buildUi();
    place();
}

bool CViewerFloatBar::GetButtonScreenRect(UINT id, RECT& out) const
{
    if (!IsWindow() || !IsWindowVisible())
        return false;
    balloonwjui::DuiControl* content = const_cast<CViewerFloatBar*>(this)->GetClientContent();
    if (content == NULL)
        return false;
    balloonwjui::DuiControl* ctrl = content->FindCtrlById(id);
    if (ctrl == NULL)
        return false;
    out = ctrl->GetRect();
    POINT tl = { out.left, out.top };
    POINT br = { out.right, out.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    out.left = tl.x;
    out.top = tl.y;
    out.right = br.x;
    out.bottom = br.y;
    return true;
}

std::wstring CViewerFloatBar::StatusTextForTest() const
{
    if (m_statusLabel == NULL)
        return std::wstring();
    return std::wstring(m_statusLabel->GetText().GetString());
}

void CViewerFloatBar::place()
{
    //参照矩形既是摆放区域，也是隐藏后的感应区域：窗口模式是画面区域，全屏时是显示器
    PlaceTopBar(m_anchor, m_anchor);
}

LRESULT CViewerFloatBar::OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //不激活本窗口；鼠标消息照常发给工具条，按钮照常响应
    return MA_NOACTIVATE;
}

LRESULT CViewerFloatBar::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    //「隐藏」「收起 / 固定」：本条自己处理（基类），不发给使用方
    if (HandleTopBarClick(n->ctrlId))
        return 0;
    //使用方的按钮：投给通知窗口，由它在这次点击通知处理完之后再执行（可能重建本条的按钮）
    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        if (m_buttons[i].m_id != n->ctrlId)
            continue;
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, m_notifyMsg, static_cast<WPARAM>(n->ctrlId), 0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenshare
