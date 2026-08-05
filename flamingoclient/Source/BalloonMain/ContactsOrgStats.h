/**
 *  ContactsOrgStats.h
 *
 *  通讯录组织架构的纯统计逻辑：从 cmd_get_contacts(1201) 下发的扁平部门数组里
 *  认出代表"公司本身"的那个根部门，以及统计某个部门连同其所有下级部门的去重人数。
 *
 *  背景：公司在组织架构里是 t_department 的一行真实部门（父部门为 0、名字取企业
 *  全称，其 id 回写 t_enterprise.f_root_dept_id，企业改名时该部门名同步更新，见
 *  docs/Admin控制台接口文档.md 第 3.3 节）。Admin 控制台是靠企业信息里的
 *  rootdeptid 认出它的，但 PC 客户端的 1201 应答只下发 enterpriseid 与 name、
 *  <u>不带 rootdeptid</u>（见 chatserversrc/ContactsProtocol.cpp 构造 enterprise
 *  节点处），所以客户端只能按"名字等于企业全称"来认，另加"顶层只有一棵树"兜底。
 *
 *  抽成不依赖 UI 的自由函数是为了能在 FlamingoUnit 里直接断言 —— 联系人树的节点
 *  拼装（CBalloonMainPanel::OnContactsArrived）依赖 DuiTreeView 与 MFC，没法在
 *  单元测试里跑。用例见 Source/Tests/ContactsOrgStatsTests.cpp。
 *
 *  典型用法（见 BalloonMainPanel.cpp 的 OnContactsArrived）：
 *      int companyIdx = balloonmain::FindCompanyRootDeptIndex(
 *          result.m_departments, result.m_enterprisename);
 *      if (companyIdx >= 0)
 *      {
 *          int total = balloonmain::CountDeptMembersRecursive(
 *              result.m_departments, result.m_departments[companyIdx].m_deptid);
 *          // 公司节点标签拼成 "明远科技有限公司 · 161 人"
 *      }
 *
 *  balloonwj@qq.com   2026-07-31
 */

#pragma once

#include <string>
#include <vector>

#include "../net/ContactsProtocolClient.h"   // net::DepartmentItem

namespace balloonmain {

/**
 *  在扁平部门数组里认出代表"公司本身"的那个根部门。
 *
 *  判定顺序：
 *    1. 先圈出"顶层部门"—— 父部门 id 为 0，或父部门没在本数组里出现（数据不
 *       一致时不能让整棵子树从树上丢掉，这类部门同样按顶层对待）。
 *    2. 企业名非空时，顶层部门里名字与企业全称<u>完全相同</u>的那个即公司根部门。
 *       服务端建企业时就是拿企业全称给它命名的，企业改名时同步更新，所以这个判据
 *       与服务端的约定一致。
 *    3. 名字没对上（企业名为空、或企业刚改名而部门名尚未同步）时，若顶层部门
 *       恰好只有一个，认定它就是公司节点。
 *    4. 顶层有多个且都不叫企业名 —— 认不出公司节点（典型为存量数据尚未建立公司
 *       根部门），返回 -1，调用方按"没有公司这一层"处理。
 *
 *    depts               扁平部门数组，顺序不限，父部门可以排在子部门之后。
 *    enterpriseNameUtf8  企业全称（UTF-8，取自 1201 应答的 enterprise.name）；
 *                        允许为空串，此时只走规则 3 的兜底。
 *
 *  返回：公司根部门在 depts 中的下标；认不出时返回 -1。
 */
int FindCompanyRootDeptIndex(const std::vector<net::DepartmentItem>& depts,
                             const std::string& enterpriseNameUtf8);

/**
 *  统计某个部门连同其所有下级部门的去重成员人数。
 *
 *  同一个人可以兼属多个部门（DeptMemberItem::m_isprimary 区分主部门与兼属部门），
 *  所以按 userid 去重，兼属者只计一次。当前登录用户<u>也计算在内</u> —— 这个数字
 *  表达的是"这个部门/公司有多少人"，与列表里能看到几行（树里会跳过自己）不是一回事。
 *
 *  部门父子关系成环属于脏数据，本函数用"已访问部门集合"兜底，只保证不死循环，
 *  不对环内数据的合理性做任何断言。
 *
 *    depts   扁平部门数组，同 FindCompanyRootDeptIndex。
 *    deptId  起算部门的部门 id；在 depts 里找不到时返回 0。
 *
 *  返回：该部门及其所有下级部门的成员去重人数；找不到该部门时为 0。
 */
int CountDeptMembersRecursive(const std::vector<net::DepartmentItem>& depts,
                              int64_t deptId);

} // namespace balloonmain
