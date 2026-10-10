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
 *  (3) 弹窗打开时隐藏外壳内的输入框：这是早先内嵌真 Win32 输入框子窗口的
 *      实现留下的做法 —— 那时输入框自带窗口句柄、总是绘制在 DUI 内容之上，
 *      不隐藏就会透过半透明遮罩显形。输入框现已是无窗口控件、由库统一绘制，
 *      弹窗本身即可盖住它们，该前提已不成立（相关代码尚未清理）。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Controls/Window/DuiFrameWindow.h"
#include "DuiNotify.h"
#include "Widgets/Toast.h"
#include "Util/AdminApi.h"   // ApiTag / HttpResult / AdminSession / 领域结构
#include "Util/AppConfig.h"  // LoginConfig（"记住密码"暂存成员需完整类型）
#include "Screens/SensitiveWordsScreen.h"  // 敏感词界面请求的排队成员需要 UiRequest 的完整类型
#include "Screens/EnterpriseQaScreen.h"    // 企业问答界面请求的排队成员需要 UiRequest 的完整类型
#include "Screens/EnterpriseBotScreen.h"   // 机器人界面请求的排队成员需要 UiRequest 的完整类型
#include "Screens/FileShareUi.h"           // 文件共享管理界面请求的排队成员需要 fsui::UiRequest 的完整类型
#include "Util/TextFileSave.h"             // SaveTextToFile 的存盘规格 filesave::SaveFileSpec

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

// 键盘回车：单行输入框里按了回车，要「点」一下所在窗口的提交按钮。为免在输入框的按键处理
// 过程中重入控件通知，延迟到本轮通知派发结束之后再做。wParam 为要点的按钮 ctrlId。
#define WM_APP_CLICK_BUTTON            (WM_APP + 9)

// 组织树搜索:把"按搜索框文本过滤组织树"延迟到本轮通知派发结束之后再做。
// 早先搜索框内嵌真 Win32 输入框子窗口,若在文本变化的同一消息周期里同步过滤
// (触发树重绘),刚输入的文字画不出来(须再点一下),故延迟一拍。搜索框改为无
// 窗口控件之后该现象不再出现,延迟仍保留 —— 与上文 (1) 所述"改动控件树的动作
// 延迟到事件派发结束之后"的约定一致。详见 OnOrgFilter。
#define WM_APP_ORG_FILTER              (WM_APP + 7)

// FlamingoLink.dll 报告长连接鉴权时 token 已失效（ILinkHost::OnSessionExpired 投递）：
// 与 HTTP 接口返回「会话已过期」同样处理，回到登录页（2026-10-04，docs/FlamingoLink方案.md）。
#define WM_APP_LINK_EXPIRED            (WM_APP + 10)

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
class SensitiveWordModal;
class SensitiveImportModal;
class EnterpriseQaModal;
class EnterpriseBotModal;
class BotRuleModal;
class BotDomainsModal;
class FileSharesScreen;
class FileShareAuditScreen;
class FileShareCenterScreen;

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
    // 敏感词：平台超管与企业管理员都可见。同样必须排在 EnterpriseDetail 之前，
    // 且与 kIdNavSensitiveWords 在导航项里的序号一致（见上面的说明）。
    SensitiveWords,   // 敏感词（词库维护与拦截记录）
    // 文件共享：平台超管与企业管理员都可见，与 kIdNavFileShares 在导航项里的序号一致，同样必须排在 EnterpriseDetail 之前。
    // 2026-10-08 起「共享审计」并入文件共享页成为第二个页签，侧栏不再有这一项；FileShareAudit 保留下来是为了
    // 不改动后面各项的序号（kIdNavFileShareAudit 的编号同样保留、不再建导航项），切到它等同于打开文件共享页的
    // 「共享审计」页签，两者显示的是同一个界面（FileShareCenterScreen）。
    FileShares,       // 文件共享页，停在上次看的页签
    FileShareAudit,   // 文件共享页的「共享审计」页签
    // 企业问答（docs/群问答方案.md）：平台超管与企业管理员都可见，与 kIdNavEnterpriseQa 在导航项里的序号一致，
    // 同样必须排在 EnterpriseDetail 之前。
    EnterpriseQa,     // 企业问答（显示在群聊「问答」页签里的问答）
    // 机器人（docs/机器人方案.md 第 7 节）：平台超管与企业管理员都可见，与 kIdNavEnterpriseBot 在导航项里的序号一致，
    // 同样必须排在 EnterpriseDetail 之前。
    EnterpriseBot,    // 机器人（企业机器人的列表、编辑、规则与外部服务域名白名单）
    EnterpriseDetail, // 企业详情（企业管理的子屏）
};

// 主界面数量（与 Screen 枚举值数量一致）。
const int kScreenCount = 12;

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
        MESSAGE_HANDLER(WM_APP_CLICK_BUTTON,           OnClickButtonMsg)
        MESSAGE_HANDLER(WM_APP_LINK_EXPIRED,           OnLinkExpired)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

    // 安装初始界面（构建根容器 + 登录界面 + 新增企业弹窗）。
    // 须在 Create 之后调用一次。
    void InitContent();

    // 切换调试控制台窗口的显隐。F12 键（由 WinMain 消息循环拦截）与顶栏
    // 「调试控制台」按钮共用此入口；窗口懒创建，重复调用安全。
    void ToggleDebugConsole();

    // 处理主窗口里按下的 Esc（由 WinMain 的消息循环在派发之前调用）：当前有叠层弹窗时，
    // 让它的关闭按钮发一次点击通知，与用户点关闭按钮走同一条路径。新建公告弹窗里已经
    // 写了标题或正文时不关闭，以免误丢内容。
    //   返回：true 表示已处理（消息循环不再派发这条按键）；false 表示没有弹窗可关。
    bool HandleEscapeKey();

private:
    // 主窗口销毁 → 退出消息循环。
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 定时器回调：目前只处理顶栏审计滚动提示的切换定时器(kTickerTimerId)。
    // Toast 的显隐与 balloonui 的动画都已由 DuiToast / DuiAnimMgr 自带的
    // 定时器推进,不再经由本函数驱动。
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
    // 不在文本变化通知的同一周期里改动控件树(缘由见 WM_APP_ORG_FILTER 说明)。
    LRESULT OnOrgFilter(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 延迟执行的「点按钮」（由 WM_APP_CLICK_BUTTON 触发）：在当前可见的弹窗或登录页里按
    // wParam 的 ctrlId 找到按钮，可见且可用时让它发一次点击通知。
    LRESULT OnClickButtonMsg(UINT, WPARAM wParam, LPARAM, BOOL& bHandled);

    // FlamingoLink.dll 报告 token 已失效（由 WM_APP_LINK_EXPIRED 触发）：仍在登录状态时
    // 按会话过期处理（HandleSessionExpired），已经退出登录时忽略。
    LRESULT OnLinkExpired(UINT, WPARAM, LPARAM, BOOL& bHandled);

    // 让 FlamingoLink.dll 的「在线用户」页面与界面一致：把企业详情屏当前的企业、占位控件的
    // 位置、页面是否活动 / 被叠层弹窗盖住转交 DLL。在切换界面、切换子 tab、页面与弹窗显隐
    // 变化、登录与退出时调用。DLL 不可用时什么都不做。
    void SyncLinkPage();

    // 企业详情屏切到「功能开关」子 tab 时调用：读取该企业的全部功能设置。平台超管读详情屏当前的企业，
    // 企业管理员传 0、由服务端按本企业处理（见 FeatureSwitchesPage::EnterpriseIdForLoad）。
    void LoadFeaturesPage();

    // 根容器显示层变化（页面切换、弹窗开关）的回调转接，转到 SyncLinkPage。
    //   user：AdminFrame*。
    static void LinkLayerChangedThunk(void* user);

    // 「在线用户」占位控件排版后的回调转接：把新位置转交 DLL。
    //   user：AdminFrame*。rc：占位控件的矩形（主窗口客户区坐标）。
    static void LinkSlotBoundsThunk(void* user, const RECT& rc);

    // 处理单行输入框的回车通知（DUIN_EDIT_ENTER）：按 kEnterSubmitRules 查出该输入框对应
    // 的提交按钮，经 WM_APP_CLICK_BUTTON 延迟「点」它。
    //   n：通知（code 已是 DUIN_EDIT_ENTER）。
    //   返回：true 表示该输入框在对照表里且发送者确是输入框，已安排提交；false 表示不归
    //         本函数处理，调用方应继续往下派发。
    bool HandleEditEnter(const balloonwjui::DuiNotify* n);

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

    // 弹出「另存为」对话框，把一段文字写入用户所选的文件，成功后按规格给出提示。
    // 用户取消对话框不算失败；文件打不开或写不全时给出错误提示。
    //   content：要写的内容，UTF-8 字节；是否补 BOM 由 spec 决定。
    //   spec：存盘规格（预填文件名、文件类型、成功提示等），见 Util/TextFileSave.h。
    void SaveTextToFile(const std::string& content, const filesave::SaveFileSpec& spec);

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

    // ---- 敏感词（模块 11，docs/敏感词方案.md 第 7 节）----
    // 敏感词界面的请求出口：补上服务器地址与回传窗口后交给 HttpWorker。
    //   user：本窗口指针。request：界面构造好的请求，m_tag 已填好。
    static void SensitiveRequestThunk(void* user, HttpRequest& request);
    // 敏感词界面的界面请求：先排进 m_swUiQueue，再推迟到事件派发结束后由 HandleSensitiveUiRequests 处理
    // （开关弹窗会改动控件树，不能在通知派发的过程中做）。
    //   user：本窗口指针。request：界面请求。
    static void SensitiveUiRequestThunk(void* user, const SensitiveWordsScreen::UiRequest& request);
    // 依次处理排队的敏感词界面请求：开关弹窗、弹确认框、显示弹窗内错误与提示。
    void HandleSensitiveUiRequests();
    // 关闭添加 / 编辑敏感词弹窗。
    void HideSensitiveWordModal();
    // 弹窗里点了「保存」：本地校验通过后交给敏感词界面提交。
    void SubmitSensitiveWordModal();
    // 企业问答界面的界面请求：先排进 m_qaUiQueue，再推迟到事件派发结束后由 HandleQaUiRequests 处理（理由同敏感词界面）。
    //   user：本窗口指针。request：界面请求。
    static void QaUiRequestThunk(void* user, const EnterpriseQaScreen::UiRequest& request);
    // 依次处理排队的企业问答界面请求：开关弹窗、弹确认框、显示弹窗内错误与提示、补上弹窗的群列表。
    void HandleQaUiRequests();
    // 关闭新增 / 编辑企业问答弹窗。
    void HideQaModal();
    // 企业问答弹窗里点了「保存」：本地校验通过后交给企业问答界面提交。
    void SubmitQaModal();

    // ---- 机器人（模块 14，docs/机器人方案.md 第 7 节）----
    // 机器人界面的界面请求：先排进 m_botUiQueue，再推迟到事件派发结束后由 HandleBotUiRequests 处理（理由同敏感词界面）。
    //   user：本窗口指针。request：界面请求。
    static void BotUiRequestThunk(void* user, const EnterpriseBotScreen::UiRequest& request);
    // 依次处理排队的机器人界面请求：开关编辑窗与白名单窗、弹确认框、显示窗口内错误与提示、补上群列表与白名单、
    // 显示「试一试」结果与重新生成的令牌。
    void HandleBotUiRequests();
    // 关闭机器人编辑窗（规则编辑窗开着时一并关闭）。
    void HideBotModal();
    // 机器人编辑窗里点了「保存」：本地校验通过后交给机器人界面提交。
    void SubmitBotModal();
    // 机器人编辑窗里点了规则的「添加」（isNew 为 true）或「编辑」与双击：取得要编辑的规则后打开规则编辑窗，
    // 机器人编辑窗暂时隐藏（内容保留）。
    void OpenBotRuleModal(bool isNew);
    // 关闭规则编辑窗，回到机器人编辑窗（不写回）。
    void CloseBotRuleModal();
    // 规则编辑窗里点了「确定」：本地校验通过后写回机器人编辑窗，再回到机器人编辑窗。
    void SubmitBotRuleModal();
    // 机器人编辑窗里点了「试一试」：交给机器人界面发请求，编辑窗先显示「正在判定…」。
    void RunBotTry();
    // 关闭外部服务域名白名单窗。
    void HideBotDomainsModal();
    // 白名单窗里点了「保存」：本地校验通过后交给机器人界面提交。
    void SubmitBotDomainsModal();
    // 关闭批量导入弹窗；导入进行中时不关闭。
    void HideSensitiveImportModal();
    // 导入弹窗里点了主按钮：显示结果之后关闭弹窗，否则把输入框里的内容交给敏感词界面分批提交。
    void SubmitSensitiveImportModal();
    // 导入弹窗里点了「从文件读取」：选一个文本文件（不超过 2 MB），按 BOM / UTF-8 / GBK 解码后填进输入框。
    void ReadSensitiveImportFile();

    // ---- 文件共享管理（模块 12，docs/文件共享方案.md 第 10 节）----
    // 功能开关面板、「文件共享」页、「共享审计」页的请求出口复用 SensitiveRequestThunk（它与界面无关）。
    // 三者的界面请求（提示、确认框）：先排进 m_fsUiQueue，再推迟到事件派发结束后由 HandleFileShareUiRequests 处理。
    //   user：本窗口指针。request：界面请求。
    static void FileShareUiRequestThunk(void* user, const fsui::UiRequest& request);
    // 依次处理排队的文件共享管理界面请求：显示提示、弹确认框（结果经 m_fsConfirmTarget 回告发起方）。
    void HandleFileShareUiRequests();



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

    // 公告中心点「重新推送」：当前选中的是已发布公告时弹确认框，确认后经 SubmitAnnouncementRepush
    // 发请求；不是已发布状态时只给出提示。
    void ConfirmAnnouncementRepush();

    // 发出重新推送请求（announcement/repush.do），应答见 OnAnnRepushed。
    //   annId：公告 ID。公告写操作进行中时只提示稍候，不重复发。
    void SubmitAnnouncementRepush(const CString& annId);

    // 处理重新推送应答：成功时提示推送人数，失败时弹出错误提示。
    void OnAnnRepushed(const HttpResult& http);

    // 发起「复制公告」：拉取原公告详情（announcement/detail.do），应答见 OnAnnCopyFetchLoaded。
    //   annId：被复制的公告 ID，空串时不做任何事。
    void StartAnnouncementCopy(const CString& annId);

    // 处理「复制公告」的 detail.do 应答：按原公告的标题、正文、级别、受众、置顶与投放目标
    // 打开一条新建公告（发送方式为立即）；失败时弹出错误提示。
    void OnAnnCopyFetchLoaded(const HttpResult& http);

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

    // ---- 危险操作经确认框确认之后才调用的提交函数 ----

    // 修改企业状态（enterprise/setstatus.do）。
    //   enterpriseId：企业 ID；newStatus：目标状态（活跃 / 停用，取值见 AdminFrame.cpp 的 kEntStatus*）。
    void SubmitEnterpriseStatus(const CString& enterpriseId, int newStatus);

    // 移除一位企业管理员（enterprise/removeadmin.do）。
    //   adminId：管理员 ID。
    void SubmitRemoveAdmin(long long adminId);

    // 让一名员工的全部设备下线（user/kickdevice.do，deviceid 传空）。
    //   userId：员工 ID。
    void SubmitKickDevices(const CString& userId);

    // 把员工详情面板上已选的角色落库（user/setrole.do）。
    //   userId：员工 ID；角色取自 OrgDetailPanel::GetUserRole。
    void SubmitUserRole(const CString& userId);

    // 修改服务号状态（service/setstatus.do）。
    //   serviceId：服务号 ID；target：0 正常 / 1 停用。
    void SubmitServiceStatus(const CString& serviceId, int target);

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

    // 登出与会话失效共用的复位：停掉顶栏滚动提示，关闭叠层弹窗与独立对话框，复位全部
    // 「请求进行中」标志与待处理状态，各界面放下指向缓存数据的指针、回到初始状态，最后
    // 清空 DataStore。换账号登录后不会看到上一位管理员的数据，也不会有界面卡在「加载中」。
    void ResetSessionState();

    // 单独拉取最近几条审计（audit/list.do 第 1 页）喂给顶栏滚动提示。进入主界面时、滚动
    // 提示转完一圈时调用；与审计页的翻页、筛选互不影响。
    void FetchTickerAudits();

    // 处理 FetchTickerAudits 的应答：成功则交给顶栏；失败只记日志，保留原来展示的内容。
    void OnTickerAuditLoaded(const HttpResult& http);

    // 发起拉取某个服务号的详情（service/detail.do），以读到它的绑定范围。列表接口
    // 不带绑定，绑定只能从这里读。
    //   serviceId：服务号 ID；
    //   forBinding：true 表示用户点了「修改绑定」，读到之后才打开绑定弹窗（读取
    //               失败则不打开，避免从空集合开始保存而清掉原有绑定）；false 表示
    //               面板选中项变化后只为显示而读取。
    void FetchServiceDetail(const CString& serviceId, bool forBinding);

    // 服务号面板当前选中的服务号若还没读到绑定，就发起一次读取（只为显示）。
    // 企业详情首次装配、服务号列表重拉、面板切换选中项之后调用。
    void FetchSelectedServiceBindingsIfNeeded();

    // 处理 service/detail.do 应答：把绑定翻译成带显示名的绑定项写入 DataStore 并刷新
    // 面板；forBinding 为 true 时再打开绑定弹窗。应答到达时企业或服务号已切换的，
    // 只更新缓存、不打开弹窗。
    //   http：HTTP 结果；forBinding：同 FetchServiceDetail。
    void OnServiceDetailLoaded(const HttpResult& http, bool forBinding);

    // 处理 service/setbindings.do 应答：成功时先把刚保存的绑定写入 DataStore（列表
    // 接口不带绑定，重拉列表不会更新它），再提示并重拉列表；失败只提示。
    void OnServiceBindingsSaved(const HttpResult& http);

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

    // 递归地把 node 子树里每个 DuiEdit 设为指定可见性。弹窗开 / 关时
    // 用它隐藏 / 恢复外壳内的输入框。
    //
    // 注：这套隐藏动作原本是为了应对早先内嵌真 Win32 输入框子窗口的实现 ——
    // 那时输入框自带窗口句柄、总是绘制在 DUI 内容之上，会透过半透明遮罩显形。
    // 输入框改为无窗口控件之后弹窗本身就能盖住它们，本函数已无必要，留着只是
    // 尚未清理。
    void SetEditsVisible(balloonwjui::DuiControl* node, bool visible);

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
    CString                  m_pendingNewPassword;             // 已提交修改、等待应答的新口令明文；仅内存，应答处理后清空
    bool                     m_annActionBusy = false;          // 公告撤回/删除/置顶请求是否进行中
    bool                     m_composeSubmitBusy = false;      // 新建公告 save+send 链是否进行中
    CString                  m_composePresetEntId;             // 企业详情点「发布公告」时记下的企业 ID，ShowComposeModal 据此预设受众后清空
    CString                  m_composeTreeEntId;               // 目标选择器在途组织树请求所属企业 ID
    bool                     m_nodeSubmitBusy = false;         // 新增部门/员工请求是否进行中
    bool                     m_svcSubmitBusy = false;          // 新建服务号请求是否进行中
    bool                     m_svcBindingFetchBusy = false;    // 「修改绑定」前读取当前绑定的请求是否进行中（防连点）
    CString                  m_bindingModalSvcId;              // 绑定弹窗正在编辑的服务号 ID：打开时记下，保存时用它，不取面板的当前选中项
    CString                  m_pendingBindingsSvcId;           // 已提交 setbindings.do、等待应答的服务号 ID；应答处理后清空
    std::vector<ServiceBinding> m_pendingBindings;             // 与上一项配套：提交的绑定列表，成功后写入 DataStore
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
    SensitiveWordsScreen*    m_sensitiveWordsScreen = nullptr; // 敏感词（借用，在 m_screenHost 内）
    SensitiveWordModal*      m_swModal = nullptr;   // 添加 / 编辑敏感词弹窗（借用，在 m_root 弹窗层内）
    SensitiveImportModal*    m_swImportModal = nullptr; // 批量导入敏感词弹窗（借用，在 m_root 弹窗层内）
    std::vector<SensitiveWordsScreen::UiRequest> m_swUiQueue;  // 待处理的敏感词界面请求，见 SensitiveUiRequestThunk
    EnterpriseQaScreen*      m_qaScreen = nullptr;  // 企业问答（借用，在 m_screenHost 内）
    EnterpriseQaModal*       m_qaModal = nullptr;   // 新增 / 编辑企业问答弹窗（借用，在 m_root 弹窗层内）
    std::vector<EnterpriseQaScreen::UiRequest> m_qaUiQueue;  // 待处理的企业问答界面请求，见 QaUiRequestThunk
    EnterpriseBotScreen*     m_botScreen = nullptr;        // 机器人（借用，在 m_screenHost 内）
    EnterpriseBotModal*      m_botModal = nullptr;         // 新建 / 编辑企业机器人的编辑窗（借用，在 m_root 弹窗层内）
    BotRuleModal*            m_botRuleModal = nullptr;     // 添加 / 编辑规则的规则编辑窗（借用，在 m_root 弹窗层内）
    BotDomainsModal*         m_botDomainsModal = nullptr;  // 外部服务域名白名单窗（借用，在 m_root 弹窗层内）
    std::vector<EnterpriseBotScreen::UiRequest> m_botUiQueue;  // 待处理的机器人界面请求，见 BotUiRequestThunk
    FileShareCenterScreen*   m_fileShareCenter = nullptr;      // 文件共享页（「文件共享」「共享审计」两个页签的外层页面；借用，在 m_screenHost 内）
    FileSharesScreen*        m_fileSharesScreen = nullptr;     // 「文件共享」页签（借用，在 m_fileShareCenter 内）
    FileShareAuditScreen*    m_fileShareAuditScreen = nullptr; // 「共享审计」页签（借用，在 m_fileShareCenter 内）
    std::vector<fsui::UiRequest> m_fsUiQueue;  // 待处理的文件共享管理界面请求，见 FileShareUiRequestThunk
    // 当前确认框的发起方（功能开关面板或「文件共享」页，借用、随主窗口常驻）；只在 m_confirmTarget 为
    // 文件共享管理的确认框时有效，确认框关闭后清空
    fsui::ConfirmTarget*     m_fsConfirmTarget = nullptr;
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
    // 正在保存的企业记录：SubmitEnterpriseUpdate 按表单新值填好，OnEnterpriseUpdated 成功时写回企业缓存与
    // m_entSettingsEnt（2026-10-06 起；此前保存后缓存里仍是旧记录）
    Enterprise               m_entUpdatePending;

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
