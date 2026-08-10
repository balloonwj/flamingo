/**
 *  flamingoAdmin —— 企业详情屏 EnterpriseDetailScreen。
 *
 *  对应设计稿「04 企业详情」。从企业管理列表点击某企业进入：
 *    · 顶部：面包屑 + 企业头（头像 / 名称 / 状态 / 套餐 / 操作按钮）+ 两个
 *      子 tab（组织架构 / 服务号）；
 *    · 「组织架构」tab：左栏组织树（OrgTree）+ 右栏详情面板（OrgDetailPanel），
 *      左栏顶部有展开 / 折叠工具条、底部有「顶级部门」按钮；
 *    · 「服务号」tab：Phase 4a 阶段为占位，4b 接入。
 *
 *  本屏是一个【自绘容器控件】：头部、工具条、子 tab 等装饰自绘，OrgTree /
 *  OrgDetailPanel / 4 个头部按钮是子控件。组织树与详情面板之间的选中联动
 *  由本屏内部用回调直接协调；需要弹窗 / 写库的动作（新增部门 / 员工、删除）
 *  经 WM_DUI_NOTIFY 上报 AdminFrame 处理。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Widgets/OrgTree.h"
#include "Mock/MockData.h"

namespace balloonwjui { class DuiButton; }   // FlatButton 已迁到 balloonwjui::DuiButton
namespace balloonwjui { class DuiEditHost; } // 组织树搜索框(寄宿真 EDIT)

namespace flamingoadmin {

class OrgDetailPanel;
class ServicesPanel;

// 企业详情屏。
class EnterpriseDetailScreen : public balloonwjui::DuiControl
{
public:
    // 上报给 AdminFrame 的待处理动作（配合 GetPendingNodeId 使用）。
    enum class Pending
    {
        None,
        AddDept,     // 请求在 GetPendingNodeId() 部门下新增子部门
        AddUser,     // 请求在 GetPendingNodeId() 部门下新增员工
        Delete,      // 请求删除 GetPendingNodeId() 节点
        SelectUser,  // 选中了 GetPendingNodeId() 员工，请求拉取其权限矩阵
        SetRole,     // 请求把 GetPendingNodeId() 员工的角色改为面板当前值
        SetPerms,    // 请求把 GetPendingNodeId() 员工的权限覆盖改为面板当前值
        SetUserStatus, // 请求启用/停用 GetPendingNodeId() 员工
        ResetPassword, // 请求重置 GetPendingNodeId() 员工的登录密码
        KickDevices,   // 请求强制下线 GetPendingNodeId() 员工的全部设备
        EditUser,      // 请求修改 GetPendingNodeId() 员工的资料
        MoveUser,      // 请求调整 GetPendingNodeId() 员工的所属部门
    };

    EnterpriseDetailScreen();

    // 载入某企业（重置组织树 / 详情面板 / 子 tab）。
    void SetEnterprise(const CString& enterpriseId);

    // 组织树增删后重新扁平、重解析当前选中节点。保持展开状态。
    void Refresh();

    // 当前企业 ID。
    CString GetEnterpriseId() const { return m_entId; }

    // 取上一次上报动作的类型 / 目标节点 ID（AdminFrame 收到通知后查询）。
    Pending GetPending() const       { return m_pending; }
    CString GetPendingNodeId() const { return m_pendingNodeId; }

    // 「新增员工」默认挂载到的部门 ID：当前选中的部门；没有选中部门（一个都没选，
    // 或选中的是某个员工）时取组织树的根，也就是代表公司本身的那个部门。
    CString DefaultParentDept() const;

    // 组织树根节点的 ID，即代表公司本身的那个部门。「新增顶级部门」以它作为父节点。
    // 企业完成迁移后这是一个真实部门 ID；组织树尚未加载时退回历史上的合成根 ID。
    CString DefaultTopParentDept() const;

    // 当前是否在「组织架构」子 tab。
    bool IsOrgTab() const { return m_subTab == kTabOrg; }

    // 切换到指定子 tab（0 = 组织架构，1 = 服务号）。供 AdminFrame 的行
    // 菜单「打开详情 → 服务号」在详情加载完成后定位到目标 tab；须在
    // SetEnterprise 之后调用。
    //   tab：目标子 tab 序号，0 组织架构 / 1 服务号；非法值忽略。
    void SelectSubTab(int tab);

    // 取服务号面板（AdminFrame 处理服务号动作时查询其选中项等）。
    ServicesPanel* GetServicesPanel() const { return m_servicesPanel; }

    // 取组织详情面板（AdminFrame 处理角色 / 权限动作时读写它）。
    OrgDetailPanel* GetOrgDetailPanel() const { return m_panel; }

    // 选中并定位到某节点（新增节点后由 AdminFrame 调用）。
    void SelectNode(const CString& nodeId);

    // 组织树搜索框文字变化时调用(由 AdminFrame 收到 DUIN_VALUECHANGED 转发):
    // 读搜索框当前文本,按部门名 / 员工姓名过滤组织树。
    void OnOrgSearchChanged();

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // OrgTree / OrgDetailPanel 的回调转发到成员函数。
    static void SelectThunk(void* user, const CString& nodeId);
    static void ActionThunk(void* user, OrgTree::Action action,
                            const CString& nodeId);
    void OnNodeSelected(const CString& nodeId);
    void OnNodeAction(OrgTree::Action action, const CString& nodeId);

    // 把一个动作记入 m_pending 并通知 AdminFrame。
    void RaisePending(Pending pending, const CString& nodeId);

    // 子 tab。
    enum { kTabOrg = 0, kTabSvc = 1 };
    void SetSubTab(int tab);

    CString          m_entId;
    const Enterprise* m_ent  = nullptr;          // 当前企业（借用）
    int              m_subTab = kTabOrg;         // 当前子 tab

    OrgTree*         m_tree  = nullptr;          // 组织树（子控件）
    balloonwjui::DuiEditHost* m_orgSearch = nullptr;  // 组织树搜索框（工具条左侧,子控件）
    OrgDetailPanel*  m_panel = nullptr;          // 组织详情面板（子控件）
    ServicesPanel*   m_servicesPanel = nullptr;  // 服务号面板（子控件）
    balloonwjui::DuiButton*  m_btnBack     = nullptr;    // 返回
    balloonwjui::DuiButton*  m_btnSettings = nullptr;    // 企业设置
    balloonwjui::DuiButton*  m_btnAnnounce = nullptr;    // 发布公告
    balloonwjui::DuiButton*  m_btnCreateGroup = nullptr; // 新建群（仅组织 tab 显示）
    balloonwjui::DuiButton*  m_btnPrimary  = nullptr;    // 主操作（新增员工 / 新建服务号）

    // Layout 时算好的可点装饰区（host 客户区坐标）。
    RECT m_rcTabOrg{};      // 子 tab：组织架构
    RECT m_rcTabSvc{};      // 子 tab：服务号
    RECT m_rcExpandAll{};   // 工具条：展开全部
    RECT m_rcCollapse{};    // 工具条：折叠
    RECT m_rcAddTopDept{};  // 页脚：新增顶级部门

    Pending m_pending = Pending::None;   // 上一次上报的动作
    CString m_pendingNodeId;             // 上一次上报动作的目标节点
};

} // namespace flamingoadmin
