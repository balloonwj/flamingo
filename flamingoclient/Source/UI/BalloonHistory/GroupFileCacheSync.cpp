/**
 *  群文件信息缓存的拉齐进度（纯逻辑）实现，说明见 GroupFileCacheSync.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "GroupFileCacheSync.h"

#include <stdlib.h>

namespace balloonhistory {

GroupFileCacheItem GroupFileCacheItemFromEntry(int64_t groupid, const net::GroupFileEntry& entry)
{
    GroupFileCacheItem item;
    item.m_groupid = groupid;
    item.m_entryid = entry.m_entryid;
    item.m_fileid = entry.m_fileid;
    item.m_name = entry.m_name;
    item.m_ext = entry.m_ext.empty() ? FileExtFromName(entry.m_name) : entry.m_ext;
    item.m_size = entry.m_size;
    item.m_md5 = entry.m_md5;
    item.m_filetype = entry.m_filetype;
    item.m_uploaderid = (int64_t)_atoi64(entry.m_uploaderid.c_str());
    item.m_uploaderName = entry.m_uploadername;
    item.m_uploadTime = entry.m_uploadtime;
    return item;
}

GroupFileCacheSync::GroupFileCacheSync()
{
}

int GroupFileCacheSync::Enqueue(const std::vector<int64_t>& groupIds, int64_t nowMs, bool force)
{
    DropTimedOut(nowMs);
    int added = 0;
    for (size_t i = 0; i < groupIds.size(); ++i)
    {
        const int64_t gid = groupIds[i];
        if (gid <= 0 || m_queued.count(gid) != 0 || m_running.count(gid) != 0)
        {
            continue;
        }
        if (!force && IsFresh(gid, nowMs))
        {
            continue;
        }
        m_queue.push_back(gid);
        m_queued.insert(gid);
        ++added;
    }
    return added;
}

bool GroupFileCacheSync::NextRequest(int64_t nowMs, int64_t& outGroupId, int& outOffset)
{
    DropTimedOut(nowMs);
    outGroupId = 0;
    outOffset = 0;
    if (m_queue.empty() || (int)m_running.size() >= kGroupFileSyncMaxInFlight)
    {
        return false;
    }
    const int64_t gid = m_queue.front();
    m_queue.pop_front();
    m_queued.erase(gid);
    Task task;
    task.m_sentMs = nowMs;
    m_running[gid] = task;
    outGroupId = gid;
    return true;
}

void GroupFileCacheSync::MarkSent(int64_t groupid, uint32_t seq, int64_t nowMs)
{
    std::map<int64_t, Task>::iterator it = m_running.find(groupid);
    if (it == m_running.end())
    {
        return;
    }
    //发送失败（没有连接等）：这个群放弃，下次 Enqueue 时重新排队
    if (seq == 0)
    {
        m_running.erase(it);
        return;
    }
    it->second.m_seq = seq;
    it->second.m_sentMs = nowMs;
    m_seqToGroup[seq] = groupid;
}

bool GroupFileCacheSync::OwnsSeq(uint32_t seq) const
{
    return seq != 0 && m_seqToGroup.count(seq) != 0;
}

int GroupFileCacheSync::OnPage(const net::GetGroupFilesResult& result, int64_t nowMs, int& outNextOffset,
                               std::vector<GroupFileCacheItem>& outItems)
{
    outNextOffset = 0;
    outItems.clear();
    std::map<uint32_t, int64_t>::iterator seqIt = m_seqToGroup.find(result.m_seq);
    if (result.m_seq == 0 || seqIt == m_seqToGroup.end())
    {
        return kGroupFileSyncIgnored;
    }
    const int64_t gid = seqIt->second;
    m_seqToGroup.erase(seqIt);
    std::map<int64_t, Task>::iterator it = m_running.find(gid);
    if (it == m_running.end())
    {
        return kGroupFileSyncIgnored;
    }

    //服务端报错：这个群放弃
    if (result.m_code != 0)
    {
        m_running.erase(it);
        return kGroupFileSyncFailed;
    }

    Task& task = it->second;
    for (size_t i = 0; i < result.m_files.size(); ++i)
    {
        task.m_buffer.push_back(GroupFileCacheItemFromEntry(gid, result.m_files[i]));
    }
    //还有后续页；本页一条都没有却说还有，不再往下拉，免得一直请求同一个偏移
    if (result.m_hasmore && !result.m_files.empty())
    {
        task.m_seq = 0;
        outNextOffset = result.m_offset + (int)result.m_files.size();
        return kGroupFileSyncNextPage;
    }

    outItems.swap(task.m_buffer);
    m_running.erase(it);
    m_syncedAt[gid] = nowMs;
    return kGroupFileSyncCompleted;
}

void GroupFileCacheSync::Forget(int64_t groupid)
{
    std::map<int64_t, Task>::iterator it = m_running.find(groupid);
    if (it != m_running.end())
    {
        m_seqToGroup.erase(it->second.m_seq);
        m_running.erase(it);
    }
    if (m_queued.erase(groupid) != 0)
    {
        for (std::deque<int64_t>::iterator q = m_queue.begin(); q != m_queue.end(); ++q)
        {
            if (*q == groupid)
            {
                m_queue.erase(q);
                break;
            }
        }
    }
    m_syncedAt.erase(groupid);
}

void GroupFileCacheSync::Reset()
{
    m_queue.clear();
    m_queued.clear();
    m_running.clear();
    m_seqToGroup.clear();
    m_syncedAt.clear();
}

bool GroupFileCacheSync::Busy() const
{
    return !m_queue.empty() || !m_running.empty();
}

bool GroupFileCacheSync::IsFresh(int64_t groupid, int64_t nowMs) const
{
    std::map<int64_t, int64_t>::const_iterator it = m_syncedAt.find(groupid);
    return it != m_syncedAt.end() && nowMs - it->second < kGroupFileSyncFreshMs;
}

void GroupFileCacheSync::DropTimedOut(int64_t nowMs)
{
    std::map<int64_t, Task>::iterator it = m_running.begin();
    while (it != m_running.end())
    {
        //只作废已经发出的请求；刚取出、还没发出的（m_seq 为 0）由调用方紧接着 MarkSent
        if (it->second.m_seq != 0 && nowMs - it->second.m_sentMs >= kGroupFileSyncTimeoutMs)
        {
            m_seqToGroup.erase(it->second.m_seq);
            it = m_running.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace balloonhistory
