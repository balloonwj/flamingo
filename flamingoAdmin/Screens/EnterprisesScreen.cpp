/**
 *  flamingoAdmin —— 企业管理界面实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/EnterprisesScreen.h"
#include "AdminFrame.h"            // WM_APP_API_RESULT
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Util/HttpWorker.h"
#include "Util/AppConfig.h"
#include "Util/Logger.h"

#include "Controls/Input/DuiEdit.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton
#include "DuiNotify.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 企业管理界面布局常量（96 dpi 逻辑像素）----
const int kHeaderH     = 100;  // 页头高度
const int kStatStripH  = 80;   // 统计卡行高
const int kFilterRowH  = 36;   // 筛选行高
const int kSideMargin  = 24;   // 内容区左 / 右 / 下统一外边距
const int kStatGap     = 12;   // 统计卡间距
const int kFilterGap   = 10;   // 筛选行控件间距

// ---- 表格度量 ----
const int kRowHeight     = 64;  // 数据行高
const int kHeaderRowH    = 40;  // 表头行高
const int kFooterH       = 42;  // 表尾行高
const int kCellPad       = 14;  // 单元格左 / 右内距
const int kAccentBarW    = 3;   // hover 行左侧强调条宽度
const int kRowAvatar     = 36;  // 「企业」列头像边长
const int kAdminAvatar   = 24;  // 「主管理员」列头像边长

// ---- 表格各列宽度（像素）。最后的「操作」列占剩余宽度 ----
const int kColEnterprise = 270;  // 企业（头像 + 名称 + ID）
const int kColIndustry   = 140;  // 行业 · 地区
const int kColScale      = 96;   // 规模（人数 + 部门数，右对齐）
const int kColPlan       = 104;  // 套餐
const int kColAdmin      = 240;  // 主管理员
const int kColStatus     = 104;  // 状态
const int kColDate       = 110;  // 开通日期

// ---- 数据拉取 ----
// 服务端分页：每页条数。
const int kPageSize = 20;

// 算出 8 个列边界的左 x 坐标（xs[7] 为「操作」列左边界）。
void ColumnLefts(int rowLeft, int xs[8])
{
    xs[0] = rowLeft;
    xs[1] = xs[0] + kColEnterprise;
    xs[2] = xs[1] + kColIndustry;
    xs[3] = xs[2] + kColScale;
    xs[4] = xs[3] + kColPlan;
    xs[5] = xs[4] + kColAdmin;
    xs[6] = xs[5] + kColStatus;
    xs[7] = xs[6] + kColDate;
}

// 状态分段下标 → 服务端 list.do 的 status 参数。
// 分段：0 全部 / 1 活跃 / 2 待审核 / 3 已停用。
// 服务端 status：-1 不限 / 0 待审核 / 1 活跃 / 2 停用（见 AdminApi.h 注释）。
int SegmentToServerStatus(int seg)
{
    switch (seg)
    {
    case 1:   // 活跃
        return 1;
    case 2:   // 待审核
        return 0;
    case 3:   // 已停用
        return 2;
    default:  // 全部
        return -1;
    }
}

// 把整数格式化为带千分位的字符串。
CString FormatThousands(int n)
{
    CString raw;
    raw.Format(_T("%d"), n);
    CString out;
    int     count = 0;
    for (int i = raw.GetLength() - 1; i >= 0; --i)
    {
        out = CString(raw[i]) + out;
        if (++count % 3 == 0 && i > 0)
        {
            out = CString(_T(',')) + out;
        }
    }
    return out;
}

// ===========================================================================
// EnterpriseTable —— 企业表格控件。
//
// 在 DuiVirtualList 之上加一层「占位文字」覆盖层：加载中 / 加载失败 / 列表为空
// 时行数为 0，基类只画空白，本控件再在表格区中央补画一行提示文字。
// ===========================================================================
class EnterpriseTable : public DuiVirtualList
{
public:
    // 设置覆盖层提示文字。空串表示不显示覆盖层（正常显示数据行）。
    void SetOverlayText(const CString& text)
    {
        if (m_overlayText == text)
        {
            return;
        }
        m_overlayText = text;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        // 先让基类把数据行画出来；行数为 0 时画的是空白。
        DuiVirtualList::OnPaint(hdc, rcDirty);

        if (m_overlayText.IsEmpty())
        {
            return;
        }
        RECT rc = m_rcItem;
        ui::DrawTextLine(hdc, m_overlayText, rc, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 命中测试：host-客户区坐标 pt 落在第几行（含已应用的滚动）。
    //   pt：host-客户区坐标点。
    //   返回：行索引；不在任何数据行上返回 -1。
    // 算法复刻 DuiVirtualList::IndexFromPoint（该方法是私有的，无法直接复用）。
    int RowFromPoint(POINT pt) const
    {
        if (!::PtInRect(&m_rcItem, pt))
        {
            return -1;
        }
        const int rowH = GetRowHeight();
        if (rowH <= 0)
        {
            return -1;
        }
        const int rel = (pt.y - m_rcItem.top) + GetScrollPos();
        const int idx = rel / rowH;
        if (idx < 0 || idx >= GetRowCount())
        {
            return -1;
        }
        return idx;
    }

    // 命中测试：点是否落在某数据行的「操作」列（三个点所在的最后一列）。
    //   pt：host-客户区坐标点。
    //   返回：命中操作列时返回该行索引；不在任何行的操作列上返回 -1。
    int RowFromActionColumn(POINT pt) const
    {
        const int row = RowFromPoint(pt);
        if (row < 0)
        {
            return -1;
        }
        int xs[8];
        ColumnLefts(m_rcItem.left, xs);
        // xs[7] 是「操作」列的左边界，其右侧到行尾即三个点所在区域。
        if (pt.x < xs[7])
        {
            return -1;
        }
        return row;
    }

private:
    CString m_overlayText;   // 覆盖层提示文字，空串表示不显示
};

// ===========================================================================
// TableHeader —— 企业表格表头。自绘列标题 + 底部分隔线。
// ===========================================================================
class TableHeader : public DuiControl
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        int xs[8];
        ColumnLefts(m_rcItem.left, xs);

        HFONT font = ui::UiFont(theme::kFontSizeSmall, false);
        const COLORREF clr = theme::kInk400;

        DrawCell(hdc, xs[0], xs[1], _T("企业"),       clr, font, false);
        DrawCell(hdc, xs[1], xs[2], _T("行业 · 地区"), clr, font, false);
        DrawCell(hdc, xs[2], xs[3], _T("规模"),       clr, font, true);
        DrawCell(hdc, xs[3], xs[4], _T("套餐"),       clr, font, false);
        DrawCell(hdc, xs[4], xs[5], _T("主管理员"),   clr, font, false);
        DrawCell(hdc, xs[5], xs[6], _T("状态"),       clr, font, false);
        DrawCell(hdc, xs[6], xs[7], _T("开通日期"),   clr, font, false);

        // 底部分隔线。
        ui::FillLine(hdc, m_rcItem.left, m_rcItem.bottom - 1,
                     m_rcItem.right - m_rcItem.left, 1, theme::kInk100);
    }

private:
    void DrawCell(HDC hdc, int left, int right, LPCTSTR text,
                  COLORREF clr, HFONT font, bool rightAlign)
    {
        RECT rc = { left + kCellPad, m_rcItem.top,
                    right - kCellPad, m_rcItem.bottom };
        ui::DrawTextLine(hdc, text, rc, clr, font,
                         (rightAlign ? DT_RIGHT : DT_LEFT)
                         | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
};

// ===========================================================================
// TableFooter —— 企业表格表尾。左侧「共 N 条」+ 右侧分页（上一页 / 页码 /
// 下一页）。翻页按钮点击经 m_owner 回调 EnterprisesScreen::TurnPage。
// ===========================================================================
class TableFooter : public DuiControl
{
public:
    // 由 EnterprisesScreen 注入自身指针，翻页按钮点击时回调它。
    void SetOwner(EnterprisesScreen* owner) { m_owner = owner; }

    // 设置分页信息：当前页码 / 总条数 / 每页条数。
    void SetPageInfo(int page, int totalCount, int pageSize)
    {
        m_page     = page;
        m_total    = totalCount;
        m_pageSize = pageSize;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        // 顶部分隔线。
        ui::FillLine(hdc, m_rcItem.left, m_rcItem.top,
                     m_rcItem.right - m_rcItem.left, 1, theme::kInk100);

        HFONT font = ui::UiFont(theme::kFontSizeSmall, false);
        const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;

        // 左侧：共 N 条。
        CString left;
        left.Format(_T("共 %d 条"), m_total);
        ui::DrawTextLine(hdc, left,
                         RECT{ m_rcItem.left + kCellPad, m_rcItem.top,
                               m_rcItem.left + 240, m_rcItem.bottom },
                         theme::kInk500, font,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        const int pageCount = PageCount();
        const int btnW = 60;
        const int btnH = 24;

        // 右侧：[上一页] 第 X / 共 Y 页 [下一页]。
        m_nextRc.right  = m_rcItem.right - kCellPad;
        m_nextRc.left   = m_nextRc.right - btnW;
        m_nextRc.top    = cy - btnH / 2;
        m_nextRc.bottom = cy + btnH / 2;
        DrawPagerBtn(hdc, m_nextRc, _T("下一页"), m_page < pageCount);

        CString pageText;
        pageText.Format(_T("第 %d / 共 %d 页"), m_page, pageCount);
        RECT pageRc = { m_nextRc.left - 8 - 120, cy - 10,
                        m_nextRc.left - 8, cy + 10 };
        ui::DrawTextLine(hdc, pageText, pageRc, theme::kInk500, font,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        m_prevRc.right  = pageRc.left - 8;
        m_prevRc.left   = m_prevRc.right - btnW;
        m_prevRc.top    = cy - btnH / 2;
        m_prevRc.bottom = cy + btnH / 2;
        DrawPagerBtn(hdc, m_prevRc, _T("上一页"), m_page > 1);
    }

    bool OnLButtonUp(POINT pt, UINT /*mkFlags*/) override
    {
        if (m_owner == nullptr)
        {
            return true;
        }
        if (::PtInRect(&m_prevRc, pt) && m_page > 1)
        {
            m_owner->TurnPage(-1);
        }
        else if (::PtInRect(&m_nextRc, pt) && m_page < PageCount())
        {
            m_owner->TurnPage(1);
        }
        return true;
    }

