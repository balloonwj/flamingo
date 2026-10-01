/**
 *  GroupListOps.cpp
 *
 *  「群」Tab 群列表纯操作函数的实现，说明见 GroupListOps.h。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "GroupListOps.h"

namespace balloonmain {

std::vector<size_t> OrderGroupsForList(const std::vector<net::UserGroupItem>& groups)
{
    //两遍扫描：先收置顶的，再收其余的。两遍都按原下标顺序走，组内顺序自然不变
    std::vector<size_t> order;
    order.reserve(groups.size());
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (groups[i].m_pinned != 0)
            order.push_back(i);
    }
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (groups[i].m_pinned == 0)
            order.push_back(i);
    }
    return order;
}

bool SetUserGroupPinned(std::vector<net::UserGroupItem>& groups, int64_t groupId, bool pinned)
{
    const int target = pinned ? 1 : 0;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (groups[i].m_groupid != groupId)
            continue;

        if (groups[i].m_pinned == target)
            return false;
        groups[i].m_pinned = target;
        return true;
    }
    return false;
}

bool IsUserGroupPinned(const std::vector<net::UserGroupItem>& groups, int64_t groupId)
{
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (groups[i].m_groupid == groupId)
            return groups[i].m_pinned != 0;
    }
    return false;
}

} // namespace balloonmain
