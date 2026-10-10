/**
 *  字幕编辑器的「自动替换表」窗口，见 CaptionReplaceDialog.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionReplaceDialog.h"

#include "CaptionFormDialog.h"   //RunCaptionModalLoop
#include "../Model/CaptionText.h"
#include "../../Source/Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"

namespace caption {

namespace {

//尺寸（逻辑像素，整窗含标题栏）
const int kDialogW = 480;
const int kDialogH = 460;
const int kTitleBarH = 32;
const int kPadding = 16;
const int kGap = 10;
const int kRowH = 30;
const int kItemH = 30;
const int kNoteH = 36;
const int kButtonW = 96;
const int kArrowW = 24;
//控件 id（避开标题栏按钮占用的 1~3）
const UINT kIdList = 10;
const UINT kIdFrom = 11;
const UINT kIdTo = 12;
const UINT kIdAdd = 13;
const UINT kIdRemove = 14;
const UINT kIdClose = 15;
//说明文字颜色
const COLORREF kNoteColor = RGB(110, 114, 122);

} // namespace

CaptionReplaceDialog::CaptionReplaceDialog(const CaptionReplaceTable& table)
    : m_table(table)
    , m_changed(false)
    , m_done(false)
    , m_list(NULL)
    , m_fromEdit(NULL)
    , m_toEdit(NULL)
{
}

CaptionReplaceDialog::~CaptionReplaceDialog()
{
    if (IsWindow())
        DestroyWindow();
}

const CaptionReplaceTable& CaptionReplaceDialog::Table() const
{
    return m_table;
}

bool CaptionReplaceDialog::Run(HWND owner)
{
    m_done = false;
    //有所有者的弹窗用 WS_OVERLAPPEDWINDOW，不能用 WS_POPUP：balloonui 按 GetParent 决定控件通知发给谁，有所有者的
    //WS_POPUP 窗口 GetParent 返回所有者，按钮的点击通知（含标题栏关闭按钮）会全部发到编辑器窗口，本窗口一条都收不到
    //（2026-10-06 导出窗口点「开始导出」没反应、也关不掉，即由此而来；外观由 DuiFrameWindow 自绘，不受影响）
    if (Create(owner, CWindow::rcDefault, TR(L"自动替换表"), WS_OVERLAPPEDWINDOW, WS_EX_DLGMODALFRAME) == NULL)
        return false;
    SetTitle(TR(L"自动替换表"));
    SetTitleBarHeight(kTitleBarH);
    //标题栏显示程序图标（开发约定文档「UI 约定」第 10 条）
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetButtons(false, false, true);
    SetResizable(false);
    ResizeClient(kDialogW, kDialogH);
    buildUi();
    RECT ownerRc = { 0, 0, 0, 0 };
    if (owner != NULL && ::GetWindowRect(owner, &ownerRc))
    {
        const int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - kDialogW) / 2;
        const int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - kDialogH) / 2;
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RunCaptionModalLoop(m_hWnd, owner, m_done);
    return m_changed;
}

void CaptionReplaceDialog::buildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> note(new balloonwjui::DuiLabel());
    note->SetText(TR(L"识别出的每一句会先按这张表替换常见的误识别，再进入字幕。英文按整词匹配，不区分大小写。"));
    note->SetTextColor(kNoteColor);
    note->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
    root->AddChild(std::move(note), balloonwjui::DuiLayout::Hint().Fixed(kNoteH));
    std::unique_ptr<balloonwjui::DuiListBox> list(new balloonwjui::DuiListBox());
    list->SetCtrlId(kIdList);
    list->SetItemHeight(kItemH);
    m_list = list.get();
    root->AddChild(std::move(list), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiEdit> from(new balloonwjui::DuiEdit());
    from->SetCtrlId(kIdFrom);
    from->SetPlaceholder(TR(L"原文（误识别的写法）"));
    from->SetVerticalCenter(true);
    from->SetShowBorder(true);
    m_fromEdit = from.get();
    row->AddChild(std::move(from), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiLabel> arrow(new balloonwjui::DuiLabel());
    arrow->SetText(L"→");
    arrow->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    row->AddChild(std::move(arrow), balloonwjui::DuiLayout::Hint().Fixed(kArrowW));
    std::unique_ptr<balloonwjui::DuiEdit> to(new balloonwjui::DuiEdit());
    to->SetCtrlId(kIdTo);
    to->SetPlaceholder(TR(L"替换为"));
    to->SetVerticalCenter(true);
    to->SetShowBorder(true);
    m_toEdit = to.get();
    row->AddChild(std::move(to), balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->SetGap(kGap);
    const UINT ids[] = { kIdAdd, kIdRemove, kIdClose };
    const wchar_t* texts[] = { TR(L"添加"), TR(L"删除所选"), TR(L"关闭") };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        if (i == 2)
            buttons->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                              balloonwjui::DuiLayout::Hint().Weight(1));
        std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
        b->SetText(texts[i]);
        b->SetVariant(i == 0 ? balloonwjui::DuiButton::Variant::Primary : balloonwjui::DuiButton::Variant::Default);
        b->SetCtrlId(ids[i]);
        buttons->AddChild(std::move(b), balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    }
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    SetClientContent(std::move(root));
    reloadList();
}

void CaptionReplaceDialog::reloadList()
{
    if (m_list == NULL)
        return;
    m_list->DeleteAllItems();
    const std::vector<CaptionReplaceRule>& rules = m_table.Rules();
    for (size_t i = 0; i < rules.size(); ++i)
    {
        const std::wstring text = rules[i].m_from + L"  →  " + rules[i].m_to;
        m_list->AddItem(text.c_str(), static_cast<LPARAM>(i));
    }
}

void CaptionReplaceDialog::addRule()
{
    const std::wstring from = NormalizeText(m_fromEdit->GetText().GetString());
    const std::wstring to = NormalizeText(m_toEdit->GetText().GetString());
    if (from.empty() || from == to)
        return;
    //原文已有：先删掉旧的，再按新的替换为文字加回去
    m_table.Remove(from);
    CaptionReplaceRule rule;
    rule.m_from = from;
    rule.m_to = to;
    if (!m_table.Add(rule))
        return;
    m_changed = true;
    m_fromEdit->SetText(L"");
    m_toEdit->SetText(L"");
    reloadList();
}

void CaptionReplaceDialog::removeSelected()
{
    if (m_list == NULL)
        return;
    const int sel = m_list->GetCurSel();
    const std::vector<CaptionReplaceRule>& rules = m_table.Rules();
    if (sel < 0 || sel >= static_cast<int>(rules.size()))
        return;
    const std::wstring from = rules[sel].m_from;
    if (m_table.Remove(from))
    {
        m_changed = true;
        reloadList();
    }
}

void CaptionReplaceDialog::finish()
{
    m_done = true;
    m_list = NULL;
    m_fromEdit = NULL;
    m_toEdit = NULL;
    if (IsWindow())
        DestroyWindow();
}

LRESULT CaptionReplaceDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //「替换为」里按回车：等同于「添加」
    if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ENTER) &&
        (n->ctrlId == kIdTo || n->ctrlId == kIdFrom))
    {
        addRule();
        return 0;
    }
    if (n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    //添加或更新一条
    case kIdAdd:
        addRule();
        return 0;
    //删除列表里选中的一条
    case kIdRemove:
        removeSelected();
        return 0;
    //关闭
    case kIdClose:
        finish();
        return 0;
    //其它控件
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CaptionReplaceDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    finish();
    return 0;
}

} // namespace caption
