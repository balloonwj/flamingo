/**
 *  帖子群「正在查看」登记表 ThreadWatchRegistry 的实现，见 ThreadWatchRegistry.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadWatchRegistry.h"

#include <set>

void ThreadWatchRegistry::watch(int64_t groupid, uint64_t conn, int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_groups[groupid][conn] = userId;
}

void ThreadWatchRegistry::unwatch(int64_t groupid, uint64_t conn)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, ConnUserMap>::iterator it = m_groups.find(groupid);
    if (it == m_groups.end())
        return;
    it->second.erase(conn);
    if (it->second.empty())
        m_groups.erase(it);
}

size_t ThreadWatchRegistry::removeConn(uint64_t conn)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    size_t removed = 0;
    for (std::map<int64_t, ConnUserMap>::iterator it = m_groups.begin(); it != m_groups.end();)
    {
        removed += it->second.erase(conn);
        if (it->second.empty())
            m_groups.erase(it++);
        else
            ++it;
    }
    return removed;
}

size_t ThreadWatchRegistry::removeUsers(int64_t groupid, const std::vector<int64_t>& userIds)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, ConnUserMap>::iterator it = m_groups.find(groupid);
    if (it == m_groups.end())
        return 0;
    std::set<int64_t> users(userIds.begin(), userIds.end());
    size_t removed = 0;
    for (ConnUserMap::iterator ci = it->second.begin(); ci != it->second.end();)
    {
        if (users.count(ci->second) > 0)
        {
            it->second.erase(ci++);
            ++removed;
        }
        else
        {
            ++ci;
        }
    }
    if (it->second.empty())
        m_groups.erase(it);
    return removed;
}

void ThreadWatchRegistry::removeGroup(int64_t groupid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_groups.erase(groupid);
}

std::vector<uint64_t> ThreadWatchRegistry::watchers(int64_t groupid) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<uint64_t> out;
    std::map<int64_t, ConnUserMap>::const_iterator it = m_groups.find(groupid);
    if (it == m_groups.end())
        return out;
    for (ConnUserMap::const_iterator ci = it->second.begin(); ci != it->second.end(); ++ci)
        out.push_back(ci->first);
    return out;
}
