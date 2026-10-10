/**
 *  DeptPickModel.cpp
 *
 *  「选择部门」小窗部门树的纯逻辑，说明见 DeptPickModel.h。只做数据运算，不依赖 balloonui / GDI / 窗口，
 *  故与 FlamingoUnit 测试工程共用、不 include Flamingo 的 stdafx.h；在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "DeptPickModel.h"

#include <wctype.h>
#include <algorithm>
#include <map>
#include <set>

#include "../../Common/Util/EncodeUtil.h"   // EncodeUtil::Utf8ToUnicode：部门名称是 UTF-8

namespace balloonmain {

namespace {

/**
 *  同级部门的排序依据：先按 m_deptorder 升序，权重相同时按在部门列表里的下标，保证结果稳定。
 */
struct SiblingKey
{
    int    m_order;   // 部门的 m_deptorder
    size_t m_index;   // 部门在部门列表里的下标
};

bool SiblingLess(const SiblingKey& a, const SiblingKey& b)
{
    if (a.m_order != b.m_order)
        return a.m_order < b.m_order;
    return a.m_index < b.m_index;
}

//转小写，用于大小写不敏感的匹配
std::wstring Lower(const std::wstring& text)
{
    std::wstring out(text);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<wchar_t>(towlower(out[i]));
    return out;
}

//部门ID → 在部门列表里的下标；同一个 ID 出现多次时只认第一个
std::map<int64_t, size_t> IndexById(const std::vector<net::DepartmentItem>& departments)
{
    std::map<int64_t, size_t> index;
    for (size_t i = 0; i < departments.size(); ++i)
    {
        if (index.find(departments[i].m_deptid) == index.end())
            index[departments[i].m_deptid] = i;
    }
    return index;
}

//是否根部门：父部门为 0，或父部门不在部门列表里
bool IsRoot(const net::DepartmentItem& dept, const std::map<int64_t, size_t>& index)
{
    return dept.m_parentdeptid == 0 || index.find(dept.m_parentdeptid) == index.end();
}

//按同级排序规则把一组部门下标排好
void SortSiblings(const std::vector<net::DepartmentItem>& departments, std::vector<size_t>& indexes)
{
    std::vector<SiblingKey> keys;
    for (size_t i = 0; i < indexes.size(); ++i)
    {
        SiblingKey k;
        k.m_order = departments[indexes[i]].m_deptorder;
        k.m_index = indexes[i];
        keys.push_back(k);
    }
    std::stable_sort(keys.begin(), keys.end(), SiblingLess);
    for (size_t i = 0; i < keys.size(); ++i)
        indexes[i] = keys[i].m_index;
}

/**
 *  建树时共用的数据，免得递归函数带一长串参数。
 */
struct TreeContext
{
    const std::vector<net::DepartmentItem>*   m_departments;   // 部门列表，借用
    std::map<int64_t, std::vector<size_t> >   m_children;      // 父部门ID → 直属下级部门的下标（已排好序）
    std::set<size_t>                          m_keep;          // 要显示的部门下标（不搜索时为全部）
    std::set<size_t>                          m_matched;       // 名称命中搜索词的部门下标（不搜索时为全部）
    std::set<size_t>                          m_visited;       // 已经输出过的部门下标，防止数据成环时死循环
    std::vector<DeptPickNode>*                m_out;           // 输出，借用
};

//深度优先输出一个部门及其下级部门
void Emit(TreeContext& ctx, size_t index, int depth, int parent)
{
    if (ctx.m_keep.find(index) == ctx.m_keep.end() || !ctx.m_visited.insert(index).second)
        return;
    const net::DepartmentItem& dept = (*ctx.m_departments)[index];
    DeptPickNode node;
    node.m_depth = depth;
    node.m_parent = parent;
    node.m_deptId = dept.m_deptid;
    node.m_name = EncodeUtil::Utf8ToUnicode(dept.m_name);
    node.m_matched = ctx.m_matched.find(index) != ctx.m_matched.end();
    ctx.m_out->push_back(node);
    const int self = static_cast<int>(ctx.m_out->size()) - 1;
    std::map<int64_t, std::vector<size_t> >::const_iterator it = ctx.m_children.find(dept.m_deptid);
    if (it == ctx.m_children.end())
        return;
    for (size_t i = 0; i < it->second.size(); ++i)
        Emit(ctx, it->second[i], depth + 1, self);
}

} // anonymous namespace

