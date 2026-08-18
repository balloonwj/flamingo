/**
 *  MemberPickerTreeModel.cpp
 *
 *  选人对话框候选树模型的实现。仅做数据运算，不依赖 balloonui / GDI / 窗口 ——
 *  故与 FlamingoUnit 测试工程共用、不 include Flamingo 的 stdafx.h；在
 *  Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601

#include "MemberPickerTreeModel.h"
#include "ContactRemarkModel.h"   // ResolveContactDisplayName（显示名口径全局唯一）

#include "../BalloonSearch/PinyinFull.h"       // 全拼与首字母（"sumuchen" / "muchen" / "smc" → 苏沐辰）

#include <algorithm>
#include <map>

namespace balloonmain {

namespace {

// "未分组"节点的显示名 —— 收拢那些没有出现在任何部门里的联系人。
const TCHAR* const kUngroupedLabel = _T("未分组");

// 取不到昵称与账号时的兜底显示名格式。
const TCHAR* const kFmtFallbackName = _T("用户%u");

// UTF-8 → CString。空串安全。
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (wlen <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.GetBuffer(wlen), wlen);
    out.ReleaseBuffer();
    return out;
}

// 联系人的显示名：「真名（备注）」，与通讯录树 / 会话列表 / 收藏区同一口径。
// 选人时看到的名字必须与用户在通讯录里看到的一致，否则他按备注去找人会找不到。
// 真名本身的逐级兜底（昵称 → 账号 → "用户<id>"）由该纯函数内部完成。
CString DisplayNameOf(const net::ContactItem& c)
{
    return balloonmain::ResolveContactDisplayName(
        Utf8ToCStr(c.m_remark), Utf8ToCStr(c.m_nickname),
        Utf8ToCStr(c.m_username), (UINT)c.m_userid);
}

// 部门排序：m_deptorder 小者靠前；并列时按部门ID，保证结果稳定可复现。
bool DeptOrderLess(const net::DepartmentItem* a, const net::DepartmentItem* b)
{
    if (a->m_deptorder != b->m_deptorder)
    {
        return a->m_deptorder < b->m_deptorder;
    }
    return a->m_deptid < b->m_deptid;
}

// 部门内成员排序：m_memberorder 小者靠前；并列时按 userid 稳定排序。
bool MemberOrderLess(const net::DeptMemberItem& a, const net::DeptMemberItem& b)
{
    if (a.m_memberorder != b.m_memberorder)
    {
        return a.m_memberorder < b.m_memberorder;
    }
    return a.m_userid < b.m_userid;
}

// 判定某联系人是否命中搜索词。filterLower 为已转小写的搜索词，空串恒命中。
//
// 按四条口径依次判定，任一命中即算命中（大小写一律不敏感）：
//   ① 显示名子串        —— "沐"        → 苏沐辰
//   ② 账号子串          —— "sumu"      → 账号 sumuchen
//   ③ 显示名拼音首字母  —— "smc"       → 苏沐辰
//   ④ 显示名全拼        —— "sumuchen" / "sumu" / "muchen" → 苏沐辰
// ③④ 只在查询词是纯 ASCII 时才有意义（用户直接打汉字的情形由 ① 覆盖），
// 故先做一次 ASCII 判定，不是纯 ASCII 就到 ② 为止。
bool MatchesFilter(const net::ContactItem& c, const CString& displayName,
                   const CString& filterLower)
{
    if (filterLower.IsEmpty())
    {
        return true;
    }

    CString name = displayName;
    name.MakeLower();
    if (name.Find(filterLower) >= 0)
    {
        return true;
    }

    CString account = Utf8ToCStr(c.m_username);
    account.MakeLower();
    if (account.Find(filterLower) >= 0)
    {
        return true;
    }

    //查询词转成窄串，顺带判定是否纯 ASCII
    std::string query;
    query.reserve(filterLower.GetLength());
    for (int i = 0; i < filterLower.GetLength(); ++i)
    {
        const wchar_t ch = filterLower.GetAt(i);
        if (ch > 0x7F)
        {
            return false;   // 查询词里有汉字等非 ASCII 字符，拼音路径不适用
        }
        query += static_cast<char>(ch);
    }

    const std::wstring nameW(displayName.GetString());

    //③ 拼音首字母：取与原文等长的首字母串，直接做子串匹配即可。
    //这里用 PinyinFull 的 InitialsOf 而不是 PinyinInitials 的 Initials —— 后者只
    //覆盖 GB2312 <u>一级</u>汉字，二级字取不到首字母（"苏沐辰"的"沐"区位码 6769 就
    //在覆盖之外，会导致 smc 搜不到这个人）；前者走 Unihan 全表，没有这个缺口。
    const std::string initials = pinyin::InitialsOf(nameW);
    if (!initials.empty() && initials.find(query) != std::string::npos)
    {
        return true;
    }

    //④ 全拼：必须从某个字的音节开头起匹配，规则见 PinyinFull.h
    return pinyin::MatchesFullPinyin(nameW, query);
}

// 构树过程中用到的上下文，避免在递归函数上挂一长串参数。
struct BuildContext
{
    // userid → 联系人资料。指针指向调用方传入的数组元素，仅构树期间有效。
    std::map<int64_t, const net::ContactItem*> m_contactById;
    // 父部门ID → 子部门指针表。
    std::map<int64_t, std::vector<const net::DepartmentItem*> > m_childrenByParent;
    // 已转小写的搜索词，空串表示不过滤。
    CString m_filterLower;
    // 当前登录用户，树里要排除。
    UINT    m_selfUserId;
    // 已经出现在某个部门里的 userid，供"未分组"去重。
    std::set<int64_t> m_placed;
};

// 递归展开一棵部门子树，把命中的节点追加进 nodes。
//   ctx        构树上下文；
//   dept       当前部门；
//   parentIdx  父节点在 nodes 里的下标，-1 表示本部门是顶层；
//   depth      本部门的缩进层级；
//   visited    已展开过的部门ID集合，用来截断脏数据造成的环；
//   nodes      输出数组。
// 返回：本部门（含子孙）是否有成员进入结果。没有则本部门整枝不出现。
bool AppendDeptSubtree(BuildContext& ctx,
                       const net::DepartmentItem& dept,
                       int parentIdx,
                       int depth,
                       std::set<int64_t>& visited,
                       std::vector<MemberPickerNode>& nodes)
{
    // 环保护：同一个部门只展开一次。脏数据里 A→B→A 这种父子互指不会把递归拖死。
    if (visited.count(dept.m_deptid) != 0)
    {
        return false;
    }
    visited.insert(dept.m_deptid);

    // 先占住本部门这一行的位置。若最终发现整枝没有成员，再把它连同子孙一起截掉。
    const size_t myIdx = nodes.size();
    MemberPickerNode deptNode;
    deptNode.m_depth  = depth;
    deptNode.m_parent = parentIdx;
    deptNode.m_kind   = kPickerNodeDept;
    deptNode.m_deptId = dept.m_deptid;
    deptNode.m_label  = Utf8ToCStr(dept.m_name);
    nodes.push_back(deptNode);

    bool anyMember = false;

    // 本部门直属成员。
    std::vector<net::DeptMemberItem> members = dept.m_members;
    std::sort(members.begin(), members.end(), MemberOrderLess);
    for (size_t i = 0; i < members.size(); ++i)
    {
        const int64_t uid = members[i].m_userid;

        // 不论是否命中搜索，只要这个人挂在某个部门下，就不该再进"未分组"。
        ctx.m_placed.insert(uid);

        if (ctx.m_selfUserId != 0 && (UINT)uid == ctx.m_selfUserId)
        {
            continue;
        }
        std::map<int64_t, const net::ContactItem*>::const_iterator it =
            ctx.m_contactById.find(uid);
        if (it == ctx.m_contactById.end())
        {
            continue;   // 资料池里没有这个人，画不出行，跳过
        }

        CString display = DisplayNameOf(*it->second);
        if (!MatchesFilter(*it->second, display, ctx.m_filterLower))
        {
            continue;
        }

        MemberPickerNode memberNode;
        memberNode.m_depth  = depth + 1;
        memberNode.m_parent = (int)myIdx;
        memberNode.m_kind   = kPickerNodeMember;
        memberNode.m_userId = (UINT)uid;
        memberNode.m_label  = display;
        nodes.push_back(memberNode);
        anyMember = true;
    }

    // 子部门。
    std::map<int64_t, std::vector<const net::DepartmentItem*> >::const_iterator itKids =
        ctx.m_childrenByParent.find(dept.m_deptid);
    if (itKids != ctx.m_childrenByParent.end())
    {
        std::vector<const net::DepartmentItem*> kids = itKids->second;
        std::sort(kids.begin(), kids.end(), DeptOrderLess);
        for (size_t i = 0; i < kids.size(); ++i)
        {
            if (AppendDeptSubtree(ctx, *kids[i], (int)myIdx, depth + 1, visited, nodes))
            {
                anyMember = true;
            }
        }
    }

    if (!anyMember)
    {
        // 整枝没人（本来就空，或被搜索过滤空了）：把本部门连同已追加的子孙一起截掉。
        nodes.resize(myIdx);
    }
    return anyMember;
}

} // anonymous namespace

std::vector<MemberPickerNode> BuildMemberPickerTree(
    const std::vector<net::ContactItem>& contacts,
    const std::vector<net::DepartmentItem>& departments,
    UINT selfUserId,
    const CString& filter)
{
    std::vector<MemberPickerNode> nodes;

    BuildContext ctx;
    ctx.m_selfUserId  = selfUserId;
    ctx.m_filterLower = filter;
    ctx.m_filterLower.Trim();
    ctx.m_filterLower.MakeLower();

    for (size_t i = 0; i < contacts.size(); ++i)
    {
        ctx.m_contactById[contacts[i].m_userid] = &contacts[i];
    }

    // 部门ID集合 + 父子索引。根部门 = 父为 0，或父不在本次下发的集合里
    // （与主面板通讯录树同口径，避免存量数据里父部门缺失时整枝丢失）。
    std::set<int64_t> deptIds;
    for (size_t i = 0; i < departments.size(); ++i)
    {
        deptIds.insert(departments[i].m_deptid);
        ctx.m_childrenByParent[departments[i].m_parentdeptid].push_back(&departments[i]);
    }

    std::vector<const net::DepartmentItem*> roots;
    for (size_t i = 0; i < departments.size(); ++i)
    {
        const net::DepartmentItem& d = departments[i];
        if (d.m_parentdeptid == 0 || deptIds.find(d.m_parentdeptid) == deptIds.end())
        {
            roots.push_back(&d);
        }
    }
    std::sort(roots.begin(), roots.end(), DeptOrderLess);

    std::set<int64_t> visited;
    for (size_t i = 0; i < roots.size(); ++i)
    {
        AppendDeptSubtree(ctx, *roots[i], -1, 0, visited, nodes);
    }

    // 没进任何部门的联系人 —— 收进末尾的"未分组"。候选来自群成员（没有部门数据）时，
    // 所有人都走这一支。
    std::vector<const net::ContactItem*> ungrouped;
    for (size_t i = 0; i < contacts.size(); ++i)
    {
        const net::ContactItem& c = contacts[i];
        if (selfUserId != 0 && (UINT)c.m_userid == selfUserId)
        {
            continue;
        }
        if (ctx.m_placed.count(c.m_userid) != 0)
        {
            continue;
        }
        if (!MatchesFilter(c, DisplayNameOf(c), ctx.m_filterLower))
        {
            continue;
        }
        ungrouped.push_back(&c);
    }

    if (!ungrouped.empty())
    {
        const size_t groupIdx = nodes.size();
        MemberPickerNode groupNode;
        groupNode.m_depth = 0;
        groupNode.m_kind  = kPickerNodeDept;
        groupNode.m_label = kUngroupedLabel;
        nodes.push_back(groupNode);

        for (size_t i = 0; i < ungrouped.size(); ++i)
        {
            MemberPickerNode memberNode;
            memberNode.m_depth  = 1;
            memberNode.m_parent = (int)groupIdx;
            memberNode.m_kind   = kPickerNodeMember;
            memberNode.m_userId = (UINT)ungrouped[i]->m_userid;
            memberNode.m_label  = DisplayNameOf(*ungrouped[i]);
            nodes.push_back(memberNode);
        }
    }

    return nodes;
}

void CollectDeptMemberIds(const std::vector<MemberPickerNode>& nodes,
                          int deptIndex,
                          std::vector<UINT>& out)
{
    if (deptIndex < 0 || deptIndex >= (int)nodes.size())
    {
        return;
    }
    if (nodes[deptIndex].m_kind != kPickerNodeDept)
    {
        return;
    }

    // 子孙判定靠 m_depth：从本节点之后往下扫，深度大于本节点的都是它的子孙，
    // 遇到第一个深度不大于本节点的就说明这一枝到头了。
    const int baseDepth = nodes[deptIndex].m_depth;
    std::set<UINT> seen(out.begin(), out.end());
    for (size_t i = (size_t)deptIndex + 1; i < nodes.size(); ++i)
    {
        if (nodes[i].m_depth <= baseDepth)
        {
            break;
        }
        if (nodes[i].m_kind != kPickerNodeMember)
        {
            continue;
        }
        // 同一人兼属多个子部门时只收一次。
        if (seen.insert(nodes[i].m_userId).second)
        {
            out.push_back(nodes[i].m_userId);
        }
    }
}

int DeriveCheckState(const std::vector<MemberPickerNode>& nodes,
                     int index,
                     const std::set<UINT>& selected)
{
    if (index < 0 || index >= (int)nodes.size())
    {
        return kPickerUnchecked;
    }

    if (nodes[index].m_kind == kPickerNodeMember)
    {
        return (selected.count(nodes[index].m_userId) != 0)
               ? kPickerChecked : kPickerUnchecked;
    }

    std::vector<UINT> ids;
    CollectDeptMemberIds(nodes, index, ids);
    if (ids.empty())
    {
        return kPickerUnchecked;   // 空部门没有"全选"可言
    }

    size_t checked = 0;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (selected.count(ids[i]) != 0)
        {
            ++checked;
        }
    }
    if (checked == 0)
    {
        return kPickerUnchecked;
    }
    return (checked == ids.size()) ? kPickerChecked : kPickerPartial;
}

} // namespace balloonmain