private:
    // 按总条数与每页条数算出总页数（至少 1）。
    int PageCount() const
    {
        const int per = (m_pageSize > 0) ? m_pageSize : 1;
        const int n = (m_total + per - 1) / per;
        return n < 1 ? 1 : n;
    }

    // 画一个分页小按钮（禁用态灰底灰字）。
    void DrawPagerBtn(HDC hdc, const RECT& rc, LPCTSTR text, bool enabled)
    {
        ui::FillRoundRect(hdc, rc, enabled ? theme::kInk0 : theme::kInk25, 5);
        ui::StrokeRoundRect(hdc, rc, theme::kInk100, 5, 1.0f);
        ui::DrawTextLine(hdc, text, rc,
                         enabled ? theme::kInk600 : theme::kInk300,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    EnterprisesScreen* m_owner = nullptr;  // 宿主界面（翻页回调）
    int  m_page     = 1;                   // 当前页码
    int  m_total    = 0;                   // 总条数
    int  m_pageSize = 1;                   // 每页条数
    RECT m_prevRc   = { 0, 0, 0, 0 };      // 「上一页」按钮命中矩形
    RECT m_nextRc   = { 0, 0, 0, 0 };      // 「下一页」按钮命中矩形
};

} // anonymous namespace

// ===========================================================================
// EnterprisesScreen
// ===========================================================================
EnterprisesScreen::EnterprisesScreen()
{
    // 行点击通知经本控件的 ctrlId 上报，故给本控件设 kIdEntTable。
    SetCtrlId(kIdEntTable);

    // ---- 页头（含导出 CSV / 新增企业按钮）----
    auto header = std::make_unique<ui::PageHeader>();
    header->SetTexts(_T("企业管理"), _T("所有企业"),
                     _T("入驻 FlamingoIM 平台的企业租户"));

    auto btnExport = std::make_unique<DuiButton>();
    btnExport->SetCtrlId(kIdEntExport);
    btnExport->SetText(_T("导出 CSV"));
    btnExport->SetVariant(DuiButton::Variant::Default);
    btnExport->SetTextPointSize(theme::kFontSizeNormal);
    header->AddAction(std::move(btnExport), 96);

    auto btnNew = std::make_unique<DuiButton>();
    btnNew->SetCtrlId(kIdEntNew);
    btnNew->SetText(_T("新增企业"));
    btnNew->SetVariant(DuiButton::Variant::Primary);
    // 14px 白色 "+" 图标:Primary 文字色是 kInk0(白)。
    static HBITMAP s_iconPlusWhite = ui::RenderIconToBitmap(
        ui::Icon::Plus, 14, theme::kInk0);
    btnNew->SetLeadingIcon(s_iconPlusWhite);
    btnNew->SetLeadingIconSize(14);
    btnNew->SetTextPointSize(theme::kFontSizeNormal);
    header->AddAction(std::move(btnNew), 112);

    AddChild(std::move(header), Hint().Fixed(kHeaderH));

    // ---- 统计卡行（5 个）----
    auto statStrip = std::make_unique<DuiHBox>();
    statStrip->SetGap(kStatGap);
    {
        auto t1 = std::make_unique<ui::StatTile>();
        m_tileTotal = t1.get();
        statStrip->AddChild(std::move(t1), Hint().Weight(1));
        auto t2 = std::make_unique<ui::StatTile>();
        m_tileActive = t2.get();
        statStrip->AddChild(std::move(t2), Hint().Weight(1));
        auto t3 = std::make_unique<ui::StatTile>();
        m_tilePending = t3.get();
        statStrip->AddChild(std::move(t3), Hint().Weight(1));
        auto t4 = std::make_unique<ui::StatTile>();
        m_tileUsers = t4.get();
        statStrip->AddChild(std::move(t4), Hint().Weight(1));
        auto t5 = std::make_unique<ui::StatTile>();
        m_tileAudit = t5.get();
        statStrip->AddChild(std::move(t5), Hint().Weight(1));
    }
    AddChild(std::move(statStrip),
             Hint().Fixed(kStatStripH).Margin(kSideMargin, 0, kSideMargin, 18));

    // ---- 筛选行：搜索框 + 状态分段 + 更多筛选 / 刷新 ----
    auto filterRow = std::make_unique<DuiHBox>();
    filterRow->SetGap(kFilterGap);

    auto search = std::make_unique<DuiEdit>();
    search->SetCtrlId(kIdEntSearch);
    search->SetPlaceholder(_T("搜索企业名称、行业…"));
    search->SetBgColor(theme::kInk25);
    search->SetShowBorder(true);
    // 左侧放大镜图标。
    search->SetIcon(DuiEdit::LeftIcon, 30,
        [](HDC hdc, const RECT& rcIcon)
        {
            const int kGlyph = 16;
            const int cx = (rcIcon.left + rcIcon.right) / 2;
            const int cy = (rcIcon.top + rcIcon.bottom) / 2;
            RECT rc = { cx - kGlyph / 2, cy - kGlyph / 2,
                        cx + kGlyph / 2, cy + kGlyph / 2 };
            ui::DrawIcon(hdc, ui::Icon::Search, rc, theme::kInk400);
        });
    m_search = search.get();
    filterRow->AddChild(std::move(search), Hint().Fixed(300));

    auto seg = std::make_unique<ui::SegControl>();
    seg->SetCtrlId(kIdEntSeg);
    seg->AddSegment(_T("全部"));
    seg->AddSegment(_T("活跃"));
    seg->AddSegment(_T("待审核"));
    seg->AddSegment(_T("已停用"));
    m_seg = seg.get();
    filterRow->AddChild(std::move(seg), Hint().Fixed(264));

    filterRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));  // 弹性空白

    auto btnMore = std::make_unique<DuiButton>();
    btnMore->SetCtrlId(kIdEntMore);
    btnMore->SetText(_T("更多筛选"));
    btnMore->SetVariant(DuiButton::Variant::Ghost);
    btnMore->SetTextPointSize(theme::kFontSizeNormal);
    filterRow->AddChild(std::move(btnMore), Hint().Fixed(88));

    auto btnRefresh = std::make_unique<DuiButton>();
    btnRefresh->SetCtrlId(kIdEntRefresh);
    btnRefresh->SetText(_T("刷新"));
    btnRefresh->SetVariant(DuiButton::Variant::Default);
    btnRefresh->SetTextPointSize(theme::kFontSizeNormal);
    filterRow->AddChild(std::move(btnRefresh), Hint().Fixed(64));

    AddChild(std::move(filterRow),
             Hint().Fixed(kFilterRowH).Margin(kSideMargin, 0, kSideMargin, 14));

    // ---- 表格卡:表头 + 虚拟列表 + 表尾 ----
    // DuiVBox + 卡片样式取代历史 ui::CardBox(已删除)。
    auto card = std::make_unique<DuiVBox>();
    card->SetBgColor    (theme::kInk0);
    card->SetCornerRadius(theme::kRadiusLg);
    card->SetBorderColor(theme::kInk100);
    card->SetBorderWidth(1.0f);

    card->AddChild(std::make_unique<TableHeader>(), Hint().Fixed(kHeaderRowH));

    // 行操作菜单走「操作」列三个点的左键点击：由 AdminFrame 在
    // WM_LBUTTONDOWN 里命中检测后弹出，故此处不注册左键行点击回调
    //（左键仅由基类做行选中高亮）。给表格设 kIdEntTableRows：双击行时
    // DuiVirtualList 会以该 ctrlId 上报 DUIN_DBLCLK，AdminFrame 据此进详情。
    auto table = std::make_unique<EnterpriseTable>();
    table->SetCtrlId(kIdEntTableRows);
    table->SetRowHeight(kRowHeight);
    table->SetPaintRowCallback(&EnterprisesScreen::PaintRowThunk, this);
    m_table = table.get();
    card->AddChild(std::move(table), Hint().Weight(1));

    auto footer = std::make_unique<TableFooter>();
    footer->SetOwner(this);
    m_footer = footer.get();
    card->AddChild(std::move(footer), Hint().Fixed(kFooterH));

    AddChild(std::move(card),
             Hint().Weight(1).Margin(kSideMargin, 0, kSideMargin, kSideMargin));

    // 注意：此处不拉取数据 —— 主窗口句柄要等 AdminFrame 调 SetHostWindow 后
    // 才可用，首次拉取由 AdminFrame 切到本界面时调 Refresh() 触发。
}

void EnterprisesScreen::Refresh()
{
    StartLoad();
}

void EnterprisesScreen::StartLoad()
{
    // 尚未注入主窗口句柄（结果无处回传），或已有请求在途，直接返回。
    if (m_hostWnd == nullptr || m_loadState == LoadState::Loading)
    {
        return;
    }

    m_loadState = LoadState::Loading;
    UpdateTableState();

    const ServerConfig& server = AppConfig::Instance().Server();
    const CString       token  = AdminSession::Instance().Token();

    // 搜索词与状态分段作为请求参数下推服务端。
    CString keyword = (m_search != nullptr) ? m_search->GetText() : CString();
    keyword.Trim();
    const int seg = (m_seg != nullptr) ? m_seg->GetActiveIndex() : 0;

    // 企业列表：服务端分页 + 服务端筛选。
    HttpRequest request = adminapi::MakeEnterpriseListRequest(
        server, token, keyword, SegmentToServerStatus(seg), m_page, kPageSize);
    request.m_replyHwnd = m_hostWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagEntList;
    HttpWorker::Instance().Post(request);

    // 5 个统计卡：dashboard/stats.do（取全局总数，与列表分页无关）。
    HttpRequest statsReq = adminapi::MakeDashboardStatsRequest(server, token);
    statsReq.m_replyHwnd = m_hostWnd;
    statsReq.m_replyMsg  = WM_APP_API_RESULT;
    statsReq.m_tag       = kApiTagEntStats;
    HttpWorker::Instance().Post(statsReq);

    FA_LOGI(_T("企业管理：发起列表拉取（第 %d 页）"), m_page);
}

