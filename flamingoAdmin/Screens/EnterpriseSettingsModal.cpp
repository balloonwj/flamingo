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
#include "Widgets/OverlayScrollbar.h"   // 主体区的悬浮滚动条

#include "Controls/Input/DuiEdit.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton
#include "DuiNotify.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// 企业没有填写行业时，行业下拉框里那一项额外项的显示文字；取值时还原成空串。
const TCHAR kIndustryNotSet[] = _T("（未填写）");

// ---- 弹窗布局常量（96 dpi 逻辑像素）----
const int kPanelWidth   = 560;   // 卡片宽度
const int kHeaderH      = 52;    // 标题栏高度
const int kFooterH      = 60;    // 按钮栏高度
const int kBodyPad      = 20;    // 主体内距
const int kBannerH      = 36;    // 提示横幅高度（2026-10-04 由 44 收紧：功能开关加到三行后，最小窗口下 2 位管理员时要排得下）
const int kSectionGap   = 12;    // 区块间距（2026-10-04 由 18 收紧，理由同 kBannerH）
const int kSectionTitleH = 24;   // 区块小标题高度
const int kFieldLabelH  = 16;    // 字段标签行高
const int kFieldGap     = 6;     // 标签与控件间距
const int kFieldCtrlH   = 34;    // 输入控件高度
const int kFieldH       = kFieldLabelH + kFieldGap + kFieldCtrlH;  // 单字段高 = 56
const int kRowGap       = 8;     // 字段行间距（2026-10-04 由 12 收紧，理由同 kBannerH）
const int kColGap       = 14;    // 双列字段列间距
const int kFormRows     = 3;     // 企业信息 3 行（名称|简称 / 行业|地区 / 套餐）
const int kAdminRowH    = 36;    // 管理员列表行高（2026-10-04 由 44 收紧，理由同 kBannerH）
const int kAdminNameTop = 2;     // 管理员行：姓名行距行顶的距离
const int kAdminLineH   = 16;    // 管理员行：姓名、邮箱各占一行，每行的高度（两行加上下各 2 像素恰为行高 36）
const int kAddLabelH    = 18;    // 「新增管理员」小标签行高
const int kAddRowH      = 34;    // 新增管理员输入行高
const int kBtnBarH      = 34;    // 底部按钮高度
// 主体区滚动（2026-10-04）
const int kModalMargin = 16;   // 卡片与窗口上下边缘至少留出的距离；内容更高时卡片按「窗口高 - 2 × 本值」截断并滚动
const int kMinBodyH    = 120;  // 主体区的最小可视高度：窗口矮到连这个高度都放不下时卡片超出窗口，不再继续压缩
const int kWheelStep   = 48;   // 滚轮每格（WHEEL_DELTA）滚动的像素数

const int kBackdropAlpha = 112;  // 遮罩不透明度
const int kModalTitlePt  = 14;   // 弹窗标题字号
const int kAdminStatusDisabled = 1;  // 管理员状态：1=停用

