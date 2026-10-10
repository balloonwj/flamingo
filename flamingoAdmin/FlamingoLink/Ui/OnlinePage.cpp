/**
 *  FlamingoLink.dll —— 「在线用户」页面窗口的实现，说明见 OnlinePage.h。
 *
 *  样式与控制台的列表页一致（参照 flamingoAdmin/Screens/FileSharesScreen.cpp）：
 *  浅灰底色上一张白色圆角卡片，表头 40 像素、行高 44 像素，悬停行灰底、行底细分隔线。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/Ui/OnlinePage.h"

#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/Layout/DuiLayout.h"

#include "FlamingoLink/FlamingoLinkApi.h"
#include "FlamingoLink/Core/LinkText.h"
#include "Theme/AdminTheme.h"
#include "Widgets/ActionTable.h"
#include "Widgets/ColumnHeader.h"
#include "Widgets/UiHelpers.h"

using namespace balloonwjui;
namespace theme = flamingoadmin::theme;
namespace aui = flamingoadmin::ui;

namespace flamingolink {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kPagePadTop     = 12;   // 页面上内边距（与上方的子页签条隔开）
const int kBarH           = 36;   // 顶部一行（汇总、连接状态、搜索框）的高度
const int kBarGap         = 12;   // 顶部一行内控件之间的间距
const int kRowGap         = 12;   // 顶部一行与表格卡片之间的间距
const int kStatusW        = 220;  // 连接状态文字的宽度（容得下「连接已断开，30 秒后第 12 次重连」）
const int kSearchW        = 220;  // 搜索框宽度（窗口取默认宽度时给左边的汇总文字留够位置）
const int kTableHeaderH   = 40;   // 表头高度
const int kRowHeight      = 44;   // 表格行高
const int kCellPad        = 14;   // 单元格左右内边距
const int kDotSize        = 8;    // 状态圆点的直径
const int kDotGap         = 6;    // 状态圆点与状态文字之间的间距

// ---- 表格的列（像素）----
const int kColNameW     = 150;  // 姓名
const int kColAccountW  = 140;  // 账号
const int kColDeviceW   = 90;   // 设备
const int kColStateW    = 90;   // 状态
const int kColLoginW    = 150;  // 登录时间
const int kColIpW       = 140;  // IP 地址

// 列的序号，与 BuildColumns 的顺序一致
enum Column
{
    kColName = 0,    // 姓名（昵称，为空时显示账号）
    kColAccount,     // 账号
    kColDept,        // 部门（占满剩余宽度）
    kColDevice,      // 设备类型
    kColState,       // 在线状态（圆点 + 文字）
    kColLogin,       // 本次登录时间
    kColIp,          // 来源 IP 地址
    kColumnCount     // 列数
};

// 控件编号：只在本页面窗口里用来区分通知来源
const UINT kIdSearch = 101;

// 等待重连期间刷新倒计时的定时器编号与周期（毫秒）
const UINT_PTR kCountdownTimerId = 1;
const UINT     kCountdownPeriodMs = 1000;

// 表格的列定义
std::vector<aui::ColumnSpec> BuildColumns()
{
    std::vector<aui::ColumnSpec> cols;
    cols.push_back(aui::ColumnSpec(_T("姓名"), kColNameW));
    cols.push_back(aui::ColumnSpec(_T("账号"), kColAccountW));
    cols.push_back(aui::ColumnSpec(_T("部门"), 0, true));
    cols.push_back(aui::ColumnSpec(_T("设备"), kColDeviceW));
    cols.push_back(aui::ColumnSpec(_T("状态"), kColStateW));
    cols.push_back(aui::ColumnSpec(_T("登录时间"), kColLoginW));
    cols.push_back(aui::ColumnSpec(_T("IP 地址"), kColIpW));
    return cols;
}

// 一个单行文字标签
std::unique_ptr<DuiLabel> MakeLabel(COLORREF color, int pointSize, bool bold)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetTextColor(color);
    label->SetTextPointSize(pointSize, bold);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    return label;
}

} // anonymous namespace

COnlinePage::COnlinePage()
    : m_source(NULL)
    , m_summary(NULL)
    , m_status(NULL)
    , m_search(NULL)
    , m_header(NULL)
    , m_table(NULL)
    , m_model(NULL)
    , m_dimmed(false)
    , m_countdownOn(false)
{
}

COnlinePage::~COnlinePage()
{
    if (IsWindow())
        DestroyWindow();
}

bool COnlinePage::CreatePage(HWND parent, IOnlineViewSource* source)
{
    m_source = source;
    RECT rc = { 0, 0, 0, 0 };
    //WS_CLIPCHILDREN：本窗口自己不画，内容全在子窗口 DuiHost 里
    return Create(parent, rc, NULL, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS) != NULL;
}

LRESULT COnlinePage::OnCreate(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    bHandled = TRUE;
    RECT rc = { 0, 0, 0, 0 };
    if (m_host.Create(m_hWnd, rc, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN) == NULL)
        return -1;
    m_host.SetRoot(BuildUi());
    return 0;
}

LRESULT COnlinePage::OnSize(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    if (m_host.IsWindow())
        m_host.MoveWindow(0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
    if (m_header != NULL && m_table != NULL)
        m_header->SetTrailingInset(m_table->ScrollBarWidth());
    return 0;
}

LRESULT COnlinePage::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    bHandled = TRUE;
    return 1;
}

LRESULT COnlinePage::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
{
    if (wParam != kCountdownTimerId)
    {
        bHandled = FALSE;
        return 0;
    }

    bHandled = TRUE;
    if (m_source != NULL)
    {
        OnlineViewState vs;
        m_source->GetViewState(vs);
        RefreshStatus(vs);
    }
    return 0;
}

LRESULT COnlinePage::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    bHandled = FALSE;
    if (m_countdownOn)
    {
        KillTimer(kCountdownTimerId);
        m_countdownOn = false;
    }
    return 0;
}

LRESULT COnlinePage::OnDuiNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    const DuiNotify* n = reinterpret_cast<const DuiNotify*>(lParam);
    if (n == NULL)
        return 0;

    //搜索框文字变化（用户输入或程序设置都会发）：名单很小，直接重新筛选，不做防抖
    if (n->code == DUIN_VALUECHANGED && n->ctrlId == kIdSearch && m_source != NULL)
    {
        OnlineViewState vs;
        m_source->GetViewState(vs);
        RefreshRows(vs);
    }
    return 0;
}

std::unique_ptr<DuiControl> COnlinePage::BuildUi()
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(theme::kInk25);
    root->SetPadding(0, kPagePadTop, 0, 0);

    // ---- 顶部一行：汇总 …… 连接状态 搜索框 ----
    std::unique_ptr<DuiHBox> bar(new DuiHBox());
    bar->SetGap(kBarGap);
    {
        std::unique_ptr<DuiLabel> summary = MakeLabel(theme::kInk700, theme::kFontSizeNormal, true);
        m_summary = summary.get();
        bar->AddChild(std::move(summary), Hint().Weight(1));

        std::unique_ptr<DuiLabel> status = MakeLabel(theme::kInk400, theme::kFontSizeNormal, false);
        status->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        m_status = status.get();
        bar->AddChild(std::move(status), Hint().Fixed(kStatusW));

        std::unique_ptr<DuiEdit> search(new DuiEdit());
        search->SetCtrlId(kIdSearch);
        search->SetPlaceholder(_T("搜索姓名、账号或部门"));
        search->SetBgColor(theme::kInk0);
        search->SetShowBorder(true);
        m_search = search.get();
        bar->AddChild(std::move(search), Hint().Fixed(kSearchW));
    }
    root->AddChild(std::move(bar), Hint().Fixed(kBarH).Margin(0, 0, 0, kRowGap));

    // ---- 表格卡片：表头 + 表格 ----
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetBgColor(theme::kInk0);
    card->SetCornerRadius(theme::kRadiusLg);
    card->SetBorderColor(theme::kInk100);
    card->SetBorderWidth(1.0f);
    {
        std::unique_ptr<aui::ColumnHeader> head(new aui::ColumnHeader());
        head->SetColumns(BuildColumns());
        head->SetCellPadding(kCellPad);
        m_header = head.get();
        card->AddChild(std::move(head), Hint().Fixed(kTableHeaderH));

        std::unique_ptr<aui::ActionTable> table(new aui::ActionTable());
        table->SetRowHeight(kRowHeight);
        table->SetRowPainter(&COnlinePage::PaintRowThunk, this);
        table->SetActionAreaWidth(0);
        table->SetTooltipProvider(&COnlinePage::TooltipThunk, this);
        m_table = table.get();
        card->AddChild(std::move(table), Hint().Weight(1));
    }
    root->AddChild(std::move(card), Hint().Weight(1));

    return std::unique_ptr<DuiControl>(root.release());
}

void COnlinePage::Refresh()
{
    if (m_source == NULL || m_summary == NULL)
        return;

    OnlineViewState vs;
    m_source->GetViewState(vs);

    m_summary->SetText(vs.m_received ? SummaryText(vs.m_table->Summary()) : CString());
    RefreshStatus(vs);
    RefreshRows(vs);
}

void COnlinePage::RefreshStatus(const OnlineViewState& vs)
{
    if (m_status == NULL)
        return;

    const ULONGLONG elapsed = ::GetTickCount64() - vs.m_waitStartTick;
    const int remain = (vs.m_state == link_state_waiting)
                           ? RemainingSeconds(static_cast<int>(elapsed), vs.m_delayMs) : 0;
    m_status->SetText(ConnectionText(vs.m_state, vs.m_attempt, remain));
    m_status->SetTextColor(ConnectionColor(vs.m_state));

    //只在等待重连期间开倒计时定时器
    const bool wantCountdown = (vs.m_state == link_state_waiting);
    if (wantCountdown && !m_countdownOn && IsWindow())
    {
        SetTimer(kCountdownTimerId, kCountdownPeriodMs, NULL);
        m_countdownOn = true;
    }
    else if (!wantCountdown && m_countdownOn)
    {
        KillTimer(kCountdownTimerId);
        m_countdownOn = false;
    }
}

void COnlinePage::RefreshRows(const OnlineViewState& vs)
{
    if (m_table == NULL || vs.m_table == NULL)
        return;

    m_model = vs.m_table;
    const CString keyword = (m_search != NULL) ? m_search->GetText() : CString();
    m_model->Filter(keyword, m_view);
    m_dimmed = (vs.m_state != link_state_connected);

    OverlayInput in;
    in.m_hasEnterprise = vs.m_hasEnterprise;
    in.m_state = vs.m_state;
    in.m_lastError = vs.m_lastError;
    in.m_received = vs.m_received;
    in.m_totalRows = static_cast<int>(m_model->Rows().size());
    in.m_filteredRows = static_cast<int>(m_view.size());
    const CString overlay = OverlayText(in);

    //有提示文字时不显示任何行：提示所说的情形下表格里的数据都不可信或不存在
    m_table->SetRowCount(overlay.IsEmpty() ? static_cast<int>(m_view.size()) : 0);
    m_table->SetOverlayText(overlay);
    m_table->Invalidate();
    if (m_header != NULL)
        m_header->SetTrailingInset(m_table->ScrollBarWidth());
}

void COnlinePage::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    static_cast<COnlinePage*>(user)->PaintRow(hdc, rowIndex, rowRect, selected, hover);
}

void COnlinePage::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    if (m_model == NULL || m_header == NULL || rowIndex < 0 || rowIndex >= static_cast<int>(m_view.size()))
        return;

    const int modelIndex = m_view[static_cast<size_t>(rowIndex)];
    if (modelIndex < 0 || modelIndex >= static_cast<int>(m_model->Rows().size()))
        return;
    const OnlineRow& row = m_model->Rows()[static_cast<size_t>(modelIndex)];

    //选中行用品牌色的浅底，悬停行用灰底
    if (selected)
        aui::FillRoundRect(hdc, rowRect, theme::kFlamingo50, 0);
    else if (hover)
        aui::FillRoundRect(hdc, rowRect, theme::kInk25, 0);

    std::vector<aui::ColumnBound> b;
    aui::ColumnHeader::ComputeColumnBounds(m_header->Columns(), rowRect.left, rowRect.right, b);
    if (b.size() < static_cast<size_t>(kColumnCount))
        return;

    //连接不正常时整行用浅灰色画，表示这是断线前的数据
    const COLORREF mainColor = m_dimmed ? theme::kInk300 : theme::kInk700;
    const COLORREF minorColor = m_dimmed ? theme::kInk300 : theme::kInk500;
    HFONT font = aui::UiFont(theme::kFontSizeNormal, false);
    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;

    CString cells[kColumnCount];
    cells[kColName] = row.m_nickname.IsEmpty() ? row.m_account : row.m_nickname;
    cells[kColAccount] = row.m_account;
    cells[kColDept] = row.m_depts.IsEmpty() ? CString(_T("—")) : row.m_depts;
    cells[kColDevice] = ClientTypeText(row.m_clientType);
    cells[kColState] = PresenceText(row.m_presence);
    cells[kColLogin] = FormatLoginTime(row.m_loginTimeMs);
    cells[kColIp] = row.m_ip.IsEmpty() ? CString(_T("—")) : row.m_ip;

    for (int i = 0; i < kColumnCount; ++i)
    {
        RECT rc = { b[i].m_left + kCellPad, rowRect.top, b[i].m_right - kCellPad, rowRect.bottom };
        if (rc.right <= rc.left)
            continue;

        //状态列：先画圆点，文字跟在圆点后面
        if (i == kColState)
        {
            const int cy = (rowRect.top + rowRect.bottom) / 2;
            const RECT dot = { rc.left, cy - kDotSize / 2, rc.left + kDotSize, cy - kDotSize / 2 + kDotSize };
            aui::FillRoundRect(hdc, dot, m_dimmed ? theme::kInk300 : PresenceColor(row.m_presence), kDotSize / 2);
            rc.left += kDotSize + kDotGap;
        }

        const COLORREF color = (i == kColName) ? mainColor : minorColor;
        aui::DrawTextLine(hdc, cells[i], rc, color, font, flags);
    }

    //行底分隔线
    aui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

CString COnlinePage::TooltipThunk(void* user, POINT pt)
{
    COnlinePage* self = static_cast<COnlinePage*>(user);
    if (self->m_table == NULL || self->m_header == NULL || self->m_model == NULL)
        return CString();

    const int rowIndex = self->m_table->RowFromPoint(pt);
    if (rowIndex < 0 || rowIndex >= static_cast<int>(self->m_view.size()))
        return CString();

    //只在部门一列上显示提示：部门多时会被截断，提示里给出完整内容
    std::vector<aui::ColumnBound> b;
    const RECT rcTable = self->m_table->GetRect();
    aui::ColumnHeader::ComputeColumnBounds(self->m_header->Columns(), rcTable.left,
                                           rcTable.right - self->m_table->ScrollBarWidth(), b);
    if (b.size() < static_cast<size_t>(kColumnCount) || pt.x < b[kColDept].m_left || pt.x >= b[kColDept].m_right)
        return CString();

    const OnlineRow& row = self->m_model->Rows()[static_cast<size_t>(self->m_view[static_cast<size_t>(rowIndex)])];
    return row.m_depts;
}

} // namespace flamingolink
