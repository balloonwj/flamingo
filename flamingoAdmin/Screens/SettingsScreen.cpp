/**
 *  flamingoAdmin —— 平台设置界面 SettingsScreen 实现。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Screens/SettingsScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"

#include "Controls/Input/DuiEdit.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kHeaderH  = 96;    // 页头高度
const int kSidePad  = 24;    // 内容区左 / 右外边距
const int kCardW    = 600;   // 卡片宽度上限
const int kCardGap  = 16;    // 页头与卡、两卡之间的间距
const int kCardPad  = 24;    // 卡片内边距
const int kEditH    = 36;    // 输入框高度
const int kBtnH     = 36;    // 按钮高度
const int kBtnW     = 112;   // 按钮宽度

const int kEyebrowPt   = 8;  // 页头小标题字号（磅）
const int kPageTitlePt = 17; // 页头大标题字号（磅）
const int kCardTitlePt = 12; // 卡片标题字号（磅）

// 「个人资料」卡内部 y 偏移（相对卡顶）。
const int kC1Title   = 20;   // 卡标题
const int kC1NameLbl = 58;   // 姓名标签
const int kC1NameEd  = 78;   // 姓名输入框
const int kC1MailLbl = 130;  // 邮箱标签
const int kC1MailVal = 150;  // 邮箱值
const int kC1ScpLbl  = 186;  // 管辖范围标签
const int kC1ScpVal  = 206;  // 管辖范围值
const int kC1Btn     = 242;  // 「保存」按钮
const int kCard1H    = kC1Btn + kBtnH + 20;   // 个人资料卡总高

// 「修改密码」卡内部 y 偏移（相对卡顶）。
const int kC2Title   = 20;   // 卡标题
const int kC2OldLbl  = 58;   // 当前密码标签
const int kC2OldEd   = 78;   // 当前密码输入框
const int kC2NewLbl  = 130;  // 新密码标签
const int kC2NewEd   = 150;  // 新密码输入框
const int kC2ConfLbl = 202;  // 确认新密码标签
const int kC2ConfEd  = 222;  // 确认新密码输入框
const int kC2Btn     = 274;  // 「修改密码」按钮
const int kCard2H    = kC2Btn + kBtnH + 20;   // 修改密码卡总高

// 构造一个统一风格的输入框。
//   id：控件 ID；placeholder：占位文字；password：是否密码模式。
std::unique_ptr<DuiEdit> MakeEdit(int id, LPCTSTR placeholder, bool password)
{
    auto e = std::make_unique<DuiEdit>();
    e->SetCtrlId(id);
    e->SetPlaceholder(placeholder);
    e->SetShowBorder(true);
    e->SetBgColor(theme::kInk0);
    if (password)
        e->SetPassword(true);
    return e;
}

// 画一行字段标签。
void DrawFieldLabel(HDC hdc, LPCTSTR text, int left, int right, int y)
{
    ui::DrawTextLine(hdc, text, RECT{ left, y, right, y + 16 },
                     theme::kInk500, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // anonymous namespace

SettingsScreen::SettingsScreen()
{
    auto nameEdit = MakeEdit(kIdSettingsName, _T("管理员姓名"), false);
    m_nameEdit = nameEdit.get();
    AddChild(std::move(nameEdit));

    auto saveBtn = std::make_unique<DuiButton>();
    saveBtn->SetCtrlId(kIdSettingsSaveProfile);
    saveBtn->SetText(_T("保存"));
    saveBtn->SetVariant(DuiButton::Variant::Primary);
    saveBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnSaveProfile = saveBtn.get();
    AddChild(std::move(saveBtn));

    auto oldPw = MakeEdit(kIdSettingsOldPw, _T("当前登录密码"), true);
    m_oldPwEdit = oldPw.get();
    AddChild(std::move(oldPw));

    auto newPw = MakeEdit(kIdSettingsNewPw, _T("新密码"), true);
    m_newPwEdit = newPw.get();
    AddChild(std::move(newPw));

    auto confPw = MakeEdit(kIdSettingsConfirmPw, _T("再次输入新密码"), true);
    m_confirmPwEdit = confPw.get();
    AddChild(std::move(confPw));

    auto changeBtn = std::make_unique<DuiButton>();
    changeBtn->SetCtrlId(kIdSettingsChangePw);
    changeBtn->SetText(_T("修改密码"));
    changeBtn->SetVariant(DuiButton::Variant::Primary);
    changeBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnChangePw = changeBtn.get();
    AddChild(std::move(changeBtn));
}

void SettingsScreen::SetProfile(const CString& name, const CString& email,
                                const CString& scopeText)
{
    if (m_nameEdit != nullptr)
    {
        m_nameEdit->SetText(name);
    }
    m_email     = email;
    m_scopeText = scopeText;
    Invalidate();
}

CString SettingsScreen::GetName() const
{
    CString s = (m_nameEdit != nullptr) ? m_nameEdit->GetText() : CString();
    s.Trim();
    return s;
}

CString SettingsScreen::GetOldPassword() const
{
    return (m_oldPwEdit != nullptr) ? m_oldPwEdit->GetText() : CString();
}

CString SettingsScreen::GetNewPassword() const
{
    return (m_newPwEdit != nullptr) ? m_newPwEdit->GetText() : CString();
}

CString SettingsScreen::GetConfirmPassword() const
{
    return (m_confirmPwEdit != nullptr) ? m_confirmPwEdit->GetText() : CString();
}

void SettingsScreen::ClearPasswordFields()
{
    if (m_oldPwEdit != nullptr)
    {
        m_oldPwEdit->SetText(_T(""));
    }
    if (m_newPwEdit != nullptr)
    {
        m_newPwEdit->SetText(_T(""));
    }
    if (m_confirmPwEdit != nullptr)
    {
        m_confirmPwEdit->SetText(_T(""));
    }
    Invalidate();
}

void SettingsScreen::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    const int contentL = rcAvail.left + kSidePad;
    int cardW = rcAvail.right - rcAvail.left - kSidePad * 2;
    if (cardW > kCardW)
    {
        cardW = kCardW;
    }
    const int card1Top = rcAvail.top + kHeaderH + kCardGap;
    m_card1Rc = RECT{ contentL, card1Top, contentL + cardW, card1Top + kCard1H };
    const int card2Top = m_card1Rc.bottom + kCardGap;
    m_card2Rc = RECT{ contentL, card2Top, contentL + cardW, card2Top + kCard2H };

    const int fieldL = contentL + kCardPad;
    const int fieldR = contentL + cardW - kCardPad;

    // 个人资料卡：姓名输入框 + 保存按钮。
    m_nameEdit->ForceLayout(RECT{ fieldL, m_card1Rc.top + kC1NameEd,
                                  fieldR, m_card1Rc.top + kC1NameEd + kEditH });
    m_btnSaveProfile->ForceLayout(RECT{ fieldR - kBtnW, m_card1Rc.top + kC1Btn,
                                        fieldR, m_card1Rc.top + kC1Btn + kBtnH });

    // 修改密码卡：三个输入框 + 修改密码按钮。
    m_oldPwEdit->ForceLayout(RECT{ fieldL, m_card2Rc.top + kC2OldEd,
                                   fieldR, m_card2Rc.top + kC2OldEd + kEditH });
    m_newPwEdit->ForceLayout(RECT{ fieldL, m_card2Rc.top + kC2NewEd,
                                   fieldR, m_card2Rc.top + kC2NewEd + kEditH });
    m_confirmPwEdit->ForceLayout(RECT{ fieldL, m_card2Rc.top + kC2ConfEd,
                                       fieldR, m_card2Rc.top + kC2ConfEd + kEditH });
    m_btnChangePw->ForceLayout(RECT{ fieldR - kBtnW, m_card2Rc.top + kC2Btn,
                                     fieldR, m_card2Rc.top + kC2Btn + kBtnH });
}

void SettingsScreen::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk25);

    const int L = m_rcItem.left;
    const int R = m_rcItem.right;

    // ---- 页头 ----
    ui::FillSolidRect(hdc, RECT{ L, m_rcItem.top, R, m_rcItem.top + kHeaderH },
                      theme::kInk0);
    ui::DrawTextLine(hdc, _T("账户"),
                     RECT{ L + kSidePad, m_rcItem.top + 20, L + 300, m_rcItem.top + 36 },
                     theme::kFlamingo600, ui::UiFont(kEyebrowPt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("平台设置"),
                     RECT{ L + kSidePad, m_rcItem.top + 36, L + 400, m_rcItem.top + 68 },
                     theme::kInk700, ui::UiFont(kPageTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("管理你的管理员资料与登录密码"),
                     RECT{ L + kSidePad, m_rcItem.top + 68, L + 600, m_rcItem.top + 88 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    HFONT titleFont = ui::UiFont(kCardTitlePt, true);
    HFONT valueFont = ui::UiFont(theme::kFontSizeNormal, false);

    // ---- 卡 1：个人资料 ----
    ui::DrawCard(hdc, m_card1Rc);
    const int c1L = m_card1Rc.left + kCardPad;
    const int c1R = m_card1Rc.right - kCardPad;
    ui::DrawTextLine(hdc, _T("个人资料"),
                     RECT{ c1L, m_card1Rc.top + kC1Title,
                           c1R, m_card1Rc.top + kC1Title + 22 },
                     theme::kInk700, titleFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawFieldLabel(hdc, _T("姓名"), c1L, c1R, m_card1Rc.top + kC1NameLbl);
    // 姓名输入框由子控件绘制。
    DrawFieldLabel(hdc, _T("邮箱"), c1L, c1R, m_card1Rc.top + kC1MailLbl);
    ui::DrawTextLine(hdc, m_email.IsEmpty() ? CString(_T("—")) : m_email,
                     RECT{ c1L, m_card1Rc.top + kC1MailVal,
                           c1R, m_card1Rc.top + kC1MailVal + 20 },
                     theme::kInk700, valueFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    DrawFieldLabel(hdc, _T("管辖范围"), c1L, c1R, m_card1Rc.top + kC1ScpLbl);
    ui::DrawTextLine(hdc, m_scopeText.IsEmpty() ? CString(_T("—")) : m_scopeText,
                     RECT{ c1L, m_card1Rc.top + kC1ScpVal,
                           c1R, m_card1Rc.top + kC1ScpVal + 20 },
                     theme::kInk700, valueFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // ---- 卡 2：修改密码 ----
    ui::DrawCard(hdc, m_card2Rc);
    const int c2L = m_card2Rc.left + kCardPad;
    const int c2R = m_card2Rc.right - kCardPad;
    ui::DrawTextLine(hdc, _T("修改密码"),
                     RECT{ c2L, m_card2Rc.top + kC2Title,
                           c2R, m_card2Rc.top + kC2Title + 22 },
                     theme::kInk700, titleFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawFieldLabel(hdc, _T("当前密码"),   c2L, c2R, m_card2Rc.top + kC2OldLbl);
    DrawFieldLabel(hdc, _T("新密码"),     c2L, c2R, m_card2Rc.top + kC2NewLbl);
    DrawFieldLabel(hdc, _T("确认新密码"), c2L, c2R, m_card2Rc.top + kC2ConfLbl);

    // 子控件（输入框 + 按钮）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

} // namespace flamingoadmin
