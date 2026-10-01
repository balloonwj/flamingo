/**
 *  v2 历史消息本地库——浏览状态 / 分页模型实现，HistoryModel.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "HistoryModel.h"
#include "V2MsgStore.h"

#include <stdint.h>   // INT64_MAX
#include <time.h>     // _mktime64：本地时间的日期换算

namespace balloonhistory {

namespace {

//struct tm 的 tm_year 是从 1900 年起算的年数
const int kTmYearBase = 1900;
//秒换算成毫秒
const int64_t kMsPerSecond = 1000;

} // namespace

HistoryModel::HistoryModel()
    : m_store(NULL)
    , m_total(0)
    , m_countValid(false)
    , m_countQueries(0)
{
}

void HistoryModel::SetStore(CV2MsgStore* store)
{
    m_store = store;
    //换了库，上次的总数不再可信
    m_countValid = false;
}

void HistoryModel::SetConversation(int ctype, int64_t cid)
{
    //换会话时把长消息的展开态一并清掉：那已经是另一批消息了，留着上一个会话的
    //展开记录既没意义，也会让集合无限长大。改筛选 / 翻页则<u>不</u>清（见 ClearExpanded）。
    const bool changed = (m_filter.m_ctype != ctype || m_filter.m_cid != cid);

    m_filter.m_ctype = ctype;
    m_filter.m_cid = cid;
    if (changed)
    {
        ClearExpanded();
    }
    ResetToFirstPage();
}

void HistoryModel::SetTypeFilter(HistoryTypeFilter type)
{
    m_filter.m_type = type;
    ResetToFirstPage();
}

void HistoryModel::SetTimeRange(HistoryTimeRange range)
{
    m_filter.m_range = range;
    ResetToFirstPage();
}

void HistoryModel::SetCustomRange(int64_t minMs, int64_t maxMs)
{
    m_filter.m_range = kHistoryTimeCustom;
    m_filter.m_customMinMs = minMs;
    m_filter.m_customMaxMs = maxMs;
    ResetToFirstPage();
}

void HistoryModel::SetKeyword(const std::string& keyword)
{
    m_filter.m_keyword = keyword;
    ResetToFirstPage();
}

void HistoryModel::SetSenderFilter(int64_t fromid)
{
    m_filter.m_fromid = (fromid > 0) ? fromid : 0;
    ResetToFirstPage();
}

int HistoryModel::PageOfPosition(int64_t nowMs, int64_t timestamp, int64_t msgid)
{
    if (m_store == NULL)
    {
        return 0;
    }
    int64_t before = 0;
    if (!m_store->CountBefore(m_filter, nowMs, timestamp, msgid, before))
    {
        return 0;
    }
    //排在前面的有 before 条，这一条是第 before+1 条
    const int pageSize = (m_filter.m_pageSize > 0) ? m_filter.m_pageSize : 1;
    return (int)(before / pageSize) + 1;
}

bool HistoryModel::ShowContextOf(int64_t nowMs, const HistoryRecord& rec)
{
    ResetSearchFilters();
    m_filter.m_type = kHistoryTypeAll;

    const int page = PageOfPosition(nowMs, rec.m_timestamp, rec.m_msgid);
    if (page > 0)
    {
        m_filter.m_page = page;
        if (Refresh(nowMs))
        {
            for (size_t i = 0; i < m_records.size(); ++i)
            {
                if (m_records[i].m_msgid == rec.m_msgid)
                {
                    return true;
                }
            }
        }
    }
    //没找到（典型是它在覆盖区间以外，全量列表里看不到）：停在第 1 页，不留在一个与它无关的页上
    m_filter.m_page = 1;
    Refresh(nowMs);
    return false;
}

HistoryDateJump HistoryModel::JumpToDate(int64_t nowMs, int64_t dayStartMs, int64_t dayEndMs)
{
    HistoryDateJump result;
    //先保证总数是按当前筛选条件统计的（条件没变时沿用，不会再统计一次）
    if (m_store == NULL || !RefreshPage(nowMs))
    {
        return result;
    }

    //时间晚于那天结束的有 before 条；排在第 before+1 条的就是那天及以前最晚的一条
    int64_t before = 0;
    if (!m_store->CountBefore(m_filter, nowMs, dayEndMs, INT64_MAX, before))
    {
        return result;
    }
    if (before >= m_total)
    {
        //那天及以前一条都没有：翻到末页，那里是最早的消息
        m_filter.m_page = PageCount();
        if (RefreshPage(nowMs))
        {
            result.m_result = kHistoryDateJumpNoneBefore;
        }
        return result;
    }

    const int pageSize = (m_filter.m_pageSize > 0) ? m_filter.m_pageSize : 1;
    m_filter.m_page = (int)(before / pageSize) + 1;
    if (!RefreshPage(nowMs))
    {
        return result;
    }
    const size_t index = (size_t)(before % pageSize);
    if (index >= m_records.size())
    {
        return result;
    }
    const HistoryRecord& target = m_records[index];
    result.m_msgid = target.m_msgid;
    result.m_result = (target.m_timestamp >= dayStartMs) ? kHistoryDateJumpExact : kHistoryDateJumpEarlier;
    return result;
}

bool HistoryModel::QuerySenders(std::vector<int64_t>& out) const
{
    out.clear();
    if (m_store == NULL)
    {
        return false;
    }
    return m_store->QuerySenders(m_filter.m_ctype, m_filter.m_cid, m_filter.m_minMsgid, out);
}

void HistoryModel::ResetSearchFilters()
{
    m_filter.m_keyword.clear();
    m_filter.m_fromid = 0;
    m_filter.m_range = kHistoryTimeAll;
    m_filter.m_customMinMs = 0;
    m_filter.m_customMaxMs = 0;
    ResetToFirstPage();
}

void HistoryModel::SetMinMsgid(int64_t minMsgid)
{
    //不重置页号：理由见头文件。页号越界由下一次 Refresh 按新总数夹取
    m_filter.m_minMsgid = (minMsgid > 0) ? minMsgid : 0;
}

void HistoryModel::SetPageSize(int pageSize)
{
    if (pageSize > 0)
    {
        m_filter.m_pageSize = pageSize;
    }
    ResetToFirstPage();
}

void HistoryModel::GoToPage(int page)
{
    int pages = PageCount();
    if (page < 1)
    {
        page = 1;
    }
    if (page > pages)
    {
        page = pages;
    }
    m_filter.m_page = page;
}

bool HistoryModel::Refresh(int64_t nowMs)
{
    if (m_store == NULL)
    {
        return false;
    }
    m_total = m_store->Count(m_filter, nowMs);
    ++m_countQueries;
    //记下这次统计用的筛选条件，供只换页的 RefreshPage 判断能否沿用这个总数
    m_countedFilter = m_filter;
    m_countValid = true;
    ClampPageToTotal();
    return m_store->QueryPage(m_filter, nowMs, m_records);
}

const HistoryFilter& HistoryModel::Filter() const
{
    return m_filter;
}

bool HistoryModel::RefreshPage(int64_t nowMs)
{
    if (m_store == NULL)
    {
        return false;
    }
    //筛选条件变过（或从没统计过）：总数可能不同，与整页刷新相同
    if (!m_countValid || !SameCountFilter(m_filter, m_countedFilter))
    {
        return Refresh(nowMs);
    }

    //沿用上次统计的总数，只按它夹取页号、取当前页
    ClampPageToTotal();
    return m_store->QueryPage(m_filter, nowMs, m_records);
}

int64_t HistoryModel::CountQueryTimes() const
{
    return m_countQueries;
}

bool HistoryModel::SameCountFilter(const HistoryFilter& a, const HistoryFilter& b)
{
    //页号与每页条数不影响总数，不比较；其余字段任何一项不同，总数都可能不同
    return a.m_ctype == b.m_ctype
        && a.m_cid == b.m_cid
        && a.m_type == b.m_type
        && a.m_range == b.m_range
        && a.m_customMinMs == b.m_customMinMs
        && a.m_customMaxMs == b.m_customMaxMs
        && a.m_keyword == b.m_keyword
        && a.m_fromid == b.m_fromid
        && a.m_minMsgid == b.m_minMsgid;
}

void HistoryModel::ClampPageToTotal()
{
    //按当前总数夹取页号：过滤 / 删除后变少、当前页越界则回弹到末页，避免查出空页。
    const int pages = PageCount();
    if (m_filter.m_page > pages)
    {
        m_filter.m_page = pages;
    }
    if (m_filter.m_page < 1)
    {
        m_filter.m_page = 1;
    }
}

int64_t HistoryModel::Total() const
{
    return m_total;
}

int HistoryModel::PageCount() const
{
    return ComputePageCount(m_total, m_filter.m_pageSize);
}

int HistoryModel::CurrentPage() const
{
    return m_filter.m_page;
}

const std::vector<HistoryRecord>& HistoryModel::Records() const
{
    return m_records;
}

bool HistoryModel::QueryAll(int64_t nowMs, std::vector<HistoryRecord>& out)
{
    out.clear();
    if (m_store == NULL)
    {
        return false;
    }
    //用当前过滤条件、较大页逐页取，直到取空或不足整页；不动 m_filter 的当前页 / 页大小。
    const int kBatch = 500;
    HistoryFilter f = m_filter;
    f.m_page = 1;
    f.m_pageSize = kBatch;
    for (;;)
    {
        std::vector<HistoryRecord> page;
        if (!m_store->QueryPage(f, nowMs, page))
        {
            return false;
        }
        if (page.empty())
        {
            break;
        }
        out.insert(out.end(), page.begin(), page.end());
        if ((int)page.size() < kBatch)
        {
            break;
        }
        f.m_page++;
    }
    return true;
}

int HistoryModel::ComputePageCount(int64_t total, int pageSize)
{
    if (pageSize <= 0 || total <= 0)
    {
        return 1;
    }
    //向上取整：(total + pageSize - 1) / pageSize。
    int64_t pages = (total + pageSize - 1) / pageSize;
    return (int)pages;
}

bool HistoryModel::IsExpanded(int64_t msgid) const
{
    if (msgid <= 0)
    {
        //服务端还没给这条分配 msgid（本端刚发出、回执未到）。这类消息不进集合 ——
        //它随时会被回执改成真 msgid，用 0 记会让所有这类消息共享同一个状态。
        return false;
    }
    return (m_expandedMsgids.find(msgid) != m_expandedMsgids.end());
}

void HistoryModel::SetExpanded(int64_t msgid, bool expanded)
{
    if (msgid <= 0)
    {
        return;
    }
    if (expanded)
    {
        m_expandedMsgids.insert(msgid);
    }
    else
    {
        m_expandedMsgids.erase(msgid);
    }
}

bool HistoryModel::ToggleExpanded(int64_t msgid)
{
    if (msgid <= 0)
    {
        return false;
    }
    const bool next = !IsExpanded(msgid);
    SetExpanded(msgid, next);
    return next;
}

void HistoryModel::ClearExpanded()
{
    m_expandedMsgids.clear();
}

void HistoryModel::ResetToFirstPage()
{
    m_filter.m_page = 1;
}

bool ShouldResetHistorySearch(bool windowOpen, bool sameConversation)
{
    //窗口原本没开：新建的搜索框是空的，模型里的旧条件必须一并清掉，界面与筛选才一致。
    //开着但换了会话：上一个会话的关键字与时间范围对新会话没有意义。
    return !windowOpen || !sameConversation;
}

bool LocalDayBoundsMs(int year, int month, int day, int64_t& outStartMs, int64_t& outEndMs)
{
    outStartMs = 0;
    outEndMs = 0;

    struct tm dayTm = {};
    dayTm.tm_year = year - kTmYearBase;
    dayTm.tm_mon = month - 1;
    dayTm.tm_mday = day;
    dayTm.tm_isdst = -1;   //是否夏令时交给系统按当地规则判断
    const __time64_t start = _mktime64(&dayTm);
    if (start == -1)
    {
        return false;
    }
    //_mktime64 会把不存在的日期换算成别的日期（2 月 30 日变成 3 月 2 日），换算后与传入的
    //不一致就说明日期不存在
    if (dayTm.tm_year != year - kTmYearBase || dayTm.tm_mon != month - 1 || dayTm.tm_mday != day)
    {
        return false;
    }

    //次日 0 点：日数加一交给 _mktime64 换算（月末、年末、夏令时切换都由它处理）
    struct tm nextTm = {};
    nextTm.tm_year = year - kTmYearBase;
    nextTm.tm_mon = month - 1;
    nextTm.tm_mday = day + 1;
    nextTm.tm_isdst = -1;
    const __time64_t next = _mktime64(&nextTm);
    if (next == -1)
    {
        return false;
    }
    outStartMs = (int64_t)start * kMsPerSecond;
    outEndMs = (int64_t)next * kMsPerSecond - 1;
    return true;
}

bool ShouldResetListScroll(const HistoryFilter& shown, const HistoryFilter& now)
{
    //显示下界 m_minMsgid 不比较：它随往前拉取由程序改动，不是用户换了一批要看的消息
    return shown.m_page != now.m_page
        || shown.m_pageSize != now.m_pageSize
        || shown.m_ctype != now.m_ctype
        || shown.m_cid != now.m_cid
        || shown.m_type != now.m_type
        || shown.m_range != now.m_range
        || shown.m_customMinMs != now.m_customMinMs
        || shown.m_customMaxMs != now.m_customMaxMs
        || shown.m_keyword != now.m_keyword
        || shown.m_fromid != now.m_fromid;
}

bool ShouldOfferViewContext(const HistoryFilter& filter, int64_t msgid)
{
    if (msgid <= 0)
    {
        return false;
    }
    //在覆盖区间以外（只有 Pin 列表会列出这种消息）：全量列表里看不到它，翻过去也找不到
    if (filter.m_minMsgid > 0 && msgid < filter.m_minMsgid)
    {
        return false;
    }
    return !filter.m_keyword.empty()
        || filter.m_type != kHistoryTypeAll
        || filter.m_range != kHistoryTimeAll
        || filter.m_fromid > 0;
}

} // namespace balloonhistory