void EnterprisesScreen::OnApiResult(ApiTag tag, const HttpResult& http)
{
    // 本屏只负责企业列表与统计卡（读）。企业写操作（create / setstatus /
    // delete / export）的结果由 AdminFrame 直接处理，不路由到这里。
    if (tag == kApiTagEntList)
    {
        OnListResult(http);
    }
    else if (tag == kApiTagEntStats)
    {
        OnStatsResult(http);
    }
}

void EnterprisesScreen::OnListResult(const HttpResult& http)
{
    const EnterpriseListResult result = adminapi::ParseEnterpriseListResponse(http);
    if (result.Succeeded())
    {
        m_enterprises = result.m_list;
        m_totalCount  = result.m_total;
        // 服务端一般会给出 logo 颜色；若某条缺失（m_logoColor 为默认黑），
        // 兜底按企业名算一个稳定的头像底色。
        for (size_t i = 0; i < m_enterprises.size(); ++i)
        {
            if (m_enterprises[i].m_logoColor == RGB(0, 0, 0))
            {
                m_enterprises[i].m_logoColor =
                    ui::AvatarColor(m_enterprises[i].m_name);
            }
        }
        m_loadState = LoadState::Loaded;
        FA_LOGI(_T("企业管理：第 %d 页 %d 家（共 %d 家）"), m_page,
                static_cast<int>(m_enterprises.size()), m_totalCount);
    }
    else
    {
        m_enterprises.clear();
        m_totalCount = 0;
        m_loadState  = LoadState::Failed;
        m_errorText  = result.m_message;
        FA_LOGW(_T("企业管理：列表加载失败：%s"), m_errorText.GetString());
    }

    if (m_table != nullptr)
    {
        m_table->SetRowCount(static_cast<int>(m_enterprises.size()));
        m_table->Invalidate();
    }
    if (m_footer != nullptr)
    {
        static_cast<TableFooter*>(m_footer)->SetPageInfo(
            m_page, m_totalCount, kPageSize);
    }
    UpdateTableState();
}

