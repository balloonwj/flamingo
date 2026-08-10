/**
 *  flamingoAdmin —— 组织架构树组装（Util/OrgTreeBuilder）单元测试。
 *
 *  覆盖 orgtree::BuildOrgTree 的组装规则，重点是"公司根部门"这一层：
 *    - 传入真实的公司根部门 ID 时，树根就是那个真实部门节点，可以直接拿它的 ID
 *      作为新增部门 / 新增员工的父部门发给服务端；
 *    - 顶级部门挂在公司根之下，成员挂在各自部门之下，子部门排在成员之前；
 *    - 公司根节点被标记为 m_isCompanyRoot，界面据此禁止删除该节点；
 *    - 企业尚未建立公司根部门（传 0）时退回合成根节点，行为与迁移前一致。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/OrgTreeBuilder.h"
#include "Widgets/OrgTree.h"

#include <vector>

namespace flamingoadmin { namespace test {

namespace {

// 测试用的部门 ID。取值刻意跨越不同位数，用于暴露"按字符串前缀判断父子关系"
// 这类实现错误（本模块按 ID 相等判断，不应受位数影响）。
const long long kRootDeptId   = 1610612737;   // 公司根部门
const long long kTechDeptId   = 1610612738;   // 技术中心（挂在公司根下）
const long long kFrontDeptId  = 16106127380;  // 前端组（挂在技术中心下，位数多一位）
const long long kMarketDeptId = 1610612739;   // 市场部（与技术中心同级，用于排序用例）

// 造一个部门原始数据。成员由调用方另行追加。
orgtree::OrgDeptRaw MakeRawDept(long long deptId, long long parentDeptId,
                                int level, int order, const CString& name)
{
    orgtree::OrgDeptRaw raw;
    raw.m_deptId       = deptId;
    raw.m_parentDeptId = parentDeptId;
    raw.m_level        = level;
    raw.m_order        = order;
    raw.m_name         = name;
    return raw;
}

// 造一个员工节点。
OrgNode MakeUser(const CString& userId, const CString& name)
{
    OrgNode user;
    user.m_kind = OrgNodeKind::User;
    user.m_id   = userId;
    user.m_name = name;
    return user;
}

// 造一份三层的部门数据：公司根 → 技术中心 → 前端组。
// 公司根下直接挂一名员工（这正是"员工直接挂在公司下"的形态），
// 前端组下挂一名员工。刻意把父部门排在子部门之后，验证组装不依赖输入顺序。
std::vector<orgtree::OrgDeptRaw> MakeSampleDepts()
{
    orgtree::OrgDeptRaw front = MakeRawDept(kFrontDeptId, kTechDeptId, 3, 0, _T("前端组"));
    front.m_members.push_back(MakeUser(_T("u-101"), _T("蒋灵")));

    orgtree::OrgDeptRaw tech = MakeRawDept(kTechDeptId, kRootDeptId, 2, 0, _T("技术中心"));

    orgtree::OrgDeptRaw root = MakeRawDept(kRootDeptId, 0, 1, 0, _T("明远科技有限公司"));
    root.m_members.push_back(MakeUser(_T("u-001"), _T("秦致远")));

    std::vector<orgtree::OrgDeptRaw> depts;
    depts.push_back(front);   // 子部门先入
    depts.push_back(root);    // 根部门居中
    depts.push_back(tech);    // 父部门后入
    return depts;
}

// 把某个部门 ID 转成节点 ID 字符串，便于断言。
CString DeptIdText(long long deptId)
{
    CString text;
    text.Format(_T("%lld"), deptId);
    return text;
}

// 合成根节点的 ID。取成变量而不是在断言里直接写 CString(orgtree::kSyntheticRootId)：
// 后者出现在语句开头时会被编译器按函数声明解析（C2751），编不过。
CString SyntheticRootId()
{
    return CString(orgtree::kSyntheticRootId);
}

} // anonymous namespace

void RunAllOrgTreeBuilderTests()
{
    // ---- 用例 1：树根就是那个真实的公司根部门 ----
    BeginCase("OrgTreeBuilder.RealRoot_UsesCompanyDept:树根为真实公司部门");
    {
        const OrgNode root = orgtree::BuildOrgTree(
            MakeSampleDepts(), kRootDeptId, _T("不应该用到的企业名"));

        // 根节点 ID 必须是真实部门 ID：新增员工时正是拿它当父部门发给服务端，
        // 若这里退回了合成的 "root"，服务端就会收到部门 ID 0。
        FA_EXPECT_STREQ(root.m_id, DeptIdText(kRootDeptId));
        // 名字取自部门本身（服务端保证它与企业全称一致），而非传入的企业名参数。
        FA_EXPECT_STREQ(root.m_name, _T("明远科技有限公司"));
        FA_EXPECT_TRUE(root.m_kind == OrgNodeKind::Dept);
        FA_EXPECT_TRUE(root.m_isCompanyRoot);
    }

    // ---- 用例 2：子部门排在成员之前，员工可直接挂在公司根下 ----
    BeginCase("OrgTreeBuilder.RealRoot_ChildrenOrder:子部门在前成员在后");
    {
        const OrgNode root = orgtree::BuildOrgTree(
            MakeSampleDepts(), kRootDeptId, CString());

        // 公司根下应当是：技术中心（部门）+ 秦致远（直接挂在公司下的员工）。
        FA_EXPECT_EQ(static_cast<int>(root.m_children.size()), 2);
        if (root.m_children.size() == 2)
        {
            FA_EXPECT_STREQ(root.m_children[0].m_id, DeptIdText(kTechDeptId));
            FA_EXPECT_TRUE(root.m_children[0].m_kind == OrgNodeKind::Dept);
            FA_EXPECT_STREQ(root.m_children[1].m_id, _T("u-001"));
            FA_EXPECT_TRUE(root.m_children[1].m_kind == OrgNodeKind::User);
        }
    }

    // ---- 用例 3：多层父子关系正确还原，且不受输入顺序影响 ----
    BeginCase("OrgTreeBuilder.RealRoot_NestedDepts:多层部门正确挂接");
    {
        const OrgNode root = orgtree::BuildOrgTree(
            MakeSampleDepts(), kRootDeptId, CString());

        // 层层断言时不提前 return，否则会连带跳过本函数后面的用例。
        FA_EXPECT_EQ(static_cast<int>(root.m_children.size()), 2);
        if (root.m_children.size() == 2)
        {
            const OrgNode& tech = root.m_children[0];
            FA_EXPECT_EQ(static_cast<int>(tech.m_children.size()), 1);
            if (tech.m_children.size() == 1)
            {
                const OrgNode& front = tech.m_children[0];
                FA_EXPECT_STREQ(front.m_id, DeptIdText(kFrontDeptId));
                FA_EXPECT_STREQ(front.m_name, _T("前端组"));
                // 子部门本身不是公司根，界面上应当允许删除。
                FA_EXPECT_FALSE(front.m_isCompanyRoot);

                FA_EXPECT_EQ(static_cast<int>(front.m_children.size()), 1);
                if (front.m_children.size() == 1)
                {
                    FA_EXPECT_STREQ(front.m_children[0].m_id, _T("u-101"));
                }
            }
        }
    }

    // ---- 用例 4：企业尚未建立公司根部门时退回合成根节点 ----
    BeginCase("OrgTreeBuilder.NoRoot_FallsBackToSynthetic:无根部门时退回合成根");
    {
        // 不含公司根部门的一份数据：只有一个顶级部门（父部门为 0）。
        std::vector<orgtree::OrgDeptRaw> depts;
        orgtree::OrgDeptRaw tech = MakeRawDept(kTechDeptId, 0, 1, 0, _T("技术中心"));
        tech.m_members.push_back(MakeUser(_T("u-101"), _T("蒋灵")));
        depts.push_back(tech);

        const OrgNode root = orgtree::BuildOrgTree(depts, 0, _T("青鸟物流集团"));

        // 退回合成节点：ID 是约定字符串，名字取传入的企业名。
        FA_EXPECT_STREQ(root.m_id, SyntheticRootId());
        FA_EXPECT_STREQ(root.m_name, _T("青鸟物流集团"));
        // 合成节点同样代表企业本身，一样不允许删除。
        FA_EXPECT_TRUE(root.m_isCompanyRoot);
        FA_EXPECT_EQ(static_cast<int>(root.m_children.size()), 1);
        if (root.m_children.size() == 1)
        {
            FA_EXPECT_STREQ(root.m_children[0].m_id, DeptIdText(kTechDeptId));
        }
    }

    // ---- 用例 5：rootDeptId 指向的部门不在数组里时同样退回合成根 ----
    BeginCase("OrgTreeBuilder.RootNotInList_FallsBack:根部门缺失时退回合成根");
    {
        std::vector<orgtree::OrgDeptRaw> depts;
        depts.push_back(MakeRawDept(kTechDeptId, 0, 1, 0, _T("技术中心")));

        // 企业记录说根部门是 kRootDeptId，但部门数组里没有它（数据不一致）。
        const OrgNode root = orgtree::BuildOrgTree(depts, kRootDeptId, _T("青鸟物流集团"));

        FA_EXPECT_STREQ(root.m_id, SyntheticRootId());
        // 关键：部门不能因为找不到父节点而从树上整棵丢失。
        FA_EXPECT_EQ(static_cast<int>(root.m_children.size()), 1);
    }

    // ---- 用例 6：父部门不存在的孤儿部门挂到根下，不丢失 ----
    BeginCase("OrgTreeBuilder.StrayDept_AttachedToRoot:孤儿部门挂到根下");
    {
        std::vector<orgtree::OrgDeptRaw> depts;
        depts.push_back(MakeRawDept(kRootDeptId, 0, 1, 0, _T("明远科技有限公司")));
        // 父部门 999 不在数组里。
        depts.push_back(MakeRawDept(kTechDeptId, 999, 2, 0, _T("技术中心")));

        const OrgNode root = orgtree::BuildOrgTree(depts, kRootDeptId, CString());

        FA_EXPECT_STREQ(root.m_id, DeptIdText(kRootDeptId));
        FA_EXPECT_EQ(static_cast<int>(root.m_children.size()), 1);
        if (root.m_children.size() == 1)
        {
            FA_EXPECT_STREQ(root.m_children[0].m_id, DeptIdText(kTechDeptId));
        }
    }

    // ---- 用例 7：同级部门按排序权重升序 ----
    BeginCase("OrgTreeBuilder.SiblingOrder_SortedByOrder:同级按排序权重升序");
    {
        std::vector<orgtree::OrgDeptRaw> depts;
        depts.push_back(MakeRawDept(kRootDeptId, 0, 1, 0, _T("明远科技有限公司")));
        // 刻意让排序权重大的先入数组。
        depts.push_back(MakeRawDept(kMarketDeptId, kRootDeptId, 2, 5, _T("市场部")));
        depts.push_back(MakeRawDept(kTechDeptId, kRootDeptId, 2, 1, _T("技术中心")));

        const OrgNode root = orgtree::BuildOrgTree(depts, kRootDeptId, CString());

        FA_EXPECT_EQ(static_cast<int>(root.m_children.size()), 2);
        if (root.m_children.size() == 2)
        {
            FA_EXPECT_STREQ(root.m_children[0].m_name, _T("技术中心"));
            FA_EXPECT_STREQ(root.m_children[1].m_name, _T("市场部"));
        }
    }

    // ---- 用例 8：空部门数组时仍给出一个公司节点 ----
    BeginCase("OrgTreeBuilder.EmptyDepts_StillHasCompanyNode:无部门时仍有公司节点");
    {
        const OrgNode root = orgtree::BuildOrgTree(
            std::vector<orgtree::OrgDeptRaw>(), 0, _T("青鸟物流集团"));

        FA_EXPECT_STREQ(root.m_name, _T("青鸟物流集团"));
        FA_EXPECT_TRUE(root.m_isCompanyRoot);
        FA_EXPECT_EQ(static_cast<int>(root.m_children.size()), 0);
    }

    // ---- 用例 9：组织树控件在公司节点上不给出删除动作 ----
    BeginCase("OrgTree.CompanyRoot_NoDeleteAction:公司节点只给新增不给删除");
    {
        // 树在整个用例期间必须存活：OrgTree 只借用根指针，不接管所有权。
        OrgNode root = orgtree::BuildOrgTree(MakeSampleDepts(), kRootDeptId, CString());

        OrgTree tree;
        tree.SetRoot(&root);

        // 第 0 行是公司节点：只有"新增子部门"和"新增员工"，没有删除。
        const std::vector<OrgTree::Action> rootActions = tree.Test_RowActions(0);
        FA_EXPECT_EQ(static_cast<int>(rootActions.size()), 2);
        if (rootActions.size() == 2)
        {
            FA_EXPECT_TRUE(rootActions[0] == OrgTree::Action::AddDept);
            FA_EXPECT_TRUE(rootActions[1] == OrgTree::Action::AddUser);
        }

        // 第 1 行是技术中心：普通部门，除两个新增动作外还应当有删除。
        const std::vector<OrgTree::Action> deptActions = tree.Test_RowActions(1);
        FA_EXPECT_EQ(static_cast<int>(deptActions.size()), 3);
        if (deptActions.size() == 3)
        {
            FA_EXPECT_TRUE(deptActions[2] == OrgTree::Action::Delete);
        }
    }
}

}} // namespace flamingoadmin::test
