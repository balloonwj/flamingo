/**
 *  屏幕共享模块的单行文字输入小窗，说明见 ShareTextInputDialog.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareTextInputDialog.h"

#include <memory>
#include "ShareAnnotation.h"
#include "ShareMessages.h"
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏程序图标）
#include "../../Source/resource.h"                      // IDR_MAINFRAME（任务栏图标，取自 exe 模块的资源）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//整窗尺寸（不含说明行）与标题栏高（逻辑像素）
const int kDlgW = 360;
const int kDlgH = 112;
const int kTitleBarH = 28;
//内容区内边距、控件间距、输入框与按钮行的高度、按钮宽
const int kPad = 10;
const int kGap = 8;
const int kRowH = 30;
const int kBtnW = 72;
//说明行的高度：有说明行时整窗加高 kHintH + kGap
const int kHintH = 20;
//说明文字的颜色：普通说明为深灰，提醒（如密码不对）为橙红，与选择窗顶部提示同色
const COLORREF kHintNormalColor = RGB(90, 96, 106);
const COLORREF kHintErrorColor = RGB(200, 80, 40);
//标注文字最多可输入的字符数（UTF-8 编码后不超过 kAnnotMaxTextBytes，中文按每字 3 字节算）
const int kAnnotMaxChars = kAnnotMaxTextBytes / 3;
//标题栏程序图标的边长（像素）与标题栏底色：图标按这个底色合成，须与 DuiFrameTitleBar 的填充色一致
const int kTitleIconSize = 16;
const COLORREF kTitleBarBgColor = RGB(245, 246, 250);

} // namespace

ShareTextInputSpec::ShareTextInputSpec()
    : m_hintIsError(false)
    , m_maxChars(kAnnotMaxChars)
    , m_validator(NULL)
    , m_resultMsg(kMsgShareAnnotText)
    , m_topmost(true)
    , m_inTaskbar(false)
    , m_allowEmpty(false)
{
}

CShareTextInputDialog::CShareTextInputDialog()
    : m_notifyWnd(NULL)
    , m_edit(NULL)
    , m_hintLabel(NULL)
    , m_finished(true)
    , m_titleIcon(NULL)
    , m_taskIconBig(NULL)
    , m_taskIconSmall(NULL)
{
}

CShareTextInputDialog::~CShareTextInputDialog()
{
    Close();
    if (m_titleIcon != NULL)
        ::DeleteObject(m_titleIcon);
    if (m_taskIconBig != NULL)
        ::DestroyIcon(m_taskIconBig);
    if (m_taskIconSmall != NULL)
        ::DestroyIcon(m_taskIconSmall);
}

void CShareTextInputDialog::applyProgramIcons(bool taskIcons)
{
    if (m_titleIcon == NULL)
        m_titleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBgColor);
    if (m_titleIcon != NULL)
        SetIcon(m_titleIcon);
    //标注文字等用途的窗口不进任务栏，只要标题栏图标
    if (!taskIcons)
        return;
    //图标资源在 exe 里：本类编进 FlamingoMedia.dll，要显式从 exe 模块（GetModuleHandle(NULL)）加载
    if (m_taskIconBig == NULL)
    {
        m_taskIconBig = (HICON)::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
                                           ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON),
                                           LR_DEFAULTCOLOR);
    }
    if (m_taskIconSmall == NULL)
    {
        m_taskIconSmall = (HICON)::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
                                             ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
                                             LR_DEFAULTCOLOR);
    }
    if (m_taskIconBig != NULL)
        ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)m_taskIconBig);
    if (m_taskIconSmall != NULL)
        ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_taskIconSmall);
}

int CShareTextInputDialog::dialogHeight() const
{
    const bool hasHint = !m_spec.m_hint.empty() || !m_spec.m_invalidHint.empty();
    return kDlgH + (hasHint ? kHintH + kGap : 0);
}

namespace {

//数字标号的号码：1~65535 的十进制数
bool isBadgeNumberText(const std::wstring& text)
{
    const long kMinNumber = 1;
    const long kMaxNumber = 65535;
    const int kDecimal = 10;
    if (text.empty())
        return false;
    wchar_t* end = NULL;
    const long n = wcstol(text.c_str(), &end, kDecimal);
    return end != NULL && *end == L'\0' && n >= kMinNumber && n <= kMaxNumber;
}

} // namespace

ShareTextInputSpec MakeAnnotTextEditSpec(int tool, const std::wstring& current)
{
    ShareTextInputSpec spec;
    spec.m_initialText = current;
    spec.m_resultMsg = kMsgShareAnnotText;
    spec.m_topmost = true;
    switch (tool)
    {
    //文字：清空即删除这段文字
    case annot_tool_text:
        spec.m_title = TR(L"修改文字");
        spec.m_placeholder = TR(L"输入要标注的文字，回车确定");
        break;
    //图元：名字可以清空
    case annot_tool_icon:
        spec.m_title = TR(L"修改名字");
        spec.m_placeholder = TR(L"输入图元的名字，可以为空");
        spec.m_allowEmpty = true;
        break;
    //数字标号：只能是 1~65535 的数
    case annot_tool_badge:
        spec.m_title = TR(L"修改编号");
        spec.m_placeholder = TR(L"输入 1～65535 之间的数字");
        spec.m_invalidHint = TR(L"请输入 1～65535 之间的数字");
        spec.m_validator = isBadgeNumberText;
        break;
    //连接线：线上文字可以清空
    default:
        spec.m_title = TR(L"修改线上文字");
        spec.m_placeholder = TR(L"输入连接线上的文字，可以为空");
        spec.m_allowEmpty = true;
        break;
    }
    return spec;
}

void CShareTextInputDialog::Open(POINT at, HWND notifyWnd, bool excludeFromCapture)
{
    //已打开时先按取消关闭旧的：要在换掉 m_spec 之前做，取消结果才会按旧窗口的结果消息发出
    if (IsWindow())
        finish(false);
    //标注文字：没有说明行，置顶，结果为 kMsgShareAnnotText
    ShareTextInputSpec spec;
    spec.m_title = TR(L"添加文字");
    spec.m_placeholder = TR(L"输入要标注的文字，回车确定");
    spec.m_maxChars = kAnnotMaxChars;
    spec.m_resultMsg = kMsgShareAnnotText;
    spec.m_topmost = true;
    Open(at, notifyWnd, excludeFromCapture, spec);
}

void CShareTextInputDialog::Open(POINT at, HWND notifyWnd, bool excludeFromCapture, const ShareTextInputSpec& spec)
{
    //已打开时先按取消关闭旧的（同上）
    if (IsWindow())
        finish(false);
    m_spec = spec;

    //摆在点击处，放不下时挪进工作区
    HMONITOR mon = ::MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    const int h = dialogHeight();
    int x = at.x;
    int y = at.y;
    if (x + kDlgW > work.right)
        x = work.right - kDlgW;
    if (y + h > work.bottom)
        y = work.bottom - h;
    if (x < work.left)
        x = work.left;
    if (y < work.top)
        y = work.top;
    openAt(x, y, notifyWnd, excludeFromCapture, spec);
}

void CShareTextInputDialog::OpenCentered(HWND anchor, HWND notifyWnd, const ShareTextInputSpec& spec)
{
    //同 Open：先关旧的，再换参数
    if (IsWindow())
        finish(false);
    m_spec = spec;
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
    const int x = work.left + ((work.right - work.left) - kDlgW) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 2;
    openAt(x, y, notifyWnd, false, spec);
}

void CShareTextInputDialog::openAt(int x, int y, HWND notifyWnd, bool excludeFromCapture, const ShareTextInputSpec& spec)
{
    m_spec = spec;
    m_notifyWnd = notifyWnd;
    m_finished = false;
    m_hintLabel = NULL;
    //不设所有者窗口，按钮通知才会发回本窗口（见头文件说明）
    const DWORD exStyle = (spec.m_inTaskbar ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW) | (spec.m_topmost ? WS_EX_TOPMOST : 0);
    if (Create(NULL, CWindow::rcDefault, spec.m_title.c_str(), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR("screenshare: create text input dialog failed.");
        m_finished = true;
        return;
    }
    SetTitle(spec.m_title.c_str());
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);
    SetResizable(false);
    //各种用途都在标题栏显示程序图标（2026-10-07 起，原先只有进任务栏的加入密码框才有）；任务栏图标只在进任务栏时设置
    applyProgramIcons(spec.m_inTaskbar);

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPad, kPad, kPad, kPad);
    root->SetGap(kGap);
    //说明行：有说明文字、或确定时可能要显示校验提示时才有
    if (!spec.m_hint.empty() || !spec.m_invalidHint.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
        hint->SetText(spec.m_hint.c_str());
        hint->SetTextColor(spec.m_hintIsError ? kHintErrorColor : kHintNormalColor);
        hint->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_hintLabel = hint.get();
        root->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(kHintH));
    }
    std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
    edit->SetCtrlId(kIdEdit);
    edit->SetMaxLength(spec.m_maxChars);
    edit->SetVerticalCenter(true);
    edit->SetShowBorder(true);
    edit->SetPlaceholder(spec.m_placeholder.c_str());
    //修改已有文字：预填并全选，直接输入即替换
    if (!spec.m_initialText.empty())
    {
        edit->SetText(spec.m_initialText.c_str());
        edit->SelectAll();
    }
    m_edit = edit.get();
    root->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
    row->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
    cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
    cancel->SetText(TR(L"取消"));
    cancel->SetCtrlId(kIdCancel);
    row->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kBtnW));
    std::unique_ptr<balloonwjui::DuiButton> ok(new balloonwjui::DuiButton());
    ok->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    ok->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    ok->SetText(TR(L"确定"));
    ok->SetCtrlId(kIdOk);
    row->AddChild(std::move(ok), balloonwjui::DuiLayout::Hint().Fixed(kBtnW));
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    SetClientContent(std::move(root));

    if (excludeFromCapture)
        ExcludeWindowFromCapture(m_hWnd);
    ::SetWindowPos(m_hWnd, spec.m_topmost ? HWND_TOPMOST : HWND_TOP, x, y, kDlgW, dialogHeight(), SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hWnd);
    m_edit->SetFocus();
}

void CShareTextInputDialog::finish(bool ok)
{
    if (m_finished)
        return;
    std::wstring* text = NULL;
    if (ok && m_edit != NULL)
    {
        const CString s = m_edit->GetText();
        const std::wstring value = s.GetString();
        //确定时输入为空：标注文字按取消处理；有校验函数的（如密码）提示后保持窗口打开
        const bool valid = (m_spec.m_validator != NULL) ? m_spec.m_validator(value)
                                                        : (m_spec.m_allowEmpty || !value.empty());
        if (!valid)
        {
            if (m_spec.m_validator != NULL && m_hintLabel != NULL)
            {
                m_hintLabel->SetText(m_spec.m_invalidHint.c_str());
                m_hintLabel->SetTextColor(kHintErrorColor);
                m_hintLabel->Invalidate();
                return;
            }
        }
        else
        {
            text = new std::wstring(value);
        }
    }
    m_finished = true;
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
    {
        if (!::PostMessage(m_notifyWnd, m_spec.m_resultMsg, 0, reinterpret_cast<LPARAM>(text)))
            delete text;
    }
    else
    {
        delete text;
    }
    m_edit = NULL;
    m_hintLabel = NULL;
    if (IsWindow())
        DestroyWindow();
}

void CShareTextInputDialog::Close()
{
    m_finished = true;
    m_edit = NULL;
    m_hintLabel = NULL;
    if (IsWindow())
        DestroyWindow();
}

LRESULT CShareTextInputDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    finish(false);
    return 0;
}

LRESULT CShareTextInputDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //输入框里按了回车：确定
    if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ENTER) && n->ctrlId == static_cast<UINT>(kIdEdit))
    {
        finish(true);
        return 0;
    }
    //输入框里按了 Esc：取消
    if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE) && n->ctrlId == static_cast<UINT>(kIdEdit))
    {
        finish(false);
        return 0;
    }
    //「确定」
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdOk))
    {
        finish(true);
        return 0;
    }
    //「取消」
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdCancel))
    {
        finish(false);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenshare
