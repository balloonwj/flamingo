/**
 *  帖子群群成员名单的实现，见 ThreadMembers.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "ThreadMembers.h"

#include <set>

#include "../ThreadGroupApi.h"   // kMemberRoleLeft / kMemberRoleUnknown：MenuRoleOf 的两个特殊取值

namespace threadgroup {

int ThreadMemberRoleRank(int role)
{
    if (role == thread_member_role_owner)
        return thread_member_role_owner;
    if (role == thread_member_role_admin)
        return thread_member_role_admin;
    return thread_member_role_member;
}

ThreadPanelRect ComputeMemberPanelRect(int clientWidth, int clientHeight, int top, int panelWidth)
{
    ThreadPanelRect rc;
    const int width = (panelWidth < clientWidth) ? panelWidth : clientWidth;
    rc.m_right = (clientWidth > 0) ? clientWidth : 0;
    rc.m_left = rc.m_right - ((width > 0) ? width : 0);
    rc.m_top = (top > 0) ? top : 0;
    rc.m_bottom = (clientHeight > rc.m_top) ? clientHeight : rc.m_top;
    return rc;
}

CThreadMemberBook::CThreadMemberBook()
    : m_loaded(false)
{
}

void CThreadMemberBook::Set(const std::vector<ThreadMember>& members)
{
    m_members.clear();
    std::set<int64_t> seen;
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].m_userId <= 0 || !seen.insert(members[i].m_userId).second)
            continue;
        m_members.push_back(members[i]);
    }
    m_loaded = true;
}

void CThreadMemberBook::Clear()
{
    m_members.clear();
    m_loaded = false;
}

bool CThreadMemberBook::IsLoaded() const
{
    return m_loaded;
}

size_t CThreadMemberBook::Count() const
{
    return m_members.size();
}

std::wstring CThreadMemberBook::NameOf(int64_t userId) const
{
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_userId == userId)
            return m_members[i].m_name;
    }
    return std::wstring();
}

int CThreadMemberBook::MenuRoleOf(int64_t userId) const
{
    if (!m_loaded)
        return kMemberRoleUnknown;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_userId == userId)
            return m_members[i].m_role;
    }
    return kMemberRoleLeft;
}

std::vector<ThreadMember> CThreadMemberBook::PickOrder(int64_t excludeUserId) const
{
    //按角色分三趟取出：每趟内部保持原顺序，相当于按角色做稳定排序
    std::vector<ThreadMember> out;
    out.reserve(m_members.size());
    for (int rank = thread_member_role_owner; rank >= thread_member_role_member; --rank)
    {
        for (size_t i = 0; i < m_members.size(); ++i)
        {
            const ThreadMember& m = m_members[i];
            if (ThreadMemberRoleRank(m.m_role) != rank)
                continue;
            if (excludeUserId != 0 && m.m_userId == excludeUserId)
                continue;
            out.push_back(m);
        }
    }
    return out;
}

} // namespace threadgroup
