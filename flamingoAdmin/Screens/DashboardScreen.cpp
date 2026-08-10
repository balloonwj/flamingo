/**
 *  flamingoAdmin —— 仪表板界面实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/DashboardScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Mock/DataStore.h"

#include "DuiNotify.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 仪表板布局常量（96 dpi 逻辑像素）----
const int kHeaderH        = 100;  // 页头高度
const int kBodyPad        = 24;   // 内容区左 / 右 / 下内距
const int kBodyGap        = 14;   // 双列之间、卡片之间的间距
const int kStatRowH       = 80;   // 统计卡行高
const int kStatGap        = 12;   // 统计卡之间的间距
const int kChartCardH     = 210;  // 柱状图卡片高度
const int kAnnounceCardH  = 236;  // 最新公告卡片高度

// ---- 卡片通用度量 ----
const int kCardPadH    = 20;   // 卡片左 / 右内距
const int kCardPadTop  = 16;   // 卡片顶部内距
const int kCardTitleH  = 22;   // 卡片标题行高
const int kCardHeaderH = 48;   // 卡片头部总高（标题 + 头部到内容的间隙）
const int kCardTitlePt = 12;   // 卡片标题字号（磅）

// ---- 柱状图 ----
const int kChartBarCount  = 14;  // 柱子数量（近 14 天）
const int kChartBarGap    = 6;   // 柱间距
const int kChartHeight    = 124; // 柱区高度
const int kChartLabelH    = 16;  // 柱下方日期标签行高

// ---- 活跃企业 / 公告 / 审计 列表 ----
const int kEntRowH      = 46;   // 活跃企业行高
const int kEntAvatar    = 32;   // 活跃企业头像边长
const int kAnnEntryH    = 60;   // 公告条目高度
const int kAuditRowH    = 44;   // 审计条目高度
const int kListCount    = 4;    // 活跃企业展示条数
const int kAnnCount     = 3;    // 最新公告展示条数
const int kAuditCount   = 8;    // 近期审计展示条数

// 量算一段文字在指定字体下的宽度（像素）。
int MeasureW(LPCTSTR text, HFONT font)
{
    HDC   dc      = ::GetDC(nullptr);
    HFONT oldFont = static_cast<HFONT>(::SelectObject(dc, font));
    SIZE  sz      = { 0, 0 };
    ::GetTextExtentPoint32(dc, text, lstrlen(text), &sz);
    ::SelectObject(dc, oldFont);
    ::ReleaseDC(nullptr, dc);
    return sz.cx;
}

// 把整数格式化为带千分位的字符串，如 2710 → "2,710"。
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
// CardControl —— 仪表板卡片基类。
//
// 负责画统一的卡片底 + 左上角标题 + 右上角可选「查看全部」链接。链接被点击
// 时按本控件的 ctrlId 发 DUIN_CLICK。子类在 ContentTop() 以下绘制各自内容。
// ===========================================================================
class CardControl : public DuiControl
{
public:
    // 设置卡片标题。
    void SetTitle(LPCTSTR title) { m_title = title; }
    // 设置右上角链接文字（空字符串表示无链接）。
    void SetLink(LPCTSTR link)   { m_link = link; }

    bool OnLButtonUp(POINT pt, UINT /*mkFlags*/) override
    {
        // 点中「查看全部」链接区域 → 上报 DUIN_CLICK，由 AdminFrame 跳转。
        RECT rcLink = LinkRect();
        if (!m_link.IsEmpty() && ::PtInRect(&rcLink, pt))
        {
            NotifyParent(DUIN_CLICK);
        }
        return true;
    }

    bool OnSetCursor(POINT pt) override
    {
        RECT rcLink = LinkRect();
        if (!m_link.IsEmpty() && ::PtInRect(&rcLink, pt))
        {
            ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
            return true;
        }
        return false;
    }

protected:
    // 画卡片底 + 标题 + 链接。
    void PaintCardFrame(HDC hdc)
    {
        ui::DrawCard(hdc, m_rcItem);

        RECT rcTitle =
        {
            m_rcItem.left + kCardPadH,  m_rcItem.top + kCardPadTop,
            m_rcItem.right - kCardPadH, m_rcItem.top + kCardPadTop + kCardTitleH,
        };
        ui::DrawTextLine(hdc, m_title, rcTitle, theme::kInk700,
                         ui::UiFont(kCardTitlePt, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (!m_link.IsEmpty())
        {
            RECT rcLink = LinkRect();
            ui::DrawTextLine(hdc, m_link, rcLink, theme::kFlamingo600,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    // 内容区起始 y 坐标。
    int ContentTop() const { return m_rcItem.top + kCardHeaderH; }

    // 「查看全部」链接的可点击矩形（host 客户区坐标）。
    RECT LinkRect() const
    {
        const int w = MeasureW(m_link, ui::UiFont(theme::kFontSizeNormal, false));
        RECT rc =
        {
            m_rcItem.right - kCardPadH - w, m_rcItem.top + kCardPadTop,
            m_rcItem.right - kCardPadH,     m_rcItem.top + kCardPadTop + kCardTitleH,
        };
        return rc;
    }

private:
    CString m_title;
    CString m_link;
};

// ===========================================================================
// ChartCard —— 「近 14 天消息量」柱状图卡片。
// ===========================================================================
class ChartCard : public CardControl
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        PaintCardFrame(hdc);

        const std::vector<MessageTrendPoint>& trend =
            DataStore::Instance().MessageTrend();
        int barCount = static_cast<int>(trend.size());
        if (barCount > kChartBarCount)
        {
            barCount = kChartBarCount;
        }
        if (barCount == 0)
        {
            // 数据未到 —— 内容区居中提示。
            RECT rc = { m_rcItem.left, ContentTop(), m_rcItem.right,
                        ContentTop() + kChartHeight };
            ui::DrawTextLine(hdc, _T("加载中…"), rc, theme::kInk400,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return;
        }

        // 峰值（取真实最大值）。
        int maxVal = 0;
        for (int i = 0; i < barCount; ++i)
        {
            if (trend[i].m_count > maxVal)
            {
                maxVal = trend[i].m_count;
            }
        }

        // 右上角副标题（灰色，非链接）：真实峰值。
        CString sub;
        sub.Format(_T("峰值 %d 条"), maxVal);
        RECT rcSub =
        {
            m_rcItem.left + kCardPadH,  m_rcItem.top + kCardPadTop,
            m_rcItem.right - kCardPadH, m_rcItem.top + kCardPadTop + kCardTitleH,
        };
        ui::DrawTextLine(hdc, sub, rcSub, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 柱状图区域。
        const int areaL  = m_rcItem.left  + kCardPadH;
        const int areaR  = m_rcItem.right - kCardPadH;
        const int baseY  = ContentTop() + kChartHeight;     // 柱底基线
        const int areaW  = areaR - areaL;
        const int barW   = (areaW - kChartBarGap * (kChartBarCount - 1))
                           / kChartBarCount;

        for (int i = 0; i < barCount; ++i)
        {
            const int x = areaL + i * (barW + kChartBarGap);
            int       h = (maxVal > 0)
                ? trend[i].m_count * kChartHeight / maxVal : 0;
            if (h < 4)
            {
                h = 4;   // 最小可见高度
            }
            const bool today = (i == barCount - 1);
            RECT rcBar = { x, baseY - h, x + barW, baseY };
            ui::FillRoundRect(hdc, rcBar,
                              today ? theme::kFlamingo500 : theme::kFlamingo300,
                              theme::kRadiusSm);

            // 柱下方日期标签（"MM-DD" 取日部分）。
            RECT rcLabel = { x, baseY + 2, x + barW, baseY + 2 + kChartLabelH };
            ui::DrawTextLine(hdc, trend[i].m_date.Mid(3), rcLabel,
                             today ? theme::kFlamingo600 : theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, today),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
};

// ===========================================================================
// ActiveEnterprisesCard —— 「活跃企业」列表卡片。
// ===========================================================================
class ActiveEnterprisesCard : public CardControl
{
public:
    // 双击某行企业 → 上报 DUIN_DBLCLK,extra = 行索引(= DataStore::Enterprises()
    // 下标,与本卡展示顺序一致);AdminFrame 据此打开该企业详情。落在标题 / 链接
    // 区或空白行不处理。本卡 ctrlId 与「查看全部」链接共用 kIdDashViewEnterprises,
    // AdminFrame 以通知码(DUIN_CLICK / DUIN_DBLCLK)区分两种交互。
    bool OnLButtonDblClk(POINT pt, UINT /*mkFlags*/) override
    {
        if (!::PtInRect(&m_rcItem, pt))
        {
            return false;
        }
        const int contentTop = ContentTop();
        if (pt.y < contentTop)
        {
            return false;
        }
        const std::vector<Enterprise>& list = DataStore::Instance().Enterprises();
        const int rows = (static_cast<int>(list.size()) < kListCount)
            ? static_cast<int>(list.size()) : kListCount;
        const int row = (pt.y - contentTop) / kEntRowH;
        if (row < 0 || row >= rows)
        {
            return false;
        }
        NotifyParent(DUIN_DBLCLK, static_cast<LPARAM>(row));
        return true;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        PaintCardFrame(hdc);

        const std::vector<Enterprise>& list = DataStore::Instance().Enterprises();
        const int rows = (static_cast<int>(list.size()) < kListCount)
            ? static_cast<int>(list.size()) : kListCount;

        for (int i = 0; i < rows; ++i)
        {
            const Enterprise& e   = list[i];
            const int         top = ContentTop() + i * kEntRowH;
            const int         cy  = top + kEntRowH / 2;
            const int         left = m_rcItem.left + kCardPadH;

            // 头像。
            RECT rcAvatar =
            {
                left, cy - kEntAvatar / 2,
                left + kEntAvatar, cy + kEntAvatar / 2,
            };
            ui::DrawAvatar(hdc, rcAvatar, e.m_logoColor, ui::Initials(e.m_shortName));

            // 状态徽章（右对齐）。
            const int chipW = ui::StatusChipWidth(hdc, e.m_status);
            ui::DrawStatusChip(hdc, m_rcItem.right - kCardPadH - chipW, cy,
                               e.m_status);

            // 人数（在徽章左侧，右对齐）。
            const int membersRight = m_rcItem.right - kCardPadH - chipW - 12;
            CString members;
            members.Format(_T("%s 人"), static_cast<LPCTSTR>(FormatThousands(e.m_members)));
            RECT rcMembers = { left, cy - 8, membersRight, cy + 8 };
            ui::DrawTextLine(hdc, members, rcMembers, theme::kInk500,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // 企业名 + 行业·地区两行。
            const int textLeft = left + kEntAvatar + 12;
            RECT rcName = { textLeft, top + 6, membersRight - 12, top + 24 };
            ui::DrawTextLine(hdc, e.m_name, rcName, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            CString meta;
            meta.Format(_T("%s · %s"),
                        static_cast<LPCTSTR>(e.m_industry),
                        static_cast<LPCTSTR>(e.m_region));
            RECT rcMeta = { textLeft, top + 24, membersRight - 12, top + 40 };
            ui::DrawTextLine(hdc, meta, rcMeta, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
};

// ===========================================================================
// LatestAnnouncementsCard —— 「最新公告」卡片。
// ===========================================================================
class LatestAnnouncementsCard : public CardControl
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        PaintCardFrame(hdc);

        const std::vector<Announcement>& list =
            DataStore::Instance().Announcements();
        const int rows = (static_cast<int>(list.size()) < kAnnCount)
            ? static_cast<int>(list.size()) : kAnnCount;
        const int left  = m_rcItem.left  + kCardPadH;
        const int right = m_rcItem.right - kCardPadH;

        for (int i = 0; i < rows; ++i)
        {
            const Announcement& a   = list[i];
            const int           top = ContentTop() + i * kAnnEntryH;

            // 顶部分隔线。
            ui::FillLine(hdc, left, top, right - left, 1, theme::kInk100);

            // 级别圆点 + 日期。
            ui::DrawSeverityDot(hdc, left + 4, top + 14, a.m_severity);
            RECT rcDate = { left + 14, top + 6, right, top + 22 };
            ui::DrawTextLine(hdc, a.m_sentAt.Mid(5, 11), rcDate, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // 标题（最多两行）。
            RECT rcTitle = { left, top + 24, right, top + kAnnEntryH - 4 };
            ui::DrawTextLine(hdc, a.m_title, rcTitle, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX
                             | DT_END_ELLIPSIS);
        }
    }
};

// ===========================================================================
// RecentAuditCard —— 「近期审计」卡片。
// ===========================================================================
class RecentAuditCard : public CardControl
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        if (!m_bVisible)
        {
            return;
        }
        PaintCardFrame(hdc);

        const std::vector<AuditLog>& list = DataStore::Instance().AuditLogs();
        const int left  = m_rcItem.left  + kCardPadH;
        const int right = m_rcItem.right - kCardPadH;

        // 在内容区高度允许的范围内尽量多画。
        int maxRows = (m_rcItem.bottom - 12 - ContentTop()) / kAuditRowH;
        if (maxRows > kAuditCount)
        {
            maxRows = kAuditCount;
        }
        const int rows = (static_cast<int>(list.size()) < maxRows)
            ? static_cast<int>(list.size()) : maxRows;

        for (int i = 0; i < rows; ++i)
        {
            const AuditLog& l   = list[i];
            const int       top = ContentTop() + i * kAuditRowH;

            ui::FillLine(hdc, left, top, right - left, 1, theme::kInk100);
            ui::DrawSeverityDot(hdc, left + 4, top + 14, l.m_severity);

            // 详情（单行省略）。
            RECT rcDetail = { left + 16, top + 5, right, top + 23 };
            ui::DrawTextLine(hdc, l.m_detail, rcDetail, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                             | DT_END_ELLIPSIS);

            // 时间 · 操作者。
            CString meta;
            meta.Format(_T("%s · %s"),
                        static_cast<LPCTSTR>(l.m_ts.Mid(5, 11)),
                        static_cast<LPCTSTR>(l.m_actor));
            RECT rcMeta = { left + 16, top + 23, right, top + 39 };
            ui::DrawTextLine(hdc, meta, rcMeta, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                             | DT_END_ELLIPSIS);
        }
    }
};

} // anonymous namespace

// ===========================================================================
// DashboardScreen
// ===========================================================================
DashboardScreen::DashboardScreen()
{
    // ---- 页头 ----
    auto header = std::make_unique<ui::PageHeader>();
    header->SetTexts(_T("平台总览"), _T("晚上好，李正阳"),
                     _T("平台运行状态、近期活动与待处理事项"));
    AddChild(std::move(header), Hint().Fixed(kHeaderH));

    // ---- 内容区：双列网格 ----
    auto body = std::make_unique<DuiHBox>();
    body->SetPadding(kBodyPad, 0, kBodyPad, kBodyPad);
    body->SetGap(kBodyGap);

    // 左列：统计卡行 + 柱状图 + 活跃企业。
    auto leftCol = std::make_unique<DuiVBox>();
    leftCol->SetGap(kBodyGap);

    auto statRow = std::make_unique<DuiHBox>();
    statRow->SetGap(kStatGap);
    {
        auto t1 = std::make_unique<ui::StatTile>();
        m_tileEnterprises = t1.get();
        statRow->AddChild(std::move(t1), Hint().Weight(1));
        auto t2 = std::make_unique<ui::StatTile>();
        m_tileUsers = t2.get();
        statRow->AddChild(std::move(t2), Hint().Weight(1));
        auto t3 = std::make_unique<ui::StatTile>();
        m_tileAudit = t3.get();
        statRow->AddChild(std::move(t3), Hint().Weight(1));
    }
    leftCol->AddChild(std::move(statRow), Hint().Fixed(kStatRowH));

    auto chart = std::make_unique<ChartCard>();
    chart->SetTitle(_T("近 14 天消息量"));
    leftCol->AddChild(std::move(chart), Hint().Fixed(kChartCardH));

    auto active = std::make_unique<ActiveEnterprisesCard>();
    active->SetTitle(_T("活跃企业"));
    active->SetLink(_T("查看全部 →"));
    active->SetCtrlId(kIdDashViewEnterprises);
    leftCol->AddChild(std::move(active), Hint().Weight(1));

    body->AddChild(std::move(leftCol), Hint().Weight(2));

    // 右列：最新公告 + 近期审计。
    auto rightCol = std::make_unique<DuiVBox>();
    rightCol->SetGap(kBodyGap);

    auto announce = std::make_unique<LatestAnnouncementsCard>();
    announce->SetTitle(_T("最新公告"));
    announce->SetLink(_T("查看全部 →"));
    announce->SetCtrlId(kIdDashViewAnnouncements);
    rightCol->AddChild(std::move(announce), Hint().Fixed(kAnnounceCardH));

    auto audit = std::make_unique<RecentAuditCard>();
    audit->SetTitle(_T("近期审计"));
    audit->SetLink(_T("查看全部 →"));
    audit->SetCtrlId(kIdDashViewAudit);
    rightCol->AddChild(std::move(audit), Hint().Weight(1));

    body->AddChild(std::move(rightCol), Hint().Weight(1));

    AddChild(std::move(body), Hint().Weight(1));

    // 三张统计卡先置「加载中」，待 dashboard/stats.do 应答后填真实数据。
    SetStatsLoading();
}

void DashboardScreen::SetStatsLoading()
{
    if (m_tileEnterprises != nullptr)
    {
        m_tileEnterprises->SetContent(_T("企业总数"), _T("—"),
                                      _T("加载中…"), ui::Accent::None);
    }
    if (m_tileUsers != nullptr)
    {
        m_tileUsers->SetContent(_T("总用户"), _T("—"),
                                _T("加载中…"), ui::Accent::Info);
    }
    if (m_tileAudit != nullptr)
    {
        m_tileAudit->SetContent(_T("今日审计"), _T("—"),
                                _T("加载中…"), ui::Accent::None);
    }
}

void DashboardScreen::SetStats(const DashboardStatsResult& stats)
{
    if (m_tileEnterprises != nullptr)
    {
        CString value;
        value.Format(_T("%d"), stats.m_enterprisesTotal);
        CString sub;
        sub.Format(_T("%d 活跃 · %d 待审核"),
                   stats.m_enterprisesActive, stats.m_enterprisesPending);
        m_tileEnterprises->SetContent(_T("企业总数"), value, sub,
                                      ui::Accent::None);
    }
    if (m_tileUsers != nullptr)
    {
        m_tileUsers->SetContent(_T("总用户"),
                                FormatThousands(stats.m_usersTotal),
                                _T("全平台员工"), ui::Accent::Info);
    }
    if (m_tileAudit != nullptr)
    {
        CString value;
        value.Format(_T("%d"), stats.m_auditToday);
        CString sub;
        sub.Format(_T("%d 条告警"), stats.m_auditTodayWarn);
        // 今日有告警时用警示色，突出异常。
        m_tileAudit->SetContent(_T("今日审计"), value, sub,
                                stats.m_auditTodayWarn > 0 ? ui::Accent::Warn
                                                           : ui::Accent::None);
    }
}

} // namespace flamingoadmin