std::vector<DeptPickNode> BuildDeptPickTree(const std::vector<net::DepartmentItem>& departments, const std::wstring& filter)
{
    std::vector<DeptPickNode> out;
    const std::map<int64_t, size_t> index = IndexById(departments);

    TreeContext ctx;
    ctx.m_departments = &departments;
    ctx.m_out = &out;
    std::vector<size_t> roots;
    for (size_t i = 0; i < departments.size(); ++i)
    {
        //重复的部门ID只认第一个
        if (index.find(departments[i].m_deptid)->second != i)
            continue;
        if (IsRoot(departments[i], index))
            roots.push_back(i);
        else
            ctx.m_children[departments[i].m_parentdeptid].push_back(i);
    }
    SortSiblings(departments, roots);
    for (std::map<int64_t, std::vector<size_t> >::iterator it = ctx.m_children.begin(); it != ctx.m_children.end(); ++it)
        SortSiblings(departments, it->second);

    //要显示哪些部门：不搜索时全部；搜索时名称命中的部门及其全部上级部门
    const std::wstring needle = Lower(filter);
    for (std::map<int64_t, size_t>::const_iterator it = index.begin(); it != index.end(); ++it)
    {
        const size_t i = it->second;
        if (!needle.empty() && Lower(EncodeUtil::Utf8ToUnicode(departments[i].m_name)).find(needle) == std::wstring::npos)
            continue;
        ctx.m_matched.insert(i);
        //沿父部门链往上把上级都留下；遇到已留下的或数据成环时停止
        size_t cur = i;
        while (ctx.m_keep.insert(cur).second && !IsRoot(departments[cur], index))
            cur = index.find(departments[cur].m_parentdeptid)->second;
    }

    for (size_t i = 0; i < roots.size(); ++i)
        Emit(ctx, roots[i], 0, -1);
    //数据成环时环上的部门从任何根部门都走不到，按列表顺序把它们当作根部门补上，每个只出现一次
    for (size_t i = 0; i < departments.size(); ++i)
    {
        if (index.find(departments[i].m_deptid)->second == i)
            Emit(ctx, i, 0, -1);
    }
    return out;
}

std::vector<int64_t> NormalizeDeptSelection(const std::vector<int64_t>& selected,
                                            const std::vector<net::DepartmentItem>& departments)
{
    const std::map<int64_t, size_t> index = IndexById(departments);
    std::set<int64_t> chosen;
    for (size_t i = 0; i < selected.size(); ++i)
    {
        if (index.find(selected[i]) != index.end())
            chosen.insert(selected[i]);
    }

    std::vector<int64_t> out;
    std::set<int64_t> seen;
    for (size_t i = 0; i < selected.size(); ++i)
    {
        const int64_t id = selected[i];
        //不在部门列表里（部门已删除），或重复
        if (chosen.find(id) == chosen.end() || !seen.insert(id).second)
            continue;
        //沿父部门链往上收集上级部门，到根部门或出现重复（数据成环）为止
        std::vector<int64_t> ancestors;
        std::set<int64_t> walked;
        walked.insert(id);
        bool onCycle = false;
        const net::DepartmentItem* dept = &departments[index.find(id)->second];
        while (!IsRoot(*dept, index))
        {
            const int64_t parent = dept->m_parentdeptid;
            if (!walked.insert(parent).second)
            {
                //走回到了自己：本部门处在环上，环上的部门互为上级，不能按「上级已含」去掉，否则会全部丢失
                onCycle = (parent == id);
                break;
            }
            ancestors.push_back(parent);
            dept = &departments[index.find(parent)->second];
        }
        //有上级也被选中时，这个部门已被上级包含
        bool covered = false;
        for (size_t k = 0; k < ancestors.size() && !onCycle; ++k)
        {
            if (chosen.find(ancestors[k]) != chosen.end())
            {
                covered = true;
                break;
            }
        }
        if (!covered)
            out.push_back(id);
    }
    return out;
}

std::wstring DeptNameOf(const std::vector<net::DepartmentItem>& departments, int64_t deptId)
{
    for (size_t i = 0; i < departments.size(); ++i)
    {
        if (departments[i].m_deptid == deptId)
            return EncodeUtil::Utf8ToUnicode(departments[i].m_name);
    }
    return std::wstring();
}

} // namespace balloonmain
