/**
 *  flamingoAdmin —— 组织架构树组装逻辑的实现。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#include "stdafx.h"
#include "Util/OrgTreeBuilder.h"

#include <map>
#include <algorithm>

namespace flamingoadmin {

namespace orgtree {

const wchar_t* const kSyntheticRootId = L"root";

namespace {

// 组装时用的排序信息。按"层级深的排前、同层级按排序权重升序"排列，
// 使得处理到某个部门时它的子部门都已经挂好，一趟自底向上就能把树接完。
struct DeptOrderKey
{
    long long m_deptId = 0;   // 部门 ID
    long long m_parentId = 0; // 父部门 ID
    int       m_level = 1;    // 部门层级
    int       m_order = 0;    // 同级排序权重
};

bool DeptDeeperFirst(const DeptOrderKey& a, const DeptOrderKey& b)
{
    if (a.m_level != b.m_level)
    {
        return a.m_level > b.m_level;
    }
    return a.m_order < b.m_order;
}

} // anonymous namespace

OrgNode BuildOrgTree(const std::vector<OrgDeptRaw>& depts,
                     long long rootDeptId,
                     const CString& enterpriseName)
{
    // 第一遍：每个部门建一个 OrgNode（此时还不挂子部门），成员先暂存一旁，
    // 同时把层级与父子关系收进排序表。
    std::map<long long, OrgNode>              nodes;        // deptid -> 部门节点
    std::map<long long, std::vector<OrgNode>> deptMembers;  // deptid -> 成员节点表
    std::vector<DeptOrderKey>                 orderKeys;    // 供排序用

    for (size_t i = 0; i < depts.size(); ++i)
    {
        const OrgDeptRaw& d = depts[i];

        DeptOrderKey key;
        key.m_deptId   = d.m_deptId;
        key.m_parentId = d.m_parentDeptId;
        key.m_level    = d.m_level;
        key.m_order    = d.m_order;
        orderKeys.push_back(key);

        OrgNode node;
        node.m_kind = OrgNodeKind::Dept;
        CString deptIdText;
        deptIdText.Format(_T("%lld"), d.m_deptId);
        node.m_id     = deptIdText;
        node.m_name   = d.m_name;
        node.m_leader = d.m_leader;

        nodes[d.m_deptId]       = node;
        deptMembers[d.m_deptId] = d.m_members;
    }

    // 真实的公司根部门必须确实出现在部门数组里，否则只能退回合成一个根节点。
    const bool hasRealRoot = (rootDeptId != 0 && nodes.find(rootDeptId) != nodes.end());

    // 第二遍：层级深的先处理。轮到某部门时它的子部门都已挂好，此时再把它自己的
    // 成员追加在子部门之后，然后把整个节点挂到父部门下。
    std::sort(orderKeys.begin(), orderKeys.end(), DeptDeeperFirst);

    // 父部门不在数组里的部门先收在这里，最后统一挂到根节点下，避免整棵子树丢失。
    std::vector<OrgNode> strayNodes;

    for (size_t i = 0; i < orderKeys.size(); ++i)
    {
        const DeptOrderKey& key = orderKeys[i];
        OrgNode& node = nodes[key.m_deptId];

        const std::vector<OrgNode>& members = deptMembers[key.m_deptId];
        for (size_t k = 0; k < members.size(); ++k)
        {
            node.m_children.push_back(members[k]);
        }

        // 公司根部门自己就是整棵树的根，不往任何地方挂，留到最后整体返回。
        if (hasRealRoot && key.m_deptId == rootDeptId)
        {
            continue;
        }

        if (key.m_parentId != 0 && nodes.find(key.m_parentId) != nodes.end())
        {
            nodes[key.m_parentId].m_children.push_back(node);
        }
        else
        {
            strayNodes.push_back(node);
        }
    }

    // 确定最终的根节点：优先用真实的公司根部门，没有才退回合成节点。
    OrgNode root;
    if (hasRealRoot)
    {
        root = nodes[rootDeptId];
    }
    else
    {
        root.m_kind = OrgNodeKind::Dept;
        root.m_id   = CString(kSyntheticRootId);
        root.m_name = enterpriseName;
    }

    // 根节点代表企业本身，无论真实还是合成，都不允许被删除。
    root.m_isCompanyRoot = true;

    for (size_t i = 0; i < strayNodes.size(); ++i)
    {
        root.m_children.push_back(strayNodes[i]);
    }

    return root;
}

} // namespace orgtree

} // namespace flamingoadmin
