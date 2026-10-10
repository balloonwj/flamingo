/**
 *  主持人看其他共享者屏幕的窗口，见 ShareScreensWindow.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareScreensWindow.h"

#include <memory>
#include "ShareMessages.h"   // kMsgShareScreensCommand：顶部按钮交给控制器的命令（2026-10-07）
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//窗口：标题栏高、最小尺寸、占工作区的比例（千分比）
const int kTitleBarH = 36;
const int kMinW = 480;
const int kMinH = 320;
const int kWorkAreaPermille = 600;
const int kPermille = 1000;
//顶部按钮行（2026-10-07）：行高、四周留白、按钮之间的间距、两个按钮的宽度（像素）
const int kToolRowH = 40;
const int kToolRowPad = 6;
const int kToolRowGap = 8;
const int kAnnotBtnW = 96;
const int kControlBtnW = 96;
//按钮行右侧提示文字的颜色（灰）
const COLORREF kHintColor = RGB(120, 120, 120);
//按钮文字（与观看窗的同名按钮一致）
const wchar_t* const kAnnotText = TR_NOOP(L"标注");
const wchar_t* const kAnnotExitText = TR_NOOP(L"退出标注");
//「申请控制」按钮的文字，按 share_viewer_control_state 取
const wchar_t* const kControlTexts[] = { TR_NOOP(L"申请控制"), TR_NOOP(L"取消申请"), TR_NOOP(L"结束控制") };
const int kControlTextCount = 3;

} // namespace

CShareScreensWindow::CShareScreensWindow()
    : m_feed(NULL)
    , m_notifyWnd(NULL)
    , m_canvas(NULL)
    , m_annotBtn(NULL)
    , m_controlBtn(NULL)
    , m_annot(NULL)
    , m_annotating(false)
    , m_annotPointer(false)
    , m_annotInput(NULL)
    , m_controlState(share_viewer_control_none)
    , m_controlScreen(-1)
    , m_controlInput(NULL)
{
}

CShareScreensWindow::~CShareScreensWindow()
{
    Close();
}

void CShareScreensWindow::Open(HWND anchor, IShareScreenFeed* feed, HWND notifyWnd)
{
    m_feed = feed;
    m_notifyWnd = notifyWnd;
    if (IsWindow())
    {
        if (!IsWindowVisible())
            ShowWindow(SW_SHOWNOACTIVATE);
        return;
    }
    if (Create(NULL, CWindow::rcDefault, TR(L"其他人的屏幕"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("screenshare: create screens window failed.");
        return;
    }
    SetTitle(TR(L"其他人的屏幕"));
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    //没有所有者、会出现在任务栏：任务栏与 Alt-Tab 也用程序图标
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetTitleBarHeight(kTitleBarH);
    SetButtons(true, true, true);
    SetResizable(true);
    SetMinSize(kMinW, kMinH);
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    //顶部按钮行（2026-10-07）：标注、申请控制，右侧一行说明
    std::unique_ptr<balloonwjui::DuiHBox> tools(new balloonwjui::DuiHBox());
    tools->SetPadding(kToolRowPad, kToolRowPad, kToolRowPad, kToolRowPad);
    tools->SetGap(kToolRowGap);
    std::unique_ptr<balloonwjui::DuiButton> annotBtn(new balloonwjui::DuiButton());
    annotBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    annotBtn->SetVariant(balloonwjui::DuiButton::Variant::Default);
    annotBtn->SetCtrlId(kIdAnnotate);
    m_annotBtn = annotBtn.get();
    tools->AddChild(std::move(annotBtn), balloonwjui::DuiLayout::Hint().Fixed(kAnnotBtnW));
    std::unique_ptr<balloonwjui::DuiButton> controlBtn(new balloonwjui::DuiButton());
    controlBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    controlBtn->SetVariant(balloonwjui::DuiButton::Variant::Default);
    controlBtn->SetCtrlId(kIdControl);
    m_controlBtn = controlBtn.get();
    tools->AddChild(std::move(controlBtn), balloonwjui::DuiLayout::Hint().Fixed(kControlBtnW));
    std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
    hint->SetText(TR(L"双击一块画面放大；申请控制针对放大的那一块，没有放大时为第一块"));
    hint->SetTextColor(kHintColor);
    hint->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    tools->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(std::move(tools), balloonwjui::DuiLayout::Hint().Fixed(kToolRowH));
    std::unique_ptr<CShareVideoCanvas> canvas(new CShareVideoCanvas());
    canvas->SetScreens(m_seats, m_names);
    canvas->SetBoards(m_boardSeats, m_boardOwnerSeats);
    canvas->SetAnnotation(m_annot);
    canvas->SetAnnotating(m_annotating && !m_annotPointer, m_annotInput);
    m_canvas = canvas.get();
    root->AddChild(std::move(canvas), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
    refreshButtons();
    //在 anchor 所在显示器的工作区居中，约占六成
    const HWND monitorOf = (anchor != NULL && ::IsWindow(anchor)) ? anchor : NULL;
    HMONITOR mon = ::MonitorFromWindow(monitorOf, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    int w = (work.right - work.left) * kWorkAreaPermille / kPermille;
    int h = (work.bottom - work.top) * kWorkAreaPermille / kPermille;
    w = w < kMinW ? kMinW : w;
    h = h < kMinH ? kMinH : h;
    ::SetWindowPos(m_hWnd, NULL, work.left + ((work.right - work.left) - w) / 2,
                   work.top + ((work.bottom - work.top) - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(SW_SHOWNOACTIVATE);
    m_lastSerials.clear();
    OnFeedFrame();
    LOG_INFO("screenshare: screens window opened, screens=%u.", static_cast<unsigned>(m_seats.size()));
}

void CShareScreensWindow::Close()
{
    m_canvas = NULL;
    m_annotBtn = NULL;
    m_controlBtn = NULL;
    m_lastSerials.clear();
    //标注与控制属于这一次打开：控制器先结束控制、退出标注，这里只复位记录
    m_annotating = false;
    m_annotPointer = false;
    m_annotInput = NULL;
    m_controlState = share_viewer_control_none;
    m_controlScreen = -1;
    m_forwarder.Reset();
    if (IsWindow())
        DestroyWindow();
}

bool CShareScreensWindow::IsOpen() const
{
    return IsWindow() && IsWindowVisible();
}

void CShareScreensWindow::SetScreens(const std::vector<int>& seats, const std::vector<std::wstring>& names)
{
    m_seats = seats;
    m_names = names;
    if (m_canvas != NULL)
        m_canvas->SetScreens(seats, names);
    //已不在清单里的画面不再记帧序号
    std::map<int, uint64_t> kept;
    for (size_t i = 0; i < seats.size(); ++i)
    {
        std::map<int, uint64_t>::iterator it = m_lastSerials.find(seats[i]);
        if (it != m_lastSerials.end())
            kept.insert(*it);
    }
    m_lastSerials.swap(kept);
}

void CShareScreensWindow::OnFeedFrame()
{
    if (m_canvas == NULL || m_feed == NULL || !IsWindowVisible())
        return;
    for (size_t i = 0; i < m_seats.size(); ++i)
    {
        int w = 0;
        int h = 0;
        uint64_t serial = 0;
        if (!m_feed->GetScreenFrame(m_seats[i], m_scratch, w, h, serial))
            continue;
        uint64_t& last = m_lastSerials[m_seats[i]];
        if (serial == last)
            continue;
        last = serial;
        m_canvas->SwapScreenFrame(m_seats[i], m_scratch, w, h);
    }
}

void CShareScreensWindow::SetAnnotation(const ShareAnnotationSession* session)
{
    m_annot = session;
    if (m_canvas != NULL)
        m_canvas->SetAnnotation(session);
}

void CShareScreensWindow::SetBoards(const std::vector<int>& seats, const std::vector<int>& ownerSeats)
{
    m_boardSeats = seats;
    m_boardOwnerSeats = ownerSeats;
    if (m_canvas != NULL)
        m_canvas->SetBoards(seats, ownerSeats);
}

void CShareScreensWindow::AnnotationChanged()
{
    if (m_canvas != NULL && IsWindow() && IsWindowVisible())
        m_canvas->Invalidate();
}

void CShareScreensWindow::SetAnnotating(bool on, IShareAnnotationInput* input)
{
    m_annotating = on;
    m_annotInput = input;
    //进入或退出标注都从「画图」开始，不沿用上一次的「鼠标」状态
    m_annotPointer = false;
    if (m_canvas != NULL)
        m_canvas->SetAnnotating(on, input);
    refreshButtons();
    LOG_INFO("screenshare: screens window annotating %d.", on ? 1 : 0);
}

void CShareScreensWindow::SetAnnotPointer(bool on)
{
    m_annotPointer = on;
    if (m_canvas != NULL)
        m_canvas->SetAnnotating(m_annotating && !on, m_annotInput);
}

void CShareScreensWindow::SetControlState(int state)
{
    if (state == m_controlState)
        return;
    //离开控制：先补发还按着的按钮与按键的抬起（控制器决定还能不能发出去），再切换状态
    if (m_controlState == share_viewer_control_active && state != share_viewer_control_active)
        m_forwarder.Release(m_controlInput);
    m_controlState = state;
    m_forwarder.Reset();
    refreshButtons();
    LOG_INFO("screenshare: screens window control state %d.", state);
    //开始控制：键盘输入要落在本窗口上
    if (state == share_viewer_control_active && IsWindow())
    {
        if (!IsWindowVisible())
            ShowWindow(SW_SHOW);
        ::SetFocus(m_hWnd);
    }
}

void CShareScreensWindow::SetControlInput(IShareControlInput* input)
{
    m_controlInput = input;
}

void CShareScreensWindow::SetControlScreen(int seat)
{
    m_controlScreen = seat;
}

int CShareScreensWindow::ActiveScreenSeat() const
{
    return m_canvas != NULL ? m_canvas->ActiveScreenSeat() : -1;
}

bool CShareScreensWindow::ActiveIsBoard() const
{
    return m_canvas != NULL && m_canvas->ActiveIsBoard();
}

RECT CShareScreensWindow::WindowRect() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (IsWindow())
        ::GetWindowRect(m_hWnd, &rc);
    return rc;
}

void CShareScreensWindow::refreshButtons()
{
    if (m_annotBtn != NULL)
        m_annotBtn->SetText(m_annotating ? TR(kAnnotExitText) : TR(kAnnotText));
    if (m_controlBtn != NULL)
    {
        const int state = (m_controlState >= 0 && m_controlState < kControlTextCount) ? m_controlState
                                                                                      : share_viewer_control_none;
        m_controlBtn->SetText(TR(kControlTexts[state]));
    }
}

LRESULT CShareScreensWindow::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //「标注 / 退出标注」：控制器决定进入还是退出（ctrlId 写进条件本身，见开发约定文档「UI 约定」第 4 条）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK)
        && n->ctrlId == static_cast<UINT>(kIdAnnotate))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareScreensCommand, kCmdAnnotate, 0);
        return 0;
    }
    //「申请控制 / 取消申请 / 结束控制」
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK)
        && n->ctrlId == static_cast<UINT>(kIdControl))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareScreensCommand, kCmdControl, 0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareScreensWindow::OnControlInputMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    //不在控制中、或正在标注（标注优先，鼠标交给画面控件画笔迹）：原样放过
    if (m_controlState != share_viewer_control_active || m_annotating || m_canvas == NULL || m_controlInput == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //按被控制的那一块换算；它没有显示出来时当作不在画面上（此时只转发键盘）
    RECT video = { 0, 0, 0, 0 };
    if (m_controlScreen < 0 || !m_canvas->ScreenVideoRect(m_controlScreen, video))
        memset(&video, 0, sizeof(video));
    //画面以外、又没有按着的按钮的鼠标消息（标题栏、按钮行）：照常交给窗口自己
    if (!m_forwarder.Handle(m_hWnd, uMsg, wParam, lParam, video, m_controlInput))
        bHandled = FALSE;
    return 0;
}

LRESULT CShareScreensWindow::OnKillFocusMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    if (m_controlState == share_viewer_control_active)
        m_forwarder.Release(m_controlInput);
    bHandled = FALSE;
    return 0;
}

LRESULT CShareScreensWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //控制中或标注中关窗：先请控制器结束控制、退出在本窗口里的标注，免得输入落在看不见的窗口上
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
    {
        if (m_controlState != share_viewer_control_none)
            ::PostMessage(m_notifyWnd, kMsgShareScreensCommand, kCmdControl, 0);
        if (m_annotating)
            ::PostMessage(m_notifyWnd, kMsgShareScreensCommand, kCmdAnnotate, 0);
    }
    //用户关窗只隐藏：画面清单再变化时控制器会重新显示
    ShowWindow(SW_HIDE);
    return 0;
}

} // namespace screenshare
