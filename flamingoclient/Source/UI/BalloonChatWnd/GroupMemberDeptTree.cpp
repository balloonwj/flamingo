/**
 *  GroupMemberDeptTree.cpp
 *
 *  群成员部门树纯模型的实现。说明见 GroupMemberDeptTree.h。只依赖标准库与协议层的部门结构，
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "GroupMemberDeptTree.h"

#include <algorithm>
#include <map>
#include <set>

namespace balloonchatwnd {

namespace {

// 群角色的网络侧取值（与 Protocol.h 的 group_member_role 一致）：群主、管理员
const int kRoleOwner = 2;
const int kRoleAdmin = 1;

// 群角色在排序里的先后：群主、管理员、其余（含不认识的值）
const int kRankOwner  = 0;
const int kRankAdmin  = 1;
const int kRankMember = 2;

// 某个群角色的排序先后
int RoleRank(int role)
{
    if (role == kRoleOwner)
        return kRankOwner;
    if (role == kRoleAdmin)
        return kRankAdmin;
    return kRankMember;
}

// 一位本群成员的排序与标记信息
struct MemberMeta
{
    int    m_rank;        // 群角色的排序先后，见 RoleRank
    size_t m_inputIndex;  // 在输入里的位置，同角色、同排序值时按它排
    bool   m_hasPrimary;  // 是否在某个部门里是主部门成员；为 false 时他的各行都不标「兼」

    MemberMeta()
        : m_rank(kRankMember)
        , m_inputIndex(0)
        , m_hasPrimary(false)
    {
    }
};

// 部门内一位成员的排序键
struct MemberSortKey
{
    int64_t m_userId;      // 成员 userid
    int     m_rank;        // 群角色的排序先后
    int     m_order;       // 部门内排序值，值小者靠前
    size_t  m_inputIndex;  // 在输入里的位置
    bool    m_secondary;   // 这个部门是不是他的兼属部门

    MemberSortKey()
        : m_userId(0)
        , m_rank(kRankMember)
        , m_order(0)
        , m_inputIndex(0)
        , m_secondary(false)
    {
    }
};

// 成员的显示先后：群角色、部门内排序值、输入顺序
bool MemberLess(const MemberSortKey& a, const MemberSortKey& b)
{
    if (a.m_rank != b.m_rank)
        return a.m_rank < b.m_rank;
    if (a.m_order != b.m_order)
        return a.m_order < b.m_order;
    return a.m_inputIndex < b.m_inputIndex;
}

// 构树过程的共享数据
struct BuildContext
{
    std::map<int64_t, MemberMeta> m_members;                       // 本群成员 userid → 排序与标记信息
    std::map<int64_t, const net::DepartmentItem*> m_depts;         // 部门 id → 部门（id 重复时取第一个）
    std::map<int64_t, std::vector<int64_t> > m_children;           // 父部门 id → 子部门 id；0 下面是顶级部门
    std::set<int64_t> m_placed;                                    // 出现在某个部门里的本群成员
    std::set<int64_t> m_expanded;                                  // 已经展开过的部门，防御异常数据造成的重复展开
};

// 部门的显示先后：排序值，再按部门 id
struct DeptLess
{
    const BuildContext* m_ctx;   // 用来按 id 取部门

    explicit DeptLess(const BuildContext* ctx)
        : m_ctx(ctx)
    {
    }

    bool operator()(int64_t a, int64_t b) const
    {
        const net::DepartmentItem* da = m_ctx->m_depts.find(a)->second;
        const net::DepartmentItem* db = m_ctx->m_depts.find(b)->second;
        if (da->m_deptorder != db->m_deptorder)
            return da->m_deptorder < db->m_deptorder;
        return a < b;
    }
};

// 算出每个部门的有效上级：上级不存在或指向自己的按顶级部门；上级链有环时从环上断开一处
std::map<int64_t, int64_t> ComputeEffectiveParents(const BuildContext& ctx)
{
    std::map<int64_t, int64_t> eff;
    for (std::map<int64_t, const net::DepartmentItem*>::const_iterator it = ctx.m_depts.begin();
         it != ctx.m_depts.end(); ++it)
    {
        const int64_t parent = it->second->m_parentdeptid;
        const bool parentKnown = (parent != it->first) && (ctx.m_depts.count(parent) != 0);
        eff[it->first] = parentKnown ? parent : 0;
    }

    //逐个部门沿上级链往上走：走回已经走过的部门说明有环，把当前这一步的上级断开，当前部门成为顶级部门
    for (std::map<int64_t, int64_t>::iterator it = eff.begin(); it != eff.end(); ++it)
    {
        std::set<int64_t> visited;
        visited.insert(it->first);
        int64_t cur = it->first;
        while (true)
        {
            const int64_t p = eff[cur];
            if (p == 0)
                break;
            if (visited.count(p) != 0)
            {
                eff[cur] = 0;
                break;
            }
            visited.insert(p);
            cur = p;
        }
    }
    return eff;
}

// 展开一个部门：先占住部门行，再依次追加下级部门与直属成员。整枝没有本群成员时把部门行连同子孙一起去掉。
//   ctx：      构树数据
//   deptId：   要展开的部门
//   depth：    部门行的缩进层级
//   parentIdx：父部门行在 rows 里的下标，顶级部门为 -1
//   rows：     输出
//   subtree：  [出参] 本部门及下级部门里出现的本群成员
void AppendDept(BuildContext& ctx, int64_t deptId, int depth, int parentIdx,
                std::vector<DeptTreeRow>& rows, std::set<int64_t>& subtree)
{
    subtree.clear();
    if (ctx.m_expanded.count(deptId) != 0)
        return;
    ctx.m_expanded.insert(deptId);

    const net::DepartmentItem& dept = *ctx.m_depts[deptId];
    const size_t myIdx = rows.size();
    DeptTreeRow head;
    head.m_kind     = kDeptTreeRowDept;
    head.m_depth    = depth;
    head.m_parent   = parentIdx;
    head.m_deptId   = deptId;
    head.m_deptName = dept.m_name;
    rows.push_back(head);

    //下级部门在前
    std::map<int64_t, std::vector<int64_t> >::iterator kids = ctx.m_children.find(deptId);
    if (kids != ctx.m_children.end())
    {
        std::vector<int64_t> sorted = kids->second;
        std::sort(sorted.begin(), sorted.end(), DeptLess(&ctx));
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            std::set<int64_t> childSet;
            AppendDept(ctx, sorted[i], depth + 1, (int)myIdx, rows, childSet);
            subtree.insert(childSet.begin(), childSet.end());
        }
    }

    //直属成员在后：只取本群成员，同一部门里重复出现的人只取一次
    std::vector<MemberSortKey> keys;
    std::set<int64_t> seen;
    for (size_t i = 0; i < dept.m_members.size(); ++i)
    {
        const net::DeptMemberItem& dm = dept.m_members[i];
        std::map<int64_t, MemberMeta>::const_iterator mi = ctx.m_members.find(dm.m_userid);
        if (mi == ctx.m_members.end() || !seen.insert(dm.m_userid).second)
            continue;
        MemberSortKey k;
        k.m_userId     = dm.m_userid;
        k.m_rank       = mi->second.m_rank;
        k.m_order      = dm.m_memberorder;
        k.m_inputIndex = mi->second.m_inputIndex;
        k.m_secondary  = (dm.m_isprimary == 0) && mi->second.m_hasPrimary;
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end(), MemberLess);
    for (size_t i = 0; i < keys.size(); ++i)
    {
        DeptTreeRow row;
        row.m_kind      = kDeptTreeRowMember;
        row.m_depth     = depth + 1;
        row.m_parent    = (int)myIdx;
        row.m_deptId    = deptId;
        row.m_userId    = keys[i].m_userId;
        row.m_secondary = keys[i].m_secondary;
        rows.push_back(row);
        subtree.insert(keys[i].m_userId);
        ctx.m_placed.insert(keys[i].m_userId);
    }

    if (subtree.empty())
    {
        rows.resize(myIdx);   //整枝没有本群成员：部门行连同已追加的子孙一起去掉
        return;
    }
    rows[myIdx].m_memberCount = (int)subtree.size();
}

} // anonymous namespace

std::vector<DeptTreeRow> BuildGroupMemberDeptTree(const std::vector<DeptTreeMemberInput>& members,
                                                  const std::vector<net::DepartmentItem>& departments)
{
    std::vector<DeptTreeRow> rows;
    if (members.empty())
        return rows;

    BuildContext ctx;
    std::vector<int64_t> memberOrder;   //去重后的成员，保持输入顺序（「未分配部门」按它排）
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].m_userId <= 0 || ctx.m_members.count(members[i].m_userId) != 0)
            continue;
        MemberMeta meta;
        meta.m_rank       = RoleRank(members[i].m_role);
        meta.m_inputIndex = i;
        ctx.m_members[members[i].m_userId] = meta;
        memberOrder.push_back(members[i].m_userId);
    }
    for (size_t i = 0; i < departments.size(); ++i)
    {
        if (departments[i].m_deptid > 0 && ctx.m_depts.count(departments[i].m_deptid) == 0)
            ctx.m_depts[departments[i].m_deptid] = &departments[i];
    }

    //谁在某个部门里是主部门成员：只有这样的人，兼属部门那一行才标「兼」
    for (std::map<int64_t, const net::DepartmentItem*>::const_iterator it = ctx.m_depts.begin();
         it != ctx.m_depts.end(); ++it)
    {
        const std::vector<net::DeptMemberItem>& dms = it->second->m_members;
        for (size_t i = 0; i < dms.size(); ++i)
        {
            std::map<int64_t, MemberMeta>::iterator mi = ctx.m_members.find(dms[i].m_userid);
            if (mi != ctx.m_members.end() && dms[i].m_isprimary != 0)
                mi->second.m_hasPrimary = true;
        }
    }

    const std::map<int64_t, int64_t> eff = ComputeEffectiveParents(ctx);
    for (std::map<int64_t, int64_t>::const_iterator it = eff.begin(); it != eff.end(); ++it)
    {
        ctx.m_children[it->second].push_back(it->first);
    }

    //顶级部门
    std::map<int64_t, std::vector<int64_t> >::iterator roots = ctx.m_children.find(0);
    if (roots != ctx.m_children.end())
    {
        std::vector<int64_t> sorted = roots->second;
        std::sort(sorted.begin(), sorted.end(), DeptLess(&ctx));
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            std::set<int64_t> subtree;
            AppendDept(ctx, sorted[i], 0, -1, rows, subtree);
        }
    }

    //不属于任何部门的本群成员：最后的「未分配部门」，先按群角色、再按输入顺序
    std::vector<MemberSortKey> loose;
    for (size_t i = 0; i < memberOrder.size(); ++i)
    {
        if (ctx.m_placed.count(memberOrder[i]) != 0)
            continue;
        const MemberMeta& meta = ctx.m_members[memberOrder[i]];
        MemberSortKey k;
        k.m_userId     = memberOrder[i];
        k.m_rank       = meta.m_rank;
        k.m_inputIndex = meta.m_inputIndex;
        loose.push_back(k);
    }
    if (!loose.empty())
    {
        std::sort(loose.begin(), loose.end(), MemberLess);
        const size_t headIdx = rows.size();
        DeptTreeRow head;
        head.m_kind        = kDeptTreeRowDept;
        head.m_deptId      = kDeptTreeUnassignedId;
        head.m_memberCount = (int)loose.size();
        rows.push_back(head);
        for (size_t i = 0; i < loose.size(); ++i)
        {
            DeptTreeRow row;
            row.m_kind   = kDeptTreeRowMember;
            row.m_depth  = 1;
            row.m_parent = (int)headIdx;
            row.m_deptId = kDeptTreeUnassignedId;
            row.m_userId = loose[i].m_userId;
            rows.push_back(row);
        }
    }
    return rows;
}

} // namespace balloonchatwnd
