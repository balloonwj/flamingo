/**
 *  flamingoAdmin —— 企业详情屏 EnterpriseDetailScreen。
 *
 *  对应设计稿「04 企业详情」。从企业管理列表点击某企业进入：
 *    · 顶部：面包屑 + 企业头（头像 / 名称 / 状态 / 套餐 / 操作按钮）+ 子 tab：组织架构 / 服务号 /
 *      在线用户（FlamingoLink.dll 可用时才有）/ 功能开关；
 *    · 「组织架构」tab：左栏组织树（OrgTree）+ 右栏详情面板（OrgDetailPanel），
 *      左栏顶部有展开 / 折叠工具条、底部有「顶级部门」按钮；
 *    · 「服务号」tab：服务号面板（ServicesPanel）；
 *    · 「在线用户」tab（2026-10-04）：内容是 FlamingoLink.dll 建的原生子窗口，本屏只放一个
 *      占位控件（ui::LinkPageSlot）占住主体区，由 AdminFrame 把它的位置转交 DLL。
 *      切换子 tab 时以 DUIN_VALUECHANGED（ctrlId 为 kIdDetailScreen，extra 为子 tab 序号）
 *      通知 AdminFrame，由它决定 DLL 页面显示与否；
 *    · 「功能开关」tab（2026-10-08）：功能开关页（FeatureSwitchesPage）铺满主体区。原先平台超管在企业设置
 *      弹窗里、企业管理员在平台设置页里设置的全部开关都集中到这里，两种角色看到的内容相同；切到这个 tab 时
 *      AdminFrame 收到上面那条通知后读取该企业的设置。
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
namespace balloonwjui { class DuiEdit; } // 组织树搜索框用的单行输入框

namespace flamingoadmin {

namespace ui { class IconButton; }           // 组织树工具条上的三个图标按钮
namespace ui { class LinkPageSlot; }         // 「在线用户」子 tab 的占位控件

class OrgDetailPanel;
class ServicesPanel;
class FeatureSwitchesPage;

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

    // 当前是否在「在线用户」子 tab。
    bool IsOnlineTab() const { return m_subTab == kTabOnline; }

    // 当前是否在「功能开关」子 tab。
    bool IsFeaturesTab() const { return m_subTab == kTabFeatures; }

    // 子 tab 序号（SelectSubTab 的参数，也是切换通知 DUIN_VALUECHANGED 的 extra）
    enum
    {
        kTabOrg      = 0,   // 组织架构
        kTabSvc      = 1,   // 服务号
        kTabOnline   = 2,   // 在线用户（FlamingoLink.dll 可用时才有）
        kTabFeatures = 3,   // 功能开关（2026-10-08；没有「在线用户」时紧跟在服务号之后显示，序号不变）
    };

    // 切换到指定子 tab。供 AdminFrame 的行菜单「打开详情 → 服务号 / 功能开关」在详情加载完成后
    // 定位到目标 tab；须在 SetEnterprise 之后调用。
    //   tab：目标子 tab 序号，取值见上面的枚举；非法值与不可用的「在线用户」忽略。
    // 副作用：切换成功时以 DUIN_VALUECHANGED 通知 AdminFrame。
    void SelectSubTab(int tab);

    // 当前子 tab 的序号，取值见上面的枚举。
    int GetSubTab() const { return m_subTab; }

    // 某个子 tab 在子 tab 条上的矩形（host-client 坐标，单元测试核对摆放与模拟点击用）；须在 Layout 之后读取。
    //   tab：子 tab 序号；不可用的「在线用户」与非法序号返回空矩形。
    RECT SubTabRect(int tab) const;

    // 设置「在线用户」子 tab 是否可用：FlamingoLink.dll 加载成功时由 AdminFrame 置为 true。
    // 不可用时不画这个 tab、也不响应点击；正停在这个 tab 上时切回组织架构。
    //   available：是否可用。
    // 副作用：重新排版并重绘本屏。
    void SetOnlineTabAvailable(bool available);

    // 「在线用户」子 tab 是否可用。
    bool IsOnlineTabAvailable() const { return m_onlineAvailable; }

    // 「在线用户」子 tab 的占位控件（借用，所有权属本屏）。AdminFrame 给它注册排版回调。
    ui::LinkPageSlot* GetOnlineSlot() const { return m_onlineSlot; }

    // 当前企业的名称；没有载入企业时为空串。
    CString GetEnterpriseName() const;

    // 按当前子 tab 设置本屏输入框的显隐：组织树搜索框只在「组织架构」tab 下可见。
    // AdminFrame::SetEditsVisible 打开叠层弹窗时隐藏、关闭时恢复外壳里的全部输入框，恢复之后
    // 须调用本方法纠正，否则在别的 tab 下搜索框会从「在线用户」页面左上角的边距里露出来。
    // 副作用：改变搜索框可见性。
    void ApplySubTabEditVisibility();

    // 取服务号面板（AdminFrame 处理服务号动作时查询其选中项等）。
    ServicesPanel* GetServicesPanel() const { return m_servicesPanel; }

    // 取「功能开关」子 tab 的功能开关页（借用，所有权属本屏）。AdminFrame 为它接上请求出口、切到该 tab 时
    // 调用其 Load，并把开关通知与接口应答转交给它。
    FeatureSwitchesPage* GetFeaturesPage() const { return m_featuresPage; }

    // 取组织详情面板（AdminFrame 处理角色 / 权限动作时读写它）。
    OrgDetailPanel* GetOrgDetailPanel() const { return m_panel; }

    // 选中并定位到某节点（新增节点后由 AdminFrame 调用）。
    void SelectNode(const CString& nodeId);

    // 组织树搜索框文字变化时调用(由 AdminFrame 收到 DUIN_VALUECHANGED 转发):
    // 读搜索框当前文本,按部门名 / 员工姓名过滤组织树。
    void OnOrgSearchChanged();

    // 展开组织树的全部部门。工具条「展开全部」按钮的 DUIN_CLICK 直接发到
    // AdminFrame(NotifyParent 只通知宿主窗口,不沿控件父链冒泡),故由
    // AdminFrame 收到通知后转调本方法。
    void ExpandAllOrg();

    // 折叠组织树(只保留根节点展开)。调用来源同 ExpandAllOrg。
    void CollapseAllOrg();

    // 按登录者角色切换头部的显示内容，进入主界面时由 AdminFrame 调用（换账号登录时会再调一次）。
    //   platformAdmin：true 为平台超管，显示「返回」「企业设置」「发布公告」，面包屑首段为「企业管理」；
    //                  false 为企业管理员，本屏就是它的「本企业」入口，没有可返回的企业列表，
    //                  企业设置与发布公告也只对平台超管开放，三个按钮都隐藏，面包屑首段改为「本企业」。
    // 副作用：改变按钮可见性并重绘本屏。
    void SetPlatformAdminMode(bool platformAdmin);

    // 当前是否按平台超管显示（供测试与 AdminFrame 查询）。
    bool IsPlatformAdminMode() const { return m_platformAdmin; }

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

    // 切换子 tab：按 tab 切换各子控件的显隐，并以 DUIN_VALUECHANGED 通知 AdminFrame。
    //   tab：子 tab 序号，取值见 public 部分的枚举。
    void SetSubTab(int tab);

    CString          m_entId;
    const Enterprise* m_ent  = nullptr;          // 当前企业（借用）
    int              m_subTab = kTabOrg;         // 当前子 tab
    bool             m_platformAdmin = true;     // 是否按平台超管显示头部，由 SetPlatformAdminMode 设置

    OrgTree*         m_tree  = nullptr;          // 组织树（子控件）
    balloonwjui::DuiEdit* m_orgSearch = nullptr;  // 组织树搜索框（工具条左侧,子控件）

    // 组织树工具条右侧的三个图标按钮（子控件，从左到右）。仅在「组织架构」
    // 子 tab 下可见，切到「服务号」tab 时由 SetSubTab 隐藏。
    ui::IconButton*  m_btnOrgRefresh   = nullptr;   // 刷新：重新拉取部门树
    ui::IconButton*  m_btnOrgExpandAll = nullptr;   // 展开全部
    ui::IconButton*  m_btnOrgCollapse  = nullptr;   // 折叠
    OrgDetailPanel*  m_panel = nullptr;          // 组织详情面板（子控件）
    ServicesPanel*   m_servicesPanel = nullptr;  // 服务号面板（子控件）
    ui::LinkPageSlot* m_onlineSlot = nullptr;    // 「在线用户」子 tab 的占位控件（子控件）
    FeatureSwitchesPage* m_featuresPage = nullptr;  // 「功能开关」子 tab 的功能开关页（子控件）
    bool             m_onlineAvailable = false;  // 「在线用户」子 tab 是否可用（FlamingoLink.dll 加载成功）
    balloonwjui::DuiButton*  m_btnBack     = nullptr;    // 返回
    balloonwjui::DuiButton*  m_btnSettings = nullptr;    // 企业设置
    balloonwjui::DuiButton*  m_btnAnnounce = nullptr;    // 发布公告
    balloonwjui::DuiButton*  m_btnCreateGroup = nullptr; // 新建群（仅组织 tab 显示）
    balloonwjui::DuiButton*  m_btnPrimary  = nullptr;    // 主操作（新增员工 / 新建服务号）

    // Layout 时算好的可点装饰区（host 客户区坐标）。工具条上的展开 / 折叠
    // 原本也是这样的自绘热区，现已换成 ui::IconButton 子控件。
    RECT m_rcTabOrg{};      // 子 tab：组织架构
    RECT m_rcTabSvc{};      // 子 tab：服务号
    RECT m_rcTabOnline{};   // 子 tab：在线用户（不可用时为空矩形）
    RECT m_rcTabFeatures{}; // 子 tab：功能开关
    RECT m_rcAddTopDept{};  // 页脚：新增顶级部门

    Pending m_pending = Pending::None;   // 上一次上报的动作
    CString m_pendingNodeId;             // 上一次上报动作的目标节点
};

} // namespace flamingoadmin
