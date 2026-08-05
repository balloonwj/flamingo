/**
 *  MemberPickerTreeModelTests.cpp
 *
 *  选人对话框候选树模型（MemberPickerTreeModel）的纯函数测试。覆盖与用户
 *  对齐过的几类情形：
 *    MP1  多层部门嵌套 —— 层级、缩进、父子下标、同级排序都对；
 *    MP2  搜索过滤 —— 只留命中成员，过滤后空掉的部门整枝不出现，祖先部门保留；
 *    MP3  排除当前登录用户自己；
 *    MP4  没进任何部门的联系人落到末尾的"未分组"；部门列表为空时全员走这一支；
 *    MP5  部门数据成环（父子互指）不死循环；
 *    MP6  部门勾选态推导 —— 全选 / 半选 / 未选，以及子部门计入父部门；
 *    MP9  搜索的拼音支持 —— 首字母与全拼都能命中，音节内部起头不命中。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不绘制。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/MemberPickerTreeModel.h"

using namespace balloonmain;

namespace {

// 造一个联系人。nickname 用 UTF-8 字面量（本工程按 /utf-8 编译）。
net::ContactItem MakeContact(int64_t uid, const char* nickname,
                             const char* username = "")
{
    net::ContactItem c;
    c.m_userid   = uid;
    c.m_nickname = (nickname != NULL) ? nickname : "";
    c.m_username = (username != NULL) ? username : "";
    return c;
}

// 造一个部门。
net::DepartmentItem MakeDept(int64_t deptId, const char* name,
                             int64_t parentId, int order = 0)
{
    net::DepartmentItem d;
    d.m_deptid       = deptId;
    d.m_name         = (name != NULL) ? name : "";
    d.m_parentdeptid = parentId;
    d.m_deptorder    = order;
    return d;
}

// 往部门里塞一个成员。
void AddMember(net::DepartmentItem& dept, int64_t uid, int order = 0)
{
    net::DeptMemberItem m;
    m.m_userid      = uid;
    m.m_memberorder = order;
    dept.m_members.push_back(m);
}

// 在结果里按显示名找节点下标；找不到返回 -1。
int IndexOfLabel(const std::vector<MemberPickerNode>& nodes, LPCTSTR label)
{
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].m_label == CString(label))
        {
            return (int)i;
        }
    }
    return -1;
}

// 统计成员节点个数。
int CountMembers(const std::vector<MemberPickerNode>& nodes)
{
    int n = 0;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].m_kind == kPickerNodeMember)
        {
            ++n;
        }
    }
    return n;
}

} // anonymous namespace

TEST_CASE("MP1 多层部门嵌套：层级 / 父子 / 同级排序都正确", "[memberpicker]")
{
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(101, "张三"));
    contacts.push_back(MakeContact(102, "李四"));
    contacts.push_back(MakeContact(103, "王五"));

    std::vector<net::DepartmentItem> depts;
    // 公司(1) → 研发部(2, order=0) / 市场部(3, order=1)；研发部下再挂一个前端组(4)。
    depts.push_back(MakeDept(1, "明远科技", 0));
    net::DepartmentItem rd = MakeDept(2, "研发部", 1, 0);
    AddMember(rd, 101);
    depts.push_back(rd);
    net::DepartmentItem mk = MakeDept(3, "市场部", 1, 1);
    AddMember(mk, 103);
    depts.push_back(mk);
    net::DepartmentItem fe = MakeDept(4, "前端组", 2, 0);
    AddMember(fe, 102);
    depts.push_back(fe);

    std::vector<MemberPickerNode> nodes = BuildMemberPickerTree(contacts, depts, 0, CString());

    const int company = IndexOfLabel(nodes, _T("明远科技"));
    const int rdIdx   = IndexOfLabel(nodes, _T("研发部"));
    const int feIdx   = IndexOfLabel(nodes, _T("前端组"));
    const int mkIdx   = IndexOfLabel(nodes, _T("市场部"));
    REQUIRE(company >= 0);
    REQUIRE(rdIdx   >= 0);
    REQUIRE(feIdx   >= 0);
    REQUIRE(mkIdx   >= 0);

    // 层级：公司 0，两个一级部门 1，前端组 2。
    CHECK(nodes[company].m_depth == 0);
    CHECK(nodes[rdIdx].m_depth   == 1);
    CHECK(nodes[mkIdx].m_depth   == 1);
    CHECK(nodes[feIdx].m_depth   == 2);

    // 父子关系。
    CHECK(nodes[company].m_parent == -1);
    CHECK(nodes[rdIdx].m_parent   == company);
    CHECK(nodes[feIdx].m_parent   == rdIdx);

    // 同级按 deptorder：研发部(0) 在市场部(1) 前面。
    CHECK(rdIdx < mkIdx);

    // 三个人都在。
    CHECK(CountMembers(nodes) == 3);

    // 成员挂在自己那一层的部门下，深度比部门多 1。
    const int zhang = IndexOfLabel(nodes, _T("张三"));
    REQUIRE(zhang >= 0);
    CHECK(nodes[zhang].m_kind   == kPickerNodeMember);
    CHECK(nodes[zhang].m_userId == 101u);
    CHECK(nodes[zhang].m_parent == rdIdx);
    CHECK(nodes[zhang].m_depth  == nodes[rdIdx].m_depth + 1);
}

TEST_CASE("MP2 搜索过滤：空掉的部门整枝不出现，命中者的祖先保留", "[memberpicker]")
{
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(101, "张三", "zhangsan"));
    contacts.push_back(MakeContact(103, "王五", "wangwu"));

    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "明远科技", 0));
    net::DepartmentItem rd = MakeDept(2, "研发部", 1, 0);
    AddMember(rd, 101);
    depts.push_back(rd);
    net::DepartmentItem mk = MakeDept(3, "市场部", 1, 1);
    AddMember(mk, 103);
    depts.push_back(mk);

    // 只搜"张三"：市场部这一枝该整个消失，但公司与研发部要留着（否则张三无处可挂）。
    std::vector<MemberPickerNode> nodes =
        BuildMemberPickerTree(contacts, depts, 0, CString(_T("张三")));

    CHECK(CountMembers(nodes) == 1);
    CHECK(IndexOfLabel(nodes, _T("张三"))     >= 0);
    CHECK(IndexOfLabel(nodes, _T("研发部"))   >= 0);
    CHECK(IndexOfLabel(nodes, _T("明远科技")) >= 0);
    CHECK(IndexOfLabel(nodes, _T("市场部"))   < 0);
    CHECK(IndexOfLabel(nodes, _T("王五"))     < 0);

    // 按账号也能搜到，且大小写不敏感。
    std::vector<MemberPickerNode> byAccount =
        BuildMemberPickerTree(contacts, depts, 0, CString(_T("ZHANGSAN")));
    CHECK(CountMembers(byAccount) == 1);
    CHECK(IndexOfLabel(byAccount, _T("张三")) >= 0);

    // 谁都搜不到时结果为空数组，连公司节点都不留。
    std::vector<MemberPickerNode> none =
        BuildMemberPickerTree(contacts, depts, 0, CString(_T("查无此人")));
    CHECK(none.empty());
}

TEST_CASE("MP3 排除当前登录用户自己", "[memberpicker]")
{
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(101, "张三"));
    contacts.push_back(MakeContact(102, "我自己"));

    std::vector<net::DepartmentItem> depts;
    net::DepartmentItem rd = MakeDept(2, "研发部", 0);
    AddMember(rd, 101);
    AddMember(rd, 102);
    depts.push_back(rd);

    std::vector<MemberPickerNode> nodes =
        BuildMemberPickerTree(contacts, depts, 102u, CString());

    CHECK(CountMembers(nodes) == 1);
    CHECK(IndexOfLabel(nodes, _T("张三"))   >= 0);
    CHECK(IndexOfLabel(nodes, _T("我自己")) < 0);

    // selfUserId 传 0 表示不排除任何人。
    std::vector<MemberPickerNode> all =
        BuildMemberPickerTree(contacts, depts, 0, CString());
    CHECK(CountMembers(all) == 2);
}

TEST_CASE("MP4 没进部门的人落到未分组；无部门数据时全员走未分组", "[memberpicker]")
{
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(101, "张三"));
    contacts.push_back(MakeContact(104, "散人"));

    std::vector<net::DepartmentItem> depts;
    net::DepartmentItem rd = MakeDept(2, "研发部", 0);
    AddMember(rd, 101);
    depts.push_back(rd);

    std::vector<MemberPickerNode> nodes = BuildMemberPickerTree(contacts, depts, 0, CString());

    const int ungrouped = IndexOfLabel(nodes, _T("未分组"));
    const int sanren    = IndexOfLabel(nodes, _T("散人"));
    REQUIRE(ungrouped >= 0);
    REQUIRE(sanren    >= 0);
    CHECK(nodes[ungrouped].m_depth  == 0);
    CHECK(nodes[sanren].m_parent    == ungrouped);
    // 未分组排在所有部门后面。
    CHECK(ungrouped > IndexOfLabel(nodes, _T("研发部")));

    // 候选来自群成员（没有部门数据）：所有人都在未分组下。
    std::vector<net::DepartmentItem> noDepts;
    std::vector<MemberPickerNode> flat =
        BuildMemberPickerTree(contacts, noDepts, 0, CString());
    REQUIRE(flat.size() == 3u);            // 未分组 + 两个人
    CHECK(flat[0].m_kind  == kPickerNodeDept);
    CHECK(flat[0].m_label == CString(_T("未分组")));
    CHECK(CountMembers(flat) == 2);
}

TEST_CASE("MP5 部门数据成环时不死循环", "[memberpicker]")
{
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(101, "张三"));
    contacts.push_back(MakeContact(102, "李四"));

    // 脏数据：A 的父是 B，B 的父是 A。两者都不是"根"（父都在集合里），
    // 但环上的部门必须仍能展开一次，且不能无限递归。
    std::vector<net::DepartmentItem> depts;
    net::DepartmentItem a = MakeDept(1, "甲部", 2);
    AddMember(a, 101);
    depts.push_back(a);
    net::DepartmentItem b = MakeDept(2, "乙部", 1);
    AddMember(b, 102);
    depts.push_back(b);

    // 跑得完就是通过 —— 死循环会让测试挂住而不是失败。
    std::vector<MemberPickerNode> nodes = BuildMemberPickerTree(contacts, depts, 0, CString());

    // 环上没有根部门，两个人都进不了部门树，于是落到未分组，一个不丢。
    CHECK(CountMembers(nodes) == 2);
    CHECK(IndexOfLabel(nodes, _T("未分组")) >= 0);
}

TEST_CASE("MP6 勾选态推导：成员两态、部门三态、子部门计入父部门", "[memberpicker]")
{
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(101, "张三"));
    contacts.push_back(MakeContact(102, "李四"));

    std::vector<net::DepartmentItem> depts;
    depts.push_back(MakeDept(1, "明远科技", 0));
    net::DepartmentItem rd = MakeDept(2, "研发部", 1, 0);
    AddMember(rd, 101);
    depts.push_back(rd);
    net::DepartmentItem fe = MakeDept(3, "前端组", 2, 0);
    AddMember(fe, 102);
    depts.push_back(fe);

    std::vector<MemberPickerNode> nodes = BuildMemberPickerTree(contacts, depts, 0, CString());
    const int company = IndexOfLabel(nodes, _T("明远科技"));
    const int rdIdx   = IndexOfLabel(nodes, _T("研发部"));
    const int zhang   = IndexOfLabel(nodes, _T("张三"));
    const int li      = IndexOfLabel(nodes, _T("李四"));
    REQUIRE(company >= 0);
    REQUIRE(rdIdx   >= 0);
    REQUIRE(zhang   >= 0);
    REQUIRE(li      >= 0);

    // 一个都没选。
    std::set<UINT> selected;
    CHECK(DeriveCheckState(nodes, zhang,   selected) == kPickerUnchecked);
    CHECK(DeriveCheckState(nodes, company, selected) == kPickerUnchecked);

    // 只选张三：他自己选中；研发部下有张三(直属)与李四(子部门)，故半选；公司同理半选。
    selected.insert(101u);
    CHECK(DeriveCheckState(nodes, zhang,   selected) == kPickerChecked);
    CHECK(DeriveCheckState(nodes, li,      selected) == kPickerUnchecked);
    CHECK(DeriveCheckState(nodes, rdIdx,   selected) == kPickerPartial);
    CHECK(DeriveCheckState(nodes, company, selected) == kPickerPartial);

    // 两个都选：研发部与公司都变成全选。
    selected.insert(102u);
    CHECK(DeriveCheckState(nodes, rdIdx,   selected) == kPickerChecked);
    CHECK(DeriveCheckState(nodes, company, selected) == kPickerChecked);

    // 部门"全选"要收集到子部门的人：公司节点下应当收到两个 userid。
    std::vector<UINT> ids;
    CollectDeptMemberIds(nodes, company, ids);
    CHECK(ids.size() == 2u);

    // 下标越界不崩、按未选处理。
    CHECK(DeriveCheckState(nodes, -1, selected)                == kPickerUnchecked);
    CHECK(DeriveCheckState(nodes, (int)nodes.size(), selected) == kPickerUnchecked);
}


// =====================================================================
// MP9. 搜索框的拼音支持（首字母 + 全拼）
// =====================================================================

TEST_CASE("MP9 搜索支持拼音首字母与全拼", "[memberpicker]")
{
    // 账号刻意取成 u1000x，不含任何拼音串 —— 否则分不清命中的是账号子串还是拼音
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(10001, "苏沐辰", "u10001"));
    contacts.push_back(MakeContact(10002, "谢知行", "u10002"));
    contacts.push_back(MakeContact(10003, "王洋",   "u10003"));

    // 三个人放在同一个部门下：命中者的部门节点会保留，成员挂在它下面
    std::vector<net::DepartmentItem> depts;
    net::DepartmentItem dept = MakeDept(1, "测试组", 0);
    AddMember(dept, 10001);
    AddMember(dept, 10002);
    AddMember(dept, 10003);
    depts.push_back(dept);

    // 首字母
    std::vector<MemberPickerNode> byInitials =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("smc")));
    CHECK(CountMembers(byInitials) == 1);
    CHECK(IndexOfLabel(byInitials, _T("苏沐辰")) >= 0);
    CHECK(IndexOfLabel(byInitials, _T("测试组")) >= 0);

    // 全拼完整
    std::vector<MemberPickerNode> byFull =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("sumuchen")));
    CHECK(CountMembers(byFull) == 1);
    CHECK(IndexOfLabel(byFull, _T("苏沐辰")) >= 0);

    // 全拼前缀
    std::vector<MemberPickerNode> byPrefix =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("sumu")));
    CHECK(CountMembers(byPrefix) == 1);
    CHECK(IndexOfLabel(byPrefix, _T("苏沐辰")) >= 0);

    // 从中间那个字起头
    std::vector<MemberPickerNode> byMiddle =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("muchen")));
    CHECK(CountMembers(byMiddle) == 1);
    CHECK(IndexOfLabel(byMiddle, _T("苏沐辰")) >= 0);

    // 音节内部起头不命中（避免误联想）：一个人都没命中时连部门节点都不留
    CHECK(BuildMemberPickerTree(contacts, depts, 0u, CString(_T("umuch"))).empty());

    // 大小写不敏感
    std::vector<MemberPickerNode> byUpperInitials =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("SMC")));
    CHECK(CountMembers(byUpperInitials) == 1);
    CHECK(IndexOfLabel(byUpperInitials, _T("苏沐辰")) >= 0);

    std::vector<MemberPickerNode> byMixedCaseFull =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("WangYang")));
    CHECK(CountMembers(byMixedCaseFull) == 1);
    CHECK(IndexOfLabel(byMixedCaseFull, _T("王洋")) >= 0);

    // 另外两个人各自也能被自己的拼音搜到，且不会互相串
    std::vector<MemberPickerNode> xieByFull =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("xiezhixing")));
    CHECK(CountMembers(xieByFull) == 1);
    CHECK(IndexOfLabel(xieByFull, _T("谢知行")) >= 0);

    std::vector<MemberPickerNode> xieByInitials =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("xzx")));
    CHECK(CountMembers(xieByInitials) == 1);
    CHECK(IndexOfLabel(xieByInitials, _T("谢知行")) >= 0);

    // 账号仍然照旧参与匹配
    std::vector<MemberPickerNode> byAccount =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("u10003")));
    CHECK(CountMembers(byAccount) == 1);
    CHECK(IndexOfLabel(byAccount, _T("王洋")) >= 0);

    // 直接打汉字仍走原文子串匹配
    std::vector<MemberPickerNode> byHanzi =
        BuildMemberPickerTree(contacts, depts, 0u, CString(_T("沐")));
    CHECK(CountMembers(byHanzi) == 1);
    CHECK(IndexOfLabel(byHanzi, _T("苏沐辰")) >= 0);
}
