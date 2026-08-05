/**
 *  GroupTransferQueue.cpp
 *
 *  群文件并发传输池的待处理队列 + 取消 / 去重簿记实现（纯逻辑、不加锁）。
 *  设计与约定见 GroupTransferQueue.h。
 *
 *  balloonwj@qq.com   2026-06-09
 */

#include "GroupTransferQueue.h"

namespace net {

GroupTransferQueue::GroupTransferQueue()
    : m_nextUploadId(1)   // 上传 id 从 1 起，0 留作「无效 / 下载项」语义
{
}

int GroupTransferQueue::EnqueueUpload(int64_t groupId, const std::wstring& localPath,
                                      int64_t selfUserId)
{
    int uploadId = m_nextUploadId;
    ++m_nextUploadId;

    GroupTransferItem item;
    item.m_isUpload   = true;
    item.m_uploadId   = uploadId;
    item.m_groupId    = groupId;
    item.m_selfUserId = selfUserId;
    item.m_localPath  = localPath;
    m_pending.push_back(item);

    return uploadId;
}

bool GroupTransferQueue::EnqueueDownload(int64_t groupId, const std::string& entryId,
                                         const std::string& fileId, const std::string& filename,
                                         const std::string& expectMd5, int64_t expectSize,
                                         int64_t selfUserId, const std::wstring& destPath)
{
    // 去重：该 entryid 已在「在传下载」集合里则忽略。
    if (m_runningDownloads.find(entryId) != m_runningDownloads.end())
    {
        return false;
    }
    // 去重：该 entryid 已在待处理队列里（某笔下载）则忽略。
    for (std::deque<GroupTransferItem>::const_iterator it = m_pending.begin();
         it != m_pending.end(); ++it)
    {
        if (!it->m_isUpload && it->m_entryId == entryId)
        {
            return false;
        }
    }

    GroupTransferItem item;
    item.m_isUpload   = false;
    item.m_uploadId   = 0;
    item.m_groupId    = groupId;
    item.m_selfUserId = selfUserId;
    item.m_entryId    = entryId;
    item.m_fileId     = fileId;
    item.m_filename   = filename;
    item.m_expectMd5  = expectMd5;
    item.m_expectSize = expectSize;
    item.m_destPath   = destPath;
    m_pending.push_back(item);

    return true;
}

bool GroupTransferQueue::PopNext(GroupTransferItem& out)
{
    if (m_pending.empty())
    {
        return false;
    }

    GroupTransferItem item = m_pending.front();
    m_pending.pop_front();

    // 取出即登记为在传，并新建取消标志（初值 false）一并返回给 worker：上传按 uploadId、
    // 下载按 entryid 各记一份，worker 流式循环每片 load 检查、Cancel* 持锁时 store。
    std::shared_ptr<std::atomic<bool> > flag(new std::atomic<bool>(false));
    if (item.m_isUpload)
    {
        m_runningUploads[item.m_uploadId] = flag;
    }
    else
    {
        m_runningDownloads[item.m_entryId] = flag;
    }
    item.m_cancelFlag = flag;

    out = item;
    return true;
}

void GroupTransferQueue::CancelUpload(int uploadId)
{
    // 在传：置位其取消标志，worker 下一个分片边界中止。
    std::map<int, std::shared_ptr<std::atomic<bool> > >::iterator rit =
        m_runningUploads.find(uploadId);
    if (rit != m_runningUploads.end())
    {
        if (rit->second)
        {
            rit->second->store(true);
        }
        return;
    }

    // 仍在队列里（还没轮到）：直接移除，使其永不开始。
    for (std::deque<GroupTransferItem>::iterator it = m_pending.begin();
         it != m_pending.end(); ++it)
    {
        if (it->m_isUpload && it->m_uploadId == uploadId)
        {
            m_pending.erase(it);
            return;
        }
    }
    // id 未知（已完成 / 不存在）：无操作。
}

void GroupTransferQueue::CompleteUpload(int uploadId)
{
    m_runningUploads.erase(uploadId);
}

void GroupTransferQueue::CancelDownload(const std::string& entryId)
{
    // 在传：置位其取消标志，worker 下一个分片边界中止。
    std::map<std::string, std::shared_ptr<std::atomic<bool> > >::iterator rit =
        m_runningDownloads.find(entryId);
    if (rit != m_runningDownloads.end())
    {
        if (rit->second)
        {
            rit->second->store(true);
        }
        return;
    }

    // 仍在队列里（还没轮到）：直接移除，使其永不开始。
    for (std::deque<GroupTransferItem>::iterator it = m_pending.begin();
         it != m_pending.end(); ++it)
    {
        if (!it->m_isUpload && it->m_entryId == entryId)
        {
            m_pending.erase(it);
            return;
        }
    }
    // entryid 未知（已完成 / 不存在）：无操作。
}

void GroupTransferQueue::CompleteDownload(const std::string& entryId)
{
    m_runningDownloads.erase(entryId);
}

int GroupTransferQueue::PendingCount() const
{
    return (int)m_pending.size();
}

int GroupTransferQueue::RunningCount() const
{
    return (int)m_runningUploads.size() + (int)m_runningDownloads.size();
}

bool GroupTransferQueue::IsUploadCancelled(int uploadId) const
{
    std::map<int, std::shared_ptr<std::atomic<bool> > >::const_iterator rit =
        m_runningUploads.find(uploadId);
    if (rit == m_runningUploads.end() || !rit->second)
    {
        return false;
    }
    return rit->second->load();
}

bool GroupTransferQueue::IsDownloadCancelled(const std::string& entryId) const
{
    std::map<std::string, std::shared_ptr<std::atomic<bool> > >::const_iterator rit =
        m_runningDownloads.find(entryId);
    if (rit == m_runningDownloads.end() || !rit->second)
    {
        return false;
    }
    return rit->second->load();
}

} // namespace net
