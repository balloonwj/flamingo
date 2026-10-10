/**
 *  flamingoAdmin —— 批量导入敏感词弹窗实现，说明见 SensitiveImportModal.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Screens/SensitiveImportModal.h"
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

// ---- 弹窗几何（96 dpi 逻辑像素），与添加 / 编辑敏感词弹窗同一套风格 ----
const int kPanelW         = 600;  // 卡片宽度
const int kPanelH         = 520;  // 卡片高度
const int kHeaderH        = 56;   // 标题栏高度
const int kFooterH        = 64;   // 按钮栏高度
const int kPadH           = 20;   // 卡片左右内边距
const int kBackdropAlpha  = 110;  // 遮罩不透明度（0-255）
const int kTitlePt        = 13;   // 标题字号（磅）
const int kHintH          = 40;   // 格式说明区高度（两行小字）
const int kGap            = 10;   // 区块之间的纵向间距
const int kTextH          = 250;  // 多行输入框高度
const int kRowH           = 32;   // 默认分类那一行（下拉框、按钮）的高度
const int kCategoryLabelW = 72;   // 「默认分类」标签宽度
const int kCategoryW      = 180;  // 默认分类下拉框宽度
const int kReadFileW      = 120;  // 「从文件读取」按钮宽度
const int kStatusH        = 22;   // 状态行高度
const int kBtnH           = 34;   // 底部按钮高度
const int kBtnGap         = 8;    // 底部按钮间距
const int kBtnWCancel     = 72;   // 「取消」按钮宽度
const int kBtnWSubmit     = 100;  // 「开始导入」/「完成」按钮宽度
const int kCloseBtnSize   = 28;   // 右上角关闭按钮边长

// 分类下拉框里「其他」的下标：下拉框按分类取值 1～6 依次排列
const int kOtherIndex = swtext::kCategoryOther - 1;

// 主按钮在两种状态下的文字
const TCHAR kSubmitText[] = _T("开始导入");
const TCHAR kDoneText[]   = _T("完成");

} // namespace

SensitiveImportModal::SensitiveImportModal()
    : m_statusIsError(false)
    , m_importing(false)
    , m_done(false)
    , m_statusTop(0)
    , m_editText(nullptr)
    , m_category(nullptr)
    , m_btnReadFile(nullptr)
    , m_btnClose(nullptr)
    , m_btnCancel(nullptr)
    , m_btnSubmit(nullptr)
{
    SetCtrlId(kIdSwImpModal);
    ::SetRectEmpty(&m_panelRect);

    auto text = std::make_unique<DuiEdit>();
    text->SetCtrlId(kIdSwImpText);
    //多行：按框宽自动换行，内容超出框高时出现竖直滚动条；不限长度，2 MB 的文件读进来也放得下
    text->SetMultiLine(true);
    text->SetWordWrap(true);
    text->SetPlaceholder(_T("在这里粘贴，或点「从文件读取」"));
    text->SetShowBorder(true);
    text->SetBgColor(theme::kInk0);
    m_editText = text.get();
    AddChild(std::move(text));

    auto category = std::make_unique<DuiComboBox>();
    category->SetCtrlId(kIdSwImpCategory);
    for (int c = 1; c <= swtext::kCategoryCount; ++c)
        category->AddString(swtext::CategoryName(c));
    category->SetCurSel(kOtherIndex, false);
    m_category = category.get();
    AddChild(std::move(category));

    auto readFile = std::make_unique<DuiButton>();
    readFile->SetCtrlId(kIdSwImpReadFile);
    readFile->SetText(_T("从文件读取"));
    readFile->SetVariant(DuiButton::Variant::Default);
    readFile->SetTextPointSize(theme::kFontSizeNormal);
    m_btnReadFile = readFile.get();
    AddChild(std::move(readFile));

    auto closeBtn = std::make_unique<DuiButton>();
    closeBtn->SetCtrlId(kIdSwImpClose);
    closeBtn->SetText(_T("×"));
    closeBtn->SetVariant(DuiButton::Variant::Ghost);
    closeBtn->SetTextPointSize(theme::kFontSizeHeading);
    m_btnClose = closeBtn.get();
    AddChild(std::move(closeBtn));

    auto cancelBtn = std::make_unique<DuiButton>();
    cancelBtn->SetCtrlId(kIdSwImpCancel);
    cancelBtn->SetText(_T("取消"));
    cancelBtn->SetVariant(DuiButton::Variant::Default);
    cancelBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnCancel = cancelBtn.get();
    AddChild(std::move(cancelBtn));

    auto submitBtn = std::make_unique<DuiButton>();
    submitBtn->SetCtrlId(kIdSwImpSubmit);
    submitBtn->SetText(kSubmitText);
    submitBtn->SetVariant(DuiButton::Variant::Primary);
    submitBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnSubmit = submitBtn.get();
    AddChild(std::move(submitBtn));
}

void SensitiveImportModal::ShowFresh()
{
    m_done = false;
    SetImporting(false);
    if (m_editText != nullptr)
        m_editText->SetText(_T(""));
    if (m_category != nullptr)
        m_category->SetCurSel(kOtherIndex, false);
    if (m_btnSubmit != nullptr)
        m_btnSubmit->SetText(kSubmitText);
    if (m_btnCancel != nullptr)
        m_btnCancel->SetVisible(true);
    if (m_btnReadFile != nullptr)
        m_btnReadFile->SetVisible(true);
    SetStatus(CString(), false);
}

CString SensitiveImportModal::GetText() const
{
    return (m_editText != nullptr) ? m_editText->GetText() : CString();
}

void SensitiveImportModal::SetText(const CString& text)
{
    if (m_editText != nullptr)
        m_editText->SetText(text);
}

int SensitiveImportModal::GetDefaultCategory() const
{
    const int index = (m_category != nullptr) ? m_category->GetCurSel() : -1;
    if (index < 0 || index >= swtext::kCategoryCount)
        return swtext::kCategoryOther;
    return index + 1;
}

void SensitiveImportModal::SetStatus(const CString& text, bool isError)
{
    if (m_status == text && m_statusIsError == isError)
        return;
    m_status = text;
    m_statusIsError = isError;
    Invalidate();
}

void SensitiveImportModal::SetImporting(bool importing)
{
    m_importing = importing;
    //导入中不许改内容、不许关闭：批次正在依次提交，中途关掉弹窗后结果就没有地方显示了。
    //显示结果之后内容同样只读，分类也不再可选
    const bool enabled = !importing;
    if (m_editText != nullptr)
        m_editText->SetReadOnly(importing || m_done);
    if (m_category != nullptr)
        m_category->SetEnabled(enabled && !m_done);
    if (m_btnSubmit != nullptr)
        m_btnSubmit->SetEnabled(enabled);
    if (m_btnCancel != nullptr)
        m_btnCancel->SetEnabled(enabled);
    if (m_btnReadFile != nullptr)
        m_btnReadFile->SetEnabled(enabled);
    if (m_btnClose != nullptr)
        m_btnClose->SetEnabled(enabled);
}

void SensitiveImportModal::ShowResult(const CString& summary, bool isError)
{
    m_done = true;
    SetImporting(false);
    if (m_editText != nullptr)
        m_editText->SetText(summary);
    if (m_btnSubmit != nullptr)
        m_btnSubmit->SetText(kDoneText);
    //结果只能看，不能在这里重新导入：「取消」与「从文件读取」都没有意义了
    if (m_btnCancel != nullptr)
        m_btnCancel->SetVisible(false);
    if (m_btnReadFile != nullptr)
        m_btnReadFile->SetVisible(false);
    SetStatus(isError ? _T("导入中途失败") : _T("导入完成"), isError);
}

void SensitiveImportModal::Layout(const RECT& rcAvail)
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

    //自上而下：格式说明 → 多行输入框 → 默认分类与「从文件读取」 → 状态行
    int y = m_panelRect.top + kHeaderH + kGap + kHintH + kGap;
    if (m_editText != nullptr)
        m_editText->ForceLayout(RECT{ L, y, R, y + kTextH });
    y += kTextH + kGap;
    if (m_category != nullptr)
        m_category->ForceLayout(RECT{ L + kCategoryLabelW, y, L + kCategoryLabelW + kCategoryW, y + kRowH });
    if (m_btnReadFile != nullptr)
        m_btnReadFile->ForceLayout(RECT{ R - kReadFileW, y, R, y + kRowH });
    m_statusTop = y + kRowH + kGap;

    //底部按钮：自右向左依次是 开始导入（完成）/ 取消
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    if (m_btnSubmit != nullptr)
        m_btnSubmit->ForceLayout(RECT{ R - kBtnWSubmit, by, R, by + kBtnH });
    if (m_btnCancel != nullptr)
    {
        m_btnCancel->ForceLayout(RECT{ R - kBtnWSubmit - kBtnGap - kBtnWCancel, by,
                                       R - kBtnWSubmit - kBtnGap, by + kBtnH });
    }
}

void SensitiveImportModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
        return;

    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;

    ui::DrawTextLine(hdc, _T("批量导入敏感词"),
                     RECT{ L, m_panelRect.top, R - kCloseBtnSize, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kTitlePt, true), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH, kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH, kPanelW, 1, theme::kInk100);

    //格式说明：两行小字
    const int hintTop = m_panelRect.top + kHeaderH + kGap;
    CString hint;
    hint.Format(_T("每行一条：词条[Tab 分类名[Tab 备注]]。只写词条、或分类名写错的行归入下面选的默认分类；")
                _T("空行忽略，重复的词条跳过。每个企业最多 %d 条。"), swtext::kWordsPerEnterprise);
    ui::DrawTextLine(hdc, hint, RECT{ L, hintTop, R, hintTop + kHintH }, theme::kInk500,
                     ui::UiFont(theme::kFontSizeSmall, false), DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    //「默认分类」标签
    const int rowTop = hintTop + kHintH + kGap + kTextH + kGap;
    ui::DrawTextLine(hdc, _T("默认分类"), RECT{ L, rowTop, L + kCategoryLabelW, rowTop + kRowH }, theme::kInk500,
                     ui::UiFont(theme::kFontSizeNormal, false), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    //状态行：进度、错误或结果
    if (!m_status.IsEmpty())
    {
        const COLORREF color = m_statusIsError ? theme::kColorDanger : theme::kInk600;
        ui::DrawTextLine(hdc, m_status, RECT{ L, m_statusTop, R, m_statusTop + kStatusH }, color,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    //子控件（输入框、下拉框、按钮）画在最上层
    DuiControl::OnPaint(hdc, rcDirty);
}

} // namespace flamingoadmin
