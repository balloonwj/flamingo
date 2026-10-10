/**
 *  聊天记录管理器「文件」「图片」两页的浏览模型实现，说明见 HistoryLibraryModel.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "HistoryLibraryModel.h"

#include "V2MsgStore.h"

namespace balloonhistory {

HistoryLibraryModel::HistoryLibraryModel()
    : m_store(NULL)
    , m_total(0)
    , m_countValid(false)
    , m_countQueries(0)
{
    m_filter.m_kind = kHistoryLibraryFiles;
    m_filter.m_pageSize = kHistoryLibraryModelFilePageSize;
}

void HistoryLibraryModel::SetStore(CV2MsgStore* store)
{
    m_store = store;
    m_countValid = false;
}

void HistoryLibraryModel::SetSelfUserId(int64_t selfUserId)
{
    m_filter.m_selfUserId = selfUserId;
}

void HistoryLibraryModel::SetKind(int kind)
{
    if (kind != kHistoryLibraryFiles && kind != kHistoryLibraryImages && kind != kHistoryLibraryLinks
        && kind != kHistoryLibraryMentions)
    {
        kind = kHistoryLibraryFiles;
    }
    if (m_filter.m_kind == kind)
    {
        return;
    }
    m_filter.m_kind = kind;
    m_filter.m_pageSize = (kind == kHistoryLibraryImages) ? kHistoryLibraryModelImagePageSize
                                                         : kHistoryLibraryModelFilePageSize;
    ResetPage();
}

void HistoryLibraryModel::SetDirection(int direction)
{
    m_filter.m_direction = direction;
    ResetPage();
}

void HistoryLibraryModel::SetRelation(int relation)
{
    if (relation < kHistoryLibraryRelAll || relation > kHistoryLibraryRelMine)
    {
        relation = kHistoryLibraryRelAll;
    }
    m_filter.m_relation = relation;
    ResetPage();
}

void HistoryLibraryModel::SetSyntaxFromIds(const std::vector<int64_t>& ids)
{
    if (m_filter.m_syntaxFromIds == ids)
    {
        return;
    }
    m_filter.m_syntaxFromIds = ids;
    ResetPage();
}

void HistoryLibraryModel::SetSource(int source)
{
    m_filter.m_source = source;
    ResetPage();
}

void HistoryLibraryModel::SetCategory(int category)
{
    m_filter.m_category = category;
    ResetPage();
}

void HistoryLibraryModel::SetTimeRange(HistoryTimeRange range)
{
    m_filter.m_range = range;
    ResetPage();
}

void HistoryLibraryModel::SetKeyword(const std::string& keyword)
{
    m_filter.m_keyword = keyword;
    ResetPage();
}

void HistoryLibraryModel::SetWildcard(bool wildcard)
{
    if (m_filter.m_wildcard == wildcard)
    {
        return;
    }
    m_filter.m_wildcard = wildcard;
    //没有关键字时解释方式不影响结果，停在原来那一页
    if (!m_filter.m_keyword.empty())
    {
        ResetPage();
    }
}

void HistoryLibraryModel::SetConversation(int ctype, int64_t cid)
{
    m_filter.m_ctype = ctype;
    m_filter.m_cid = cid;
    ResetPage();
}

void HistoryLibraryModel::SetCtypeOnly(int ctypeOnly)
{
    m_filter.m_ctypeOnly = ctypeOnly;
    ResetPage();
}

void HistoryLibraryModel::ResetFilters()
{
    m_filter.m_direction = kHistoryLibraryDirAll;
    m_filter.m_source = kHistoryLibrarySourceAll;
    m_filter.m_category = kHistoryFileCatAll;
    m_filter.m_range = kHistoryTimeAll;
    m_filter.m_customMinMs = 0;
    m_filter.m_customMaxMs = 0;
    m_filter.m_keyword.clear();
    m_filter.m_wildcard = false;
    m_filter.m_syntaxFromIds.clear();
    m_filter.m_relation = kHistoryLibraryRelAll;
    m_filter.m_ctype = 1;
    m_filter.m_cid = 0;
    m_filter.m_ctypeOnly = 0;
    ResetPage();
}

void HistoryLibraryModel::GoToPage(int page)
{
    m_filter.m_page = page;
    ClampPage();
}

bool HistoryLibraryModel::Refresh(int64_t nowMs)
{
    if (m_store == NULL)
    {
        return false;
    }
    int64_t total = 0;
    ++m_countQueries;
    if (!m_store->CountLibrary(m_filter, nowMs, total))
    {
        m_countValid = false;
        return false;
    }
    m_total = total;
    m_countedFilter = m_filter;
    m_countValid = true;
    ClampPage();
    return m_store->QueryLibraryPage(m_filter, nowMs, m_items);
}

bool HistoryLibraryModel::RefreshPage(int64_t nowMs)
{
    if (m_store == NULL)
    {
        return false;
    }
    if (!m_countValid || !SameCountFilter(m_filter, m_countedFilter))
    {
        return Refresh(nowMs);
    }
    ClampPage();
    return m_store->QueryLibraryPage(m_filter, nowMs, m_items);
}

bool HistoryLibraryModel::RefreshConvCounts(int64_t nowMs)
{
    if (m_store == NULL)
    {
        m_convCounts.clear();
        return false;
    }
    return m_store->CountLibraryByConversation(m_filter, nowMs, m_convCounts);
}

const HistoryLibraryFilter& HistoryLibraryModel::Filter() const
{
    return m_filter;
}

int64_t HistoryLibraryModel::Total() const
{
    return m_total;
}

int HistoryLibraryModel::PageCount() const
{
    const int pageSize = (m_filter.m_pageSize > 0) ? m_filter.m_pageSize : 1;
    if (m_total <= 0)
    {
        return 1;
    }
    return (int)((m_total + pageSize - 1) / pageSize);
}

int HistoryLibraryModel::CurrentPage() const
{
    return m_filter.m_page;
}

const std::vector<HistoryLibraryItem>& HistoryLibraryModel::Items() const
{
    return m_items;
}

const std::vector<HistoryLibraryConvCount>& HistoryLibraryModel::ConvCounts() const
{
    return m_convCounts;
}

int HistoryLibraryModel::CountOf(int ctype, int64_t cid) const
{
    for (size_t i = 0; i < m_convCounts.size(); ++i)
    {
        if (m_convCounts[i].m_ctype == ctype && m_convCounts[i].m_cid == cid)
        {
            return m_convCounts[i].m_count;
        }
    }
    return 0;
}

int64_t HistoryLibraryModel::CountAll() const
{
    int64_t sum = 0;
    for (size_t i = 0; i < m_convCounts.size(); ++i)
    {
        sum += m_convCounts[i].m_count;
    }
    return sum;
}

int64_t HistoryLibraryModel::CountQueryTimes() const
{
    return m_countQueries;
}

void HistoryLibraryModel::ResetPage()
{
    m_filter.m_page = 1;
}

void HistoryLibraryModel::ClampPage()
{
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

bool HistoryLibraryModel::SameCountFilter(const HistoryLibraryFilter& a, const HistoryLibraryFilter& b)
{
    return a.m_kind == b.m_kind
        && a.m_direction == b.m_direction
        && a.m_source == b.m_source
        && a.m_category == b.m_category
        && a.m_range == b.m_range
        && a.m_customMinMs == b.m_customMinMs
        && a.m_customMaxMs == b.m_customMaxMs
        && a.m_keyword == b.m_keyword
        && a.m_wildcard == b.m_wildcard
        && a.m_syntaxFromIds == b.m_syntaxFromIds
        && a.m_relation == b.m_relation
        && a.m_selfUserId == b.m_selfUserId
        && a.m_ctype == b.m_ctype
        && a.m_cid == b.m_cid
        && a.m_ctypeOnly == b.m_ctypeOnly;
}

} // namespace balloonhistory
