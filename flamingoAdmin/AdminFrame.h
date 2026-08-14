/**
 *  flamingoAdmin —— 主窗口 AdminFrame。
 *
 *  继承 balloonui 的 DuiFrameWindow，负责：
 *    · 登录门控：未登录显示登录界面，登录后切换到主界面外壳；
 *    · 屏路由：外壳内根据侧边栏点击切换 5 个主界面；
 *    · 事件汇聚：所有控件的 WM_DUI_NOTIFY 在此分发；
 *    · 新增企业弹窗的开 / 关 / 提交；
 *    · 全局 Toast 提示。
 *
 *  三条关键设计，均为避免崩溃：
 *  (1) 界面切换延迟执行：会变动可见性 / 数据的导航动作经
 *      PostMessage(WM_APP_NAVIGATE) 延迟到 DUI 事件派发结束之后再执行。
 *  (2) 页面 / 屏 / 弹窗一律常驻、切换只切显隐：登录界面、主界面外壳、5 个
 *      主界面、新增企业弹窗一经构建便常驻，<u>绝不销毁</u> —— 销毁一棵宿主
 *      仍在跟踪（hover / 焦点）的控件树会留下悬空指针而崩溃。
 *  (3) 弹窗打开时隐藏外壳内的真 EDIT 子窗：HWND 子窗总是绘制在 DUI 背景
 *      之上，不隐藏会透过半透明遮罩显形。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Controls/Window/DuiFrameWindow.h"
#include "DuiNotify.h"
#include "Widgets/Toast.h"
#include "Util/AdminApi.h"   // ApiTag / HttpResult / AdminSession / 领域结构
#include "Util/AppConfig.h"  // LoginConfig（"记住密码"暂存成员需完整类型）

#include <memory>
#include <string>
#include <vector>

// 自定义消息：延迟执行的导航动作。wParam 为导航命令码（见 AdminFrame.cpp）。
#define WM_APP_NAVIGATE  (WM_APP + 1)

// 自定义消息：网络测试工作线程回传结果。wParam 为 ProbeResult。
#define WM_APP_NETTEST_RESULT  (WM_APP + 2)

// 自定义消息：HTTP 工作线程回传的登录应答。lParam 为 new 出来的 HttpResult*，
// 由消息处理函数负责 delete。
#define WM_APP_LOGIN_RESULT  (WM_APP + 3)

// 自定义消息：HTTP 工作线程回传的业务 API 应答。wParam 为 ApiTag（标明是哪个
// 接口），lParam 为 new 出来的 HttpResult*，由消息处理函数负责 delete。
#define WM_APP_API_RESULT  (WM_APP + 4)

// 自定义消息：EmployeeEditDialog 收 WM_DESTROY 时投递的延迟 delete。dialog
// 不能在自己 message handler 里 self-delete(栈展开还要回它的成员),所以
// 由 OnEmployeeEditDialogDestroyed PostMessage 到主窗口,等当前 WM_DESTROY
// 栈完全展开后再 delete。wParam 携带 EmployeeEditDialog* 指针。
#define WM_APP_DELETE_EMP_EDIT_DIALOG  (WM_APP + 5)

// 同理:ConfirmDialog WM_DESTROY 后的延迟 delete。wParam 携带 ConfirmDialog*。
#define WM_APP_DELETE_CONFIRM_DIALOG   (WM_APP + 6)

// 同理:NodeModal(新增部门 / 新增员工对话框)WM_DESTROY 后的延迟 delete。
// wParam 携带 NodeModal*。
// 注意取 +8 而不是紧接上一条的 +7 —— +7 已被下面的 WM_APP_ORG_FILTER 占用。
// 两条消息若同号,BEGIN_MSG_MAP 里靠前的 handler 会把另一条吃掉:组织树搜索
// 过滤将永不生效,而过滤消息的 wParam(0)还会被当成 NodeModal* 去 delete。
#define WM_APP_DELETE_NODE_MODAL       (WM_APP + 8)

// 组织树搜索:把"按搜索框文本过滤组织树"延迟到 EN_CHANGE 派发结束之后再做。
// 若在 EN_CHANGE 同一消息周期里同步过滤(触发树重绘),搜索框 EDIT 子窗口刚
// 输入的文字不会被画出来(须再点一下),故延迟一拍。详见 OnOrgFilter。
#define WM_APP_ORG_FILTER              (WM_APP + 7)

namespace flamingoadmin {

class Sidebar;
class TopBar;
class ScreenHost;
class DashboardScreen;
class EnterprisesScreen;
class EnterpriseDetailScreen;
class AnnouncementsScreen;
class AuditScreen;
class SettingsScreen;
class NetworkSettingsScreen;
class NewEnterpriseModal;
class NodeModal;
class ConfirmDialog;
struct ConfirmDialogSpec;
class ServiceModal;
class BindingModal;
class ComposeModal;
class GroupModal;
class EmployeeEditDialog;
class EnterpriseSettingsModal;
class ClientVersionsScreen;
class ClientVersionModal;

// 屏路由枚举。前 5 个对应侧边栏导航项（数值顺序与导航项一致）；
// EnterpriseDetail 是「企业管理」的子屏（点企业行进入），不在侧栏。
enum class Screen
{
    Dashboard,        // 仪表板
    Enterprises,      // 企业管理
    Announcements,    // 公告中心
    Audit,            // 审计日志
    Settings,         // 平台设置
    // 客户端版本：仅平台超管可见。位置必须排在 EnterpriseDetail 之前 ——
    // 前 6 项与侧栏导航项 kIdNav* 一一对应且数值连续，EnterpriseDetail 是
    // 没有导航项的子屏，只能挂在末尾。
    ClientVersions,   // 客户端版本（发布升级包、发布 / 撤回）
    EnterpriseDetail, // 企业详情（企业管理的子屏）
};

// 主界面数量（与 Screen 枚举值数量一致）。
const int kScreenCount = 7;

// 主窗口。
class AdminFrame : public balloonwjui::DuiFrameWindow
{
    // EmployeeEditDialog 在自己 submit / 校验路径上需要调 AdminFrame 的
    // ShowToast(私有)与 OnEmployeeEditDialogSubmit / Destroyed 回调(私有,
    // 只让 dialog 自己访问)。声明 friend 以最小改动满足耦合,而不必把
    // 这些接口提到 public 区(它们对 admin 其它代码并非通用 API)。
    friend class EmployeeEditDialog;
    friend class ConfirmDialog;
    //NodeModal 同理:它在自己的提交 / 销毁路径上要调 ShowToast 与
    //OnNodeModalSubmit / Destroyed 这几个私有成员。
    friend class NodeModal;

public:
    BEGIN_MSG_MAP(AdminFrame)
        MESSAGE_HANDLER(WM_DESTROY,      OnDestroy)
        MESSAGE_HANDLER(WM_TIMER,        OnTimer)
        MESSAGE_HANDLER(WM_DUI_NOTIFY,   OnDuiNotify)
        MESSAGE_HANDLER(WM_APP_NAVIGATE, OnNavigate)
        MESSAGE_HANDLER(WM_APP_NETTEST_RESULT, OnNetTestResult)
        MESSAGE_HANDLER(WM_APP_LOGIN_RESULT, OnLoginResult)
        MESSAGE_HANDLER(WM_APP_API_RESULT, OnApiResult)
        MESSAGE_HANDLER(WM_APP_DELETE_EMP_EDIT_DIALOG, OnDeleteEmpEditDialog)
        MESSAGE_HANDLER(WM_APP_DELETE_CONFIRM_DIALOG,  OnDeleteConfirmDialog)
        MESSAGE_HANDLER(WM_APP_DELETE_NODE_MODAL,      OnDeleteNodeModal)
        MESSAGE_HANDLER(WM_APP_ORG_FILTER,             OnOrgFilter)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

    // 安装初始界面（构建根容器 + 登录界面 + 新增企业弹窗）。
    // 须在 Create 之后调用一次。
    void InitContent();

    // 把新增企业弹窗里的真 EDIT 子窗藏好。须由 WinMain 在主窗口
    // ShowWindow 之前、且 InitContent + 一次布局之后调用一次。
    void HideModalEdits();

    // 切换调试控制台窗口的显隐。F12 键（由 WinMain 消息循环拦截）与顶栏
    // 「调试控制台」按钮共用此入口；窗口懒创建，重复调用安全。
    void ToggleDebugConsole();

private:
    // 主窗口销毁 → 退出消息循环。
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 定时器：驱动 Toast 到时自动隐藏。
    LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 汇聚所有子控件的 WM_DUI_NOTIFY，按通知码 + ctrlId 分发。
    LRESULT OnDuiNotify(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 延迟导航动作处理（由 PostMessage(WM_APP_NAVIGATE) 触发）。
    LRESULT OnNavigate(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 网络测试结果处理（由测试工作线程 PostMessage 回传）。
    LRESULT OnNetTestResult(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 登录结果处理（由 HTTP 工作线程 PostMessage 回传）。lParam 携带的
    // HttpResult* 在本函数内被接管并 delete。
    LRESULT OnLoginResult(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 业务 API 结果处理（由 HTTP 工作线程 PostMessage 回传）。按 wParam 的
    // ApiTag 把结果派发到对应业务屏；并集中处理「会话失效」。lParam 携带的
    // HttpResult* 在本函数内被接管并 delete。
    LRESULT OnApiResult(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 异步 delete EmployeeEditDialog 入口。Dialog 在自己 WM_DESTROY 里
    // 不能 self-delete(栈展开还要走它成员),由 OnEmployeeEditDialogDestroyed
    // PostMessage 到此延迟 delete。wParam 是 EmployeeEditDialog* 指针。
    LRESULT OnDeleteEmpEditDialog(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 同理:异步 delete ConfirmDialog 入口。wParam 是 ConfirmDialog*。
    LRESULT OnDeleteConfirmDialog(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 同理:异步 delete NodeModal 入口。wParam 是 NodeModal*。
    LRESULT OnDeleteNodeModal(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 组织树搜索过滤(延迟执行)入口。由 kIdOrgSearch 的 DUIN_VALUECHANGED
    // PostMessage(WM_APP_ORG_FILTER) 触发,延后一拍按搜索框文本过滤组织树,
    // 避免在 EN_CHANGE 同周期同步重绘把刚输入的文字盖掉。
    LRESULT OnOrgFilter(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 鼠标左键按下：默认不拦截左键（列表选中、双击等照常），只「附加」一件
    // 事 —— 在企业管理界面点中某行「操作」列的三个点时，弹出该行的操作菜单。
    // 双击进详情已改由 balloonui 的双击合成（DuiHost::EnableDoubleClick）经
    // DUIN_DBLCLK 上报，见 OnDuiNotify，不在此处理。
    LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 登录成功 → 构建（首次）并显示主界面外壳。
    void EnterShell();

    // 切换内容区到指定主界面（含数据刷新与侧栏 / 面包屑同步）。
    void SwitchScreen(Screen screen);

    // 处理登出 → 显示登录界面。
    void HandleLogout();

    // 处理会话失效（任一业务接口返回 code=2）：清会话、退回登录页并提示。
    void HandleSessionExpired();

    // 折叠 / 展开侧边栏（标准 224px ↔ 紧凑 64px）。
    void ToggleSidebar();

    // 从登录界面进入网络设置页（载入当前配置）。
    void ShowNetworkSettings();

    // 从网络设置页返回登录界面（不保存）。
    void BackToLoginFromNetwork();

    // 保存网络设置：校验 → 写 AppConfig 并落盘 → 刷新登录页信息 → 返回登录。
    void SaveNetworkSettings();

    // 启动网络连通性测试：校验 → 起工作线程做 TCP 探测，结果经
    // WM_APP_NETTEST_RESULT 回传。
    void StartNetTest();

    // 校验登录表单：账户 / 密码任一为空则弹 Toast 并停留；通过则延迟进入外壳。
    void TrySubmitLogin();

    // 打开 / 关闭新增企业弹窗。
    void ShowNewEnterpriseModal();
    void HideNewEnterpriseModal();

    // 提交新增企业弹窗：校验 → 调 create.do → 应答回来后再关弹窗 / 提示。
    void SubmitNewEnterprise();

    // 处理 create.do 应答：成功则关弹窗、刷新列表、弹出初始密码；失败则提示。
    void OnEnterpriseCreated(const HttpResult& http);

    // 处理 setstatus.do / delete.do 应答（只关心成败）。
    //   http：HTTP 结果；okText：成功时的 Toast 文案。
    void OnEnterpriseActionDone(const HttpResult& http, LPCTSTR okText);

    // 处理 export.do 应答：成功则弹保存对话框把 CSV 写盘；失败则提示。
    void OnEnterpriseExported(const HttpResult& http);

    // 发起企业列表导出（export.do）。
    void StartEnterpriseExport();

    // 弹出保存对话框，把 CSV 文本（UTF-8 字节）写入用户所选文件。
    //   csvUtf8：CSV 文本，UTF-8 字节。
    //   defaultName：保存对话框预填的默认文件名。
    void SaveCsvToFile(const std::string& csvUtf8, LPCTSTR defaultName);

    // 把单个企业导成一行 CSV 并弹保存对话框（本地数据，无需联网）。
    //   ent：要导出的企业记录。
    void ExportSingleEnterprise(const Enterprise& ent);

    // 弹出企业行操作菜单（打开详情 / 企业设置 / 审核停用启用 / 删除 /
    // 复制 ID / 导出 CSV），并执行所选动作。
    void ShowEnterpriseMenu();

    // 发起加载企业详情：并发拉取 detail.do / dept/tree.do / service/list.do，
    // 三者都回来后注入 DataStore 并打开企业详情屏。
    //   enterpriseId：要查看的企业 ID。
    void StartLoadEnterpriseDetail(const CString& enterpriseId);

    // 处理企业详情三个并发请求之一的结果（按 tag 区分）。三者齐备后注入
    // DataStore 并切到企业详情屏。
    void OnDetailPartLoaded(ApiTag tag, const HttpResult& http);

    // ---- 仪表盘（模块 2）----

    // 切换到仪表盘时发起数据加载：并发拉 dashboard/stats.do（统计卡）与
    // enterprise/list.do（活跃企业列表）。
    void StartLoadDashboard();

    // 处理 dashboard/stats.do 应答：成功则把统计填入仪表盘统计卡。
    void OnDashboardStatsLoaded(const HttpResult& http);

    // 处理仪表盘「活跃企业」用的 enterprise/list.do 应答：成功则把企业列表
    // 注入 DataStore 并重绘仪表盘。
    void OnDashboardEnterprisesLoaded(const HttpResult& http);

    // 处理仪表盘「最新公告」用的 announcement/list.do 应答：注入 DataStore 并重绘。
    void OnDashboardAnnouncementsLoaded(const HttpResult& http);

    // 处理仪表盘「近期审计」用的 audit/list.do 应答：注入 DataStore 并重绘。
    void OnDashboardAuditLoaded(const HttpResult& http);

    // 处理仪表盘「近 14 天消息量」柱状图的 dashboard/messagetrend.do 应答。
    void OnDashboardTrendLoaded(const HttpResult& http);

    // ---- 审计日志（模块 8）----

    // 拉取审计日志当前页（audit/list.do）。切到审计界面 / 点刷新 / 切换筛选 /
    // 翻页时调用 —— 页码与筛选均取自 AuditScreen 的当前状态。
    void StartLoadAudit();
    // ---- 客户端版本（模块 10，仅平台超管）----
    // 拉取版本记录列表；切进「客户端版本」界面与点「刷新」时调用。
    void StartLoadClientVersions();
    // 扫描服务器升级包目录；点「新建版本」时先扫一次，拿到候选包再开弹窗。
    void StartScanPackages();
    // 对某条版本记录执行发布 / 撤回。
    //   action：动作种类；id：目标记录 ID。
    void StartClientVersionAction(int action, long long id);
    void OnClientVersionsLoaded(const HttpResult& http);
    void OnPackagesScanned(const HttpResult& http);
    // 发布 / 撤回 / 修改 的应答统一走这里：成功就提示并重拉列表。
    void OnClientVersionActionDone(const HttpResult& http);
    // 处理「客户端版本」界面上报的行内操作：取出待处理动作后发对应请求。
    void HandleClientVersionRowAction();
    // 打开新建版本弹窗（候选包已由 scanpackages 应答带回）。
    //   scan：扫描结果，装进弹窗的候选包列表。
    void ShowClientVersionModal(const adminapi::ScanPackagesResult& scan);
    void HideClientVersionModal();
    // 提交新建版本。publish 为 true 直接发布，false 存为草稿。
    void SubmitClientVersionModal(bool publish);



    // 拉取审计统计概览（audit/stats.do），用于审计界面 4 个统计卡。
    void StartLoadAuditStats();

    // 发起审计日志导出（audit/export.do），按当前筛选导出。
    void StartAuditExport();

    // 处理 audit/list.do 应答：成功则把本页审计记录注入 DataStore、刷新分页。
    void OnAuditListLoaded(const HttpResult& http);

    // 处理 audit/stats.do 应答：成功则填充审计界面 4 个统计卡。
    void OnAuditStatsLoaded(const HttpResult& http);

    // 处理 audit/export.do 应答：成功则弹保存对话框把 CSV 写盘。
    void OnAuditExported(const HttpResult& http);

    // ---- 平台设置（模块 1）----

    // 切换到平台设置界面时拉取当前管理员资料（profile.do）。
    void StartLoadProfile();

    // 处理 profile.do 应答：成功则填充平台设置界面的「个人资料」卡。
    void OnProfileLoaded(const HttpResult& http);

    // 提交「保存个人资料」（updateprofile.do）。
    void SubmitUpdateProfile();

    // 处理 updateprofile.do 应答（只关心成败）。
    void OnProfileUpdated(const HttpResult& http);

    // 提交「修改密码」（changepassword.do），含客户端两次一致性校验。
    void SubmitChangePassword();

    // 处理 changepassword.do 应答：成功则清空密码输入框。
    void OnPasswordChanged(const HttpResult& http);

    // ---- 公告中心（模块 7）----

    // 切换到公告中心 / 公告写操作成功后，拉取公告列表（announcement/list.do）。
    void StartLoadAnnouncements();

    // 处理 announcement/list.do 应答：成功则把公告列表注入 DataStore 并刷新。
    void OnAnnouncementsLoaded(const HttpResult& http);

    // 拉取某公告的详情（announcement/detail.do），用于公告预览区。
    //   annId：目标公告 ID。
    void StartLoadAnnouncementDetail(const CString& annId);

    // 处理 announcement/detail.do 应答：成功则用详情覆盖 DataStore 中该公告。
    void OnAnnouncementDetailLoaded(const HttpResult& http);

    // 提交「切换置顶」（announcement/setpinned.do）—— 取当前选中公告的置顶
    // 状态、提交其反值。
    void SubmitAnnouncementSetPinned();

    // 处理 recall/delete/setpinned 应答（只关心成败）。
    //   http：HTTP 结果；okText：成功时的 Toast 文案。
    void OnAnnouncementActionDone(const HttpResult& http, LPCTSTR okText);

    // 处理 announcement/save.do 应答：成功则接着发起 send.do 发布。
    void OnAnnSaved(const HttpResult& http);

    // 处理 announcement/send.do 应答：成功则关弹窗、刷新公告列表。
    void OnAnnSent(const HttpResult& http);

    // 处理新建公告目标选择器的 dept/tree.do 应答：把组织树回填 ComposeModal。
    void OnComposeTreeLoaded(const HttpResult& http);

    // 把当前已载入的公告列表导出为 CSV（公告模块无服务端导出接口）。
    void ExportAnnouncementsCsv();

    // 发起「编辑公告」：拉取既有公告详情（announcement/detail.do）。
    void StartAnnouncementEdit(const CString& annId);

    // 处理「编辑公告」的 detail.do 应答：把既有公告回填新建公告弹窗并打开。
    void OnAnnEditFetchLoaded(const HttpResult& http);

    // 展示新建公告弹窗（预拉企业列表 + 隐藏外壳 EDIT + 显示弹窗）。
    // 由 ShowComposeModal（新建）与 OnAnnEditFetchLoaded（编辑）共用。
    void PresentComposeModal();

    // 打开 / 关闭 组织节点对话框（新增部门 / 员工）。对话框是独立顶层窗口，
    // ShowNodeModal 负责 new + CreateAndShow，HideNodeModal 只发起关闭
    // （真正的收尾在 OnNodeModalDestroyed）。提交入口是 OnNodeModalSubmit，
    // 由对话框自己在校验通过后回调，不再走 navigate 消息。
    void ShowNodeModal();
    void HideNodeModal();

    // 打开 / 关闭 / 提交 新建群弹窗（把企业成员拉进一个新群）。
    void ShowGroupModal();
    void HideGroupModal();
    void SubmitGroupModal();

    // 修改员工资料对话框(EmployeeEditDialog)的两个回调:
    //   · OnEmployeeEditDialogSubmit:dialog 内点击「保存」按钮后调用,
    //     AdminFrame 在此从 dialog->GetXxx 拉数据 + 调 MakeUserUpdateRequest
    //     发 HTTP。HTTP reply 在 OnUserUpdated 处理。
    //   · OnEmployeeEditDialogDestroyed:dialog 收 WM_DESTROY 时调用,
    //     AdminFrame 把 m_employeeEditDialog 指针清空。
    void OnEmployeeEditDialogSubmit(EmployeeEditDialog* dlg);
    void OnEmployeeEditDialogDestroyed(EmployeeEditDialog* dlg);

    // 组织节点对话框(NodeModal)的两个回调,与上面 EmployeeEditDialog 那对同构:
    //   · OnNodeModalSubmit:对话框内点「创建」且通过必填校验后调用,
    //     AdminFrame 在此按模式发建部门 / 建员工的 HTTP 请求。
    //   · OnNodeModalDestroyed:对话框收 WM_DESTROY 时调用,AdminFrame 清空
    //     m_nodeModal 指针并经 WM_APP_DELETE_NODE_MODAL 安排延迟 delete。
    void OnNodeModalSubmit(NodeModal* dlg);
    void OnNodeModalDestroyed(NodeModal* dlg);

    // 处理 user/detail.do 应答：成功则预填并打开修改员工资料弹窗。
    void OnUserDetailLoaded(const HttpResult& http);

    // 处理 user/update.do 应答：成功则关弹窗、刷新组织树。
    void OnUserUpdated(const HttpResult& http);

    // 弹出部门选择菜单，选中目标部门后调 user/move.do 调整员工所属部门。
    void ShowDeptPickerMenu();

    // 处理 user/move.do 应答：成功则刷新组织树。
    void OnUserMoved(const HttpResult& http);

    // ---- 企业设置弹窗（模块 3：改企业信息 + 管理员管理）----

    // 打开企业设置弹窗：记下目标企业，拉取其管理员列表后填充并显示弹窗。
    // 企业列表行菜单与企业详情屏头部按钮共用此入口。
    //   ent：要管理的企业记录（拷贝保存到 m_entSettingsEnt）。
    void OpenEnterpriseSettings(const Enterprise& ent);

    // 关闭企业设置弹窗。
    void HideEnterpriseSettings();

    // 提交「保存企业信息」（enterprise/update.do）。
    void SubmitEnterpriseUpdate();

    // 提交「新增管理员」（enterprise/addadmin.do）。
    void SubmitAddAdmin();

    // 重新拉取当前企业的管理员列表（enterprise/admins.do）。
    void FireEnterpriseAdmins();

    // 处理 enterprise/admins.do 应答：首次打开弹窗 / 增删后刷新列表。
    void OnEnterpriseAdminsLoaded(const HttpResult& http);

    // 处理 enterprise/update.do 应答：成功则关弹窗。
    void OnEnterpriseUpdated(const HttpResult& http);

    // 处理 enterprise/addadmin.do 应答：成功则关弹窗并展示初始密码。
    void OnEnterpriseAdminAdded(const HttpResult& http);

    // 处理 enterprise/removeadmin.do 应答：成功则刷新管理员列表。
    void OnEnterpriseAdminRemoved(const HttpResult& http);

    // 处理 dept/create.do 应答：成功则关弹窗、刷新组织树。
    void OnDeptCreated(const HttpResult& http);

    // 处理 user/create.do 应答：成功则关弹窗、刷新组织树、展示初始密码。
    void OnUserCreated(const HttpResult& http);

    // 处理 group/create.do 应答：成功则关弹窗、提示已建群。
    void OnGroupCreated(const HttpResult& http);

    // 处理 dept/delete.do · user/delete.do 应答：成功则刷新组织树。
    void OnOrgNodeDeleted(const HttpResult& http);

    // 重新拉取当前企业的 dept/tree.do（应答里带各部门成员，故部门与员工一次
    // 刷完）。两类调用来源：组织写操作成功后的自动刷新，以及组织树工具条上
    // 「刷新」按钮的手动刷新。
    //   showToastOnDone：完成后是否弹「组织架构已刷新」提示。写操作后的自动
    //                    刷新传 false —— 那些流程自己已经弹过「已删除」之类
    //                    的提示，再弹一条会连出两个 toast；手动刷新传 true。
    void ReloadOrgTree(bool showToastOnDone = false);

    // 处理组织树重拉应答：覆盖 DataStore 的组织树并刷新企业详情屏。
    void OnOrgTreeReloaded(const HttpResult& http);

    // 按 spec 创建并显示通用确认/信息对话框(独立顶层窗口 + 真模态)。
    //   spec :标题 / 消息 / 按钮文字 / 模式 / 复制密码文本等。
    // 上一代 ShowConfirmModal/HideConfirmModal 配对已删除 —— 对话框
    // on-demand new/CreateAndShow,关闭走 WM_CLOSE 自销。
    void ShowConfirmDialog(const ConfirmDialogSpec& spec);

    // ConfirmDialog 的两个回调:
    //   · OnConfirmDialogConfirmed   :用户点「确认 / 知道了」按钮后调用。
    //     AdminFrame 据自身 m_confirmTarget / m_confirmDeleteNodeId 状态机
    //     执行待确认操作(原 ExecuteConfirmedDelete 的逻辑);完成后 dialog
    //     PostMessage WM_CLOSE 自销。
    //   · OnConfirmDialogDestroyed   :dialog WM_DESTROY 时调用,AdminFrame
    //     把 m_confirmDialog 指针清空 + 异步 delete 对象。
    void OnConfirmDialogConfirmed(ConfirmDialog* dlg);
    void OnConfirmDialogDestroyed(ConfirmDialog* dlg);

    // 打开 / 关闭 / 提交 新建服务号弹窗。
    void ShowServiceModal();
    void HideServiceModal();
    void SubmitServiceModal();

    // 停用 / 重新启用 当前选中的服务号。
    void ToggleServiceStatus();

    // 打开 / 关闭 / 保存 服务号绑定范围弹窗。
    void ShowBindingModal();
    void HideBindingModal();
    void SaveBindingModal();

    // 处理 service/save.do 应答：成功则关弹窗、刷新服务号列表。
    void OnServiceSaved(const HttpResult& http);

    // 处理 service/setstatus.do · delete.do · setbindings.do 应答（只关心成败）。
    //   http：HTTP 结果；okText：成功时的 Toast 文案。
    void OnServiceActionDone(const HttpResult& http, LPCTSTR okText);

    // 服务号写操作成功后，重新拉取当前企业的 service/list.do。
    void ReloadServiceList();

    // 处理服务号列表重拉应答：覆盖 DataStore 的服务号并刷新企业详情屏。
    void OnServiceListReloaded(const HttpResult& http);

    // ---- 企业详情子屏：角色与权限（模块 6）----

    // 发起拉取某员工的权限矩阵（user/getpermissions.do）。
    void FireGetPermissions(const CString& userId);

    // 处理 user/getpermissions.do 应答：把权限矩阵填入组织详情面板。
    void OnUserPermissionsLoaded(const HttpResult& http);

    // 处理 user/setrole.do 应答：成功后重拉权限矩阵（改角色会清空逐人覆盖）。
    void OnUserRoleSet(const HttpResult& http);

    // 处理 user/setpermissions.do 应答：失败时重拉权限矩阵纠正乐观状态。
    void OnUserPermsSet(const HttpResult& http);

    // 发起拉取某员工的登录记录（user/loginrecords.do）。
    void FireLoginRecords(const CString& userId);

    // 发起拉取某员工的操作历史（user/activity.do）。
    void FireActivity(const CString& userId);

    // 发起拉取某员工的消息与会话统计（user/messagestats.do）。
    void FireMessageStats(const CString& userId);

    // 处理 user/loginrecords.do 应答：把登录数据填入组织详情面板。
    void OnUserLoginRecordsLoaded(const HttpResult& http);

    // 处理 user/activity.do 应答：把操作历史填入组织详情面板。
    void OnUserActivityLoaded(const HttpResult& http);

    // 处理 user/messagestats.do 应答：把消息统计填入组织详情面板。
    void OnUserMessageStatsLoaded(const HttpResult& http);

    // 处理 user/setstatus.do 应答：成功后重拉组织树刷新员工状态。
    void OnUserStatusSet(const HttpResult& http);

    // 处理 user/resetpassword.do 应答：成功后用信息弹窗展示新密码。
    void OnUserPasswordReset(const HttpResult& http);

    // 处理 user/kickdevice.do 应答：成功后重拉登录记录刷新在线设备。
    void OnUserDevicesKicked(const HttpResult& http);

    // 打开 / 关闭 / 提交 新建公告弹窗。
    void ShowComposeModal();
    void HideComposeModal();
    void SubmitComposeModal();

    // Toast 语义。提示条按类型用"浅色语义底 + 同色系深字/图标"配色(见
    // ShowToast 实现), 三色一眼区分。caller 按业务语义选择。
    //   · Info(默认):淡绿底 + 深绿对勾, 3 秒。完成态 / 中性信息("已保存"、"列表已刷新")
    //   · Warn:淡黄底 + 深橙三角感叹号, 5 秒。非阻塞警示("请填写..."、"将在...开放")
    //   · Error:淡红底 + 深红叉号, 8 秒。错误 / 失败("登录失败"、"网络错误")
    enum class ToastKind
    {
        Info,    // 完成态 / 中性信息:淡绿底 + 深绿对勾, 3 秒
        Warn,    // 非阻塞警示:淡黄底 + 深橙三角感叹号, 5 秒
        Error,   // 错误 / 失败:淡红底 + 深红叉号, 8 秒
    };

    // 弹出一条 Toast 提示。
    //   text:提示文字;
    //   kind:语义色, 默认 Info(绿)。
    void ShowToast(LPCTSTR text, ToastKind kind = ToastKind::Info);

    // 给登录界面的账户 / 密码输入框预填示例值（与设计稿默认值一致）。
    void PrefillLogin();

    // 递归地对 node 子树里的每个 DuiEditHost 调一次 EnsureCreated。
    void EnsureEditsCreated(balloonwjui::DuiControl* node);

    // 递归地把 node 子树里每个 DuiEditHost 设为指定可见性。弹窗开 / 关时
    // 用它隐藏 / 恢复外壳内的真 EDIT 子窗，避免其透过半透明遮罩显形。
    void SetEditsVisible(balloonwjui::DuiControl* node, bool visible);

    // 递归地对 node 子树里每个 DuiEditHost 的真 EDIT 子窗调
    // ShowWindow(SW_HIDE)（直接操作 HWND，不改控件的 m_bVisible）。
    void HideEditHwnds(balloonwjui::DuiControl* node);

    bool                     m_loggedIn   = false;             // 是否已登录
    bool                     m_sidebarCollapsed = false;       // 侧边栏是否紧凑态
    bool                     m_netTestBusy = false;            // 网络测试是否进行中
    bool                     m_loginBusy   = false;            // 登录请求是否进行中
    // 本次提交登录用的账号 / 口令 / "记住密码"勾选状态。TrySubmitLogin 提交时暂存，
    // OnLoginResult 在登录成功那一支据它落盘。登录是异步的，其间用户可能又改了
    // 输入框，故不能等应答回来再去读控件。
    LoginConfig              m_pendingLogin;
    bool                     m_entCreateBusy = false;          // 新增企业请求是否进行中
    bool                     m_entExportBusy = false;          // 导出企业请求是否进行中
    bool                     m_auditExportBusy = false;        // 导出审计日志请求是否进行中
    bool                     m_updateProfileBusy = false;      // 保存个人资料请求是否进行中
    bool                     m_changePwBusy = false;           // 修改密码请求是否进行中
    bool                     m_annActionBusy = false;          // 公告撤回/删除/置顶请求是否进行中
    bool                     m_composeSubmitBusy = false;      // 新建公告 save+send 链是否进行中
    CString                  m_composeTreeEntId;               // 目标选择器在途组织树请求所属企业 ID
    bool                     m_nodeSubmitBusy = false;         // 新增部门/员工请求是否进行中
    bool                     m_svcSubmitBusy = false;          // 新建服务号请求是否进行中
    Screen                   m_route      = Screen::Dashboard; // 当前界面
    ui::RootContainer*       m_root       = nullptr;  // 客户区根容器（借用，frame 持有）
    balloonwjui::DuiToast*   m_toast      = nullptr;  // Toast 层(借用; balloonwjui::DuiToast)
    balloonwjui::DuiControl* m_loginPage  = nullptr;  // 登录界面页（借用，常驻）
    balloonwjui::DuiControl* m_networkPage = nullptr; // 网络设置页（借用，常驻）
    balloonwjui::DuiControl* m_shellPage  = nullptr;  // 主界面外壳页（借用，常驻）
    NetworkSettingsScreen*   m_networkScreen = nullptr; // 网络设置页（借用，= m_networkPage）
    Sidebar*                 m_sidebar    = nullptr;  // 侧栏（借用，在 m_shellPage 内）
    TopBar*                  m_topBar     = nullptr;  // 顶栏（借用，在 m_shellPage 内）
    ScreenHost*              m_screenHost = nullptr;  // 内容区屏宿主（借用，在 m_shellPage 内）
    DashboardScreen*         m_dashboard  = nullptr;  // 仪表板（借用，在 m_screenHost 内）
    EnterprisesScreen*       m_enterprises = nullptr; // 企业管理（借用，在 m_screenHost 内）
    EnterpriseDetailScreen*  m_enterpriseDetail = nullptr; // 企业详情（借用，在 m_screenHost 内）
    AnnouncementsScreen*     m_announcementsScreen = nullptr; // 公告中心（借用，在 m_screenHost 内）
    AuditScreen*             m_auditScreen = nullptr; // 审计日志（借用，在 m_screenHost 内）
    ClientVersionsScreen*    m_clientVersionsScreen = nullptr; // 客户端版本（借用，在 m_screenHost 内）
    ClientVersionModal*      m_clientVersionModal = nullptr;   // 新建版本弹窗（借用，在 m_root 弹窗层内）
    SettingsScreen*          m_settingsScreen = nullptr; // 平台设置（借用，在 m_screenHost 内）
    NewEnterpriseModal*      m_modal        = nullptr; // 新增企业弹窗（借用，在 m_root 内）
    // 组织节点对话框(新增部门 / 新增员工):独立顶层窗口,on-demand 创建/销毁。
    // 只在对话框显示期间非空;OnNodeModalDestroyed 收到通知后清回 nullptr。
    NodeModal*               m_nodeModal    = nullptr;
    // 通用确认/信息对话框:独立顶层窗口,on-demand 创建/销毁。
    // 只在弹窗显示期间非空;OnConfirmDialogDestroyed 收到通知后清回 nullptr。
    ConfirmDialog*           m_confirmDialog = nullptr;
    ServiceModal*            m_serviceModal = nullptr; // 新建服务号弹窗（借用，在 m_root 内）
    BindingModal*            m_bindingModal = nullptr; // 服务号绑定弹窗（借用，在 m_root 内）
    ComposeModal*            m_composeModal = nullptr; // 新建公告弹窗（借用，在 m_root 内）
    GroupModal*              m_groupModal   = nullptr; // 新建群弹窗（借用，在 m_root 内）
    bool                     m_groupSubmitBusy = false; // 新建群请求是否进行中
    // 修改员工资料对话框:独立顶层窗口,on-demand 创建/销毁。
    // 只在弹窗显示期间非空;OnEmployeeEditDialogDestroyed 收到通知后清回 nullptr。
    EmployeeEditDialog*      m_employeeEditDialog = nullptr;
    bool                     m_empEditBusy = false;        // 修改员工资料请求是否进行中
    EnterpriseSettingsModal* m_enterpriseSettingsModal = nullptr; // 企业设置弹窗（借用，在 m_root 内）
    bool                     m_entSettingsOpen = false;    // 企业设置弹窗当前是否打开
    bool                     m_entUpdateBusy = false;      // 保存企业信息请求是否进行中
    bool                     m_addAdminBusy = false;       // 新增管理员请求是否进行中
    // 企业设置弹窗当前操作的企业记录。企业列表行菜单与企业详情屏头部两个
    // 入口都先把目标企业拷进此处，再据此拉管理员列表、填充弹窗。
    Enterprise               m_entSettingsEnt;

    // 组织节点弹窗提交时，新部门 / 员工要挂载到的父部门 ID。
    CString                  m_nodeModalParentId;

    // 待打开的组织节点对话框的两个构造参数。按钮点击处（OnDuiNotify）先把它们
    // 记下来再 PostMessage(kNavShowNodeModal)，由 ShowNodeModal 取用来 new 对话框
    // —— 对话框改成独立顶层窗口后模式在构造时定死，不能再像以前那样先建好实例
    // 再 SetMode 切换。之所以仍绕一次 PostMessage 而不就地创建：沿用原有时序，
    // 不在 DUI 通知回调栈里创建顶层窗口。
    bool                     m_nodeModalPendingIsDept = false; // true=新增部门，false=新增员工
    CString                  m_nodeModalParentName;            // 挂载目标部门名，仅用于对话框顶部横幅

    // 确认弹窗点「确认」后待处理项的 ID（组织节点 / 服务号 / 公告 / 企业 / 员工 ID）。
    CString                  m_confirmDeleteNodeId;
    // 待处理项类型：0=组织节点 1=服务号 2=公告 3=企业 4=重置员工密码 5=启用/停用员工。
    int                      m_confirmTarget = 0;
    // 确认弹窗为「启用/停用员工」时的目标状态：0=启用 1=停用。
    int                      m_confirmStatusTarget = 0;

    // 企业行操作菜单的弹出位置（点击企业行时记下的屏幕坐标）。
    POINT                    m_entMenuPt = { 0, 0 };

    // ---- 企业详情加载（detail.do + dept/tree.do + service/list.do 并发）----
    CString              m_detailLoadEntId;           // 正在加载的企业 ID，空=无加载
    int                  m_detailLoadRemaining = 0;   // 还未回来的请求数
    bool                 m_detailLoadFailed = false;  // 三个请求中是否出过错
    Enterprise           m_detailEnt;                 // 累积：企业信息
    OrgNode              m_detailTree;                // 累积：组织树
    // 组织架构树应答的原始结果。三个请求的回来顺序不确定，而组装组织树需要先知道
    // 企业的公司根部门 ID（来自 detail.do），所以先把应答原样存下，等三个都回来
    // 再解析，避免树先回来时拿不到根部门 ID 而退回合成根节点。
    HttpResult           m_detailTreeHttp;
    std::vector<Service> m_detailServices;            // 累积：服务号列表

    // ---- 组织树重拉（ReloadOrgTree / OnOrgTreeReloaded）----
    // 是否有一次 dept/tree.do 重拉正在途中。用于「刷新」按钮的防连点：在途时
    // 再点只提示、不再发请求。请求发出时置 true，应答（含失败）回来时清 false。
    bool                 m_orgTreeReloading = false;
    // 本次重拉完成后是否要弹「组织架构已刷新」提示。由 ReloadOrgTree 的
    // showToastOnDone 参数写入，OnOrgTreeReloaded 读取后复位。
    bool                 m_orgTreeReloadToast = false;
    // 企业详情加载完成后要定位到的子 tab：0=组织架构 1=服务号。行菜单
    // 「打开详情 → 服务号」用它；每次发起加载前由调用方设置。
    int                  m_detailInitialTab = 0;
    CString              m_permUserId;                // 权限 tab 当前操作的员工 ID
    CString              m_moveUserId;                // 待调整部门的员工 ID
    POINT                m_deptMenuPt = { 0, 0 };     // 部门选择菜单的弹出屏幕坐标

    // 6 个界面，按 Screen 枚举值索引（借用，均在 m_screenHost 内）。
    balloonwjui::DuiControl* m_screens[kScreenCount] = {};
};

} // namespace flamingoadmin
