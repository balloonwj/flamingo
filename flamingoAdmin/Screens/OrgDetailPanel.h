/**
 *  flamingoAdmin —— 组织详情面板 OrgDetailPanel。
 *
 *  企业详情屏「组织架构」tab 右栏：根据组织树当前选中节点，呈现
 *    · 未选中 —— 居中占位提示；
 *    · 选中部门 —— 部门头 + 下属部门卡片 + 直接挂载员工表；
 *    · 选中员工 —— 资料头 + 6 个 tab 条 + 基本信息 + 危险操作。
 *
 *  本控件是【自绘控件，无子控件】，内置滚轮滚动；所有可点区域（动作按钮、
 *  子部门卡片、员工行、tab 条）在 OnPaint 时记录矩形、在 OnLButtonUp 里
 *  按坐标命中。与外部通过【回调】通信，不走 WM_DUI_NOTIFY。
 *
 *  注：Phase 4a-① 仅实现员工详情的「基本信息」tab，其余 5 个 tab 显示
 *  占位文案，待 4a-② 接入。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Widgets/OrgTree.h"
#include "Mock/MockData.h"
#include "Util/AdminApi.h"   // PermissionEntry / PermOverride

#include <vector>

namespace balloonwjui {
class DuiEditHost;     // 基本信息各值字段：只读 DuiEditHost（可选中 + 自带右键菜单）
}

namespace flamingoadmin {

class OverlayScrollbar;   // 本面板右缘的悬浮 auto-hide 滚动条子控件

// 组织详情面板。
class OrgDetailPanel : public balloonwjui::DuiControl
{
public:
    // 选中节点变化回调（点击子部门卡片 / 员工行时触发）。
    typedef void (*SelectFn)(void* user, const CString& nodeId);
    // 节点动作回调（新增子部门 / 新增员工 / 删除）。复用 OrgTree::Action。
    typedef void (*ActionFn)(void* user, OrgTree::Action action,
                             const CString& nodeId);

    OrgDetailPanel();

    // 注册回调。
    void SetCallbacks(SelectFn selFn, ActionFn actFn, void* user);

    // 设置当前展示的节点。
    //   node：选中节点；nullptr 表示未选中（显示占位）。
    //   ent：所属企业（部门为根节点时用其名称 / 头像）。
    void SetNode(const OrgNode* node, const Enterprise* ent);

    // 用从服务端取回的权限矩阵填充「权限与角色」tab。AdminFrame 收到
    // user/getpermissions.do 应答后调用。
    //   role：员工角色（0 普通成员 / 1 部门负责人 / 2 企业管理员）。
    //   items：权限矩阵各项；本函数按 key 匹配到内置 13 项权限。
    void SetPermissionData(int role, const std::vector<PermissionEntry>& items);

    // 取「权限与角色」tab 当前选中的角色（供 AdminFrame 调 setrole.do）。
    int GetUserRole() const { return m_userRole; }

    // 取当前与角色默认不同的权限覆盖项（供 AdminFrame 调 setpermissions.do）。
    std::vector<PermOverride> GetPermOverrides() const;

    // 用从服务端取回的登录数据填充「设备登录」tab。AdminFrame 收到
    // user/loginrecords.do 应答后调用。
    //   devices：当前在线设备；records：历史登录记录。
    void SetLoginData(const std::vector<LoginDevice>& devices,
                      const std::vector<LoginRecord>& records);

    // 用从服务端取回的操作历史填充「操作历史」tab。AdminFrame 收到
    // user/activity.do 应答后调用。
    void SetActivityData(const std::vector<ActivityEntry>& items);

    // 用从服务端取回的消息统计填充「消息与会话」tab。AdminFrame 收到
    // user/messagestats.do 应答后调用。
    //   sent / received / conversations：发送 / 接收 / 会话数。
    //   trend：近 14 天每日发送趋势。
    void SetMessageStats(int sent, int received, int conversations,
                         const std::vector<MessageTrendPoint>& trend);

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // 一个可点击的命中区。
    struct Hit
    {
        RECT    m_rc;       // 矩形（host 客户区坐标，已应用滚动）
        int     m_kind;     // 0=切 tab 1=选节点 2=动作按钮 3=切权限开关 4=选角色
        int     m_arg;      // kind=0:tab 下标；kind=2:OrgTree::Action；
                            // kind=3:权限下标；kind=4:角色下标
        CString m_nodeId;   // kind=1/2 涉及的节点 ID
    };

    // 三种内容的绘制（返回内容总高度，供滚动夹取用）。
    int PaintEmpty(HDC hdc);
    int PaintDept (HDC hdc);
    int PaintUser (HDC hdc);

    // 渲染一条基本信息字段：自绘 label + 圆角框，把值字段交给
    // m_basicValueEdits[idx] 子控件（只读 DuiEditHost）；若该行没完整落在
    // 面板可见区，则用 DrawKVCellValueOnly 兜底自绘（HWND 子窗口不随面板
    // GDI 裁剪，部分落外会画到面板外，故滚动跨边界时退回自绘）。
    //   hdc / x / y / w：单元格左上角与宽度；
    //   idx：fields[] 索引，决定用 m_basicValueEdits 里的哪一个；
    //   label / value：字段标签与值（value 空时显示"—"）。
    void RenderBasicField(HDC hdc, int idx, int x, int y, int w,
                          LPCTSTR label, const CString& value);

    // 懒创建 11 个基本信息值字段的 EDIT 子 HWND，并给每个挂滚轮转发子类
    // 过程（幂等）。必须在 host HWND 就绪后调（OnPaint 期间 GetHost() 有效）；
    // 未就绪时本次跳过、下次重绘再试，直到全部就绪。
    void EnsureBasicEditsCreated();

    // 在 OnPaint 末尾按本帧标记（m_basicEditShown）统一同步各值字段 EDIT
    // 的显隐。SetVisible 幂等 —— 未变化不触发 ShowWindow，避免每帧
    // hide→show 造成 HWND 闪烁。
    void ApplyBasicEditVisibility();

    // 滚轮转发子类过程：鼠标悬在某值字段 EDIT 上滚动时，EDIT 子窗口会截走
    // WM_MOUSEWHEEL（只读单行 EDIT 自身又无内容可滚），导致"停在字段上滚不动
    // 面板"。本过程把滚轮转交本面板 OnMouseWheel 实现正常滚动。
    //   dwRefData：SetWindowSubclass 时传入的本 OrgDetailPanel 实例。
    static LRESULT CALLBACK FieldEditWheelProc(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam,
                                               UINT_PTR uIdSubclass,
                                               DWORD_PTR dwRefData);

    // 员工详情各 tab 的内容绘制。入参 y 为内容起始坐标，返回绘制后的 y。
    int PaintUserBasic   (HDC hdc, int y);   // 基本信息
    int PaintUserRoles   (HDC hdc, int y);   // 权限与角色
    int PaintUserServices(HDC hdc, int y);   // 服务号
    int PaintUserSessions(HDC hdc, int y);   // 消息与会话
    int PaintUserDevices (HDC hdc, int y);   // 设备登录
    int PaintUserHistory (HDC hdc, int y);   // 操作历史

    // 把滚动位置夹到合法范围。
    void ClampScroll();

    // 悬浮滚动条拖动滑块时的回调（注册给 OverlayScrollbar）：把拖动产生的
    // 新滚动偏移写回本面板并请求重绘。
    //   ctx：本 OrgDetailPanel 实例；newScroll：拖动产生的新滚动偏移（像素）。
    static void OnPanelSbScroll(void* ctx, int newScroll);

    const OrgNode*     m_node    = nullptr;  // 当前节点（借用）
    const Enterprise*  m_ent     = nullptr;  // 所属企业（借用）
    int                m_userTab = 0;        // 员工详情当前 tab（0=基本信息）
    int                m_scrollY = 0;        // 纵向滚动偏移
    int                m_contentH = 0;       // 上次绘制得到的内容总高度
    CString            m_curUserId;          // 当前选中员工 ID（用于判断 SetNode 是否换了员工）

    // 「权限与角色」tab 的交互状态。切换员工时清空，待 user/getpermissions.do
    // 应答经 SetPermissionData 填入真实数据。
    int                m_userRole = 0;       // 员工角色（服务端口径：0 普通成员/1 部门负责人/2 企业管理员）
    std::vector<char>  m_userPerms;          // 13 项权限的当前生效开关（0/1），与 kPerms 同序
    std::vector<char>  m_permDefault;        // 13 项权限的角色默认值（0/1），与 kPerms 同序

    // 「设备登录」「操作历史」tab 的数据。切换员工时清空，待对应接口应答
    // 经 SetLoginData / SetActivityData 填入。
    std::vector<LoginDevice>   m_loginDevices;  // 当前在线设备
    std::vector<LoginRecord>   m_loginRecords;  // 历史登录记录
    std::vector<ActivityEntry> m_activity;      // 操作历史时间线

    // 「消息与会话」tab 的数据。切换员工时清空，待 user/messagestats.do 应答
    // 经 SetMessageStats 填入。
    int  m_msgSent          = 0;      // 发送消息数
    int  m_msgReceived      = 0;      // 接收消息数
    int  m_msgConversations = 0;      // 单聊会话数
    bool m_msgStatsLoaded   = false;  // 是否已收到消息统计
    std::vector<MessageTrendPoint> m_msgTrend;  // 近 14 天每日发送趋势

    std::vector<Hit>   m_hits;               // 本帧记录的命中区

    // 本面板右缘的悬浮 auto-hide 滚动条（子控件，所有权归 m_children；
    // 此处仅借用裸指针，生命周期与本面板一致）。
    OverlayScrollbar*  m_panelSb = nullptr;

    // 基本信息 11 个值字段（姓名 / 职位 / 登录账号 / 工号 / 入职日期 /
    // 邮箱 / 手机号 / 座机 / 工位地址 / 性别 / 个性签名），全部用只读
    // DuiEditHost（寄宿真 Win32 EDIT）接入，让每个值都是真正的只读文本框：
    // 原生鼠标拖选 + 右键菜单（复制 / 全选）+ Ctrl+C / Ctrl+A。
    //
    // 与 PaintUserBasic 内 fields[] 数组**同序**：索引 i 处的子控件对应
    // fields[i] 的值字段。改 fields[] 顺序时必须同步检查所有 idx 计算。
    //
    // 单个字段在「该行完整落在面板可见区」时显形；滚出可见区时由
    // PaintUserBasic 自绘文本兜底、对应 EDIT 隐藏，避免 HWND 子窗口画到
    // 面板外（HWND 不随面板 GDI 裁剪）。切屏时由 ScreenHost::ShowScreen 的
    // SetVisible(false) 经 balloonui SyncHwndVisibilitySubtree_ 自动隐藏。
    //
    // 所有权归 m_children；本数组仅借用裸指针。
    std::vector<balloonwjui::DuiEditHost*>  m_basicValueEdits;

    // 本帧各值字段是否应显示（与 m_basicValueEdits 同序同长）。OnPaint 开头
    // 清零，RenderBasicField 命中显形的置 1，OnPaint 末尾据此统一 reconcile。
    std::vector<char>                       m_basicEditShown;

    // 11 个值字段 EDIT 是否都已创建并挂好滚轮子类（EnsureBasicEditsCreated
    // 全部就绪后置 true，后续重绘跳过创建逻辑）。
    bool                                    m_basicEditsReady = false;

    SelectFn           m_selFn   = nullptr;
    ActionFn           m_actFn   = nullptr;
    void*              m_cbUser  = nullptr;
};

} // namespace flamingoadmin
