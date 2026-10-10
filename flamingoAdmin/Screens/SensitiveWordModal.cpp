/**
 *  flamingoAdmin —— 添加 / 编辑敏感词弹窗实现，说明见 SensitiveWordModal.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Screens/SensitiveWordModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Util/SensitiveWordText.h"

#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 弹窗几何（96 dpi 逻辑像素），与客户端版本弹窗同一套风格 ----
const int kPanelW        = 480;  // 卡片宽度
const int kPanelH        = 372;  // 卡片高度
const int kHeaderH       = 56;   // 标题栏高度
const int kFooterH       = 64;   // 按钮栏高度
const int kPadH          = 20;   // 卡片左右内边距
const int kBackdropAlpha = 110;  // 遮罩不透明度（0-255）
const int kTitlePt       = 13;   // 标题字号（磅）
const int kLabelH        = 20;   // 字段标题行高
const int kEditH         = 32;   // 输入框与下拉框高度
const int kFieldGap      = 12;   // 字段之间的纵向间距
const int kErrorH        = 22;   // 错误提示行高
const int kBtnH          = 34;   // 底部按钮高度
const int kBtnGap        = 8;    // 底部按钮间距
const int kBtnWCancel    = 72;   // 「取消」按钮宽度
const int kBtnWSave      = 88;   // 「保存」按钮宽度
const int kCloseBtnSize  = 28;   // 右上角关闭按钮边长

// 分类下拉框里「其他」的下标：下拉框按分类取值 1～6 依次排列
const int kOtherIndex = swtext::kCategoryOther - 1;

// 按 UTF-16 码元数估算字符数：词条与备注里的字绝大多数在基本平面内，与服务端按 Unicode 字符计数一致；
// 少数四字节字符（多数 emoji）在这里算两个，最多让本地校验略严，服务端仍以自己的计数为准
int CharCount(const CString& text)
{
    return text.GetLength();
}

} // namespace

SensitiveWordModal::SensitiveWordModal()
    : m_editingId(0)
    , m_busy(false)
    , m_errorTop(0)
    , m_editWord(nullptr)
    , m_category(nullptr)
    , m_editRemark(nullptr)
    , m_btnClose(nullptr)
    , m_btnCancel(nullptr)
    , m_btnSave(nullptr)
{
    SetCtrlId(kIdSwModal);
    ::SetRectEmpty(&m_panelRect);

    auto word = std::make_unique<DuiEdit>();
    word->SetCtrlId(kIdSwModalWord);
    word->SetPlaceholder(_T("例如：违禁品（忽略大小写、全半角、空白与符号）"));
    word->SetShowBorder(true);
    word->SetBgColor(theme::kInk0);
    word->SetMaxLength(swtext::kWordMaxChars);
    m_editWord = word.get();
    AddChild(std::move(word));

    auto category = std::make_unique<DuiComboBox>();
    category->SetCtrlId(kIdSwModalCategory);
    for (int c = 1; c <= swtext::kCategoryCount; ++c)
        category->AddString(swtext::CategoryName(c));
    category->SetCurSel(kOtherIndex, false);
    m_category = category.get();
    AddChild(std::move(category));

    auto remark = std::make_unique<DuiEdit>();
    remark->SetCtrlId(kIdSwModalRemark);
    remark->SetPlaceholder(_T("选填，例如：来源、添加原因"));
    remark->SetShowBorder(true);
    remark->SetBgColor(theme::kInk0);
    remark->SetMaxLength(swtext::kRemarkMaxChars);
    m_editRemark = remark.get();
    AddChild(std::move(remark));

    auto closeBtn = std::make_unique<DuiButton>();
    closeBtn->SetCtrlId(kIdSwModalClose);
    closeBtn->SetText(_T("×"));
    closeBtn->SetVariant(DuiButton::Variant::Ghost);
    closeBtn->SetTextPointSize(theme::kFontSizeHeading);
    m_btnClose = closeBtn.get();
    AddChild(std::move(closeBtn));

    auto cancelBtn = std::make_unique<DuiButton>();
    cancelBtn->SetCtrlId(kIdSwModalCancel);
    cancelBtn->SetText(_T("取消"));
    cancelBtn->SetVariant(DuiButton::Variant::Default);
    cancelBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnCancel = cancelBtn.get();
    AddChild(std::move(cancelBtn));

    auto saveBtn = std::make_unique<DuiButton>();
    saveBtn->SetCtrlId(kIdSwModalSave);
    saveBtn->SetText(_T("保存"));
    saveBtn->SetVariant(DuiButton::Variant::Primary);
    saveBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnSave = saveBtn.get();
    AddChild(std::move(saveBtn));
}

void SensitiveWordModal::ShowForAdd()
{
    m_editingId = 0;
    if (m_editWord != nullptr)
        m_editWord->SetText(_T(""));
    if (m_category != nullptr)
        m_category->SetCurSel(kOtherIndex, false);
    if (m_editRemark != nullptr)
        m_editRemark->SetText(_T(""));
    SetError(CString());
    SetBusy(false);
}

void SensitiveWordModal::ShowForEdit(const adminapi::SensitiveWordItem& item)
{
    m_editingId = item.m_id;
    if (m_editWord != nullptr)
        m_editWord->SetText(item.m_word);
    if (m_category != nullptr)
    {
        const int category = (item.m_category >= 1 && item.m_category <= swtext::kCategoryCount)
                                 ? item.m_category : swtext::kCategoryOther;
        m_category->SetCurSel(category - 1, false);
    }
    if (m_editRemark != nullptr)
        m_editRemark->SetText(item.m_remark);
    SetError(CString());
    SetBusy(false);
}

bool SensitiveWordModal::Validate(CString* error) const
{
    CString reason;
    const CString word = GetWord();
    if (word.IsEmpty())
        reason = _T("请输入词条");
    else if (CharCount(word) > swtext::kWordMaxChars)
        reason.Format(_T("词条不能超过 %d 个字"), swtext::kWordMaxChars);
    else if (CharCount(GetRemark()) > swtext::kRemarkMaxChars)
        reason.Format(_T("备注不能超过 %d 个字"), swtext::kRemarkMaxChars);

    if (error != nullptr)
        *error = reason;
    return reason.IsEmpty();
}

CString SensitiveWordModal::GetWord() const
{
    CString text = (m_editWord != nullptr) ? m_editWord->GetText() : CString();
    text.Trim();
    return text;
}

int SensitiveWordModal::GetCategory() const
{
    const int index = (m_category != nullptr) ? m_category->GetCurSel() : -1;
    if (index < 0 || index >= swtext::kCategoryCount)
        return swtext::kCategoryOther;
    return index + 1;
}

CString SensitiveWordModal::GetRemark() const
{
    CString text = (m_editRemark != nullptr) ? m_editRemark->GetText() : CString();
    text.Trim();
    return text;
}

void SensitiveWordModal::SetError(const CString& text)
{
    if (m_error == text)
        return;
    m_error = text;
    Invalidate();
}

void SensitiveWordModal::SetBusy(bool busy)
{
    m_busy = busy;
    if (m_btnSave != nullptr)
    {
        m_btnSave->SetEnabled(!busy);
        m_btnSave->SetText(busy ? _T("保存中…") : _T("保存"));
    }
}

void SensitiveWordModal::Layout(const RECT& rcAvail)
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

    //自上而下：词条 → 分类 → 备注 → 错误提示行
    int y = m_panelRect.top + kHeaderH + kFieldGap + kLabelH;
    if (m_editWord != nullptr)
        m_editWord->ForceLayout(RECT{ L, y, R, y + kEditH });
    y += kEditH + kFieldGap + kLabelH;
    if (m_category != nullptr)
        m_category->ForceLayout(RECT{ L, y, R, y + kEditH });
    y += kEditH + kFieldGap + kLabelH;
    if (m_editRemark != nullptr)
        m_editRemark->ForceLayout(RECT{ L, y, R, y + kEditH });
    m_errorTop = y + kEditH + kFieldGap / 2;

    //底部按钮：自右向左依次是 保存 / 取消
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    if (m_btnSave != nullptr)
        m_btnSave->ForceLayout(RECT{ R - kBtnWSave, by, R, by + kBtnH });
    if (m_btnCancel != nullptr)
        m_btnCancel->ForceLayout(RECT{ R - kBtnWSave - kBtnGap - kBtnWCancel, by, R - kBtnWSave - kBtnGap, by + kBtnH });
}

void SensitiveWordModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
        return;

    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;

    ui::DrawTextLine(hdc, IsEditing() ? _T("编辑敏感词") : _T("添加敏感词"),
                     RECT{ L, m_panelRect.top, R - kCloseBtnSize, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kTitlePt, true), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH, kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH, kPanelW, 1, theme::kInk100);

    //字段标题
    HFONT labelFont = ui::UiFont(theme::kFontSizeSmall, false);
    CString wordLabel;
    wordLabel.Format(_T("词条（最多 %d 个字）"), swtext::kWordMaxChars);
    CString remarkLabel;
    remarkLabel.Format(_T("备注（选填，最多 %d 个字）"), swtext::kRemarkMaxChars);
    const LPCTSTR labels[] = { wordLabel.GetString(), _T("分类"), remarkLabel.GetString() };
    int labelTop = m_panelRect.top + kHeaderH + kFieldGap;
    for (int i = 0; i < 3; ++i)
    {
        ui::DrawTextLine(hdc, labels[i], RECT{ L, labelTop, R, labelTop + kLabelH }, theme::kInk400, labelFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        labelTop += kLabelH + kEditH + kFieldGap;
    }

    //红字错误提示
    if (!m_error.IsEmpty())
    {
        ui::DrawTextLine(hdc, m_error, RECT{ L, m_errorTop, R, m_errorTop + kErrorH }, theme::kColorDanger,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    //子控件（输入框、下拉框、按钮）画在最上层
    DuiControl::OnPaint(hdc, rcDirty);
}

} // namespace flamingoadmin
