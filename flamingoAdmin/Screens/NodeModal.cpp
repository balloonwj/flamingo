/**
 *  flamingoAdmin —— 组织节点弹窗 NodeModal 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/NodeModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Input/DuiEditHost.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 弹窗布局常量（96 dpi 逻辑像素）----
const int kPanelWidth   = 520;   // 表单卡片宽度
const int kHeaderH      = 52;    // 标题栏高度
const int kFooterH      = 60;    // 按钮栏高度
const int kBodyPad      = 20;    // 主体内距
const int kBannerH      = 52;    // 提示横幅高度
const int kFieldLabelH  = 16;    // 字段标签行高
const int kFieldGap     = 6;     // 标签与控件间距
const int kFieldCtrlH   = 34;    // 输入控件高度
const int kFieldH       = kFieldLabelH + kFieldGap + kFieldCtrlH;   // 单字段高 = 56
const int kRowGap       = 14;    // 字段行间距
const int kColGap       = 14;    // 双列字段列间距

const int kBackdropAlpha = 112;  // 遮罩不透明度
const int kModalTitlePt  = 14;   // 弹窗标题字号

// 按模式算面板高度。
int PanelHeight(NodeModal::Mode mode)
{
    // 部门模式 2 个字段；员工模式 3 行字段。
    const int rows  = (mode == NodeModal::Mode::AddDept) ? 2 : 3;
    const int formH = rows * kFieldH + (rows - 1) * kRowGap;
    return kHeaderH + (kBodyPad + kBannerH + kRowGap + formH + kBodyPad) + kFooterH;
}

} // anonymous namespace

NodeModal::NodeModal()
{
    SetVisible(false);

    // ---- 输入控件 ----
    auto mkEdit = [](int ctrlId, LPCTSTR placeholder) -> std::unique_ptr<DuiEditHost>
    {
        auto e = std::make_unique<DuiEditHost>();
        e->SetCtrlId(ctrlId);
        e->SetPlaceholder(placeholder);
        e->SetShowBorder(true);
        e->SetBgColor(theme::kInk0);
        return e;
    };

    auto deptName = mkEdit(kIdNodeDeptName, _T("例如：增长团队"));
    m_editDeptName = deptName.get();
    AddChild(std::move(deptName));

    auto deptLeader = mkEdit(kIdNodeDeptLead, _T("负责人姓名（可选）"));
    m_editDeptLeader = deptLeader.get();
    AddChild(std::move(deptLeader));

    auto userName = mkEdit(kIdNodeUserName, _T("员工姓名"));
    m_editUserName = userName.get();
    AddChild(std::move(userName));

    auto userTitle = mkEdit(kIdNodeUserTitle, _T("职位"));
    m_editUserTitle = userTitle.get();
    AddChild(std::move(userTitle));

    auto userEmail = mkEdit(kIdNodeUserEmail, _T("name@company.com"));
    m_editUserEmail = userEmail.get();
    AddChild(std::move(userEmail));

    auto userPhone = mkEdit(kIdNodeUserPhone, _T("手机号（可选）"));
    m_editUserPhone = userPhone.get();
    AddChild(std::move(userPhone));

    auto gender = std::make_unique<DuiComboBox>();
    gender->SetCtrlId(kIdNodeUserGender);
    gender->AddString(_T("男"));
    gender->AddString(_T("女"));
    gender->AddString(_T("其他"));
    gender->SetCurSel(0, false);
    m_cbUserGender = gender.get();
    AddChild(std::move(gender));

    // ---- 关闭 / 取消 / 创建 按钮 ----
    auto closeBtn = std::make_unique<ui::IconButton>();
    closeBtn->SetCtrlId(kIdNodeClose);
    closeBtn->SetIcon(ui::Icon::Close);
    closeBtn->SetIconSize(16);
    closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    AddChild(std::move(closeBtn));

    auto cancel = std::make_unique<DuiButton>();
    cancel->SetCtrlId(kIdNodeCancel);
    cancel->SetText(_T("取消"));
    cancel->SetVariant(DuiButton::Variant::Default);
    cancel->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(cancel));

    auto submit = std::make_unique<DuiButton>();
    submit->SetCtrlId(kIdNodeSubmit);
    submit->SetText(_T("创建"));
    submit->SetVariant(DuiButton::Variant::Primary);
    submit->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(submit));

    SetMode(Mode::AddDept, CString());
}

void NodeModal::SetMode(Mode mode, const CString& parentName)
{
    m_mode       = mode;
    m_parentName = parentName;

    // 按模式切换字段可见性（部门 / 员工两组互斥）。
    const bool dept = (mode == Mode::AddDept);
    m_editDeptName->SetVisible(dept);
    m_editDeptLeader->SetVisible(dept);
    m_editUserName->SetVisible(!dept);
    m_editUserTitle->SetVisible(!dept);
    m_editUserEmail->SetVisible(!dept);
    m_editUserPhone->SetVisible(!dept);
    m_cbUserGender->SetVisible(!dept);

    ResetFields();

    // 模式变化导致面板高度变化，须重新布局。
    if (m_rcItem.right > m_rcItem.left)
    {
        ForceLayout(m_rcItem);
    }
}

void NodeModal::ResetFields()
{
    m_editDeptName->SetText(_T(""));
    m_editDeptLeader->SetText(_T(""));
    m_editUserName->SetText(_T(""));
    m_editUserTitle->SetText(_T(""));
    m_editUserEmail->SetText(_T(""));
    m_editUserPhone->SetText(_T(""));
    m_cbUserGender->SetCurSel(0, false);
}

bool NodeModal::Validate() const
{
    if (m_mode == Mode::AddDept)
    {
        CString n = GetDeptName();
        n.Trim();
        return !n.IsEmpty();
    }
    CString name = GetUserName();
    CString mail = GetUserEmail();
    name.Trim();
    mail.Trim();
    return !name.IsEmpty() && !mail.IsEmpty();
}

CString NodeModal::GetDeptName() const   { return m_editDeptName->GetText(); }
CString NodeModal::GetDeptLeader() const { return m_editDeptLeader->GetText(); }
CString NodeModal::GetUserName() const   { return m_editUserName->GetText(); }
CString NodeModal::GetUserTitle() const  { return m_editUserTitle->GetText(); }
CString NodeModal::GetUserEmail() const  { return m_editUserEmail->GetText(); }
CString NodeModal::GetUserPhone() const  { return m_editUserPhone->GetText(); }
CString NodeModal::GetUserGender() const { return m_cbUserGender->GetText(); }

void NodeModal::BuildFieldLayout(std::vector<FieldRect>& out) const
{
    out.clear();

    const int formLeft  = m_panelRect.left + kBodyPad;
    const int formRight = m_panelRect.right - kBodyPad;
    const int formW     = formRight - formLeft;
    const int colW      = (formW - kColGap) / 2;
    const int formTop   = m_panelRect.top + kHeaderH + kBodyPad + kBannerH + kRowGap;

    // 追加一个字段描述。
    auto add = [&](DuiControl* ctrl, LPCTSTR label, int x, int y, int w)
    {
        FieldRect fr;
        fr.m_ctrl     = ctrl;
        fr.m_label    = label;
        fr.m_label_rc = RECT{ x, y, x + w, y + kFieldLabelH };
        fr.m_ctrl_rc  = RECT{ x, y + kFieldLabelH + kFieldGap,
                              x + w, y + kFieldLabelH + kFieldGap + kFieldCtrlH };
        out.push_back(fr);
    };

    if (m_mode == Mode::AddDept)
    {
        int y = formTop;
        add(m_editDeptName,   _T("部门名称 *"), formLeft, y, formW);
        y += kFieldH + kRowGap;
        add(m_editDeptLeader, _T("部门负责人"), formLeft, y, formW);
    }
    else
    {
        const int rx = formLeft + colW + kColGap;
        int y = formTop;
        add(m_editUserName,  _T("姓名 *"), formLeft, y, colW);
        add(m_editUserTitle, _T("职位"),   rx,       y, colW);
        y += kFieldH + kRowGap;
        add(m_editUserEmail, _T("邮箱 *"), formLeft, y, colW);
        add(m_editUserPhone, _T("手机号"), rx,       y, colW);
        y += kFieldH + kRowGap;
        add(m_cbUserGender,  _T("性别"),   formLeft, y, colW);
    }
}

void NodeModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 居中算面板矩形（高度随模式变化）。
    const int panelH = PanelHeight(m_mode);
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelWidth / 2;
    m_panelRect.right  = cx + kPanelWidth / 2;
    m_panelRect.top    = cy - panelH / 2;
    m_panelRect.bottom = cy + panelH / 2;

    // 按已知顺序定位 关闭 / 取消 / 创建（构造时它们是最后加入的 3 个子控件，
    // 即 m_children 末尾三项：close、cancel、submit）。
    const size_t n = m_children.size();
    if (n >= 3)
    {
        DuiControl* closeBtn = m_children[n - 3].get();
        DuiControl* cancel   = m_children[n - 2].get();
        DuiControl* submit   = m_children[n - 1].get();

        RECT rcClose = { m_panelRect.right - 12 - 30,
                         m_panelRect.top + (kHeaderH - 30) / 2,
                         m_panelRect.right - 12,
                         m_panelRect.top + (kHeaderH + 30) / 2 };
        closeBtn->ForceLayout(rcClose);

        const int by = m_panelRect.bottom - kFooterH + (kFooterH - 34) / 2;
        RECT rcSubmit = { m_panelRect.right - 20 - 96, by,
                          m_panelRect.right - 20,      by + 34 };
        RECT rcCancel = { rcSubmit.left - 10 - 76, by, rcSubmit.left - 10, by + 34 };
        cancel->ForceLayout(rcCancel);
        submit->ForceLayout(rcSubmit);
    }

    // 定位当前模式的字段控件。
    std::vector<FieldRect> fields;
    BuildFieldLayout(fields);
    for (const FieldRect& fr : fields)
    {
        fr.m_ctrl->ForceLayout(fr.m_ctrl_rc);
    }
}

void NodeModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 半透明遮罩。
    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);

    // 表单卡片。
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    // 标题。
    RECT rcTitle = { m_panelRect.left + 20, m_panelRect.top,
                     m_panelRect.right - 50, m_panelRect.top + kHeaderH };
    ui::DrawTextLine(hdc,
                     m_mode == Mode::AddDept ? _T("新增部门") : _T("新增员工"),
                     rcTitle, theme::kInk700, ui::UiFont(kModalTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 标题栏 / 按钮栏分隔线。
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelWidth, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelWidth, 1, theme::kInk100);

    // 提示横幅：挂载至 X。
    RECT banner = { m_panelRect.left + kBodyPad, m_panelRect.top + kHeaderH + kBodyPad,
                    m_panelRect.right - kBodyPad,
                    m_panelRect.top + kHeaderH + kBodyPad + kBannerH };
    ui::FillRoundRect(hdc, banner, theme::kInk25, theme::kRadiusMd);
    ui::StrokeRoundRect(hdc, banner, theme::kInk100, theme::kRadiusMd, 1.0f);
    RECT bi = { banner.left + 12, (banner.top + banner.bottom) / 2 - 8,
                banner.left + 28, (banner.top + banner.bottom) / 2 + 8 };
    ui::DrawIcon(hdc, ui::Icon::Folder, bi, theme::kFlamingo500);
    CString bannerText;
    bannerText.Format(_T("挂载至：%s"),
                      static_cast<LPCTSTR>(m_parentName.IsEmpty()
                          ? CString(_T("—")) : m_parentName));
    RECT bt = { banner.left + 38, banner.top, banner.right - 12, banner.bottom };
    ui::DrawTextLine(hdc, bannerText, bt, theme::kInk500,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // 字段标签。
    std::vector<FieldRect> fields;
    BuildFieldLayout(fields);
    for (const FieldRect& fr : fields)
    {
        ui::DrawTextLine(hdc, fr.m_label, fr.m_label_rc, theme::kInk500,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 绘制子控件（输入框 / 下拉框 / 按钮）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

balloonwjui::DuiControl* NodeModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
    // 命中面板：优先下发给子控件（输入框 / 按钮）；否则返回自身。
    if (::PtInRect(&m_panelRect, ptHostClient))
    {
        for (const auto& child : m_children)
        {
            if (DuiControl* hit = child->HitTest(ptHostClient))
            {
                return hit;
            }
        }
        return this;
    }
    // 命中遮罩空白处：返回自身（OnLButtonUp 吞掉点击，不关闭）。
    return this;
}

bool NodeModal::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    // 点击遮罩 / 面板空白处：仅吞掉点击，不关闭弹窗。
    return true;
}

} // namespace flamingoadmin
