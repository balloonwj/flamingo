/**
 *  NotifyModeModel.cpp
 *
 *  会话消息提醒模式内存模型（模式表）的实现。纯数据操作，不碰窗口、不碰网络，
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里
 *  本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "NotifyModeModel.h"
#include "../../net/Protocol.h"   // enum conversation_notify_mode / conversation_type

namespace balloonmain {

NotifyModeModel::NotifyModeModel()
{
}

bool NotifyModeModel::IsValid(int ctype, int64_t cid, int mode)
{
    if (ctype != conversation_single && ctype != conversation_group)
        return false;
    if (cid <= 0)
        return false;
    return mode >= notify_mode_allow && mode <= notify_mode_mention_follow;
}

void NotifyModeModel::Reset(const std::vector<net::NotifyModeEntry>& entries)
{
    m_modes.clear();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const net::NotifyModeEntry& e = entries[i];
        if (!IsValid(e.m_ctype, e.m_cid, e.m_mode))
            continue;
        //默认模式不存，查不到即按默认处理
        if (e.m_mode == notify_mode_allow)
            continue;
        m_modes[Key(e.m_ctype, e.m_cid)] = e.m_mode;
    }
}

int NotifyModeModel::GetMode(int ctype, int64_t cid) const
{
    std::map<Key, int>::const_iterator it = m_modes.find(Key(ctype, cid));
    if (it == m_modes.end())
        return notify_mode_allow;
    return it->second;
}

bool NotifyModeModel::SetMode(int ctype, int64_t cid, int mode)
{
    if (!IsValid(ctype, cid, mode))
        return false;

    if (GetMode(ctype, cid) == mode)
        return false;

    if (mode == notify_mode_allow)
        m_modes.erase(Key(ctype, cid));
    else
        m_modes[Key(ctype, cid)] = mode;
    return true;
}

bool NotifyModeModel::ApplyEntries(const std::vector<net::NotifyModeEntry>& entries)
{
    bool changed = false;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (SetMode(entries[i].m_ctype, entries[i].m_cid, entries[i].m_mode))
            changed = true;
    }
    return changed;
}

int NotifyModeModel::CountMode(int mode) const
{
    int count = 0;
    for (std::map<Key, int>::const_iterator it = m_modes.begin(); it != m_modes.end(); ++it)
    {
        if (it->second == mode)
            ++count;
    }
    return count;
}

bool NotifyModeModel::CanSetStarred(int ctype, int64_t cid) const
{
    if (GetMode(ctype, cid) == notify_mode_starred)
        return true;
    return CountMode(notify_mode_starred) < kNotifyModeStarredMax;
}

void NotifyModeModel::ExportEntries(std::vector<net::NotifyModeEntry>& out) const
{
    out.clear();
    for (std::map<Key, int>::const_iterator it = m_modes.begin(); it != m_modes.end(); ++it)
    {
        net::NotifyModeEntry e;
        e.m_ctype = it->first.first;
        e.m_cid   = it->first.second;
        e.m_mode  = it->second;
        out.push_back(e);
    }
}

void NotifyModeModel::Clear()
{
    m_modes.clear();
}

} // namespace balloonmain
