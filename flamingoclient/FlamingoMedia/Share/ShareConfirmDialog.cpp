/**
 *  屏幕共享模块的确认小窗，说明见 ShareConfirmDialog.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareConfirmDialog.h"

#include <memory>
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//整窗宽、标题栏高（逻辑像素）
const int kDlgW = 360;
const int kTitleBarH = 28;
//内容区内边距与控件间距
const int kPad = 12;
const int kGap = 8;
//提问文字一行的高度与默认行数（两行）
const int kPromptLineH = 20;
const int kPromptDefaultLines = 2;
//勾选项行高与勾选项宽（带圆角边框，「允许他再次加入」七个字再窄会被截掉）
const int kCheckH = 26;
const int kCheckW = 140;
//按钮行高与按钮宽
const int kButtonRowH = 30;
const int kBtnW = 72;
//提问文字颜色（深灰，与名单里的文字一致）
const COLORREF kPromptColor = RGB(40, 44, 52);

} // namespace

ShareConfirmSpec::ShareConfirmSpec()
    : m_checked(false)
    , m_resultMsg(0)
    , m_promptLines(kPromptDefaultLines)
    , m_width(0)
{
}

CShareConfirmDialog::CShareConfirmDialog()
    : m_notifyWnd(NULL)
    , m_check(NULL)
    , m_finished(true)
{
}

CShareConfirmDialog::~CShareConfirmDialog()
{
    Close();
}

int CShareConfirmDialog::dialogHeight() const
{
    const int lines = (m_spec.m_promptLines > 0) ? m_spec.m_promptLines : kPromptDefaultLines;
    int h = kTitleBarH + 2 * kPad + lines * kPromptLineH + kGap + kButtonRowH;
    if (!m_spec.m_checkText.empty())
        h += kCheckH + kGap;
    return h;
}

void CShareConfirmDialog::OpenCentered(HWND anchor, HWND notifyWnd, const ShareConfirmSpec& spec,
                                       bool excludeFromCapture)
{
    //已打开时先按取消关闭旧的：要在换掉 m_spec 之前做，取消结果才会按旧窗口的结果消息发出
    if (IsWindow())
        finish(false);
    m_spec = spec;
    m_notifyWnd = notifyWnd;
    m_finished = false;
    m_check = NULL;

    //不设所有者窗口（原为让按钮通知发回本窗口；2026-10-14 起有无所有者，控件通知都发回本窗口，见头文件说明）
    if (Create(NULL, CWindow::rcDefault, spec.m_title.c_str(), WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST) == NULL)
    {
        LOG_ERROR("screenshare: create confirm dialog failed.");
        m_finished = true;
        return;
    }
    SetTitle(spec.m_title.c_str());
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);
    SetResizable(false);

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPad, kPad, kPad, kPad);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> prompt(new balloonwjui::DuiLabel());
    prompt->SetText(spec.m_prompt.c_str());
    prompt->SetTextColor(kPromptColor);
    prompt->SetCtrlId(kIdPrompt);
    //名字很长时自动换行，最后一行放不下的部分省略
    prompt->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    const int lines = (spec.m_promptLines > 0) ? spec.m_promptLines : kPromptDefaultLines;
    root->AddChild(std::move(prompt), balloonwjui::DuiLayout::Hint().Fixed(lines * kPromptLineH));
    if (!spec.m_checkText.empty())
    {
        std::unique_ptr<balloonwjui::DuiHBox> checkRow(new balloonwjui::DuiHBox());
        std::unique_ptr<balloonwjui::DuiButton> check(new balloonwjui::DuiButton());
        check->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        check->SetText(spec.m_checkText.c_str());
        check->SetCtrlId(kIdCheck);
        check->SetCheck(spec.m_checked, false);
        m_check = check.get();
        checkRow->AddChild(std::move(check), balloonwjui::DuiLayout::Hint().Fixed(kCheckW));
        checkRow->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                           balloonwjui::DuiLayout::Hint().Weight(1));
        root->AddChild(std::move(checkRow), balloonwjui::DuiLayout::Hint().Fixed(kCheckH));
    }
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kGap);
    row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
    cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
    cancel->SetText(TR(L"取消"));
    cancel->SetCtrlId(kIdCancel);
    row->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kBtnW));
    std::unique_ptr<balloonwjui::DuiButton> ok(new balloonwjui::DuiButton());
    ok->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    ok->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    ok->SetText(spec.m_okText.c_str());
    ok->SetCtrlId(kIdOk);
    row->AddChild(std::move(ok), balloonwjui::DuiLayout::Hint().Fixed(kBtnW));
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    SetClientContent(std::move(root));

    if (excludeFromCapture)
        ExcludeWindowFromCapture(m_hWnd);
    //居中在 anchor 所在显示器的工作区上
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
    const int h = dialogHeight();
    const int w = (spec.m_width > 0) ? spec.m_width : kDlgW;
    const int x = work.left + ((work.right - work.left) - w) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 2;
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hWnd);
}

void CShareConfirmDialog::finish(bool ok)
{
    if (m_finished)
        return;
    m_finished = true;
    const bool checked = (m_check != NULL && m_check->IsChecked());
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, m_spec.m_resultMsg, ok ? 1 : 0, (ok && checked) ? 1 : 0);
    m_check = NULL;
    if (IsWindow())
        DestroyWindow();
}

void CShareConfirmDialog::Close()
{
    m_finished = true;
    m_check = NULL;
    if (IsWindow())
        DestroyWindow();
}

bool CShareConfirmDialog::IsOpen() const
{
    return IsWindow() != FALSE;
}

LRESULT CShareConfirmDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    finish(false);
    return 0;
}

LRESULT CShareConfirmDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    //确认按钮
    if (n->ctrlId == static_cast<UINT>(kIdOk))
    {
        finish(true);
        return 0;
    }
    //「取消」
    if (n->ctrlId == static_cast<UINT>(kIdCancel))
    {
        finish(false);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenshare
