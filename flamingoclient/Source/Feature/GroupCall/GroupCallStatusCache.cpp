/**
 *  GroupCallStatusCache.cpp
 *  说明见 GroupCallStatusCache.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "GroupCallStatusCache.h"

namespace balloongroupcall {

void GroupCallStatusCache::Update(int64_t groupId, const std::string& roomId, bool active, int media, int count,
                                  int64_t initiatorId, int64_t startTime)
{
    if (groupId <= 0)
        return;
    Entry& e = m_entries[groupId];
    //说是进行中、却没有房间号：按没有处理，免得横幅显示一个点不进去的「加入」
    e.m_active = active && !roomId.empty();
    e.m_info = GroupCallActiveInfo();
    if (!e.m_active)
        return;
    e.m_info.m_roomId = roomId;
    e.m_info.m_media = media;
    e.m_info.m_count = count;
    e.m_info.m_initiatorId = initiatorId;
    e.m_info.m_startTime = startTime;
}

int GroupCallStatusCache::RoomState(int64_t groupId, const std::string& roomId) const
{
    std::map<int64_t, Entry>::const_iterator it = m_entries.find(groupId);
    if (it == m_entries.end())
        return kGroupCallRoomUnknown;
    if (it->second.m_active && it->second.m_info.m_roomId == roomId)
        return kGroupCallRoomActive;
    return kGroupCallRoomEnded;
}

bool GroupCallStatusCache::GetActive(int64_t groupId, GroupCallActiveInfo& out) const
{
    std::map<int64_t, Entry>::const_iterator it = m_entries.find(groupId);
    if (it == m_entries.end() || !it->second.m_active)
        return false;
    out = it->second.m_info;
    return true;
}

void GroupCallStatusCache::Clear()
{
    m_entries.clear();
}

} // namespace balloongroupcall
