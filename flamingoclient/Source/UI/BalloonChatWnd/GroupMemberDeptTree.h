/**
 *  GroupMemberDeptTree.h
 *
 *  群聊窗「成员」页签部门树视图的<u>纯模型</u>（docs/群成员部门树方案.md）：给定本群成员（userid 与群角色）
 *  与企业部门数组，算出部门树按显示顺序展开后的扁平行序列。不碰窗口与 balloonui，可被 FlamingoUnit
 *  测试工程直接调用断言。
 *
 *  规则：
 *    · 只出现含本群成员的部门及其上级部门；部门行带人数 —— 本部门及下级部门里的本群成员数，同一个人只算一次；
 *    · 同一部门内下级部门在前、成员在后；部门按排序值（再按部门 id），成员先按群角色（群主、管理员、成员）、
 *      再按部门内排序值、最后按成员在输入里的先后；
 *    · 一个人属于多个部门时在每个所属部门下都出现，兼属部门那一行标记为兼属（他在别处有主部门时才标，
 *      数据里一个主部门都没有的人不标）；
 *    · 不属于任何部门的本群成员放在最后的「未分配部门」下（部门 id 为 kDeptTreeUnassignedId，名字留空，
 *      由调用方显示当前界面语言的文字）；
 *    · 部门的上级链出现环时，从环上断开一处，被断开的部门按顶级部门处理，保证不死循环、每个部门只出现一次。
 *
 *  典型用法（群聊窗）：
 *      std::vector<DeptTreeMemberInput> in = ...;   // 按服务端返回的成员顺序
 *      std::vector<DeptTreeRow> rows = BuildGroupMemberDeptTree(in, departments);
 *      for (...) 按 rows[i].m_kind 建部门标题行或成员行，按 m_parent 挂到列表上
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../net/ContactsProtocolClient.h"   // net::DepartmentItem / DeptMemberItem

namespace balloonchatwnd {

// 「未分配部门」那一行的部门 id。真实部门 id 都是正数，取 -1 不会撞上
const int64_t kDeptTreeUnassignedId = -1;

/**
 *  部门树里一行的类型。
 */
enum DeptTreeRowKind
{
    kDeptTreeRowDept   = 1,   // 部门标题行：折叠箭头 + 部门名 + 人数
    kDeptTreeRowMember = 2,   // 成员行
};

/**
 *  输入：本群的一位成员。
 */
struct DeptTreeMemberInput
{
    int64_t m_userId;   // 成员 userid
    int     m_role;     // 群角色，取值见 Protocol.h 的 group_member_role（0 成员 1 管理员 2 群主）；不认识的值按成员

    DeptTreeMemberInput()
        : m_userId(0)
        , m_role(0)
    {
    }
};

/**
 *  输出：部门树按显示顺序展开后的一行。
 */
struct DeptTreeRow
{
    int         m_kind;        // 行类型，取值见 DeptTreeRowKind
    int         m_depth;       // 缩进层级：顶级部门为 0，它的直属成员与下级部门为 1，依次类推
    int         m_parent;      // 父部门行在结果里的下标；顶级部门为 -1
    int64_t     m_deptId;      // 部门行为本部门 id，成员行为所在部门 id；「未分配部门」为 kDeptTreeUnassignedId
    std::string m_deptName;    // 部门行的部门名（UTF-8）；「未分配部门」与成员行为空串
    int         m_memberCount; // 部门行：本部门及下级部门里的本群成员数（同一人只算一次）；成员行为 0
    int64_t     m_userId;      // 成员行的 userid；部门行为 0
    bool        m_secondary;   // 成员行：这一行所在部门是不是他的兼属部门（画「兼」字）；部门行为 false

    DeptTreeRow()
        : m_kind(kDeptTreeRowDept)
        , m_depth(0)
        , m_parent(-1)
        , m_deptId(0)
        , m_memberCount(0)
        , m_userId(0)
        , m_secondary(false)
    {
    }
};

/**
 *  算出部门树的扁平行序列。纯函数。
 *    members：     本群成员，顺序即服务端返回的顺序（同角色、同排序值时按它排）；userid 重复的只取第一个。
 *    departments： 企业部门数组（1201 应答的扁平数组），可以为空（此时全部成员都在「未分配部门」下）。
 *  @return 行序列（值语义）；members 为空时返回空。
 */
std::vector<DeptTreeRow> BuildGroupMemberDeptTree(const std::vector<DeptTreeMemberInput>& members,
                                                  const std::vector<net::DepartmentItem>& departments);

} // namespace balloonchatwnd
