/**
 *  flamingoAdmin —— 侧边栏实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Shell/Sidebar.h"
#include "UiIds.h"
#include "Util/AdminApi.h"   // AdminSession：按管辖范围决定加不加「客户端版本」
#include "Theme/AdminTheme.h"

#include "Controls/Basic/DuiLabel.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 侧边栏布局常量（96 dpi 逻辑像素）----
const int kHeaderHeight   = 56;   // 品牌头部高度
// 原 kFooterHeight（页脚高度）随页脚移到顶栏一并删除，见本文件下方说明。
const int kNavItemHeight  = 36;   // 单个导航项高度
// 导航项数量不再是常量：仅平台超管能看到「客户端版本」。
// 导航区高度改为建完后按实际项数现算（见 BuildNav 末尾）。
const int kNavGap         = 2;    // 导航项间距
const int kNavPadV        = 10;   // 导航区上下内边距
// 导航区总高度 = 上下内边距 + N 项 + (N-1) 个间距。


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

    // 改文字（按角色改名用，如「企业管理」→「本企业」）。
    void SetLabel(LPCTSTR label)
    {
        m_label = label;
        Invalidate();
    }

    // 取文字。
    CString GetLabel() const { return m_label; }

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
    // 敏感词：平台超管与企业管理员都可见，排在「审计日志」之后（同属安全合规类）。
    // 侧栏里的位置与导航 ID 无关，界面映射只看 kIdNavSensitiveWords 的数值。
    nav->AddChild(MakeNavItem(kIdNavSensitiveWords, ui::Icon::ShieldAlert, _T("敏感词")),
                  Hint().Fixed(kNavItemHeight));
    // 文件共享：平台超管与企业管理员都可见，紧接「敏感词」（同属合规管控类）。2026-10-08 起「共享审计」并入
    // 文件共享页成为它的第二个页签，侧栏不再单列（kIdNavFileShareAudit 的编号保留，不建导航项）。
    nav->AddChild(MakeNavItem(kIdNavFileShares, ui::Icon::Folder, _T("文件共享")),
                  Hint().Fixed(kNavItemHeight));
    // 企业问答：平台超管与企业管理员都可见，显示在群聊「问答」页签里（docs/群问答方案.md）
    nav->AddChild(MakeNavItem(kIdNavEnterpriseQa, ui::Icon::Info, _T("企业问答")),
                  Hint().Fixed(kNavItemHeight));
    // 机器人：平台超管与企业管理员都可见，与「企业问答」并列（docs/机器人方案.md 4.1）
    nav->AddChild(MakeNavItem(kIdNavEnterpriseBot, ui::Icon::Bot, _T("机器人")),
                  Hint().Fixed(kNavItemHeight));
    nav->AddChild(MakeNavItem(kIdNavSettings,      ui::Icon::Settings,  _T("平台设置")),
                  Hint().Fixed(kNavItemHeight));
    // 客户端版本：升级包是平台级资产，不属于任何一家企业，只给平台超管看。一律建出来，
    // 显隐由 ApplyRole 按当前登录者决定 —— 侧栏只建一次，换账号登录时角色可能变。
    nav->AddChild(MakeNavItem(kIdNavClientVersions, ui::Icon::Download, _T("客户端版本")),
                  Hint().Fixed(kNavItemHeight));
    // 高度按全部导航项算。企业管理员隐藏的几项让出的位置落在导航区底部，与下面的
    // 留白连成一片，看不出差别。
    const int navCount = static_cast<int>(m_navItems.size());
    const int navAreaHeight = kNavPadV * 2 + kNavItemHeight * navCount
                              + kNavGap * (navCount - 1);
    m_nav = nav.get();
    AddChild(std::move(nav), Hint().Fixed(navAreaHeight));
    ApplyRole(AdminSession::Instance().IsPlatformAdmin());

    // 弹性空白，占住导航项以下的全部剩余高度。
    // 原本这里的作用是"把页脚顶到底部"，2026-08-12 页脚（管理员姓名/角色 + 登出
    // 按钮）整体移到了顶栏右上角，侧栏底部不再有内容，这块空白就纯粹是留白了。
    AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
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

// 侧栏页脚（管理员姓名 / 角色 + 登出按钮）已于 2026-08-12 整体移到顶栏右上角，
// 见 TopBar 的构造。BuildFooter 及其相关的 m_footerInfo、kFooterHeight 一并删除。

std::unique_ptr<DuiControl> Sidebar::MakeNavItem(int navId, ui::Icon icon,
                                                 LPCTSTR label)
{
    auto item = std::make_unique<SidebarNavItem>();
    item->SetCtrlId(navId);
    item->Setup(icon, label);
    m_navItems.push_back(item.get());
    return item;
}

SidebarNavItem* Sidebar::FindNavItem(int navId) const
{
    for (size_t i = 0; i < m_navItems.size(); ++i)
    {
        if (m_navItems[i] != nullptr && static_cast<int>(m_navItems[i]->GetCtrlId()) == navId)
        {
            return m_navItems[i];
        }
    }
    return nullptr;
}

void Sidebar::ApplyRole(bool platformAdmin)
{
    SidebarNavItem* ents = FindNavItem(kIdNavEnterprises);
    if (ents != nullptr)
    {
        ents->SetLabel(platformAdmin ? _T("企业管理") : _T("本企业"));
    }
    SidebarNavItem* ann = FindNavItem(kIdNavAnnouncements);
    if (ann != nullptr)
    {
        ann->SetVisible(platformAdmin);
    }
    SidebarNavItem* cv = FindNavItem(kIdNavClientVersions);
    if (cv != nullptr)
    {
        cv->SetVisible(platformAdmin);
    }
    // SetVisible 不会触发重排，导航区的矩形没变时 SetRect 也不会往下排，必须 ForceLayout。
    if (m_nav != nullptr)
    {
        m_nav->ForceLayout(m_nav->GetRect());
    }
    Invalidate();
}

bool Sidebar::IsNavVisible(int navId) const
{
    SidebarNavItem* item = FindNavItem(navId);
    return item != nullptr && item->IsVisible();
}

CString Sidebar::GetNavLabel(int navId) const
{
    SidebarNavItem* item = FindNavItem(navId);
    return (item != nullptr) ? item->GetLabel() : CString();
}

void Sidebar::SetActiveNav(int navId)
{
    for (size_t i = 0; i < m_navItems.size(); ++i)
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
    for (size_t i = 0; i < m_navItems.size(); ++i)
    {
        if (m_navItems[i] != nullptr)
        {
            m_navItems[i]->SetCollapsed(collapsed);
        }
    }
    // 头部副标题在紧凑态隐藏。balloonui 的 HBox 布局会跳过不可见子控件，
    // 余下的徽标自动重排。
    //（页脚用户信息原本也在这里一并隐藏，页脚移到顶栏后不再需要。）
    if (m_headerTitles != nullptr)
    {
        m_headerTitles->SetVisible(!collapsed);
    }
    Invalidate();
}

} // namespace flamingoadmin
