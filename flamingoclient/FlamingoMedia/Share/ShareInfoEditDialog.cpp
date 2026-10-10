/**
 *  屏幕共享的「修改标题与描述」小窗，说明见 ShareInfoEditDialog.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareInfoEditDialog.h"

#include <memory>
#include "ShareMessages.h"
#include "ShareRoomPolicy.h"   // NormalizeShareInfo、kShareTitleMaxChars、kShareDescMaxChars
#include "ShareSources.h"      // ExcludeWindowFromCapture
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//整窗宽、标题栏高（逻辑像素）
const int kDlgW = 420;
const int kTitleBarH = 28;
//内容区内边距、控件间距、输入框与按钮行的高度、按钮宽、输入框左侧「标题」「描述」文字的宽度
const int kPad = 10;
const int kGap = 8;
const int kRowH = 30;
const int kBtnW = 72;
const int kLabelW = 36;
//说明行的高度
const int kHintH = 20;
//整窗高：标题栏 + 上下内边距 + 说明行 + 标题、描述、按钮三行 + 四处间距
const int kDlgH = kTitleBarH + kPad * 2 + kHintH + kRowH * 3 + kGap * 3;
//说明文字的颜色：普通说明为深灰，提醒为橙红，与加入密码输入框一致
const COLORREF kHintNormalColor = RGB(90, 96, 106);
const COLORREF kHintErrorColor = RGB(200, 80, 40);
//普通的填写说明与输入不合格时的提示
const wchar_t* const kNormalHint = TR_NOOP(L"标题必填，最多 30 字；描述选填，最多 100 字");
const wchar_t* const kInvalidHint = TR_NOOP(L"标题不能为空，最多 30 字；描述最多 100 字");

} // namespace

CShareInfoEditDialog::CShareInfoEditDialog()
    : m_notifyWnd(NULL)
    , m_titleEdit(NULL)
    , m_descEdit(NULL)
    , m_hintLabel(NULL)
    , m_finished(true)
{
}

CShareInfoEditDialog::~CShareInfoEditDialog()
{
    Close();
}

bool CShareInfoEditDialog::IsShown() const
{
    return IsWindow() != FALSE;
}

void CShareInfoEditDialog::Open(HWND monitorRef, HWND notifyWnd, const std::wstring& title, const std::wstring& desc,
                                const std::wstring& errorHint)
{
    //已打开时先按取消关闭旧的
    if (IsWindow())
        finish(false);
    m_notifyWnd = notifyWnd;
    m_finished = false;
    //置顶、不进任务栏；不设所有者窗口，按钮通知才会发回本窗口（见头文件说明）
    const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    if (Create(NULL, CWindow::rcDefault, TR(L"修改标题与描述"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR("screenshare: create info edit dialog failed.");
        m_finished = true;
        return;
    }
    SetTitle(TR(L"修改标题与描述"));
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);
    SetResizable(false);

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPad, kPad, kPad, kPad);
    root->SetGap(kGap);
    //说明行：平时是填写说明，服务端拒绝或输入不合格时是提醒色的提示
    std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
    hint->SetText(errorHint.empty() ? TR(kNormalHint) : errorHint.c_str());
    hint->SetTextColor(errorHint.empty() ? kHintNormalColor : kHintErrorColor);
    hint->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    m_hintLabel = hint.get();
    root->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(kHintH));

    //标题、描述两行：左侧文字加输入框，与选择窗里的写法一致
    const wchar_t* const labels[] = { TR(L"标题"), TR(L"描述") };
    const UINT ids[] = { kIdTitleEdit, kIdDescEdit };
    const int maxChars[] = { static_cast<int>(kShareTitleMaxChars), static_cast<int>(kShareDescMaxChars) };
    const wchar_t* const placeholders[] = { TR(L"共享标题（必填）"), TR(L"描述（选填）") };
    const std::wstring* const values[] = { &title, &desc };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        std::unique_ptr<balloonwjui::DuiHBox> line(new balloonwjui::DuiHBox());
        line->SetGap(kGap);
        std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
        label->SetText(labels[i]);
        label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        line->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Fixed(kLabelW));
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetCtrlId(ids[i]);
        edit->SetMaxLength(maxChars[i]);
        edit->SetVerticalCenter(true);
        edit->SetShowBorder(true);
        edit->SetPlaceholder(placeholders[i]);
        //还没挂进控件树，用不发通知的写法填初始内容（与选择窗相同）
        edit->SetTextNoNotify(values[i]->c_str());
        if (ids[i] == kIdTitleEdit)
            m_titleEdit = edit.get();
        else
            m_descEdit = edit.get();
        line->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Weight(1));
        root->AddChild(std::move(line), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }

    //按钮行：右对齐的「取消」「保存」
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
    std::unique_ptr<balloonwjui::DuiButton> save(new balloonwjui::DuiButton());
    save->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    save->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    save->SetText(TR(L"保存"));
    save->SetCtrlId(kIdSave);
    row->AddChild(std::move(save), balloonwjui::DuiLayout::Hint().Fixed(kBtnW));
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    SetClientContent(std::move(root));

    //发起人的桌面正被共享：本窗口不出现在观看者的画面里
    ExcludeWindowFromCapture(m_hWnd);
    //居中在定位窗口所在显示器的工作区上
    const HWND monitorOf = (monitorRef != NULL && ::IsWindow(monitorRef)) ? monitorRef : NULL;
    HMONITOR mon = ::MonitorFromWindow(monitorOf, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - kDlgW) / 2;
    const int y = work.top + ((work.bottom - work.top) - kDlgH) / 2;
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, kDlgW, kDlgH, SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hWnd);
    m_titleEdit->SetFocus();
    LOG_INFO("screenshare: info edit dialog opened, with error hint=%d.", errorHint.empty() ? 0 : 1);
}

void CShareInfoEditDialog::finish(bool ok)
{
    if (m_finished)
        return;
    ShareInfoEditResult* result = NULL;
    if (ok && m_titleEdit != NULL && m_descEdit != NULL)
    {
        std::wstring title = m_titleEdit->GetText().GetString();
        std::wstring desc = m_descEdit->GetText().GetString();
        //不合格：窗口不关，说明行改为提醒
        if (!NormalizeShareInfo(title, desc))
        {
            if (m_hintLabel != NULL)
            {
                m_hintLabel->SetText(TR(kInvalidHint));
                m_hintLabel->SetTextColor(kHintErrorColor);
                m_hintLabel->Invalidate();
            }
            return;
        }
        result = new ShareInfoEditResult();
        result->m_title = title;
        result->m_desc = desc;
    }
    m_finished = true;
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
    {
        if (!::PostMessage(m_notifyWnd, kMsgShareInfoEditResult, 0, reinterpret_cast<LPARAM>(result)))
            delete result;
    }
    else
    {
        delete result;
    }
    m_titleEdit = NULL;
    m_descEdit = NULL;
    m_hintLabel = NULL;
    if (IsWindow())
        DestroyWindow();
}

void CShareInfoEditDialog::Close()
{
    m_finished = true;
    m_titleEdit = NULL;
    m_descEdit = NULL;
    m_hintLabel = NULL;
    if (IsWindow())
        DestroyWindow();
}

LRESULT CShareInfoEditDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    finish(false);
    return 0;
}

LRESULT CShareInfoEditDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    const bool fromEdit = (n->ctrlId == static_cast<UINT>(kIdTitleEdit) || n->ctrlId == static_cast<UINT>(kIdDescEdit));
    //输入框里按了回车：保存
    if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ENTER) && fromEdit)
    {
        finish(true);
        return 0;
    }
    //输入框里按了 Esc：取消
    if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE) && fromEdit)
    {
        finish(false);
        return 0;
    }
    //「保存」
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdSave))
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