void EnterprisesScreen::UpdateTableState()
{
    CString overlay;
    switch (m_loadState)
    {
    case LoadState::Loading:
        overlay = _T("正在加载企业数据…");
        break;
    case LoadState::Failed:
        overlay = _T("加载失败：") + m_errorText;
        break;
    case LoadState::Loaded:
        if (m_enterprises.empty())
        {
            // 区分「整个平台无企业」与「当前筛选 / 翻页无匹配」。
            const bool filtering =
                (m_seg != nullptr && m_seg->GetActiveIndex() != 0)
                || (m_search != nullptr && !m_search->GetText().IsEmpty());
            overlay = filtering ? CString(_T("没有匹配的企业"))
                                : CString(_T("暂无企业"));
        }
        break;
    case LoadState::Idle:
    default:
        break;
    }
    if (m_table != nullptr)
    {
        static_cast<EnterpriseTable*>(m_table)->SetOverlayText(overlay);
    }
}

void EnterprisesScreen::OnStatsResult(const HttpResult& http)
{
    const DashboardStatsResult r = adminapi::ParseDashboardStatsResponse(http);
    if (!r.Succeeded())
    {
        // 统计卡加载失败不影响列表展示，仅记日志。
        FA_LOGW(_T("企业管理：统计卡加载失败：%s"), r.m_message.GetString());
        return;
    }
    CString value;
    if (m_tileTotal != nullptr)
    {
        value.Format(_T("%d"), r.m_enterprisesTotal);
        m_tileTotal->SetContent(_T("企业总数"), value, _T("入驻租户"),
                                ui::Accent::None);
    }
    if (m_tileActive != nullptr)
    {
        value.Format(_T("%d"), r.m_enterprisesActive);
        const int pct = (r.m_enterprisesTotal > 0)
            ? r.m_enterprisesActive * 100 / r.m_enterprisesTotal : 0;
        CString sub;
        sub.Format(_T("%d%% 活跃占比"), pct);
        m_tileActive->SetContent(_T("活跃企业"), value, sub, ui::Accent::Ok);
    }
    if (m_tilePending != nullptr)
    {
        value.Format(_T("%d"), r.m_enterprisesPending);
        m_tilePending->SetContent(_T("待审核"), value, _T("需平台审核"),
                                  ui::Accent::Warn);
    }
    if (m_tileUsers != nullptr)
    {
        m_tileUsers->SetContent(_T("总用户数"), FormatThousands(r.m_usersTotal),
                                _T("全平台员工"), ui::Accent::None);
    }
    if (m_tileAudit != nullptr)
    {
        value.Format(_T("%d"), r.m_auditToday);
        m_tileAudit->SetContent(_T("今日审计事件"), value, _T("今日"),
                                ui::Accent::Info);
    }
}

