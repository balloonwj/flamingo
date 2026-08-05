/**
 *  ContactsOrgStats.cpp
 *
 *  ContactsOrgStats.h 所声明的两个组织架构统计函数的实现。纯逻辑，只依赖 STL 与
 *  net::DepartmentItem，不触及 UI 与网络。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "ContactsOrgStats.h"

#include <map>
#include <set>

namespace balloonmain {

int FindCompanyRootDeptIndex(const std::vector<net::DepartmentItem>& depts,
                             const std::string& enterpriseNameUtf8)
{
    // 部门 id 集合 —— 判定"顶层"时要用：父部门 id 不在集合里，说明父部门没随本次
    // 应答下发，该部门在客户端看来就是一棵树的根。
    std::set<int64_t> deptIds;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        deptIds.insert(depts[i].m_deptid);
    }

    // 顶层部门在 depts 中的下标，保持原数组顺序。
    std::vector<size_t> rootIndexes;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        if (depts[i].m_parentdeptid == 0
            || deptIds.find(depts[i].m_parentdeptid) == deptIds.end())
        {
            rootIndexes.push_back(i);
        }
    }

    if (rootIndexes.empty())
    {
        return -1;
    }

    // 规则 2：名字与企业全称完全相同的顶层部门即公司根部门。
    if (!enterpriseNameUtf8.empty())
    {
        for (size_t i = 0; i < rootIndexes.size(); ++i)
        {
            if (depts[rootIndexes[i]].m_name == enterpriseNameUtf8)
            {
                return (int)rootIndexes[i];
            }
        }
    }

    // 规则 3：名字没对上，但顶层只有一棵树时认定它就是公司节点。
    if (rootIndexes.size() == 1)
    {
        return (int)rootIndexes[0];
    }

    // 规则 4：顶层有多个且都不叫企业名 —— 认不出公司这一层。
    return -1;
}

int CountDeptMembersRecursive(const std::vector<net::DepartmentItem>& depts,
                              int64_t deptId)
{
    // "父部门 id → 子部门下标列表" 与 "部门 id → 下标" 两个索引，避免逐层线性扫。
    // 部门 id 重复属于脏数据，此时后出现者覆盖先出现者（只影响该 id 取到哪一行，
    // 不影响遍历终止性）。
    std::map<int64_t, std::vector<size_t> > childrenByParent;
    std::map<int64_t, size_t> indexByDeptId;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        childrenByParent[depts[i].m_parentdeptid].push_back(i);
        indexByDeptId[depts[i].m_deptid] = i;
    }

    if (indexByDeptId.find(deptId) == indexByDeptId.end())
    {
        return 0;
    }

    std::set<int64_t> countedUsers;    // 已计入的 userid，兼属多部门者只算一次
    std::set<int64_t> visitedDepts;    // 已展开的部门 id，父子成环时兜底防死循环
    std::vector<int64_t> pending;      // 待展开的部门 id（显式栈，不用递归）
    pending.push_back(deptId);

    while (!pending.empty())
    {
        const int64_t current = pending.back();
        pending.pop_back();

        if (!visitedDepts.insert(current).second)
        {
            continue;   // 这个部门已经展开过（环或重复挂载）
        }

        std::map<int64_t, size_t>::const_iterator dit = indexByDeptId.find(current);
        if (dit != indexByDeptId.end())
        {
            const std::vector<net::DeptMemberItem>& members = depts[dit->second].m_members;
            for (size_t k = 0; k < members.size(); ++k)
            {
                countedUsers.insert(members[k].m_userid);
            }
        }

        std::map<int64_t, std::vector<size_t> >::const_iterator cit =
            childrenByParent.find(current);
        if (cit != childrenByParent.end())
        {
            for (size_t k = 0; k < cit->second.size(); ++k)
            {
                pending.push_back(depts[cit->second[k]].m_deptid);
            }
        }
    }

    return (int)countedUsers.size();
}

} // namespace balloonmain
