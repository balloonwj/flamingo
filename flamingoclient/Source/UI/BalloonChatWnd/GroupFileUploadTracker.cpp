/**
 *  GroupFileUploadTracker.cpp
 *
 *  群文件「正在上传」行的内存模型实现（纯逻辑）。设计见 GroupFileUploadTracker.h。
 *
 *  balloonwj@qq.com   2026-06-09
 */

#include "GroupFileUploadTracker.h"

namespace balloonchatwnd {

GroupFileUploadTracker::GroupFileUploadTracker()
{
}

void GroupFileUploadTracker::Begin(int uploadId, const std::wstring& name, const std::wstring& ext)
{
    // 已存在同 id（理论上不会）：就地更新、进度归零，不产生重复行。
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_uploadId == uploadId)
        {
            m_items[i].m_name    = name;
            m_items[i].m_ext     = ext;
            m_items[i].m_percent = 0;
            return;
        }
    }

    GroupUploadEntry e;
    e.m_uploadId = uploadId;
    e.m_name     = name;
    e.m_ext      = ext;
    e.m_percent  = 0;
    // 插到最前：最近发起的显示在最上。
    m_items.insert(m_items.begin(), e);
}

void GroupFileUploadTracker::SetProgress(int uploadId, int percent)
{
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_uploadId == uploadId)
        {
            m_items[i].m_percent = percent;
            return;
        }
    }
    // uploadId 不存在：无操作。
}

bool GroupFileUploadTracker::Remove(int uploadId)
{
    for (std::vector<GroupUploadEntry>::iterator it = m_items.begin();
         it != m_items.end(); ++it)
    {
        if (it->m_uploadId == uploadId)
        {
            m_items.erase(it);
            return true;
        }
    }
    return false;
}

const GroupUploadEntry* GroupFileUploadTracker::Find(int uploadId) const
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_uploadId == uploadId)
        {
            return &m_items[i];
        }
    }
    return NULL;
}

int GroupFileUploadTracker::Count() const
{
    return (int)m_items.size();
}

void GroupFileUploadTracker::Clear()
{
    m_items.clear();
}

} // namespace balloonchatwnd
