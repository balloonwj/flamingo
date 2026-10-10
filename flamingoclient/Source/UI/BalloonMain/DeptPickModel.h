/**
 *  DeptPickModel.h
 *
 *  「选择部门」小窗（个人机器人规则「发送者 ▸ 指定的部门」，docs/机器人方案.md §11 Q19 之后的 2b）左侧部门树的纯逻辑：
 *  把「部门列表 + 搜索词」排成一棵只含部门的树，以及确定时整理选择结果。不碰窗口，单测见
 *  Source/Tests/DeptPickModelTests.cpp（标签 [deptpick]）。
 *
 *  层级口径与选人对话框（MemberPickerTreeModel）、主面板通讯录树一致：
 *    · 根部门 = m_parentdeptid 为 0，或其父部门不在本次的部门列表里；
 *    · 同级部门按 m_deptorder 升序，权重相同时保持部门列表里的先后顺序；
 *    · 部门数据因脏数据成环时靠「已访问集合」截断，环上的部门只出现一次。
 *  选择的语义是「含下级部门」：选了上级就等于选了它下面的全部部门，所以确定时去掉已被上级包含的下级部门。
 *
 *  典型用法：
 *      std::vector<balloonmain::DeptPickNode> nodes = balloonmain::BuildDeptPickTree(depts, L"研发");
 *      std::vector<int64_t> ids = balloonmain::NormalizeDeptSelection(checked, depts);
 *
 *  本文件不依赖 ATL / WTL，可以在任何地方包含。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../net/ContactsProtocolClient.h"   // net::DepartmentItem

namespace balloonmain {

/**
 *  部门树里的一个节点。按显示顺序（深度优先展开后自上而下）存放在数组里，靠 m_depth / m_parent 表达层级。
 */
struct DeptPickNode
{
    int          m_depth;    // 缩进层级，顶层为 0，每下一层加 1
    int          m_parent;   // 父节点在数组里的下标；顶层节点为 -1
    int64_t      m_deptId;   // 部门ID
    std::wstring m_name;     // 部门名称
    bool         m_matched;  // 搜索时名称命中了搜索词；为 false 的节点只是为了显示命中部门所在的位置而保留的上级。不搜索时恒为 true

    DeptPickNode()
        : m_depth(0)
        , m_parent(-1)
        , m_deptId(0)
        , m_matched(true)
    {
    }
};

/**
 *  把部门列表排成一棵只含部门的树。
 *    departments：部门列表（扁平数组，靠 m_parentdeptid 还原成树），只读。
 *    filter：     搜索词；空串表示不过滤。否则按部门名称做大小写不敏感的子串匹配，保留命中的部门及其全部上级部门。
 *  @return 按显示顺序排列的节点；没有部门或没有命中时返回空数组
 */
std::vector<DeptPickNode> BuildDeptPickTree(const std::vector<net::DepartmentItem>& departments, const std::wstring& filter);

/**
 *  整理选择结果：去掉重复的、不在部门列表里的（部门已删除），以及上级部门也被选中的部门（选上级已含下级）。
 *    selected：   已勾选的部门ID，按勾选先后排列。
 *    departments：部门列表，只读。
 *  @return 整理后的部门ID，保持原来的先后顺序
 */
std::vector<int64_t> NormalizeDeptSelection(const std::vector<int64_t>& selected,
                                            const std::vector<net::DepartmentItem>& departments);

/**
 *  按部门ID取部门名称。
 *    departments：部门列表，只读。
 *    deptId：     部门ID。
 *  @return 部门名称；找不到时返回空串
 */
std::wstring DeptNameOf(const std::vector<net::DepartmentItem>& departments, int64_t deptId);

} // namespace balloonmain
