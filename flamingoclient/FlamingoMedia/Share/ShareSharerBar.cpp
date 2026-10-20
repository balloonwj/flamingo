/**
 *  多人共享的共享者控制条与主持人退出选择框，见 ShareSharerBar.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareSharerBar.h"

#include <memory>
#include "ShareMessages.h"
#include "ShareSources.h"
#include "ShareBarButtonArt.h"   // ShareBarLookFor / ShareBarTooltipFor、kShareRevokeBtnMinW、kShareTopBar*：与主持人控制条共用
#include "ShareRoomPolicy.h"     // MakeShareControlledByText：被控制时的状态文字
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"   //「更多」菜单（2026-10-08）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"         // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText、MeasureUiTextWidth：按文字宽度定按钮与状态文字的宽

namespace screenshare {

namespace {

//状态文字最少占的宽（像素）；实际按文字测量（被控制时含控制者的名字，简体中文界面也要量），
//整条超出工作区时由基类收窄，状态文字以省略号结尾
const int kSharerTipMinW = 120;
//状态文字量出的宽度之外多留的像素，避免取整误差让 DT_END_ELLIPSIS 把最后一个字换成省略号
const int kSharerTipSlackPx = 4;
//「标注」「更多」「停止共享」三个总有的图标按钮；「屏幕当白板」提供时再加一个
const int kSharerFixedIconButtons = 3;
//「收回控制」按钮上下留白（像素）：与主持人控制条的文字按钮一致，比图标按钮的留白小一点
const int kSharerRevokeVMargin = 8;
//退出选择框：宽、标题栏高、内边距、行间距、说明文字高、按钮高、底部两个按钮的宽
const int kDlgW = 380;
const int kDlgTitleH = 36;
const int kDlgPad = 16;
const int kDlgGap = 8;
const int kDlgTextH = 44;
const int kDlgBtnH = 32;
const int kDlgBottomBtnW = 96;
//共享者控制条状态点的颜色：蓝色，与主持人控制条的绿色状态点区分
const COLORREF kDotColor = RGB(30, 136, 229);

//某块显示器（或主显示器）的工作区
RECT workAreaOf(const RECT& anchor)
{
    HMONITOR mon = ::MonitorFromRect(&anchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

std::unique_ptr<balloonwjui::DuiButton> makeButton(const wchar_t* text, UINT id,
                                                   balloonwjui::DuiButton::Variant variant)
{
    std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
    b->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    b->SetVariant(variant);
    b->SetText(text);
    b->SetCtrlId(id);
    return b;
}

} // namespace

//==================== CShareSharerBar ====================

CShareSharerBar::CShareSharerBar()
    : CShareTopBar(kIdHide, kIdToggle)
    , m_notifyWnd(NULL)
    , m_annotButton(NULL)
    , m_screenBoardButton(NULL)
    , m_moreButton(NULL)
    , m_stopButton(NULL)
    , m_annotating(false)
    , m_screenBoardAvailable(false)
    , m_screenBoardActive(false)
    , m_tipLabel(NULL)
{
    memset(&m_anchor, 0, sizeof(m_anchor));
}

CShareSharerBar::~CShareSharerBar()
{
    Hide();
    //「更多」菜单期间推迟的销毁还没执行（对象先于那条消息析构）：在这里销毁，基类析构时窗口已不再引用背景图
    if (IsWindow())
        DestroyWindow();
}

void CShareSharerBar::Show(const RECT& anchor, HWND notifyWnd)
{
    m_notifyWnd = notifyWnd;
    m_anchor = anchor;
    if (IsWindow())
    {
        //「更多」菜单弹出期间被隐藏、等待销毁的窗口：直接复用并取消推迟的销毁（否则 m_hWnd 不为空，再 Create
        //会触发 atlwin.h 的断言），内容按当前状态重建
        if (m_popup.TakePendingDestroy())
        {
            LOG_INFO("screenshare: sharer bar reused while its menu is closing.");
            BuildUi();
        }
    }
    else
    {
        //没有所有者的置顶小窗：不抢焦点、不进任务栏，每次摆放都放回最上层
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        if (!CreateTopBar(NULL, TR(L"屏幕共享"), WS_POPUP, exStyle))
        {
            LOG_ERROR("screenshare: create sharer bar failed.");
            return;
        }
        SetTopBarTopmost(true);
        //新建的条从一直展开开始；之后重复调用 Show 只重新摆位，不改收起状态
        ResetCollapse();
        BuildUi();
        //控制条浮在被共享的内容上，默认不能被采进共享画面；「允许截图」打开时（2026-10-08）允许
        ApplyBarCaptureState(m_hWnd);
    }
    placeAtAnchor();
}

void CShareSharerBar::placeAtAnchor()
{
    HMONITOR mon = ::MonitorFromRect(&m_anchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    RECT monitor;
    if (::GetMonitorInfo(mon, &mi))
    {
        work = mi.rcWork;
        monitor = mi.rcMonitor;
    }
    else
    {
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        monitor = work;
    }
    //摆在工作区顶部居中，隐藏后的感应区取整块显示器的顶边（与主持人控制条一致）
    PlaceTopBar(work, monitor);
}

void CShareSharerBar::Hide()
{
    m_annotButton = NULL;
    m_screenBoardButton = NULL;
    m_moreButton = NULL;
    m_stopButton = NULL;
    m_tipLabel = NULL;
    m_annotating = false;
    //停止共享之后不会再被控制；下一次共享从没人控制开始
    m_controllerName.clear();
    //下一次共享由控制器按共享内容重新设置
    m_screenBoardAvailable = false;
    m_screenBoardActive = false;
    //停掉收起检查、回到一直展开（基类）
    StopTopBar();
    if (!IsWindow())
        return;
    //「更多」菜单正在弹出：菜单的嵌套消息循环还在本窗口的消息处理里，此时销毁窗口，ATL 要等那条消息返回才清 m_hWnd，
    //菜单也不会随之关闭。改为先关菜单、只隐藏窗口，等 TrackPopup 返回后由 OnShowMoreMenu 销毁
    if (ShareBarShouldDeferDestroy(m_popup.IsActive()))
    {
        m_popup.Close();
        m_popup.SetPendingDestroy();
        ShowWindow(SW_HIDE);
        LOG_INFO("screenshare: sharer bar hidden while its menu is open, destroy deferred.");
        return;
    }
    DestroyWindow();
}

bool CShareSharerBar::GetMoreMenuAnchor(POINT& pt) const
{
    if (m_moreButton == NULL || !IsWindow())
        return false;
    const RECT& rc = m_moreButton->GetRect();
    POINT corner = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &corner);
    pt = corner;
    return true;
}

LRESULT CShareSharerBar::OnShowMoreMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //已经有菜单开着、窗口已隐藏（等待销毁、已停止共享或已隐藏工具条）：不再弹
    POINT pt = { 0, 0 };
    if (m_popup.IsActive() || m_popup.IsPendingDestroy() || !IsWindowVisible() || !GetMoreMenuAnchor(pt))
        return 0;
    balloonwjui::DuiMenu menu;
    //允许截图：本机的工具条与控制条能否被截图，只对这一场共享有效
    menu.AppendChecked(kMenuAllowCapture, TR(L"允许截图"), IsBarCaptureAllowed());
    //菜单弹在「更多」按钮下方
    const std::vector<std::unique_ptr<balloonwjui::DuiMenu> > noSubMenus;
    m_popup.BeginMenu(&menu, noSubMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    m_popup.End();
    //菜单期间控制条被隐藏（停止了共享）：现在才销毁窗口，丢弃这次选择（本条消息是投递来的，返回后 ATL 即清空 m_hWnd）
    if (m_popup.TakePendingDestroy())
    {
        LOG_INFO("screenshare: sharer bar destroyed after its menu closed.");
        DestroyWindow();
        return 0;
    }
    if (!ShareBarShouldDeliverChoice(chosen, m_popup.WasInterrupted(), m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return 0;
    if (chosen == kMenuAllowCapture)
        ::PostMessage(m_notifyWnd, kMsgShareSharerCommand, share_sharer_allow_capture, IsBarCaptureAllowed() ? 0 : 1);
    return 0;
}

void CShareSharerBar::SetScreenBoard(bool available, bool active)
{
    if (available == m_screenBoardAvailable && active == m_screenBoardActive)
        return;
    const bool buttonChanged = (available != m_screenBoardAvailable);
    m_screenBoardAvailable = available;
    m_screenBoardActive = active;
    if (!IsWindow())
        return;
    //按钮出现或消失：重建内容并按新的宽度重新摆位（命令经 PostMessage 交给控制器，这里不在按钮的点击通知里）；
    //只是进入或退出屏幕白板：换外观与悬停提示即可
    if (buttonChanged)
    {
        BuildUi();
        placeAtAnchor();
    }
    else
    {
        refreshLooks();
    }
}

void CShareSharerBar::SetControlledBy(const std::wstring& controllerName)
{
    if (controllerName == m_controllerName)
        return;
    m_controllerName = controllerName;
    LOG_INFO("screenshare: sharer bar controlled state %d.", m_controllerName.empty() ? 0 : 1);
    //按钮出现或消失：重建内容并按新的宽度重新摆位（由控制器在处理 1725 或收回命令时调用，不在按钮的点击通知里）
    if (IsWindow())
    {
        BuildUi();
        placeAtAnchor();
    }
}

std::wstring CShareSharerBar::TipTextForTest() const
{
    if (m_tipLabel == NULL)
        return std::wstring();
    return std::wstring(m_tipLabel->GetText().GetString());
}

RECT CShareSharerBar::TipRectForTest() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (m_tipLabel != NULL)
        rc = m_tipLabel->GetRect();
    return rc;
}

void CShareSharerBar::SetAnnotating(bool on)
{
    m_annotating = on;
    refreshLooks();
}

void CShareSharerBar::refreshLooks()
{
    //标注：标注中蓝底，提示「退出标注」
    if (m_annotButton != NULL)
    {
        Art().Apply(m_annotButton, ShareBarLookFor(share_bar_annotate, m_annotating),
                    ShareBarTooltipFor(share_bar_annotate, m_annotating, false));
    }
    //屏幕当白板：图标与观看者的「新建白板」相同，提示写本条自己的说法（在屏幕白板里时蓝底、提示「退出屏幕白板」）
    if (m_screenBoardButton != NULL)
    {
        Art().Apply(m_screenBoardButton, ShareBarLookFor(share_bar_board, m_screenBoardActive),
                    m_screenBoardActive ? TR(L"退出屏幕白板") : TR(L"屏幕当白板"));
    }
    if (m_moreButton != NULL)
        Art().Apply(m_moreButton, ShareBarLookFor(share_bar_more, false), ShareBarTooltipFor(share_bar_more, false, false));
    //停止共享：红底方块；停的只是自己的画面，提示写「停止共享」而不是主持人的「结束共享」
    if (m_stopButton != NULL)
        Art().Apply(m_stopButton, ShareBarLookFor(share_bar_stop_share, false), TR(L"停止共享"));
    ApplyOwnButtonLooks();
}

void CShareSharerBar::BuildUi()
{
    //状态文字：有人在控制本人屏幕（2026-10-12）时写在最前面，与主持人控制条一致
    std::wstring tip = TR(L"你正在共享屏幕");
    if (!m_controllerName.empty())
        tip = MakeShareControlledByText(m_controllerName) + L" · " + tip;
    //状态文字是运行期内容（含控制者的名字），按实际宽度量，不用「简体中文界面恒为设计宽度」的 DefaultButtonWidthForText
    int tipW = i18n::MeasureUiTextWidth(tip.c_str(), i18n::DefaultFontPixelHeight(), false) + kSharerTipSlackPx;
    if (tipW < kSharerTipMinW)
        tipW = kSharerTipMinW;
    const int revokeW = i18n::DefaultButtonWidthForText(TR(L"收回控制"), kShareRevokeBtnMinW);
    const int iconButtons = m_screenBoardAvailable ? kSharerFixedIconButtons + 1 : kSharerFixedIconButtons;
    //整条宽：左右内边距、状态点、状态文字、「收回控制」（有时）、图标按钮，再加最右端的「隐藏」「收起 / 固定」；
    //横条里每个控件之前有一个 kShareBarBtnGap 的间距（第一个除外）
    int width = kShareTopBarRowPadLeft + kShareTopBarRowPadRight + kShareTopBarDotW + kShareBarBtnGap + tipW +
                iconButtons * (kShareBarBtnPx + kShareBarBtnGap) + OwnButtonsWidth();
    if (!m_controllerName.empty())
        width += revokeW + kShareBarBtnGap;
    SetTopBarWidth(width);
    //标题条：展开时写「屏幕共享」，收起成小把手时写「▼ 屏幕共享」
    SetTopBarTitles(TR(L"屏幕共享"), TR(L"屏幕共享"));

    std::unique_ptr<balloonwjui::DuiHBox> row = MakeTopBarRow();
    std::unique_ptr<balloonwjui::DuiLabel> dot(new balloonwjui::DuiLabel());
    dot->SetText(_T("●"));
    dot->SetTextColor(kDotColor);
    dot->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    row->AddChild(std::move(dot), balloonwjui::DuiLayout::Hint().Fixed(kShareTopBarDotW));
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetText(tip.c_str());
    label->SetTextColor(kShareTopBarTextColor);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_tipLabel = label.get();
    row->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Weight(1));
    //收回控制（2026-10-12）：只在有人控制本人屏幕时才有；被控制期间本人可以照常操作，只有点它（或控制者自己结束）才收回。
    //少见而要紧的操作，保留红色文字按钮
    if (!m_controllerName.empty())
    {
        row->AddChild(makeButton(TR(L"收回控制"), kIdRevokeControl, balloonwjui::DuiButton::Variant::Danger),
                      balloonwjui::DuiLayout::Hint().Fixed(revokeW).Margin(0, kSharerRevokeVMargin, 0,
                                                                           kSharerRevokeVMargin));
    }
    //图标按钮（外观与悬停提示由 refreshLooks 装上）：标注（第 22 期 22e）、屏幕当白板（2026-10-07，只共享一个窗口、
    //或已在屏幕白板里时才有）、更多（2026-10-08，目前只有「允许截图」一项）、停止共享
    m_annotButton = AddTopBarIconButton(row.get(), kIdAnnotate);
    m_screenBoardButton = m_screenBoardAvailable ? AddTopBarIconButton(row.get(), kIdScreenBoard) : NULL;
    m_moreButton = AddTopBarIconButton(row.get(), kIdMore);
    m_stopButton = AddTopBarIconButton(row.get(), kIdStop);
    //「隐藏」与「收起 / 固定」：最右端，由基类加
    AddTopBarOwnButtons(row.get());
    SetClientContent(MakeTopBarRoot(std::move(row)));
    refreshLooks();
}

LRESULT CShareSharerBar::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    //「隐藏」「收起 / 固定」：基类处理，不发给控制器
    if (HandleTopBarClick(n->ctrlId))
        return 0;
    //停止共享
    if (n->ctrlId == static_cast<UINT>(kIdStop))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareSharerCommand, share_sharer_stop, 0);
        return 0;
    }
    //标注 / 退出标注（第 22 期 22e）
    if (n->ctrlId == static_cast<UINT>(kIdAnnotate))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareSharerCommand, share_sharer_annotate, 0);
        return 0;
    }
    //收回控制（2026-10-12）：交给控制器发 1735 收回
    if (n->ctrlId == static_cast<UINT>(kIdRevokeControl))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareSharerCommand, share_sharer_revoke_control, 0);
        return 0;
    }
    //更多（2026-10-08）：投给自己，等这次点击通知处理完再弹菜单
    if (n->ctrlId == static_cast<UINT>(kIdMore))
    {
        PostMessage(kMsgShowMoreMenu, 0, 0);
        return 0;
    }
    //屏幕当白板 / 退出屏幕白板（2026-10-07）：由控制器提示确认后切换共享内容
    if (n->ctrlId == static_cast<UINT>(kIdScreenBoard))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareSharerCommand, share_sharer_screen_board, 0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

//==================== CShareHostExitDialog ====================

CShareHostExitDialog::CShareHostExitDialog()
    : m_notifyWnd(NULL)
{
}

CShareHostExitDialog::~CShareHostExitDialog()
{
    Close();
}

void CShareHostExitDialog::Open(HWND anchor, HWND notifyWnd,
                                const std::vector<std::pair<int64_t, std::wstring> >& candidates)
{
    Close();
    m_notifyWnd = notifyWnd;
    m_candidates = candidates;
    if (Create(NULL, CWindow::rcDefault, TR(L"退出屏幕共享"), WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW) == NULL)
    {
        LOG_ERROR("screenshare: create host exit dialog failed.");
        return;
    }
    SetTitle(TR(L"退出屏幕共享"));
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetTitleBarHeight(kDlgTitleH);
    SetButtons(false, false, true);
    SetResizable(false);

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kDlgPad, kDlgPad, kDlgPad, kDlgPad);
    root->SetGap(kDlgGap);
    std::unique_ptr<balloonwjui::DuiLabel> text(new balloonwjui::DuiLabel());
    text->SetText(TR(L"还有其他人在共享屏幕。你可以把主持人移交给其中一位后离开，或结束整个共享。"));
    text->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
    root->AddChild(std::move(text), balloonwjui::DuiLayout::Hint().Fixed(kDlgTextH));
    //每个可以移交的人一个按钮
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        root->AddChild(makeButton(TRF(L"移交给 {0} 并离开", m_candidates[i].second).c_str(),
                                  static_cast<UINT>(kIdTransferBase + i), balloonwjui::DuiButton::Variant::Default),
                       balloonwjui::DuiLayout::Hint().Fixed(kDlgBtnH));
    }
    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->SetGap(kDlgGap);
    buttons->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
    buttons->AddChild(makeButton(TR(L"取消"), kIdCancel, balloonwjui::DuiButton::Variant::Default),
                      balloonwjui::DuiLayout::Hint().Fixed(kDlgBottomBtnW));
    buttons->AddChild(makeButton(TR(L"结束共享"), kIdEnd, balloonwjui::DuiButton::Variant::Danger),
                      balloonwjui::DuiLayout::Hint().Fixed(kDlgBottomBtnW));
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(kDlgBtnH));
    SetClientContent(std::move(root));

    //在 anchor 所在显示器的工作区居中；高度按按钮个数算
    const int h = kDlgTitleH + 2 * kDlgPad + kDlgTextH + kDlgGap +
                  static_cast<int>(m_candidates.size()) * (kDlgBtnH + kDlgGap) + kDlgBtnH;
    RECT anchorRect = { 0, 0, 0, 0 };
    if (anchor != NULL && ::IsWindow(anchor))
        ::GetWindowRect(anchor, &anchorRect);
    const RECT work = workAreaOf(anchorRect);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, work.left + ((work.right - work.left) - kDlgW) / 2,
                   work.top + ((work.bottom - work.top) - h) / 2, kDlgW, h, SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hWnd);
}

void CShareHostExitDialog::Close()
{
    m_notifyWnd = NULL;
    if (IsWindow())
        DestroyWindow();
}

void CShareHostExitDialog::finish(int command, int64_t userId)
{
    const HWND notify = m_notifyWnd;
    Close();
    if (notify != NULL && ::IsWindow(notify))
        ::PostMessage(notify, kMsgShareSharerCommand, static_cast<WPARAM>(command), static_cast<LPARAM>(userId));
}

LRESULT CShareHostExitDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    const int id = static_cast<int>(n->ctrlId);
    //移交给某人
    if (id >= kIdTransferBase && id < kIdTransferBase + static_cast<int>(m_candidates.size()))
    {
        finish(share_sharer_transfer, m_candidates[id - kIdTransferBase].first);
        return 0;
    }
    switch (id)
    {
    //结束整个共享
    case kIdEnd:
        finish(share_sharer_end_all, 0);
        return 0;
    //取消
    case kIdCancel:
        finish(share_sharer_cancel, 0);
        return 0;
    //其余控件（标题栏等）交给基类
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareHostExitDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //关窗等同取消
    finish(share_sharer_cancel, 0);
    return 0;
}

} // namespace screenshare
