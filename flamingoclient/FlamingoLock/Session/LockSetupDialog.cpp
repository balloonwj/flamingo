/**
 *  LockSetupDialog.cpp —— 见同名头文件说明。控件用 balloonui 现成控件程序化组合，写法参照
 *  Source/UI/BalloonMain/BalloonChangePwdDlg.cpp（修改密码窗口）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "LockSetupDialog.h"

#include <memory>

#include "../Core/LockPassword.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"
#include "../../Source/Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText：「预览」按钮按译文只增不减地加宽
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

namespace flamingolock {

namespace {

// ---- 窗口尺寸（逻辑像素）----
const int kDlgWidth = 400;          // 窗口宽
const int kDlgTitleBarH = 36;       // 标题栏高
const int kRootPadding = 22;        // 根容器内边距
const int kRootGap = 10;            // 根容器子项间距
const int kLabelH = 16;             // 字段标题行高
const int kFieldH = 34;             // 输入框、下拉框高
const int kHintH = 18;              // 说明文字行高
const int kErrorH = 18;             // 错误提示行高
const int kBottomBarH = 34;         // 底部按钮栏高
const int kButtonWidth = 78;        // 「取消」「锁屏」按钮宽
const int kButtonGap = 10;          // 两个按钮的间距
const int kPreviewButtonWidth = 64; // 「预览」按钮的设计宽度（简体中文）；译文更长时只增不减地加宽
const int kPreviewGap = 8;          // 下拉框与「预览」按钮的间距
const int kFieldCount = 3;          // 字段组数：密码、再输一次、锁屏画面（下拉框与「预览」按钮同一行，行高仍为 kFieldH）
// 内容高度 = 上下内边距 + 三组（标题 + 控件）+ 说明 + 错误提示 + 按钮栏 + 各子项间距。
// 2026-10-07 加「预览」按钮时放在下拉框右侧同一行，不增加行数，内容高度不变
const int kDlgContentH = kRootPadding * 2 + kFieldCount * (kLabelH + kFieldH) + kHintH + kErrorH + kBottomBarH
                       + kRootGap * (kFieldCount * 2 + 2);

// ---- 控件 id（窗口内唯一）----
const UINT kIdPassword = 1;         // 密码框
const UINT kIdConfirm = 2;          // 再输一次
const UINT kIdAnimation = 3;        // 锁屏画面下拉框
const UINT kIdCancel = 4;           // 取消
const UINT kIdLock = 5;             // 锁屏
const UINT kIdPreview = 6;          // 预览（锁屏画面下拉框右侧）

// ---- 颜色 ----
const COLORREF kLabelColor = RGB(0x3A, 0x3A, 0x3A);   // 字段标题深灰
const COLORREF kHintColor = RGB(0x8A, 0x8A, 0x90);    // 说明文字浅灰
const COLORREF kErrorColor = RGB(0xD9, 0x3B, 0x3B);   // 错误提示红
const COLORREF kFieldBgColor = RGB(0xF4, 0xF3, 0xF0); // 输入框浅灰底（与修改密码窗口一致）

// 下拉框各项的文字，顺序与 ChoiceAt 一致；到使用处再 TR
const wchar_t* const kChoiceTexts[kChoiceCount] =
{
    TR_NOOP(L"八荣八耻"),
    TR_NOOP(L"火烈鸟漫步"),
    TR_NOOP(L"粒子时钟"),
    TR_NOOP(L"弹跳标志与烟花"),
    TR_NOOP(L"汉字数字雨"),
    TR_NOOP(L"随机"),
    TR_NOOP(L"轮流播放（每分钟换一种）"),
};

} // namespace

CLockSetupDialog::CLockSetupDialog()
    : m_password(NULL)
    , m_confirm(NULL)
    , m_animation(NULL)
    , m_error(NULL)
    , m_finished(false)
{
    m_initialChoice = DefaultChoice();
    m_resultChoice = DefaultChoice();
}

CLockSetupDialog::~CLockSetupDialog()
{
    if (!m_resultPassword.empty())
    {
        ::SecureZeroMemory(&m_resultPassword[0], m_resultPassword.size() * sizeof(wchar_t));
    }
    //模块关闭时预览可能还开着：先关掉预览窗口，再销毁本窗口
    m_preview.End();
    if (IsWindow())
    {
        DestroyWindow();
    }
}

bool CLockSetupDialog::Open(const LockAnimationChoice& choice)
{
    if (IsWindow())
    {
        BringToFront();
        return true;
    }
    m_initialChoice = choice;
    m_finished = false;
    if (Create(NULL, CWindow::rcDefault, TR(L"锁屏"), WS_OVERLAPPEDWINDOW, WS_EX_TOPMOST) == NULL)
    {
        LOG_ERROR("lock setup dialog: create failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        return false;
    }
    SetTitle(TR(L"锁屏"));
    SetTitleBarHeight(kDlgTitleBarH);
    SetButtons(false, false, true);   // 只留关闭按钮
    //标题栏左侧的程序图标：标题栏不透明，底色取 balloonui 不透明标题栏的颜色。本窗口没有所有者窗口、
    //会出现在任务栏上，任务栏与 Alt+Tab 的图标一并设置。图标资源从 exe 模块加载
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    BuildUi();
    //kDlgContentH 是标题栏以下的内容高度，整窗高度要加上标题栏（DuiFrameWindow::ResizeClient 设的是整窗）
    ResizeClient(kDlgWidth, kDlgContentH + kDlgTitleBarH);
    SetMinSize(kDlgWidth, kDlgContentH + kDlgTitleBarH);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    BringToFront();
    LOG_INFO("lock setup dialog opened, initial choice %s.", ChoiceToToken(choice));
    return true;
}

void CLockSetupDialog::BringToFront()
{
    if (!IsWindow())
    {
        return;
    }
    if (IsIconic())
    {
        ShowWindow(SW_RESTORE);
    }
    ::SetForegroundWindow(m_hWnd);
    if (m_password != NULL)
    {
        m_password->SetFocus();
    }
}

void CLockSetupDialog::AddFieldLabel(balloonwjui::DuiVBox* root, LPCTSTR label)
{
    std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
    lab->SetText(label);
    lab->SetTextColor(kLabelColor);
    lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kLabelH));
}

void CLockSetupDialog::BuildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRootPadding);
    root->SetGap(kRootGap);

    AddFieldLabel(root.get(), TR(L"本次锁屏的临时密码"));
    {
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetCtrlId(kIdPassword);
        edit->SetPassword(true);
        edit->SetShowEyeToggle(true);
        edit->SetBgColor(kFieldBgColor);
        edit->SetPlaceholder(TRF(L"{0}~{1} 个字母、数字或符号", kMinPasswordChars, kMaxPasswordChars).c_str());
        m_password = edit.get();
        root->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Fixed(kFieldH));
    }
    AddFieldLabel(root.get(), TR(L"再输入一次"));
    {
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetCtrlId(kIdConfirm);
        edit->SetPassword(true);
        edit->SetShowEyeToggle(true);
        edit->SetBgColor(kFieldBgColor);
        m_confirm = edit.get();
        root->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Fixed(kFieldH));
    }
    AddFieldLabel(root.get(), TR(L"锁屏画面"));
    {
        //下拉框占满剩余宽度，右侧是「预览」按钮
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kPreviewGap);
        std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
        combo->SetCtrlId(kIdAnimation);
        for (int i = 0; i < kChoiceCount; ++i)
        {
            combo->AddString(TR(kChoiceTexts[i]));
        }
        combo->SetMaxVisibleItems(kChoiceCount);
        combo->SetCurSel(ChoiceIndex(m_initialChoice), false);
        m_animation = combo.get();
        row->AddChild(std::move(combo), balloonwjui::DuiLayout::Hint().Weight(1));

        const CString previewText = TR(L"预览");
        std::unique_ptr<balloonwjui::DuiButton> preview(new balloonwjui::DuiButton());
        preview->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        preview->SetVariant(balloonwjui::DuiButton::Variant::Default);
        preview->SetCtrlId(kIdPreview);
        preview->SetText(previewText);
        row->AddChild(std::move(preview),
                      balloonwjui::DuiLayout::Hint().Fixed(i18n::DefaultButtonWidthForText(previewText, kPreviewButtonWidth)));
        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kFieldH));
    }
    {
        std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
        hint->SetText(TR(L"密码只在本次锁屏有效，解锁后即作废。"));
        hint->SetTextColor(kHintColor);
        hint->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        root->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(kHintH));
    }
    {
        std::unique_ptr<balloonwjui::DuiLabel> error(new balloonwjui::DuiLabel());
        error->SetText(_T(""));
        error->SetTextColor(kErrorColor);
        error->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_error = error.get();
        root->AddChild(std::move(error), balloonwjui::DuiLayout::Hint().Fixed(kErrorH));
    }
    {
        std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
        bottom->SetGap(kButtonGap);
        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        bottom->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
        cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
        cancel->SetCtrlId(kIdCancel);
        cancel->SetText(TR(L"取消"));
        bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kButtonWidth));

        std::unique_ptr<balloonwjui::DuiButton> lock(new balloonwjui::DuiButton());
        lock->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        lock->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        lock->SetCtrlId(kIdLock);
        lock->SetText(TR(L"锁屏"));
        bottom->AddChild(std::move(lock), balloonwjui::DuiLayout::Hint().Fixed(kButtonWidth));
        root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));
    }
    SetClientContent(std::move(root));
}

void CLockSetupDialog::ShowError(LPCTSTR text)
{
    if (m_error != NULL)
    {
        m_error->SetText(text);
        m_error->Invalidate();
    }
}

void CLockSetupDialog::Confirm()
{
    //预览期间本窗口是禁用的，正常走不到这里；仍然拦一下，避免预览窗口还在时开始锁屏
    if (m_finished || m_password == NULL || m_confirm == NULL || m_preview.IsActive())
    {
        return;
    }
    CString pw = m_password->GetText();
    CString again = m_confirm->GetText();
    const std::wstring pwText(pw.GetString(), pw.GetLength());
    const std::wstring againText(again.GetString(), again.GetLength());
    const PasswordCheck check = CheckNewPassword(pwText, againText);
    switch (check)
    {
    //合规：记下结果，关窗并通知锁屏线程
    case kPasswordOk:
        break;

    //第一个框为空
    case kPasswordEmpty:
        ShowError(TR(L"请输入临时密码。"));
        m_password->SetFocus();
        return;

    //含有中文、全角等字符
    case kPasswordBadChar:
        ShowError(TR(L"密码只能包含英文字母、数字和符号。"));
        m_password->SetFocus();
        return;

    //太短
    case kPasswordTooShort:
        ShowError(TRF(L"密码至少 {0} 个字符。", kMinPasswordChars).c_str());
        m_password->SetFocus();
        return;

    //太长
    case kPasswordTooLong:
        ShowError(TRF(L"密码最多 {0} 个字符。", kMaxPasswordChars).c_str());
        m_password->SetFocus();
        return;

    //两次不一致
    default:
        ShowError(TR(L"两次输入的密码不一致。"));
        m_confirm->SetFocus();
        return;
    }

    m_resultPassword = pwText;
    m_resultChoice = ChoiceAt(m_animation != NULL ? m_animation->GetCurSel() : 0);
    m_finished = true;
    //两个输入框里的明文随窗口一起销毁；CString 的缓冲先清零
    if (!pw.IsEmpty())
    {
        ::SecureZeroMemory(pw.GetBuffer(), pw.GetLength() * sizeof(wchar_t));
        pw.ReleaseBuffer(0);
    }
    if (!again.IsEmpty())
    {
        ::SecureZeroMemory(again.GetBuffer(), again.GetLength() * sizeof(wchar_t));
        again.ReleaseBuffer(0);
    }
    m_password->SetText(_T(""));
    m_confirm->SetText(_T(""));
    LOG_INFO("lock setup dialog confirmed, choice %s.", ChoiceToToken(m_resultChoice));
    if (IsWindow())
    {
        DestroyWindow();
    }
    ::PostThreadMessage(::GetCurrentThreadId(), kThreadMsgSetupDone, 0, 0);
}

void CLockSetupDialog::Cancel()
{
    if (m_finished)
    {
        return;
    }
    m_finished = true;
    LOG_INFO("lock setup dialog cancelled.");
    //预览还开着（例如系统发来的关闭请求）时一并关掉
    m_preview.End();
    if (IsWindow())
    {
        DestroyWindow();
    }
    ::PostThreadMessage(::GetCurrentThreadId(), kThreadMsgSetupCancelled, 0, 0);
}

bool CLockSetupDialog::IsPreviewActive() const
{
    return m_preview.IsActive();
}

void CLockSetupDialog::RenderPreviewFrame()
{
    m_preview.RenderFrame();
}

void CLockSetupDialog::StartPreview()
{
    if (m_finished || m_preview.IsActive() || !IsWindow())
    {
        return;
    }
    const LockAnimationChoice choice = ChoiceAt(m_animation != NULL ? m_animation->GetCurSel() : 0);
    if (!m_preview.Begin(this, m_hWnd, choice))
    {
        ShowError(TR(L"无法打开预览。"));
        return;
    }
    ShowError(_T(""));
    //预览期间禁用本窗口：不能再点「预览」或「锁屏」，回车也不会触发按钮。预览窗口此时已取得前台，
    //禁用本窗口不会让前台落到别的程序上
    EnableWindow(FALSE);
    LOG_INFO("lock setup dialog: preview started, choice %s.", ChoiceToToken(choice));
}

void CLockSetupDialog::OnPreviewEnding()
{
    if (!IsWindow())
    {
        return;
    }
    EnableWindow(TRUE);
    BringToFront();
    LOG_INFO("lock setup dialog: preview finished, dialog re-enabled.");
}

LRESULT CLockSetupDialog::OnStartPreviewMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    StartPreview();
    return 0;
}

void CLockSetupDialog::TakeResult(std::wstring& password, LockAnimationChoice& choice)
{
    password = m_resultPassword;
    choice = m_resultChoice;
    if (!m_resultPassword.empty())
    {
        ::SecureZeroMemory(&m_resultPassword[0], m_resultPassword.size() * sizeof(wchar_t));
    }
    m_resultPassword.clear();
}

LRESULT CLockSetupDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    Cancel();
    return 0;
}

LRESULT CLockSetupDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdCancel)
    {
        Cancel();
        return 0;
    }
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdLock)
    {
        Confirm();
        return 0;
    }
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdPreview)
    {
        //本通知是在按钮自己的鼠标处理里同步发来的：先投给自己，等点击处理完再建全屏窗口、禁用本窗口
        PostMessage(kMsgSetupStartPreview);
        return 0;
    }
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && (pNotify->ctrlId == kIdPassword || pNotify->ctrlId == kIdConfirm))
    {
        //改了密码就清掉上一次的错误提示
        ShowError(_T(""));
    }
    bHandled = FALSE;
    return 0;
}

} // namespace flamingolock
