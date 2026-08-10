/**
 *  flamingoAdmin —— 企业设置弹窗 EnterpriseSettingsModal 实现。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Screens/EnterpriseSettingsModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"

#include "Controls/Input/DuiEditHost.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton
#include "DuiNotify.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 弹窗布局常量（96 dpi 逻辑像素）----
const int kPanelWidth   = 560;   // 卡片宽度
const int kHeaderH      = 52;    // 标题栏高度
const int kFooterH      = 60;    // 按钮栏高度
const int kBodyPad      = 20;    // 主体内距
const int kBannerH      = 44;    // 提示横幅高度
const int kSectionGap   = 18;    // 区块间距
const int kSectionTitleH = 24;   // 区块小标题高度
const int kFieldLabelH  = 16;    // 字段标签行高
const int kFieldGap     = 6;     // 标签与控件间距
const int kFieldCtrlH   = 34;    // 输入控件高度
const int kFieldH       = kFieldLabelH + kFieldGap + kFieldCtrlH;  // 单字段高 = 56
const int kRowGap       = 12;    // 字段行间距
const int kColGap       = 14;    // 双列字段列间距
const int kFormRows     = 3;     // 企业信息 3 行（名称|简称 / 行业|地区 / 套餐）
const int kAdminRowH    = 44;    // 管理员列表行高
const int kAddLabelH    = 18;    // 「新增管理员」小标签行高
const int kAddRowH      = 34;    // 新增管理员输入行高
const int kBtnBarH      = 34;    // 底部按钮高度

const int kBackdropAlpha = 112;  // 遮罩不透明度
const int kModalTitlePt  = 14;   // 弹窗标题字号
const int kAdminStatusDisabled = 1;  // 管理员状态：1=停用

// 新建一个带边框的输入框。
std::unique_ptr<DuiEditHost> MakeEdit(int ctrlId, LPCTSTR placeholder)
{
    std::unique_ptr<DuiEditHost> e(new DuiEditHost());
    e->SetCtrlId(ctrlId);
    e->SetPlaceholder(placeholder);
    e->SetShowBorder(true);
    e->SetBgColor(theme::kInk0);
    return e;
}

} // anonymous namespace

EnterpriseSettingsModal::EnterpriseSettingsModal()
{
    SetVisible(false);
    SetCtrlId(kIdEntSettings);   // 自绘「移除」动作经此 ID 上报

    // ---- 企业信息：4 个文本框 + 1 个套餐下拉 ----
    std::unique_ptr<DuiEditHost> eName = MakeEdit(kIdEntSetName, _T("企业全称"));
    m_editName = eName.get();
    AddChild(std::move(eName));

    std::unique_ptr<DuiEditHost> eShort = MakeEdit(kIdEntSetShort, _T("企业简称"));
    m_editShort = eShort.get();
    AddChild(std::move(eShort));

    std::unique_ptr<DuiEditHost> eInd = MakeEdit(kIdEntSetIndustry, _T("所属行业"));
    m_editIndustry = eInd.get();
    AddChild(std::move(eInd));

    std::unique_ptr<DuiEditHost> eReg = MakeEdit(kIdEntSetRegion, _T("主要地区"));
    m_editRegion = eReg.get();
    AddChild(std::move(eReg));

    std::unique_ptr<DuiComboBox> cbPlan(new DuiComboBox());
    cbPlan->SetCtrlId(kIdEntSetPlan);
    cbPlan->AddString(_T("专业版"));
    cbPlan->AddString(_T("企业版"));
    cbPlan->AddString(_T("旗舰版"));
    cbPlan->SetCurSel(0, false);
    m_cbPlan = cbPlan.get();
    AddChild(std::move(cbPlan));

    // ---- 新增管理员行：姓名 + 邮箱 ----
    std::unique_ptr<DuiEditHost> eNewName =
        MakeEdit(kIdEntSetNewName, _T("管理员姓名"));
    m_editNewName = eNewName.get();
    AddChild(std::move(eNewName));

    std::unique_ptr<DuiEditHost> eNewEmail =
        MakeEdit(kIdEntSetNewEmail, _T("管理员邮箱"));
    m_editNewEmail = eNewEmail.get();
    AddChild(std::move(eNewEmail));

    // ---- 关闭 / 取消 / 保存 / 添加 按钮（最后加入的 4 个子控件）----
    std::unique_ptr<ui::IconButton> closeBtn(new ui::IconButton());
    closeBtn->SetCtrlId(kIdEntSetClose);
    closeBtn->SetIcon(ui::Icon::Close);
    closeBtn->SetIconSize(16);
    closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    AddChild(std::move(closeBtn));

    std::unique_ptr<DuiButton> cancel(new DuiButton());
    cancel->SetCtrlId(kIdEntSetCancel);
    cancel->SetText(_T("取消"));
    cancel->SetVariant(DuiButton::Variant::Default);
    cancel->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(cancel));

    std::unique_ptr<DuiButton> save(new DuiButton());
    save->SetCtrlId(kIdEntSetSave);
    save->SetText(_T("保存企业信息"));
    save->SetVariant(DuiButton::Variant::Primary);
    save->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(save));

    std::unique_ptr<DuiButton> addAdmin(new DuiButton());
    addAdmin->SetCtrlId(kIdEntSetAddAdmin);
    addAdmin->SetText(_T("添加"));
    addAdmin->SetVariant(DuiButton::Variant::Default);
    addAdmin->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(addAdmin));
}

int EnterpriseSettingsModal::PanelHeight() const
{
    const int rows = (m_admins.empty() ? 1 : static_cast<int>(m_admins.size()));
    const int adminsH = rows * kAdminRowH;
    const int formH   = kFormRows * kFieldH + (kFormRows - 1) * kRowGap;
    return kHeaderH
         + (kBodyPad + kBannerH + kSectionGap + kSectionTitleH + formH
            + kSectionGap + kSectionTitleH + adminsH + 8 + kAddLabelH
            + kAddRowH + kBodyPad)
         + kFooterH;
}

void EnterpriseSettingsModal::SetData(const Enterprise& ent,
                                      const std::vector<AdminItem>& admins)
{
    m_entId   = ent.m_id;
    m_entName = ent.m_name;
    m_editName->SetText(ent.m_name);
    m_editShort->SetText(ent.m_shortName);
    m_editIndustry->SetText(ent.m_industry);
    m_editRegion->SetText(ent.m_region);
    // 套餐：把企业记录里的套餐名映射回下拉下标。
    int planIndex = 0;
    if (ent.m_plan == _T("企业版"))
    {
        planIndex = 1;
    }
    else if (ent.m_plan == _T("旗舰版"))
    {
        planIndex = 2;
    }
    m_cbPlan->SetCurSel(planIndex, false);
    ClearNewAdmin();
    SetAdmins(admins);
}

void EnterpriseSettingsModal::SetAdmins(const std::vector<AdminItem>& admins)
{
    m_admins = admins;
    // 管理员条数变化会改变面板高度，须重新布局。
    if (m_rcItem.right > m_rcItem.left)
    {
        ForceLayout(m_rcItem);
    }
    Invalidate();
}

bool EnterpriseSettingsModal::ValidateEnterprise() const
{
    CString name = GetName();
    name.Trim();
    return !name.IsEmpty();
}

bool EnterpriseSettingsModal::ValidateNewAdmin() const
{
    CString name = GetNewAdminName();
    CString mail = GetNewAdminEmail();
    name.Trim();
    mail.Trim();
    return !name.IsEmpty() && !mail.IsEmpty();
}

CString EnterpriseSettingsModal::GetName() const      { return m_editName->GetText(); }
CString EnterpriseSettingsModal::GetShortName() const { return m_editShort->GetText(); }
CString EnterpriseSettingsModal::GetIndustry() const  { return m_editIndustry->GetText(); }
CString EnterpriseSettingsModal::GetRegion() const    { return m_editRegion->GetText(); }

int EnterpriseSettingsModal::GetPlan() const
{
    const CString text = m_cbPlan->GetText();
    if (text == _T("企业版"))
    {
        return 2;
    }
    if (text == _T("旗舰版"))
    {
        return 3;
    }
    return 1;   // 专业版
}

CString EnterpriseSettingsModal::GetNewAdminName() const
{
    return m_editNewName->GetText();
}

CString EnterpriseSettingsModal::GetNewAdminEmail() const
{
    return m_editNewEmail->GetText();
}

void EnterpriseSettingsModal::ClearNewAdmin()
{
    m_editNewName->SetText(_T(""));
    m_editNewEmail->SetText(_T(""));
}

void EnterpriseSettingsModal::LayoutFormControls()
{
    const int formLeft = m_panelRect.left + kBodyPad;
    const int formW    = (m_panelRect.right - kBodyPad) - formLeft;
    const int colW     = (formW - kColGap) / 2;
    const int rx       = formLeft + colW + kColGap;
    const int formTop  = m_panelRect.top + kHeaderH + kBodyPad + kBannerH
                       + kSectionGap + kSectionTitleH;
    const int ctrlDy   = kFieldLabelH + kFieldGap;

    const int row0 = formTop;
    const int row1 = formTop + kFieldH + kRowGap;
    const int row2 = formTop + 2 * (kFieldH + kRowGap);
    m_editName->ForceLayout(RECT{ formLeft, row0 + ctrlDy,
                                  formLeft + colW, row0 + ctrlDy + kFieldCtrlH });
    m_editShort->ForceLayout(RECT{ rx, row0 + ctrlDy,
                                   rx + colW, row0 + ctrlDy + kFieldCtrlH });
    m_editIndustry->ForceLayout(RECT{ formLeft, row1 + ctrlDy,
                                      formLeft + colW, row1 + ctrlDy + kFieldCtrlH });
    m_editRegion->ForceLayout(RECT{ rx, row1 + ctrlDy,
                                    rx + colW, row1 + ctrlDy + kFieldCtrlH });
    m_cbPlan->ForceLayout(RECT{ formLeft, row2 + ctrlDy,
                                formLeft + colW, row2 + ctrlDy + kFieldCtrlH });
}

void EnterpriseSettingsModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 居中算面板矩形（高度随管理员条数变化）。
    const int panelH = PanelHeight();
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelWidth / 2;
    m_panelRect.right  = cx + kPanelWidth / 2;
    m_panelRect.top    = cy - panelH / 2;
    m_panelRect.bottom = cy + panelH / 2;

    LayoutFormControls();

    // 新增管理员输入行：姓名 + 邮箱（在「添加」按钮左侧）。
    const int formLeft = m_panelRect.left + kBodyPad;
    const int formW    = (m_panelRect.right - kBodyPad) - formLeft;
    const int addRowTop = m_panelRect.bottom - kFooterH - kBodyPad - kAddRowH;
    const int addBtnW = 70;
    const int newNameW = 150;
    const int newEmailW = formW - addBtnW - 10 - newNameW - 10;
    m_editNewName->ForceLayout(RECT{ formLeft, addRowTop,
                                     formLeft + newNameW, addRowTop + kAddRowH });
    m_editNewEmail->ForceLayout(RECT{ formLeft + newNameW + 10, addRowTop,
                                      formLeft + newNameW + 10 + newEmailW,
                                      addRowTop + kAddRowH });

    // 关闭 / 取消 / 保存 / 添加 是最后加入的 4 个子控件。
    const size_t n = m_children.size();
    if (n >= 4)
    {
        DuiControl* closeBtn = m_children[n - 4].get();
        DuiControl* cancel   = m_children[n - 3].get();
        DuiControl* save     = m_children[n - 2].get();
        DuiControl* addAdmin = m_children[n - 1].get();

        closeBtn->ForceLayout(RECT{ m_panelRect.right - 12 - 30,
                                    m_panelRect.top + (kHeaderH - 30) / 2,
                                    m_panelRect.right - 12,
                                    m_panelRect.top + (kHeaderH + 30) / 2 });

        const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnBarH) / 2;
        RECT rcSave = { m_panelRect.right - 20 - 120, by,
                        m_panelRect.right - 20,       by + kBtnBarH };
        RECT rcCancel = { rcSave.left - 10 - 76, by, rcSave.left - 10, by + kBtnBarH };
        save->ForceLayout(rcSave);
        cancel->ForceLayout(rcCancel);

        addAdmin->ForceLayout(RECT{ formLeft + formW - addBtnW, addRowTop,
                                    formLeft + formW, addRowTop + kAddRowH });
    }
}

void EnterpriseSettingsModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    m_removeHits.clear();

    // 半透明遮罩。
    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);

    // 卡片。
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int left  = m_panelRect.left + kBodyPad;
    const int right = m_panelRect.right - kBodyPad;
    const int w     = right - left;

    // 标题。
    ui::DrawTextLine(hdc, _T("企业设置"),
                     RECT{ m_panelRect.left + 20, m_panelRect.top,
                           m_panelRect.right - 50, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kModalTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelWidth, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelWidth, 1, theme::kInk100);

    // 提示横幅。
    RECT banner = { left, m_panelRect.top + kHeaderH + kBodyPad,
                    right, m_panelRect.top + kHeaderH + kBodyPad + kBannerH };
    ui::FillRoundRect(hdc, banner, theme::kInk25, theme::kRadiusMd);
    ui::StrokeRoundRect(hdc, banner, theme::kInk100, theme::kRadiusMd, 1.0f);
    RECT bi = { banner.left + 12, (banner.top + banner.bottom) / 2 - 8,
                banner.left + 28, (banner.top + banner.bottom) / 2 + 8 };
    ui::DrawIcon(hdc, ui::Icon::Building, bi, theme::kFlamingo500);
    CString bannerText;
    bannerText.Format(_T("企业：%s"),
                      static_cast<LPCTSTR>(m_entName.IsEmpty()
                          ? CString(_T("—")) : m_entName));
    ui::DrawTextLine(hdc, bannerText,
                     RECT{ banner.left + 38, banner.top, banner.right - 12, banner.bottom },
                     theme::kInk500, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // ---- 企业信息 区块标题 + 字段标签 ----
    const int infoTitleY = banner.bottom + kSectionGap;
    ui::DrawTextLine(hdc, _T("企业信息"),
                     RECT{ left, infoTitleY, right, infoTitleY + kSectionTitleH },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const int formTop = infoTitleY + kSectionTitleH;
    const int colW    = (w - kColGap) / 2;
    const int rx      = left + colW + kColGap;
    HFONT labelFont   = ui::UiFont(theme::kFontSizeSmall, false);
    const int row0 = formTop;
    const int row1 = formTop + kFieldH + kRowGap;
    const int row2 = formTop + 2 * (kFieldH + kRowGap);
    ui::DrawTextLine(hdc, _T("企业全称 *"), RECT{ left, row0, left + colW, row0 + kFieldLabelH },
                     theme::kInk500, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("企业简称"), RECT{ rx, row0, rx + colW, row0 + kFieldLabelH },
                     theme::kInk500, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("所属行业"), RECT{ left, row1, left + colW, row1 + kFieldLabelH },
                     theme::kInk500, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("主要地区"), RECT{ rx, row1, rx + colW, row1 + kFieldLabelH },
                     theme::kInk500, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("套餐"), RECT{ left, row2, left + colW, row2 + kFieldLabelH },
                     theme::kInk500, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 企业管理员 区块标题 + 列表 ----
    const int formBottom = formTop + kFormRows * kFieldH + (kFormRows - 1) * kRowGap;
    const int admTitleY  = formBottom + kSectionGap;
    ui::DrawTextLine(hdc, _T("企业管理员"),
                     RECT{ left, admTitleY, right, admTitleY + kSectionTitleH },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const int admTop = admTitleY + kSectionTitleH;
    if (m_admins.empty())
    {
        RECT box = { left, admTop, right, admTop + kAdminRowH };
        ui::FillRoundRect(hdc, box, theme::kInk25, theme::kRadiusMd);
        ui::DrawTextLine(hdc, _T("该企业暂无管理员"), box, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else
    {
        RECT listCard = { left, admTop, right,
                          admTop + kAdminRowH * static_cast<int>(m_admins.size()) };
        ui::DrawCard(hdc, listCard);
        for (size_t i = 0; i < m_admins.size(); ++i)
        {
            const AdminItem& a = m_admins[i];
            const int top = admTop + static_cast<int>(i) * kAdminRowH;
            if (i > 0)
            {
                ui::FillLine(hdc, left + 14, top, w - 28, 1, theme::kInk100);
            }
            ui::DrawTextLine(hdc, a.m_name,
                             RECT{ left + 14, top + 6, left + 200, top + 24 },
                             theme::kInk700, ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ui::DrawTextLine(hdc, a.m_email,
                             RECT{ left + 14, top + 23, right - 150, top + 40 },
                             theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            // 状态徽章。
            const bool disabled = (a.m_status == kAdminStatusDisabled);
            LPCTSTR stTxt = disabled ? _T("停用") : _T("正常");
            COLORREF stClr = disabled ? theme::kInk400 : theme::kColorOk;
            const int cw = ui::ChipWidth(hdc, stTxt, true);
            RECT chip = { right - 14 - 56 - 10 - cw, top + kAdminRowH / 2 - 10,
                          right - 14 - 56 - 10,      top + kAdminRowH / 2 + 10 };
            ui::DrawChip(hdc, chip, stTxt, theme::kInk600, theme::kInk50, true, stClr);
            // 「移除」链接。
            RECT rm = { right - 14 - 56, top, right - 14, top + kAdminRowH };
            ui::DrawTextLine(hdc, _T("移除"), rm, theme::kColorDanger,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RemoveHit hit;
            hit.m_rc = rm;
            hit.m_adminId = a.m_adminId;
            m_removeHits.push_back(hit);
        }
    }

    // ---- 新增管理员 小标签（输入行控件由 base OnPaint 画）----
    const int admBottom = admTop
        + (m_admins.empty() ? kAdminRowH
                            : kAdminRowH * static_cast<int>(m_admins.size()));
    const int addLabelY = admBottom + 8;
    ui::DrawTextLine(hdc, _T("新增管理员"),
                     RECT{ left, addLabelY, right, addLabelY + kAddLabelH },
                     theme::kInk500, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 子控件（输入框 / 下拉 / 按钮）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

balloonwjui::DuiControl* EnterpriseSettingsModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
    if (::PtInRect(&m_panelRect, ptHostClient))
    {
        // 优先下发给子控件（输入框 / 下拉 / 按钮）。
        for (const auto& child : m_children)
        {
            if (DuiControl* hit = child->HitTest(ptHostClient))
            {
                return hit;
            }
        }
        return this;   // 面板空白 / 自绘「移除」区：由本控件 OnLButtonUp 处理
    }
    return this;       // 遮罩空白处：吞掉点击，不关闭
}

bool EnterpriseSettingsModal::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 命中某行「移除」→ 记下管理员 ID 并上报 AdminFrame。
    for (size_t i = 0; i < m_removeHits.size(); ++i)
    {
        if (::PtInRect(&m_removeHits[i].m_rc, pt))
        {
            m_pendingRemoveAdminId = m_removeHits[i].m_adminId;
            NotifyParent(DUIN_CLICK);
            return true;
        }
    }
    return true;   // 其余位置：吞掉点击，不关闭弹窗
}

} // namespace flamingoadmin