// 新建一个带边框的输入框。
std::unique_ptr<DuiEdit> MakeEdit(int ctrlId, LPCTSTR placeholder)
{
    std::unique_ptr<DuiEdit> e(new DuiEdit());
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
    std::unique_ptr<DuiEdit> eName = MakeEdit(kIdEntSetName, _T("企业全称"));
    m_editName = eName.get();
    AddChild(std::move(eName));

    std::unique_ptr<DuiEdit> eShort = MakeEdit(kIdEntSetShort, _T("企业简称"));
    m_editShort = eShort.get();
    AddChild(std::move(eShort));

    // 所属行业：与新增企业弹窗相同的下拉框（原先是自由输入）。
    std::unique_ptr<DuiComboBox> cbIndustry(new DuiComboBox());
    cbIndustry->SetCtrlId(kIdEntSetIndustry);
    for (int i = 0; i < adminapi::kIndustryOptionCount; ++i)
    {
        cbIndustry->AddString(adminapi::kIndustryOptions[i]);
    }
    cbIndustry->SetCurSel(0, false);
    m_cbIndustry = cbIndustry.get();
    AddChild(std::move(cbIndustry));

    std::unique_ptr<DuiEdit> eReg = MakeEdit(kIdEntSetRegion, _T("主要地区"));
    m_editRegion = eReg.get();
    AddChild(std::move(eReg));

    std::unique_ptr<DuiComboBox> cbPlan(new DuiComboBox());
    cbPlan->SetCtrlId(kIdEntSetPlan);
    // 套餐选项与新增企业、「更多筛选」共用（下标即套餐编号）。企业的套餐不在列表中时，
    // LoadEnterprise 会把它作为额外一项加在末尾并选中，不改就原样保存。
    for (int i = 0; i < adminapi::kPlanOptionCount; ++i)
    {
        cbPlan->AddString(adminapi::kPlanOptions[i]);
    }
    cbPlan->SetCurSel(0, false);
    m_cbPlan = cbPlan.get();
    AddChild(std::move(cbPlan));

    // 账号前缀（2026-10-06）：第三行右列，原先空着；不填时新账号按默认的 EMP 生成
    std::unique_ptr<DuiEdit> ePrefix = MakeEdit(kIdEntSetPrefix, _T("不填时为 EMP，如 HW."));
    ePrefix->SetMaxLength(adminapi::kAccountPrefixMaxLength);
    m_editPrefix = ePrefix.get();
    AddChild(std::move(ePrefix));

    // ---- 新增管理员行：姓名 + 邮箱 ----
    std::unique_ptr<DuiEdit> eNewName =
        MakeEdit(kIdEntSetNewName, _T("管理员姓名"));
    m_editNewName = eNewName.get();
    AddChild(std::move(eNewName));

    std::unique_ptr<DuiEdit> eNewEmail =
        MakeEdit(kIdEntSetNewEmail, _T("管理员邮箱"));
    m_editNewEmail = eNewEmail.get();
    AddChild(std::move(eNewEmail));

    // ---- 关闭 / 取消 / 保存 / 添加 按钮 ----
    std::unique_ptr<ui::IconButton> closeBtn(new ui::IconButton());
    closeBtn->SetCtrlId(kIdEntSetClose);
    closeBtn->SetIcon(ui::Icon::Close);
    closeBtn->SetIconSize(16);
    closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    m_closeBtn = closeBtn.get();
    AddChild(std::move(closeBtn));

    std::unique_ptr<DuiButton> cancel(new DuiButton());
    cancel->SetCtrlId(kIdEntSetCancel);
    cancel->SetText(_T("取消"));
    cancel->SetVariant(DuiButton::Variant::Default);
    cancel->SetTextPointSize(theme::kFontSizeNormal);
    m_cancelBtn = cancel.get();
    AddChild(std::move(cancel));

    std::unique_ptr<DuiButton> save(new DuiButton());
    save->SetCtrlId(kIdEntSetSave);
    save->SetText(_T("保存企业信息"));
    save->SetVariant(DuiButton::Variant::Primary);
    save->SetTextPointSize(theme::kFontSizeNormal);
    m_saveBtn = save.get();
    AddChild(std::move(save));

    std::unique_ptr<DuiButton> addAdmin(new DuiButton());
    addAdmin->SetCtrlId(kIdEntSetAddAdmin);
    addAdmin->SetText(_T("添加"));
    addAdmin->SetVariant(DuiButton::Variant::Default);
    addAdmin->SetTextPointSize(theme::kFontSizeNormal);
    m_addAdminBtn = addAdmin.get();
    AddChild(std::move(addAdmin));

    // ---- 主体区的悬浮滚动条：最后加入，画在主体内容之上 ----
    std::unique_ptr<OverlayScrollbar> sb(new OverlayScrollbar());
    sb->SetOnScroll(&EnterpriseSettingsModal::OnScrollbarScroll, this);
    m_scrollbar = sb.get();
    AddChild(std::move(sb));
}

