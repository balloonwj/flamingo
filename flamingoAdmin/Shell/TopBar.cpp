/**
 *  flamingoAdmin —— 顶栏实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Shell/TopBar.h"
#include "UiIds.h"
#include "Util/AdminApi.h"   // AdminSession：右上角账户区显示当前登录管理员
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/CommonWidgets.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 顶栏布局常量（96 dpi 逻辑像素）----
const int kCrumbIconSize = 14;    // 面包屑前导图标边长
const int kToggleSize    = 30;    // 折叠侧栏按钮边长
const int kCrumbWidth    = 96;    // 面包屑文字固定宽度
const int kLogoutSize    = 30;    // 登出按钮边长（与折叠按钮同尺寸，视觉对齐）
// 右上角账户区（姓名 + 账号·角色两行）。两行高度合计 34，顶栏高 46（见
// theme::kTopBarHeight），垂直居中后上下各余 6，放得下。
const int kAccountWidth      = 220;  // 账户区宽度：放得下「姓名 · 企业管理员」与常见长度的邮箱，更长的在末尾显示省略号
const int kAccountNameHeight = 18;   // 第一行「姓名」行高
const int kAccountRoleHeight = 16;   // 第二行「账号 · 角色」行高

// 审计滚动提示循环展示的最近条数。
const int kTickerCycle   = 6;

} // anonymous namespace

// ===========================================================================
// AuditTicker —— 顶栏审计滚动提示。
//
// 自绘：一个级别圆点 + 一行「最新审计 · <操作者> <说明>」文字。每隔数秒由
// AdminFrame 的定时器调 Advance() 切到下一条，循环展示最近 kTickerCycle 条
// 审计记录。数据由 AdminFrame 单独拉取后经 SetLogs 交给本控件。
// ===========================================================================
class AuditTicker : public DuiControl
{
public:
    // 前进到下一条并重绘。返回 true 表示刚好转完一整圈（回到第一条）。
    bool Advance()
    {
        Invalidate();
        const int cycle = Cycle();
        if (cycle == 0)
        {
            // 没有数据时不报告「转完一圈」：否则调用方每一拍都去拉一次审计，太频繁。
            // 空列表由进入主界面、仪表板加载这两条路径补上数据。
            m_index = 0;
            return false;
        }
        m_index = (m_index + 1) % cycle;
        return m_index == 0;
    }

    // 换一批审计记录，并从第一条重新开始。
    void SetLogs(const std::vector<AuditLog>& logs)
    {
        m_logs  = logs;
        m_index = 0;
        Invalidate();
    }

    // 当前显示的文字；没有数据时为「暂无审计」。
    CString CurrentText() const
    {
        if (m_logs.empty())
        {
            return CString(_T("最新审计 · 暂无审计"));
        }
        const AuditLog& l = m_logs[m_index % Cycle()];
        CString text;
        text.Format(_T("最新审计 · %s  %s"),
                    static_cast<LPCTSTR>(l.m_actor),
                    static_cast<LPCTSTR>(l.m_detail));
        return text;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        const int textLeftWithDot = 18;   // 有级别圆点时文字的左缩进
        const int textLeftNoDot   = 2;    // 没有数据（不画圆点）时文字的左缩进
        int textLeft = textLeftNoDot;
        if (!m_logs.empty())
        {
            const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;
            ui::DrawSeverityDot(hdc, m_rcItem.left + 5, cy,
                                m_logs[m_index % Cycle()].m_severity);
            textLeft = textLeftWithDot;
        }
        RECT rcText = { m_rcItem.left + textLeft, m_rcItem.top,
                        m_rcItem.right - 8, m_rcItem.bottom };
        ui::DrawTextLine(hdc, CurrentText(), rcText, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                         | DT_END_ELLIPSIS);
    }

private:
    // 循环展示的条数：最新的 kTickerCycle 条，不足时取全部。
    int Cycle() const
    {
        return (static_cast<int>(m_logs.size()) < kTickerCycle)
               ? static_cast<int>(m_logs.size()) : kTickerCycle;
    }

    std::vector<AuditLog> m_logs;        // 展示的审计记录（最新在前），由 SetLogs 整体替换
    int                   m_index = 0;   // 当前展示条目，取值 [0, Cycle())
};

// ===========================================================================
// TopBar
// ===========================================================================
TopBar::TopBar()
{
    SetFillColor(theme::kInk0);                       // 白底
    SetEdgeBorder(kEdgeBottom, theme::kInk100);       // 底部分隔线
    SetPadding(12, 0, 18, 0);
    SetGap(10);

    // 折叠 / 展开侧边栏按钮。
    auto toggle = std::make_unique<ui::IconButton>();
    toggle->SetCtrlId(kIdSidebarToggle);
    toggle->SetIcon(ui::Icon::Menu);
    toggle->SetIconSize(18);
    toggle->SetColors(theme::kInk500, theme::kInk700, theme::kInk100);
    toggle->SetTooltip(_T("折叠 / 展开侧边栏"));
    AddChild(std::move(toggle),
             Hint().Fixed(kToggleSize, kToggleSize).AlignC(DuiLayout::AlignCenter));

    // 面包屑前导图标（固定用一个通用的"位置"图标）。
    auto crumbIcon = std::make_unique<ui::IconView>();
    crumbIcon->SetIcon(ui::Icon::Dashboard);
    crumbIcon->SetColor(theme::kInk400);
    crumbIcon->SetIconSize(kCrumbIconSize);
    AddChild(std::move(crumbIcon),
             Hint().Fixed(16, 16).AlignC(DuiLayout::AlignCenter));

    // 面包屑文字（固定宽度）。
    auto crumb = ui::MakeLabel(_T("仪表板"), theme::kFontSizeNormal, false,
                               theme::kInk500);
    m_breadcrumb = crumb.get();
    AddChild(std::move(crumb), Hint().Fixed(kCrumbWidth));

    // 审计滚动提示。占据中部全部剩余宽度，把搜索框 / 铃铛挤到右侧。
    auto ticker = std::make_unique<AuditTicker>();
    m_ticker = ticker.get();
    AddChild(std::move(ticker), Hint().Weight(1));

    // 当前管理员的账户区：第一行「姓名 · 角色」，第二行完整的登录账户（登录邮箱）。与登出
    // 按钮一同从侧边栏页脚移来，两行右对齐紧靠登出按钮。
    //
    // 内容取自 AdminSession 而不是写死。顶栏只在第一次登录时构建，换账号登录时由
    // AdminFrame 调 SetAccount 刷新这两行。
    {
        const AdminSession& session = AdminSession::Instance();
        const UINT kRightAlign = DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;

        std::unique_ptr<DuiVBox> account_box(new DuiVBox());
        std::unique_ptr<DuiLabel> nameLabel = ui::MakeLabel(
            MakeNameRoleLine(session.Name(), session.IsPlatformAdmin()),
            theme::kFontSizeNormal, true, theme::kInk700, kRightAlign);
        m_nameLine = nameLabel.get();
        account_box->AddChild(std::move(nameLabel), Hint().Fixed(kAccountNameHeight));
        std::unique_ptr<DuiLabel> accountLabel = ui::MakeLabel(
            session.Email(), theme::kFontSizeSmall, false, theme::kInk400, kRightAlign);
        m_accountLine = accountLabel.get();
        account_box->AddChild(std::move(accountLabel), Hint().Fixed(kAccountRoleHeight));
        AddChild(std::unique_ptr<DuiControl>(account_box.release()),
                 Hint().Fixed(kAccountWidth, kAccountNameHeight + kAccountRoleHeight)
                       .AlignC(DuiLayout::AlignCenter));
    }

    // 登出按钮。原先在侧边栏页脚（左下角），2026-08-12 移到这里 —— 顶栏右上角
    // 是用户找"退出登录"的习惯位置。控件 ID 仍是 kIdLogout，点击照旧冒泡到
    // AdminFrame::OnDuiNotify 的同一个分支，处理逻辑未变。
    auto logout = std::make_unique<ui::IconButton>();
    logout->SetCtrlId(kIdLogout);
    logout->SetIcon(ui::Icon::Logout);
    logout->SetIconSize(16);
    logout->SetColors(theme::kInk500, theme::kInk700, theme::kInk100);
    logout->SetTooltip(_T("退出登录"));
    AddChild(std::move(logout),
             Hint().Fixed(kLogoutSize, kLogoutSize).AlignC(DuiLayout::AlignCenter));
}

void TopBar::SetBreadcrumb(LPCTSTR text)
{
    if (m_breadcrumb != nullptr)
    {
        m_breadcrumb->SetText(text);
    }
}

bool TopBar::AdvanceTicker()
{
    return (m_ticker != nullptr) ? m_ticker->Advance() : false;
}

void TopBar::SetTickerAudits(const std::vector<AuditLog>& logs)
{
    if (m_ticker != nullptr)
    {
        m_ticker->SetLogs(logs);
    }
}

void TopBar::SetAccount(const CString& name, const CString& email, bool platformAdmin)
{
    if (m_nameLine != nullptr)
    {
        m_nameLine->SetText(MakeNameRoleLine(name, platformAdmin));
    }
    if (m_accountLine != nullptr)
    {
        m_accountLine->SetText(email);
    }
}

CString TopBar::GetNameLine() const
{
    return (m_nameLine != nullptr) ? m_nameLine->GetText() : CString();
}

CString TopBar::GetAccountLine() const
{
    return (m_accountLine != nullptr) ? m_accountLine->GetText() : CString();
}

CString TopBar::GetTickerText() const
{
    return (m_ticker != nullptr) ? m_ticker->CurrentText() : CString();
}

CString TopBar::MakeNameRoleLine(const CString& name, bool platformAdmin)
{
    const LPCTSTR role = platformAdmin ? _T("平台超管") : _T("企业管理员");
    if (name.IsEmpty())
    {
        return CString(role);
    }
    CString line;
    line.Format(_T("%s · %s"), static_cast<LPCTSTR>(name), role);
    return line;
}

} // namespace flamingoadmin
