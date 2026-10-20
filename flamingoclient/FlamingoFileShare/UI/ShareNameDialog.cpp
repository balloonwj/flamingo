/**
 *  输入一个名字的小窗口 CShareNameDialog 的实现，见 ShareNameDialog.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "ShareNameDialog.h"

#include <memory>

#include "../Engine/ShareBrowseModel.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiEdit;

namespace fileshare {

namespace {

// ---- 尺寸（像素）----
const int kContentWidth   = 380;   //整窗宽度
const int kContentHeight  = 168;   //标题栏以下内容区的高度
const int kTitleBarHeight = 32;    //标题栏高，与客户端其余窗口一致
const int kPadding        = 16;    //四周内边距
const int kPromptHeight   = 22;    //提示文字高
const int kEditHeight     = 32;    //输入框高
const int kErrorHeight    = 22;    //错误提示高
const int kButtonHeight   = 30;    //按钮高
const int kButtonWidth    = 80;    //按钮宽
const int kGap            = 6;     //各行之间的间距
const int kButtonGap      = 8;     //两个按钮之间的间距

// ---- 控件 id（只在本窗口内唯一）----
const UINT kIdEdit   = 201;   //输入框
const UINT kIdOk     = 202;   //「确定」
const UINT kIdCancel = 203;   //「取消」

// ---- 配色 ----
const COLORREF kBgColor     = RGB(0xFF, 0xFF, 0xFF);   //白底
const COLORREF kTextColor   = RGB(0x1F, 0x1F, 0x1F);   //提示文字
const COLORREF kErrorColor  = RGB(0xD9, 0x3B, 0x3B);   //错误提示

// ---- 文字 ----
const wchar_t* const kOkText     = TR_NOOP(L"确定");
const wchar_t* const kCancelText = TR_NOOP(L"取消");

} // namespace

CShareNameDialog::CShareNameDialog()
    : m_owner(NULL)
    , m_purpose(0)
    , m_listener(NULL)
    , m_edit(NULL)
    , m_error(NULL)
{
}

CShareNameDialog::~CShareNameDialog()
{
    Close();
}

bool CShareNameDialog::Open(HWND owner, int purpose, const wchar_t* title, const wchar_t* prompt,
                            const std::wstring& initial, IShareNameDialogListener* listener)
{
    if (listener == NULL)
        return false;
    Close();

    //带所有者创建（Create 的父窗口参数给的是所有者）：始终在所有者之上。控件通知发回本窗口，与有无所有者无关
    //（2026-10-14 起，见开发约定文档 UI 约定第 11 条）
    HWND parent = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
    if (Create(parent, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("share name dialog: Create failed, error: %lu.", ::GetLastError());
        return false;
    }
    m_owner = parent;
    m_purpose = purpose;
    m_listener = listener;
    SetTitle(title);
    SetTitleBarHeight(kTitleBarHeight);
    SetButtons(false, false, true);
    //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 图标（图标资源在 exe 里，见 TitleBarIcon.h）
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetResizable(false);
    BuildContent(prompt);
    //DuiFrameWindow 的 ResizeClient 设的是整窗尺寸，标题栏也在其内（UI 约定第 7 条）
    ResizeClient(kContentWidth, kContentHeight + kTitleBarHeight);
    CenterWindow(parent);

    if (m_edit != NULL)
    {
        m_edit->SetTextNoNotify(initial.c_str());
        m_edit->SelectAll();
    }
    SetError(std::wstring());
    if (m_owner != NULL)
        ::EnableWindow(m_owner, FALSE);
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (m_edit != NULL)
        m_edit->SetFocus();
    LOG_INFO("share name dialog opened, purpose: %d.", purpose);
    return true;
}

void CShareNameDialog::Close()
{
    RestoreOwner();
    if (IsWindow())
        DestroyWindow();
    m_edit = NULL;
    m_error = NULL;
    m_listener = NULL;
}

bool CShareNameDialog::IsOpen() const
{
    return IsWindow() != FALSE;
}

void CShareNameDialog::SetInputText(const std::wstring& text)
{
    if (m_edit != NULL)
        m_edit->SetTextNoNotify(text.c_str());
}

bool CShareNameDialog::Confirm()
{
    if (m_edit == NULL || m_listener == NULL)
        return false;
    const std::wstring name(m_edit->GetText().GetString());
    std::wstring error;
    if (!ShareBrowseModel::CheckNewName(name, error))
    {
        SetError(error);
        return false;
    }
    //先恢复所有者并隐藏本窗口，再回调：监听者可能随即弹出别的窗口或提示
    IShareNameDialogListener* listener = m_listener;
    const int purpose = m_purpose;
    RestoreOwner();
    ShowWindow(SW_HIDE);
    listener->OnShareNameEntered(purpose, name);
    //销毁放到消息处理完之后：本函数可能正处在本窗口的通知处理里
    if (IsWindow())
        PostMessage(WM_CLOSE);
    return true;
}

std::wstring CShareNameDialog::GetErrorText() const
{
    if (m_error == NULL || !m_error->IsVisible())
        return std::wstring();
    return std::wstring(m_error->GetText().GetString());
}

LRESULT CShareNameDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    Close();
    return 0;
}

LRESULT CShareNameDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //点「确定」，或在输入框里按回车：检查并确认
    if ((pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdOk)
        || (pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ENTER && pNotify->ctrlId == kIdEdit))
    {
        Confirm();
        return 0;
    }
    //点「取消」，或在输入框里按 Esc：直接关闭（通知处理里不能销毁窗口，经消息投递）
    if ((pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdCancel)
        || (pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ESCAPE && pNotify->ctrlId == kIdEdit))
    {
        PostMessage(WM_CLOSE);
        return 0;
    }
    //输入有变化：清掉上一次的错误提示
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdEdit)
    {
        SetError(std::wstring());
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

void CShareNameDialog::BuildContent(const wchar_t* prompt)
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBgColor);
    root->SetPadding(kPadding, kPadding, kPadding, kPadding);
    root->SetGap(kGap);

    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(prompt);
    label->SetTextColor(kTextColor);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    root->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Fixed(kPromptHeight));

    std::unique_ptr<DuiEdit> edit(new DuiEdit());
    edit->SetCtrlId(kIdEdit);
    edit->SetBgColor(kBgColor);
    edit->SetMaxLength(static_cast<int>(kShareNameMaxChars));
    m_edit = edit.get();
    root->AddChild(std::unique_ptr<DuiControl>(edit.release()), Hint().Fixed(kEditHeight));

    std::unique_ptr<DuiLabel> error(new DuiLabel());
    error->SetTextColor(kErrorColor);
    error->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    m_error = error.get();
    root->AddChild(std::unique_ptr<DuiControl>(error.release()), Hint().Fixed(kErrorHeight));

    //按钮行：右对齐的「确定」「取消」
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kButtonGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> ok(new DuiButton());
    ok->SetText(TR(kOkText));
    ok->SetCtrlId(kIdOk);
    buttons->AddChild(std::unique_ptr<DuiControl>(ok.release()), Hint().Fixed(kButtonWidth));
    std::unique_ptr<DuiButton> cancel(new DuiButton());
    cancel->SetText(TR(kCancelText));
    cancel->SetVariant(DuiButton::Variant::Ghost);
    cancel->SetCtrlId(kIdCancel);
    buttons->AddChild(std::unique_ptr<DuiControl>(cancel.release()), Hint().Fixed(kButtonWidth));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonHeight));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
}

void CShareNameDialog::SetError(const std::wstring& text)
{
    if (m_error == NULL)
        return;
    //错误行固定占位，只换文字：显隐不改变布局，不必重排
    m_error->SetText(text.c_str());
    m_error->Invalidate();
}

void CShareNameDialog::RestoreOwner()
{
    if (m_owner == NULL)
        return;
    const HWND owner = m_owner;
    m_owner = NULL;
    if (::IsWindow(owner))
    {
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
    }
}

} // namespace fileshare
