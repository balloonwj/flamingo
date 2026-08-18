/**
 *  MemberPickerTreeModel.h
 *
 *  "选人对话框"（CCreateGroupDlg：新建群聊 / 邀请入群 / 添加禁言成员 / 转让群主
 *  四种用途共用）左侧候选列表的树模型。
 *
 *  把"联系人资料池 + 部门列表 + 搜索词 → 一棵按部门分层的节点序列"这件纯计算的事
 *  从对话框里抽出来，好处有两个：一是对话框只管把节点喂给 DuiTreeView、不再自己
 *  推层级；二是层级 / 排序 / 过滤 / 勾选态这些容易改错的规则可以被 FlamingoUnit
 *  直接断言，不必每次都开对话框肉眼核对（对照 BadgeRender、GroupRowText 的组织方式）。
 *
 *  层级口径与主面板通讯录树保持一致（见 CBalloonMainPanel::OnContactsArrived）：
 *    · 根部门 = m_parentdeptid 为 0，或其父部门不在本次下发的部门集合里；
 *    · 同级部门按 m_deptorder 升序，部门内成员按 m_memberorder 升序；
 *    · 一个人可兼属多个部门，会在每个所属部门下各出现一次；
 *    · 没进任何部门的联系人统一落到末尾的"未分组"节点下；
 *    · 当前登录用户自己不出现在树里。
 *
 *  输出是一个<u>扁平数组</u>而非嵌套结构：每个节点带 m_depth（缩进层级）与
 *  m_parent（父节点下标），顺序即"深度优先展开后自上而下的显示顺序"。DuiTreeView
 *  的 AddRoot / AddChild 正好按这个顺序逐个建节点。
 *
 *  本模块只做数据运算，不碰任何绘制 / 窗口 API。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <atlstr.h>       // CString
#include <vector>
#include <set>
#include <stdint.h>

#include "../../net/ContactsProtocolClient.h"   // net::ContactItem / net::DepartmentItem

namespace balloonmain {

/**
 *  节点种类。
 */
enum MemberPickerNodeKind
{
    kPickerNodeDept   = 0,   // 部门节点（含公司根那一层）：分组标题，勾它等于全选其下所有成员
    kPickerNodeMember = 1,   // 成员节点：一个候选人，勾它等于选中此人
};

/**
 *  勾选态。部门节点三态，成员节点只会是选中 / 未选中两种。
 */
enum MemberPickerCheckState
{
    kPickerUnchecked = 0,   // 未选中：成员未被选，或部门下一个成员都没被选
    kPickerChecked   = 1,   // 已选中：成员已被选，或部门下所有成员都已被选
    kPickerPartial   = 2,   // 半选：仅部门节点会出现，其下成员只被选中了一部分
};

/**
 *  树里的一个节点。按显示顺序存放在数组里，靠 m_depth / m_parent 表达层级。
 */
struct MemberPickerNode
{
    int      m_depth;    // 缩进层级，顶层为 0，每下一层 +1
    int      m_parent;   // 父节点在数组里的下标；顶层节点为 -1
    int      m_kind;     // 节点种类，取值见 MemberPickerNodeKind
    int64_t  m_deptId;   // 部门节点的部门ID；成员节点恒为 0。"未分组"节点也为 0
    UINT     m_userId;   // 成员节点的 userid；部门节点恒为 0
    CString  m_label;    // 显示文字：部门节点为部门名，成员节点为其显示名

    MemberPickerNode()
        : m_depth(0)
        , m_parent(-1)
        , m_kind(kPickerNodeDept)
        , m_deptId(0)
        , m_userId(0)
    {
    }
};

/**
 *  构建候选人树。
 *
 *    contacts     联系人资料池（候选人来源）。调用方保留所有权，本函数只读。
 *                 显示名取"昵称 → 账号 → 用户<id>"，与对话框原有口径一致。
 *    departments  部门列表（扁平数组，靠 m_parentdeptid 还原成树）。传空数组表示
 *                 候选没有组织架构信息（如候选来自群成员），此时所有人落到"未分组"。
 *    selfUserId   当前登录用户 userid，树里排除此人；传 0 表示不排除任何人。
 *    filter       搜索词。空串表示不过滤；否则按显示名与账号做<u>大小写不敏感</u>的
 *                 子串匹配，只保留命中的成员，命中后其所有祖先部门一并保留。
 *                 过滤后不含任何成员的部门不出现在结果里。
 *
 *  返回：按显示顺序排列的节点数组；没有任何候选时返回空数组。
 *
 *  说明：部门数据若因脏数据成环（A 的父是 B、B 的父是 A），本函数靠"已访问集合"
 *  截断，不会死循环，环上的部门只展开一次。
 */
std::vector<MemberPickerNode> BuildMemberPickerTree(
    const std::vector<net::ContactItem>& contacts,
    const std::vector<net::DepartmentItem>& departments,
    UINT selfUserId,
    const CString& filter);

/**
 *  收集某个部门节点<u>及其所有子孙部门</u>下的成员 userid。
 *
 *    nodes      BuildMemberPickerTree 的结果。
 *    deptIndex  部门节点在 nodes 里的下标；下标越界或该节点不是部门节点时不收集。
 *    out        收集结果（追加写入，调用方负责先清空）。同一人兼属多个子部门时
 *               只写入一次。
 */
void CollectDeptMemberIds(const std::vector<MemberPickerNode>& nodes,
                          int deptIndex,
                          std::vector<UINT>& out);

/**
 *  按当前已选集合推导某个节点该显示的勾选态。
 *
 *    nodes     BuildMemberPickerTree 的结果。
 *    index     目标节点在 nodes 里的下标。
 *    selected  已选中的 userid 集合。
 *
 *  返回：
 *    · 成员节点 —— 在 selected 里返回 kPickerChecked，否则 kPickerUnchecked；
 *    · 部门节点 —— 其下成员全选返回 kPickerChecked，一个都没选（或压根没有成员）
 *                  返回 kPickerUnchecked，选了一部分返回 kPickerPartial；
 *    · 下标越界 —— 返回 kPickerUnchecked。
 */
int DeriveCheckState(const std::vector<MemberPickerNode>& nodes,
                     int index,
                     const std::set<UINT>& selected);

} // namespace balloonmain
