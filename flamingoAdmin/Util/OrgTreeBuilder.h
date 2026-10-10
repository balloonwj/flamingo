/**
 *  flamingoAdmin —— 组织架构树的组装逻辑。
 *
 *  把服务端 dept/tree.do 应答里那份"扁平的部门数组 + 每个部门内嵌的成员数组"
 *  组装成 OrgNode 树。之所以从 AdminApi 里单独拆出来，是因为这段逻辑（父子挂接、
 *  同级排序、公司根节点的识别与标记、孤儿部门的兜底）是整个解析过程中真正容易
 *  出错的部分，拆出来之后不依赖 HTTP 与 JSON，可以直接写单元测试。
 *
 *  典型用法（见 Util/AdminApi.cpp 的 ParseDeptTreeResponse）：
 *      std::vector<OrgDeptRaw> depts;   // 先由 JSON 逐条填好
 *      OrgNode root = orgtree::BuildOrgTree(depts, rootDeptId, enterpriseName);
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <atlstr.h>
#include <vector>
#include "Mock/MockData.h"      // OrgNode / OrgNodeKind

namespace flamingoadmin {

namespace orgtree {

// 从应答中解析出来的单个部门的原始数据。成员已经解析成 OrgNode，负责人姓名也已
// 由调用方对照成员表解析好，本结构不再关心 JSON 层面的细节。
struct OrgDeptRaw
{
    long long            m_deptId       = 0;   // 部门 ID
    long long            m_parentDeptId = 0;   // 父部门 ID；0 表示没有父部门
    int                  m_level        = 1;   // 部门层级，公司根部门为 1，每向下一级加一
    int                  m_order        = 0;   // 同级排序权重，值小者靠前
    CString              m_name;               // 部门名称
    CString              m_leader;             // 部门负责人姓名，无负责人时为空串
    std::vector<OrgNode> m_members;            // 本部门的直属成员节点，按应答给出的顺序
};

/**
 *  把扁平的部门数组组装成一棵组织架构树。
 *  @param depts         扁平的部门数组，顺序不限；父部门可以排在子部门之后。
 *  @param rootDeptId    公司根部门 ID，取自企业记录的 rootdeptid。传 0 表示该企业
 *                       尚未建立公司根部门（存量数据迁移之前）。
 *  @param enterpriseName 企业全称。仅在 rootDeptId 为 0 或在 depts 里找不到对应部门、
 *                       需要退回合成一个根节点时用作根节点名称。
 *  @return 整棵树的根节点。根节点的 m_isCompanyRoot 恒为 true：无论它是真实的公司
 *          根部门还是退回合成出来的节点，都代表企业本身，都不允许被删除。
 *
 *  组装规则：
 *    · 子部门排在本部门的成员之前，同级部门按 m_order 升序。
 *    · 父部门不在 depts 里的部门（含 m_parentDeptId 为 0 的顶级部门）一律直接挂到
 *      根节点下，避免任何一个部门因数据不一致而从树上整棵丢失。
 */
OrgNode BuildOrgTree(const std::vector<OrgDeptRaw>& depts,
                     long long rootDeptId,
                     const CString& enterpriseName);

// 退回合成根节点时使用的节点 ID。历史上整棵树的根一直是这个合成节点，新增员工时
// 客户端会把它翻译成部门 ID 0 发给服务端，而服务端据此跳过入部门这一步，导致员工
// 建出来却不属于任何部门。企业建立公司根部门之后，根节点用的是真实部门 ID，不再
// 出现这个字符串；这里保留它只是为了兼容尚未迁移的存量数据。
extern const wchar_t* const kSyntheticRootId;

// ---- 部门负责人选择器（新增部门弹窗）----

// 负责人候选人：本企业的一名员工。
struct LeaderCandidate
{
    CString m_userId;    // 员工 ID（组织树节点 ID，即服务端 userid 的十进制文字）
    CString m_name;      // 姓名，作为下拉项的主文字
    CString m_subText;   // 副文字「部门 · 职务」（没有职务时只有部门），用来区分同名的人
};

// 下拉框第 0 项的文字：不设置负责人。
extern const wchar_t* const kLeaderNoneText;

// ResolveLeaderChoice 的返回值：所选内容无法确定为某一名员工（手输的姓名对不上，或对上了
// 多个同名的人），调用方应拦下提交并提示「请从列表中选择负责人」。
const long long kLeaderInvalid = -1;

/**
 *  从组织树收集负责人候选人：深度优先遍历，按员工 ID 去重（同一员工挂在多个部门时只取
 *  第一次出现处的部门与职务）。
 *  @param root 企业组织树根节点（借用，可为 nullptr —— 组织树尚未加载时返回空表）。
 *  @return 候选人列表，顺序与组织树一致。
 */
std::vector<LeaderCandidate> CollectLeaderCandidates(const OrgNode* root);

/**
 *  把负责人下拉框的状态换算成员工 ID。下拉框第 0 项是 kLeaderNoneText，第 i + 1 项对应
 *  candidates[i]；下拉框可输入，文字可能与选中项不一致，因此以文字为准：
 *    · 文字去掉首尾空白后为空，或选中第 0 项且文字就是 kLeaderNoneText：返回 0（不设置）；
 *    · 选中项的姓名与文字相同：返回该员工 ID；
 *    · 否则按文字在候选人里找姓名完全相同的人，恰好一个时返回其 ID；
 *    · 其余情况返回 kLeaderInvalid。
 *  @param candidates CollectLeaderCandidates 的结果。
 *  @param comboSel   下拉框当前选中项序号（-1 表示没有选中项）。
 *  @param comboText  下拉框当前显示的文字。
 *  @return 员工 ID；0 表示不设置；kLeaderInvalid 表示无法确定。
 */
long long ResolveLeaderChoice(const std::vector<LeaderCandidate>& candidates,
                              int comboSel, const CString& comboText);

} // namespace orgtree

} // namespace flamingoadmin
