/**
 *  搜索候选缓存：把联系人与群在搜索匹配时要用到、但与查询词无关的数据（显示名、
 *  小写形式、拼音首字母串、音节序列）预先算好并保存，供主面板搜索下拉在每次按键时复用。
 *
 *  背景：主面板搜索框每按一次键，都要对全部联系人和群重新匹配一遍。以前每次都现做
 *  UTF-8 → UTF-16 转换、取显示名、转小写、查拼音表，5000 个联系人时一次按键约
 *  170 ms，而这些结果在打字过程中根本不变。
 *
 *  一致性的保证方式：本类不依赖外部在数据变化时通知它失效，而是在每次 Sync 时把
 *  缓存的每一条与资料池对应位置逐项比对（userid 与参与匹配的几个文本字段），只重算
 *  不一致的那几条，数量变化时截断或追加。因此资料池无论在哪里、以什么方式被修改，
 *  下一次 Sync 之后缓存都与资料池一致；数据没变时 Sync 只做比对，开销很小。
 *
 *  本文件不依赖 stdafx.h，与 FlamingoUnit 测试工程共用同一份源码
 *  （见 Tests/SearchCandidateCacheTests.cpp）。包含本头文件之前调用方须已引入 ATL。
 *
 *  典型用法（每次按键）：
 *      m_candidates.SyncContacts(*m_contacts);
 *      contactsearch::PreparedQuery pq = contactsearch::PrepareQuery(query);
 *      for (size_t i = 0; i < m_candidates.ContactCount(); ++i)
 *      {
 *          const balloonsearch::ContactCandidate& c = m_candidates.ContactAt(i);
 *          contactsearch::MatchResult mr = contactsearch::MatchPrepared(
 *              pq, c.m_shownName, c.m_accountLower, c.m_regionLower);
 *          ...   // 第 i 条候选对应资料池的第 i 个联系人
 *      }
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <atlstr.h>   // CString

#include <stdint.h>
#include <string>
#include <vector>

#include "ContactMatch.h"                    // contactsearch::PreparedName
#include "../../net/ContactsProtocolClient.h" // net::ContactItem
#include "../../net/GroupProtocolClient.h"    // net::UserGroupItem

namespace balloonsearch {

// 一个联系人候选的预处理结果。前五个字段是数据源快照，其余是由快照算出的结果。
struct ContactCandidate
{
    // —— 数据源快照：Sync 时与资料池逐项比对，任何一项不同就重算本条 ——
    int64_t     m_userid;     // 联系人 userid
    std::string m_remark;     // 备注名（UTF-8），空串表示未设置
    std::string m_nickname;   // 昵称（UTF-8）
    std::string m_username;   // 账号（UTF-8）
    std::string m_address;    // 所在地区（UTF-8）

    // —— 由快照算出的结果 ——
    CString                     m_display;       // 界面显示名，即 ResolveContactDisplayName 的结果
                                                 //（不含自己那一行的"（我）"后缀，后缀由调用方追加）
    contactsearch::PreparedName m_shownName;     // 按显示名预处理，搜索高亮区间相对显示名给出
    bool                        m_nickDiffers;   // 显示名与原始昵称不同时为 true（设了备注、或昵称为空）
    contactsearch::PreparedName m_nickName;      // 按原始昵称预处理，用于"用真名也能搜到设了备注的人"；
                                                 // m_nickDiffers 为 false 时不填，保持为空
    std::wstring                m_accountLower;  // 账号转 UTF-16 后再转小写
    std::wstring                m_regionLower;   // 地区转 UTF-16 后再转小写

    ContactCandidate()
        : m_userid(0)
        , m_nickDiffers(false)
    {
    }
};

// 一个群候选的预处理结果。
struct GroupCandidate
{
    // —— 数据源快照 ——
    int64_t     m_groupid;   // 群 id
    std::string m_name;      // 群名（UTF-8）

    // —— 由快照算出的结果 ——
    contactsearch::PreparedName m_preparedName;   // 按群名预处理（群名为空时各字段为空）

    GroupCandidate()
        : m_groupid(0)
    {
    }
};

/**
 *  一条部门候选：部门的快照（id、名字、上级），以及按名字预处理好的匹配用数据。
 *  名字为空的部门同样占一个位置（保持与部门列表按位置一一对应），只是名字匹配不到。
 */
