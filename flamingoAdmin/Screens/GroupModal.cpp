/**
 *  flamingoAdmin —— 新建群弹窗 GroupModal 实现。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "Screens/GroupModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Mock/DataStore.h"

#include "Controls/Input/DuiEditHost.h"
#include "DuiHost.h"   // InvalidateDuiRect：滚动 / 勾选时只刷新成员列表面板
#include "Widgets/OverlayScrollbar.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 弹窗布局常量（96 dpi 逻辑像素）----
const int kPanelW    = 560;   // 卡片宽度
const int kHeaderH   = 52;    // 标题栏高度
const int kFooterH   = 60;    // 按钮栏高度
const int kBodyPad   = 20;    // 主体左 / 右 / 上 / 下内距
const int kLabelH    = 18;    // 字段标签高度
const int kFieldGap  = 6;     // 标签与其控件的间距
const int kRowGap    = 12;    // 区块之间的间距

const int kNameEditH = 38;    // 群名输入框高度
const int kListH     = 320;   // 成员列表面板高度
const int kRowH      = 32;    // 成员列表行高
const int kBtnH      = 34;    // 底部按钮高度

// 群名区块高度（标签 + 间距 + 输入框）。
const int kNameBlockH = kLabelH + kFieldGap + kNameEditH;

// 成员列表内容上 / 下内边距（可视高度 = kListH - 2×该值）。
const int kListInnerPadV = 4;
// 悬浮滚动条距列表面板右内缘的间距。
const int kListSbInset = 3;

// 行内勾选框 / 群主单选 / 管理员复选的尺寸与可点区宽度。
const int kCheckSz    = 18;   // 勾选框边长
const int kRadioSz    = 16;   // 群主单选圆点直径
const int kOwnerZoneW = 64;   // 「群主」可点区宽度（单选 + 文字）
const int kAdminZoneW = 86;   // 「管理员」可点区宽度（复选 + 文字）

const int kBackdropAlpha = 112;  // 遮罩不透明度
const int kModalTitlePt  = 14;   // 弹窗标题字号（磅）

// 整窗高度（群名区 + 成员列表区，按固定高度排布）。
int PanelHeight()
{
    const int bodyH = kBodyPad + kNameBlockH + kRowGap
                      + (kLabelH + kFieldGap + kListH) + kBodyPad;
    return kHeaderH + bodyH + kFooterH;
}

// 画一个小方框复选框（已选 = 实心 + 白勾），与 ComposeModal 的画法一致。
void DrawCheckBox(HDC hdc, const RECT& box, bool checked)
{
    ui::FillRoundRect(hdc, box, checked ? theme::kFlamingo500 : theme::kInk0,
                      theme::kRadiusSm);
    if (!checked)
    {
        ui::StrokeRoundRect(hdc, box, theme::kInk200, theme::kRadiusSm, 1.5f);
        return;
    }
    // 白色对勾：三段短线。
    const int cx = box.left;
    const int cy = box.top;
    ui::FillLine(hdc, cx + 4, cy + 9,  4, 2, theme::kInk0);
    ui::FillLine(hdc, cx + 7, cy + 11, 2, 2, theme::kInk0);
    ui::FillLine(hdc, cx + 8, cy + 5,  2, 6, theme::kInk0);
}

// 画一个单选圆点（已选 = 外圈高亮 + 实心内点）。box 为正方形外框。
void DrawRadio(HDC hdc, const RECT& box, bool checked)
{
    const int r = (box.bottom - box.top) / 2;
    ui::StrokeRoundRect(hdc, box, checked ? theme::kFlamingo500 : theme::kInk200,
                        r, 1.5f);
    if (checked)
    {
        RECT inner = { box.left + 4, box.top + 4, box.right - 4, box.bottom - 4 };
        ui::FillRoundRect(hdc, inner, theme::kFlamingo500,
                          (inner.bottom - inner.top) / 2);
    }
}

} // anonymous namespace

GroupModal::GroupModal()
{
    SetVisible(false);
    SetCtrlId(kIdGroupModal);

    // 群名称输入框。
    std::unique_ptr<DuiEditHost> name = std::make_unique<DuiEditHost>();
    name->SetCtrlId(kIdGroupName);
    name->SetPlaceholder(_T("例如：研发一组"));
    name->SetShowBorder(true);
    name->SetBgColor(theme::kInk0);
    m_editName = name.get();
    AddChild(std::move(name));

    // 成员列表悬浮滚动条。须在 close / cancel / submit 之前 AddChild，
    // 以保持 Layout 里「末三个子控件 = 关闭 / 取消 / 创建」的约定。
    std::unique_ptr<OverlayScrollbar> sb = std::make_unique<OverlayScrollbar>();
    m_sb = sb.get();
    m_sb->SetOnScroll(&GroupModal::OnSbScroll, this);
    AddChild(std::move(sb));

    std::unique_ptr<ui::IconButton> closeBtn = std::make_unique<ui::IconButton>();
    closeBtn->SetCtrlId(kIdGroupClose);
    closeBtn->SetIcon(ui::Icon::Close);
    closeBtn->SetIconSize(16);
    closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    AddChild(std::move(closeBtn));

    std::unique_ptr<DuiButton> cancel = std::make_unique<DuiButton>();
    cancel->SetCtrlId(kIdGroupCancel);
    cancel->SetText(_T("取消"));
    cancel->SetVariant(DuiButton::Variant::Default);
    cancel->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(cancel));

    std::unique_ptr<DuiButton> submit = std::make_unique<DuiButton>();
    submit->SetCtrlId(kIdGroupSubmit);
    submit->SetText(_T("创建"));
    submit->SetVariant(DuiButton::Variant::Primary);
    submit->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(submit));
}

void GroupModal::SetEnterprise(const CString& enterpriseId)
{
    m_entId = enterpriseId;
    m_members.clear();
    m_scroll = 0;
    if (m_editName != nullptr)
    {
        m_editName->SetText(_T(""));
    }

    // 从该企业的组织树递归收集全部员工。组织树由企业详情屏先行加载到 DataStore。
    const OrgNode* root = DataStore::Instance().OrgTree(enterpriseId);
    if (root != nullptr)
    {
        CollectMembers(*root, root->m_name);
    }

    // 企业切换会重建列表内容，已在 RootContainer 内时需要重新布局滚动条等。
    if (m_rcItem.right > m_rcItem.left)
    {
        ForceLayout(m_rcItem);
    }
}

void GroupModal::CollectMembers(const OrgNode& node, const CString& deptName)
{
    for (size_t i = 0; i < node.m_children.size(); ++i)
    {
        const OrgNode& child = node.m_children[i];
        if (child.m_kind == OrgNodeKind::User)
        {
            Member m;
            m.m_userid = _ttoi64(child.m_id);
            m.m_name   = child.m_name;
            m.m_dept   = deptName;
            m_members.push_back(m);
        }
        else
        {
            // 子部门：以其名字作为下属员工的部门名继续递归。
            CollectMembers(child, child.m_name);
        }
    }
}

bool GroupModal::Validate() const
{
    CString n = GetGroupName();
    n.Trim();
    if (n.IsEmpty())
    {
        return false;
    }
    return GetOwnerId() != 0;
}

CString GroupModal::GetGroupName() const
{
    return (m_editName != nullptr) ? m_editName->GetText() : CString();
}

long long GroupModal::GetOwnerId() const
{
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_owner)
        {
            return m_members[i].m_userid;
        }
    }
    return 0;
}

std::vector<long long> GroupModal::GetAdminIds() const
{
    std::vector<long long> ids;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        const Member& m = m_members[i];
        if (m.m_joined && m.m_admin && !m.m_owner)
        {
            ids.push_back(m.m_userid);
        }
    }
    return ids;
}

std::vector<long long> GroupModal::GetMemberIds() const
{
    std::vector<long long> ids;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        const Member& m = m_members[i];
        if (m.m_joined && !m.m_owner)
        {
            ids.push_back(m.m_userid);
        }
    }
    return ids;
}

RECT GroupModal::ListRect() const
{
    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;
    const int top = bodyTop + kNameBlockH + kRowGap + kLabelH + kFieldGap;
    RECT rc;
    rc.left   = fl;
    rc.top    = top;
    rc.right  = fr;
    rc.bottom = top + kListH;
    return rc;
}

void GroupModal::InvalidateList()
{
    if (m_pHost == nullptr)
    {
        return;
    }
    // 从「成员」标签行顶部一直到列表面板底部。这块区域不含任何真子窗输入框，
    // 单独刷新它即可让滚动 / 勾选只重绘面板自身，不牵连群名 DuiEditHost。
    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;
    RECT rc = { fl, bodyTop + kNameBlockH + kRowGap,
                fr, bodyTop + kNameBlockH + kRowGap + kLabelH + kFieldGap + kListH };
    m_pHost->InvalidateDuiRect(rc);
}

void GroupModal::OnSbScroll(void* ctx, int newScroll)
{
    GroupModal* self = static_cast<GroupModal*>(ctx);
    self->m_scroll = newScroll;
    self->InvalidateList();
}

void GroupModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    const int panelH = PanelHeight();
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelW / 2;
    m_panelRect.right  = cx + kPanelW / 2;
    m_panelRect.top    = cy - panelH / 2;
    m_panelRect.bottom = cy + panelH / 2;

    // 群名输入框。
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;
    if (m_editName != nullptr)
    {
        m_editName->ForceLayout(RECT{ m_panelRect.left + kBodyPad,
            bodyTop + kLabelH + kFieldGap,
            m_panelRect.right - kBodyPad,
            bodyTop + kLabelH + kFieldGap + kNameEditH });
    }

    // 成员列表悬浮滚动条：摆在列表面板右内缘的细竖条上，高 = 列表可视区高度。
    if (m_sb != nullptr)
    {
        RECT lr = ListRect();
        m_sb->ForceLayout(RECT{
            lr.right - kListSbInset - OverlayScrollbar::kSuggestedWidth,
            lr.top + kListInnerPadV,
            lr.right - kListSbInset,
            lr.bottom - kListInnerPadV });
    }

    // 关闭（右上）、取消 / 创建（右下）：构造时它们是最后加入的 3 个子控件。
    const size_t n = m_children.size();
    if (n >= 3)
    {
        DuiControl* closeBtn = m_children[n - 3].get();
        DuiControl* cancel   = m_children[n - 2].get();
        DuiControl* submit   = m_children[n - 1].get();

        closeBtn->ForceLayout(RECT{ m_panelRect.right - 12 - 30,
            m_panelRect.top + (kHeaderH - 30) / 2,
            m_panelRect.right - 12, m_panelRect.top + (kHeaderH + 30) / 2 });

        const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
        RECT rcSubmit = { m_panelRect.right - 20 - 96, by,
                          m_panelRect.right - 20,      by + kBtnH };
        RECT rcCancel = { rcSubmit.left - 10 - 76, by, rcSubmit.left - 10, by + kBtnH };
        cancel->ForceLayout(rcCancel);
        submit->ForceLayout(rcSubmit);
    }
}

void GroupModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }

    // 半透明遮罩 + 居中卡片。
    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    // 标题。
    RECT rcTitle = { m_panelRect.left + 20, m_panelRect.top,
                     m_panelRect.right - 50, m_panelRect.top + kHeaderH };
    ui::DrawTextLine(hdc, _T("新建群"), rcTitle, theme::kInk700,
                     ui::UiFont(kModalTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 标题栏 / 按钮栏分隔线。
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelW, 1, theme::kInk100);

    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;

    // 群名标签。
    ui::DrawTextLine(hdc, _T("群名称 *"),
                     RECT{ fl, bodyTop, fr, bodyTop + kLabelH },
                     theme::kInk500, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 成员标签 + 已选统计（右对齐）。
    int joined = 0;
    bool hasOwner = false;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_joined)
        {
            ++joined;
        }
        if (m_members[i].m_owner)
        {
            hasOwner = true;
        }
    }
    const int lblTop = bodyTop + kNameBlockH + kRowGap;
    ui::DrawTextLine(hdc, _T("成员（勾选加入 · ★ 群主 · 复选 管理员）"),
                     RECT{ fl, lblTop, fr - 130, lblTop + kLabelH },
                     theme::kInk500, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    CString stat;
    stat.Format(_T("已选 %d 人%s"), joined, hasOwner ? _T("") : _T("（未设群主）"));
    ui::DrawTextLine(hdc, stat, RECT{ fr - 160, lblTop, fr, lblTop + kLabelH },
                     hasOwner ? theme::kInk400 : theme::kFlamingo500,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 成员列表面板（自绘可滚动区，重建命中区）。
    PaintMemberList(hdc, ListRect());

    // 绘制子控件（群名输入框 / 悬浮滚动条 / 按钮）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

void GroupModal::PaintMemberList(HDC hdc, const RECT& rc)
{
    m_hits.clear();

    ui::FillRoundRect(hdc, rc, theme::kInk25, theme::kRadiusMd);
    ui::StrokeRoundRect(hdc, rc, theme::kInk150, theme::kRadiusMd, 1.0f);

    m_contentH = static_cast<int>(m_members.size()) * kRowH;

    if (m_members.empty())
    {
        ui::DrawTextLine(hdc, _T("该企业暂无可选成员"), rc, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (m_sb != nullptr)
        {
            m_sb->SetMetrics(m_contentH, m_scroll);
        }
        return;
    }

    int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    HFONT rowFont = ui::UiFont(theme::kFontSizeSmall, false);

    for (size_t i = 0; i < m_members.size(); ++i)
    {
        const int top = rc.top + kListInnerPadV
                        + static_cast<int>(i) * kRowH - m_scroll;
        if (top + kRowH <= rc.top || top >= rc.bottom)
        {
            continue;   // 列表可视区外的行不绘制
        }
        const Member& m = m_members[i];
        const int cy = top + kRowH / 2;

        // 已加入的行加一层淡底，便于一眼看出选中集合。
        if (m.m_joined)
        {
            ui::FillSolidRect(hdc, RECT{ rc.left + 1, top, rc.right - 1, top + kRowH },
                              theme::kInk50);
        }

        // 右侧「群主」「管理员」可点区。
        RECT adminZone = { rc.right - 12 - kAdminZoneW, top, rc.right - 12, top + kRowH };
        RECT ownerZone = { adminZone.left - 10 - kOwnerZoneW, top,
                           adminZone.left - 10, top + kRowH };

        // 加入勾选框 + 姓名 + 部门。
        RECT joinBox = { rc.left + 14, cy - kCheckSz / 2,
                         rc.left + 14 + kCheckSz, cy + kCheckSz / 2 };
        DrawCheckBox(hdc, joinBox, m.m_joined);

        const int textL = joinBox.right + 10;
        int textMid = textL + 130;
        if (textMid > ownerZone.left - 10)
        {
            textMid = ownerZone.left - 10;
        }
        ui::DrawTextLine(hdc, m.m_name, RECT{ textL, top, textMid, top + kRowH },
                         theme::kInk700, rowFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                         | DT_END_ELLIPSIS);
        if (!m.m_dept.IsEmpty() && textMid + 6 < ownerZone.left - 10)
        {
            ui::DrawTextLine(hdc, m.m_dept,
                             RECT{ textMid + 6, top, ownerZone.left - 10, top + kRowH },
                             theme::kInk400, rowFont,
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                             | DT_END_ELLIPSIS);
        }

        // 群主单选 + 文字。
        RECT ownerRadio = { ownerZone.left, cy - kRadioSz / 2,
                            ownerZone.left + kRadioSz, cy + kRadioSz / 2 };
        DrawRadio(hdc, ownerRadio, m.m_owner);
        ui::DrawTextLine(hdc, _T("群主"),
                         RECT{ ownerRadio.right + 5, top, ownerZone.right, top + kRowH },
                         m.m_owner ? theme::kFlamingo500 : theme::kInk500, rowFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 管理员复选 + 文字（群主不可同时是管理员，置灰）。
        RECT adminBox = { adminZone.left, cy - kCheckSz / 2,
                          adminZone.left + kCheckSz, cy + kCheckSz / 2 };
        DrawCheckBox(hdc, adminBox, m.m_admin && !m.m_owner);
        ui::DrawTextLine(hdc, _T("管理员"),
                         RECT{ adminBox.right + 6, top, adminZone.right, top + kRowH },
                         m.m_owner ? theme::kInk200
                                   : (m.m_admin ? theme::kFlamingo500 : theme::kInk500),
                         rowFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 命中区：左侧整段（勾选框 + 姓名 + 部门）切换加入；右侧两段切角色。
        Hit hJoin;
        hJoin.m_rc = RECT{ rc.left, top, ownerZone.left - 6, top + kRowH };
        hJoin.m_kind = 0;
        hJoin.m_index = static_cast<int>(i);
        m_hits.push_back(hJoin);

        Hit hOwner;
        hOwner.m_rc = ownerZone;
        hOwner.m_kind = 1;
        hOwner.m_index = static_cast<int>(i);
        m_hits.push_back(hOwner);

        Hit hAdmin;
        hAdmin.m_rc = adminZone;
        hAdmin.m_kind = 2;
        hAdmin.m_index = static_cast<int>(i);
        m_hits.push_back(hAdmin);
    }

    if (m_sb != nullptr)
    {
        m_sb->SetMetrics(m_contentH, m_scroll);
    }
    ::RestoreDC(hdc, saved);
}

balloonwjui::DuiControl* GroupModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
    // 命中卡片：优先下发给子控件（群名输入框 / 滚动条 / 按钮）；否则返回自身
    //（成员列表行的点击由本控件 OnLButtonUp 据 m_hits 处理）。
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

bool GroupModal::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (size_t h = 0; h < m_hits.size(); ++h)
    {
        const Hit& hit = m_hits[h];
        if (!::PtInRect(&hit.m_rc, pt))
        {
            continue;
        }
        if (hit.m_index < 0 || hit.m_index >= static_cast<int>(m_members.size()))
        {
            return true;
        }
        Member& m = m_members[hit.m_index];

        if (hit.m_kind == 0)            // 切换加入
        {
            m.m_joined = !m.m_joined;
            if (!m.m_joined)
            {
                // 退出群一并清掉其角色。
                m.m_owner = false;
                m.m_admin = false;
            }
        }
        else if (hit.m_kind == 1)       // 设 / 取消群主（整列表唯一）
        {
            if (m.m_owner)
            {
                m.m_owner = false;
            }
            else
            {
                for (size_t k = 0; k < m_members.size(); ++k)
                {
                    m_members[k].m_owner = false;
                }
                m.m_owner  = true;
                m.m_joined = true;   // 群主必然在群内
                m.m_admin  = false;  // 群主不再同时是管理员
            }
        }
        else if (hit.m_kind == 2)       // 设 / 取消管理员
        {
            if (!m.m_owner)             // 群主不可同时是管理员
            {
                m.m_admin = !m.m_admin;
                if (m.m_admin)
                {
                    m.m_joined = true;   // 管理员必然在群内
                }
            }
        }
        InvalidateList();
        return true;
    }
    // 点击卡片 / 遮罩空白处：仅吞掉，不关闭弹窗。
    return true;
}

bool GroupModal::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    RECT lr = ListRect();
    if (!::PtInRect(&lr, pt))
    {
        return true;
    }
    m_scroll -= (zDelta / WHEEL_DELTA) * kRowH * 2;
    int maxS = m_contentH - (kListH - kListInnerPadV * 2);
    if (maxS < 0)
    {
        maxS = 0;
    }
    if (m_scroll < 0)
    {
        m_scroll = 0;
    }
    if (m_scroll > maxS)
    {
        m_scroll = maxS;
    }
    if (m_sb != nullptr)
    {
        m_sb->SetMetrics(m_contentH, m_scroll);
        m_sb->Flash();
    }
    InvalidateList();
    return true;
}

} // namespace flamingoadmin
