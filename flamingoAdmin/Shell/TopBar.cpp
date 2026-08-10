/**
 *  flamingoAdmin —— 顶栏实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Shell/TopBar.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/CommonWidgets.h"
#include "Mock/DataStore.h"

#include "Controls/Input/DuiEditHost.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 顶栏布局常量（96 dpi 逻辑像素）----
const int kCrumbIconSize = 14;    // 面包屑前导图标边长
const int kToggleSize    = 30;    // 折叠侧栏按钮边长
const int kCrumbWidth    = 96;    // 面包屑文字固定宽度
const int kSearchWidth   = 280;   // 搜索框宽度
const int kSearchHeight  = 30;    // 搜索框高度
const int kBellSize      = 30;    // 铃铛按钮边长

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

    // 全局搜索框（寄宿真 EDIT；EnsureCreated 由 AdminFrame 负责）。
    auto search = std::make_unique<DuiEditHost>();
    search->SetCtrlId(kIdTopSearch);
    search->SetPlaceholder(_T("跳转到企业 / 部门 / 员工…"));
    search->SetBgColor(theme::kInk25);
    search->SetShowBorder(true);
    AddChild(std::move(search),
             Hint().Fixed(kSearchWidth, kSearchHeight).AlignC(DuiLayout::AlignCenter));

    // 通知铃铛。
    auto bell = std::make_unique<ui::IconButton>();
    bell->SetCtrlId(kIdTopBell);
    bell->SetIcon(ui::Icon::Bell);
    bell->SetIconSize(16);
    bell->SetColors(theme::kInk500, theme::kInk700, theme::kInk100);
    AddChild(std::move(bell),
             Hint().Fixed(kBellSize, kBellSize).AlignC(DuiLayout::AlignCenter));
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
