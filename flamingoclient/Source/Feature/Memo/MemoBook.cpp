/**
 *  MemoBook.cpp
 *
 *  客户端备忘簿的实现，用法见 MemoBook.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 AutoReplyPolicy.cpp 同例。
#include "MemoBook.h"
#include <algorithm>

namespace memo {

//一天里最多列出的事件次数：每天、工作日、每周重复在一天里只有一次，留出余量即可
const size_t kMaxEventsPerDay = 4;

//月历一屏里每条备忘最多列出的事件次数：每天重复时一屏 42 天各一次
const size_t kMaxEventsPerRange = 64;

MemoRuleSpec RuleOfItem(const net::MemoItem& item)
{
    MemoRuleSpec rule;
    rule.m_start = item.m_start;
    rule.m_advance = item.m_advance;
    rule.m_repeat = item.m_repeat;
    rule.m_weekdays = item.m_weekdays;
    rule.m_count = item.m_count;
    return rule;
}

//按开始时间、ID 升序
static bool ItemLess(const net::MemoItem& a, const net::MemoItem& b)
{
    if (a.m_start != b.m_start)
        return a.m_start < b.m_start;
    return a.m_memoid < b.m_memoid;
}

//本人能不能看到这条群备忘：被指定、或提醒全体（群主、管理员看到的不靠这条判断，他们自己不需要角标提示）
static bool IsTargetOf(const net::MemoItem& item, int64_t selfId)
{
    if (item.m_targetAll)
        return true;
    return std::find(item.m_targets.begin(), item.m_targets.end(), selfId) != item.m_targets.end();
}

CMemoBook::CMemoBook()
    : m_loaded(false)
    , m_selfId(0)
{
}

void CMemoBook::SetSelfId(int64_t selfId)
{
    m_selfId = selfId;
}

bool CMemoBook::IsRemindedToSelf(const net::MemoItem& item) const
{
    if (item.m_ctype != net::kMemoCtypeGroup)
        return true;
    return IsTargetOf(item, m_selfId);
}

void CMemoBook::SortItems()
{
    std::sort(m_items.begin(), m_items.end(), ItemLess);
}

void CMemoBook::ReplaceAll(const std::vector<net::MemoItem>& items)
{
    m_items = items;
    SortItems();
    m_unseen.clear();
    m_loaded = true;
}

bool CMemoBook::ApplyUpsert(const net::MemoItem& item, int64_t selfId)
{
    bool existed = false;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_memoid == item.m_memoid)
        {
            m_items[i] = item;
            existed = true;
            break;
        }
    }
    if (!existed)
        m_items.push_back(item);
    SortItems();

    if (!existed && item.m_ctype == net::kMemoCtypeGroup && item.m_editor != selfId && IsTargetOf(item, selfId))
        m_unseen[item.m_groupid].insert(item.m_memoid);
    return existed;
}

bool CMemoBook::ApplyDelete(int64_t memoid)
{
    for (std::vector<net::MemoItem>::iterator it = m_items.begin(); it != m_items.end(); ++it)
    {
        if (it->m_memoid != memoid)
            continue;
        const int64_t groupid = it->m_groupid;
        m_items.erase(it);
        std::map<int64_t, std::set<int64_t> >::iterator u = m_unseen.find(groupid);
        if (u != m_unseen.end())
            u->second.erase(memoid);
        return true;
    }
    return false;
}

void CMemoBook::Clear()
{
    m_items.clear();
    m_unseen.clear();
    m_loaded = false;
    m_selfId = 0;
}

bool CMemoBook::IsLoaded() const
{
    return m_loaded;
}

const net::MemoItem* CMemoBook::Find(int64_t memoid) const
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_memoid == memoid)
            return &m_items[i];
    }
    return NULL;
}

const std::vector<net::MemoItem>& CMemoBook::Items() const
{
    return m_items;
}

void CMemoBook::ListGroup(int64_t groupid, std::vector<net::MemoItem>& out) const
{
    out.clear();
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_ctype == net::kMemoCtypeGroup && m_items[i].m_groupid == groupid)
            out.push_back(m_items[i]);
    }
}

//当天列表按事件时间、备忘 ID 升序
static bool DayEntryLess(const MemoDayEntry& a, const MemoDayEntry& b)
{
    if (a.m_event != b.m_event)
        return a.m_event < b.m_event;
    return a.m_memoid < b.m_memoid;
}

void CMemoBook::ListDayEntries(int64_t day, std::vector<MemoDayEntry>& out) const
{
    out.clear();
    const LocalMinute from = day * kMinutesPerDay;
    const LocalMinute to = from + kMinutesPerDay;
    std::vector<MemoOccurrence> occ;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (!IsRemindedToSelf(m_items[i]))
            continue;
        ListEventsInRange(RuleOfItem(m_items[i]), from, to, kMaxEventsPerDay, occ);
        for (size_t k = 0; k < occ.size(); ++k)
        {
            MemoDayEntry entry;
            entry.m_memoid = m_items[i].m_memoid;
            entry.m_event = occ[k].m_event;
            out.push_back(entry);
        }
    }
    std::sort(out.begin(), out.end(), DayEntryLess);
}

void CMemoBook::CollectEventDays(int64_t firstDay, int64_t endDay, std::set<int64_t>& out) const
{
    out.clear();
    const LocalMinute from = firstDay * kMinutesPerDay;
    const LocalMinute to = endDay * kMinutesPerDay;
    std::vector<MemoOccurrence> occ;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (!IsRemindedToSelf(m_items[i]))
            continue;
        ListEventsInRange(RuleOfItem(m_items[i]), from, to, kMaxEventsPerRange, occ);
        for (size_t k = 0; k < occ.size(); ++k)
            out.insert(occ[k].m_event / kMinutesPerDay);
    }
}

void CMemoBook::BuildScheduleInputs(LocalMinute (*toLocalMinute)(int64_t unixMs),
                                    std::vector<MemoScheduleInput>& out) const
{
    out.clear();
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (!IsRemindedToSelf(m_items[i]))
            continue;
        MemoScheduleInput in;
        in.m_memoid = m_items[i].m_memoid;
        in.m_rule = RuleOfItem(m_items[i]);
        in.m_updatedAt = (toLocalMinute != NULL) ? toLocalMinute(m_items[i].m_updateTime) : 0;
        out.push_back(in);
    }
}

int CMemoBook::UnseenCount(int64_t groupid) const
{
    std::map<int64_t, std::set<int64_t> >::const_iterator it = m_unseen.find(groupid);
    if (it == m_unseen.end())
        return 0;
    return static_cast<int>(it->second.size());
}

void CMemoBook::MarkGroupSeen(int64_t groupid)
{
    m_unseen.erase(groupid);
}

} // namespace memo
