/**
 *  flamingoAdmin —— 组织树控件 OrgTree 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Widgets/OrgTree.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/OverlayScrollbar.h"   // 树区右缘的悬浮 auto-hide 滚动条子控件
#include "Util/Pinyin.h"                // 拼音首字母搜索

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 组织树布局常量（96 dpi 逻辑像素）----
const int kRowH       = 30;   // 行高
const int kIndent     = 16;   // 每级缩进
const int kRowPadL    = 8;    // 行左内距
const int kRowMargin  = 4;    // 行圆角底相对行矩形的左 / 右内缩
const int kChevronW   = 16;   // 展开箭头列宽
const int kNodeIcon   = 18;   // 节点图标 / 头像边长
const int kGap        = 6;    // 图标与文字间距
const int kActionBtnW = 24;   // hover 动作按钮宽度
const int kActionIcon = 13;   // 动作按钮内图标边长
const int kAccentW    = 2;    // 选中行左侧强调条宽度
const int kWheelRows  = 3;    // 滚轮一格滚动的行数
const int kSbInset    = 3;    // 悬浮滚动条距树区右内缘的间距（像素）

// 递归把 node 子树里所有部门 id 收集进 set。
void CollectDeptIds(const OrgNode* node, std::set<CString>& out)
{
    if (node == nullptr || node->m_kind != OrgNodeKind::Dept)
    {
        return;
    }
    out.insert(node->m_id);
    for (const auto& child : node->m_children)
    {
        CollectDeptIds(&child, out);
    }
}

} // anonymous namespace

OrgTree::OrgTree()
{
    // 树区右缘的悬浮 auto-hide 滚动条：内容溢出时才出现，滚动 / 拖动时淡入。
    auto sb = std::make_unique<OverlayScrollbar>();
    m_treeSb = sb.get();
    m_treeSb->SetOnScroll(&OrgTree::OnTreeSbScroll, this);
    AddChild(std::move(sb));
}

void OrgTree::SetCallbacks(SelectFn selFn, ActionFn actFn, void* user)
{
    m_selFn  = selFn;
    m_actFn  = actFn;
    m_cbUser = user;
}

void OrgTree::SetRoot(const OrgNode* root)
{
    m_root = root;
    m_expanded.clear();
    m_selectedId.Empty();
    m_scrollY  = 0;
    m_hoverRow = -1;

    // 默认展开：根 + 根的一级子部门。
    if (m_root != nullptr)
    {
        m_expanded.insert(m_root->m_id);
        for (const auto& child : m_root->m_children)
        {
            if (child.m_kind == OrgNodeKind::Dept)
            {
                m_expanded.insert(child.m_id);
            }
        }
    }
    Flatten();
    ClampScroll();
    Invalidate();
}

void OrgTree::Rebuild()
{
    Flatten();
    ClampScroll();
    Invalidate();
}

void OrgTree::SetSelectedId(const CString& nodeId)
{
    m_selectedId = nodeId;
    Invalidate();
}

void OrgTree::ExpandAll()
{
    m_expanded.clear();
    CollectDeptIds(m_root, m_expanded);
    Flatten();
    ClampScroll();
    Invalidate();
}

void OrgTree::CollapseAll()
{
    m_expanded.clear();
    if (m_root != nullptr)
    {
        m_expanded.insert(m_root->m_id);   // 根始终展开
    }
    Flatten();
    ClampScroll();
    Invalidate();
}

void OrgTree::Flatten()
{
    m_rows.clear();
    if (m_root != nullptr)
    {
        FlattenInto(m_root, 0);
    }
}

void OrgTree::FlattenInto(const OrgNode* node, int depth)
{
    const bool filtering = !m_filter.IsEmpty();

    // 过滤态下:该子树无任何匹配则整支跳过(只保留匹配节点 + 其上级路径)。
    if (filtering && !SubtreeMatches(node))
    {
        return;
    }

    Row row;
    row.m_node  = node;
    row.m_depth = depth;
    m_rows.push_back(row);

    // 过滤态下强制展开(让匹配可见);非过滤态按 m_expanded。
    const bool expand = filtering
        ? true
        : (m_expanded.find(node->m_id) != m_expanded.end());
    if (node->m_kind == OrgNodeKind::Dept && expand)
    {
        for (const auto& child : node->m_children)
        {
            FlattenInto(&child, depth + 1);
        }
    }
}

bool OrgTree::NodeMatches(const OrgNode* node) const
{
    if (node == nullptr || m_filter.IsEmpty())
    {
        return false;
    }
    // ① 名称子串匹配(大小写不敏感)。
    CString name = node->m_name;
    name.MakeLower();
    if (name.Find(m_filter) >= 0)
    {
        return true;
    }
    // ② 拼音首字母子串匹配:如 "yfb" 命中"研发部"、"zs" 命中"张三"。
    //    m_filter 已小写;PinyinInitials 输出亦为小写。
    const CString py = PinyinInitials(node->m_name);
    if (!py.IsEmpty() && py.Find(m_filter) >= 0)
    {
        return true;
    }
    return false;
}

bool OrgTree::SubtreeMatches(const OrgNode* node) const
{
    if (node == nullptr)
    {
        return false;
    }
    if (NodeMatches(node))
    {
        return true;
    }
    for (const auto& child : node->m_children)
    {
        if (SubtreeMatches(&child))
        {
            return true;
        }
    }
    return false;
}

void OrgTree::SetFilter(const CString& keyword)
{
    CString kw = keyword;
    kw.Trim();
    kw.MakeLower();
    if (kw == m_filter)
    {
        return;
    }
    m_filter  = kw;
    m_scrollY = 0;          // 过滤变化后回到顶部
    Flatten();
    ClampScroll();
    Invalidate();
}

std::vector<CString> OrgTree::Test_VisibleNames() const
{
    std::vector<CString> out;
    out.reserve(m_rows.size());
    for (const Row& r : m_rows)
    {
        out.push_back(r.m_node->m_name);
    }
    return out;
}

void OrgTree::ClampScroll()
{
    const int viewH    = m_rcItem.bottom - m_rcItem.top;
    const int contentH = static_cast<int>(m_rows.size()) * kRowH;
    int maxScroll = contentH - viewH;
    if (maxScroll < 0)
    {
        maxScroll = 0;
    }
    if (m_scrollY < 0)
    {
        m_scrollY = 0;
    }
    if (m_scrollY > maxScroll)
    {
        m_scrollY = maxScroll;
    }
}

RECT OrgTree::RowRect(int i) const
{
    const int top = m_rcItem.top + i * kRowH - m_scrollY;
    RECT rc = { m_rcItem.left, top, m_rcItem.right, top + kRowH };
    return rc;
}

int OrgTree::RowAtPoint(POINT pt) const
{
    if (!::PtInRect(&m_rcItem, pt))
    {
        return -1;
    }
    const int idx = (pt.y - m_rcItem.top + m_scrollY) / kRowH;
    if (idx < 0 || idx >= static_cast<int>(m_rows.size()))
    {
        return -1;
    }
    return idx;
}

void OrgTree::ActionButtons(int rowIndex,
                            std::vector<std::pair<Action, RECT>>& out) const
{
    out.clear();
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size()))
    {
        return;
    }
    const OrgNode* node = m_rows[rowIndex].m_node;
    const bool isDept = (node->m_kind == OrgNodeKind::Dept);
    // 公司根节点代表企业本身，可以往下加子部门和员工，但不允许删除。判定依据是
    // 服务端给出的公司根部门标记，不要改用节点 ID 是否等于某个约定字符串来判断
    // ——迁移之后根节点用的是真实部门 ID。
    const bool isRoot = node->m_isCompanyRoot;

    // 部门：新增子部门 + 新增员工 (+ 删除，根除外)；员工：删除。
    std::vector<Action> acts;
    if (isDept)
    {
        acts.push_back(Action::AddDept);
        acts.push_back(Action::AddUser);
        if (!isRoot)
        {
            acts.push_back(Action::Delete);
        }
    }
    else
    {
        acts.push_back(Action::Delete);
    }

    const RECT rr = RowRect(rowIndex);
    const int  cy = (rr.top + rr.bottom) / 2;
    const int  count = static_cast<int>(acts.size());
    int x = rr.right - kRowMargin - count * kActionBtnW;
    for (int i = 0; i < count; ++i)
    {
        RECT rc = { x, cy - kActionBtnW / 2, x + kActionBtnW, cy + kActionBtnW / 2 };
        out.push_back(std::make_pair(acts[i], rc));
        x += kActionBtnW;
    }
}

std::vector<OrgTree::Action> OrgTree::Test_RowActions(int rowIndex) const
{
    std::vector<std::pair<Action, RECT>> buttons;
    ActionButtons(rowIndex, buttons);

    std::vector<Action> actions;
    actions.reserve(buttons.size());
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        actions.push_back(buttons[i].first);
    }
    return actions;
}

void OrgTree::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;
    ClampScroll();

    // 悬浮滚动条：摆在树区右内缘的细竖条上，高度 = 可视区高度（与
    // ClampScroll / OnMouseWheel 用到的 viewH 一致），宽度取 kSuggestedWidth。
    m_treeSb->ForceLayout(RECT{
        m_rcItem.right - kSbInset - OverlayScrollbar::kSuggestedWidth,
        m_rcItem.top,
        m_rcItem.right - kSbInset,
        m_rcItem.bottom });
}

void OrgTree::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 树区白底。
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk0);

    // 过滤无结果:居中提示,不再走行绘制。
    if (m_rows.empty() && !m_filter.IsEmpty())
    {
        RECT rc = { m_rcItem.left, m_rcItem.top + 24, m_rcItem.right,
                    m_rcItem.top + 60 };
        ui::DrawTextLine(hdc, _T("无匹配结果"), rc, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    // 行绘制裁剪到树区矩形，避免最底部那一行画出界、压到下方页脚上。
    int savedClip = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, m_rcItem.left, m_rcItem.top,
                        m_rcItem.right, m_rcItem.bottom);

    const int total = static_cast<int>(m_rows.size());
    // 全部可见行的内容总高，供悬浮滚动条按比例算滑块。
    m_contentH = total * kRowH;
    // 只画落在可视区内的行。
    int first = (m_scrollY) / kRowH;
    if (first < 0)
    {
        first = 0;
    }
    int last = (m_scrollY + (m_rcItem.bottom - m_rcItem.top)) / kRowH + 1;
    if (last > total)
    {
        last = total;
    }

    for (int i = first; i < last; ++i)
    {
        const OrgNode* node  = m_rows[i].m_node;
        const int      depth = m_rows[i].m_depth;
        const bool     isDept = (node->m_kind == OrgNodeKind::Dept);
        const bool     isRoot = node->m_isCompanyRoot;   // 公司节点，画法与普通部门不同
        const bool     selected = (node->m_id == m_selectedId);
        const bool     hovered  = (i == m_hoverRow);

        const RECT rr = RowRect(i);
        const int  cy = (rr.top + rr.bottom) / 2;

        // 行底色（选中 > hover）。
        RECT bg = { rr.left + kRowMargin, rr.top + 1,
                    rr.right - kRowMargin, rr.bottom - 1 };
        if (selected)
        {
            ui::FillRoundRect(hdc, bg, theme::kFlamingo50, theme::kRadiusMd);
            RECT accent = { bg.left, bg.top + 6, bg.left + kAccentW, bg.bottom - 6 };
            ui::FillSolidRect(hdc, accent, theme::kFlamingo500);
        }
        else if (hovered)
        {
            ui::FillRoundRect(hdc, bg, theme::kInk50, theme::kRadiusMd);
        }

        int x = rr.left + kRowPadL + depth * kIndent;

        // 展开箭头（仅部门）。
        if (isDept)
        {
            const bool expanded = (m_expanded.find(node->m_id) != m_expanded.end());
            RECT rcChev = { x + (kChevronW - 12) / 2, cy - 6,
                            x + (kChevronW - 12) / 2 + 12, cy + 6 };
            ui::DrawIcon(hdc, expanded ? ui::Icon::ChevronDown : ui::Icon::ChevronRight,
                         rcChev, theme::kInk400);
        }
        x += kChevronW;

        // 节点图标：部门 = 文件夹；员工 = 头像。
        RECT rcIcon = { x, cy - kNodeIcon / 2, x + kNodeIcon, cy + kNodeIcon / 2 };
        if (isDept)
        {
            ui::DrawIcon(hdc, ui::Icon::Folder, rcIcon,
                         isRoot ? theme::kFlamingo500 : theme::kInk500);
        }
        else
        {
            ui::DrawAvatar(hdc, rcIcon, ui::AvatarColor(node->m_name),
                           ui::Initials(node->m_name));
        }
        x += kNodeIcon + kGap;

        // 右侧区域：hover 时是动作按钮，否则是 meta（部门子节点数 / 员工休假徽章）。
        int rightLimit = rr.right - kRowMargin;
        if (hovered)
        {
            std::vector<std::pair<Action, RECT>> btns;
            ActionButtons(i, btns);
            if (!btns.empty())
            {
                rightLimit = btns.front().second.left - 4;
            }
            for (const auto& b : btns)
            {
                ui::FillRoundRect(hdc, b.second, theme::kInk0, theme::kRadiusSm);
                ui::StrokeRoundRect(hdc, b.second, theme::kInk150,
                                    theme::kRadiusSm, 1.0f);
                ui::Icon ic = ui::Icon::Folder;
                COLORREF cl = theme::kInk500;
                if (b.first == Action::AddDept) { ic = ui::Icon::Folder; }
                else if (b.first == Action::AddUser) { ic = ui::Icon::User; }
                else { ic = ui::Icon::Trash; cl = theme::kColorDanger; }
                const int bcx = (b.second.left + b.second.right) / 2;
                const int bcy = (b.second.top + b.second.bottom) / 2;
                RECT ri = { bcx - kActionIcon / 2, bcy - kActionIcon / 2,
                            bcx + kActionIcon / 2, bcy + kActionIcon / 2 };
                ui::DrawIcon(hdc, ic, ri, cl);
            }
        }
        else if (isDept)
        {
            // 部门：直接子节点数。
            CString cnt;
            cnt.Format(_T("%d"), static_cast<int>(node->m_children.size()));
            RECT rcCnt = { rr.right - kRowMargin - 30, rr.top,
                           rr.right - kRowMargin, rr.bottom };
            ui::DrawTextLine(hdc, cnt, rcCnt, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            rightLimit = rcCnt.left - 4;
        }
        else if (node->m_userStatus == UserStatus::OnLeave)
        {
            // 员工休假徽章。
            const int chipW = ui::ChipWidth(hdc, _T("休假"), false);
            RECT rcChip = { rr.right - kRowMargin - chipW, cy - 9,
                            rr.right - kRowMargin, cy + 9 };
            ui::DrawChip(hdc, rcChip, _T("休假"), theme::kColorWarn,
                         theme::kInk50, false, 0);
            rightLimit = rcChip.left - 4;
        }

        // 节点名。
        RECT rcName = { x, rr.top, rightLimit, rr.bottom };
        ui::DrawTextLine(hdc, node->m_name, rcName,
                         selected ? theme::kFlamingo700 : theme::kInk700,
                         ui::UiFont(theme::kFontSizeNormal, isDept),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                         | DT_END_ELLIPSIS);
    }

    // 行绘制结束，还原裁剪区（base OnPaint 画的滚动条子控件不受此裁剪影响）。
    ::RestoreDC(hdc, savedClip);

    // 把最新内容总高 / 滚动量同步给悬浮滚动条，再由 base OnPaint 绘制它自身
    // （悬浮于树区右缘、auto-hide）。
    m_treeSb->SetMetrics(m_contentH, m_scrollY);
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

bool OrgTree::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    const int row = RowAtPoint(pt);
    if (row < 0)
    {
        return true;
    }
    const OrgNode* node  = m_rows[row].m_node;
    const int      depth = m_rows[row].m_depth;

    // 命中展开箭头 → 切换展开 / 折叠。
    if (node->m_kind == OrgNodeKind::Dept)
    {
        const int chevL = m_rcItem.left + kRowPadL + depth * kIndent;
        if (pt.x >= chevL && pt.x < chevL + kChevronW)
        {
            if (m_expanded.find(node->m_id) != m_expanded.end())
            {
                m_expanded.erase(node->m_id);
            }
            else
            {
                m_expanded.insert(node->m_id);
            }
            Flatten();
            ClampScroll();
            Invalidate();
            return true;
        }
    }

    // 命中 hover 动作按钮 → 触发动作回调。
    std::vector<std::pair<Action, RECT>> btns;
    ActionButtons(row, btns);
    for (const auto& b : btns)
    {
        if (::PtInRect(&b.second, pt))
        {
            if (m_actFn != nullptr)
            {
                m_actFn(m_cbUser, b.first, node->m_id);
            }
            return true;
        }
    }

    // 否则 → 选中该节点。
    if (node->m_id != m_selectedId)
    {
        m_selectedId = node->m_id;
        Invalidate();
        if (m_selFn != nullptr)
        {
            m_selFn(m_cbUser, node->m_id);
        }
    }
    return true;
}

bool OrgTree::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    const int row = RowAtPoint(pt);
    if (row != m_hoverRow)
    {
        m_hoverRow = row;
        Invalidate();
    }
    return true;
}

bool OrgTree::OnMouseLeave()
{
    if (m_hoverRow != -1)
    {
        m_hoverRow = -1;
        Invalidate();
    }
    return true;
}

bool OrgTree::OnMouseWheel(POINT /*pt*/, short zDelta, UINT /*mkFlags*/)
{
    m_scrollY -= (zDelta / WHEEL_DELTA) * kRowH * kWheelRows;
    ClampScroll();
    // 同步内容总高 / 滚动量给悬浮滚动条并令其淡入（停止滚动后自动淡出）。
    m_contentH = static_cast<int>(m_rows.size()) * kRowH;
    m_treeSb->SetMetrics(m_contentH, m_scrollY);
    m_treeSb->Flash();
    Invalidate();
    return true;
}

bool OrgTree::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
    return true;
}

void OrgTree::OnTreeSbScroll(void* ctx, int newScroll)
{
    // OverlayScrollbar 拖动滑块的回调：把新滚动量写回本控件并重绘。
    OrgTree* self = static_cast<OrgTree*>(ctx);
    self->m_scrollY = newScroll;
    self->Invalidate();
}

} // namespace flamingoadmin
