/**
 *  ScreenShareStatusCache.cpp —— 按会话记录的屏幕共享状态，见 ScreenShareStatusCache.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ScreenShareStatusCache.h"

namespace balloonscreenshare {

void ScreenShareStatusCache::Update(int ctype, int64_t cid, const std::string& roomId, int64_t hostId,
                                    const std::wstring& hostName, bool active, bool locked, bool forced,
                                    const std::wstring& title)
{
    Entry& entry = m_entries[ConversationKey(ctype, cid)];
    //没有进行中的共享，或给出的房间号为空（不应出现）：该会话的所有卡片都视为已结束
    if (!active || roomId.empty())
    {
        entry.m_active = false;
        entry.m_info = ScreenShareActiveInfo();
        return;
    }
    //查询应答不带发起人名：同一场共享、主持人没换时沿用已知的名字；换了主持人（2026-10-08）时留空，
    //由聊天窗按新主持人的 userid 取名字
    std::wstring name = hostName;
    if (name.empty() && entry.m_active && entry.m_info.m_roomId == roomId && entry.m_info.m_hostId == hostId)
        name = entry.m_info.m_hostName;
    //老服务端的查询应答不带标题（第 15 期）：同一场共享沿用已知的标题
    std::wstring shareTitle = title;
    if (shareTitle.empty() && entry.m_active && entry.m_info.m_roomId == roomId)
        shareTitle = entry.m_info.m_title;
    entry.m_active = true;
    entry.m_info.m_roomId = roomId;
    entry.m_info.m_hostId = hostId;
    entry.m_info.m_hostName = name;
    entry.m_info.m_locked = locked;
    entry.m_info.m_forced = forced;
    entry.m_info.m_title = shareTitle;
}

int ScreenShareStatusCache::RoomState(int ctype, int64_t cid, const std::string& roomId) const
{
    if (roomId.empty())
        return kScreenShareRoomUnknown;
    std::map<ConversationKey, Entry>::const_iterator it = m_entries.find(ConversationKey(ctype, cid));
    if (it == m_entries.end())
        return kScreenShareRoomUnknown;
    //同一会话同时只有一场共享：进行中的正是这一场才算正在共享，其余（更早的卡片）都已结束
    if (it->second.m_active && it->second.m_info.m_roomId == roomId)
        return kScreenShareRoomActive;
    return kScreenShareRoomEnded;
}

bool ScreenShareStatusCache::GetActive(int ctype, int64_t cid, ScreenShareActiveInfo& out) const
{
    std::map<ConversationKey, Entry>::const_iterator it = m_entries.find(ConversationKey(ctype, cid));
    if (it == m_entries.end() || !it->second.m_active)
        return false;
    out = it->second.m_info;
    return true;
}

bool ScreenShareStatusCache::ApplyInfo(int ctype, int64_t cid, const std::string& roomId, const std::wstring& title,
                                       int64_t hostId, const std::wstring& hostName)
{
    //推送里没有可更新的内容
    if (title.empty() && hostId <= 0)
        return false;
    //只更新缓存记着的正是这一场的会话；没有记录或已结束时不新建（打开聊天窗时的查询会拿到最新状态）
    std::map<ConversationKey, Entry>::iterator it = m_entries.find(ConversationKey(ctype, cid));
    if (it == m_entries.end() || !it->second.m_active || it->second.m_info.m_roomId != roomId)
        return false;
    ScreenShareActiveInfo& info = it->second.m_info;
    if (!title.empty())
        info.m_title = title;
    //换了主持人：名字随推送，推送里为空时留空、由聊天窗按 userid 取名字，不沿用上一位主持人的名字
    if (hostId > 0)
    {
        info.m_hostId = hostId;
        info.m_hostName = hostName;
    }
    return true;
}

void ScreenShareStatusCache::Clear()
{
    m_entries.clear();
}

} // namespace balloonscreenshare
