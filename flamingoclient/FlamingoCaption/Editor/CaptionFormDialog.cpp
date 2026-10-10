/**
 *  字幕编辑器的通用小表单对话框，见 CaptionFormDialog.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionFormDialog.h"

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace caption {

namespace {

//行的种类
const int kFieldEdit = 0;
const int kFieldCheck = 1;
const int kFieldNote = 2;
//尺寸（逻辑像素，整窗含标题栏）
const int kDialogW = 440;
const int kTitleBarH = 32;
const int kPadding = 16;
const int kGap = 10;
const int kRowH = 30;
const int kNoteH = 40;
const int kLabelW = 96;
const int kButtonW = 96;
//控件 id：按钮从 kIdButtonBase 起，行从 kIdFieldBase 起（避开标题栏按钮占用的 1~3）
const UINT kIdButtonBase = 100;
const UINT kIdFieldBase = 200;
//说明文字颜色
const COLORREF kNoteColor = RGB(110, 114, 122);

} // namespace

void RunCaptionModalLoop(HWND dlg, HWND owner, const bool& done)
{
    const bool ownerEnabled = (owner != NULL && ::IsWindowEnabled(owner) != FALSE);
    if (ownerEnabled)
        ::EnableWindow(owner, FALSE);
    ::ShowWindow(dlg, SW_SHOW);
    ::SetForegroundWindow(dlg);
    MSG msg;
    while (!done && ::IsWindow(dlg))
    {
        const BOOL got = ::GetMessage(&msg, NULL, 0, 0);
        if (got == 0)
        {
            //WM_QUIT：重新投递给外层消息循环，自己退出
            ::PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (got == -1)
            break;
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
    if (ownerEnabled)
    {
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
    }
}

CaptionFormDialog::CaptionFormDialog()
    : m_handler(NULL)
    , m_done(false)
    , m_result(-1)
{
}

CaptionFormDialog::~CaptionFormDialog()
{
    if (IsWindow())
        DestroyWindow();
}

int CaptionFormDialog::AddEdit(const std::wstring& label, const std::wstring& text)
{
    Field f;
    f.m_kind = kFieldEdit;
    f.m_label = label;
    f.m_text = text;
    f.m_checked = false;
    f.m_edit = NULL;
    f.m_check = NULL;
    f.m_note = NULL;
    m_fields.push_back(f);
    return static_cast<int>(m_fields.size()) - 1;
}

int CaptionFormDialog::AddCheck(const std::wstring& label, bool checked)
{
    const int idx = AddEdit(label, std::wstring());
    m_fields[idx].m_kind = kFieldCheck;
    m_fields[idx].m_checked = checked;
    return idx;
}

int CaptionFormDialog::AddNote(const std::wstring& text)
{
    const int idx = AddEdit(text, std::wstring());
    m_fields[idx].m_kind = kFieldNote;
    return idx;
}

int CaptionFormDialog::AddButton(const std::wstring& text, bool primary)
{
    Button b;
    b.m_text = text;
    b.m_primary = primary;
    m_buttons.push_back(b);
    return static_cast<int>(m_buttons.size()) - 1;
}

std::wstring CaptionFormDialog::Text(int field) const
{
    if (field < 0 || field >= static_cast<int>(m_fields.size()))
        return std::wstring();
    const Field& f = m_fields[field];
    if (f.m_edit != NULL && IsWindow())
        return f.m_edit->GetText().GetString();
    return f.m_text;
}

bool CaptionFormDialog::Checked(int field) const
{
    if (field < 0 || field >= static_cast<int>(m_fields.size()))
        return false;
    const Field& f = m_fields[field];
    if (f.m_check != NULL && IsWindow())
        return f.m_check->IsChecked();
    return f.m_checked;
}

void CaptionFormDialog::SetNote(int field, const std::wstring& text)
{
    if (field < 0 || field >= static_cast<int>(m_fields.size()))
        return;
    m_fields[field].m_label = text;
    if (m_fields[field].m_note != NULL && IsWindow())
        m_fields[field].m_note->SetText(text.c_str());
}

void CaptionFormDialog::buildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);
    for (size_t i = 0; i < m_fields.size(); ++i)
    {
        Field& f = m_fields[i];
        const UINT id = kIdFieldBase + static_cast<UINT>(i);
        if (f.m_kind == kFieldNote)
        {
            std::unique_ptr<balloonwjui::DuiLabel> note(new balloonwjui::DuiLabel());
            note->SetText(f.m_label.c_str());
            note->SetTextColor(kNoteColor);
            note->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
            note->SetCtrlId(id);
            f.m_note = note.get();
            root->AddChild(std::move(note), balloonwjui::DuiLayout::Hint().Fixed(kNoteH));
            continue;
        }
        if (f.m_kind == kFieldCheck)
        {
            std::unique_ptr<balloonwjui::DuiButton> check(new balloonwjui::DuiButton());
            check->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
            check->SetText(f.m_label.c_str());
            check->SetCheck(f.m_checked, false);
            check->SetCtrlId(id);
            f.m_check = check.get();
            root->AddChild(std::move(check), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
            continue;
        }
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kGap);
        std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
        label->SetText(f.m_label.c_str());
        label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        row->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Fixed(kLabelW));
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetTextNoNotify(f.m_text.c_str());
        edit->SetVerticalCenter(true);
        edit->SetShowBorder(true);
        edit->SetCtrlId(id);
        f.m_edit = edit.get();
        row->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Weight(1));
        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }
    root->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->SetGap(kGap);
    buttons->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
        b->SetText(m_buttons[i].m_text.c_str());
        b->SetVariant(m_buttons[i].m_primary ? balloonwjui::DuiButton::Variant::Primary
                                             : balloonwjui::DuiButton::Variant::Default);
        b->SetCtrlId(kIdButtonBase + static_cast<UINT>(i));
        buttons->AddChild(std::move(b), balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    }
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    SetClientContent(std::move(root));
}

int CaptionFormDialog::Run(HWND owner, const std::wstring& title, ICaptionFormHandler* handler)
{
    m_handler = handler;
    m_done = false;
    m_result = -1;
    //有所有者的弹窗用 WS_OVERLAPPEDWINDOW，不能用 WS_POPUP：balloonui 按 GetParent 决定控件通知发给谁，有所有者的
    //WS_POPUP 窗口 GetParent 返回所有者，按钮的点击通知（含标题栏关闭按钮）会全部发到编辑器窗口，本窗口一条都收不到
    //（2026-10-06 导出窗口点「开始导出」没反应、也关不掉，即由此而来；外观由 DuiFrameWindow 自绘，不受影响）
    if (Create(owner, CWindow::rcDefault, title.c_str(), WS_OVERLAPPEDWINDOW, WS_EX_DLGMODALFRAME) == NULL)
        return -1;
    SetTitle(title.c_str());
    SetTitleBarHeight(kTitleBarH);
    //标题栏显示程序图标（开发约定文档「UI 约定」第 10 条）
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetButtons(false, false, true);
    SetResizable(false);
    int contentH = 0;
    for (size_t i = 0; i < m_fields.size(); ++i)
        contentH += (m_fields[i].m_kind == kFieldNote ? kNoteH : kRowH) + kGap;
    //整窗高 = 标题栏 + 上下内边距 + 各行 + 一个间距 + 按钮行（DuiFrameWindow 的 ResizeClient 设的是整窗）
    const int height = kTitleBarH + 2 * kPadding + contentH + kGap + kRowH;
    ResizeClient(kDialogW, height);
    buildUi();
    //居中在属主上
    RECT ownerRc = { 0, 0, 0, 0 };
    if (owner != NULL && ::GetWindowRect(owner, &ownerRc))
    {
        const int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - kDialogW) / 2;
        const int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - height) / 2;
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RunCaptionModalLoop(m_hWnd, owner, m_done);
    return m_result;
}

void CaptionFormDialog::collect()
{
    for (size_t i = 0; i < m_fields.size(); ++i)
    {
        Field& f = m_fields[i];
        if (f.m_edit != NULL)
            f.m_text = f.m_edit->GetText().GetString();
        if (f.m_check != NULL)
            f.m_checked = f.m_check->IsChecked();
        f.m_edit = NULL;
        f.m_check = NULL;
        f.m_note = NULL;
    }
}

void CaptionFormDialog::finish(int result)
{
    if (m_done)
        return;
    collect();
    m_result = result;
    m_done = true;
    if (IsWindow())
        DestroyWindow();
}

LRESULT CaptionFormDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId >= kIdButtonBase &&
        n->ctrlId < kIdButtonBase + m_buttons.size())
    {
        const int button = static_cast<int>(n->ctrlId - kIdButtonBase);
        const bool close = (m_handler == NULL) || m_handler->OnFormButton(button, *this);
        if (close)
            finish(button);
        return 0;
    }
    //输入框里按回车：等同于点第一个主按钮
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ENTER) &&
        n->ctrlId >= kIdFieldBase && n->ctrlId < kIdFieldBase + m_fields.size())
    {
        for (size_t i = 0; i < m_buttons.size(); ++i)
        {
            if (!m_buttons[i].m_primary)
                continue;
            const bool close = (m_handler == NULL) || m_handler->OnFormButton(static_cast<int>(i), *this);
            if (close)
                finish(static_cast<int>(i));
            break;
        }
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CaptionFormDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    finish(-1);
    return 0;
}

LRESULT CaptionFormDialog::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam == VK_ESCAPE)
    {
        finish(-1);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace caption
