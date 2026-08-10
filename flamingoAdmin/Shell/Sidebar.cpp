/**
 *  flamingoAdmin —— 侧边栏实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Shell/Sidebar.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"

#include "Controls/Basic/DuiLabel.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 侧边栏布局常量（96 dpi 逻辑像素）----
const int kHeaderHeight   = 56;   // 品牌头部高度
const int kFooterHeight   = 56;   // 页脚高度
const int kNavItemHeight  = 36;   // 单个导航项高度
const int kNavItemCount   = 5;    // 导航项数量
const int kNavGap         = 2;    // 导航项间距
const int kNavPadV        = 10;   // 导航区上下内边距
// 导航区总高度 = 上下内边距 + N 项 + (N-1) 个间距。
const int kNavAreaHeight  = kNavPadV * 2 + kNavItemHeight * kNavItemCount
                            + kNavGap * (kNavItemCount - 1);

// 导航项内部度量。
const int kNavIconSize    = 16;   // 图标边长
const int kNavIconLeft    = 12;   // 图标距左边距
const int kNavLabelLeft   = 38;   // 文字距左边距
const int kNavAccentWidth = 3;    // 选中态左侧强调条宽度

} // anonymous namespace

// ===========================================================================
// SidebarNavItem —— 侧边栏单个导航项。
//
// 自绘：选中态有 flamingo-50 圆角底 + 左侧强调条，hover 态有浅灰圆角底。
// 被点击时经 NotifyParent 上报 DUIN_CLICK（ctrlId 即导航项的 kIdNav* 值）。
// ===========================================================================
class SidebarNavItem : public DuiControl
{
public:
    // 设置图标与文字。
    void Setup(ui::Icon icon, LPCTSTR label)
    {
        m_icon  = icon;
        m_label = label;
    }

    // 设置选中态。
    void SetActive(bool active)
    {
        m_active = active;
        Invalidate();
    }

    // 设置紧凑态：紧凑时只画居中图标、不画文字。
    void SetCollapsed(bool collapsed)
    {
        m_collapsed = collapsed;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        // 背景：选中 > hover > 无。
        if (m_active)
        {
            ui::FillRoundRect(hdc, m_rcItem, theme::kFlamingo50, theme::kRadiusMd);
            RECT bar =
            {
                m_rcItem.left, m_rcItem.top + 8,
                m_rcItem.left + kNavAccentWidth, m_rcItem.bottom - 8,
            };
            ui::FillSolidRect(hdc, bar, theme::kFlamingo500);
        }
        else if (m_bHover)
        {
            ui::FillRoundRect(hdc, m_rcItem, theme::kInk50, theme::kRadiusMd);
        }

        const COLORREF fg = m_active ? theme::kFlamingo700 : theme::kInk500;

        // 图标：标准态居左，紧凑态水平居中。
        const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;
        const int iconLeft = m_collapsed
            ? (m_rcItem.left + m_rcItem.right) / 2 - kNavIconSize / 2
            : m_rcItem.left + kNavIconLeft;
        RECT rcIcon =
        {
            iconLeft, cy - kNavIconSize / 2,
            iconLeft + kNavIconSize, cy + kNavIconSize / 2,
        };
        ui::DrawIcon(hdc, m_icon, rcIcon, fg);

        // 文字（紧凑态不画）。
        if (!m_collapsed)
        {
            RECT rcText =
            {
                m_rcItem.left + kNavLabelLeft, m_rcItem.top,
                m_rcItem.right - 10, m_rcItem.bottom,
            };
            ui::DrawTextLine(hdc, m_label, rcText, fg,
                             ui::UiFont(theme::kFontSizeNormal, m_active),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    bool OnMouseEnter() override { m_bHover = true;  Invalidate(); return true; }
    bool OnMouseLeave() override { m_bHover = false; Invalidate(); return true; }

    bool OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/) override
    {
        NotifyParent(DUIN_CLICK);
        return true;
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
        return true;
    }

private:
    ui::Icon m_icon      = ui::Icon::Dashboard;
    CString  m_label;
    bool     m_active    = false;
    bool     m_collapsed = false;
};

// ===========================================================================
// Sidebar
// ===========================================================================
Sidebar::Sidebar()
{
    SetFillColor(theme::kInk0);                      // 白底
    SetEdgeBorder(kEdgeRight, theme::kInk100);       // 右侧分隔线

    AddChild(BuildHeader(), Hint().Fixed(kHeaderHeight));

    // 导航区：带内边距与间距的 VBox，容纳 5 个导航项。
    auto nav = std::make_unique<DuiVBox>();
    nav->SetPadding(8, kNavPadV, 8, kNavPadV);
    nav->SetGap(kNavGap);
    nav->AddChild(MakeNavItem(kIdNavDashboard,     ui::Icon::Dashboard, _T("仪表板")),
                  Hint().Fixed(kNavItemHeight));
    nav->AddChild(MakeNavItem(kIdNavEnterprises,   ui::Icon::Building,  _T("企业管理")),
                  Hint().Fixed(kNavItemHeight));
    nav->AddChild(MakeNavItem(kIdNavAnnouncements, ui::Icon::Megaphone, _T("公告中心")),
                  Hint().Fixed(kNavItemHeight));
    nav->AddChild(MakeNavItem(kIdNavAudit,         ui::Icon::Shield,    _T("审计日志")),
                  Hint().Fixed(kNavItemHeight));
    nav->AddChild(MakeNavItem(kIdNavSettings,      ui::Icon::Settings,  _T("平台设置")),
                  Hint().Fixed(kNavItemHeight));
    AddChild(std::move(nav), Hint().Fixed(kNavAreaHeight));

    // 弹性空白，把页脚顶到底部。
    AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));

    AddChild(BuildFooter(), Hint().Fixed(kFooterHeight));
}

std::unique_ptr<DuiControl> Sidebar::BuildHeader()
{
    // 头部：白底 + 底部分隔线，徽标 + 名称两行。
    auto header = std::make_unique<ui::SolidHBox>();
    header->SetFillColor(theme::kInk0);
    header->SetEdgeBorder(ui::SolidHBox::kEdgeBottom, theme::kInk100);
    header->SetPadding(16, 0, 16, 0);
    header->SetGap(10);

    auto logo = std::make_unique<ui::LogoBadge>();
    logo->SetGlyphPointSize(theme::kFontSizeTitle);
    header->AddChild(std::move(logo),
                     Hint().Fixed(32, 32).AlignC(DuiLayout::AlignCenter));

    auto titles = std::make_unique<DuiVBox>();
    titles->AddChild(ui::MakeLabel(_T("FlamingoIM"), theme::kFontSizeNormal, true,
                                   theme::kInk700), Hint().Fixed(18));
    titles->AddChild(ui::MakeLabel(_T("Enterprise Admin"), theme::kFontSizeSmall, false,
                                   theme::kInk400), Hint().Fixed(16));
    m_headerTitles = titles.get();
    header->AddChild(std::move(titles),
                     Hint().Weight(1).Fixed(-1, 34).AlignC(DuiLayout::AlignCenter));

    return header;
}

std::unique_ptr<DuiControl> Sidebar::BuildFooter()
{
    // 页脚：白底 + 顶部分隔线，用户名 / 角色 + 登出按钮。
    auto footer = std::make_unique<ui::SolidHBox>();
    footer->SetFillColor(theme::kInk0);
    footer->SetEdgeBorder(ui::SolidHBox::kEdgeTop, theme::kInk100);
    footer->SetPadding(14, 0, 10, 0);
    footer->SetGap(8);

    auto info = std::make_unique<DuiVBox>();
    info->AddChild(ui::MakeLabel(_T("李正阳"), theme::kFontSizeNormal, true,
                                 theme::kInk700), Hint().Fixed(18));
    info->AddChild(ui::MakeLabel(_T("admin · 超管"), theme::kFontSizeSmall, false,
                                 theme::kInk400), Hint().Fixed(16));
    m_footerInfo = info.get();
    footer->AddChild(std::move(info),
                     Hint().Weight(1).Fixed(-1, 34).AlignC(DuiLayout::AlignCenter));

    auto logout = std::make_unique<ui::IconButton>();
    logout->SetCtrlId(kIdLogout);
    logout->SetIcon(ui::Icon::Logout);
    logout->SetIconSize(16);
    logout->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    logout->SetTooltip(_T("退出登录"));
    footer->AddChild(std::move(logout),
                     Hint().Fixed(30, 30).AlignC(DuiLayout::AlignCenter));

    return footer;
}

std::unique_ptr<DuiControl> Sidebar::MakeNavItem(int navId, ui::Icon icon,
                                                 LPCTSTR label)
{
    auto item = std::make_unique<SidebarNavItem>();
    item->SetCtrlId(navId);
    item->Setup(icon, label);
    if (m_navCount < kNavItemCount)
    {
        m_navItems[m_navCount++] = item.get();
    }
    return item;
}

void Sidebar::SetActiveNav(int navId)
{
    for (int i = 0; i < m_navCount; ++i)
    {
        if (m_navItems[i] != nullptr)
        {
            m_navItems[i]->SetActive(
                static_cast<int>(m_navItems[i]->GetCtrlId()) == navId);
        }
    }
}

void Sidebar::SetCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
    {
        return;
    }
    m_collapsed = collapsed;

    // 导航项切到只画图标的紧凑态。
    for (int i = 0; i < m_navCount; ++i)
    {
        if (m_navItems[i] != nullptr)
        {
            m_navItems[i]->SetCollapsed(collapsed);
        }
    }
    // 头部副标题、页脚用户信息在紧凑态隐藏。balloonui 的 HBox 布局会跳过
    // 不可见子控件，余下的徽标 / 登出按钮自动重排。
    if (m_headerTitles != nullptr)
    {
        m_headerTitles->SetVisible(!collapsed);
    }
    if (m_footerInfo != nullptr)
    {
        m_footerInfo->SetVisible(!collapsed);
    }
    Invalidate();
}

} // namespace flamingoadmin
