/**
 *  flamingoAdmin —— 审计日志界面 AuditScreen 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/AuditScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"
#include "Mock/DataStore.h"
#include "DuiNotify.h"
#include "Widgets/OverlayScrollbar.h"   // 表格区 / 详情区的悬浮 auto-hide 滚动条子控件
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kHeaderH    = 96;   // 页头高度
const int kSidePad    = 24;   // 内容区左 / 右 / 下统一外边距
const int kStatH      = 70;   // 统计卡行高
const int kFilterH    = 44;   // 筛选行高
const int kDetailW    = 420;  // 右栏事件详情宽度
const int kRowH       = 44;   // 审计表数据行高
const int kTableHdrH  = 38;   // 审计表表头行高
const int kFooterH    = 40;   // 分页页脚条高度

const int kEyebrowPt   = 8;   // 页头小标题字号（磅）
const int kPageTitlePt = 17;  // 页头大标题字号（磅）

const int kStatCount   = 4;   // 统计卡数量

// 每页条数。服务端 audit/list.do 的 pagesize 上限为 500，本界面取较小值
// 以契合「翻页浏览」体验。
const int kAuditPageSize = 20;

// 悬浮滚动条距其所在滚动区右边缘的内缩间距（像素）。
const int kSbInset = 3;

// 表格各列左偏移（相对表格区左边）。
const int kColTime   = 0;
const int kColSev    = 130;
const int kColAction = 166;
const int kColActor  = 296;
const int kColDetail = 446;

const TCHAR* const kSevFilters[]   = { _T("全部"), _T("普通"), _T("警告"), _T("危险") };
const TCHAR* const kActorFilters[] = { _T("全部"), _T("管理员"), _T("系统") };

// 审计级别 → 中文。
LPCTSTR SevText(Severity s)
{
    switch (s)
    {
    case Severity::Warn:
        return _T("警告");
    case Severity::Danger:
        return _T("危险");
    case Severity::Info:
        return _T("信息");
    case Severity::Normal:
    default:
        return _T("普通");
    }
}

// 画一个分段筛选条，返回各段矩形（用于命中）。
void DrawSegmented(HDC hdc, const RECT& box, const TCHAR* const* labels,
                   int count, int active, std::vector<RECT>& outSegs)
{
    outSegs.clear();
    ui::FillRoundRect(hdc, box, theme::kInk50, 7);
    ui::StrokeRoundRect(hdc, box, theme::kInk100, 7, 1.0f);
    const int segW = (box.right - box.left - 4) / count;
    for (int i = 0; i < count; ++i)
    {
        RECT seg = { box.left + 2 + i * segW, box.top + 2,
                     box.left + 2 + (i + 1) * segW, box.bottom - 2 };
        const bool act = (i == active);
        if (act)
        {
            ui::FillRoundRect(hdc, seg, theme::kInk0, 5);
        }
        ui::DrawTextLine(hdc, labels[i], seg,
                         act ? theme::kInk700 : theme::kInk500,
                         ui::UiFont(theme::kFontSizeSmall, act),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        outSegs.push_back(seg);
    }
}

// 画一个分页小按钮（上一页 / 下一页）。禁用态用灰底灰字。
void DrawPagerButton(HDC hdc, const RECT& rc, LPCTSTR text, bool enabled)
{
    ui::FillRoundRect(hdc, rc, enabled ? theme::kInk0 : theme::kInk25, 6);
    ui::StrokeRoundRect(hdc, rc, theme::kInk100, 6, 1.0f);
    ui::DrawTextLine(hdc, text, rc,
                     enabled ? theme::kInk600 : theme::kInk300,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // anonymous namespace

AuditScreen::AuditScreen()
{
    // 行筛选 / 翻页动作经本控件的 ctrlId 上报 AdminFrame，故给本控件设 ctrlId。
    SetCtrlId(kIdAuditScreen);

    auto refreshBtn = std::make_unique<DuiButton>();
    refreshBtn->SetCtrlId(kIdAuditRefresh);
    refreshBtn->SetText(_T("刷新"));
    refreshBtn->SetVariant(DuiButton::Variant::Default);
    refreshBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnRefresh = refreshBtn.get();
    AddChild(std::move(refreshBtn));

    auto exportBtn = std::make_unique<DuiButton>();
    exportBtn->SetCtrlId(kIdAuditExport);
    exportBtn->SetText(_T("导出"));
    exportBtn->SetVariant(DuiButton::Variant::Default);
    exportBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnExport = exportBtn.get();
    AddChild(std::move(exportBtn));

    // 表格区悬浮滚动条：内容溢出时淡入、停止滚动后淡出。Layout 不靠子控件
    // 下标定位（刷新 / 导出按钮均经成员指针摆放），故此处追加 AddChild 安全。
    auto tableSb = std::make_unique<OverlayScrollbar>();
    m_tableSb = tableSb.get();
    m_tableSb->SetOnScroll(&AuditScreen::OnTableSbScroll, this);
    AddChild(std::move(tableSb));

    // 详情区悬浮滚动条。
    auto detailSb = std::make_unique<OverlayScrollbar>();
    m_detailSb = detailSb.get();
    m_detailSb->SetOnScroll(&AuditScreen::OnDetailSbScroll, this);
    AddChild(std::move(detailSb));
}

int AuditScreen::GetPageSize() const
{
    return kAuditPageSize;
}

int AuditScreen::PageCount() const
{
    const int n = (m_totalCount + kAuditPageSize - 1) / kAuditPageSize;
    return n < 1 ? 1 : n;
}

void AuditScreen::SetPageData(int totalCount)
{
    m_totalCount = totalCount;
    // 新页数据已由 AdminFrame 注入 DataStore；选中行重置到新页首行，滚动复位。
    const std::vector<AuditLog>& logs = DataStore::Instance().AuditLogs();
    m_selectedId   = logs.empty() ? -1 : logs.front().m_id;
    m_tableScroll  = 0;
    m_detailScroll = 0;
    Invalidate();
}

void AuditScreen::SetStats(int today, int total, int warn, int danger)
{
    m_statToday   = today;
    m_statTotal   = total;
    m_statWarn    = warn;
    m_statDanger  = danger;
    m_statsLoaded = true;
    Invalidate();
}

void AuditScreen::RequestReload()
{
    // 立即重绘以反映筛选高亮 / 页码变化，再把重新加载意图上报 AdminFrame。
    Invalidate();
    NotifyParent(DUIN_CLICK);
}

void AuditScreen::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 页头按钮。
    const int cy = rcAvail.top + 52;
    RECT rcExp = { rcAvail.right - kSidePad - 64, cy - 17,
                   rcAvail.right - kSidePad,      cy + 17 };
    RECT rcRef = { rcExp.left - 8 - 64, cy - 17, rcExp.left - 8, cy + 17 };
    m_btnRefresh->ForceLayout(rcRef);
    m_btnExport->ForceLayout(rcExp);

    // 主体区 = 页头 + 统计 + 筛选 之下；表格区底部留出分页页脚条。
    const int bodyT = rcAvail.top + kHeaderH + kStatH + 14 + kFilterH;
    const int bodyL = rcAvail.left + kSidePad;
    const int bodyR = rcAvail.right - kSidePad;
    const int bodyB = rcAvail.bottom - kSidePad;
    m_tableRc  = RECT{ bodyL, bodyT, bodyR - kDetailW, bodyB - kFooterH };
    m_footerRc = RECT{ bodyL, bodyB - kFooterH, bodyR - kDetailW, bodyB };
    m_detailRc = RECT{ bodyR - kDetailW, bodyT, bodyR, bodyB };

    // 表格区悬浮滚动条：摆在表格区右内缘的细竖条上，纵向只覆盖数据行区
    // （表头之下），与 OnMouseWheel 夹取滚动量用的可视区高度保持一致。
    m_tableSb->ForceLayout(RECT{
        m_tableRc.right - kSbInset - OverlayScrollbar::kSuggestedWidth,
        m_tableRc.top + kTableHdrH,
        m_tableRc.right - kSbInset,
        m_tableRc.bottom });

    // 详情区悬浮滚动条：摆在详情区右内缘的细竖条上，纵向覆盖整个详情区，
    // 与 OnMouseWheel 夹取详情滚动量用的可视区高度保持一致。
    m_detailSb->ForceLayout(RECT{
        m_detailRc.right - kSbInset - OverlayScrollbar::kSuggestedWidth,
        m_detailRc.top,
        m_detailRc.right - kSbInset,
        m_detailRc.bottom });
}

void AuditScreen::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    m_hits.clear();
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk25);

    const int L = m_rcItem.left;
    const int R = m_rcItem.right;

    // ---- 页头 ----
    ui::FillSolidRect(hdc, RECT{ L, m_rcItem.top, R, m_rcItem.top + kHeaderH },
                      theme::kInk0);
    ui::DrawTextLine(hdc, _T("安全审计"),
                     RECT{ L + kSidePad, m_rcItem.top + 20, L + 300, m_rcItem.top + 36 },
                     theme::kFlamingo600, ui::UiFont(kEyebrowPt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("审计日志"),
                     RECT{ L + kSidePad, m_rcItem.top + 36, L + 400, m_rcItem.top + 68 },
                     theme::kInk700, ui::UiFont(kPageTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("所有管理动作均被完整记录并不可篡改"),
                     RECT{ L + kSidePad, m_rcItem.top + 68, L + 600, m_rcItem.top + 88 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 统计卡（数据来自 audit/stats.do，经 SetStats 注入；未到时显示「—」）----
    struct Stat { LPCTSTR label; int value; COLORREF clr; };
    Stat stats[] =
    {
        { _T("今日事件"), m_statToday,  theme::kColorInfo },
        { _T("累计事件"), m_statTotal,  theme::kInk700 },
        { _T("警告级"),   m_statWarn,   theme::kColorWarn },
        { _T("危险级"),   m_statDanger, theme::kColorDanger },
    };
    const int statTop = m_rcItem.top + kHeaderH;
    const int statW = (R - L - kSidePad * 2 - 36) / kStatCount;
    for (int i = 0; i < kStatCount; ++i)
    {
        const int sx = L + kSidePad + i * (statW + 12);
        RECT card = { sx, statTop, sx + statW, statTop + kStatH };
        ui::DrawCard(hdc, card);
        RECT sl = { sx + 16, statTop + 12, sx + statW - 12, statTop + 30 };
        ui::DrawTextLine(hdc, stats[i].label, sl, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        CString v;
        if (m_statsLoaded)
        {
            v.Format(_T("%d"), stats[i].value);
        }
        else
        {
            v = _T("—");
        }
        RECT sv = { sx + 16, statTop + 30, sx + statW - 12, statTop + kStatH - 8 };
        ui::DrawTextLine(hdc, v, sv, stats[i].clr,
                         ui::UiFont(theme::kFontSizeHeading + 2, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // ---- 筛选行 ----
    const int filterTop = statTop + kStatH + 14;
    const int fcy = filterTop + kFilterH / 2;
    int fx = L + kSidePad;
    // 「级别」。
    ui::DrawTextLine(hdc, _T("级别"), RECT{ fx, fcy - 10, fx + 36, fcy + 10 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    fx += 36;
    {
        RECT box = { fx, fcy - 14, fx + 224, fcy + 14 };
        std::vector<RECT> segs;
        DrawSegmented(hdc, box, kSevFilters, 4, m_sevFilter, segs);
        for (int i = 0; i < static_cast<int>(segs.size()); ++i)
        {
            Hit h;
            h.m_rc = segs[i];
            h.m_kind = 1;
            h.m_arg = i;
            m_hits.push_back(h);
        }
        fx = box.right + 20;
    }
    // 「操作者」。
    ui::DrawTextLine(hdc, _T("操作者"), RECT{ fx, fcy - 10, fx + 52, fcy + 10 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    fx += 52;
    {
        RECT box = { fx, fcy - 14, fx + 180, fcy + 14 };
        std::vector<RECT> segs;
        DrawSegmented(hdc, box, kActorFilters, 3, m_actorFilter, segs);
        for (int i = 0; i < static_cast<int>(segs.size()); ++i)
        {
            Hit h;
            h.m_rc = segs[i];
            h.m_kind = 2;
            h.m_arg = i;
            m_hits.push_back(h);
        }
    }
    // 右侧：当前筛选下的总条数（服务端报告）。
    CString cntText;
    cntText.Format(_T("共 %d 条"), m_totalCount);
    ui::DrawTextLine(hdc, cntText, RECT{ R - kSidePad - 120, fcy - 10,
                                        R - kSidePad, fcy + 10 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 主体卡片（表格 + 页脚 + 详情）----
    RECT body = { m_tableRc.left, m_tableRc.top, m_detailRc.right, m_detailRc.bottom };
    ui::DrawCard(hdc, body);
    ui::FillLine(hdc, m_detailRc.left, body.top, 1,
                 body.bottom - body.top, theme::kInk100);

    {
        int saved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, m_tableRc.left, m_tableRc.top,
                            m_tableRc.right, m_tableRc.bottom);
        PaintTable(hdc, m_tableRc);
        ::RestoreDC(hdc, saved);
    }
    PaintFooter(hdc, m_footerRc);
    {
        int saved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, m_detailRc.left, m_detailRc.top,
                            m_detailRc.right, m_detailRc.bottom);
        PaintDetail(hdc, m_detailRc);
        ::RestoreDC(hdc, saved);
    }

    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

void AuditScreen::PaintTable(HDC hdc, const RECT& rc)
{
    // 表格直接展示 DataStore 里的当前页（服务端分页，已是一页数据）。
    const std::vector<AuditLog>& logs = DataStore::Instance().AuditLogs();

    // 表头（固定）。
    HFONT hf = ui::UiFont(theme::kFontSizeSmall, false);
    struct HdrCol { int x; int r; LPCTSTR text; };
    const HdrCol hdrs[] =
    {
        { kColTime,   kColSev,    _T("时间") },
        { kColSev,    kColAction, _T("级别") },
        { kColAction, kColActor,  _T("操作码") },
        { kColActor,  kColDetail, _T("操作者") },
        { kColDetail, rc.right - rc.left, _T("说明") },
    };
    for (const HdrCol& c : hdrs)
    {
        RECT r = { rc.left + 16 + c.x, rc.top, rc.left + c.r, rc.top + kTableHdrH };
        ui::DrawTextLine(hdc, c.text, r, theme::kInk400, hf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    ui::FillLine(hdc, rc.left, rc.top + kTableHdrH - 1, rc.right - rc.left,
                 1, theme::kInk100);

    // 数据行（页内滚动）。
    const RECT rowsArea = { rc.left, rc.top + kTableHdrH, rc.right, rc.bottom };
    int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rowsArea.left, rowsArea.top,
                        rowsArea.right, rowsArea.bottom);
    m_tableContentH = static_cast<int>(logs.size()) * kRowH;
    // 把内容总高 / 滚动量同步给表格区悬浮滚动条；它作为子控件会在本帧
    // base OnPaint 阶段绘制自身（悬浮于表格行区右缘、auto-hide）。
    m_tableSb->SetMetrics(m_tableContentH, m_tableScroll);

    if (logs.empty())
    {
        // 空页提示。
        ui::DrawTextLine(hdc, _T("没有匹配的审计记录"), rowsArea, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    for (int i = 0; i < static_cast<int>(logs.size()); ++i)
    {
        const AuditLog& l = logs[i];
        const int top = rowsArea.top + i * kRowH - m_tableScroll;
        if (top + kRowH <= rowsArea.top || top >= rowsArea.bottom)
        {
            continue;
        }
        const bool sel = (l.m_id == m_selectedId);
        RECT row = { rc.left, top, rc.right, top + kRowH };
        if (sel)
        {
            ui::FillSolidRect(hdc, row, theme::kFlamingo50);
            ui::FillSolidRect(hdc, RECT{ rc.left, top, rc.left + 3, top + kRowH },
                              theme::kFlamingo500);
        }
        ui::FillLine(hdc, rc.left + 16, row.bottom - 1, rc.right - rc.left - 24,
                     1, theme::kInk100);
        const int cy = top + kRowH / 2;
        HFONT rf = ui::UiFont(theme::kFontSizeSmall, false);
        // 时间。
        ui::DrawTextLine(hdc, l.m_ts.Mid(5),
                         RECT{ rc.left + 16 + kColTime, top, rc.left + kColSev, top + kRowH },
                         theme::kInk500, rf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // 级别圆点。
        ui::DrawSeverityDot(hdc, rc.left + 16 + kColSev + 4, cy, l.m_severity);
        // 操作码（按级别着色的小标签）。
        {
            COLORREF bg = theme::kInk50;
            COLORREF fg = theme::kInk600;
            if (l.m_severity == Severity::Danger)
            {
                bg = RGB(0xFF, 0xF0, 0xEF);
                fg = theme::kColorDanger;
            }
            else if (l.m_severity == Severity::Warn)
            {
                bg = RGB(0xFD, 0xF2, 0xDD);
                fg = RGB(0xA3, 0x6A, 0x0C);
            }
            const int cw = ui::ChipWidth(hdc, l.m_action, false);
            RECT chip = { rc.left + 16 + kColAction, cy - 10,
                          rc.left + 16 + kColAction + cw, cy + 10 };
            ui::DrawChip(hdc, chip, l.m_action, fg, bg, false, 0);
        }
        // 操作者。
        ui::DrawTextLine(hdc, l.m_actor,
                         RECT{ rc.left + 16 + kColActor, top,
                               rc.left + kColDetail - 8, top + kRowH },
                         theme::kInk700, rf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        // 说明。
        ui::DrawTextLine(hdc, l.m_detail,
                         RECT{ rc.left + 16 + kColDetail, top, rc.right - 14, top + kRowH },
                         theme::kInk500, rf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        if (row.bottom > rowsArea.top && row.top < rowsArea.bottom)
        {
            Hit h;
            h.m_rc = row;
            h.m_kind = 0;
            h.m_arg = l.m_id;
            m_hits.push_back(h);
        }
    }
    ::RestoreDC(hdc, saved);
}

void AuditScreen::PaintFooter(HDC hdc, const RECT& rc)
{
    // 顶部分隔线。
    ui::FillLine(hdc, rc.left, rc.top, rc.right - rc.left, 1, theme::kInk100);
    const int cy = (rc.top + rc.bottom) / 2;
    HFONT f = ui::UiFont(theme::kFontSizeSmall, false);

    // 左侧：服务端报告的总条数。
    CString totalText;
    totalText.Format(_T("共 %d 条"), m_totalCount);
    ui::DrawTextLine(hdc, totalText,
                     RECT{ rc.left + 16, cy - 10, rc.left + 200, cy + 10 },
                     theme::kInk400, f,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    const int pageCount = PageCount();

    // 右侧：[上一页]  第 X / 共 Y 页  [下一页]。
    const int btnW = 64;
    const int btnH = 26;
    RECT next = { rc.right - 16 - btnW, cy - btnH / 2,
                  rc.right - 16,        cy + btnH / 2 };
    const bool canNext = (m_page < pageCount);
    DrawPagerButton(hdc, next, _T("下一页"), canNext);
    if (canNext)
    {
        Hit h;
        h.m_rc = next;
        h.m_kind = 4;
        h.m_arg = 0;
        m_hits.push_back(h);
    }

    CString pageText;
    pageText.Format(_T("第 %d / 共 %d 页"), m_page, pageCount);
    RECT pageRc = { next.left - 8 - 140, cy - 10, next.left - 8, cy + 10 };
    ui::DrawTextLine(hdc, pageText, pageRc, theme::kInk600, f,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT prev = { pageRc.left - 8 - btnW, cy - btnH / 2,
                  pageRc.left - 8,        cy + btnH / 2 };
    const bool canPrev = (m_page > 1);
    DrawPagerButton(hdc, prev, _T("上一页"), canPrev);
    if (canPrev)
    {
        Hit h;
        h.m_rc = prev;
        h.m_kind = 3;
        h.m_arg = 0;
        m_hits.push_back(h);
    }
}

void AuditScreen::PaintDetail(HDC hdc, const RECT& rc)
{
    // 找选中日志。
    const std::vector<AuditLog>& logs = DataStore::Instance().AuditLogs();
    const AuditLog* sel = nullptr;
    for (const AuditLog& l : logs)
    {
        if (l.m_id == m_selectedId)
        {
            sel = &l;
            break;
        }
    }
    if (sel == nullptr)
    {
        RECT t = { rc.left, (rc.top + rc.bottom) / 2 - 12, rc.right,
                   (rc.top + rc.bottom) / 2 + 12 };
        ui::DrawTextLine(hdc, _T("选择左侧条目查看详情"), t, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        m_detailContentH = rc.bottom - rc.top;
        // 无选中条目时内容恰好填满可视区，同步给详情区悬浮滚动条（此时不溢出）。
        m_detailSb->SetMetrics(m_detailContentH, m_detailScroll);
        return;
    }

    int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);

    const int left  = rc.left + 22;
    const int right = rc.right - 22;
    const int w     = right - left;
    int y = rc.top + 22 - m_detailScroll;

    // 级别徽章 + 事件号。
    const int cw = ui::ChipWidth(hdc, SevText(sel->m_severity), true);
    RECT chip = { left, y, left + cw, y + 22 };
    ui::DrawChip(hdc, chip, SevText(sel->m_severity), theme::kInk600,
                 theme::kInk50, true, ui::SeverityColor(sel->m_severity));
    CString evt;
    evt.Format(_T("EVT-%06d"), sel->m_id);
    ui::DrawTextLine(hdc, evt, RECT{ chip.right + 10, y, right, y + 22 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += 32;

    // 详情标题。
    RECT dt = { left, y, right, y + 26 };
    ui::DrawTextLine(hdc, sel->m_detail, dt, theme::kInk700,
                     ui::UiFont(theme::kFontSizeHeading, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    y += 28;
    ui::DrawTextLine(hdc, sel->m_action, RECT{ left, y, right, y + 20 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += 28;
    ui::FillLine(hdc, left, y, w, 1, theme::kInk100);
    y += 14;

    // KV 字段。只列 audit/list.do 提供的真实字段。
    struct KV { LPCTSTR label; CString value; };
    KV kvs[] =
    {
        { _T("时间"),    sel->m_ts },
        { _T("操作者"),  sel->m_actor },
        { _T("IP 地址"), sel->m_ip },
        { _T("目标"),    sel->m_target },
    };
    const int kvCount = static_cast<int>(sizeof(kvs) / sizeof(kvs[0]));
    for (int i = 0; i < kvCount; ++i)
    {
        RECT kl = { left, y, left + 90, y + 24 };
        ui::DrawTextLine(hdc, kvs[i].label, kl, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT kv = { left + 90, y, right, y + 24 };
        ui::DrawTextLine(hdc, kvs[i].value, kv, theme::kInk700,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += 26;
    }
    y += 8;

    // 事件载荷（深色面板）。内容全部取自真实字段，仅作 JSON 形式展示。
    ui::DrawTextLine(hdc, _T("事件载荷"), RECT{ left, y, right, y + 18 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeSmall, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += 24;
    struct JLine { LPCTSTR key; CString val; };
    JLine jlines[] =
    {
        { _T("\"event\""),     CString(_T("\"")) + sel->m_action + _T("\",") },
        { _T("\"actor\""),     CString(_T("\"")) + sel->m_actor  + _T("\",") },
        { _T("\"target\""),    CString(_T("\"")) + sel->m_target + _T("\",") },
        { _T("\"ip\""),        CString(_T("\"")) + sel->m_ip     + _T("\",") },
        { _T("\"timestamp\""), CString(_T("\"")) + sel->m_ts     + _T("\"") },
    };
    const int jlineH = 22;
    const int jlineCount = static_cast<int>(sizeof(jlines) / sizeof(jlines[0]));
    const int jboxH  = jlineH * jlineCount + 20;
    RECT jbox = { left, y, right, y + jboxH };
    ui::FillRoundRect(hdc, jbox, RGB(0x10, 0x15, 0x21), theme::kRadiusMd);
    HFONT jf = ui::UiFont(theme::kFontSizeSmall, false);
    for (int i = 0; i < jlineCount; ++i)
    {
        const int jy = y + 10 + i * jlineH;
        RECT kr = { left + 14, jy, left + 110, jy + jlineH };
        ui::DrawTextLine(hdc, jlines[i].key, kr, RGB(0x7D, 0xD3, 0xFC), jf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT vr = { left + 114, jy, right - 14, jy + jlineH };
        ui::DrawTextLine(hdc, jlines[i].val, vr, RGB(0xFC, 0xD3, 0x4D), jf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    y += jboxH + 14;

    m_detailContentH = y - (rc.top - m_detailScroll);
    // 把内容总高 / 滚动量同步给详情区悬浮滚动条；它作为子控件会在本帧
    // base OnPaint 阶段绘制自身（悬浮于详情区右缘、auto-hide）。
    m_detailSb->SetMetrics(m_detailContentH, m_detailScroll);
    ::RestoreDC(hdc, saved);
}

bool AuditScreen::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (const Hit& h : m_hits)
    {
        if (!::PtInRect(&h.m_rc, pt))
        {
            continue;
        }
        if (h.m_kind == 0)            // 选条目（页内操作，不重新请求）
        {
            if (h.m_arg != m_selectedId)
            {
                m_selectedId   = h.m_arg;
                m_detailScroll = 0;
                Invalidate();
            }
        }
        else if (h.m_kind == 1)       // 切级别筛选 → 回到第 1 页、重新拉取
        {
            if (h.m_arg != m_sevFilter)
            {
                m_sevFilter = h.m_arg;
                m_page = 1;
                RequestReload();
            }
        }
        else if (h.m_kind == 2)       // 切操作者筛选 → 回到第 1 页、重新拉取
        {
            if (h.m_arg != m_actorFilter)
            {
                m_actorFilter = h.m_arg;
                m_page = 1;
                RequestReload();
            }
        }
        else if (h.m_kind == 3)       // 上一页
        {
            if (m_page > 1)
            {
                --m_page;
                RequestReload();
            }
        }
        else if (h.m_kind == 4)       // 下一页
        {
            if (m_page < PageCount())
            {
                ++m_page;
                RequestReload();
            }
        }
        return true;
    }
    return true;
}

bool AuditScreen::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    if (::PtInRect(&m_tableRc, pt))
    {
        m_tableScroll -= (zDelta / WHEEL_DELTA) * kRowH * 2;
        const int viewH = m_tableRc.bottom - m_tableRc.top - kTableHdrH;
        int maxS = m_tableContentH - viewH;
        if (maxS < 0)
            maxS = 0;
        if (m_tableScroll < 0)
            m_tableScroll = 0;
        if (m_tableScroll > maxS)
            m_tableScroll = maxS;
        // 同步滚动量给表格区悬浮滚动条并令其淡入（停止滚动后自动淡出）。
        m_tableSb->SetMetrics(m_tableContentH, m_tableScroll);
        m_tableSb->Flash();
        Invalidate();
    }
    else if (::PtInRect(&m_detailRc, pt))
    {
        m_detailScroll -= (zDelta / WHEEL_DELTA) * 60;
        const int viewH = m_detailRc.bottom - m_detailRc.top;
        int maxS = m_detailContentH - viewH;
        if (maxS < 0)
            maxS = 0;
        if (m_detailScroll < 0)
            m_detailScroll = 0;
        if (m_detailScroll > maxS)
            m_detailScroll = maxS;
        // 同步滚动量给详情区悬浮滚动条并令其淡入（停止滚动后自动淡出）。
        m_detailSb->SetMetrics(m_detailContentH, m_detailScroll);
        m_detailSb->Flash();
        Invalidate();
    }
    return true;
}

void AuditScreen::OnTableSbScroll(void* ctx, int newScroll)
{
    // OverlayScrollbar 拖动滑块的回调：把新滚动量写回本界面并重绘。
    AuditScreen* self = static_cast<AuditScreen*>(ctx);
    self->m_tableScroll = newScroll;
    self->Invalidate();
}

void AuditScreen::OnDetailSbScroll(void* ctx, int newScroll)
{
    // OverlayScrollbar 拖动滑块的回调：把新滚动量写回本界面并重绘。
    AuditScreen* self = static_cast<AuditScreen*>(ctx);
    self->m_detailScroll = newScroll;
    self->Invalidate();
}

bool AuditScreen::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
    return true;
}

} // namespace flamingoadmin