int EnterpriseSettingsModal::ContentHeight() const
{
    const int rows = (m_admins.empty() ? 1 : static_cast<int>(m_admins.size()));
    const int adminsH = rows * kAdminRowH;
    const int formH   = kFormRows * kFieldH + (kFormRows - 1) * kRowGap;
    return kBodyPad + kBannerH + kSectionGap + kSectionTitleH + formH
         + kSectionGap + kSectionTitleH + adminsH + 8 + kAddLabelH
         + kAddRowH + kBodyPad;
}

int EnterpriseSettingsModal::PanelHeight() const
{
    return kHeaderH + ContentHeight() + kFooterH;
}

void EnterpriseSettingsModal::SetData(const Enterprise& ent,
                                      const std::vector<AdminItem>& admins)
{
    m_entId   = ent.m_id;
    m_entName = ent.m_name;
    m_editName->SetText(ent.m_name);
    m_editShort->SetText(ent.m_shortName);
    // 行业：先去掉上一次打开时加的额外项，再选中企业现有的行业。不在选项里的行业（或空行业
    // 显示的「（未填写）」）作为额外项加在末尾，保存时原样取回，不会被改成别的行业。
    while (m_cbIndustry->GetCount() > adminapi::kIndustryOptionCount)
    {
        m_cbIndustry->DeleteString(m_cbIndustry->GetCount() - 1);
    }
    int industryIndex = -1;
    for (int i = 0; i < adminapi::kIndustryOptionCount; ++i)
    {
        if (ent.m_industry == adminapi::kIndustryOptions[i])
        {
            industryIndex = i;
            break;
        }
    }
    if (industryIndex < 0)
    {
        m_cbIndustry->AddString(ent.m_industry.IsEmpty() ? kIndustryNotSet
                                                         : static_cast<LPCTSTR>(ent.m_industry));
        industryIndex = m_cbIndustry->GetCount() - 1;
    }
    m_cbIndustry->SetCurSel(industryIndex, false);
    m_editRegion->SetText(ent.m_region);
    // 套餐：先去掉上一次打开时加的额外项，再按企业记录里的套餐名选中对应下标。名称不在
    // 列表中（如「套餐3」）时把它作为额外一项加在末尾并选中，避免保存时被悄悄改成别的套餐。
    while (m_cbPlan->GetCount() > adminapi::kPlanOptionCount)
    {
        m_cbPlan->DeleteString(m_cbPlan->GetCount() - 1);
    }
    int planIndex = adminapi::PlanCodeFromName(ent.m_plan);
    if (planIndex < 0 || planIndex >= adminapi::kPlanOptionCount)
    {
        m_cbPlan->AddString(ent.m_plan);
        planIndex = m_cbPlan->GetCount() - 1;
    }
    m_cbPlan->SetCurSel(planIndex, false);
    m_editPrefix->SetText(ent.m_accountPrefix);
    ClearNewAdmin();
    //打开（或换成另一家企业）时从顶部看起；SetAdmins 随即按新内容重排
    m_scroll = 0;
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

bool EnterpriseSettingsModal::ValidateAccountPrefix() const
{
    CString normalized;
    return adminapi::NormalizeAccountPrefix(GetAccountPrefix(), normalized);
}

CString EnterpriseSettingsModal::GetAccountPrefix() const
{
    return m_editPrefix->GetText();
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
CString EnterpriseSettingsModal::GetIndustry() const
{
    // 「（未填写）」只是空行业的显示文字，取值时还原成空串。
    const CString text = m_cbIndustry->GetText();
    return (text == kIndustryNotSet) ? CString() : text;
}
CString EnterpriseSettingsModal::GetRegion() const    { return m_editRegion->GetText(); }

int EnterpriseSettingsModal::GetPlan() const
{
    // 列表中的名称换算为下标；额外项「套餐N」还原为 N。都认不出时（理论上不会发生）按专业版。
    const int kFallbackPlan = 1;
    const int code = adminapi::PlanCodeFromName(m_cbPlan->GetText());
    return (code >= 0) ? code : kFallbackPlan;
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

void EnterpriseSettingsModal::LayoutFormControls(int contentTop)
{
    const int formLeft = m_panelRect.left + kBodyPad;
    const int formW    = (m_panelRect.right - kBodyPad) - formLeft;
    const int colW     = (formW - kColGap) / 2;
    const int rx       = formLeft + colW + kColGap;
    const int formTop  = contentTop + kBodyPad + kBannerH + kSectionGap + kSectionTitleH;
    const int ctrlDy   = kFieldLabelH + kFieldGap;

    const int row0 = formTop;
    const int row1 = formTop + kFieldH + kRowGap;
    const int row2 = formTop + 2 * (kFieldH + kRowGap);
    m_editName->ForceLayout(RECT{ formLeft, row0 + ctrlDy,
                                  formLeft + colW, row0 + ctrlDy + kFieldCtrlH });
    m_editShort->ForceLayout(RECT{ rx, row0 + ctrlDy,
                                   rx + colW, row0 + ctrlDy + kFieldCtrlH });
    m_cbIndustry->ForceLayout(RECT{ formLeft, row1 + ctrlDy,
                                    formLeft + colW, row1 + ctrlDy + kFieldCtrlH });
    m_editRegion->ForceLayout(RECT{ rx, row1 + ctrlDy,
                                    rx + colW, row1 + ctrlDy + kFieldCtrlH });
    m_cbPlan->ForceLayout(RECT{ formLeft, row2 + ctrlDy,
                                formLeft + colW, row2 + ctrlDy + kFieldCtrlH });
    m_editPrefix->ForceLayout(RECT{ rx, row2 + ctrlDy,
                                    rx + colW, row2 + ctrlDy + kFieldCtrlH });
}

void EnterpriseSettingsModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 居中算面板矩形：高度取内容全部展开时的高度与窗口可用高度（上下各留 kModalMargin）中较小的一个，
    // 但主体区至少留 kMinBodyH，窗口再矮时卡片超出窗口、不再继续压缩。
    const int contentH = ContentHeight();
    const int availH   = (rcAvail.bottom - rcAvail.top) - 2 * kModalMargin;
    const int minPanelH = kHeaderH + kMinBodyH + kFooterH;
    int panelH = PanelHeight();
    if (panelH > availH)
    {
        panelH = (availH > minPanelH) ? availH : minPanelH;
    }
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelWidth / 2;
    m_panelRect.right  = cx + kPanelWidth / 2;
    m_panelRect.top    = cy - panelH / 2;
    m_panelRect.bottom = m_panelRect.top + panelH;

    // 主体区：标题栏与底部按钮栏之间。滚动偏移夹取到 [0, 内容高 - 可视高]。
    m_bodyRect = RECT{ m_panelRect.left, m_panelRect.top + kHeaderH, m_panelRect.right, m_panelRect.bottom - kFooterH };
    const int bodyH = m_bodyRect.bottom - m_bodyRect.top;
    m_scrollMax = (contentH > bodyH) ? (contentH - bodyH) : 0;
    if (m_scroll > m_scrollMax)
    {
        m_scroll = m_scrollMax;
    }
    if (m_scroll < 0)
    {
        m_scroll = 0;
    }
    // 主体内容的顶边：随滚动上移
    const int contentTop = m_bodyRect.top - m_scroll;

    LayoutFormControls(contentTop);

    // 新增管理员输入行：姓名 + 邮箱（在「添加」按钮左侧），位于管理员列表与「新增管理员」小标签之下。
    const int formLeft = m_panelRect.left + kBodyPad;
    const int formW    = (m_panelRect.right - kBodyPad) - formLeft;
    const int formH    = kFormRows * kFieldH + (kFormRows - 1) * kRowGap;
    const int adminsH  = (m_admins.empty() ? 1 : static_cast<int>(m_admins.size())) * kAdminRowH;
    const int addRowTop = contentTop + kBodyPad + kBannerH + kSectionGap + kSectionTitleH + formH
                        + kSectionGap + kSectionTitleH + adminsH + 8 + kAddLabelH;
    const int addBtnW = 70;
    const int newNameW = 150;
    const int newEmailW = formW - addBtnW - 10 - newNameW - 10;
    m_editNewName->ForceLayout(RECT{ formLeft, addRowTop,
                                     formLeft + newNameW, addRowTop + kAddRowH });
    m_editNewEmail->ForceLayout(RECT{ formLeft + newNameW + 10, addRowTop,
                                      formLeft + newNameW + 10 + newEmailW,
                                      addRowTop + kAddRowH });
    m_addAdminBtn->ForceLayout(RECT{ formLeft + formW - addBtnW, addRowTop,
                                     formLeft + formW, addRowTop + kAddRowH });

    // 标题栏的关闭按钮与底部的取消 / 保存：固定在卡片上，不随主体滚动。
    m_closeBtn->ForceLayout(RECT{ m_panelRect.right - 12 - 30,
                                  m_panelRect.top + (kHeaderH - 30) / 2,
                                  m_panelRect.right - 12,
                                  m_panelRect.top + (kHeaderH + 30) / 2 });
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnBarH) / 2;
    RECT rcSave = { m_panelRect.right - 20 - 120, by,
                    m_panelRect.right - 20,       by + kBtnBarH };
    RECT rcCancel = { rcSave.left - 10 - 76, by, rcSave.left - 10, by + kBtnBarH };
    m_saveBtn->ForceLayout(rcSave);
    m_cancelBtn->ForceLayout(rcCancel);

    // 悬浮滚动条：贴主体区右缘、与主体区等高；内容放得下时它不显示也不命中。
    m_scrollbar->ForceLayout(RECT{ m_bodyRect.right - OverlayScrollbar::kSuggestedWidth, m_bodyRect.top,
                                   m_bodyRect.right, m_bodyRect.bottom });
    m_scrollbar->SetMetrics(contentH, m_scroll);
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

    // ---- 主体区：裁剪到 m_bodyRect，内容随滚动偏移上移（标题栏与按钮栏不动）----
    const int savedDc = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, m_bodyRect.left, m_bodyRect.top, m_bodyRect.right, m_bodyRect.bottom);
    const int contentTop = m_bodyRect.top - m_scroll;

    // 提示横幅。
    RECT banner = { left, contentTop + kBodyPad,
                    right, contentTop + kBodyPad + kBannerH };
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
    // 账号前缀：标签里写明规则与影响范围（只影响之后新建的账号），表单行数不变，不另占说明行
    ui::DrawTextLine(hdc, _T("账号前缀（字母与点号，只影响新账号）"), RECT{ rx, row2, rx + colW, row2 + kFieldLabelH },
                     theme::kInk500, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

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
            const int nameTop  = top + kAdminNameTop;
            const int emailTop = nameTop + kAdminLineH;
            ui::DrawTextLine(hdc, a.m_name,
                             RECT{ left + 14, nameTop, left + 200, nameTop + kAdminLineH },
                             theme::kInk700, ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ui::DrawTextLine(hdc, a.m_email,
                             RECT{ left + 14, emailTop, right - 150, emailTop + kAdminLineH },
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
            // 只记下主体区内看得见的那部分，滚出主体区的「移除」点不到
            RECT visible;
            if (::IntersectRect(&visible, &rm, &m_bodyRect))
            {
                RemoveHit hit;
                hit.m_rc = visible;
                hit.m_adminId = a.m_adminId;
                m_removeHits.push_back(hit);
            }
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

    // 主体区的子控件（输入框 / 下拉 / 「添加」），同样裁剪到主体区。
    RECT bodyDirty;
    if (::IntersectRect(&bodyDirty, &rcDirty, &m_bodyRect))
    {
        for (const auto& child : m_children)
        {
            if (!child->IsVisible() || !IsBodyChild(child.get()))
            {
                continue;
            }
            const RECT rcChild = child->GetRect();
            RECT rcInter;
            if (::IntersectRect(&rcInter, &rcChild, &bodyDirty))
            {
                child->OnPaint(hdc, rcInter);
            }
        }
    }
    ::RestoreDC(hdc, savedDc);

    // 标题栏的关闭按钮、底部的取消 / 保存与滚动条：不随主体滚动，不裁剪，画在主体内容之上。
    for (const auto& child : m_children)
    {
        if (!child->IsVisible() || IsBodyChild(child.get()))
        {
            continue;
        }
        const RECT rcChild = child->GetRect();
        RECT rcInter;
        if (::IntersectRect(&rcInter, &rcChild, &rcDirty))
        {
            child->OnPaint(hdc, rcInter);
        }
    }
}

balloonwjui::DuiControl* EnterpriseSettingsModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
    if (::PtInRect(&m_panelRect, ptHostClient))
    {
        // 标题栏与按钮栏里的按钮、滚动条不随主体滚动，始终可点，先于主体区的子控件命中。
        for (const auto& child : m_children)
        {
            if (IsBodyChild(child.get()))
            {
                continue;
            }
            if (DuiControl* hit = child->HitTest(ptHostClient))
            {
                return hit;
            }
        }
        // 主体区的子控件（输入框 / 下拉 / 「添加」）只在主体区可视范围内命中：滚出主体区的部分
        // 被标题栏或按钮栏盖住，点不到。
        if (::PtInRect(&m_bodyRect, ptHostClient))
        {
            for (const auto& child : m_children)
            {
                if (!IsBodyChild(child.get()))
                {
                    continue;
                }
                if (DuiControl* hit = child->HitTest(ptHostClient))
                {
                    return hit;
                }
            }
        }
        return this;   // 面板空白 / 自绘「移除」区：由本控件 OnLButtonUp 处理
    }
    return this;       // 遮罩空白处：吞掉点击，不关闭
}

bool EnterpriseSettingsModal::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    if (!m_bVisible)
    {
        return false;
    }
    // 鼠标在卡片上且内容放不下时才滚动主体区
    if (m_scrollMax > 0 && ::PtInRect(&m_panelRect, pt))
    {
        ScrollTo(m_scroll - (static_cast<int>(zDelta) * kWheelStep) / WHEEL_DELTA);
        m_scrollbar->Flash();
    }
    // 弹窗盖住了整个页面：滚轮一律在这里处理掉，下层页面不跟着滚动
    return true;
}

void EnterpriseSettingsModal::ScrollTo(int scroll)
{
    if (scroll > m_scrollMax)
    {
        scroll = m_scrollMax;
    }
    if (scroll < 0)
    {
        scroll = 0;
    }
    if (scroll == m_scroll)
    {
        return;
    }
    m_scroll = scroll;
    // 重新摆放主体区的子控件，并同步滚动条
    ForceLayout(m_rcItem);
    Invalidate();
}

void EnterpriseSettingsModal::OnScrollbarScroll(void* ctx, int newScroll)
{
    EnterpriseSettingsModal* self = static_cast<EnterpriseSettingsModal*>(ctx);
    if (self != nullptr)
    {
        self->ScrollTo(newScroll);
    }
}

bool EnterpriseSettingsModal::IsBodyChild(const DuiControl* child) const
{
    return child != m_closeBtn && child != m_cancelBtn && child != m_saveBtn && child != m_scrollbar;
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
