/**
 *  联系人部门信息表的实现。整体说明见 ContactDeptTable.h。
 *
 *  做法：
 *    1. 按数组顺序扫描一遍全部部门的成员记录，为每个人记下"第一次出现的部门与职务"和
 *       "第一次作为主部门成员出现的部门与职务"，两者择一；
 *    2. 每个部门的路径只在第一次用到时拼一次，结果按部门缓存。
 *  这与原先逐人计算的结果一致：原算法对每个人都按同样的数组顺序找"第一个主部门、否则
 *  第一个所在部门"，路径也是同样沿上级上溯、最多 kDeptPathMaxDepth 层。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "ContactDeptTable.h"

namespace balloonmain {

const char* const kDeptPathSeparator = " / ";

namespace {

// 扫描过程中每个人的中间结果。部门以其在数组中的下标记录，-1 表示尚未找到。
struct MemberChoice
{
    int         m_firstDept;       // 第一次出现的部门下标
    std::string m_firstPosition;   // 第一次出现那条记录的职务
    int         m_primaryDept;     // 第一次作为主部门成员出现的部门下标
    std::string m_primaryPosition; // 那条主部门记录的职务

    MemberChoice()
        : m_firstDept(-1)
        , m_primaryDept(-1)
    {
    }
};

// 沿上级上溯拼出某个部门的路径（从顶级到该部门）。
//   depts：部门数组；index：起点部门的下标；byId：deptid → 下标。
std::string BuildDeptPath(const std::vector<net::DepartmentItem>& depts, int index,
                          const std::map<int64_t, int>& byId)
{
    std::vector<const std::string*> chain;
    int cur = index;
    int guard = 0;
    //guard 防止异常数据成环时无限上溯，最多 kDeptPathMaxDepth 层，与原算法相同
    while (cur >= 0 && guard < kDeptPathMaxDepth)
    {
        chain.push_back(&depts[(size_t)cur].m_name);
        const int64_t parent = depts[(size_t)cur].m_parentdeptid;
        if (parent == 0)
        {
            break;
        }
        std::map<int64_t, int>::const_iterator it = byId.find(parent);
        cur = (it != byId.end()) ? it->second : -1;
        ++guard;
    }

    std::string path;
    for (size_t i = chain.size(); i > 0; --i)
    {
        if (!path.empty())
        {
            path += kDeptPathSeparator;
        }
        path += *chain[i - 1];
    }
    return path;
}

} // anonymous namespace

void BuildContactDeptTable(const std::vector<net::DepartmentItem>& depts,
                           std::map<unsigned int, ContactDeptInfo>& out)
{
    out.clear();

    //deptid → 下标。同一个 deptid 出现多次时后者覆盖前者，与原算法 byId[...] = 的写法一致
    std::map<int64_t, int> byId;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        byId[depts[i].m_deptid] = (int)i;
    }

    //一遍扫描，为每个人选出部门与职务
    std::map<unsigned int, MemberChoice> choices;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        const std::vector<net::DeptMemberItem>& ms = depts[i].m_members;
        for (size_t k = 0; k < ms.size(); ++k)
        {
            MemberChoice& c = choices[(unsigned int)ms[k].m_userid];
            if (c.m_primaryDept >= 0)
            {
                continue;   //已找到主部门，原算法在此即停止查找
            }
            if (c.m_firstDept < 0)
            {
                c.m_firstDept     = (int)i;
                c.m_firstPosition = ms[k].m_position;
            }
            if (ms[k].m_isprimary == 1)
            {
                c.m_primaryDept     = (int)i;
                c.m_primaryPosition = ms[k].m_position;
            }
        }
    }

    //每个部门的路径只拼一次
    std::map<int, std::string> pathCache;
    for (std::map<unsigned int, MemberChoice>::const_iterator it = choices.begin(); it != choices.end(); ++it)
    {
        const MemberChoice& c = it->second;
        const bool usePrimary = (c.m_primaryDept >= 0);
        const int  deptIndex  = usePrimary ? c.m_primaryDept : c.m_firstDept;

        std::map<int, std::string>::iterator pit = pathCache.find(deptIndex);
        if (pit == pathCache.end())
        {
            pit = pathCache.insert(std::make_pair(deptIndex, BuildDeptPath(depts, deptIndex, byId))).first;
        }

        ContactDeptInfo& info = out[it->first];
        info.m_position = usePrimary ? c.m_primaryPosition : c.m_firstPosition;
        info.m_deptPath = pit->second;
    }
}

} // namespace balloonmain
