/**
 *  聊天记录页时间轴模式的窗口逻辑，BalloonHistoryWindowTimeline.cpp
 *
 *  CBalloonHistoryWindow 的一部分（同一个类，实现分到本文件，免得 BalloonHistoryWindow.cpp 继续变长）：「列表 | 时间轴」
 *  的切换、导航条的统计与重建、命中位置的统计与上一处 / 下一处跳转、点导航条的月份跳到那个月
 *  （docs/聊天记录管理器方案.md §11.7）。界面控件由 HistoryWindowLayout / HistoryTimelineLayout 建，纯逻辑在
 *  HistoryTimeline，本地库查询在 CV2MsgStore。
 *
 *  时间轴模式下 HistoryModel 的关键字只用来高亮（SetKeywordHighlightOnly），列表照常分页、保留上下文；命中位置
 *  另外按含关键字的条件查出（最多 kTimelineMaxHits 处），跳转时用 HistoryModel::PageOfPosition 换算出所在的页，
 *  再借「查看上下文」「跳到这一天」共用的定位机制（m_locateMsgid）高亮并滚到那一行。
 *
 *  统计（各月条数、各月命中数、命中位置）在 10 万条的会话上要几百毫秒，交给主面板的后台查询线程
 *  （CV2MsgSearchWorker，kV2SearchTimelineStats）执行，结果经 WM_HISTORY_TIMELINE_STATS_DONE 投回；列表照常先显示，
 *  结果到达后再更新导航条、命中条与行上的命中节点。没有主面板的调试入口退回在界面线程上直接统计。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "BalloonHistoryWindow.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView、命中带宽度
#include "../BalloonMain/BalloonMainPanel.h"   // SearchWorker：时间轴统计交给主面板的后台查询线程
#include "HistoryRowFormat.h"       // LocalDayKey
#include "V2MsgSearchWorker.h"      // kV2SearchTimelineStats、V2SearchResponse
#include "HistoryTimelineLayout.h"  // 导航条内容、命中条、模式切换
#include "HistoryWindowLayout.h"    // kIdHistory* 控件编号

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//日期键 = 年*10000+月*100+日
const int64_t kDayKeyYearFactor = 10000;
const int64_t kDayKeyMonthFactor = 100;
//年月键 = 年*100+月
const int64_t kMonthKeyYearFactor = 100;

//星期几的文字，下标与 WeekdayOfDayKey 的返回值一一对应（0 星期日）
const wchar_t* const kWeekdayTexts[] =
{
    TR_NOOP(L"星期日"), TR_NOOP(L"星期一"), TR_NOOP(L"星期二"), TR_NOOP(L"星期三"),
    TR_NOOP(L"星期四"), TR_NOOP(L"星期五"), TR_NOOP(L"星期六"),
};
const int kWeekdayCount = 7;

//取当前 Unix 毫秒时间
int64_t TimelineNowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    //FILETIME 为自 1601 起的 100ns 数；减去到 1970 的偏移后换算成毫秒
    const unsigned long long kEpochDiff100ns = 116444736000000000ULL;
    const unsigned long long k100nsPerMs = 10000ULL;
    return (int64_t)((u.QuadPart - kEpochDiff100ns) / k100nsPerMs);
}

//两组条件统计出的导航数据与命中位置是否必然相同：页号、每页条数以外的字段都相同
bool SameTimelineFilter(const HistoryFilter& a, const HistoryFilter& b)
{
    return a.m_ctype == b.m_ctype
        && a.m_cid == b.m_cid
        && a.m_type == b.m_type
        && a.m_range == b.m_range
        && a.m_customMinMs == b.m_customMinMs
        && a.m_customMaxMs == b.m_customMaxMs
        && a.m_keyword == b.m_keyword
        && a.m_wildcard == b.m_wildcard
        && a.m_fromid == b.m_fromid
        && a.m_syntaxFromIds == b.m_syntaxFromIds
        && a.m_minMsgid == b.m_minMsgid;
}

//日期键拆成某一天的起止（Unix 毫秒）
bool DayKeyBounds(int64_t dayKey, int64_t& startMs, int64_t& endMs)
{
    return LocalDayBoundsMs((int)(dayKey / kDayKeyYearFactor), (int)((dayKey / kDayKeyMonthFactor) % kDayKeyMonthFactor),
                            (int)(dayKey % kDayKeyMonthFactor), startMs, endMs);
}

} // namespace

void CBalloonHistoryWindow::SetTimelineMode(bool timeline)
{
    if (m_timelineMode == timeline)
    {
        return;
    }
    m_timelineMode = timeline;
    //时间轴模式下关键字只高亮、不筛选，列表保留上下文
    m_model.SetKeywordHighlightOnly(timeline);
    ClearLocatedRow();
    m_hits.Clear();
    m_timelineYears.clear();
    m_navExpandedYears.clear();
    m_navCurrentMonthKey = 0;
    m_timelineStatsValid = false;
    //在途的统计结果到达时序号对不上，会被丢弃
    m_timelineStatsSeq = 0;
    m_timelineJumpPending = false;
    ApplyHistoryTimelineMode(BodyRoot(), timeline);
    LOG_INFO("history window: timeline mode %d, keyword bytes=%u.", (int)timeline,
             (unsigned)m_model.Filter().m_keyword.size());
    //列表换成另一种样式（有关键字时还换了一批消息），完整刷新
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
}

void CBalloonHistoryWindow::ResetTimelineState()
{
    m_timelineMode = false;
    m_model.SetKeywordHighlightOnly(false);
    m_hits.Clear();
    m_timelineYears.clear();
    m_navExpandedYears.clear();
    m_navCurrentMonthKey = 0;
    m_timelineStatsValid = false;
    m_timelineStatsSeq = 0;
    m_timelineJumpPending = false;
}

bool CBalloonHistoryWindow::RefreshTimelineStats(bool force)
{
    //与文件 / 图片页相同按 ManagerStore 取库：调试入口没有真实本地库（m_pStore 为空），用窗口预置的假库
    CV2MsgStore* store = ManagerStore();
    if (!m_timelineMode || store == NULL)
    {
        m_hits.Clear();
        m_timelineYears.clear();
        m_timelineStatsValid = false;
        m_timelineStatsSeq = 0;
        m_timelineJumpPending = false;
        return false;
    }

    //含关键字的完整条件（页号不参与比较）
    const HistoryFilter full = m_model.Filter();
    if (!force && m_timelineStatsValid && SameTimelineFilter(full, m_timelineStatsFilter))
    {
        return false;
    }
    const bool keywordChanged = !m_timelineStatsValid
                             || full.m_keyword != m_timelineStatsFilter.m_keyword
                             || full.m_wildcard != m_timelineStatsFilter.m_wildcard;
    //换了会话：上一个会话展开过的年份没有意义
    if (!m_timelineStatsValid || full.m_ctype != m_timelineStatsFilter.m_ctype
        || full.m_cid != m_timelineStatsFilter.m_cid)
    {
        m_navExpandedYears.clear();
    }
    //提交之前就记下这次的条件：结果到达之前的只换页刷新不会再提交一次
    m_timelineStatsFilter = full;
    m_timelineStatsValid = true;

    //各月条数按列表的条件（不含关键字）统计，命中数与命中位置按含关键字的条件统计；命中位置多取一条，
    //据此判断命中是否多于上限
    V2SearchRequest req;
    req.m_kind = kV2SearchTimelineStats;
    req.m_filter = full;
    req.m_nowMs = TimelineNowMs();
    req.m_limit = kTimelineMaxHits + 1;
    req.m_target = m_hWnd;
    req.m_msg = WM_HISTORY_TIMELINE_STATS_DONE;

    //真实会话：交给主面板的后台查询线程，界面线程不等结果
    CV2MsgSearchWorker* worker = (m_pMainPanel != NULL) ? m_pMainPanel->SearchWorker() : NULL;
    if (worker != NULL)
    {
        const uint32_t seq = worker->Submit(req);
        if (seq != 0)
        {
            m_timelineStatsSeq = seq;
            //换了关键字：上一个关键字的命中位置已经作废；结果回来之前命中条显示「正在查找」
            if (keywordChanged)
            {
                m_hits.Clear();
                m_timelineJumpPending = HasKeywordFilter(full);
            }
            LOG_INFO("history timeline: stats submitted, seq %u, keyword bytes %u, keyword changed %d.", seq,
                     (unsigned)full.m_keyword.size(), (int)keywordChanged);
            return false;
        }
        LOG_WARNING("history timeline: stats submit failed, querying on the UI thread.");
    }

    //调试入口（没有主面板与后台线程）或提交失败：在界面线程上直接统计
    m_timelineStatsSeq = 0;
    m_timelineJumpPending = false;
    std::unique_ptr<V2SearchResponse> resp(CV2MsgSearchWorker::Execute(store, req));
    return ApplyTimelineStats(*resp, keywordChanged);
}

bool CBalloonHistoryWindow::ApplyTimelineStats(const V2SearchResponse& resp, bool keywordChanged)
{
    std::vector<HistoryPosition> positions = resp.m_positions;
    const bool truncated = (positions.size() > (size_t)kTimelineMaxHits);
    if (truncated)
    {
        positions.resize((size_t)kTimelineMaxHits);
    }

    //关键字没变（数据变了、或其它筛选条件变了）：当前那一处若还在，继续停在它上面
    int64_t keepMsgid = 0;
    if (!keywordChanged && m_hits.At(m_hits.Current()) != NULL)
    {
        keepMsgid = m_hits.At(m_hits.Current())->m_msgid;
    }
    m_hits.Reset(positions, truncated);
    if (keepMsgid != 0)
    {
        m_hits.SetCurrent(m_hits.IndexOfMsgid(keepMsgid));
    }
    m_timelineYears = BuildTimelineYears(resp.m_monthCounts, resp.m_monthHits);
    LOG_INFO("history timeline: stats seq %u, months=%u, years=%u, hits=%d%s, keyword changed %d, ok %d, "
             "query %.1f ms.", resp.m_seq, (unsigned)resp.m_monthCounts.size(), (unsigned)m_timelineYears.size(),
             m_hits.Count(), truncated ? "+" : "", (int)keywordChanged, (int)resp.m_ok, resp.m_elapsedMs);
    return keywordChanged && HasKeywordFilter(m_model.Filter()) && m_hits.Count() > 0;
}

LRESULT CBalloonHistoryWindow::OnTimelineStatsDoneMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam,
                                                      BOOL& /*bHandled*/)
{
    //结果对象归本窗口释放：先接管，函数返回时自动 delete
    std::unique_ptr<V2SearchResponse> resp(reinterpret_cast<V2SearchResponse*>(lParam));
    const uint32_t seq = (uint32_t)wParam;
    if (resp.get() == NULL || seq == 0 || seq != m_timelineStatsSeq || !m_timelineMode || !IsWindow())
    {
        LOG_INFO("history timeline: stats result seq %u dropped (latest %u).", seq, m_timelineStatsSeq);
        return 0;
    }
    m_timelineStatsSeq = 0;
    const bool jumpPending = m_timelineJumpPending;
    m_timelineJumpPending = false;
    const bool jumpToFirst = ApplyTimelineStats(*resp, jumpPending);
    UpdateHitBar();
    RebuildTimelineNav();
    if (jumpToFirst)
    {
        //换了关键字：跳到第一处命中（另投一次只换页的刷新，列表里命中的节点随之标黄）
        JumpToHit(0);
    }
    else if (HasKeywordFilter(m_model.Filter()) && m_mgrTab == kHistoryManagerTabChat)
    {
        //列表是在统计结果到达之前建的，按只换页刷新一次，让命中的行节点标黄
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
    }
    return 0;
}

void CBalloonHistoryWindow::DecorateTimelineRows(HistoryViewData& data)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    //当天条数：按当前页所跨的日期一次查出（页里最新一条那天的结束到最早一条那天的开始）
    std::vector<HistoryPeriodCount> dayCounts;
    CV2MsgStore* store = ManagerStore();
    if (store != NULL && !recs.empty())
    {
        int64_t fromMs = 0;
        int64_t toMs = 0;
        int64_t unused = 0;
        if (DayKeyBounds(LocalDayKey(recs.back().m_timestamp), fromMs, unused)
            && DayKeyBounds(LocalDayKey(recs.front().m_timestamp), unused, toMs))
        {
            store->CountByLocalDay(m_model.QueryFilter(), TimelineNowMs(), fromMs, toMs, dayCounts);
        }
    }

    const bool hasKeyword = HasKeywordFilter(m_model.Filter());
    for (size_t i = 0; i < data.m_rows.size() && i < recs.size(); ++i)
    {
        HistoryRowView& row = data.m_rows[i];
        row.m_timeline = true;
        //命中：在取到的命中位置里；命中多于上限、或统计结果还没回来时，按界面上找到的高亮处判断
        const bool byHighlight = m_hits.Truncated() || m_timelineStatsSeq != 0;
        row.m_timelineHit = hasKeyword
                         && (m_hits.Contains(recs[i].m_msgid) || (byHighlight && !row.m_highlights.empty()));
        if (row.m_groupLabel.IsEmpty())
        {
            continue;
        }
        //日期大节点：完整日期，右侧写星期几与当天条数
        const int64_t dayKey = LocalDayKey(recs[i].m_timestamp);
        //日期文字只有数字与连字符（ASCII），直接转换
        row.m_groupLabel = CString(FormatDayKey(dayKey).c_str());
        const int weekday = WeekdayOfDayKey(dayKey);
        const CString weekdayText = (weekday >= 0 && weekday < kWeekdayCount) ? TR(kWeekdayTexts[weekday]) : _T("");
        row.m_groupDetail = TRF(L"{0} · {1} 条", (LPCTSTR)weekdayText,
                                (long long)PeriodCountOf(dayCounts, dayKey)).c_str();
    }
}

void CBalloonHistoryWindow::RebuildTimelineNav()
{
    DuiControl* body = BodyRoot();
    DuiScrollView* scroll = (body != NULL) ? dynamic_cast<DuiScrollView*>(body->FindCtrlById(kIdHistoryNavScroll))
                                           : NULL;
    if (scroll == NULL)
    {
        return;
    }
    //当前月份：定位行（跳到的那一处命中或那一天）在本页时取它，否则取本页第一条
    const std::vector<HistoryRecord>& recs = m_model.Records();
    int64_t monthKey = 0;
    if (m_locateMsgid != 0)
    {
        for (size_t i = 0; i < recs.size(); ++i)
        {
            if (recs[i].m_msgid == m_locateMsgid)
            {
                monthKey = LocalMonthKeyOf(recs[i].m_timestamp);
                break;
            }
        }
    }
    if (monthKey == 0 && !recs.empty())
    {
        monthKey = LocalMonthKeyOf(recs.front().m_timestamp);
    }
    //默认展开当前月份所在的那一年；当前月份变了（翻到了别的年份）时把那一年也展开。月份没变时不动，
    //用户收起的年份保持收起
    if (m_navExpandedYears.empty())
    {
        const int year = DefaultExpandedYear(m_timelineYears, monthKey);
        if (year > 0)
        {
            m_navExpandedYears.insert(year);
        }
    }
    else if (monthKey != 0 && monthKey != m_navCurrentMonthKey)
    {
        m_navExpandedYears.insert((int)(monthKey / kMonthKeyYearFactor));
    }
    m_navCurrentMonthKey = monthKey;

    //换内容时 DuiScrollView 保留原来的滚动位置
    scroll->SetContent(BuildTimelineNavContent(m_timelineYears, m_navCurrentMonthKey, m_navExpandedYears));
    scroll->ForceLayout(scroll->GetRect());
    scroll->Invalidate();
}

void CBalloonHistoryWindow::UpdateHitBar()
{
    const bool show = m_timelineMode && HasKeywordFilter(m_model.Filter());
    CString info;
    CString position;
    const int count = m_hits.Count();
    if (show)
    {
        //换了关键字、统计结果还没回来（后台线程正在查）
        if (count == 0 && m_timelineStatsSeq != 0)
        {
            info = TR(L"正在查找命中的消息…");
        }
        else if (count == 0)
        {
            info = TR(L"没有找到命中的消息，时间轴模式下保留上下文、只高亮命中");
        }
        else if (m_hits.Truncated())
        {
            info = TRF(L"命中超过 {0} 处，只在最近的 {0} 处之间跳转", count).c_str();
        }
        else
        {
            info = TRF(L"共 {0} 处命中，时间轴模式下保留上下文、只高亮命中", count).c_str();
        }
        if (m_hits.Current() >= 0)
        {
            position = TRF(L"第 {0} / {1} 处", m_hits.Current() + 1, count).c_str();
        }
    }
    ApplyHistoryHitBar(BodyRoot(), show, info, position, show && count > 0);
}

void CBalloonHistoryWindow::GoToHit(bool next)
{
    if (!m_timelineMode || !HasKeywordFilter(m_model.Filter()))
    {
        return;
    }
    if (m_hits.Count() == 0)
    {
        ShowToast((m_timelineStatsSeq != 0) ? TR(L"正在查找命中的消息，请稍候") : TR(L"没有找到命中的消息"));
        return;
    }
    int index = -1;
    bool wrapped = false;
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (m_hits.Current() < 0 && !recs.empty())
    {
        //还没有当前处：从当前页第一条开始往下找；往上找时取它上面最近的一处
        const int first = m_hits.FirstAtOrAfter(recs.front().m_timestamp, recs.front().m_msgid);
        if (next)
        {
            index = (first >= 0) ? first : 0;
            wrapped = (first < 0);
        }
        else
        {
            index = (first > 0) ? (first - 1) : (m_hits.Count() - 1);
            wrapped = (first == 0);
        }
    }
    else
    {
        index = next ? m_hits.Next(wrapped) : m_hits.Prev(wrapped);
    }
    if (wrapped)
    {
        ShowToast(next ? TR(L"已经是最后一处，从第一处重新开始") : TR(L"已经是第一处，从最后一处重新开始"));
    }
    JumpToHit(index);
}

void CBalloonHistoryWindow::JumpToHit(int index)
{
    const HistoryPosition* hit = m_hits.At(index);
    if (hit == NULL)
    {
        return;
    }
    const HistoryPosition target = *hit;
    m_hits.SetCurrent(index);
    ClearLocatedRow();
    //按列表的条件（时间轴模式下不含关键字）换算这一处在第几页
    const int page = m_model.PageOfPosition(TimelineNowMs(), target.m_timestamp, target.m_msgid);
    if (page > 0)
    {
        m_model.GoToPage(page);
    }
    m_locateMsgid = target.m_msgid;
    m_scrollToLocated = true;
    LOG_INFO("history timeline: jump to hit %d/%d, msgid=%lld, page %d.", index + 1, m_hits.Count(),
             (long long)target.m_msgid, page);
    UpdateHitBar();
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

void CBalloonHistoryWindow::JumpToMonth(int year, int month)
{
    int64_t startMs = 0;
    int64_t endMs = 0;
    if (!LocalMonthBoundsMs(year, month, startMs, endMs))
    {
        return;
    }
    //与「跳到这一天」相同：跳转在全部时间里进行，时间筛选多半不包含那个月
    if (m_model.Filter().m_range != kHistoryTimeAll)
    {
        LOG_INFO("history timeline: month jump clears time range %d.", (int)m_model.Filter().m_range);
        m_model.SetTimeRange(kHistoryTimeAll);
        SyncTimeFilterControls();
    }
    LOG_INFO("history timeline: jump to month %04d-%02d.", year, month);
    ClearLocatedRow();
    ApplyDateJump(startMs, endMs, false);
    //JumpToDate 已按（可能刚改过的）筛选条件统计并取好了页，只换页的刷新不会再统计一次
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

bool CBalloonHistoryWindow::HandleTimelineNavClick(POINT pt)
{
    DuiControl* body = BodyRoot();
    DuiControl* nav = (body != NULL) ? body->FindCtrlById(kIdHistoryNav) : NULL;
    DuiScrollView* scroll = (body != NULL) ? dynamic_cast<DuiScrollView*>(body->FindCtrlById(kIdHistoryNavScroll))
                                           : NULL;
    if (nav == NULL || !nav->IsVisible() || scroll == NULL || scroll->GetContent() == NULL)
    {
        return false;
    }
    //落点须在导航条视口内、且不在右侧滚动条的命中带上（按在滚动条上是要拖动它）
    const RECT view = scroll->GetRect();
    if (pt.x < view.left || pt.x >= view.right - DuiScrollBar::kOverlayBandPx || pt.y < view.top || pt.y >= view.bottom)
    {
        return false;
    }
    const std::vector<std::unique_ptr<DuiControl> >& kids = scroll->GetContent()->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const UINT id = kids[i]->GetCtrlId();
        if (id == 0)
        {
            continue;
        }
        const RECT rc = kids[i]->GetRect();
        if (::PtInRect(&rc, pt))
        {
            ::PostMessage(m_hWnd, WM_HISTORY_TIMELINE_NAV_CLICK, (WPARAM)id, 0);
            return true;
        }
    }
    return false;
}

LRESULT CBalloonHistoryWindow::OnTimelineNavClickMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/,
                                                     BOOL& /*bHandled*/)
{
    if (!m_timelineMode)
    {
        return 0;
    }
    const UINT id = (UINT)wParam;
    int year = 0;
    int month = 0;
    //月份行：跳到那个月
    if (TimelineNavMonthOfId(m_timelineYears, id, year, month))
    {
        JumpToMonth(year, month);
        return 0;
    }
    //年标题行：展开 / 收起
    if (TimelineNavYearOfId(m_timelineYears, id, year))
    {
        if (m_navExpandedYears.count(year) > 0)
        {
            m_navExpandedYears.erase(year);
        }
        else
        {
            m_navExpandedYears.insert(year);
        }
        LOG_INFO("history timeline: year %d %s.", year, (m_navExpandedYears.count(year) > 0) ? "expanded" : "collapsed");
        RebuildTimelineNav();
    }
    return 0;
}
