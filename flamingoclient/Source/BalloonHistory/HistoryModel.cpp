/**
 *  v2 历史消息本地库——浏览状态 / 分页模型实现，HistoryModel.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "HistoryModel.h"
#include "V2MsgStore.h"

namespace balloonhistory {

HistoryModel::HistoryModel()
    : m_store(NULL)
    , m_total(0)
{
}

void HistoryModel::SetStore(CV2MsgStore* store)
{
    m_store = store;
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
    //按新总数夹取页号：过滤 / 删除后变少、当前页越界则回弹到末页，避免查出空页。
    int pages = PageCount();
    if (m_filter.m_page > pages)
    {
        m_filter.m_page = pages;
    }
    if (m_filter.m_page < 1)
    {
        m_filter.m_page = 1;
    }
    return m_store->QueryPage(m_filter, nowMs, m_records);
}

const HistoryFilter& HistoryModel::Filter() const
{
    return m_filter;
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

} // namespace balloonhistory