void EnterprisesScreen::OnSearchChanged()
{
    // 搜索词变化 → 回到第 1 页、按新条件重新拉取。
    m_page = 1;
    StartLoad();
}

void EnterprisesScreen::OnSegmentChanged(int /*segIndex*/)
{
    // 状态分段切换 → 回到第 1 页、按新条件重新拉取。
    m_page = 1;
    StartLoad();
}

void EnterprisesScreen::TurnPage(int delta)
{
    const int n = (m_totalCount + kPageSize - 1) / kPageSize;
    const int pageCount = (n < 1) ? 1 : n;
    const int target = m_page + delta;
    if (target < 1 || target > pageCount)
    {
        return;
    }
    m_page = target;
    StartLoad();
}

void EnterprisesScreen::PaintRowThunk(void* user, HDC hdc, int rowIndex,
                                      const RECT& rowRect, bool /*selected*/,
                                      bool hover)
{
    static_cast<EnterprisesScreen*>(user)->PaintRow(hdc, rowIndex, rowRect, hover);
}

void EnterprisesScreen::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect,
                                 bool hover)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_enterprises.size()))
    {
        return;
    }
    const Enterprise& e = m_enterprises[rowIndex];

    // 行背景：hover 时浅品牌色底 + 左侧强调条。
    ui::FillSolidRect(hdc, rowRect, hover ? theme::kFlamingo50 : theme::kInk0);
    if (hover)
    {
        RECT bar = { rowRect.left, rowRect.top,
                     rowRect.left + kAccentBarW, rowRect.bottom };
        ui::FillSolidRect(hdc, bar, theme::kFlamingo500);
    }
    // 行底分隔线。
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1,
                 rowRect.right - rowRect.left, 1, theme::kInk100);

    int xs[8];
    ColumnLefts(rowRect.left, xs);
    const int cy = (rowRect.top + rowRect.bottom) / 2;

    // ---- 列 0：企业（头像 + 名称 + ID）----
    RECT rcAvatar = { xs[0] + kCellPad, cy - kRowAvatar / 2,
                      xs[0] + kCellPad + kRowAvatar, cy + kRowAvatar / 2 };
    ui::DrawAvatar(hdc, rcAvatar, e.m_logoColor, ui::Initials(e.m_shortName));
    const int nameLeft = xs[0] + kCellPad + kRowAvatar + 12;
    RECT rcName = { nameLeft, rowRect.top + 12, xs[1] - kCellPad, rowRect.top + 32 };
    ui::DrawTextLine(hdc, e.m_name, rcName, theme::kInk700,
                     ui::UiFont(theme::kFontSizeNormal, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                     | DT_END_ELLIPSIS);
    RECT rcId = { nameLeft, rowRect.top + 32, xs[1] - kCellPad, rowRect.top + 50 };
    ui::DrawTextLine(hdc, e.m_id, rcId, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 列 1：行业 · 地区 ----
    RECT rcInd = { xs[1] + kCellPad, rowRect.top + 12,
                   xs[2] - kCellPad, rowRect.top + 32 };
    ui::DrawTextLine(hdc, e.m_industry, rcInd, theme::kInk700,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                     | DT_END_ELLIPSIS);
    RECT rcRegion = { xs[1] + kCellPad, rowRect.top + 32,
                      xs[2] - kCellPad, rowRect.top + 50 };
    ui::DrawTextLine(hdc, e.m_region, rcRegion, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 列 2：规模（人数 + 部门数，右对齐）----
    RECT rcMembers = { xs[2] + kCellPad, rowRect.top + 12,
                       xs[3] - kCellPad, rowRect.top + 32 };
    ui::DrawTextLine(hdc, FormatThousands(e.m_members), rcMembers, theme::kInk700,
                     ui::UiFont(theme::kFontSizeNormal, true),
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    CString depts;
    depts.Format(_T("%d 部门"), e.m_depts);
    RECT rcDepts = { xs[2] + kCellPad, rowRect.top + 32,
                     xs[3] - kCellPad, rowRect.top + 50 };
    ui::DrawTextLine(hdc, depts, rcDepts, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 列 3：套餐徽章 ----
    {
        const int chipW = ui::ChipWidth(hdc, e.m_plan, false);
        RECT rcChip = { xs[3] + kCellPad, cy - 10,
                        xs[3] + kCellPad + chipW, cy + 10 };
        ui::DrawChip(hdc, rcChip, e.m_plan, theme::kFlamingo700,
                     theme::kFlamingo50, false, 0);
    }

    // ---- 列 4：主管理员（头像 + 姓名 + 邮箱）----
    RECT rcAdminAva = { xs[4] + kCellPad, cy - kAdminAvatar / 2,
                        xs[4] + kCellPad + kAdminAvatar, cy + kAdminAvatar / 2 };
    ui::DrawAvatar(hdc, rcAdminAva, ui::AvatarColor(e.m_admin),
                   ui::Initials(e.m_admin));
    const int adminTextLeft = xs[4] + kCellPad + kAdminAvatar + 8;
    RECT rcAdminName = { adminTextLeft, rowRect.top + 12,
                         xs[5] - kCellPad, rowRect.top + 32 };
    ui::DrawTextLine(hdc, e.m_admin, rcAdminName, theme::kInk700,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT rcAdminMail = { adminTextLeft, rowRect.top + 32,
                         xs[5] - kCellPad, rowRect.top + 50 };
    ui::DrawTextLine(hdc, e.m_contact, rcAdminMail, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                     | DT_END_ELLIPSIS);

    // ---- 列 5：状态徽章 ----
    ui::DrawStatusChip(hdc, xs[5] + kCellPad, cy, e.m_status);

    // ---- 列 6：开通日期 ----
    RECT rcDate = { xs[6] + kCellPad, rowRect.top, xs[7] - kCellPad, rowRect.bottom };
    ui::DrawTextLine(hdc, e.m_createdAt, rcDate, theme::kInk500,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 列 7：操作（横向三点）----
    {
        const int kDot = 3;   // 圆点直径
        const int kGap = 4;   // 圆点间距
        const int totalW = kDot * 3 + kGap * 2;
        int x = (xs[7] + rowRect.right - totalW) / 2;
        for (int d = 0; d < 3; ++d)
        {
            RECT rcDot = { x, cy - kDot / 2, x + kDot, cy + kDot / 2 };
            ui::FillRoundRect(hdc, rcDot, theme::kInk300, kDot / 2);
            x += kDot + kGap;
        }
    }
}

bool EnterprisesScreen::HitTestActionDotsAt(POINT hostClientPt)
{
    if (m_table == nullptr)
    {
        return false;
    }
    const int row = static_cast<EnterpriseTable*>(m_table)
                        ->RowFromActionColumn(hostClientPt);
    if (row < 0 || row >= static_cast<int>(m_enterprises.size()))
    {
        return false;
    }
    // 记下被点击的企业 ID，供 AdminFrame 弹行操作菜单时取用。
    m_clickedEnterpriseId = m_enterprises[row].m_id;
    return true;
}

const Enterprise* EnterprisesScreen::GetClickedEnterprise() const
{
    for (size_t i = 0; i < m_enterprises.size(); ++i)
    {
        if (m_enterprises[i].m_id == m_clickedEnterpriseId)
        {
            return &m_enterprises[i];
        }
    }
    return nullptr;
}

CString EnterprisesScreen::EnterpriseIdByRow(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_enterprises.size()))
    {
        return CString();
    }
    return m_enterprises[rowIndex].m_id;
}

} // namespace flamingoadmin
