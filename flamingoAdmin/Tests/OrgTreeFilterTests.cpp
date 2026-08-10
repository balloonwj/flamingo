/**
 *  flamingoAdmin —— 组织树 OrgTree 搜索过滤单元测试。
 *
 *  在【无 host】下裸构造 OrgTree,SetRoot 一棵小组织树后,验证 SetFilter:
 *    - 只保留"名称包含关键字"的节点及其上级路径,并强制展开;
 *    - 关键字无匹配时可见行为空;
 *    - 清空关键字恢复正常(默认展开)显示。
 *  断言走测试钩子 OrgTree::Test_VisibleNames()(按显示顺序返回节点名)。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Widgets/OrgTree.h"
#include "Mock/MockData.h"

#include <vector>

namespace flamingoadmin { namespace test {

namespace {

// 造一棵小组织树:
//   全公司(root)
//     ├ 研发部
//     │   ├ 张三
//     │   └ 李四
//     └ 市场部
//         └ 王五
// 返回根节点(自包含,子节点均为值拷贝)。
OrgNode MakeSampleOrg()
{
    OrgNode u1;
    u1.m_kind = OrgNodeKind::User;
    u1.m_id   = _T("u1");
    u1.m_name = _T("张三");

    OrgNode u2;
    u2.m_kind = OrgNodeKind::User;
    u2.m_id   = _T("u2");
    u2.m_name = _T("李四");

    OrgNode u3;
    u3.m_kind = OrgNodeKind::User;
    u3.m_id   = _T("u3");
    u3.m_name = _T("王五");

    OrgNode rd;
    rd.m_kind = OrgNodeKind::Dept;
    rd.m_id   = _T("d1");
    rd.m_name = _T("研发部");
    rd.m_children.push_back(u1);
    rd.m_children.push_back(u2);

    OrgNode mk;
    mk.m_kind = OrgNodeKind::Dept;
    mk.m_id   = _T("d2");
    mk.m_name = _T("市场部");
    mk.m_children.push_back(u3);

    OrgNode root;
    root.m_kind = OrgNodeKind::Dept;
    root.m_id   = _T("root");
    root.m_name = _T("全公司");
    root.m_children.push_back(rd);
    root.m_children.push_back(mk);
    return root;
}

// 断言可见名按序等于 expected。
void ExpectNames(const std::vector<CString>& got,
                 const std::vector<CString>& expected)
{
    FA_EXPECT_EQ(static_cast<int>(got.size()),
                 static_cast<int>(expected.size()));
    const size_t n = (got.size() < expected.size()) ? got.size() : expected.size();
    for (size_t i = 0; i < n; ++i)
    {
        FA_EXPECT_STREQ(got[i], expected[i]);
    }
}

} // anonymous namespace

void RunAllOrgTreeFilterTests()
{
    // ---- 用例 1:匹配员工 → 保留该员工 + 其上级路径 ----
    BeginCase("OrgTreeFilter.MatchUser_KeepsAncestorPath:匹配员工保留上级路径");
    {
        OrgNode root = MakeSampleOrg();
        OrgTree tree;
        tree.SetRoot(&root);

        tree.SetFilter(_T("张"));
        // 张三命中 → 全公司 / 研发部(上级) / 张三;李四、市场部、王五被滤掉。
        ExpectNames(tree.Test_VisibleNames(),
                    { _T("全公司"), _T("研发部"), _T("张三") });
    }

    // ---- 用例 2:匹配部门 → 保留该部门 + 上级(其非匹配子节点不展开) ----
    BeginCase("OrgTreeFilter.MatchDept_KeepsDeptOnly:匹配部门只保留部门与上级");
    {
        OrgNode root = MakeSampleOrg();
        OrgTree tree;
        tree.SetRoot(&root);

        tree.SetFilter(_T("市场"));
        // 市场部命中 → 全公司 / 市场部;王五不含"市场"故不显示。
        ExpectNames(tree.Test_VisibleNames(),
                    { _T("全公司"), _T("市场部") });
    }

    // ---- 用例 3:无匹配 → 可见行为空 ----
    BeginCase("OrgTreeFilter.NoMatch_Empty:无匹配则可见为空");
    {
        OrgNode root = MakeSampleOrg();
        OrgTree tree;
        tree.SetRoot(&root);

        tree.SetFilter(_T("查无此人"));
        FA_EXPECT_EQ(static_cast<int>(tree.Test_VisibleNames().size()), 0);
    }

    // ---- 用例 4:清空关键字 → 恢复默认展开显示 ----
    BeginCase("OrgTreeFilter.ClearFilter_RestoresFullTree:清空恢复全树");
    {
        OrgNode root = MakeSampleOrg();
        OrgTree tree;
        tree.SetRoot(&root);

        tree.SetFilter(_T("张"));          // 先过滤
        tree.SetFilter(_T(""));            // 再清空
        // SetRoot 默认展开 根 + 一级部门 → 全部可见。
        ExpectNames(tree.Test_VisibleNames(),
                    { _T("全公司"), _T("研发部"), _T("张三"),
                      _T("李四"), _T("市场部"), _T("王五") });
    }

    // ---- 用例 5:空白关键字等同清空(Trim) ----
    BeginCase("OrgTreeFilter.WhitespaceKeyword_TreatedAsClear:全空白等同清空");
    {
        OrgNode root = MakeSampleOrg();
        OrgTree tree;
        tree.SetRoot(&root);

        tree.SetFilter(_T("张"));
        tree.SetFilter(_T("   "));         // 全空白 → Trim 后为空 → 不过滤
        FA_EXPECT_EQ(static_cast<int>(tree.Test_VisibleNames().size()), 6);
    }
}

}} // namespace flamingoadmin::test
