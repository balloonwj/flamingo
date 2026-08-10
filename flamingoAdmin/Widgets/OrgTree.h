/**
 *  flamingoAdmin —— 组织树控件 OrgTree。
 *
 *  企业详情屏「组织架构」tab 左栏的树形控件：把一棵 OrgNode 组织树扁平成
 *  可见行列表自绘呈现，支持展开 / 折叠、选中、滚轮滚动，以及 hover 时浮出
 *  的「新增子部门 / 新增员工 / 删除」动作按钮。
 *
 *  本控件是【自绘控件，无子控件】—— 行、图标、动作按钮全部在 OnPaint 里
 *  画，交互在 OnLButtonUp 里按坐标命中。这样选中 / 增删都不涉及销毁任何
 *  子控件，从根本上避免「销毁宿主跟踪中的控件」导致的崩溃。
 *
 *  与外部的通信走【回调】（函数指针 + user 数据），不走 WM_DUI_NOTIFY：
 *    · SelectFn —— 选中节点变化；
 *    · ActionFn —— 对某节点发起新增子部门 / 新增员工 / 删除。
 *  由 EnterpriseDetailScreen 注册回调并居中协调。
 *
 *  组织树数据（OrgNode）的所有权在 DataStore；本控件只【借用】根指针。
 *  注意：DataStore 增删节点会令子节点 vector 重新分配，使行内的节点指针
 *  失效 —— 故每次增删后调用方必须调 Rebuild() 重新扁平。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"

#include <vector>
#include <set>
#include <utility>

namespace flamingoadmin {

class OverlayScrollbar;   // 树区右缘的悬浮 auto-hide 滚动条子控件

// 组织树控件。
class OrgTree : public balloonwjui::DuiControl
{
public:
    // 对某节点发起的动作类型。
    enum class Action
    {
        AddDept,   // 在该部门下新增子部门
        AddUser,   // 在该部门下新增员工
        Delete,    // 删除该节点（连同子树）
        SetRole,   // 设置该员工的角色（由 OrgDetailPanel 权限 tab 触发）
        SetPerms,  // 设置该员工的权限覆盖（由 OrgDetailPanel 权限 tab 触发）
        SetUserStatus, // 启用/停用该员工（由 OrgDetailPanel 危险操作触发）
        ResetPassword, // 重置该员工登录密码（由 OrgDetailPanel 危险操作触发）
        KickDevices,   // 强制下线该员工全部设备（由 OrgDetailPanel 设备 tab 触发）
        EditUser,      // 修改该员工资料（由 OrgDetailPanel 基本信息 tab 触发）
        MoveUser,      // 调整该员工所属部门（由 OrgDetailPanel 基本信息 tab 触发）
    };

    // 选中节点变化回调。nodeId 为新选中节点 ID。
    typedef void (*SelectFn)(void* user, const CString& nodeId);
    // 节点动作回调。
    typedef void (*ActionFn)(void* user, Action action, const CString& nodeId);

    OrgTree();

    // 注册回调。selFn / actFn 任一可为 nullptr。
    void SetCallbacks(SelectFn selFn, ActionFn actFn, void* user);

    // 绑定组织树根节点（借用指针）。会重置展开集为「根 + 一级部门」、
    // 清空选中、回到顶部，并重建行表。
    void SetRoot(const OrgNode* root);

    // 树结构变化后重建可见行表。增删节点后必须调用。
    void Rebuild();

    // 设置 / 读取当前选中节点 ID。
    void    SetSelectedId(const CString& nodeId);
    CString GetSelectedId() const { return m_selectedId; }

    // 展开 / 折叠全部部门。
    void ExpandAll();
    void CollapseAll();

    // 按关键字过滤组织树:只保留"名称(部门名 / 员工姓名)包含关键字"的节点及其
    // 上级路径,并强制展开使匹配可见;关键字为空 / 全空白时清除过滤、恢复按
    // m_expanded 的正常显示。大小写不敏感。
    //   keyword：过滤关键字。
    void SetFilter(const CString& keyword);

    // 测试用:返回当前可见行的节点名称(按显示顺序),供过滤逻辑单测断言。
    std::vector<CString> Test_VisibleNames() const;

    // 测试用:返回第 rowIndex 行 hover 动作按钮对应的动作列表(按显示顺序)。
    // 供"公司根节点只给新增、不给删除"这类断言使用；行号越界时返回空表。
    std::vector<Action> Test_RowActions(int rowIndex) const;

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    bool OnMouseLeave() override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // 一条可见行：节点指针 + 其在树中的深度。
    struct Row
    {
        const OrgNode* m_node;
        int            m_depth;
    };

    // 按 m_expanded(或过滤态)把 m_root 子树扁平进 m_rows。
    void Flatten();
    void FlattenInto(const OrgNode* node, int depth);

    // 节点自身名称是否包含当前过滤关键字(m_filter 已小写;空过滤恒 false)。
    bool NodeMatches(const OrgNode* node) const;
    // 节点或其任一后代是否匹配 —— 决定该子树在过滤时是否保留。
    bool SubtreeMatches(const OrgNode* node) const;

    // 把滚动位置夹到合法范围。
    void ClampScroll();

    // 行 i 的矩形（host 客户区坐标，已应用滚动）。
    RECT RowRect(int i) const;

    // 命中坐标 pt 落在第几行；不在任何行上返回 -1。
    int RowAtPoint(POINT pt) const;

    // 算出第 i 行 hover 动作按钮的「动作 + 矩形」列表。
    void ActionButtons(int rowIndex,
                       std::vector<std::pair<Action, RECT>>& out) const;

    // 悬浮滚动条拖动滑块时的回调（注册给 OverlayScrollbar）：把新滚动偏移
    // 写回本控件并重绘。
    //   ctx：本 OrgTree 实例；newScroll：拖动产生的新滚动偏移（像素）。
    static void OnTreeSbScroll(void* ctx, int newScroll);

    const OrgNode*        m_root      = nullptr;  // 组织树根（借用）
    std::set<CString>     m_expanded;             // 展开的部门 ID 集
    CString               m_filter;               // 过滤关键字(小写);空 = 不过滤
    std::vector<Row>      m_rows;                 // 当前可见行（扁平）
    CString               m_selectedId;          // 当前选中节点 ID
    int                   m_scrollY   = 0;        // 纵向滚动偏移（像素）
    int                   m_hoverRow  = -1;       // 当前 hover 行（-1 = 无）
    int                   m_contentH  = 0;        // 全部可见行的内容总高（像素，OnPaint 算出）

    SelectFn              m_selFn     = nullptr;
    ActionFn              m_actFn     = nullptr;
    void*                 m_cbUser    = nullptr;

    // 树区右缘的悬浮 auto-hide 滚动条子控件（生命周期由本控件 m_children 持有）。
    OverlayScrollbar*     m_treeSb    = nullptr;
};

} // namespace flamingoadmin
