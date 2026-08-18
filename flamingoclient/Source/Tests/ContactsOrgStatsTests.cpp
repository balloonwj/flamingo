/**
 *  ContactsOrgStatsTests.cpp
 *
 *  通讯录组织架构统计（BalloonMain/ContactsOrgStats）的单元测试。
 *
 *  这一层值得测的原因：联系人页要在公司根节点后面显示"· N 人"，而"哪个部门代表
 *  公司"在 PC 客户端只能靠"名字等于企业全称"外加"顶层只有一棵树"兜底来判断
 *  （1201 应答不下发 rootdeptid），人数又要沿多级部门递归去重。这些分支靠手工点
 *  界面几乎覆盖不全 —— 存量数据没有公司根部门、企业刚改名部门名还没同步、成员
 *  兼属多个部门、脏数据把父子关系连成环，每一种都得单独构造数据才能撞上。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/ContactsOrgStats.h"

using namespace balloonmain;

// =====================================================================
// 构造测试数据的小工具
// =====================================================================

// 按 部门id / 部门名 / 父部门id 造一个部门（成员随后用 AddMember 追加）。
static net::DepartmentItem MakeDept(int64_t deptId, const char* name, int64_t parentDeptId)
{
    net::DepartmentItem d;
    d.m_deptid = deptId;
    d.m_name = name;
    d.m_parentdeptid = parentDeptId;
    return d;
}

// 往部门里塞一个成员（只关心 userid，其余字段与统计无关）。
static void AddMember(net::DepartmentItem& dept, int64_t userId)
{
    net::DeptMemberItem m;
    m.m_userid = userId;
    dept.m_members.push_back(m);
}

// 造一棵贴近真实数据的组织架构：
//   明远科技有限公司(1)          直属 2 人：101 102
//     └ 技术中心(2)              直属 1 人：103
//         └ 研发部(3)            直属 1 人：104
//             └ 前端组(4)        直属 2 人：105 106
//   市场部(5) —— 与公司节点<u>不</u>相连的另一棵顶层树，直属 1 人：107
// 去重后公司这棵树共 6 人（101–106），市场部那棵 1 人。
static std::vector<net::DepartmentItem> MakeSampleOrg()
{
    std::vector<net::DepartmentItem> depts;

    net::DepartmentItem company = MakeDept(1, "明远科技有限公司", 0);
    AddMember(company, 101);
    AddMember(company, 102);

    net::DepartmentItem center = MakeDept(2, "技术中心", 1);
    AddMember(center, 103);

    net::DepartmentItem rd = MakeDept(3, "研发部", 2);
    AddMember(rd, 104);

    net::DepartmentItem front = MakeDept(4, "前端组", 3);
    AddMember(front, 105);
    AddMember(front, 106);

    net::DepartmentItem market = MakeDept(5, "市场部", 0);
    AddMember(market, 107);

    // 故意让父部门排在子部门后面，验证实现不依赖数组顺序。
    depts.push_back(front);
    depts.push_back(rd);
    depts.push_back(center);
    depts.push_back(company);
    depts.push_back(market);
    return depts;
}

// 在数组里按部门 id 找下标（断言用）。
static int IndexOfDept(const std::vector<net::DepartmentItem>& depts, int64_t deptId)
{
    for (size_t i = 0; i < depts.size(); ++i)
    {
        if (depts[i].m_deptid == deptId)
        {
            return (int)i;
        }
    }
    return -1;
}

// =====================================================================
// A. FindCompanyRootDeptIndex —— 认出代表"公司本身"的那个根部门
// =====================================================================

TEST_CASE("O1 顶层部门里名字等于企业全称的那个即公司根部门", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts = MakeSampleOrg();

    const int idx = FindCompanyRootDeptIndex(depts, "明远科技有限公司");
    REQUIRE(idx >= 0);
    CHECK(depts[idx].m_deptid == 1);
}

TEST_CASE("O2 有多个顶层部门时按名字命中，而不是取排在最前的那个", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts = MakeSampleOrg();

    // 样例数据里"市场部"也是顶层，且"前端组"排在数组最前面 —— 命中的必须是公司。
    const int idx = FindCompanyRootDeptIndex(depts, "明远科技有限公司");
    REQUIRE(idx >= 0);
    CHECK(depts[idx].m_name == "明远科技有限公司");
    CHECK(idx != 0);
}

TEST_CASE("O3 企业名为空但顶层只有一棵树时，兜底认定它是公司节点", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "明远科技有限公司", 0));
    depts.push_back(MakeDept(2, "技术中心", 1));

    const int idx = FindCompanyRootDeptIndex(depts, std::string());
    REQUIRE(idx >= 0);
    CHECK(depts[idx].m_deptid == 1);
}

TEST_CASE("O4 企业名为空且顶层有多棵树时认不出公司节点", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "技术中心", 0));
    depts.push_back(MakeDept(2, "市场部", 0));

    CHECK(FindCompanyRootDeptIndex(depts, std::string()) == -1);
}

TEST_CASE("O5 企业刚改名、部门名还没同步时，顶层唯一仍能兜底认出", "[contacts][orgstats]")
{
    // 企业已改叫"明远科技集团"，公司根部门名字还停在旧称。
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "明远科技有限公司", 0));
    depts.push_back(MakeDept(2, "技术中心", 1));

    const int idx = FindCompanyRootDeptIndex(depts, "明远科技集团");
    REQUIRE(idx >= 0);
    CHECK(depts[idx].m_deptid == 1);
}

TEST_CASE("O6 存量数据没有公司根部门（多个顶层且都不叫企业名）时返回 -1", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "技术中心", 0));
    depts.push_back(MakeDept(2, "市场部", 0));
    depts.push_back(MakeDept(3, "研发部", 1));

    CHECK(FindCompanyRootDeptIndex(depts, "明远科技有限公司") == -1);
}

TEST_CASE("O7 部门数组为空时返回 -1", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts;

    CHECK(FindCompanyRootDeptIndex(depts, "明远科技有限公司") == -1);
    CHECK(FindCompanyRootDeptIndex(depts, std::string()) == -1);
}

TEST_CASE("O8 父部门没随应答下发的部门按顶层对待", "[contacts][orgstats]")
{
    // 研发部的父部门 99 不在数组里 —— 它在客户端看来就是一棵树的根。
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(3, "研发部", 99));

    const int idx = FindCompanyRootDeptIndex(depts, std::string());
    REQUIRE(idx >= 0);
    CHECK(depts[idx].m_deptid == 3);
}

// =====================================================================
// B. CountDeptMembersRecursive —— 部门连同下级部门的去重人数
// =====================================================================

TEST_CASE("O9 公司节点的人数是整棵子树的成员之和", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts = MakeSampleOrg();

    // 公司(1) 这棵树：101 102 103 104 105 106，共 6 人；市场部那棵不计入。
    CHECK(CountDeptMembersRecursive(depts, 1) == 6);
}

TEST_CASE("O10 只统计给定子树，不把兄弟子树算进来", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts = MakeSampleOrg();

    CHECK(CountDeptMembersRecursive(depts, 2) == 4);   // 技术中心：103 104 105 106
    CHECK(CountDeptMembersRecursive(depts, 4) == 2);   // 前端组：105 106
    CHECK(CountDeptMembersRecursive(depts, 5) == 1);   // 市场部：107
}

TEST_CASE("O11 兼属多个部门的人只计一次", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts;

    net::DepartmentItem company = MakeDept(1, "明远科技有限公司", 0);
    net::DepartmentItem rd = MakeDept(2, "研发部", 1);
    net::DepartmentItem market = MakeDept(3, "市场部", 1);
    // 201 同时挂在研发部与市场部（主部门 + 兼属部门），202 只在研发部。
    AddMember(rd, 201);
    AddMember(rd, 202);
    AddMember(market, 201);

    depts.push_back(company);
    depts.push_back(rd);
    depts.push_back(market);

    CHECK(CountDeptMembersRecursive(depts, 1) == 2);
}

TEST_CASE("O12 部门 id 不存在时返回 0", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts = MakeSampleOrg();

    CHECK(CountDeptMembersRecursive(depts, 12345) == 0);
}

TEST_CASE("O13 空部门（自身无人、也没有下级）返回 0", "[contacts][orgstats]")
{
    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "明远科技有限公司", 0));

    CHECK(CountDeptMembersRecursive(depts, 1) == 0);
}

TEST_CASE("O14 父子关系成环的脏数据不会死循环", "[contacts][orgstats]")
{
    // 1 的父是 2、2 的父是 1 —— 两个部门互为父子。
    std::vector<net::DepartmentItem> depts;
    net::DepartmentItem a = MakeDept(1, "A部", 2);
    net::DepartmentItem b = MakeDept(2, "B部", 1);
    AddMember(a, 301);
    AddMember(b, 302);
    depts.push_back(a);
    depts.push_back(b);

    // 只要求返回有限值、不卡死；环内两个部门的人都能数到。
    CHECK(CountDeptMembersRecursive(depts, 1) == 2);
}
