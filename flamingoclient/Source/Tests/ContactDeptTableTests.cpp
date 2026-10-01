/**
 *  联系人部门信息表的单元测试（Suite CDT），ContactDeptTableTests.cpp
 *
 *  覆盖 balloonmain::BuildContactDeptTable：
 *    CDT1  先出现在兼属部门、后出现在主部门 —— 取主部门，职务取主部门那一条；
 *    CDT2  没有主部门、出现在两个部门 —— 取第一个部门及其职务；
 *    CDT3  三级部门 —— 路径为"顶级 / 二级 / 三级"；
 *    CDT4  不在任何部门 —— 表里没有此人；
 *    CDT5  上级关系成环 —— 能正常结束，路径层数有上限；
 *    CDT6  上级部门 id 指向不存在的部门 —— 路径只包含已知的那几级；
 *    CDT7  随机数据下与原先"逐人计算"的算法逐人完全一致（原算法作为参照实现写在本文件里）。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include <random>

#include "catch_amalgamated.hpp"
#include "../UI/BalloonMain/ContactDeptTable.h"

using balloonmain::BuildContactDeptTable;
using balloonmain::ContactDeptInfo;
using balloonmain::kDeptPathMaxDepth;

namespace {

typedef std::map<unsigned int, ContactDeptInfo> DeptTable;

// 造一个部门。
net::DepartmentItem MakeDept(int64_t deptid, const char* name, int64_t parent)
{
    net::DepartmentItem d;
    d.m_deptid       = deptid;
    d.m_name         = name;
    d.m_parentdeptid = parent;
    return d;
}

// 往部门里加一个成员。
void AddMember(net::DepartmentItem& dept, int64_t userid, const char* position, int isprimary)
{
    net::DeptMemberItem m;
    m.m_userid    = userid;
    m.m_position  = position;
    m.m_isprimary = isprimary;
    dept.m_members.push_back(m);
}

// 原先主面板 ComputeContactDeptInfo 的算法（逐人计算），改为 UTF-8 std::string 版本，
// 作为 CDT7 的参照实现。逻辑逐行照搬，不做任何"顺手优化"。
void ReferenceDeptInfo(const std::vector<net::DepartmentItem>& depts, unsigned int userId,
                       std::string& outPosition, std::string& outDeptPath)
{
    outPosition.clear();
    outDeptPath.clear();

    std::map<int64_t, const net::DepartmentItem*> byId;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        byId[depts[i].m_deptid] = &depts[i];
    }

    const net::DepartmentItem* primaryDept = NULL;
    const net::DepartmentItem* anyDept = NULL;
    std::string position;
    for (size_t i = 0; i < depts.size() && primaryDept == NULL; ++i)
    {
        const std::vector<net::DeptMemberItem>& ms = depts[i].m_members;
        for (size_t k = 0; k < ms.size(); ++k)
        {
            if ((unsigned int)ms[k].m_userid != userId)
            {
                continue;
            }
            if (anyDept == NULL)
            {
                anyDept  = &depts[i];
                position = ms[k].m_position;
            }
            if (ms[k].m_isprimary == 1)
            {
                primaryDept = &depts[i];
                position    = ms[k].m_position;
                break;
            }
        }
    }
    const net::DepartmentItem* dept = (primaryDept != NULL) ? primaryDept : anyDept;
    if (dept == NULL)
    {
        return;
    }
    outPosition = position;

    std::vector<std::string> chain;
    const net::DepartmentItem* cur = dept;
    int guard = 0;
    while (cur != NULL && guard < kDeptPathMaxDepth)
    {
        chain.push_back(cur->m_name);
        if (cur->m_parentdeptid == 0)
        {
            break;
        }
        std::map<int64_t, const net::DepartmentItem*>::iterator it = byId.find(cur->m_parentdeptid);
        cur = (it != byId.end()) ? it->second : NULL;
        ++guard;
    }
    std::string path;
    for (size_t i = chain.size(); i > 0; --i)
    {
        if (!path.empty())
        {
            path += " / ";
        }
        path += chain[i - 1];
    }
    outDeptPath = path;
}

} // namespace

TEST_CASE("CDT1 先出现在兼属部门、后出现在主部门:取主部门及其职务", "[depttable]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "市场部", 0));
    depts.push_back(MakeDept(2, "研发中心", 0));
    AddMember(depts[0], 100001, "顾问", 0);
    AddMember(depts[1], 100001, "工程师", 1);

    DeptTable table;
    BuildContactDeptTable(depts, table);

    REQUIRE(table.count(100001) == 1);
    CHECK(table[100001].m_deptPath == "研发中心");
    CHECK(table[100001].m_position == "工程师");
}

TEST_CASE("CDT2 没有主部门、出现在两个部门:取第一个部门及其职务", "[depttable]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "市场部", 0));
    depts.push_back(MakeDept(2, "研发中心", 0));
    AddMember(depts[0], 100001, "顾问", 0);
    AddMember(depts[1], 100001, "工程师", 0);

    DeptTable table;
    BuildContactDeptTable(depts, table);

    REQUIRE(table.count(100001) == 1);
    CHECK(table[100001].m_deptPath == "市场部");
    CHECK(table[100001].m_position == "顾问");
}

TEST_CASE("CDT3 三级部门:路径为 顶级 / 二级 / 三级", "[depttable]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(3, "客户端组", 2));   // 故意把下级排在前面，路径仍须从顶级拼起
    depts.push_back(MakeDept(1, "研发中心", 0));
    depts.push_back(MakeDept(2, "终端部", 1));
    AddMember(depts[0], 100001, "组长", 1);
    AddMember(depts[1], 100002, "总监", 1);

    DeptTable table;
    BuildContactDeptTable(depts, table);

    CHECK(table[100001].m_deptPath == "研发中心 / 终端部 / 客户端组");
    CHECK(table[100002].m_deptPath == "研发中心");
}

TEST_CASE("CDT4 不在任何部门:表里没有此人", "[depttable]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "研发中心", 0));
    AddMember(depts[0], 100001, "工程师", 1);

    DeptTable table;
    table[999].m_deptPath = "旧内容";   // 原有内容须被清空
    BuildContactDeptTable(depts, table);

    CHECK(table.count(100002) == 0);
    CHECK(table.count(999) == 0);
    CHECK(table.size() == 1);
}

TEST_CASE("CDT5 上级关系成环:能正常结束,路径层数有上限", "[depttable]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "甲", 2));
    depts.push_back(MakeDept(2, "乙", 1));
    AddMember(depts[0], 100001, "工程师", 1);

    DeptTable table;
    BuildContactDeptTable(depts, table);

    REQUIRE(table.count(100001) == 1);
    // 与参照实现完全一致：环上的部门名交替出现，最多 kDeptPathMaxDepth 层。
    std::string refPos;
    std::string refPath;
    ReferenceDeptInfo(depts, 100001, refPos, refPath);
    CHECK(table[100001].m_deptPath == refPath);

    // 层数 = 分隔符个数 + 1，不超过上限。
    size_t separators = 0;
    for (size_t pos = refPath.find(" / "); pos != std::string::npos; pos = refPath.find(" / ", pos + 1))
    {
        ++separators;
    }
    CHECK((int)(separators + 1) <= kDeptPathMaxDepth);
}

TEST_CASE("CDT6 上级部门 id 指向不存在的部门:路径只包含已知的那几级", "[depttable]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(2, "终端部", 77));   // 77 不存在
    depts.push_back(MakeDept(3, "客户端组", 2));
    AddMember(depts[1], 100001, "组长", 1);

    DeptTable table;
    BuildContactDeptTable(depts, table);

    CHECK(table[100001].m_deptPath == "终端部 / 客户端组");
}

TEST_CASE("CDT7 随机数据下与逐人计算的原算法逐人完全一致", "[depttable]")
{
    const int kDeptCount   = 30;    // 部门数
    const int kUserCount   = 300;   // 人数
    const int kMemberships = 450;   // 成员记录数（多于人数，覆盖一人多部门）
    const int64_t kFirstUser = 100000;
    std::mt19937 rng(20261001);

    std::vector<net::DepartmentItem> depts;
    for (int i = 0; i < kDeptCount; ++i)
    {
        //前 5 个是顶级部门，其余随机挂在排在它前面的某个部门下
        const int64_t parent = (i < 5) ? 0 : (int64_t)(1 + rng() % (unsigned)i);
        std::string name = "部门" + std::to_string(i + 1);
        depts.push_back(MakeDept(i + 1, name.c_str(), parent));
    }
    for (int k = 0; k < kMemberships; ++k)
    {
        const int64_t userid = kFirstUser + (int64_t)(rng() % kUserCount);
        net::DepartmentItem& dept = depts[rng() % kDeptCount];
        std::string position = "职务" + std::to_string(k);
        AddMember(dept, userid, position.c_str(), (rng() % 3 == 0) ? 1 : 0);
    }

    DeptTable table;
    BuildContactDeptTable(depts, table);

    int mismatches = 0;
    for (int u = 0; u < kUserCount; ++u)
    {
        const unsigned int userid = (unsigned int)(kFirstUser + u);
        std::string refPos;
        std::string refPath;
        ReferenceDeptInfo(depts, userid, refPos, refPath);

        DeptTable::const_iterator it = table.find(userid);
        if (refPath.empty() && refPos.empty())
        {
            //参照实现认为此人不在任何部门：表里也不应有他
            if (it != table.end())
            {
                ++mismatches;
            }
            continue;
        }
        if (it == table.end() || it->second.m_deptPath != refPath || it->second.m_position != refPos)
        {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}