struct DepartmentCandidate
{
    // —— 数据源快照 ——
    int64_t     m_deptid;     // 部门 id
    int64_t     m_parentid;   // 上级部门 id，0 表示顶级部门
    std::string m_name;       // 部门名（UTF-8）

    // —— 由快照算出的结果 ——
    contactsearch::PreparedName m_preparedName;   // 按部门名预处理（名字为空时各字段为空）

    DepartmentCandidate()
        : m_deptid(0)
        , m_parentid(0)
    {
    }
};

/**
 *  SearchCandidateCache —— 联系人与群的搜索候选缓存。说明见文件头。
 *  第 i 条联系人候选始终对应最近一次 SyncContacts 传入的资料池的第 i 个联系人，
 *  群候选同理。只在 UI 线程使用，不做线程同步。
 */
class SearchCandidateCache
{
public:
    /**
     *  让联系人候选与资料池保持一致。逐位置比对 userid、备注、昵称、账号、地区五项，
     *  不一致的条目重算；资料池变短时截掉多出的条目，变长时追加新条目。
     *  签名、标签、头像、在线状态等不参与匹配的字段变化不会触发重算。
     *    contacts：主面板的联系人资料池。本类只读取，不持有。
     *  返回：本次重算（含新追加）的条目数，供日志与单元测试使用。
     */
    int SyncContacts(const std::vector<net::ContactItem>& contacts);

    /**
     *  让群候选与群列表保持一致。逐位置比对群 id 与群名，规则同 SyncContacts；
     *  成员数、头像、置顶等字段变化不会触发重算。
     *    groups：主面板的"我的群"列表。本类只读取，不持有。
     *  返回：本次重算（含新追加）的条目数。
     */
    int SyncGroups(const std::vector<net::UserGroupItem>& groups);

    /**
     *  让部门候选与部门列表保持一致。逐位置比对部门 id、名字与上级部门，规则同 SyncContacts；
     *  成员、排序权重、负责人等字段变化不会触发重算。
     *    departments：主面板的部门列表（组织架构）。本类只读取，不持有。
     *  返回：本次重算（含新追加）的条目数。
     */
    int SyncDepartments(const std::vector<net::DepartmentItem>& departments);

    // 当前部门候选条数，等于最近一次 SyncDepartments 传入的部门列表大小。
    size_t DepartmentCount() const;

    /**
     *  取第 index 条部门候选。
     *    index：取值范围 [0, DepartmentCount())，越界属于调用方错误。
     *  返回：候选的常引用，在下一次 SyncDepartments 或 Clear 之前有效。
     */
    const DepartmentCandidate& DepartmentAt(size_t index) const;

    // 当前联系人候选条数，等于最近一次 SyncContacts 传入的资料池大小。
    size_t ContactCount() const;

    /**
     *  取第 index 条联系人候选。
     *    index：取值范围 [0, ContactCount())，越界属于调用方错误。
     *  返回：候选的常引用，在下一次 SyncContacts 或 Clear 之前有效。
     */
    const ContactCandidate& ContactAt(size_t index) const;

    // 当前群候选条数，等于最近一次 SyncGroups 传入的群列表大小。
    size_t GroupCount() const;

    /**
     *  取第 index 条群候选。
     *    index：取值范围 [0, GroupCount())，越界属于调用方错误。
     *  返回：候选的常引用，在下一次 SyncGroups 或 Clear 之前有效。
     */
    const GroupCandidate& GroupAt(size_t index) const;

    // 清空全部候选（如退出登录时），下一次 Sync 会整批重建。
    void Clear();

private:
    std::vector<ContactCandidate> m_contacts;   // 联系人候选，与资料池按位置一一对应
    std::vector<GroupCandidate>   m_groups;     // 群候选，与群列表按位置一一对应
    std::vector<DepartmentCandidate> m_departments;   // 部门候选，与部门列表按位置一一对应
};

} // namespace balloonsearch
