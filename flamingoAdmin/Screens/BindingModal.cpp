/**
 *  flamingoAdmin —— 服务号绑定范围弹窗 BindingModal 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/BindingModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/OverlayScrollbar.h"  // 部门 / 员工列表的悬浮滚动条子控件
#include "Mock/DataStore.h"

#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kPanelW    = 640;
const int kPanelH    = 540;
const int kHeaderH   = 52;
const int kFooterH   = 60;
const int kBodyPad   = 20;
const int kTabH      = 34;   // 模式分段条高度
const int kPickerH   = 210;  // 部门 / 员工列表区高度
const int kSelH      = 72;   // 已选绑定区高度
const int kReachH    = 28;   // 触达提示行高
const int kDeptRowH  = 48;   // 部门行高
const int kUserRowH  = 44;   // 员工行高

const int kBackdropAlpha = 112;
const int kModalTitlePt  = 14;

// 悬浮滚动条距部门 / 员工列表区右内缘的间距（像素）。0：命中带贴区域右缘，滑块离右缘 2 像素由 DuiScrollBar 决定，与 PC 客户端一致。
const int kListSbInset = 0;

const TCHAR* const kModes[] = { _T("整个企业"), _T("按部门"), _T("按员工") };
const int kModeCount = 3;

int MeasureW(LPCTSTR text, HFONT font)
{
    HDC   dc = ::GetDC(nullptr);
    HFONT of = static_cast<HFONT>(::SelectObject(dc, font));
    SIZE  sz = { 0, 0 };
    ::GetTextExtentPoint32(dc, text, lstrlen(text), &sz);
    ::SelectObject(dc, of);
    ::ReleaseDC(nullptr, dc);
    return sz.cx;
}

// 绑定类型配色。
COLORREF BindColor(BindingKind k)
{
    switch (k)
    {
    case BindingKind::Enterprise: return theme::kFlamingo600;
    case BindingKind::Dept:       return theme::kColorInfo;
    case BindingKind::User:       return theme::kColorOk;
    default:                      return theme::kInk400;
    }
}

} // anonymous namespace

BindingModal::BindingModal()
{
    SetVisible(false);

    auto closeBtn = std::make_unique<ui::IconButton>();
    closeBtn->SetCtrlId(kIdBindClose);
    closeBtn->SetIcon(ui::Icon::Close);
    closeBtn->SetIconSize(16);
    closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    AddChild(std::move(closeBtn));

    auto cancel = std::make_unique<DuiButton>();
    cancel->SetCtrlId(kIdBindCancel);
    cancel->SetText(_T("取消"));
    cancel->SetVariant(DuiButton::Variant::Default);
    cancel->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(cancel));

    auto save = std::make_unique<DuiButton>();
    save->SetCtrlId(kIdBindSave);
    save->SetText(_T("保存绑定"));
    save->SetVariant(DuiButton::Variant::Primary);
    save->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(save));

    // 部门 / 员工列表的悬浮滚动条。Layout 用 m_children[0..2] 定位
    // 关闭 / 取消 / 保存三个按钮（FRONT 索引），故本子控件放在它们之后
    // AddChild 不会打乱既有索引。
    auto sb = std::make_unique<OverlayScrollbar>();
    m_listSb = sb.get();
    m_listSb->SetOnScroll(&BindingModal::OnListSbScroll, this);
    AddChild(std::move(sb));
}

void BindingModal::WalkOrg(const OrgNode* node, std::vector<CString>& path,
                           std::vector<DeptItem>& depts,
                           std::vector<UserItem>& users)
{
    if (node == nullptr)
    {
        return;
    }
    if (node->m_kind == OrgNodeKind::Dept)
    {
        //公司节点本身不作为"部门"候选项：投放到整个企业已有单独的一档选项。
        if (!node->m_isCompanyRoot)
        {
            DeptItem d;
            d.m_id   = node->m_id;
            d.m_name = node->m_name;
            for (size_t i = 0; i < path.size(); ++i)
            {
                d.m_path += path[i];
                if (i + 1 < path.size())
                {
                    d.m_path += _T(" / ");
                }
            }
            depts.push_back(d);
        }
        path.push_back(node->m_name);
        for (const auto& c : node->m_children)
        {
            WalkOrg(&c, path, depts, users);
        }
        path.pop_back();
    }
    else
    {
        UserItem u;
        u.m_id    = node->m_id;
        u.m_name  = node->m_name;
        u.m_title = node->m_title;
        u.m_dept  = path.empty() ? CString() : path.back();
        users.push_back(u);
    }
}

void BindingModal::CollectTree()
{
    m_depts.clear();
    m_users.clear();
    std::vector<CString> path;
    WalkOrg(DataStore::Instance().OrgTree(m_entId), path, m_depts, m_users);
}

void BindingModal::Configure(const CString& serviceName,
                             const CString& enterpriseId,
                             const std::vector<ServiceBinding>& bindings)
{
    m_serviceName = serviceName;
    m_entId       = enterpriseId;
    m_bindings    = bindings;
    m_mode        = 0;
    m_listScroll  = 0;
    CollectTree();
}

bool BindingModal::IsBound(BindingKind kind, const CString& id) const
{
    for (const auto& b : m_bindings)
    {
        if (b.m_kind == kind && b.m_id == id)
        {
            return true;
        }
    }
    return false;
}

void BindingModal::ToggleBinding(BindingKind kind, const CString& id,
                                 const CString& label)
{
    for (size_t i = 0; i < m_bindings.size(); ++i)
    {
        if (m_bindings[i].m_kind == kind && m_bindings[i].m_id == id)
        {
            m_bindings.erase(m_bindings.begin() + i);
            Invalidate();
            return;
        }
    }
    ServiceBinding b;
    b.m_kind  = kind;
    b.m_id    = id;
    b.m_label = label;
    m_bindings.push_back(b);
    Invalidate();
}

void BindingModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelW / 2;
    m_panelRect.right  = cx + kPanelW / 2;
    m_panelRect.top    = cy - kPanelH / 2;
    m_panelRect.bottom = cy + kPanelH / 2;

    // 关闭 / 取消 / 保存 按钮。
    m_children[0]->ForceLayout(RECT{ m_panelRect.right - 12 - 30,
        m_panelRect.top + (kHeaderH - 30) / 2,
        m_panelRect.right - 12, m_panelRect.top + (kHeaderH + 30) / 2 });
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - 34) / 2;
    RECT rcSave   = { m_panelRect.right - 20 - 104, by,
                      m_panelRect.right - 20,       by + 34 };
    RECT rcCancel = { rcSave.left - 10 - 76, by, rcSave.left - 10, by + 34 };
    m_children[1]->ForceLayout(rcCancel);
    m_children[2]->ForceLayout(rcSave);

    // 部门 / 员工列表滚动区。
    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int y1 = m_panelRect.top + kHeaderH + kBodyPad + kTabH + 14;
    m_pickerRect = RECT{ fl, y1, fr, y1 + kPickerH };

    // 列表悬浮滚动条：摆在列表区右内缘的细竖条上，高 = 列表区高度；仅
    // 「按部门 / 按员工」两种模式下才有可滚动列表，「整个企业」模式隐藏它。
    const bool listVisible = (m_mode == 1 || m_mode == 2);
    m_listSb->SetVisible(listVisible);
    if (listVisible)
    {
        m_listSb->ForceLayout(RECT{
            m_pickerRect.right - kListSbInset - OverlayScrollbar::kSuggestedWidth,
            m_pickerRect.top,
            m_pickerRect.right - kListSbInset,
            m_pickerRect.bottom });
    }
}

void BindingModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    m_hits.clear();
    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);

    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    // 标题。
    CString title;
    title.Format(_T("修改绑定范围 · %s"), static_cast<LPCTSTR>(m_serviceName));
    RECT rcTitle = { m_panelRect.left + 20, m_panelRect.top,
                     m_panelRect.right - 50, m_panelRect.top + kHeaderH };
    ui::DrawTextLine(hdc, title, rcTitle, theme::kInk700,
                     ui::UiFont(kModalTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelW, 1, theme::kInk100);

    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int fw = fr - fl;

    // ---- 模式分段 ----
    int ty = m_panelRect.top + kHeaderH + kBodyPad;
    const int segW = fw / kModeCount;
    for (int i = 0; i < kModeCount; ++i)
    {
        RECT seg = { fl + i * segW, ty, fl + (i + 1) * segW, ty + kTabH };
        const bool act = (i == m_mode);
        ui::FillRoundRect(hdc, seg, act ? theme::kFlamingo50 : theme::kInk0,
                          theme::kRadiusMd);
        ui::StrokeRoundRect(hdc, seg, act ? theme::kFlamingo300 : theme::kInk150,
                            theme::kRadiusMd, 1.0f);
        ui::DrawTextLine(hdc, kModes[i], seg,
                         act ? theme::kFlamingo700 : theme::kInk600,
                         ui::UiFont(theme::kFontSizeNormal, act),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit h; h.m_rc = seg; h.m_kind = 0; h.m_arg = i;
        m_hits.push_back(h);
    }

    // ---- 选择区 ----
    const RECT& pk = m_pickerRect;
    if (m_mode == 0)
    {
        // 整个企业。
        const Enterprise* ent = DataStore::Instance().FindEnterprise(m_entId);
        const CString shortName = ent != nullptr ? ent->m_shortName : CString();
        const CString label = CString(_T("全员（")) + shortName + _T("）");
        const bool bound = IsBound(BindingKind::Enterprise, _T("root"));

        RECT card = { pk.left, pk.top, pk.right, pk.top + 76 };
        ui::DrawCard(hdc, card);
        RECT ib = { pk.left + 16, pk.top + 18, pk.left + 16 + 40, pk.top + 18 + 40 };
        ui::FillRoundRect(hdc, ib, RGB(0xFF, 0xF1, 0xEF), theme::kRadiusMd);
        RECT ii = { (ib.left + ib.right) / 2 - 10, (ib.top + ib.bottom) / 2 - 10,
                    (ib.left + ib.right) / 2 + 10, (ib.top + ib.bottom) / 2 + 10 };
        ui::DrawIcon(hdc, ui::Icon::Building, ii, theme::kFlamingo600);
        RECT nm = { ib.right + 14, pk.top + 18, pk.right - 130, pk.top + 40 };
        ui::DrawTextLine(hdc, ent != nullptr ? ent->m_name : CString(), nm,
                         theme::kInk700, ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        RECT sub = { ib.right + 14, pk.top + 40, pk.right - 130, pk.top + 58 };
        ui::DrawTextLine(hdc, _T("全员可见 · 含未来新成员"), sub, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT btn = { pk.right - 16 - 96, pk.top + 22, pk.right - 16, pk.top + 22 + 32 };
        ui::FillRoundRect(hdc, btn, bound ? theme::kFlamingo500 : theme::kInk0,
                          theme::kRadiusMd);
        if (!bound)
        {
            ui::StrokeRoundRect(hdc, btn, theme::kInk150, theme::kRadiusMd, 1.0f);
        }
        ui::DrawTextLine(hdc, bound ? _T("已绑定") : _T("绑定"), btn,
                         bound ? theme::kInk0 : theme::kInk600,
                         ui::UiFont(theme::kFontSizeNormal, bound),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit h; h.m_rc = btn; h.m_kind = 1; h.m_bindKind = BindingKind::Enterprise;
        h.m_bindId = _T("root"); h.m_bindLabel = label;
        m_hits.push_back(h);
    }
    else
    {
        // 部门 / 员工列表（可滚动）。
        ui::FillRoundRect(hdc, pk, theme::kInk0, theme::kRadiusLg);
        ui::StrokeRoundRect(hdc, pk, theme::kInk150, theme::kRadiusLg, 1.0f);

        int saved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, pk.left, pk.top, pk.right, pk.bottom);

        const int rowH = (m_mode == 1) ? kDeptRowH : kUserRowH;
        const int count = (m_mode == 1) ? static_cast<int>(m_depts.size())
                                        : static_cast<int>(m_users.size());
        m_listContentH = count * rowH;
        // 把最新内容总高 / 滚动量同步给悬浮滚动条子控件；它作为子控件会在
        // 本函数末尾的 base OnPaint 里绘制自身（悬浮于列表区右缘、auto-hide）。
        m_listSb->SetMetrics(m_listContentH, m_listScroll);
        for (int i = 0; i < count; ++i)
        {
            const int top = pk.top + i * rowH - m_listScroll;
            if (top + rowH <= pk.top || top >= pk.bottom)
            {
                continue;   // 不在可视区，跳过
            }
            const int cy = top + rowH / 2;
            if (i > 0)
            {
                ui::FillLine(hdc, pk.left + 12, top, pk.right - pk.left - 24,
                             1, theme::kInk100);
            }
            BindingKind bk;
            CString bid, blabel, line1, line2;
            if (m_mode == 1)
            {
                bk = BindingKind::Dept;
                bid = m_depts[i].m_id;
                blabel = m_depts[i].m_name;
                line1 = m_depts[i].m_name;
                line2 = m_depts[i].m_path;
                // 部门图标。
                RECT ib = { pk.left + 14, cy - 13, pk.left + 14 + 26, cy + 13 };
                ui::FillRoundRect(hdc, ib, RGB(0xE6, 0xEF, 0xFF), theme::kRadiusSm);
                RECT ii = { ib.left + 6, ib.top + 6, ib.right - 6, ib.bottom - 6 };
                ui::DrawIcon(hdc, ui::Icon::Folder, ii, theme::kColorInfo);
            }
            else
            {
                bk = BindingKind::User;
                bid = m_users[i].m_id;
                blabel = m_users[i].m_name;
                line1 = m_users[i].m_name;
                line2 = m_users[i].m_title + _T(" · ") + m_users[i].m_dept;
                RECT av = { pk.left + 14, cy - 13, pk.left + 14 + 26, cy + 13 };
                ui::DrawAvatar(hdc, av, ui::AvatarColor(m_users[i].m_name),
                               ui::Initials(m_users[i].m_name));
            }
            RECT r1 = { pk.left + 50, top + 6, pk.right - 110, top + rowH / 2 + 2 };
            ui::DrawTextLine(hdc, line1, r1, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, m_mode == 1),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            RECT r2 = { pk.left + 50, top + rowH / 2 - 2, pk.right - 110, top + rowH - 4 };
            ui::DrawTextLine(hdc, line2, r2, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            const bool bound = IsBound(bk, bid);
            RECT btn = { pk.right - 14 - 76, cy - 14, pk.right - 14, cy + 14 };
            ui::FillRoundRect(hdc, btn, bound ? theme::kFlamingo500 : theme::kInk0,
                              theme::kRadiusSm);
            if (!bound)
            {
                ui::StrokeRoundRect(hdc, btn, theme::kInk150, theme::kRadiusSm, 1.0f);
            }
            ui::DrawTextLine(hdc, bound ? _T("已绑定") : _T("绑定"), btn,
                             bound ? theme::kInk0 : theme::kInk600,
                             ui::UiFont(theme::kFontSizeSmall, bound),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            // 仅当按钮在可视区内时记录命中。
            if (btn.bottom > pk.top && btn.top < pk.bottom)
            {
                Hit h; h.m_rc = btn; h.m_kind = 1; h.m_bindKind = bk;
                h.m_bindId = bid; h.m_bindLabel = blabel;
                m_hits.push_back(h);
            }
        }
        ::RestoreDC(hdc, saved);
    }

    // ---- 已选绑定 ----
    int sy = pk.bottom + 12;
    CString selTitle;
    selTitle.Format(_T("已选 %d 项"), static_cast<int>(m_bindings.size()));
    ui::DrawTextLine(hdc, selTitle, RECT{ fl, sy, fr, sy + 16 }, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    sy += 22;
    int chipX = fl, chipY = sy;
    HFONT chipFont = ui::UiFont(theme::kFontSizeSmall, false);
    for (const auto& b : m_bindings)
    {
        const int tw = MeasureW(b.m_label, chipFont);
        const int cw = 10 + tw + 8 + 14 + 8;   // 左padding + 文字 + 间隔 + × + 右padding
        if (chipX + cw > fr)
        {
            chipX = fl;
            chipY += 30;
        }
        RECT chip = { chipX, chipY, chipX + cw, chipY + 24 };
        ui::FillRoundRect(hdc, chip, theme::kInk50, theme::kRadiusSm);
        ui::StrokeRoundRect(hdc, chip, theme::kInk150, theme::kRadiusSm, 1.0f);
        RECT ct = { chip.left + 10, chip.top, chip.left + 10 + tw, chip.bottom };
        ui::DrawTextLine(hdc, b.m_label, ct, BindColor(b.m_kind), chipFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // × 删除。
        RECT x = { chip.right - 8 - 14, chip.top + 5, chip.right - 8, chip.bottom - 5 };
        ui::DrawIcon(hdc, ui::Icon::Close, x, theme::kInk400);
        Hit h; h.m_rc = chip; h.m_kind = 1; h.m_bindKind = b.m_kind;
        h.m_bindId = b.m_id; h.m_bindLabel = b.m_label;
        m_hits.push_back(h);
        chipX += cw + 6;
    }

    // ---- 触达提示 ----
    const int reach = DataStore::Instance().ComputeReach(m_entId, m_bindings);
    RECT rch = { fl, m_panelRect.bottom - kFooterH - kReachH - 8, fr,
                 m_panelRect.bottom - kFooterH - 8 };
    ui::FillRoundRect(hdc, rch, theme::kFlamingo50, theme::kRadiusMd);
    CString reachText;
    reachText.Format(_T("当前选择将触达 %d 名员工"), reach);
    ui::DrawTextLine(hdc, reachText, rch, theme::kFlamingo700,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

balloonwjui::DuiControl* BindingModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
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
    return this;
}

bool BindingModal::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (const Hit& h : m_hits)
    {
        if (!::PtInRect(&h.m_rc, pt))
        {
            continue;
        }
        if (h.m_kind == 0)            // 切模式
        {
            if (h.m_arg != m_mode)
            {
                m_mode       = h.m_arg;
                m_listScroll = 0;
                Invalidate();
            }
        }
        else if (h.m_kind == 1)       // 切换绑定
        {
            ToggleBinding(h.m_bindKind, h.m_bindId, h.m_bindLabel);
        }
        return true;
    }
    return true;   // 点遮罩 / 空白：吞掉，不关闭
}

bool BindingModal::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    if ((m_mode == 1 || m_mode == 2) && ::PtInRect(&m_pickerRect, pt))
    {
        m_listScroll -= (zDelta / WHEEL_DELTA) * 48;
        int maxScroll = m_listContentH - (m_pickerRect.bottom - m_pickerRect.top);
        if (maxScroll < 0) { maxScroll = 0; }
        if (m_listScroll < 0) { m_listScroll = 0; }
        if (m_listScroll > maxScroll) { m_listScroll = maxScroll; }
        // 同步滚动量给悬浮滚动条并令其淡入（停止滚动后自动淡出）。
        m_listSb->SetMetrics(m_listContentH, m_listScroll);
        m_listSb->Flash();
        Invalidate();
    }
    return true;
}

void BindingModal::OnListSbScroll(void* ctx, int newScroll)
{
    // OverlayScrollbar 拖动滑块的回调：把新滚动量写回本弹窗并重绘列表。
    BindingModal* self = static_cast<BindingModal*>(ctx);
    self->m_listScroll = newScroll;
    self->Invalidate();
}

} // namespace flamingoadmin
