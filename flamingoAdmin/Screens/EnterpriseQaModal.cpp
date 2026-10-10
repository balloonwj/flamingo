/**
 *  flamingoAdmin —— 新增 / 编辑企业问答弹窗实现，说明见 EnterpriseQaModal.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "Screens/EnterpriseQaModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"

#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/List/DuiListBox.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 弹窗几何（96 dpi 逻辑像素），与敏感词弹窗同一套风格 ----
const int kPanelW        = 560;  // 卡片宽度
const int kPanelH        = 580;  // 卡片高度
const int kHeaderH       = 56;   // 标题栏高度
const int kFooterH       = 64;   // 按钮栏高度
const int kPadH          = 20;   // 卡片左右内边距
const int kBackdropAlpha = 110;  // 遮罩不透明度（0-255）
const int kTitlePt       = 13;   // 标题字号（磅）
const int kLabelH        = 20;   // 字段标题行高
const int kEditH         = 32;   // 单行输入框与下拉框高度
const int kAnswerH       = 120;  // 答案输入框高度
const int kGroupListH    = 140;  // 群勾选列表高度
const int kFieldGap      = 12;   // 字段之间的纵向间距
const int kListGap       = 8;    // 应用范围下拉框与群列表之间的间距
const int kErrorH        = 22;   // 错误提示行高
const int kBtnH          = 34;   // 底部按钮高度
const int kBtnGap        = 8;    // 底部按钮间距
const int kBtnWCancel    = 72;   // 「取消」按钮宽度
const int kBtnWSave      = 88;   // 「保存」按钮宽度
const int kCloseBtnSize  = 28;   // 右上角关闭按钮边长
const int kScopeComboW   = 200;  // 应用范围下拉框宽度

} // namespace

EnterpriseQaModal::EnterpriseQaModal()
    : m_editingId(0)
    , m_busy(false)
    , m_errorTop(0)
    , m_question(nullptr)
    , m_answer(nullptr)
    , m_scope(nullptr)
    , m_groupList(nullptr)
    , m_btnClose(nullptr)
    , m_btnCancel(nullptr)
    , m_btnSave(nullptr)
{
    SetCtrlId(kIdQaModal);
    ::SetRectEmpty(&m_panelRect);

    auto question = std::make_unique<DuiEdit>();
    question->SetCtrlId(kIdQaModalQuestion);
    question->SetPlaceholder(_T("例如：报销流程怎么走？"));
    question->SetShowBorder(true);
    question->SetBgColor(theme::kInk0);
    question->SetMaxLength(qaapi::kQaQuestionMaxChars);
    m_question = question.get();
    AddChild(std::move(question));

    auto answer = std::make_unique<DuiEdit>();
    answer->SetCtrlId(kIdQaModalAnswer);
    answer->SetPlaceholder(_T("可以写网址，群成员点了用浏览器打开"));
    answer->SetShowBorder(true);
    answer->SetBgColor(theme::kInk0);
    answer->SetMultiLine(true);
    answer->SetWordWrap(true);
    answer->SetMaxLength(qaapi::kQaAnswerMaxChars);
    m_answer = answer.get();
    AddChild(std::move(answer));

    auto scope = std::make_unique<DuiComboBox>();
    scope->SetCtrlId(kIdQaModalScope);
    scope->AddString(_T("全部群"));
    scope->AddString(_T("所选的群"));
    scope->SetCurSel(qaapi::kQaScopeAll, false);
    m_scope = scope.get();
    AddChild(std::move(scope));

    auto groups = std::make_unique<DuiListBox>();
    groups->SetCtrlId(kIdQaModalGroups);
    groups->SetShowCheckboxes(true);
    groups->SetEnabled(false);
    m_groupList = groups.get();
    AddChild(std::move(groups));

    auto closeBtn = std::make_unique<DuiButton>();
    closeBtn->SetCtrlId(kIdQaModalClose);
    closeBtn->SetText(_T("×"));
    closeBtn->SetVariant(DuiButton::Variant::Ghost);
    closeBtn->SetTextPointSize(theme::kFontSizeHeading);
    m_btnClose = closeBtn.get();
    AddChild(std::move(closeBtn));

    auto cancelBtn = std::make_unique<DuiButton>();
    cancelBtn->SetCtrlId(kIdQaModalCancel);
    cancelBtn->SetText(_T("取消"));
    cancelBtn->SetVariant(DuiButton::Variant::Default);
    cancelBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnCancel = cancelBtn.get();
    AddChild(std::move(cancelBtn));

    auto saveBtn = std::make_unique<DuiButton>();
    saveBtn->SetCtrlId(kIdQaModalSave);
    saveBtn->SetText(_T("保存"));
    saveBtn->SetVariant(DuiButton::Variant::Primary);
    saveBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnSave = saveBtn.get();
    AddChild(std::move(saveBtn));
}

void EnterpriseQaModal::ShowForAdd(const std::vector<qaapi::QaGroup>& groups)
{
    m_editingId = 0;
    if (m_question != nullptr)
        m_question->SetText(_T(""));
    if (m_answer != nullptr)
        m_answer->SetText(_T(""));
    m_groups = groups;
    FillGroupList(std::set<long long>());
    SetScope(qaapi::kQaScopeAll);
    SetError(CString());
    SetBusy(false);
}

void EnterpriseQaModal::ShowForEdit(const qaapi::QaItem& item, const std::vector<qaapi::QaGroup>& groups)
{
    m_editingId = item.m_id;
    if (m_question != nullptr)
        m_question->SetText(item.m_question);
    if (m_answer != nullptr)
        m_answer->SetText(item.m_answer);
    m_groups = groups;
    FillGroupList(std::set<long long>(item.m_groupIds.begin(), item.m_groupIds.end()));
    SetScope(item.m_scope);
    SetError(CString());
    SetBusy(false);
}

void EnterpriseQaModal::SetGroups(const std::vector<qaapi::QaGroup>& groups)
{
    const std::set<long long> checked = CheckedGroups();
    m_groups = groups;
    FillGroupList(checked);
}

void EnterpriseQaModal::FillGroupList(const std::set<long long>& checked)
{
    if (m_groupList == nullptr)
        return;
    m_groupList->DeleteAllItems();
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        const int row = m_groupList->AddItem(m_groups[i].m_name, static_cast<LPARAM>(i));
        if (checked.count(m_groups[i].m_id) != 0)
            m_groupList->SetItemChecked(row, true, false);
    }
    m_groupList->Invalidate();
}

std::set<long long> EnterpriseQaModal::CheckedGroups() const
{
    std::set<long long> checked;
    if (m_groupList == nullptr)
        return checked;
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        if (m_groupList->IsItemChecked(static_cast<int>(i)))
            checked.insert(m_groups[i].m_id);
    }
    return checked;
}

void EnterpriseQaModal::SetScope(int scope)
{
    if (m_scope != nullptr)
        m_scope->SetCurSel(scope == qaapi::kQaScopeSelected ? qaapi::kQaScopeSelected : qaapi::kQaScopeAll, false);
    OnScopeChanged();
}

void EnterpriseQaModal::OnScopeChanged()
{
    const bool selected = (m_scope != nullptr && m_scope->GetCurSel() == qaapi::kQaScopeSelected);
    if (m_groupList != nullptr)
    {
        m_groupList->SetEnabled(selected);
        m_groupList->Invalidate();
    }
}

qaapi::QaItem EnterpriseQaModal::GetItem() const
{
    qaapi::QaItem item;
    item.m_id = m_editingId;
    item.m_question = (m_question != nullptr) ? m_question->GetText() : CString();
    item.m_question.Trim();
    item.m_answer = (m_answer != nullptr) ? m_answer->GetText() : CString();
    item.m_answer.Trim();
    item.m_scope = (m_scope != nullptr && m_scope->GetCurSel() == qaapi::kQaScopeSelected)
                   ? qaapi::kQaScopeSelected : qaapi::kQaScopeAll;
    if (item.m_scope == qaapi::kQaScopeSelected)
    {
        const std::set<long long> checked = CheckedGroups();
        //按群列表的顺序给出，便于核对
        for (size_t i = 0; i < m_groups.size(); ++i)
        {
            if (checked.count(m_groups[i].m_id) != 0)
                item.m_groupIds.push_back(m_groups[i].m_id);
        }
    }
    return item;
}

bool EnterpriseQaModal::Validate(CString* error) const
{
    return qaapi::ValidateQa(GetItem(), error);
}

void EnterpriseQaModal::SetError(const CString& text)
{
    if (m_error == text)
        return;
    m_error = text;
    Invalidate();
}

void EnterpriseQaModal::SetBusy(bool busy)
{
    m_busy = busy;
    if (m_btnSave != nullptr)
    {
        m_btnSave->SetEnabled(!busy);
        m_btnSave->SetText(busy ? _T("保存中…") : _T("保存"));
    }
}

int EnterpriseQaModal::GroupRowCount() const
{
    return static_cast<int>(m_groups.size());
}

bool EnterpriseQaModal::IsGroupRowChecked(int index) const
{
    if (m_groupList == nullptr || index < 0 || index >= GroupRowCount())
        return false;
    return m_groupList->IsItemChecked(index);
}

void EnterpriseQaModal::SetGroupRowChecked(int index, bool checked)
{
    if (m_groupList == nullptr || index < 0 || index >= GroupRowCount())
        return;
    m_groupList->SetItemChecked(index, checked, false);
}

void EnterpriseQaModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect = RECT{ cx - kPanelW / 2, cy - kPanelH / 2, cx + kPanelW / 2, cy + kPanelH / 2 };

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;

    if (m_btnClose != nullptr)
    {
        m_btnClose->ForceLayout(RECT{ R - kCloseBtnSize, m_panelRect.top + (kHeaderH - kCloseBtnSize) / 2,
                                      R, m_panelRect.top + (kHeaderH + kCloseBtnSize) / 2 });
    }

    //自上而下：问题 → 答案 → 应用范围（下拉框 + 群列表）→ 错误提示行
    int y = m_panelRect.top + kHeaderH + kFieldGap + kLabelH;
    if (m_question != nullptr)
        m_question->ForceLayout(RECT{ L, y, R, y + kEditH });
    y += kEditH + kFieldGap + kLabelH;
    if (m_answer != nullptr)
        m_answer->ForceLayout(RECT{ L, y, R, y + kAnswerH });
    y += kAnswerH + kFieldGap + kLabelH;
    if (m_scope != nullptr)
        m_scope->ForceLayout(RECT{ L, y, L + kScopeComboW, y + kEditH });
    y += kEditH + kListGap;
    if (m_groupList != nullptr)
        m_groupList->ForceLayout(RECT{ L, y, R, y + kGroupListH });
    m_errorTop = y + kGroupListH + kFieldGap / 2;

    //底部按钮：自右向左依次是 保存 / 取消
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    if (m_btnSave != nullptr)
        m_btnSave->ForceLayout(RECT{ R - kBtnWSave, by, R, by + kBtnH });
    if (m_btnCancel != nullptr)
        m_btnCancel->ForceLayout(RECT{ R - kBtnWSave - kBtnGap - kBtnWCancel, by, R - kBtnWSave - kBtnGap, by + kBtnH });
}

void EnterpriseQaModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
        return;

    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;

    ui::DrawTextLine(hdc, IsEditing() ? _T("编辑企业问答") : _T("新增企业问答"),
                     RECT{ L, m_panelRect.top, R - kCloseBtnSize, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kTitlePt, true), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH, kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH, kPanelW, 1, theme::kInk100);

    //字段标题：与 Layout 里各输入框的位置一一对应
    HFONT labelFont = ui::UiFont(theme::kFontSizeSmall, false);
    const UINT labelFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    CString questionLabel;
    questionLabel.Format(_T("问题（最多 %d 个字）"), qaapi::kQaQuestionMaxChars);
    CString answerLabel;
    answerLabel.Format(_T("答案（最多 %d 个字）"), qaapi::kQaAnswerMaxChars);
    int labelTop = m_panelRect.top + kHeaderH + kFieldGap;
    ui::DrawTextLine(hdc, questionLabel, RECT{ L, labelTop, R, labelTop + kLabelH }, theme::kInk400, labelFont, labelFlags);
    labelTop += kLabelH + kEditH + kFieldGap;
    ui::DrawTextLine(hdc, answerLabel, RECT{ L, labelTop, R, labelTop + kLabelH }, theme::kInk400, labelFont, labelFlags);
    labelTop += kLabelH + kAnswerH + kFieldGap;
    ui::DrawTextLine(hdc, _T("应用范围（应用到的群里，成员在群聊「问答」页签看到它，只读）"),
                     RECT{ L, labelTop, R, labelTop + kLabelH }, theme::kInk400, labelFont, labelFlags);

    //红字错误提示
    if (!m_error.IsEmpty())
    {
        ui::DrawTextLine(hdc, m_error, RECT{ L, m_errorTop, R, m_errorTop + kErrorH }, theme::kColorDanger,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    //子控件（输入框、下拉框、列表、按钮）画在最上层
    DuiControl::OnPaint(hdc, rcDirty);
}

} // namespace flamingoadmin
