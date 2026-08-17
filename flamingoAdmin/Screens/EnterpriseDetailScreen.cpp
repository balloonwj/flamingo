/**
 *  flamingoAdmin —— 企业详情屏 EnterpriseDetailScreen 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/EnterpriseDetailScreen.h"
#include "Screens/OrgDetailPanel.h"
#include "Screens/ServicesPanel.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"
#include "Mock/DataStore.h"
#include "Util/OrgTreeBuilder.h"     // kSyntheticRootId（尚未迁移的存量数据兜底）
#include "Util/OrgToolbarLayout.h"   // 工具条搜索框 / 图标按钮的摆位（纯函数，有单测）

#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton
#include "Controls/Input/DuiEdit.h" // 组织树搜索框
#include "DuiNotify.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 企业详情屏布局常量（96 dpi 逻辑像素）----
const int kHeaderH      = 152;  // 头部高度
const int kTreeColW     = 340;  // 组织树左栏宽度
const int kToolbarH     = 44;   // 树工具条高度
const int kTreeFooterH  = 40;   // 树页脚高度
const int kSubtabH      = 40;   // 子 tab 条高度
const int kSidePad      = 24;   // 头部左 / 右内距
const int kBreadcrumbY  = 16;   // 面包屑距头部顶部
const int kAvatarY      = 42;   // 企业头像距头部顶部
const int kAvatarSz     = 44;   // 企业头像边长
const int kBtnH         = 34;   // 头部操作按钮高度
const int kToolBtn      = 28;   // 工具条图标按钮边长
const int kToolBtnGap    = 4;   // 工具条相邻图标按钮之间的间距
const int kToolSidePad   = 12;  // 工具条左 / 右内边距
const int kToolSearchGap = 8;   // 搜索框右边界与最左侧图标按钮之间的间距
const int kOrgSearchH    = 28;  // 组织树搜索框高度
const int kToolBtnIcon   = 14;  // 工具条图标按钮内图标边长
const int kToolBtnCount  = 3;   // 工具条图标按钮个数（刷新 / 展开全部 / 折叠）

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

// 组织树搜索框右侧放大镜图标的画法。写成自由函数(而非 lambda)以遵开发约定文档
// 「尽量不用 lambda」;交给 DuiEdit::SetIcon(RightIcon,...) 在右侧 gutter 内
// 居中绘制。rcIcon 为图标可用矩形(host 客户区坐标)。
void DrawOrgSearchIcon(HDC hdc, const RECT& rcIcon)
{
    const int kGlyph = 16;   // 放大镜图标边长(像素)
    const int cx = (rcIcon.left + rcIcon.right) / 2;
    const int cy = (rcIcon.top + rcIcon.bottom) / 2;
    RECT rc = { cx - kGlyph / 2, cy - kGlyph / 2,
                cx + kGlyph / 2, cy + kGlyph / 2 };
    ui::DrawIcon(hdc, ui::Icon::Search, rc, theme::kInk400);
}

} // anonymous namespace

EnterpriseDetailScreen::EnterpriseDetailScreen()
{
    // 本屏的 ctrlId：组织动作（增删）经此 ID 上报 AdminFrame。
    SetCtrlId(kIdDetailScreen);

    // ---- 返回 / 设置 / 公告 / 主操作 按钮 ----
    auto back = std::make_unique<DuiButton>();
    back->SetCtrlId(kIdDetailBack);
    back->SetText(_T(""));
    back->SetVariant(DuiButton::Variant::Default);
    // 14px 深字色 "<" 图标:Default 文字色是 kInk600, 与 FlatButton 内部行为对齐。
    static HBITMAP s_iconChevronLeftInk600 = ui::RenderIconToBitmap(
        ui::Icon::ChevronLeft, 14, theme::kInk600);
    back->SetLeadingIcon(s_iconChevronLeftInk600);
    back->SetLeadingIconSize(14);
    m_btnBack = back.get();
    AddChild(std::move(back));

    auto settings = std::make_unique<DuiButton>();
    settings->SetCtrlId(kIdDetailSettings);
    settings->SetText(_T("企业设置"));
    settings->SetVariant(DuiButton::Variant::Default);
    settings->SetTextPointSize(theme::kFontSizeNormal);
    m_btnSettings = settings.get();
    AddChild(std::move(settings));

    auto announce = std::make_unique<DuiButton>();
    announce->SetCtrlId(kIdDetailAnnounce);
    announce->SetText(_T("发布公告"));
    announce->SetVariant(DuiButton::Variant::Default);
    announce->SetTextPointSize(theme::kFontSizeNormal);
    m_btnAnnounce = announce.get();
    AddChild(std::move(announce));

    auto createGroup = std::make_unique<DuiButton>();
    createGroup->SetCtrlId(kIdDetailCreateGroup);
    createGroup->SetText(_T("新建群"));
    createGroup->SetVariant(DuiButton::Variant::Default);
    createGroup->SetTextPointSize(theme::kFontSizeNormal);
    m_btnCreateGroup = createGroup.get();
    AddChild(std::move(createGroup));

    auto primary = std::make_unique<DuiButton>();
    primary->SetCtrlId(kIdDetailPrimary);
    primary->SetText(_T("新增员工"));
    primary->SetVariant(DuiButton::Variant::Primary);
    // 14px 白色 "+" 图标:Primary 文字色是 kInk0(白)。
    static HBITMAP s_iconPlusWhite = ui::RenderIconToBitmap(
        ui::Icon::Plus, 14, theme::kInk0);
    primary->SetLeadingIcon(s_iconPlusWhite);
    primary->SetLeadingIconSize(14);
    primary->SetTextPointSize(theme::kFontSizeNormal);
    m_btnPrimary = primary.get();
    AddChild(std::move(primary));

    // ---- 组织树搜索框(工具条左侧) ----
    // 输入框是无窗口控件,构造完即可使用,无需额外的创建步骤。
    // 文字变化经 DUIN_VALUECHANGED 上报 AdminFrame,再转发到 OnOrgSearchChanged。
    auto orgSearch = std::make_unique<DuiEdit>();
    orgSearch->SetCtrlId(kIdOrgSearch);
    orgSearch->SetPlaceholder(_T("搜索部门 / 员工…"));
    orgSearch->SetBgColor(theme::kInk25);
    orgSearch->SetShowBorder(true);
    // 右侧内部放大镜图标(装饰,gutter 30px;文字区自动避让该 gutter)。
    orgSearch->SetIcon(DuiEdit::RightIcon, 30, &DrawOrgSearchIcon);
    m_orgSearch = orgSearch.get();
    AddChild(std::move(orgSearch));

    // ---- 组织树工具条右侧的三个图标按钮 ----
    // 复用 admin 自己的 ui::IconButton（hover 圆角底色 + tooltip + DUIN_CLICK），
    // 三个按钮外观统一。点击通知直接发到 AdminFrame（NotifyParent 只通知宿主
    // 窗口，不沿控件父链冒泡），由 AdminFrame 转调回本屏或发起网络请求。
    auto btnRefresh = std::make_unique<ui::IconButton>();
    btnRefresh->SetCtrlId(kIdOrgRefresh);
    btnRefresh->SetIcon(ui::Icon::Refresh);
    btnRefresh->SetIconSize(kToolBtnIcon);
    btnRefresh->SetColors(theme::kInk500, theme::kInk700, theme::kInk50);
    btnRefresh->SetTooltip(_T("刷新组织架构"));
    m_btnOrgRefresh = btnRefresh.get();
    AddChild(std::move(btnRefresh));

    auto btnExpandAll = std::make_unique<ui::IconButton>();
    btnExpandAll->SetCtrlId(kIdOrgExpandAll);
    btnExpandAll->SetIcon(ui::Icon::ChevronDown);
    btnExpandAll->SetIconSize(kToolBtnIcon);
    btnExpandAll->SetColors(theme::kInk500, theme::kInk700, theme::kInk50);
    btnExpandAll->SetTooltip(_T("展开全部部门"));
    m_btnOrgExpandAll = btnExpandAll.get();
    AddChild(std::move(btnExpandAll));

    auto btnCollapse = std::make_unique<ui::IconButton>();
    btnCollapse->SetCtrlId(kIdOrgCollapse);
    btnCollapse->SetIcon(ui::Icon::ChevronRight);
    btnCollapse->SetIconSize(kToolBtnIcon);
    btnCollapse->SetColors(theme::kInk500, theme::kInk700, theme::kInk50);
    btnCollapse->SetTooltip(_T("折叠全部部门"));
    m_btnOrgCollapse = btnCollapse.get();
    AddChild(std::move(btnCollapse));

    // ---- 组织树 + 详情面板 ----
    auto tree = std::make_unique<OrgTree>();
    tree->SetCallbacks(&EnterpriseDetailScreen::SelectThunk,
                       &EnterpriseDetailScreen::ActionThunk, this);
    m_tree = tree.get();
    AddChild(std::move(tree));

    auto panel = std::make_unique<OrgDetailPanel>();
    panel->SetCallbacks(&EnterpriseDetailScreen::SelectThunk,
                        &EnterpriseDetailScreen::ActionThunk, this);
    m_panel = panel.get();
    AddChild(std::move(panel));

    // ---- 服务号面板（服务号子 tab 的内容）----
    auto svcPanel = std::make_unique<ServicesPanel>();
    m_servicesPanel = svcPanel.get();
    AddChild(std::move(svcPanel));
}

void EnterpriseDetailScreen::SetEnterprise(const CString& enterpriseId)
{
    m_entId = enterpriseId;
    m_ent   = DataStore::Instance().FindEnterprise(enterpriseId);
    m_tree->SetRoot(DataStore::Instance().OrgTree(enterpriseId));
    // 切换企业清除上一个企业残留的搜索过滤(SetRoot 不清 m_filter)。
    m_tree->SetFilter(_T(""));
    if (m_orgSearch != nullptr)
    {
        m_orgSearch->SetText(_T(""));
    }
    m_panel->SetNode(nullptr, m_ent);
    m_servicesPanel->SetEnterprise(enterpriseId);
    SetSubTab(kTabOrg);
    Invalidate();
}

void EnterpriseDetailScreen::Refresh()
{
    m_ent = DataStore::Instance().FindEnterprise(m_entId);
    m_tree->Rebuild();
    // 重新解析当前选中节点（删除后可能已不存在）。
    const CString sel = m_tree->GetSelectedId();
    const OrgNode* node = sel.IsEmpty()
        ? nullptr : DataStore::Instance().FindOrgNode(m_entId, sel);
    if (node == nullptr)
    {
        m_tree->SetSelectedId(CString());
    }
    m_panel->SetNode(node, m_ent);
    m_servicesPanel->Refresh();
    Invalidate();
}

CString EnterpriseDetailScreen::DefaultParentDept() const
{
    const CString sel = m_tree->GetSelectedId();
    if (!sel.IsEmpty())
    {
        const OrgNode* node = DataStore::Instance().FindOrgNode(m_entId, sel);
        if (node != nullptr && node->m_kind == OrgNodeKind::Dept)
        {
            return sel;
        }
    }

    // 没有选中部门（一个都没选，或选中的是某个员工）时落到组织树的根节点，
    // 也就是代表公司本身的那个部门，语义即"直接挂在公司下"。
    return DefaultTopParentDept();
}

CString EnterpriseDetailScreen::DefaultTopParentDept() const
{
    // 根节点在企业完成迁移后是一个真实部门，其 ID 可以直接发给服务端。
    const OrgNode* root = DataStore::Instance().OrgTree(m_entId);
    if (root != nullptr)
    {
        return root->m_id;
    }

    // 连组织树都还没加载时退回历史上的合成根 ID，由提交处沿用旧的兼容处理。
    return CString(orgtree::kSyntheticRootId);
}

void EnterpriseDetailScreen::SelectNode(const CString& nodeId)
{
    m_tree->SetSelectedId(nodeId);
    OnNodeSelected(nodeId);
}

void EnterpriseDetailScreen::OnOrgSearchChanged()
{
    if (m_tree == nullptr || m_orgSearch == nullptr)
    {
        return;
    }
    // 按搜索框当前文本过滤组织树(部门名 / 员工姓名;空文本恢复全树)。
    // 这里不调本屏 Invalidate():SetFilter 内部已对组织树自身 Invalidate,树会
    // 正常刷新,整屏重绘是多余的开销。早先搜索框内嵌真 Win32 输入框子窗口时,
    // 整屏重绘还会把背景填到搜索框所在区域、盖掉刚输入的文字(须再点一下才恢复);
    // 搜索框改为无窗口控件、与本屏一同绘制之后,该现象已不复存在。
    m_tree->SetFilter(m_orgSearch->GetText());
}

void EnterpriseDetailScreen::ExpandAllOrg()
{
    if (m_tree != nullptr)
    {
        m_tree->ExpandAll();
    }
}

void EnterpriseDetailScreen::CollapseAllOrg()
{
    if (m_tree != nullptr)
    {
        m_tree->CollapseAll();
    }
}

void EnterpriseDetailScreen::SelectThunk(void* user, const CString& nodeId)
{
    static_cast<EnterpriseDetailScreen*>(user)->OnNodeSelected(nodeId);
}

void EnterpriseDetailScreen::ActionThunk(void* user, OrgTree::Action action,
                                         const CString& nodeId)
{
    static_cast<EnterpriseDetailScreen*>(user)->OnNodeAction(action, nodeId);
}

void EnterpriseDetailScreen::OnNodeSelected(const CString& nodeId)
{
    m_tree->SetSelectedId(nodeId);
    const OrgNode* node = DataStore::Instance().FindOrgNode(m_entId, nodeId);
    m_panel->SetNode(node, m_ent);
    // 选中员工时上报，由 AdminFrame 拉取该员工的权限矩阵填充权限 tab。
    if (node != nullptr && node->m_kind == OrgNodeKind::User)
    {
        RaisePending(Pending::SelectUser, nodeId);
    }
}

void EnterpriseDetailScreen::OnNodeAction(OrgTree::Action action,
                                          const CString& nodeId)
{
    switch (action)
    {
    case OrgTree::Action::AddDept:
        RaisePending(Pending::AddDept, nodeId);
        break;
    case OrgTree::Action::AddUser:
        RaisePending(Pending::AddUser, nodeId);
        break;
    case OrgTree::Action::Delete:
        RaisePending(Pending::Delete, nodeId);
        break;
    case OrgTree::Action::SetRole:
        RaisePending(Pending::SetRole, nodeId);
        break;
    case OrgTree::Action::SetPerms:
        RaisePending(Pending::SetPerms, nodeId);
        break;
    case OrgTree::Action::SetUserStatus:
        RaisePending(Pending::SetUserStatus, nodeId);
        break;
    case OrgTree::Action::ResetPassword:
        RaisePending(Pending::ResetPassword, nodeId);
        break;
    case OrgTree::Action::KickDevices:
        RaisePending(Pending::KickDevices, nodeId);
        break;
    case OrgTree::Action::EditUser:
        RaisePending(Pending::EditUser, nodeId);
        break;
    case OrgTree::Action::MoveUser:
        RaisePending(Pending::MoveUser, nodeId);
        break;
    default:
        break;
    }
}

void EnterpriseDetailScreen::RaisePending(Pending pending, const CString& nodeId)
{
    m_pending       = pending;
    m_pendingNodeId = nodeId;
    NotifyParent(DUIN_CLICK);   // ctrlId = kIdDetailScreen，AdminFrame 据此查询
}

void EnterpriseDetailScreen::SetSubTab(int tab)
{
    m_subTab = tab;
    const bool org = (tab == kTabOrg);
    m_tree->SetVisible(org);
    m_panel->SetVisible(org);
    m_servicesPanel->SetVisible(!org);
    m_btnPrimary->SetText(org ? _T("新增员工") : _T("新建服务号"));
    // 「新建群」只在组织架构 tab 有意义；服务号 tab 隐藏。
    m_btnCreateGroup->SetVisible(org);
    // 工具条三个图标按钮同理：服务号 tab 下整个树列都不显示，按钮须一并隐藏，
    // 否则会浮在服务号面板上方。
    m_btnOrgRefresh->SetVisible(org);
    m_btnOrgExpandAll->SetVisible(org);
    m_btnOrgCollapse->SetVisible(org);
    Invalidate();
}

void EnterpriseDetailScreen::SelectSubTab(int tab)
{
    // 仅接受合法序号；非法值忽略，保持当前 tab 不变。
    if (tab != kTabOrg && tab != kTabSvc)
    {
        return;
    }
    SetSubTab(tab);
}

void EnterpriseDetailScreen::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;
    const int L = rcAvail.left;
    const int R = rcAvail.right;
    const int T = rcAvail.top;

    // ---- 头部操作按钮 ----
    const int entRowCy = T + kAvatarY + kAvatarSz / 2;
    m_btnBack->ForceLayout(RECT{ L + kSidePad, entRowCy - kBtnH / 2,
                                 L + kSidePad + 36, entRowCy + kBtnH / 2 });

    int bx = R - kSidePad;
    auto place = [&](DuiButton* btn, int w)
    {
        RECT rc = { bx - w, entRowCy - kBtnH / 2, bx, entRowCy + kBtnH / 2 };
        btn->ForceLayout(rc);   // FlatButton 继承 DuiControl::ForceLayout
        bx -= w + 8;
    };
    place(m_btnPrimary, 116);
    place(m_btnCreateGroup, 96);
    place(m_btnAnnounce, 96);
    place(m_btnSettings, 96);

    // ---- 子 tab 矩形 ----
    const int tabY = T + kHeaderH - kSubtabH;
    HFONT tf = ui::UiFont(theme::kFontSizeNormal, true);
    const int orgW = MeasureW(_T("组织架构"), tf) + 56;
    const int svcW = MeasureW(_T("服务号"),  tf) + 56;
    m_rcTabOrg = RECT{ L + kSidePad,        tabY, L + kSidePad + orgW,        tabY + kSubtabH };
    m_rcTabSvc = RECT{ m_rcTabOrg.right,    tabY, m_rcTabOrg.right + svcW,    tabY + kSubtabH };

    // ---- 主体区 ----
    const int bodyT = T + kHeaderH;
    const int treeColR = L + kTreeColW;

    // 树工具条：搜索框占左侧，三个图标按钮（刷新 / 展开全部 / 折叠）整组右对齐。
    // 具体摆位交给 Util/OrgToolbarLayout 的纯函数，那部分有单测。
    OrgToolbarSpec toolSpec;
    toolSpec.m_buttonCount = kToolBtnCount;
    toolSpec.m_buttonSize  = kToolBtn;
    toolSpec.m_buttonGap   = kToolBtnGap;
    toolSpec.m_sidePad     = kToolSidePad;
    toolSpec.m_searchGap   = kToolSearchGap;
    toolSpec.m_searchH     = kOrgSearchH;
    const OrgToolbarMetrics toolMetrics = LayoutOrgToolbar(
        RECT{ L, bodyT, treeColR, bodyT + kToolbarH }, toolSpec);

    m_btnOrgRefresh  ->ForceLayout(toolMetrics.m_rcButtons[0]);
    m_btnOrgExpandAll->ForceLayout(toolMetrics.m_rcButtons[1]);
    m_btnOrgCollapse ->ForceLayout(toolMetrics.m_rcButtons[2]);
    m_orgSearch      ->ForceLayout(toolMetrics.m_rcSearch);

    // 组织树本体。
    m_tree->ForceLayout(RECT{ L, bodyT + kToolbarH,
                              treeColR - 1, rcAvail.bottom - kTreeFooterH });

    // 树页脚「顶级部门」按钮。
    const int footCy = rcAvail.bottom - kTreeFooterH / 2;
    HFONT sf = ui::UiFont(theme::kFontSizeNormal, false);
    const int addW = MeasureW(_T("+ 顶级部门"), sf) + 20;
    m_rcAddTopDept = RECT{ treeColR - 12 - addW, footCy - 13,
                           treeColR - 12,        footCy + 13 };

    // 组织详情面板。
    m_panel->ForceLayout(RECT{ treeColR, bodyT, R, rcAvail.bottom });

    // 服务号面板铺满整个主体区。
    m_servicesPanel->ForceLayout(RECT{ L, bodyT, R, rcAvail.bottom });
}

void EnterpriseDetailScreen::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    const int L = m_rcItem.left;
    const int R = m_rcItem.right;
    const int T = m_rcItem.top;

    // ---- 头部白底 ----
    RECT header = { L, T, R, T + kHeaderH };
    ui::FillSolidRect(hdc, header, theme::kInk0);

    // 面包屑：企业管理 › 企业名。
    HFONT crumbFont = ui::UiFont(theme::kFontSizeSmall, false);
    RECT rcCrumb1 = { L + kSidePad, T + kBreadcrumbY, L + kSidePad + 64,
                      T + kBreadcrumbY + 18 };
    ui::DrawTextLine(hdc, _T("企业管理"), rcCrumb1, theme::kFlamingo600,
                     ui::UiFont(theme::kFontSizeSmall, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const int crumbW = MeasureW(_T("企业管理"), ui::UiFont(theme::kFontSizeSmall, true));
    RECT rcChev = { L + kSidePad + crumbW + 4, T + kBreadcrumbY + 3,
                    L + kSidePad + crumbW + 16, T + kBreadcrumbY + 15 };
    ui::DrawIcon(hdc, ui::Icon::ChevronRight, rcChev, theme::kInk300);
    if (m_ent != nullptr)
    {
        RECT rcCrumb2 = { rcChev.right + 4, T + kBreadcrumbY, R - kSidePad,
                          T + kBreadcrumbY + 18 };
        ui::DrawTextLine(hdc, m_ent->m_name, rcCrumb2, theme::kInk500,
                         crumbFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 企业头像 + 名称 + 状态 / 套餐徽章 + 元信息。
    if (m_ent != nullptr)
    {
        const int avX = L + kSidePad + 44;   // 头像在返回按钮右侧
        RECT av = { avX, T + kAvatarY, avX + kAvatarSz, T + kAvatarY + kAvatarSz };
        ui::DrawAvatar(hdc, av, m_ent->m_logoColor, ui::Initials(m_ent->m_shortName));

        const int txtX = av.right + 14;
        HFONT nameFont = ui::UiFont(theme::kFontSizeHeading, true);
        ui::DrawTextLine(hdc, m_ent->m_name,
                         RECT{ txtX, T + kAvatarY + 1, R - 360, T + kAvatarY + 25 },
                         theme::kInk700, nameFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        const int nameW = MeasureW(m_ent->m_name, nameFont);

        // 状态徽章。
        int chipX = txtX + nameW + 12;
        chipX += ui::DrawStatusChip(hdc, chipX, T + kAvatarY + 12, m_ent->m_status) + 8;
        // 套餐徽章。
        const int planW = ui::ChipWidth(hdc, m_ent->m_plan, false);
        RECT planChip = { chipX, T + kAvatarY + 2, chipX + planW, T + kAvatarY + 22 };
        ui::DrawChip(hdc, planChip, m_ent->m_plan, theme::kFlamingo700,
                     theme::kFlamingo50, false, 0);

        // 元信息行。
        CString meta;
        meta.Format(_T("%s    %s · %s    开通 %s"),
                    static_cast<LPCTSTR>(m_ent->m_id),
                    static_cast<LPCTSTR>(m_ent->m_industry),
                    static_cast<LPCTSTR>(m_ent->m_region),
                    static_cast<LPCTSTR>(m_ent->m_createdAt));
        ui::DrawTextLine(hdc, meta,
                         RECT{ txtX, T + kAvatarY + 26, R - 360, T + kAvatarY + 44 },
                         theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    // ---- 子 tab 条 ----
    int orgUsers = 0, orgDepts = 0;
    DataStore::Instance().CountOrg(m_entId, orgUsers, orgDepts);
    const int svcCount = static_cast<int>(DataStore::Instance().Services(m_entId).size());

    auto drawTab = [&](const RECT& rc, LPCTSTR label, int count, bool active)
    {
        ui::DrawTextLine(hdc, label,
                         RECT{ rc.left, rc.top, rc.right - 24, rc.bottom },
                         active ? theme::kFlamingo600 : theme::kInk500,
                         ui::UiFont(theme::kFontSizeNormal, active),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // 数量徽章。
        CString cnt;
        cnt.Format(_T("%d"), count);
        RECT badge = { rc.right - 30, (rc.top + rc.bottom) / 2 - 9,
                       rc.right - 6,  (rc.top + rc.bottom) / 2 + 9 };
        ui::FillRoundRect(hdc, badge,
                          active ? theme::kFlamingo100 : theme::kInk100, 9);
        ui::DrawTextLine(hdc, cnt, badge,
                         active ? theme::kFlamingo700 : theme::kInk500,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (active)
        {
            RECT ul = { rc.left + 6, rc.bottom - 2, rc.right - 6, rc.bottom };
            ui::FillSolidRect(hdc, ul, theme::kFlamingo500);
        }
    };
    drawTab(m_rcTabOrg, _T("组织架构"), orgUsers + orgDepts, m_subTab == kTabOrg);
    drawTab(m_rcTabSvc, _T("服务号"),   svcCount,            m_subTab == kTabSvc);

    // 头部底部分隔线。
    ui::FillLine(hdc, L, T + kHeaderH - 1, R - L, 1, theme::kInk100);

    const int bodyT = T + kHeaderH;
    const int treeColR = L + kTreeColW;

    if (m_subTab == kTabOrg)
    {
        // 树列白底。
        ui::FillSolidRect(hdc, RECT{ L, bodyT, treeColR, m_rcItem.bottom },
                          theme::kInk0);
        // 树列右分隔线。
        ui::FillLine(hdc, treeColR - 1, bodyT, 1, m_rcItem.bottom - bodyT,
                     theme::kInk100);

        // 工具条:左侧是组织树搜索框(m_orgSearch,自绘 EDIT 自行绘制),右侧是
        // 刷新 / 展开 / 折叠三个 ui::IconButton 子控件(各自绘制,见本函数末尾
        // 基类 OnPaint)。原"组织架构"标题与上方子 tab 重复,已移除,腾给搜索框。
        // 这里只画工具条底分隔线。
        ui::FillLine(hdc, L, bodyT + kToolbarH - 1, kTreeColW - 1, 1, theme::kInk100);

        // 树页脚。
        const int footTop = m_rcItem.bottom - kTreeFooterH;
        ui::FillSolidRect(hdc, RECT{ L, footTop, treeColR - 1, m_rcItem.bottom },
                          theme::kInk25);
        ui::FillLine(hdc, L, footTop, kTreeColW - 1, 1, theme::kInk100);
        CString footText;
        footText.Format(_T("共 %d 员工 · %d 部门"), orgUsers, orgDepts);
        ui::DrawTextLine(hdc, footText,
                         RECT{ L + 12, footTop, treeColR, m_rcItem.bottom },
                         theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::FillRoundRect(hdc, m_rcAddTopDept, theme::kInk0, theme::kRadiusSm);
        ui::StrokeRoundRect(hdc, m_rcAddTopDept, theme::kInk150, theme::kRadiusSm, 1.0f);
        ui::DrawTextLine(hdc, _T("+ 顶级部门"), m_rcAddTopDept, theme::kInk600,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    // 服务号 tab 的主体由 m_servicesPanel 子控件自行绘制（下方 base
    // OnPaint 会绘制可见子控件），此处不需额外绘制。

    // 绘制子控件（按钮、组织树、详情面板）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

bool EnterpriseDetailScreen::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 子 tab 切换。
    if (::PtInRect(&m_rcTabOrg, pt)) { SetSubTab(kTabOrg); return true; }
    if (::PtInRect(&m_rcTabSvc, pt)) { SetSubTab(kTabSvc); return true; }

    if (m_subTab == kTabOrg)
    {
        // 展开 / 折叠 / 刷新已改为 ui::IconButton 子控件，点击由控件自身处理，
        // 这里不再做热区命中测试。
        if (::PtInRect(&m_rcAddTopDept, pt))
        {
            //「新增顶级部门」即在公司节点下加一个子部门，父节点取组织树的根。
            RaisePending(Pending::AddDept, DefaultTopParentDept());
            return true;
        }
    }
    return true;
}

bool EnterpriseDetailScreen::OnSetCursor(POINT pt)
{
    // 工具条三个图标按钮是子控件，光标由 IconButton::OnSetCursor 自行处理，
    // 这里只管本屏自绘的热区（子 tab、页脚「顶级部门」）。
    const bool onHit =
        ::PtInRect(&m_rcTabOrg, pt) || ::PtInRect(&m_rcTabSvc, pt) ||
        (m_subTab == kTabOrg && ::PtInRect(&m_rcAddTopDept, pt));
    if (onHit)
    {
        ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
        return true;
    }
    return false;
}

} // namespace flamingoadmin
