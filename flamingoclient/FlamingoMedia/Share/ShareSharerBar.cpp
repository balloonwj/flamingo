/**
 *  多人共享的共享者控制条与主持人退出选择框，见 ShareSharerBar.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareSharerBar.h"

#include <memory>
#include "ShareMessages.h"
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"   //「更多」菜单（2026-10-08）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"         // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText：译文较长时加宽按钮

namespace screenshare {

namespace {

//共享者控制条：整条宽、标题栏高、内容横条高、按钮宽、按钮上下留白、横条内边距与间距、距工作区顶部的距离
const int kBarW = 300;
const int kBarTitleH = 18;
const int kBarContentH = 44;
const int kStopW = 84;
//「标注」按钮的宽（第 22 期 22e）
const int kAnnotW = 84;
//「屏幕当白板 / 退出屏幕白板」按钮的最小宽（2026-10-07），译文较长时按两种文字中较宽的一个加宽
const int kScreenBoardW = 104;
//「更多」按钮的最小宽（2026-10-08），译文较长时加宽
const int kMoreW = 56;
const int kBtnVMargin = 8;
const int kRowPad = 12;
const int kRowGap = 8;
const int kTopMargin = 8;
//退出选择框：宽、标题栏高、内边距、行间距、说明文字高、按钮高、底部两个按钮的宽
const int kDlgW = 380;
const int kDlgTitleH = 36;
const int kDlgPad = 16;
const int kDlgGap = 8;
const int kDlgTextH = 44;
const int kDlgBtnH = 32;
const int kDlgBottomBtnW = 96;
//底色与文字色（与控制条一致）、红点颜色
const COLORREF kBarBgColor = RGB(44, 48, 56);
const COLORREF kBarTextColor = RGB(255, 255, 255);
const COLORREF kDotColor = RGB(30, 136, 229);
//红点所占的宽
const int kDotW = 14;
//纯色背景图的边长、32 位 DIB 的位深与每像素字节数、不透明
const int kSolidBitmapSize = 8;
const WORD kBitsPerPixel32 = 32;
const int kBytesPerPixel = 4;
const BYTE kOpaque = 255;

//纯色位图（不透明）
HBITMAP makeSolidBitmap(COLORREF c, int side)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = side;
    bi.bmiHeader.biHeight = -side;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* p = static_cast<BYTE*>(bits);
    for (int i = 0; i < side * side; ++i)
    {
        p[i * kBytesPerPixel + 0] = GetBValue(c);
        p[i * kBytesPerPixel + 1] = GetGValue(c);
        p[i * kBytesPerPixel + 2] = GetRValue(c);
        p[i * kBytesPerPixel + 3] = kOpaque;
    }
    return bmp;
}

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
    : m_notifyWnd(NULL)
    , m_bgBitmap(NULL)
    , m_annotButton(NULL)
    , m_moreButton(NULL)
    , m_annotating(false)
    , m_screenBoardAvailable(false)
    , m_screenBoardActive(false)
{
    memset(&m_anchor, 0, sizeof(m_anchor));
}

CShareSharerBar::~CShareSharerBar()
{
    Hide();
    if (m_bgBitmap != NULL)
        ::DeleteObject(m_bgBitmap);
}

void CShareSharerBar::Show(const RECT& anchor, HWND notifyWnd)
{
    m_notifyWnd = notifyWnd;
    if (!IsWindow())
    {
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        if (Create(NULL, CWindow::rcDefault, TR(L"屏幕共享"), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("screenshare: create sharer bar failed.");
            return;
        }
        SetTitle(TR(L"屏幕共享"));
        SetTitleBarHeight(kBarTitleH);
        SetButtons(false, false, false);
        SetResizable(false);
        SetBorderPx(0);
        if (m_bgBitmap == NULL)
            m_bgBitmap = makeSolidBitmap(kBarBgColor, kSolidBitmapSize);
        if (m_bgBitmap != NULL)
        {
            RECT insets = { 0, 0, 0, 0 };
            SetBgImage(m_bgBitmap, insets);
            SetTitleBarTransparent(true);
            SetTitleTextColor(kBarTextColor);
        }
        BuildUi();
        //控制条浮在被共享的内容上，默认不能被采进共享画面；「允许截图」打开时（2026-10-08）允许
        ApplyBarCaptureState(m_hWnd);
    }
    m_anchor = anchor;
    const RECT work = workAreaOf(anchor);
    int w = i18n::DefaultButtonWidthForText(TR(L"你正在共享屏幕"), kBarW - kStopW) + kStopW + kAnnotW + kRowGap;
    //「更多」（2026-10-08）
    w += i18n::DefaultButtonWidthForText(TR(L"更多"), kMoreW) + kRowGap;
    //「屏幕当白板」（2026-10-07）：提供时整条加宽
    if (m_screenBoardAvailable)
        w += screenBoardWidth() + kRowGap;
    const int x = work.left + ((work.right - work.left) - w) / 2;
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, x, work.top + kTopMargin, w, kBarTitleH + kBarContentH,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CShareSharerBar::Hide()
{
    m_annotButton = NULL;
    m_moreButton = NULL;
    m_annotating = false;
    //下一次共享由控制器按共享内容重新设置
    m_screenBoardAvailable = false;
    m_screenBoardActive = false;
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

LRESULT CShareSharerBar::OnShowMoreMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //已经有菜单开着、窗口已隐藏（等待销毁或已停止共享）：不再弹
    if (m_popup.IsActive() || m_popup.IsPendingDestroy() || !IsWindowVisible() || m_moreButton == NULL)
        return 0;
    balloonwjui::DuiMenu menu;
    //允许截图：本机的工具条与控制条能否被截图，只对这一场共享有效
    menu.AppendChecked(kMenuAllowCapture, TR(L"允许截图"), IsBarCaptureAllowed());
    //菜单弹在「更多」按钮下方
    const RECT& rc = m_moreButton->GetRect();
    POINT pt = { rc.left, rc.bottom };
    ClientToScreen(&pt);
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

int CShareSharerBar::screenBoardWidth() const
{
    return i18n::DefaultButtonWidthForTexts(TR(L"屏幕当白板"), TR(L"退出屏幕白板"), kScreenBoardW);
}

void CShareSharerBar::SetScreenBoard(bool available, bool active)
{
    if (available == m_screenBoardAvailable && active == m_screenBoardActive)
        return;
    m_screenBoardAvailable = available;
    m_screenBoardActive = active;
    //按钮出现、消失或文字变了：重建内容并按新的宽度重新摆位（命令经 PostMessage 交给控制器，这里不在按钮的点击通知里）
    if (IsWindow())
    {
        BuildUi();
        Show(m_anchor, m_notifyWnd);
    }
}

void CShareSharerBar::SetAnnotating(bool on)
{
    m_annotating = on;
    if (m_annotButton != NULL)
    {
        m_annotButton->SetText(on ? TR(L"退出标注") : TR(L"标注"));
        m_annotButton->Invalidate();
    }
}

void CShareSharerBar::BuildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kBarBgColor);
    root->SetPadding(0);
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(kRowPad, 0, kRowPad, 0);
    row->SetGap(kRowGap);
    std::unique_ptr<balloonwjui::DuiLabel> dot(new balloonwjui::DuiLabel());
    dot->SetText(_T("●"));
    dot->SetTextColor(kDotColor);
    dot->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    row->AddChild(std::move(dot), balloonwjui::DuiLayout::Hint().Fixed(kDotW));
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetText(TR(L"你正在共享屏幕"));
    label->SetTextColor(kBarTextColor);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    row->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Weight(1));
    //在自己共享的内容上标注（第 22 期 22e）
    std::unique_ptr<balloonwjui::DuiButton> annot = makeButton(m_annotating ? TR(L"退出标注") : TR(L"标注"), kIdAnnotate,
                                                               balloonwjui::DuiButton::Variant::Default);
    m_annotButton = annot.get();
    row->AddChild(std::move(annot), balloonwjui::DuiLayout::Hint().Fixed(kAnnotW).Margin(0, kBtnVMargin, 0, kBtnVMargin));
    //屏幕当白板（2026-10-07，方案第 14.1 节 ③）：只共享一个窗口、或已在屏幕白板里时才有
    if (m_screenBoardAvailable)
    {
        row->AddChild(makeButton(m_screenBoardActive ? TR(L"退出屏幕白板") : TR(L"屏幕当白板"), kIdScreenBoard,
                                 balloonwjui::DuiButton::Variant::Default),
                      balloonwjui::DuiLayout::Hint().Fixed(screenBoardWidth()).Margin(0, kBtnVMargin, 0, kBtnVMargin));
    }
    //更多（2026-10-08）：目前只有「允许截图」一项
    std::unique_ptr<balloonwjui::DuiButton> more = makeButton(TR(L"更多"), kIdMore, balloonwjui::DuiButton::Variant::Default);
    m_moreButton = more.get();
    row->AddChild(std::move(more), balloonwjui::DuiLayout::Hint()
                                       .Fixed(i18n::DefaultButtonWidthForText(TR(L"更多"), kMoreW))
                                       .Margin(0, kBtnVMargin, 0, kBtnVMargin));
    row->AddChild(makeButton(TR(L"停止共享"), kIdStop, balloonwjui::DuiButton::Variant::Danger),
                  balloonwjui::DuiLayout::Hint().Fixed(kStopW).Margin(0, kBtnVMargin, 0, kBtnVMargin));
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
}

LRESULT CShareSharerBar::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //停止共享
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdStop))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareSharerCommand, share_sharer_stop, 0);
        return 0;
    }
    //标注 / 退出标注（第 22 期 22e）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdAnnotate))
    {
        if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
            ::PostMessage(m_notifyWnd, kMsgShareSharerCommand, share_sharer_annotate, 0);
        return 0;
    }
    //更多（2026-10-08）：投给自己，等这次点击通知处理完再弹菜单
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdMore))
    {
        PostMessage(kMsgShowMoreMenu, 0, 0);
        return 0;
    }
    //屏幕当白板 / 退出屏幕白板（2026-10-07）：由控制器提示确认后切换共享内容
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) &&
        n->ctrlId == static_cast<UINT>(kIdScreenBoard))
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
