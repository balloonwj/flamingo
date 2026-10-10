/**
 *  GroupCallHub.cpp —— exe 侧的群通话中心，见 GroupCallHub.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "GroupCallHub.h"

#include "../../Common/Log/IULog.h"
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace balloongroupcall {

CGroupCallHub& CGroupCallHub::GetInstance()
{
    static CGroupCallHub instance;
    return instance;
}

CGroupCallHub::CGroupCallHub()
    : m_module(NULL)
{
}

void CGroupCallHub::AttachModule(flamingomedia::IMediaModule* module)
{
    m_module = module;
}

bool CGroupCallHub::IsModuleReady() const
{
    return m_module != NULL;
}

bool CGroupCallHub::StartCall(int64_t groupId, const CString& groupName, int media,
                              const std::vector<int64_t>& invitees)
{
    if (m_module == NULL)
    {
        LOG_WARNING("groupcall hub: start rejected, media module not loaded, group=%lld.", (long long)groupId);
        return false;
    }
    LOG_INFO("groupcall hub: start, group=%lld, media=%d, invitees=%u.", (long long)groupId, media,
             (unsigned)invitees.size());
    return m_module->StartGroupCall(groupId, groupName, media, invitees.empty() ? NULL : &invitees[0],
                                    (uint32_t)invitees.size());
}

bool CGroupCallHub::JoinCall(int64_t groupId, const CString& groupName, const std::string& roomId)
{
    if (m_module == NULL || roomId.empty())
        return false;
    LOG_INFO("groupcall hub: join, group=%lld, room=%s.", (long long)groupId, roomId.c_str());
    return m_module->JoinGroupCall(groupId, groupName, roomId.c_str());
}

void CGroupCallHub::Query(int64_t groupId)
{
    if (m_module == NULL || groupId <= 0)
        return;
    m_module->QueryGroupCall(groupId);
}

bool CGroupCallHub::InviteMembers(int64_t groupId, const std::vector<int64_t>& userIds)
{
    if (m_module == NULL || userIds.empty())
        return false;
    LOG_INFO("groupcall hub: invite more, group=%lld, count=%u.", (long long)groupId, (unsigned)userIds.size());
    return m_module->InviteToGroupCall(groupId, &userIds[0], (uint32_t)userIds.size());
}

void CGroupCallHub::OnStatus(int64_t groupId, const char* roomId, bool active, int media, int count,
                             int64_t initiatorId, int64_t startTime)
{
    m_cache.Update(groupId, roomId != NULL ? std::string(roomId) : std::string(), active, media, count, initiatorId,
                   startTime);
}

int CGroupCallHub::RoomState(int64_t groupId, const std::string& roomId) const
{
    return m_cache.RoomState(groupId, roomId);
}

bool CGroupCallHub::GetActive(int64_t groupId, GroupCallActiveInfo& out) const
{
    return m_cache.GetActive(groupId, out);
}

void CGroupCallHub::ClearStatus()
{
    m_cache.Clear();
}

} // namespace balloongroupcall
