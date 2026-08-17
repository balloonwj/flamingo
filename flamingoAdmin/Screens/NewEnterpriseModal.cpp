/**
 *  flamingoAdmin —— 新增企业弹窗实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/NewEnterpriseModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiEditHost.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 弹窗布局常量（96 dpi 逻辑像素）----
const int kPanelWidth   = 560;   // 表单卡片宽度
const int kPanelHeight  = 522;   // 表单卡片高度
const int kHeaderH      = 52;    // 卡片标题栏高度
const int kFooterH      = 60;    // 卡片按钮栏高度
const int kBodyPad      = 20;    // 卡片主体内距
const int kBannerH      = 52;    // 提示横幅高度
const int kSectionGap   = 14;    // 主体内各区块间距

const int kFieldLabelH  = 18;    // 字段标签行高
const int kFieldGap     = 6;     // 字段内标签 / 控件 / 提示之间的间距
const int kFieldCtrlH   = 36;    // 字段输入控件高度
const int kHintH        = 16;    // 字段提示行高
const int kFieldH       = kFieldLabelH + kFieldGap + kFieldCtrlH;          // 无提示字段总高 = 60
const int kFieldHintedH = kFieldH + kFieldGap + kHintH;                    // 带提示字段总高 = 82
const int kFormRowGap   = 14;    // 表单行间距

// 半透明遮罩的不透明度（0~255）。
const int kBackdropAlpha = 112;

const int kModalTitlePt = 14;    // 弹窗标题字号（磅）

// 构造一个表单字段：标签（+ 必填项已在标签文字里带 *）+ 输入控件 + 可选提示。
//   label：字段标签文字；control：输入控件（被 move 进来）；
//   hint：控件下方的灰色提示（nullptr / 空串表示无提示）。
std::unique_ptr<DuiControl> MakeField(LPCTSTR label,
                                      std::unique_ptr<DuiControl> control,
                                      LPCTSTR hint)
{
    auto field = std::make_unique<DuiVBox>();
    field->SetGap(kFieldGap);
    field->AddChild(ui::MakeLabel(label, theme::kFontSizeSmall, false,
                                  theme::kInk500),
                    Hint().Fixed(kFieldLabelH));
    field->AddChild(std::move(control), Hint().Fixed(kFieldCtrlH));
    if (hint != nullptr && hint[0] != _T('\0'))
    {
        field->AddChild(ui::MakeLabel(hint, theme::kFontSizeSmall, false,
                                      theme::kInk400),
                        Hint().Fixed(kHintH));
    }
    return std::move(field);
}

// 构造一个单行文本输入框。
std::unique_ptr<DuiEditHost> MakeEdit(int ctrlId, LPCTSTR placeholder)
{
    auto edit = std::make_unique<DuiEditHost>();
    edit->SetCtrlId(ctrlId);
    edit->SetPlaceholder(placeholder);
    edit->SetShowBorder(true);
    edit->SetBgColor(theme::kInk0);
    return edit;
}

// ===========================================================================
// InfoBanner —— 弹窗顶部的浅色提示横幅。
// ===========================================================================
class InfoBanner : public DuiControl
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        // 浅灰圆角底 + 浅边框。
        ui::FillRoundRect(hdc, m_rcItem, theme::kInk25, theme::kRadiusMd);
        ui::StrokeRoundRect(hdc, m_rcItem, theme::kInk100, theme::kRadiusMd, 1.0f);

        // 左侧信息图标。
        const int kGlyph = 16;
        RECT rcIcon =
        {
            m_rcItem.left + 12,          m_rcItem.top + 10,
            m_rcItem.left + 12 + kGlyph, m_rcItem.top + 10 + kGlyph,
        };
        ui::DrawIcon(hdc, ui::Icon::Info, rcIcon, theme::kColorInfo);

        // 提示文字（两行）。
        RECT rcText =
        {
            m_rcItem.left + 12 + kGlyph + 10, m_rcItem.top + 8,
            m_rcItem.right - 12,              m_rcItem.bottom - 8,
        };
        ui::DrawTextLine(hdc,
            _T("企业创建后将进入「待审核」状态，平台运营审核通过后即可")
            _T("使用 FlamingoIM 客户端。"),
            rcText, theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
};

} // anonymous namespace

// ===========================================================================
// NewEnterpriseModal
// ===========================================================================
NewEnterpriseModal::NewEnterpriseModal()
{
    SetVisible(false);              // 初始隐藏，由 AdminFrame 在打开时显示

    // ---- 表单卡片(白色圆角)----
    // DuiVBox + 卡片样式取代历史 ui::CardBox(已删除)。
    auto panel = std::make_unique<DuiVBox>();
    panel->SetBgColor    (theme::kInk0);
    panel->SetCornerRadius(theme::kRadiusLg);
    panel->SetBorderColor(theme::kInk100);
    panel->SetBorderWidth(1.0f);

    // 标题栏：标题 + 关闭按钮。
    auto headerBar = std::make_unique<DuiHBox>();
    headerBar->SetPadding(20, 0, 12, 0);
    headerBar->AddChild(ui::MakeLabel(_T("新增企业"), kModalTitlePt, true,
                                      theme::kInk700),
                        Hint().Weight(1));
    {
        auto closeBtn = std::make_unique<ui::IconButton>();
        closeBtn->SetCtrlId(kIdModalClose);
        closeBtn->SetIcon(ui::Icon::Close);
        closeBtn->SetIconSize(16);
        closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
        headerBar->AddChild(std::move(closeBtn),
                            Hint().Fixed(30, 30).AlignC(DuiLayout::AlignCenter));
    }
    panel->AddChild(std::move(headerBar), Hint().Fixed(kHeaderH));

    // 主体：提示横幅 + 表单。
    auto body = std::make_unique<DuiVBox>();
    body->SetPadding(kBodyPad);
    body->SetGap(kSectionGap);

    body->AddChild(std::make_unique<InfoBanner>(), Hint().Fixed(kBannerH));

    auto form = std::make_unique<DuiVBox>();
    form->SetGap(kFormRowGap);

    // 第 1 行：企业全称（整行）。
    {
        auto edit = MakeEdit(kIdModalName, _T("例如：明远科技有限公司"));
        m_editName = edit.get();
        form->AddChild(MakeField(_T("企业全称 *"), std::move(edit), nullptr),
                       Hint().Fixed(kFieldH));
    }

    // 第 2 行：简称 + 所属行业。
    {
        auto row = std::make_unique<DuiHBox>();
        row->SetGap(kFormRowGap);

        auto editShort = MakeEdit(kIdModalShort, _T("明远科技"));
        m_editShort = editShort.get();
        row->AddChild(MakeField(_T("简称"), std::move(editShort), nullptr),
                      Hint().Weight(1));

        auto cbIndustry = std::make_unique<DuiComboBox>();
        cbIndustry->SetCtrlId(kIdModalIndustry);
        cbIndustry->AddString(_T("软件与信息技术"));
        cbIndustry->AddString(_T("金融服务"));
        cbIndustry->AddString(_T("运输与仓储"));
        cbIndustry->AddString(_T("生物医药"));
        cbIndustry->AddString(_T("广告与设计"));
        cbIndustry->AddString(_T("制造业"));
        cbIndustry->SetCurSel(0, false);
        m_cbIndustry = cbIndustry.get();
        row->AddChild(MakeField(_T("所属行业"), std::move(cbIndustry), nullptr),
                      Hint().Weight(1));

        form->AddChild(std::move(row), Hint().Fixed(kFieldH));
    }

    // 第 3 行：主要地区 + 套餐。
    {
        auto row = std::make_unique<DuiHBox>();
        row->SetGap(kFormRowGap);

        auto editRegion = MakeEdit(kIdModalRegion, _T("例如：上海"));
        m_editRegion = editRegion.get();
        row->AddChild(MakeField(_T("主要地区"), std::move(editRegion), nullptr),
                      Hint().Weight(1));

        auto cbPlan = std::make_unique<DuiComboBox>();
        cbPlan->SetCtrlId(kIdModalPlan);
        cbPlan->AddString(_T("专业版"));
        cbPlan->AddString(_T("企业版"));
        cbPlan->AddString(_T("旗舰版"));
        cbPlan->SetCurSel(0, false);
        m_cbPlan = cbPlan.get();
        row->AddChild(MakeField(_T("套餐"), std::move(cbPlan), nullptr),
                      Hint().Weight(1));

        form->AddChild(std::move(row), Hint().Fixed(kFieldH));
    }

    // 第 4 行：主管理员姓名 + 管理员邮箱（邮箱带提示）。
    {
        auto row = std::make_unique<DuiHBox>();
        row->SetGap(kFormRowGap);

        auto editAdmin = MakeEdit(kIdModalAdmin, _T("例如：李正阳"));
        m_editAdmin = editAdmin.get();
        row->AddChild(MakeField(_T("主管理员姓名 *"), std::move(editAdmin), nullptr),
                      Hint().Weight(1));

        auto editContact = MakeEdit(kIdModalContact, _T("admin@company.com"));
        m_editContact = editContact.get();
        row->AddChild(MakeField(_T("管理员邮箱 *"), std::move(editContact),
                                _T("将向此邮箱发送初始密码与登录指引")),
                      Hint().Weight(1));

        form->AddChild(std::move(row), Hint().Fixed(kFieldHintedH));
    }

    body->AddChild(std::move(form), Hint().Weight(1));
    panel->AddChild(std::move(body), Hint().Weight(1));

    // 按钮栏：取消 + 创建企业（右对齐）。
    auto footerBar = std::make_unique<DuiHBox>();
    footerBar->SetPadding(20, 0, 20, 0);
    footerBar->SetGap(10);
    footerBar->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
    {
        auto cancel = std::make_unique<DuiButton>();
        cancel->SetCtrlId(kIdModalCancel);
        cancel->SetText(_T("取消"));
        cancel->SetVariant(DuiButton::Variant::Default);
        cancel->SetTextPointSize(theme::kFontSizeNormal);
        footerBar->AddChild(std::move(cancel),
                            Hint().Fixed(76, 36).AlignC(DuiLayout::AlignCenter));

        auto submit = std::make_unique<DuiButton>();
        submit->SetCtrlId(kIdModalSubmit);
        submit->SetText(_T("创建企业"));
        submit->SetVariant(DuiButton::Variant::Primary);
        submit->SetTextPointSize(theme::kFontSizeNormal);
        footerBar->AddChild(std::move(submit),
                            Hint().Fixed(104, 36).AlignC(DuiLayout::AlignCenter));
    }
    panel->AddChild(std::move(footerBar), Hint().Fixed(kFooterH));

    m_panel = panel.get();
    AddChild(std::move(panel));

    Reset();
}

void NewEnterpriseModal::Reset()
{
    // 文本框清空；地区预填「上海」（与设计稿默认值一致）。
    if (m_editName    != nullptr) { m_editName->SetText(_T("")); }
    if (m_editShort   != nullptr) { m_editShort->SetText(_T("")); }
    if (m_editRegion  != nullptr) { m_editRegion->SetText(_T("上海")); }
    if (m_editAdmin   != nullptr) { m_editAdmin->SetText(_T("")); }
    if (m_editContact != nullptr) { m_editContact->SetText(_T("")); }
    if (m_cbIndustry  != nullptr) { m_cbIndustry->SetCurSel(0, false); }
    if (m_cbPlan      != nullptr) { m_cbPlan->SetCurSel(0, false); }
}

bool NewEnterpriseModal::Validate() const
{
    CString name    = GetEnterpriseName();
    CString admin   = GetAdminName();
    CString contact = GetContact();
    name.Trim();
    admin.Trim();
    contact.Trim();
    return !name.IsEmpty() && !admin.IsEmpty() && !contact.IsEmpty();
}

CString NewEnterpriseModal::GetEnterpriseName() const
{
    return m_editName != nullptr ? m_editName->GetText() : CString();
}

CString NewEnterpriseModal::GetShortName() const
{
    return m_editShort != nullptr ? m_editShort->GetText() : CString();
}

CString NewEnterpriseModal::GetIndustry() const
{
    return m_cbIndustry != nullptr ? m_cbIndustry->GetText() : CString();
}

CString NewEnterpriseModal::GetRegion() const
{
    return m_editRegion != nullptr ? m_editRegion->GetText() : CString();
}

CString NewEnterpriseModal::GetPlan() const
{
    return m_cbPlan != nullptr ? m_cbPlan->GetText() : CString();
}

CString NewEnterpriseModal::GetAdminName() const
{
    return m_editAdmin != nullptr ? m_editAdmin->GetText() : CString();
}

CString NewEnterpriseModal::GetContact() const
{
    return m_editContact != nullptr ? m_editContact->GetText() : CString();
}

void NewEnterpriseModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 表单卡片在客户区居中。
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelWidth / 2;
    m_panelRect.right  = cx + kPanelWidth / 2;
    m_panelRect.top    = cy - kPanelHeight / 2;
    m_panelRect.bottom = cy + kPanelHeight / 2;

    if (m_panel != nullptr)
    {
        m_panel->ForceLayout(m_panelRect);
    }
}

void NewEnterpriseModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 先压暗整个客户区。
    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    // 再绘制表单卡片（m_panel 是唯一子控件）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);

    // 标题栏 / 按钮栏的分隔线。
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelWidth, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelWidth, 1, theme::kInk100);
}

balloonwjui::DuiControl* NewEnterpriseModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
    // 命中表单卡片 → 下发给卡片子树；命中卡片之外的遮罩 → 返回自身
    // （由 OnLButtonUp 吞掉点击，不关闭弹窗）。
    if (::PtInRect(&m_panelRect, ptHostClient) && m_panel != nullptr)
    {
        return m_panel->HitTest(ptHostClient);
    }
    return this;
}

bool NewEnterpriseModal::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    // 点击遮罩空白处：仅吞掉本次点击（阻断对背后界面的操作），<u>不</u>
    // 关闭弹窗。关闭弹窗只能通过标题栏的 X 按钮或「取消」按钮。
    return true;
}

} // namespace flamingoadmin
