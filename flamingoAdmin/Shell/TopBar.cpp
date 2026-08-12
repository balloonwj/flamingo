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
#include "Mock/DataStore.h"

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
const int kAccountWidth      = 150;  // 账户区宽度；姓名过长时由 DuiLabel 自行截断
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
// 审计记录。数据实时取自 DataStore。
// ===========================================================================
class AuditTicker : public DuiControl
{
public:
    // 前进到下一条并重绘。
    void Advance()
    {
        ++m_index;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        const std::vector<AuditLog>& logs = DataStore::Instance().AuditLogs();
        if (logs.empty())
        {
            return;
        }
        // 审计日志最新者在前；循环展示最近 kTickerCycle 条。
        const int cycle = (static_cast<int>(logs.size()) < kTickerCycle)
                          ? static_cast<int>(logs.size()) : kTickerCycle;
        const AuditLog& l = logs[m_index % cycle];

        const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;
        ui::DrawSeverityDot(hdc, m_rcItem.left + 5, cy, l.m_severity);

        CString text;
        text.Format(_T("最新审计 · %s  %s"),
                    static_cast<LPCTSTR>(l.m_actor),
                    static_cast<LPCTSTR>(l.m_detail));
        RECT rcText = { m_rcItem.left + 18, m_rcItem.top,
                        m_rcItem.right - 8, m_rcItem.bottom };
        ui::DrawTextLine(hdc, text, rcText, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                         | DT_END_ELLIPSIS);
    }

private:
    int m_index = 0;   // 当前展示条目（对 cycle 取模）
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

    // 当前管理员的姓名与角色。与登出按钮一同从侧边栏页脚移来，两行右对齐紧靠
    // 登出按钮，构成右上角常见的「账户区」。
    //
    // 内容取自 AdminSession 而不是像原页脚那样写死"李正阳 / admin · 超管" ——
    // 顶栏与侧栏一样是登录成功后才构建的，此时会话已填好，直接显示真实登录者
    // 更有意义（写死的名字在换账号登录时是错的）。
    {
        const AdminSession& session = AdminSession::Instance();

        //第二行形如「li · 超管」：取邮箱 @ 之前的部分作账号名，与角色拼在一起。
        //邮箱异常（无 @）时退化为整串，不做额外处理 —— 这里只是展示。
        CString account = session.Email();
        const int atPos = account.Find(_T('@'));
        if (atPos > 0)
        {
            account = account.Left(atPos);
        }
        CString roleLine;
        roleLine.Format(_T("%s · %s"), (LPCTSTR)account,
                        session.IsPlatformAdmin() ? _T("超管") : _T("企业管理员"));

        const UINT kRightAlign = DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

        std::unique_ptr<DuiVBox> account_box(new DuiVBox());
        account_box->AddChild(ui::MakeLabel(session.Name(), theme::kFontSizeNormal, true,
                                            theme::kInk700, kRightAlign),
                              Hint().Fixed(kAccountNameHeight));
        account_box->AddChild(ui::MakeLabel(roleLine, theme::kFontSizeSmall, false,
                                            theme::kInk400, kRightAlign),
                              Hint().Fixed(kAccountRoleHeight));
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

void TopBar::AdvanceTicker()
{
    if (m_ticker != nullptr)
    {
        m_ticker->Advance();
    }
}

} // namespace flamingoadmin
