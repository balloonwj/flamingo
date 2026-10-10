/**
 *  flamingoAdmin —— 外部服务域名白名单窗实现，说明见 BotDomainsModal.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Screens/BotDomainsModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Util/BotRuleText.h"

#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiEdit.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 窗口几何（96 dpi 逻辑像素），与企业问答的编辑窗同一套风格 ----
const int kPanelW        = 560;  // 卡片宽度
const int kPanelH        = 480;  // 卡片高度
const int kHeaderH       = 56;   // 标题栏高度
const int kFooterH       = 64;   // 按钮栏高度
const int kPadH          = 20;   // 卡片左右内边距
const int kPadV          = 14;   // 主体区上下内边距
const int kNoteH         = 64;   // 说明段落高度（三行小字）
const int kNoteGap       = 8;    // 说明段落与输入框之间的间距
const int kBackdropAlpha = 110;  // 遮罩不透明度（0-255）
const int kTitlePt       = 13;   // 标题字号（磅）
const int kBtnH          = 34;   // 底部按钮高度
const int kBtnGap        = 8;    // 底部按钮间距
const int kBtnWCancel    = 72;   // 「取消」按钮宽度
const int kBtnWSave      = 88;   // 「保存」按钮宽度
const int kCloseBtnSize  = 28;   // 右上角关闭按钮边长

std::unique_ptr<DuiButton> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetVariant(variant);
    button->SetTextPointSize(theme::kFontSizeNormal);
    return button;
}

} // namespace

BotDomainsModal::BotDomainsModal()
    : m_busy(false)
    , m_text(nullptr)
    , m_btnClose(nullptr)
    , m_btnCancel(nullptr)
    , m_btnSave(nullptr)
{
    SetCtrlId(kIdBotDomainsModal);
    ::SetRectEmpty(&m_panelRect);
    ::SetRectEmpty(&m_noteRect);
    ::SetRectEmpty(&m_errorRect);

    std::unique_ptr<DuiEdit> text(new DuiEdit());
    text->SetCtrlId(kIdBotDomainsText);
    text->SetPlaceholder(_T("一行一个域名，如 example.com"));
    text->SetShowBorder(true);
    text->SetBgColor(theme::kInk0);
    text->SetMultiLine(true);
    text->SetWordWrap(false);
    m_text = text.get();
    AddChild(std::move(text));

    std::unique_ptr<DuiButton> closeBtn(new DuiButton());
    closeBtn->SetCtrlId(kIdBotDomainsClose);
    closeBtn->SetText(_T("×"));
    closeBtn->SetVariant(DuiButton::Variant::Ghost);
    closeBtn->SetTextPointSize(theme::kFontSizeHeading);
    m_btnClose = closeBtn.get();
    AddChild(std::move(closeBtn));

    std::unique_ptr<DuiButton> cancelBtn = MakeButton(kIdBotDomainsCancel, _T("取消"), DuiButton::Variant::Default);
    m_btnCancel = cancelBtn.get();
    AddChild(std::move(cancelBtn));

    std::unique_ptr<DuiButton> saveBtn = MakeButton(kIdBotDomainsSave, _T("保存"), DuiButton::Variant::Primary);
    m_btnSave = saveBtn.get();
    AddChild(std::move(saveBtn));
}

void BotDomainsModal::ShowWithDomains(const std::vector<CString>& domains)
{
    SetText(botrule::JoinDomains(domains));
    m_error.Empty();
    SetBusy(false);
    Invalidate();
}

void BotDomainsModal::SetText(const CString& text)
{
    if (m_text != nullptr)
        m_text->SetText(text);
}

std::vector<CString> BotDomainsModal::GetDomains() const
{
    return botrule::SplitDomains((m_text != nullptr) ? m_text->GetText() : CString());
}

bool BotDomainsModal::Validate(CString* error) const
{
    return botrule::ValidateDomains(GetDomains(), error);
}

void BotDomainsModal::SetError(const CString& text)
{
    if (m_error == text)
        return;
    m_error = text;
    Invalidate();
}

void BotDomainsModal::SetBusy(bool busy)
{
    m_busy = busy;
    if (m_btnSave != nullptr)
    {
        m_btnSave->SetEnabled(!busy);
        m_btnSave->SetText(busy ? _T("保存中…") : _T("保存"));
    }
}

void BotDomainsModal::Layout(const RECT& rcAvail)
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
    const int top = m_panelRect.top + kHeaderH + kPadV;
    m_noteRect = RECT{ L, top, R, top + kNoteH };
    const int editTop = top + kNoteH + kNoteGap;
    const int editBottom = m_panelRect.bottom - kFooterH - kPadV;
    if (m_text != nullptr)
        m_text->ForceLayout(RECT{ L, editTop, R, editBottom });

    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    const int saveLeft = R - kBtnWSave;
    const int cancelLeft = saveLeft - kBtnGap - kBtnWCancel;
    if (m_btnSave != nullptr)
        m_btnSave->ForceLayout(RECT{ saveLeft, by, R, by + kBtnH });
    if (m_btnCancel != nullptr)
        m_btnCancel->ForceLayout(RECT{ cancelLeft, by, cancelLeft + kBtnWCancel, by + kBtnH });
    m_errorRect = RECT{ L, by, cancelLeft - kBtnGap, by + kBtnH };
}

void BotDomainsModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
        return;

    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;
    ui::DrawTextLine(hdc, _T("外部服务域名白名单"), RECT{ L, m_panelRect.top, R - kCloseBtnSize, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kTitlePt, true), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH, kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH, kPanelW, 1, theme::kInk100);

    CString note;
    note.Format(_T("机器人规则里「呼叫外部服务」的地址，主机必须等于下面某一项或是它的子域名。一行一个域名，如 ")
                _T("example.com，最多 %d 条；白名单为空时不能配置外部服务。"),
                botapi::kBotDomainMax);
    ui::DrawTextLine(hdc, note, m_noteRect, theme::kInk500, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    if (!m_error.IsEmpty())
    {
        ui::DrawTextLine(hdc, m_error, m_errorRect, theme::kColorDanger, ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    DuiControl::OnPaint(hdc, rcDirty);
}

} // namespace flamingoadmin
