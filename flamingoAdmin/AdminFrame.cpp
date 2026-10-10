/**
 *  flamingoAdmin —— 主窗口 AdminFrame 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "AdminFrame.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/Widgets.h"
#include "Widgets/Toast.h"
#include "Widgets/CommonWidgets.h"
#include "Shell/Sidebar.h"
#include "Shell/TopBar.h"
#include "Shell/ScreenHost.h"
#include "Screens/LoginScreen.h"
#include "Screens/PlaceholderScreen.h"
#include "Screens/DashboardScreen.h"
#include "Screens/EnterprisesScreen.h"
#include "Screens/EnterpriseDetailScreen.h"
#include "Screens/OrgDetailPanel.h"
#include "Screens/NewEnterpriseModal.h"
#include "Screens/NodeModal.h"
#include "Screens/ConfirmDialog.h"
#include "Screens/ServiceModal.h"
#include "Screens/ServicesPanel.h"
#include "Screens/BindingModal.h"
#include "Screens/EmployeeEditDialog.h"
#include "Screens/EnterpriseSettingsModal.h"
#include "Screens/AnnouncementsScreen.h"
#include "Screens/ComposeModal.h"
#include "Screens/GroupModal.h"
#include "Screens/AuditScreen.h"
#include "Screens/ClientVersionsScreen.h"
#include "Screens/ClientVersionModal.h"
#include "Screens/SensitiveWordsScreen.h"
#include "Screens/EnterpriseQaScreen.h"
#include "Screens/EnterpriseQaModal.h"
#include "Screens/EnterpriseBotScreen.h"
#include "Screens/EnterpriseBotModal.h"
#include "Screens/BotRuleModal.h"
#include "Screens/BotDomainsModal.h"
#include "Screens/FileSharesScreen.h"
#include "Screens/FileShareAuditScreen.h"
#include "Screens/FileShareCenterScreen.h"   // 文件共享页（「文件共享」「共享审计」两个页签，2026-10-08）
#include "Screens/FeatureSwitchesPage.h"     // 企业详情的「功能开关」页（2026-10-08）
#include "Screens/SensitiveWordModal.h"
#include "Screens/SensitiveImportModal.h"
#include "Screens/SettingsScreen.h"
#include "Screens/NetworkSettingsScreen.h"
#include "Mock/DataStore.h"
#include "Util/AppConfig.h"
#include "Util/LoginText.h"   // 登录页加密说明与账号密码回填
#include "Util/NetProbe.h"
#include "Util/HttpClient.h"
#include "Util/HttpWorker.h"
#include "Util/AdminApi.h"
#include "Util/OrgTreeBuilder.h"   // kSyntheticRootId（尚未迁移的存量数据兜底）
#include "Util/DebugConsoleWindow.h"

#include "Controls/Basic/DuiLabel.h"

#include <thread>
#include "Util/Logger.h"
#include "Util/SensitiveWordText.h"
#include "Util/LinkBridge.h"          // FlamingoLink.dll：长连接与「在线用户」页（docs/FlamingoLink方案.md）
#include "Util/LinkPageState.h"
#include "Widgets/LinkPageSlot.h"

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/Input/DuiComboBox.h"   // Esc 处理时判断弹窗里是否有展开着的下拉框
#include "Controls/Basic/DuiButton.h"   // 「记住密码」复选框（StyleCheckbox）的读写
#include "Controls/List/DuiMenu.h"
#include "Controls/Input/DuiSwitch.h"   // 机器人开关的分发：按开关的控件编号找面板

#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

// 导出 CSV 用到 GetSaveFileName，需链接通用对话框库。
#pragma comment(lib, "comdlg32.lib")

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- WM_APP_NAVIGATE 的导航命令码（wParam）----
// 注：进入主界面外壳不走此处 —— 登录成功由 OnLoginResult 直接调 EnterShell()。
const WPARAM kNavLogout          = 2;   // 登出回到登录界面
const WPARAM kNavShowModal       = 3;   // 打开新增企业弹窗
const WPARAM kNavHideModal       = 4;   // 关闭新增企业弹窗
const WPARAM kNavSubmitModal     = 5;   // 提交新增企业弹窗
const WPARAM kNavShowNodeModal   = 7;   // 打开组织节点对话框（新增部门 / 员工）
const WPARAM kNavHideNodeModal   = 8;   // 关闭组织节点对话框
// 9 历史用于「提交组织节点弹窗」的 navigate 分发。NodeModal 改成独立顶层窗口后,
// 「创建」按钮由对话框自己接管、校验通过后直接回调 AdminFrame::OnNodeModalSubmit,
// 不再 PostMessage 到主窗口。与上面 10/11/12 同理,ID 保留空缺不复用。
// 10/11/12 历史用于「通用确认弹窗」的 navigate 分发,ConfirmDialog 改成
// 独立顶层窗口后,按钮事件 dialog 自己接管,不再 PostMessage 到主窗口。
// ID 段保留空缺不复用,避免误重用引发 stale handler。
const WPARAM kNavShowServiceModal  = 13; // 打开新建服务号弹窗
const WPARAM kNavHideServiceModal  = 14; // 关闭新建服务号弹窗
const WPARAM kNavSubmitServiceModal = 15;// 提交新建服务号弹窗
const WPARAM kNavToggleService     = 16; // 停用 / 启用 服务号
const WPARAM kNavShowBindingModal  = 17; // 打开绑定范围弹窗
const WPARAM kNavHideBindingModal  = 18; // 关闭绑定范围弹窗
const WPARAM kNavSaveBindingModal  = 19; // 保存绑定范围弹窗
const WPARAM kNavShowCompose       = 20; // 打开新建公告弹窗
const WPARAM kNavHideCompose       = 21; // 关闭新建公告弹窗
const WPARAM kNavSubmitCompose     = 22; // 提交新建公告弹窗
const WPARAM kNavToggleSidebar     = 23; // 折叠 / 展开侧边栏
const WPARAM kNavShowNetwork       = 24; // 登录页 → 网络设置页
const WPARAM kNavBackToLogin       = 25; // 网络设置页 → 登录页（不保存）
const WPARAM kNavSaveNetwork       = 26; // 保存网络设置并返回登录页
const WPARAM kNavShowEntMenu       = 27; // 弹出企业行操作菜单
// 28/29/30 历史用于「修改员工资料弹窗」的 navigate 分发,EmployeeEditDialog
// 改成独立顶层窗口后,按钮事件 dialog 自己接管,不再 PostMessage 到主窗口。
// ID 段保留空缺不复用,避免误重用引发 stale handler。
const WPARAM kNavShowDeptMenu      = 31; // 弹出「调部门」的部门选择菜单
const WPARAM kNavHideEntSettings   = 32; // 关闭企业设置弹窗
const WPARAM kNavExportAnnouncements = 33; // 导出公告列表为 CSV
const WPARAM kNavShowGroupModal    = 34; // 打开新建群弹窗
const WPARAM kNavHideGroupModal    = 35; // 关闭新建群弹窗
const WPARAM kNavSubmitGroupModal  = 36; // 提交新建群弹窗
const WPARAM kNavShowCvModal       = 37; // 打开新建客户端版本弹窗
const WPARAM kNavHideCvModal       = 38; // 关闭新建客户端版本弹窗
const WPARAM kNavSubmitCvDraft     = 39; // 提交新建客户端版本（存草稿）
const WPARAM kNavSubmitCvPublish   = 40; // 提交新建客户端版本（直接发布）
const WPARAM kNavRescanPackages    = 41; // 弹窗内「重新扫描」升级包目录
const WPARAM kNavSwUiRequests      = 42; // 处理排队的敏感词界面请求（开关弹窗、确认框、提示）
const WPARAM kNavSwHideModal       = 43; // 关闭添加 / 编辑敏感词弹窗
const WPARAM kNavSwSubmitModal     = 44; // 提交添加 / 编辑敏感词弹窗
const WPARAM kNavSwHideImport      = 45; // 关闭批量导入敏感词弹窗
const WPARAM kNavSwSubmitImport    = 46; // 导入弹窗的主按钮（开始导入 / 完成）
const WPARAM kNavSwImportReadFile  = 47; // 导入弹窗的「从文件读取」
const WPARAM kNavFsUiRequests      = 48; // 处理排队的文件共享管理界面请求（提示、确认框）
const WPARAM kNavQaUiRequests      = 49; // 处理排队的企业问答界面请求（开关弹窗、确认框、提示）
const WPARAM kNavQaHideModal       = 50; // 关闭新增 / 编辑企业问答弹窗
const WPARAM kNavQaSubmitModal     = 51; // 提交新增 / 编辑企业问答弹窗
const WPARAM kNavBotUiRequests     = 52; // 处理排队的机器人界面请求（开关编辑窗与白名单窗、确认框、提示）
const WPARAM kNavBotHideModal      = 53; // 关闭机器人编辑窗
const WPARAM kNavBotSubmitModal    = 54; // 提交机器人编辑窗
const WPARAM kNavBotAddRule        = 55; // 机器人编辑窗里点了规则的「添加」：打开规则编辑窗
const WPARAM kNavBotEditRule       = 56; // 机器人编辑窗里点了规则的「编辑」或双击一行：打开规则编辑窗
const WPARAM kNavBotCloseRule      = 57; // 关闭规则编辑窗，回到机器人编辑窗
const WPARAM kNavBotSubmitRule     = 58; // 规则编辑窗的「确定」
const WPARAM kNavBotHideDomains    = 59; // 关闭外部服务域名白名单窗
const WPARAM kNavBotSubmitDomains  = 60; // 提交外部服务域名白名单窗
const WPARAM kNavScreenBase        = 100;// 切换界面：kNavScreenBase + (int)Screen

// 注:历史的 kToastTimerId / kToastTickMs 已删除 ——
// balloonwjui::DuiToast 内部走 DuiAnimMgr 自驱(渐入 → 显示 → 渐出),
// 不再需要 admin 主动启 toast 专用 timer 调 Tick()。
//
// 注:历史的 kAnimPulseTimerId / kAnimPulseMs(16ms 动画脉冲定时器)同样已删除 ——
// balloonwjui::DuiAnimMgr 自 2026-08-14 起内置共享脉冲定时器(见 DuiAnimation.cpp
// 的 DuiAnimMgr::PulseProc):有动画加入时自行安装、全部播完后自行卸载,宿主程序
// 不必也不应再周期性调用 TickAll。定时器 ID 3 就此空缺,不再复用。

// ---- 顶栏审计滚动提示定时器 ----
// 周期取 4500ms：足够看清一条、又不至于久到像静止。
const UINT_PTR kTickerTimerId   = 2;       // 定时器 ID
const UINT     kTickerIntervalMs = 4500;   // 切换周期（毫秒）
const int      kTickerAuditCount = 6;      // 顶栏滚动提示单独拉取的审计条数（与顶栏循环展示的条数一致）

// ---- 网络连通性测试 ----
const int kNetTestTimeoutMs = 2000;        // TCP 探测超时（毫秒）

// 业务接口返回此错误码表示会话失效（对应服务端 error_code_notlogin）。
const int kErrCodeSessionExpired = 2;

// ---- 企业行操作菜单的项 ID（DuiMenu::TrackPopup 返回值）----
const UINT kMenuEntDetailOrg = 1;   // 打开详情：定位到「组织架构」子 tab
const UINT kMenuEntApprove   = 2;   // 审核通过：待审核 → 活跃
const UINT kMenuEntSuspend   = 3;   // 停用企业：活跃 → 停用
const UINT kMenuEntActivate  = 4;   // 启用企业：停用 → 活跃
const UINT kMenuEntDelete    = 5;   // 删除企业：物理删除
const UINT kMenuEntDetailSvc = 6;   // 打开详情：定位到「服务号」子 tab
const UINT kMenuEntSettings  = 7;   // 企业设置：改企业信息 + 管理员管理
const UINT kMenuEntCopyId    = 8;   // 复制企业 ID 到剪贴板
const UINT kMenuEntExportOne = 9;   // 导出该企业为单行 CSV
const UINT kMenuEntDetailSub = 10;  // 「打开详情」子菜单父项（点父项不返回此 id）
const UINT kMenuEntDetailFeatures = 11;  // 打开详情：定位到「功能开关」子 tab（2026-10-08）

// ---- 企业详情子 tab 序号（与 EnterpriseDetailScreen 内部序号一致）----
const int kDetailTabOrg     = 0;   // 组织架构 tab
const int kDetailTabService = 1;   // 服务号 tab
const int kDetailTabFeatures = EnterpriseDetailScreen::kTabFeatures;   // 功能开关 tab（2026-10-08）

// 员工登录历史一次拉取的条数（设备登录 tab 无翻页，一次取够近期记录）。
const int kLoginRecordsPageSize = 50;

// ---- 仪表盘「活跃企业」列表的拉取参数 ----
const int kDashEntStatusAll = -1;     // status=-1：不按状态筛选
const int kDashEntPageSize  = 1000;   // 一次拉够（与企业管理界面同口径）

// ---- 公告列表的拉取参数 ----
const int kAnnStatusAll     = -1;     // status=-1：不按状态筛选
const int kAnnStatusDraft   = 0;      // 草稿：尚未发布，定时公告到点之前也处于这个状态
const int kAnnStatusPublished = 1;    // 已发布：只有这个状态的公告可以撤回与重新推送
const int kAnnFetchPageSize = 1000;   // 一次拉够（公告中心无分页 UI）

// 仪表盘「最新公告 / 近期审计」卡只展示开头几条，拉一小页即可。
const int kDashAnnPageSize   = 10;    // 仪表盘最新公告拉取条数
const int kDashAuditPageSize = 10;    // 仪表盘近期审计拉取条数

// ---- 企业状态 / 套餐编码（与 docs/Admin控制台接口文档.md 模块 3 一致）----
const int kEntStatusActive    = 1;   // 企业状态：活跃
const int kEntStatusSuspended = 2;   // 企业状态：停用
const int kPlanProfessional   = 1;   // 套餐：专业版。新增企业弹窗的套餐取不到时按它提交

// 确认弹窗 m_confirmTarget 取值：3=删除企业，4=重置员工密码，5=启用/停用员工。
const int kConfirmTargetEnterprise = 3;
const int kConfirmTargetResetPwd   = 4;
const int kConfirmTargetSetStatus  = 5;
const int kConfirmTargetAnnDelete  = 6;   // 公告物理删除(任意状态,区别于 2=撤回)
const int kConfirmTargetSwDelete   = 7;   // 删除敏感词
const int kConfirmTargetSwDisable  = 8;   // 关闭敏感词过滤总开关
const int kConfirmTargetFileShare  = 9;   // 文件共享管理的确认框（切换功能开关、强制取消共享），结果交给 m_fsConfirmTarget
const int kConfirmTargetCvPublish  = 10;  // 发布一条客户端版本记录（m_confirmDeleteNodeId 为记录 ID）
const int kConfirmTargetCvDisable  = 11;  // 撤回一条已发布的客户端版本记录（m_confirmDeleteNodeId 为记录 ID）
const int kConfirmTargetEntSuspend = 12;  // 停用企业（m_confirmDeleteNodeId 为企业 ID）
const int kConfirmTargetRemoveAdmin = 13; // 移除企业管理员（m_confirmDeleteNodeId 为管理员 ID）
const int kConfirmTargetKick       = 14;  // 强制员工全部设备下线（m_confirmDeleteNodeId 为员工 ID）
const int kConfirmTargetSetRole    = 15;  // 修改员工角色（m_confirmDeleteNodeId 为员工 ID；取消时重拉权限矩阵恢复面板）
const int kConfirmTargetSvcDisable = 16;  // 停用服务号（m_confirmDeleteNodeId 为服务号 ID）
const int kConfirmTargetAnnRepush  = 17;  // 重新推送已发布的公告（m_confirmDeleteNodeId 为公告 ID）
const int kConfirmTargetQaDelete   = 18;  // 删除企业问答（待删的问答由企业问答界面自己记着）

// 单行输入框里按回车时要「点」的提交按钮。只列在这里的输入框才响应回车；新建公告、
// 客户端版本两个弹窗会直接发布，故意不列。企业设置弹窗里「新增管理员」的两个输入框
// 对应「添加」，其余输入框对应「保存企业信息」。
struct EnterSubmitRule
{
    UINT m_editId;     // 输入框 ctrlId
    UINT m_buttonId;   // 要点的按钮 ctrlId（与输入框在同一个弹窗或页面里）
};
const EnterSubmitRule kEnterSubmitRules[] =
{
    { kIdLoginAccount,   kIdLoginSubmit },     // 登录页：账户
    { kIdLoginPassword,  kIdLoginSubmit },     // 登录页：密码
    { kIdModalName,      kIdModalSubmit },     // 新增企业：企业全称
    { kIdModalShort,     kIdModalSubmit },     // 新增企业：企业简称
    { kIdModalRegion,    kIdModalSubmit },     // 新增企业：主要地区
    { kIdModalAdmin,     kIdModalSubmit },     // 新增企业：主管理员姓名
    { kIdModalContact,   kIdModalSubmit },     // 新增企业：管理员邮箱
    { kIdEntSetName,     kIdEntSetSave },      // 企业设置：企业全称
    { kIdEntSetShort,    kIdEntSetSave },      // 企业设置：企业简称
    { kIdEntSetRegion,   kIdEntSetSave },      // 企业设置：主要地区
    { kIdEntSetPrefix,   kIdEntSetSave },      // 企业设置：账号前缀
    { kIdEntSetNewName,  kIdEntSetAddAdmin },  // 企业设置：新增管理员姓名
    { kIdEntSetNewEmail, kIdEntSetAddAdmin },  // 企业设置：新增管理员邮箱
    { kIdSvcName,        kIdSvcSubmit },       // 新建服务号：名称
    { kIdSvcGlyph,       kIdSvcSubmit },       // 新建服务号：头像文字
    { kIdGroupName,      kIdGroupSubmit },     // 新建群：群名称
    { kIdSwModalWord,    kIdSwModalSave },     // 敏感词：词条
    { kIdSwModalRemark,  kIdSwModalSave },     // 敏感词：备注
    { kIdQaModalQuestion, kIdQaModalSave },    // 企业问答：问题
    { kIdBotModalTryText, kIdBotModalTryRun }, // 机器人编辑窗：「试一试」输入框按回车即试一试
    { kIdBotRuleName,     kIdBotRuleSave },    // 规则编辑窗：规则名称
};

// 判断控件子树里是否有下拉框正展开着。有的话 Esc 应当只收起下拉列表，不关闭整个弹窗。
//   node：子树根（借用）。
bool HasOpenComboPopup(const DuiControl* node)
{
    const DuiComboBox* combo = dynamic_cast<const DuiComboBox*>(node);
    if (combo != nullptr && combo->IsPopupOpen())
    {
        return true;
    }
    const std::vector<std::unique_ptr<DuiControl>>& children = node->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        if (HasOpenComboPopup(children[i].get()))
        {
            return true;
        }
    }
    return false;
}

// 员工角色编码（user/setrole.do 的 role，与 OrgDetailPanel::m_userRole 同一口径）。
const int kUserRoleDeptLeader = 1;   // 部门负责人
const int kUserRoleEntAdmin   = 2;   // 企业管理员（可登录控制台管理本企业）

// 网络测试工作线程体：阻塞做一次 TCP 探测，结果经 PostMessage 回传主窗口。
// host 取 std::wstring 值传入，使工作线程持有与 UI 线程完全独立的副本，
// 避免跨线程共享 CString 引用计数。
void RunNetTest(HWND hwnd, std::wstring host, int port)
{
    const ProbeResult r = ProbeTcp(CString(host.c_str()), port,
                                   kNetTestTimeoutMs);
    ::PostMessage(hwnd, WM_APP_NETTEST_RESULT, static_cast<WPARAM>(r), 0);
}

// 把 CString（UTF-16）转换为 UTF-8 字节串。用于把本地拼好的 CSV 文本交给
// SaveTextToFile 写盘（该函数约定接收 UTF-8）。
std::string Utf8FromWide(const CString& wide)
{
    const int wideLen = wide.GetLength();
    if (wideLen <= 0)
    {
        return std::string();
    }
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, wideLen,
                                            nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
    {
        return std::string();
    }
    std::string out(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, wideLen,
                          &out[0], bytes, nullptr, nullptr);
    return out;
}

// 把一个字段转义为合法的 CSV 单元格：含逗号 / 双引号 / 换行时，用双引号
// 整体包裹，内部的双引号翻倍。
//   raw：原始字段文本。
//   返回：可直接拼入 CSV 的单元格文本。
CString CsvCell(const CString& raw)
{
    const bool needQuote = raw.Find(_T(',')) >= 0
                        || raw.Find(_T('"')) >= 0
                        || raw.Find(_T('\n')) >= 0
                        || raw.Find(_T('\r')) >= 0;
    if (!needQuote)
    {
        return raw;
    }
    CString escaped = raw;
    escaped.Replace(_T("\""), _T("\"\""));
    return _T("\"") + escaped + _T("\"");
}

// 企业入驻状态枚举 → 中文文案（用于 CSV 导出）。
//   status：企业入驻状态。
//   返回：对应中文，未知值返回「未知」。
LPCTSTR EnterpriseStatusText(EnterpriseStatus status)
{
    switch (status)
    {
    case EnterpriseStatus::Active:
        return _T("活跃");
    case EnterpriseStatus::Pending:
        return _T("待审核");
    case EnterpriseStatus::Suspended:
        return _T("已停用");
    default:
        return _T("未知");
    }
}

// 审计界面筛选下标 → 服务端筛选码。UI 下标 0 表示「全部」→ 服务端 -1（不限）；
// 其余 UI 下标 n 对应服务端码 n-1（级别：0 信息 / 1 警告 / 2 危险；操作者：
// 0 管理员 / 1 系统）。级别与操作者两个分段的映射规律一致，共用本函数。
//   uiIndex：审计界面分段控件的当前下标。
//   返回：服务端 audit/list.do 的 severity / actortype 参数值。
int AuditFilterToServer(int uiIndex)
{
    return uiIndex <= 0 ? -1 : uiIndex - 1;
}

// 公告级别（客户端 Severity）→ 中文文案（用于 CSV 导出），与界面共用一套名称。
LPCTSTR AnnSeverityText(Severity sev)
{
    return adminapi::AnnSeverityLabel(sev);
}

// 公告状态码 → 中文文案（0 草稿 / 1 已发布 / 2 已撤回）。
LPCTSTR AnnStatusText(int status)
{
    switch (status)
    {
    case 1:
        return _T("已发布");
    case 2:
        return _T("已撤回");
    default:
        return _T("草稿");
    }
}

// 一个主界面的展示信息：导航项 ID、占位图标、名称、占位副标题。
struct ScreenInfo
{
    int      m_navId;
    ui::Icon m_icon;
    LPCTSTR  m_name;
    LPCTSTR  m_subtitle;
};

// 取某个主界面的展示信息。name / navId 用于面包屑与侧栏高亮；icon /
// subtitle 仅在该界面仍为占位界面时（公告 / 审计 / 设置）用于占位绘制。
ScreenInfo GetScreenInfo(Screen screen)
{
    switch (screen)
    {
    case Screen::Dashboard:
        return { kIdNavDashboard, ui::Icon::Dashboard, _T("仪表板"),
                 _T("平台运行状态、近期活动与待处理事项") };
    case Screen::Enterprises:
        return { kIdNavEnterprises, ui::Icon::Building, _T("企业管理"),
                 _T("企业入驻、组织架构与服务号管理") };
    case Screen::Announcements:
        return { kIdNavAnnouncements, ui::Icon::Megaphone, _T("公告中心"),
                 _T("面向企业与员工发布平台公告（Phase 5 接入）") };
    case Screen::Audit:
        return { kIdNavAudit, ui::Icon::Shield, _T("审计日志"),
                 _T("所有管理操作的留痕与追溯（Phase 5 接入）") };
    case Screen::Settings:
        return { kIdNavSettings, ui::Icon::Settings, _T("平台设置"),
                 _T("域名、SSO、安全策略、品牌定制等") };
    case Screen::ClientVersions:
        return { kIdNavClientVersions, ui::Icon::Download, _T("客户端版本"),
                 _T("发布 PC 客户端升级包，控制发布与撤回") };
    //敏感词：词库维护与拦截记录
    case Screen::SensitiveWords:
        return { kIdNavSensitiveWords, ui::Icon::ShieldAlert, _T("敏感词"),
                 _T("维护企业的敏感词库，查看服务端拦下的内容") };
    //文件共享：查看共享与成员、强制取消
    case Screen::FileShares:
        return { kIdNavFileShares, ui::Icon::Folder, _T("文件共享"),
                 _T("查看企业成员发起的文件共享，必要时强制取消") };
    //文件共享页的「共享审计」页签：侧栏高亮与面包屑沿用「文件共享」
    case Screen::FileShareAudit:
        return { kIdNavFileShares, ui::Icon::Folder, _T("文件共享"),
                 _T("按人、按动作、按时间查询文件共享的操作记录") };
    //企业问答：显示在群聊「问答」页签里的问答
    case Screen::EnterpriseQa:
        return { kIdNavEnterpriseQa, ui::Icon::Info, _T("企业问答"),
                 _T("维护显示在群聊「问答」页签里的问答，可应用到全部群或所选的群") };
    //机器人：企业机器人的列表、编辑、规则与外部服务域名白名单
    case Screen::EnterpriseBot:
        return { kIdNavEnterpriseBot, ui::Icon::Bot, _T("机器人"),
                 _T("配置在本企业群里自动应答、定时提醒、接收外部系统推送的企业机器人") };
    case Screen::EnterpriseDetail:
    default:
        // 企业详情是「企业管理」的子屏：侧栏高亮 / 面包屑沿用企业管理。
        return { kIdNavEnterprises, ui::Icon::Building, _T("企业管理"),
                 _T("企业入驻、组织架构与服务号管理") };
    }
}

// 把新增企业弹窗套餐下拉框的文本换算为套餐编号（adminapi::PlanCodeFromName），
// 不认识时回退为「专业版」。
//   planName：下拉框的当前文本。
int PlanCodeForNewEnterprise(const CString& planName)
{
    const int code = adminapi::PlanCodeFromName(planName);
    return (code >= 0) ? code : kPlanProfessional;
}

// 把一段文本放入系统剪贴板。
//   owner：剪贴板拥有者窗口句柄。
//   text：要复制的文本。
void CopyTextToClipboard(HWND owner, const CString& text)
{
    if (!::OpenClipboard(owner))
    {
        return;
    }
    ::EmptyClipboard();

    // 分配可移动全局内存并填入文本（含结尾 '\0'）。
    const SIZE_T bytes =
        (static_cast<SIZE_T>(text.GetLength()) + 1) * sizeof(TCHAR);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != nullptr)
    {
        void* dst = ::GlobalLock(hMem);
        if (dst != nullptr)
        {
            memcpy(dst, static_cast<LPCTSTR>(text), bytes);
            ::GlobalUnlock(hMem);
            // SetClipboardData 成功后由剪贴板接管 hMem，本函数不再释放；
            // 仅失败时需自行释放。
            if (::SetClipboardData(CF_UNICODETEXT, hMem) == nullptr)
            {
                ::GlobalFree(hMem);
            }
        }
        else
        {
            ::GlobalFree(hMem);
        }
    }
    ::CloseClipboard();
}

// 「调部门」部门选择菜单的一项。
struct DeptPick
{
    CString m_id;     // 部门 ID
    CString m_name;   // 菜单显示名（带层级缩进前缀）
};

// 在组织树里递归查找 userId 员工所属部门的 ID。
//   dept：起始部门节点（可为合成根）。
//   返回：命中返回其父部门 ID；未找到返回空串。
CString FindParentDeptId(const OrgNode* dept, const CString& userId)
{
    if (dept == nullptr)
    {
        return CString();
    }
    for (size_t i = 0; i < dept->m_children.size(); ++i)
    {
        const OrgNode& c = dept->m_children[i];
        if (c.m_kind == OrgNodeKind::User && c.m_id == userId)
        {
            return dept->m_id;
        }
    }
    for (size_t i = 0; i < dept->m_children.size(); ++i)
    {
        const OrgNode& c = dept->m_children[i];
        if (c.m_kind == OrgNodeKind::Dept)
        {
            const CString r = FindParentDeptId(&c, userId);
            if (!r.IsEmpty())
            {
                return r;
            }
        }
    }
    return CString();
}

// 递归收集 dept 下的所有子部门（不含 dept 自身），名称按层级缩进。
//   depth：当前递归深度，用于缩进；excludeId：要排除的部门 ID。
void CollectDepts(const OrgNode* dept, int depth, const CString& excludeId,
                  std::vector<DeptPick>& out)
{
    if (dept == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < dept->m_children.size(); ++i)
    {
        const OrgNode& c = dept->m_children[i];
        if (c.m_kind != OrgNodeKind::Dept)
        {
            continue;
        }
        if (c.m_id != excludeId)
        {
            DeptPick pick;
            pick.m_id = c.m_id;
            CString indent;
            for (int k = 0; k < depth; ++k)
            {
                indent += _T("   ");
            }
            pick.m_name = indent + c.m_name;
            out.push_back(pick);
        }
        CollectDepts(&c, depth + 1, excludeId, out);
    }
}

} // anonymous namespace

void AdminFrame::InitContent()
{
    // 开启 balloonui 的双击合成：企业列表「双击行进详情」依赖 DUI 控件收到
    // OnLButtonDblClk。详见 DuiHost::EnableDoubleClick 注释。
    // （2026-10-06 更正：balloonui 窗口的窗口类其实带 CS_DBLCLKS，系统本就会投递双击，这里的双击合成实际不起作用，保留无害）
    EnableDoubleClick(true);

    // 构建客户区根容器：含 Toast 层 + 登录界面页 + 新增企业弹窗层。
    // SetClientContent 仅此一次，之后所有界面切换都在 RootContainer 内部
    // 切显隐，不再替换客户区根。
    auto root  = std::make_unique<ui::RootContainer>();
    // toast 层走 balloonwjui::DuiToast(顶层浮出 + 渐入渐出 + 自驱定时)。
    // 历史 ui::Toast 已删除。
    auto toast = std::make_unique<balloonwjui::DuiToast>();
    m_toast = toast.get();
    root->SetToast(std::move(toast));

    auto login = BuildLoginScreen();
    m_loginPage = login.get();
    root->AddPage(std::move(login));

    // 网络设置页：与登录页同级常驻，从登录页的「网络设置」链接进入。
    auto network = std::make_unique<NetworkSettingsScreen>();
    m_networkScreen = network.get();
    m_networkPage   = network.get();
    root->AddPage(std::move(network));

    m_root = root.get();
    SetClientContent(std::move(root));

    // 页面切换、弹窗开关时重新决定 FlamingoLink.dll 的「在线用户」页显示与否：
    // 它是原生子窗口，会画在叠层弹窗与登录页之上。
    m_root->SetLayerChangedCallback(&AdminFrame::LinkLayerChangedThunk, this);

    // 加载 FlamingoLink.dll（长连接与「在线用户」页）。失败只记日志，没有这个功能，其余照常。
    LinkBridge::Instance().Load(m_hWnd, WM_APP_LINK_EXPIRED);

    PrefillLogin();

    // 弹窗层：新增企业弹窗 + 组织节点弹窗。两者一经构建便常驻，开 / 关
    // 只切显隐。弹窗内的文本框是无窗口控件，随所在弹窗一起参与 DUI 合成，
    // 弹窗逻辑上隐藏时它们不会画出任何内容，因此不需要额外的预隐藏步骤。
    auto modal = std::make_unique<NewEnterpriseModal>();
    m_modal = modal.get();
    m_root->AddModal(std::move(modal));

    // 组织节点对话框(NodeModal,新增部门 / 新增员工)已经改成独立顶层
    // DuiFrameWindow,on-demand 在 ShowNodeModal 里 new + CreateAndShow,
    // 关闭即销毁。RootContainer 不再持有它。

    // 通用确认/信息对话框(ConfirmDialog)已经改成独立顶层 DuiFrameWindow,
    // on-demand 在调用方需要时 new + CreateAndShow,关闭即销毁。
    // RootContainer 不再持有它。

    auto serviceModal = std::make_unique<ServiceModal>();
    m_serviceModal = serviceModal.get();
    m_root->AddModal(std::move(serviceModal));

    auto bindingModal = std::make_unique<BindingModal>();
    m_bindingModal = bindingModal.get();
    m_root->AddModal(std::move(bindingModal));

    auto composeModal = std::make_unique<ComposeModal>();
    m_composeModal = composeModal.get();
    m_root->AddModal(std::move(composeModal));

    auto groupModal = std::make_unique<GroupModal>();
    m_groupModal = groupModal.get();
    m_root->AddModal(std::move(groupModal));

    // 新建客户端版本弹窗（仅平台超管会用到，但与其它弹窗一样常驻，
    // 开 / 关只切显隐 —— 销毁一棵宿主仍在跟踪的控件树会留下悬空指针）。
    auto cvModal = std::make_unique<ClientVersionModal>();
    m_clientVersionModal = cvModal.get();
    m_root->AddModal(std::move(cvModal));

    // 新增 / 编辑企业问答弹窗：同样常驻，开 / 关只切显隐。
    auto qaModal = std::make_unique<EnterpriseQaModal>();
    m_qaModal = qaModal.get();
    m_root->AddModal(std::move(qaModal));

    // 机器人的编辑窗、规则编辑窗与外部服务域名白名单窗：同样常驻，开 / 关只切显隐。弹窗层同一时刻只显示一个弹窗，
    // 打开规则编辑窗时机器人编辑窗被隐藏（内容保留），关闭规则编辑窗后再显示回来。
    std::unique_ptr<EnterpriseBotModal> botModal(new EnterpriseBotModal());
    m_botModal = botModal.get();
    m_root->AddModal(std::move(botModal));
    std::unique_ptr<BotRuleModal> botRuleModal(new BotRuleModal());
    m_botRuleModal = botRuleModal.get();
    m_root->AddModal(std::move(botRuleModal));
    std::unique_ptr<BotDomainsModal> botDomainsModal(new BotDomainsModal());
    m_botDomainsModal = botDomainsModal.get();
    m_root->AddModal(std::move(botDomainsModal));

    // 添加 / 编辑敏感词弹窗：同样常驻，开 / 关只切显隐。
    auto swModal = std::make_unique<SensitiveWordModal>();
    m_swModal = swModal.get();
    m_root->AddModal(std::move(swModal));

    // 批量导入敏感词弹窗：同样常驻，开 / 关只切显隐。
    auto swImportModal = std::make_unique<SensitiveImportModal>();
    m_swImportModal = swImportModal.get();
    m_root->AddModal(std::move(swImportModal));

    // 修改员工资料弹窗(EmployeeEditDialog)不再预创建 —— 它已经改成
    // 独立顶层 DuiFrameWindow,on-demand 在 OnUserDetailLoaded 里 new 出来,
    // 关闭时 DestroyWindow + AdminFrame 清指针。RootContainer 不再持有它。

    auto entSettingsModal = std::make_unique<EnterpriseSettingsModal>();
    m_enterpriseSettingsModal = entSettingsModal.get();
    m_root->AddModal(std::move(entSettingsModal));

    m_root->ShowPage(m_loginPage);
    FA_LOGI(_T("显示登录界面"));
}

LRESULT AdminFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    // 先关闭并卸载 FlamingoLink.dll：它的页面窗口是本窗口的子窗口，此刻还在，DLL 自己销毁它，
    // 再停连接线程、释放 DLL。放在最前面，之后的清理不会再碰到 DLL 里的窗口。
    LinkBridge::Instance().Unload();

    // 销毁调试控制台窗口（Destroy 会摘掉 DebugConsole 的 sink 并销毁窗口）。
    // 须在下面的退出消息循环逻辑之前完成，确保调试窗口及其 sink 干净拆除。
    flamingoadmin::DebugConsoleWindow::Instance().Destroy();

    // DuiFrameWindow 不主动结束消息循环，单 frame 进程须自己 PostQuitMessage。
    ::PostQuitMessage(0);
    bHandled = FALSE;   // 让 DuiFrameWindow / DuiHost 的 WM_DESTROY 清理继续执行
    return 0;
}

LRESULT AdminFrame::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
{
    // 历史的 kToastTimerId 与 kAnimPulseTimerId 两个分支均已删除 ——
    // balloonwjui::DuiToast 与 DuiAnimMgr 都自带定时驱动,不再需要 admin
    // 周期性调 Tick / TickAll。本函数目前只剩顶栏审计滚动提示这一路定时器。
    if (wParam == kTickerTimerId)
    {
        // 顶栏审计滚动提示前进一条；转完一整圈时重新拉取最近的审计，展示的总是最新几条。
        if (m_topBar != nullptr && m_topBar->AdvanceTicker())
        {
            FetchTickerAudits();
        }
        bHandled = TRUE;
        return 0;
    }
    if (wParam == SensitiveWordsScreen::kHitSearchTimerId)
    {
        // 敏感词界面「拦截记录」的发送者搜索防抖到点（定时器由界面自己停掉）。
        if (m_sensitiveWordsScreen != nullptr)
        {
            m_sensitiveWordsScreen->OnHitSearchTimer();
        }
        else
        {
            KillTimer(SensitiveWordsScreen::kHitSearchTimerId);
        }
        bHandled = TRUE;
        return 0;
    }
    if (wParam == FileSharesScreen::kSearchTimerId)
    {
        // 「文件共享」页的分享者搜索防抖到点（定时器由界面自己停掉）。
        if (m_fileSharesScreen != nullptr)
        {
            m_fileSharesScreen->OnSearchTimer();
        }
        else
        {
            KillTimer(FileSharesScreen::kSearchTimerId);
        }
        bHandled = TRUE;
        return 0;
    }
    if (wParam == FileShareAuditScreen::kSearchTimerId || wParam == FileShareAuditScreen::kShareIdTimerId)
    {
        // 「共享审计」页的操作人搜索、共享 ID 输入防抖到点（定时器由界面自己停掉）。
        if (m_fileShareAuditScreen == nullptr)
        {
            KillTimer(wParam);
        }
        else if (wParam == FileShareAuditScreen::kSearchTimerId)
        {
            m_fileShareAuditScreen->OnSearchTimer();
        }
        else
        {
            m_fileShareAuditScreen->OnShareIdTimer();
        }
        bHandled = TRUE;
        return 0;
    }
    if (wParam == SensitiveWordsScreen::kSearchTimerId)
    {
        // 敏感词界面的搜索防抖到点：按搜索框当前文字查询（定时器由界面自己停掉）。
        if (m_sensitiveWordsScreen != nullptr)
        {
            m_sensitiveWordsScreen->OnSearchTimer();
        }
        else
        {
            KillTimer(SensitiveWordsScreen::kSearchTimerId);
        }
        bHandled = TRUE;
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT AdminFrame::OnDuiNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* n = reinterpret_cast<const DuiNotify*>(lParam);
    if (n == nullptr)
    {
        bHandled = FALSE;
        return 0;
    }
    bHandled = TRUE;

    // ---- 单行输入框里按了回车：对照表里的输入框提交所在窗口（见 kEnterSubmitRules）----
    // 不在表里的输入框（搜索框等）HandleEditEnter 返回 false，照常往下派发。
    if (n->code == DuiEdit::DUIN_EDIT_ENTER && HandleEditEnter(n))
    {
        return 0;
    }

    // ---- 双击类通知 ----
    if (n->code == DUIN_DBLCLK)
    {
        // 企业表格行双击 → 打开该企业详情。双击经 DuiHost 的双击合成
        //（EnableDoubleClick）由 DuiVirtualList 上报，n->extra 为行索引。
        if (n->ctrlId == kIdEntTableRows && m_enterprises != nullptr)
        {
            const CString entId = m_enterprises->EnterpriseIdByRow(
                static_cast<int>(n->extra));
            if (!entId.IsEmpty())
            {
                m_detailInitialTab = kDetailTabOrg;
                StartLoadEnterpriseDetail(entId);
            }
        }
        else if (n->ctrlId == kIdDashViewEnterprises)
        {
            // 仪表盘「活跃企业」双击某行 → 打开该企业详情。n->extra 是行索引,
            // 即 DataStore::Enterprises() 下标(与活跃企业卡展示顺序一致)。
            const std::vector<Enterprise>& list =
                DataStore::Instance().Enterprises();
            const int idx = static_cast<int>(n->extra);
            if (idx >= 0 && idx < static_cast<int>(list.size()))
            {
                m_detailInitialTab = kDetailTabOrg;
                StartLoadEnterpriseDetail(list[idx].m_id);
            }
        }
        else if (n->ctrlId == kIdBotModalRules)
        {
            // 机器人编辑窗的规则列表双击一行 → 编辑这条规则。打开规则编辑窗会切换弹窗显隐，
            // 推迟到本轮通知派发结束之后再做。
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotEditRule, 0);
        }
        return 0;
    }

    // ---- 点击类通知 ----
    if (n->code == DUIN_CLICK)
    {
        switch (n->ctrlId)
        {
        case kIdLoginSubmit:
            // 登录：先校验，再决定是否进入外壳。
            TrySubmitLogin();
            break;

        case kIdLoginNetwork:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowNetwork, 0);
            break;
        case kIdNetBack:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBackToLogin, 0);
            break;
        case kIdNetSave:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSaveNetwork, 0);
            break;
        case kIdNetTest:
            StartNetTest();
            break;

        case kIdNavDashboard:
        case kIdNavEnterprises:
        case kIdNavAnnouncements:
        case kIdNavAudit:
        case kIdNavSettings:
        case kIdNavClientVersions:
        case kIdNavSensitiveWords:
        case kIdNavFileShares:
        case kIdNavEnterpriseQa:
        case kIdNavEnterpriseBot:
            // 切换界面延迟到事件派发结束后执行。kIdNav* 连续递增、与
            // Screen 枚举一一对应。
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE,
                          kNavScreenBase + (n->ctrlId - kIdNavDashboard), 0);
            break;

        case kIdLogout:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavLogout, 0);
            break;

        case kIdSidebarToggle:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavToggleSidebar, 0);
            break;

        case kIdDashViewEnterprises:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE,
                          kNavScreenBase + static_cast<int>(Screen::Enterprises), 0);
            break;
        case kIdDashViewAnnouncements:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE,
                          kNavScreenBase + static_cast<int>(Screen::Announcements), 0);
            break;
        case kIdDashViewAudit:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE,
                          kNavScreenBase + static_cast<int>(Screen::Audit), 0);
            break;

        case kIdEntNew:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowModal, 0);
            break;
        case kIdEntExport:
            StartEnterpriseExport();
            break;
        case kIdEntMore:
            // 展开 / 收起「更多筛选」条。
            if (m_enterprises != nullptr)
            {
                m_enterprises->ToggleMoreFilters();
            }
            break;
        //「更多筛选」条的「清除」：清空行业、套餐与起止日期后重新拉取
        case kIdEntMoreClear:
            if (m_enterprises != nullptr)
            {
                m_enterprises->ClearMoreFilters();
            }
            break;
        case kIdEntRefresh:
            if (m_enterprises != nullptr)
            {
                m_enterprises->Refresh();
            }
            ShowToast(_T("企业列表已刷新"));
            break;
        // 企业表格行的操作菜单走「操作」列三个点的左键点击 —— 见
        // AdminFrame::OnLButtonDown，不在此处理。

        case kIdModalClose:
        case kIdModalCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavHideModal, 0);
            break;
        case kIdModalSubmit:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSubmitModal, 0);
            break;

        case kIdDetailBack:
            // 企业详情「返回」→ 回到企业管理列表。
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE,
                          kNavScreenBase + static_cast<int>(Screen::Enterprises), 0);
            break;
        case kIdDetailSettings:
            // 打开企业设置：取当前详情企业记录，走统一入口。
            if (m_enterpriseDetail != nullptr)
            {
                const Enterprise* ent = DataStore::Instance().FindEnterprise(
                    m_enterpriseDetail->GetEnterpriseId());
                if (ent != nullptr)
                {
                    OpenEnterpriseSettings(*ent);
                }
            }
            break;
        case kIdDetailAnnounce:
            // 发布公告：打开新建公告，受众预设为「指定企业：本企业」。打开动作延迟到
            // 事件派发结束后，与公告中心的「新建公告」走同一条路径。
            if (m_enterpriseDetail != nullptr)
            {
                m_composePresetEntId = m_enterpriseDetail->GetEnterpriseId();
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowCompose, 0);
            }
            break;
        case kIdDetailPrimary:
            // 组织架构 tab：新增员工到默认部门；服务号 tab：新建服务号（与服务号面板里的同名按钮相同）。
            if (m_enterpriseDetail != nullptr && m_enterpriseDetail->IsOrgTab())
            {
                const CString parentId = m_enterpriseDetail->DefaultParentDept();
                const OrgNode* p = DataStore::Instance().FindOrgNode(
                    m_enterpriseDetail->GetEnterpriseId(), parentId);
                m_nodeModalParentId      = parentId;
                m_nodeModalPendingIsDept = false;
                m_nodeModalParentName    = (p != nullptr) ? p->m_name : CString();
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowNodeModal, 0);
            }
            else
            {
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowServiceModal, 0);
            }
            break;
        case kIdDetailCreateGroup:
            // 组织架构 tab：把本企业成员拉进一个新群。先载入企业成员，再弹窗。
            if (m_enterpriseDetail != nullptr && m_enterpriseDetail->IsOrgTab()
                && m_groupModal != nullptr)
            {
                m_groupModal->SetEnterprise(m_enterpriseDetail->GetEnterpriseId());
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowGroupModal, 0);
            }
            break;
        case kIdOrgRefresh:
            // 组织树工具条「刷新」→ 重新拉取当前企业的部门树（含成员）。
            // 树的展开状态、滚动位置、搜索过滤词都由 OrgTree::Rebuild 保留。
            if (m_enterpriseDetail != nullptr && m_enterpriseDetail->IsOrgTab())
            {
                if (m_orgTreeReloading)
                {
                    // 上一次刷新还没回来，避免连点堆请求。
                    ShowToast(_T("正在刷新组织架构，请稍候…"));
                }
                else
                {
                    ReloadOrgTree(true);
                }
            }
            break;
        case kIdOrgExpandAll:
            // 组织树工具条「展开全部」。IconButton 的通知直接到宿主窗口，
            // 不沿控件父链冒泡，故在这里转调回详情屏。
            if (m_enterpriseDetail != nullptr)
            {
                m_enterpriseDetail->ExpandAllOrg();
            }
            break;
        case kIdOrgCollapse:
            // 组织树工具条「折叠」。转调理由同上。
            if (m_enterpriseDetail != nullptr)
            {
                m_enterpriseDetail->CollapseAllOrg();
            }
            break;
        case kIdDetailScreen:
        {
            // 企业详情屏上报的组织动作（增删部门 / 员工）。
            if (m_enterpriseDetail == nullptr)
            {
                break;
            }
            const EnterpriseDetailScreen::Pending pend =
                m_enterpriseDetail->GetPending();
            const CString nodeId = m_enterpriseDetail->GetPendingNodeId();
            if (pend == EnterpriseDetailScreen::Pending::Delete)
            {
                // 删除前先弹确认框（不可逆操作须二次确认）。
                const OrgNode* n = DataStore::Instance().FindOrgNode(
                    m_enterpriseDetail->GetEnterpriseId(), nodeId);
                const CString name = (n != nullptr) ? n->m_name : nodeId;
                const bool isUser = (n != nullptr && n->m_kind == OrgNodeKind::User);
                CString msg;
                if (isUser)
                {
                    // 服务端 user/delete.do 物理删除账号（t_user 与部门成员、权限记录一并删除）。
                    msg.Format(_T("确定要删除员工「%s」吗？该员工的账号将被永久删除，")
                               _T("此操作不可撤销。"),
                               static_cast<LPCTSTR>(name));
                }
                else
                {
                    // 服务端 dept/delete.do 在部门下还有在用子部门或在职成员时以 112 拒绝删除。
                    msg.Format(_T("确定要删除部门「%s」吗？部门下还有子部门或在职成员时")
                               _T("不能删除，请先移走。"),
                               static_cast<LPCTSTR>(name));
                }
                m_confirmDeleteNodeId = nodeId;
                m_confirmTarget       = 0;   // 0 = 组织节点
                ConfirmDialogSpec spec;
                spec.m_title         = isUser ? _T("删除员工") : _T("删除部门");
                spec.m_message       = msg;
                spec.m_okText        = _T("删除");
                spec.m_confirmDanger = true;
                ShowConfirmDialog(spec);
            }
            else if (pend == EnterpriseDetailScreen::Pending::AddDept ||
                     pend == EnterpriseDetailScreen::Pending::AddUser)
            {
                const OrgNode* p = DataStore::Instance().FindOrgNode(
                    m_enterpriseDetail->GetEnterpriseId(), nodeId);
                m_nodeModalParentId      = nodeId;
                m_nodeModalPendingIsDept =
                    (pend == EnterpriseDetailScreen::Pending::AddDept);
                m_nodeModalParentName    = (p != nullptr) ? p->m_name : CString();
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowNodeModal, 0);
            }
            else if (pend == EnterpriseDetailScreen::Pending::SelectUser)
            {
                // 选中员工 → 拉取权限矩阵、登录记录、操作历史，填充各 tab。
                m_permUserId = nodeId;
                FireGetPermissions(nodeId);
                FireLoginRecords(nodeId);
                FireActivity(nodeId);
                FireMessageStats(nodeId);
            }
            else if (pend == EnterpriseDetailScreen::Pending::SetRole ||
                     pend == EnterpriseDetailScreen::Pending::SetPerms)
            {
                // 员工角色 / 权限覆盖变更 → 落库。
                OrgDetailPanel* panel = m_enterpriseDetail->GetOrgDetailPanel();
                if (panel != nullptr && !nodeId.IsEmpty())
                {
                    m_permUserId = nodeId;
                    const ServerConfig& server = AppConfig::Instance().Server();
                    const CString token = AdminSession::Instance().Token();
                    const long long uid = _ttoi64(nodeId);
                    if (pend == EnterpriseDetailScreen::Pending::SetRole)
                    {
                        // 角色决定能否登录控制台等权限，先确认；面板上已先切换到新角色。
                        LPCTSTR roleText = _T("普通成员");
                        if (panel->GetUserRole() == kUserRoleDeptLeader)
                        {
                            roleText = _T("部门负责人");
                        }
                        else if (panel->GetUserRole() == kUserRoleEntAdmin)
                        {
                            roleText = _T("企业管理员");
                        }
                        const OrgNode* n = DataStore::Instance().FindOrgNode(
                            m_enterpriseDetail->GetEnterpriseId(), nodeId);
                        const CString name = (n != nullptr) ? n->m_name : nodeId;
                        CString msg;
                        msg.Format(_T("确定要把「%s」的角色改为「%s」吗？"),
                                   static_cast<LPCTSTR>(name), roleText);
                        m_confirmDeleteNodeId = nodeId;
                        m_confirmTarget       = kConfirmTargetSetRole;
                        ConfirmDialogSpec spec;
                        spec.m_title   = _T("修改员工角色");
                        spec.m_message = msg;
                        spec.m_okText  = _T("修改");
                        ShowConfirmDialog(spec);
                    }
                    else
                    {
                        HttpRequest req = adminapi::MakeUserSetPermissionsRequest(
                            server, token, uid, panel->GetPermOverrides());
                        req.m_tag       = kApiTagUserSetPerms;
                        req.m_replyHwnd = m_hWnd;
                        req.m_replyMsg  = WM_APP_API_RESULT;
                        HttpWorker::Instance().Post(req);
                    }
                }
            }
            else if (pend == EnterpriseDetailScreen::Pending::SetUserStatus)
            {
                // 启用 / 停用员工：按当前状态算目标状态，先弹确认框，
                // 确认后由 OnConfirmDialogConfirmed 落库。
                const OrgNode* n = DataStore::Instance().FindOrgNode(
                    m_enterpriseDetail->GetEnterpriseId(), nodeId);
                const bool disabled =
                    (n != nullptr && n->m_userStatus == UserStatus::Disabled);
                m_confirmStatusTarget = disabled ? 0 : 1;   // 0 启用 / 1 停用
                m_confirmDeleteNodeId = nodeId;
                m_confirmTarget       = kConfirmTargetSetStatus;
                ConfirmDialogSpec spec;
                spec.m_title         = disabled ? _T("启用账号") : _T("停用账号");
                spec.m_message       = disabled
                    ? _T("确定启用该员工的账号吗?启用后该员工可重新登录各客户端。")
                    : _T("确定停用该员工的账号吗?停用后该员工将无法登录任何客户端。");
                spec.m_okText        = disabled ? _T("启用") : _T("停用");
                spec.m_confirmDanger = !disabled;   // 停用红、启用蓝
                ShowConfirmDialog(spec);
            }
            else if (pend == EnterpriseDetailScreen::Pending::ResetPassword)
            {
                // 重置密码不可逆 → 先弹确认框,确认后由 OnConfirmDialogConfirmed 落库。
                m_confirmDeleteNodeId = nodeId;
                m_confirmTarget       = kConfirmTargetResetPwd;
                ConfirmDialogSpec spec;
                spec.m_title         = _T("重置登录密码");
                // 2026-10-07 起服务端重置成功后会踢掉该员工全部在线设备，确认框里一并说明
                spec.m_message       = _T("确定重置该员工的登录密码吗?重置后将生成新密码,")
                                       _T("该员工的旧密码立即失效,已登录的设备会被强制下线。");
                spec.m_okText        = _T("重置");
                spec.m_confirmDanger = true;
                ShowConfirmDialog(spec);
            }
            else if (pend == EnterpriseDetailScreen::Pending::KickDevices)
            {
                // 强制下线全部设备：先确认，确认后由 SubmitKickDevices 提交（deviceid 传空）。
                const OrgNode* n = DataStore::Instance().FindOrgNode(
                    m_enterpriseDetail->GetEnterpriseId(), nodeId);
                const CString name = (n != nullptr) ? n->m_name : nodeId;
                CString msg;
                msg.Format(_T("确定要让「%s」的全部设备下线吗？该员工需要重新登录才能")
                           _T("继续使用。"),
                           static_cast<LPCTSTR>(name));
                m_confirmDeleteNodeId = nodeId;
                m_confirmTarget       = kConfirmTargetKick;
                ConfirmDialogSpec spec;
                spec.m_title         = _T("强制下线");
                spec.m_message       = msg;
                spec.m_okText        = _T("下线");
                spec.m_confirmDanger = true;
                ShowConfirmDialog(spec);
            }
            else if (pend == EnterpriseDetailScreen::Pending::EditUser)
            {
                // 修改员工资料：先拉完整资料，回来后预填弹窗并打开。
                HttpRequest req = adminapi::MakeUserDetailRequest(
                    AppConfig::Instance().Server(),
                    AdminSession::Instance().Token(), _ttoi64(nodeId));
                req.m_replyHwnd = m_hWnd;
                req.m_replyMsg  = WM_APP_API_RESULT;
                req.m_tag       = kApiTagUserDetail;
                HttpWorker::Instance().Post(req);
                ShowToast(_T("正在加载员工资料…"));
            }
            else if (pend == EnterpriseDetailScreen::Pending::MoveUser)
            {
                // 调部门：记下员工与光标位置，延迟到本通知处理结束后弹菜单
                //（菜单 TrackPopup 是阻塞的嵌套循环，不能在 DUI 派发里直接调）。
                m_moveUserId = nodeId;
                ::GetCursorPos(&m_deptMenuPt);
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowDeptMenu, 0);
            }
            break;
        }

        // kIdNodeClose/Cancel/Submit 历史在此 PostMessage 到主窗口的 navigate
        // 分发。NodeModal 改成独立顶层窗口后，这三个按钮事件不再经主窗口
        // OnDuiNotify —— 对话框自己 CHAIN 到 DuiFrameWindow 收 WM_DUI_NOTIFY
        // 并在 NodeModal::OnDuiNotify 里处理（取消→RequestClose，创建→校验后
        // 回调 OnNodeModalSubmit），故这里不再需要对应分支。

        case kIdGroupClose:
        case kIdGroupCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavHideGroupModal, 0);
            break;
        case kIdGroupSubmit:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSubmitGroupModal, 0);
            break;

        // ---- 客户端版本界面与其弹窗 ----
        case kIdCvRefresh:
            StartLoadClientVersions();
            break;
        case kIdCvNew:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowCvModal, 0);
            break;
        case kIdCvRowAction:
            HandleClientVersionRowAction();
            break;
        case kIdCvModalClose:
        case kIdCvModalCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavHideCvModal, 0);
            break;
        case kIdCvModalSaveDraft:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSubmitCvDraft, 0);
            break;
        case kIdCvModalPublish:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSubmitCvPublish, 0);
            break;
        case kIdCvModalRescan:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavRescanPackages, 0);
            break;

        // ---- 企业问答界面与其弹窗 ----
        //「新增问答」：界面会发出「打开新增弹窗」的界面请求，经队列推迟处理
        case kIdQaAdd:
            if (m_qaScreen != nullptr)
            {
                m_qaScreen->OnAddClicked();
            }
            break;
        //「刷新」
        case kIdQaRefresh:
            if (m_qaScreen != nullptr)
            {
                m_qaScreen->OnRefreshClicked();
            }
            break;
        //弹窗的关闭按钮与「取消」
        case kIdQaModalClose:
        case kIdQaModalCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavQaHideModal, 0);
            break;
        //弹窗的「保存」
        case kIdQaModalSave:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavQaSubmitModal, 0);
            break;

        // ---- 机器人界面与其编辑窗、规则编辑窗、白名单窗 ----
        //「新建机器人」：界面会发出「打开新建编辑窗」的界面请求，经队列推迟处理
        case kIdBotAdd:
            if (m_botScreen != nullptr)
            {
                m_botScreen->OnAddClicked();
            }
            break;
        //「刷新」
        case kIdBotRefresh:
            if (m_botScreen != nullptr)
            {
                m_botScreen->OnRefreshClicked();
            }
            break;
        //「外部服务域名白名单」：界面会发出「打开白名单窗」的界面请求
        case kIdBotDomains:
            if (m_botScreen != nullptr)
            {
                m_botScreen->OnDomainsClicked();
            }
            break;
        //编辑窗的关闭按钮与「取消」
        case kIdBotModalClose:
        case kIdBotModalCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotHideModal, 0);
            break;
        //编辑窗的「保存」
        case kIdBotModalSave:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotSubmitModal, 0);
            break;
        //规则的「添加」：打开规则编辑窗会切换弹窗显隐，推迟到通知派发结束后再做
        case kIdBotModalRuleAdd:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotAddRule, 0);
            break;
        //规则的「编辑」：同上
        case kIdBotModalRuleEdit:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotEditRule, 0);
            break;
        //规则的「上移」：只改规则列表里的数据，按钮本身不受影响，可以就地处理
        case kIdBotModalRuleUp:
            if (m_botModal != nullptr)
            {
                m_botModal->MoveSelectedRule(-1);
            }
            break;
        //规则的「下移」：同上
        case kIdBotModalRuleDown:
            if (m_botModal != nullptr)
            {
                m_botModal->MoveSelectedRule(1);
            }
            break;
        //规则的「删除」：同上
        case kIdBotModalRuleDelete:
            if (m_botModal != nullptr)
            {
                m_botModal->DeleteSelectedRule();
            }
            break;
        //Webhook「复制」：地址、令牌、签名密钥放进剪贴板
        case kIdBotModalCopy:
            if (m_botModal != nullptr && m_botModal->HasSecrets())
            {
                CopyTextToClipboard(m_hWnd, m_botModal->WebhookCopyText());
                ShowToast(_T("已复制 Webhook 地址、令牌与签名密钥"));
            }
            break;
        //Webhook「重新生成」：界面先请宿主弹确认框，确认后才发请求
        case kIdBotModalResetToken:
            if (m_botModal != nullptr && m_botScreen != nullptr)
            {
                m_botScreen->RequestResetToken(m_botModal->EditingId());
            }
            break;
        //「试一试」
        case kIdBotModalTryRun:
            RunBotTry();
            break;
        //规则编辑窗的关闭按钮与「取消」：回到机器人编辑窗
        case kIdBotRuleClose:
        case kIdBotRuleCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotCloseRule, 0);
            break;
        //规则编辑窗的「确定」
        case kIdBotRuleSave:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotSubmitRule, 0);
            break;
        //白名单窗的关闭按钮与「取消」
        case kIdBotDomainsClose:
        case kIdBotDomainsCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotHideDomains, 0);
            break;
        //白名单窗的「保存」
        case kIdBotDomainsSave:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavBotSubmitDomains, 0);
            break;

        // ---- 敏感词界面与其弹窗 ----
        //「添加」按钮：界面会发出「打开添加弹窗」的界面请求，经队列推迟处理
        case kIdSwAdd:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnAddClicked();
            }
            break;
        //弹窗的关闭按钮与「取消」
        case kIdSwModalClose:
        case kIdSwModalCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSwHideModal, 0);
            break;
        //弹窗的「保存」
        case kIdSwModalSave:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSwSubmitModal, 0);
            break;
        //「批量导入」按钮：界面会发出「打开导入弹窗」的界面请求
        case kIdSwImport:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnImportClicked();
            }
            break;
        //「导出」按钮：界面取到内容后发出「存盘」的界面请求
        case kIdSwExport:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnExportClicked();
            }
            break;
        //导入弹窗的关闭按钮与「取消」
        //文件共享页页头的「刷新」：刷新当前页签（文件共享或共享审计）
        case kIdFsRefresh:
            if (m_fileShareCenter != nullptr)
            {
                m_fileShareCenter->Refresh();
            }
            break;

        case kIdSwImpClose:
        case kIdSwImpCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSwHideImport, 0);
            break;
        //导入弹窗的主按钮（开始导入 / 完成）
        case kIdSwImpSubmit:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSwSubmitImport, 0);
            break;
        //导入弹窗的「从文件读取」：打开文件对话框要跑嵌套消息循环，推迟到通知派发结束后再做
        case kIdSwImpReadFile:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSwImportReadFile, 0);
            break;

        // kIdEmpEditClose/Cancel/Submit 历史在此 PostMessage 到主窗口的
        // navigate 分发,EmployeeEditDialog 改成独立顶层窗口后,这些按钮
        // 事件不再经主窗口 OnDuiNotify —— dialog 自己 CHAIN 到 DuiFrameWindow
        // 处理,不需要 PostMessage 路由。

        case kIdEntSetClose:
        case kIdEntSetCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavHideEntSettings, 0);
            break;
        case kIdEntSetSave:
            SubmitEnterpriseUpdate();
            break;
        case kIdEntSetAddAdmin:
            SubmitAddAdmin();
            break;
        //企业详情「功能开关」页里「云端录像空间」的「保存」：校验后提交配额与保留天数
        case kIdCloudQuotaSave:
            if (m_enterpriseDetail != nullptr && m_enterpriseDetail->GetFeaturesPage() != nullptr)
            {
                m_enterpriseDetail->GetFeaturesPage()->OnQuotaSaveClicked(n->ctrlId);
            }
            break;
        case kIdEntSettings:
            // 弹窗自身上报：某管理员行点了「移除」。
            if (m_enterpriseSettingsModal != nullptr)
            {
                const long long aid =
                    m_enterpriseSettingsModal->GetPendingRemoveAdminId();
                if (aid != 0)
                {
                    // 移除立即生效，先确认（确认后见 OnConfirmDialogConfirmed）。
                    const CString adminName =
                        m_enterpriseSettingsModal->GetPendingRemoveAdminName();
                    CString msg;
                    msg.Format(_T("确定要移除企业管理员「%s」吗？移除后该账号立即失去")
                               _T("本企业控制台的管理权限。"),
                               adminName.IsEmpty() ? _T("该管理员")
                                                   : static_cast<LPCTSTR>(adminName));
                    m_confirmDeleteNodeId.Format(_T("%lld"), aid);
                    m_confirmTarget = kConfirmTargetRemoveAdmin;
                    ConfirmDialogSpec spec;
                    spec.m_title         = _T("移除管理员");
                    spec.m_message       = msg;
                    spec.m_okText        = _T("移除");
                    spec.m_confirmDanger = true;
                    ShowConfirmDialog(spec);
                }
            }
            break;

        // kIdConfirmClose/Cancel/Ok/Copy 历史在此 PostMessage 到主窗口
        // navigate 分发,ConfirmDialog 改成独立顶层窗口后这些按钮事件由
        // dialog 自己处理,不再经此。

        case kIdServicesPanel:
        {
            // 服务号面板上报的动作（新建 / 停用 / 删除 / 改绑定）。
            ServicesPanel* sp = (m_enterpriseDetail != nullptr)
                ? m_enterpriseDetail->GetServicesPanel() : nullptr;
            if (sp == nullptr)
            {
                break;
            }
            const ServicesPanel::Pending spend = sp->GetPending();
            if (spend == ServicesPanel::Pending::New)
            {
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowServiceModal, 0);
            }
            else if (spend == ServicesPanel::Pending::ToggleStatus)
            {
                ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavToggleService, 0);
            }
            else if (spend == ServicesPanel::Pending::Delete)
            {
                const CString svcId = sp->GetSelectedServiceId();
                CString name = svcId;
                const std::vector<Service>& list =
                    DataStore::Instance().Services(sp->GetEnterpriseId());
                for (const Service& s : list)
                {
                    if (s.m_id == svcId)
                    {
                        name = s.m_name;
                        break;
                    }
                }
                CString msg;
                msg.Format(_T("确定要删除服务号「%s」吗？所有员工客户端将")
                           _T("立即移除它，此操作不可撤销。"),
                           static_cast<LPCTSTR>(name));
                m_confirmDeleteNodeId = svcId;
                m_confirmTarget       = 1;   // 1 = 服务号
                ConfirmDialogSpec spec;
                spec.m_title         = _T("删除服务号");
                spec.m_message       = msg;
                spec.m_okText        = _T("删除");
                spec.m_confirmDanger = true;
                ShowConfirmDialog(spec);
            }
            else if (spend == ServicesPanel::Pending::EditBindings)
            {
                // 先从服务端读当前绑定，读到后再打开弹窗（见 OnServiceDetailLoaded）。
                // 不能用缓存里的绑定直接打开：列表接口不带绑定，缓存可能是空的，而
                // setbindings.do 是整体替换，从空集合开始保存会清掉原有绑定。
                FetchServiceDetail(sp->GetSelectedServiceId(), true);
            }
            else if (spend == ServicesPanel::Pending::Selected)
            {
                // 面板切换了选中项：读一次它的绑定，供「概览」「绑定范围」显示。
                FetchSelectedServiceBindingsIfNeeded();
            }
            break;
        }

        case kIdSvcClose:
        case kIdSvcCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavHideServiceModal, 0);
            break;
        case kIdSvcSubmit:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSubmitServiceModal, 0);
            break;

        case kIdBindClose:
        case kIdBindCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavHideBindingModal, 0);
            break;
        case kIdBindSave:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSaveBindingModal, 0);
            break;

        case kIdAnnNew:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowCompose, 0);
            break;
        case kIdAnnExport:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavExportAnnouncements, 0);
            break;

        // ---- 公告详情面板(AnnouncementPreviewPanel)的 5 个操作按钮 ----
        // 每个按钮的 DUIN_CLICK 经 NotifyParent 一路冒到此 switch;annId 通过
        // m_announcementsScreen->GetSelectedId() 取(详情面板与左侧列表选中
        // 是同一份状态)。
        case kIdAnnPreviewEdit:
        {
            if (m_announcementsScreen == nullptr)
            {
                break;
            }
            StartAnnouncementEdit(m_announcementsScreen->GetSelectedId());
            break;
        }
        case kIdAnnPreviewCopy:
        {
            // 复制：先拉原公告详情，应答回来后按其内容打开新建公告。
            if (m_announcementsScreen == nullptr)
            {
                break;
            }
            StartAnnouncementCopy(m_announcementsScreen->GetSelectedId());
            break;
        }
        case kIdAnnPreviewRepush:
        {
            // 重新推送：先确认，再推给受众里在线且未读的成员。
            ConfirmAnnouncementRepush();
            break;
        }
        case kIdAnnPreviewTogglePin:
        {
            if (m_announcementsScreen == nullptr)
            {
                break;
            }
            SubmitAnnouncementSetPinned();
            break;
        }
        case kIdAnnPreviewDelete:
        {
            // 「撤回」按钮:仅对已发布公告显示(详情面板按 status 控制其可见性),
            // 把公告改回未发布状态。物理删除走下面的 kIdAnnPreviewDeleteAnn。
            if (m_announcementsScreen == nullptr)
            {
                break;
            }
            const CString annId = m_announcementsScreen->GetSelectedId();
            CString title = annId;
            for (const Announcement& a : DataStore::Instance().Announcements())
            {
                if (a.m_id == annId)
                {
                    title = a.m_title;
                    break;
                }
            }
            CString msg;
            msg.Format(_T("确定要撤回公告「%s」吗?撤回后所有用户客户端将不再")
                       _T("显示它。"),
                       static_cast<LPCTSTR>(title));
            m_confirmDeleteNodeId = annId;
            m_confirmTarget       = 2;   // 2 = 公告(撤回;OnConfirm 按 status 走 recall)
            ConfirmDialogSpec spec;
            spec.m_title         = _T("撤回公告");
            spec.m_message       = msg;
            spec.m_okText        = _T("撤回");
            spec.m_confirmDanger = true;
            ShowConfirmDialog(spec);
            break;
        }
        case kIdAnnPreviewDeleteAnn:
        {
            // 「删除公告」按钮:任意状态都物理删除(服务端 delete.do 会级联删
            // target / read 并写审计 announcement.delete)。
            if (m_announcementsScreen == nullptr)
            {
                break;
            }
            const CString annId = m_announcementsScreen->GetSelectedId();
            CString title = annId;
            for (const Announcement& a : DataStore::Instance().Announcements())
            {
                if (a.m_id == annId)
                {
                    title = a.m_title;
                    break;
                }
            }
            CString msg;
            msg.Format(_T("确定要删除公告「%s」吗?此操作将永久删除该公告且")
                       _T("不可恢复。"),
                       static_cast<LPCTSTR>(title));
            m_confirmDeleteNodeId = annId;
            m_confirmTarget       = kConfirmTargetAnnDelete;
            ConfirmDialogSpec spec;
            spec.m_title         = _T("删除公告");
            spec.m_message       = msg;
            spec.m_okText        = _T("删除");
            spec.m_confirmDanger = true;
            ShowConfirmDialog(spec);
            break;
        }

        case kIdAnnScreen:
        {
            // 公告界面上报的动作（选中拉详情 / 置顶 / 撤回删除 / 其它）。
            if (m_announcementsScreen == nullptr)
            {
                break;
            }
            const AnnouncementsScreen::Pending ap =
                m_announcementsScreen->GetPending();
            if (ap == AnnouncementsScreen::Pending::SelectDetail)
            {
                StartLoadAnnouncementDetail(
                    m_announcementsScreen->GetSelectedId());
            }
            else if (ap == AnnouncementsScreen::Pending::TogglePin)
            {
                SubmitAnnouncementSetPinned();
            }
            else if (ap == AnnouncementsScreen::Pending::Edit)
            {
                StartAnnouncementEdit(m_announcementsScreen->GetSelectedId());
            }
            else if (ap == AnnouncementsScreen::Pending::Delete)
            {
                const CString annId = m_announcementsScreen->GetSelectedId();
                CString title  = annId;
                int     status = 0;
                for (const Announcement& a : DataStore::Instance().Announcements())
                {
                    if (a.m_id == annId)
                    {
                        title  = a.m_title;
                        status = a.m_status;
                        break;
                    }
                }
                // 已发布(status==1)走「撤回」，草稿 / 已撤回走「删除」。
                const bool recall = (status == 1);
                CString msg;
                if (recall)
                {
                    msg.Format(_T("确定要撤回公告「%s」吗？所有用户客户端将不再")
                               _T("显示它，此操作不可撤销。"),
                               static_cast<LPCTSTR>(title));
                }
                else
                {
                    msg.Format(_T("确定要删除公告「%s」吗？此操作不可恢复。"),
                               static_cast<LPCTSTR>(title));
                }
                m_confirmDeleteNodeId = annId;
                m_confirmTarget       = 2;   // 2 = 公告
                ConfirmDialogSpec spec;
                spec.m_title         = recall ? _T("撤回公告") : _T("删除公告");
                spec.m_message       = msg;
                spec.m_okText        = recall ? _T("撤回") : _T("删除");
                spec.m_confirmDanger = true;
                ShowConfirmDialog(spec);
            }
            else if (ap == AnnouncementsScreen::Pending::Misc)
            {
                ShowToast(_T("该功能将在后续阶段开放"), ToastKind::Warn);
            }
            break;
        }

        case kIdComposeClose:
        case kIdComposeCancel:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavHideCompose, 0);
            break;
        case kIdComposeSubmit:
            ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavSubmitCompose, 0);
            break;
        case kIdComposeModal:
        {
            // 新建公告弹窗的目标选择器请求拉取某企业的组织树。
            if (m_composeModal == nullptr)
            {
                break;
            }
            const CString entId = m_composeModal->GetPendingTreeEnterpriseId();
            if (!entId.IsEmpty())
            {
                m_composeTreeEntId = entId;
                HttpRequest request = adminapi::MakeDeptTreeRequest(
                    AppConfig::Instance().Server(),
                    AdminSession::Instance().Token(), _ttoi64(entId));
                request.m_replyHwnd = m_hWnd;
                request.m_replyMsg  = WM_APP_API_RESULT;
                request.m_tag       = kApiTagComposeTree;
                HttpWorker::Instance().Post(request);
            }
            break;
        }

        case kIdAuditRefresh:
            StartLoadAudit();
            StartLoadAuditStats();
            break;
        case kIdAuditExport:
            StartAuditExport();
            break;
        case kIdAuditScreen:
            // 审计界面上报：切换筛选 / 翻页 → 按新状态重拉当前页。统计卡为
            // 全局口径、不随筛选翻页变化，故此处不重拉统计。
            StartLoadAudit();
            break;
        case kIdSettingsSaveProfile:
            SubmitUpdateProfile();
            break;
        case kIdSettingsChangePw:
            SubmitChangePassword();
            break;

        default:
            bHandled = FALSE;
            break;
        }
        return 0;
    }

    // ---- 状态变化类通知 ----
    if (n->code == DUIN_VALUECHANGED)
    {
        switch (n->ctrlId)
        {
        case kIdEntSearch:
            // 搜索框文字变化 → 企业管理界面重做筛选（仅刷新数据，安全同步）。
            if (m_enterprises != nullptr)
            {
                m_enterprises->OnSearchChanged();
            }
            break;
        case kIdOrgSearch:
            // 企业详情组织树搜索框文字变化 → 延后一拍再过滤组织树。这一做法源于
            // 早先搜索框内嵌真 Win32 输入框子窗口的时期:在文本变化通知的同一周期
            // 里同步过滤会触发树重绘,把刚输入的文字盖掉(须再点一下才恢复)。搜索框
            // 改为无窗口控件之后该现象不再出现,仍保留 PostMessage 延迟,以免在通知
            // 派发过程中改动控件树(见 WM_APP_ORG_FILTER 说明)。
            ::PostMessage(m_hWnd, WM_APP_ORG_FILTER, 0, 0);
            break;
        case kIdEntSeg:
            if (m_enterprises != nullptr)
            {
                m_enterprises->OnSegmentChanged(static_cast<int>(n->extra));
            }
            break;
        //企业详情屏切换了子 tab（extra 为子 tab 序号）：「在线用户」tab 的内容是
        //FlamingoLink.dll 的子窗口，显示与否要重新决定；切到「功能开关」tab 时读取该企业的设置
        case kIdDetailScreen:
            SyncLinkPage();
            if (static_cast<int>(n->extra) == EnterpriseDetailScreen::kTabFeatures)
            {
                LoadFeaturesPage();
            }
            break;
        //文件共享页的「文件共享 / 共享审计」页签切换，extra 为新页签下标
        case kIdFsTabs:
            if (m_fileShareCenter != nullptr)
            {
                m_fileShareCenter->OnTabChanged(static_cast<int>(n->extra));
            }
            break;
        //「更多筛选」条的行业 / 套餐下拉框选择变化、起止日期输入框文字变化
        case kIdEntMoreIndustry:
        case kIdEntMorePlan:
        case kIdEntMoreFrom:
        case kIdEntMoreTo:
            if (m_enterprises != nullptr)
            {
                m_enterprises->OnMoreFilterChanged();
            }
            break;
        //敏感词界面的「词库 / 拦截记录」页签切换，extra 为新页签下标
        case kIdSwTabs:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnTabChanged(static_cast<int>(n->extra));
            }
            break;
        //企业问答界面的企业选择框（仅平台超管），extra 为选中项下标
        case kIdQaEnterprise:
            if (m_qaScreen != nullptr)
            {
                m_qaScreen->OnEnterpriseSelected(static_cast<int>(n->extra));
            }
            break;
        //企业问答界面的搜索框：本地筛选
        case kIdQaSearch:
            if (m_qaScreen != nullptr)
            {
                m_qaScreen->OnSearchChanged();
            }
            break;
        //企业问答弹窗的应用范围下拉框：群列表只在「所选的群」时可用
        case kIdQaModalScope:
            if (m_qaModal != nullptr)
            {
                m_qaModal->OnScopeChanged();
            }
            break;
        //机器人界面的企业选择框（仅平台超管），extra 为选中项下标
        case kIdBotEnterprise:
            if (m_botScreen != nullptr)
            {
                m_botScreen->OnEnterpriseSelected(static_cast<int>(n->extra));
            }
            break;
        //机器人编辑窗的应用范围下拉框：群列表只在「所选的群」时可用
        case kIdBotModalScope:
            if (m_botModal != nullptr)
            {
                m_botModal->OnScopeChanged();
            }
            break;
        //机器人编辑窗的规则列表选中行变化：刷新规则按钮是否可用
        case kIdBotModalRules:
            if (m_botModal != nullptr)
            {
                m_botModal->OnRuleSelectionChanged();
            }
            break;
        //规则编辑窗的触发方式：重填动作并显隐相关控件
        case kIdBotRuleTrigger:
            if (m_botRuleModal != nullptr)
            {
                m_botRuleModal->OnTriggerChanged();
            }
            break;
        //规则编辑窗的动作：显隐「按 Markdown 显示」与外部服务地址
        case kIdBotRuleAction:
            if (m_botRuleModal != nullptr)
            {
                m_botRuleModal->OnActionChanged();
            }
            break;
        //规则编辑窗的定时种类：显隐星期与几号
        case kIdBotRuleSchedKind:
            if (m_botRuleModal != nullptr)
            {
                m_botRuleModal->OnScheduleKindChanged();
            }
            break;
        //规则编辑窗的「只在以下时间段响应」：时间段控件随之可用或不可用
        case kIdBotRuleWindowOn:
            if (m_botRuleModal != nullptr)
            {
                m_botRuleModal->OnWindowToggled();
            }
            break;
        //企业详情「功能开关」页里的十三个开关之一被拨动，extra 为拨动后的状态（1 开 / 0 关）：交给它所在的面板弹确认框
        case kIdFeatSwitchRegister:
        case kIdFeatSwitchInviteConfirm:
        case kIdFeatSwitchSecretChat:
        case kIdFeatSwitchThreadGroup:
        case kIdFeatSwitchFileShare:
        case kIdFeatSwitchP2P:
        case kIdFeatSwitchGroupCall:
        case kIdFeatSwitchCaption:
        case kIdFeatSwitchCloudRecord:
        case kIdFeatSwitchBot:
        case kIdFeatSwitchBotGroup:
        case kIdFeatSwitchBotPersonal:
        case kIdFeatSwitchBotGroupExternal:
            if (m_enterpriseDetail != nullptr && m_enterpriseDetail->GetFeaturesPage() != nullptr)
            {
                m_enterpriseDetail->GetFeaturesPage()->OnSwitchToggled(n->ctrlId, n->extra != 0);
            }
            break;
        //敏感词界面的企业选择框（仅平台超管），extra 为选中项下标
        case kIdSwEnterprise:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnEnterpriseSelected(static_cast<int>(n->extra));
            }
            break;
        //敏感词界面的搜索框文字变化：界面自己做防抖
        case kIdSwSearch:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnSearchChanged();
            }
            break;
        //敏感词界面的分类筛选，extra 为选中项下标
        case kIdSwCategory:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnCategoryChanged(static_cast<int>(n->extra));
            }
            break;
        //敏感词过滤总开关被用户拨动，extra 为拨动后的状态（1 开 / 0 关）
        case kIdSwSwitch:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnSwitchToggled(n->extra != 0);
            }
            break;
        //拦截记录的时间范围，extra 为选中项下标
        case kIdSwHitRange:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnHitRangeChanged(static_cast<int>(n->extra));
            }
            break;
        //拦截记录的发送者搜索框文字变化：界面自己做防抖
        case kIdSwHitSearch:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnHitSearchChanged();
            }
            break;
        //拦截记录的来源筛选，extra 为选中项下标
        case kIdSwHitSource:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnHitSourceChanged(static_cast<int>(n->extra));
            }
            break;
        //拦截记录的分类筛选，extra 为选中项下标
        case kIdSwHitCategory:
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnHitCategoryChanged(static_cast<int>(n->extra));
            }
            break;
        //「文件共享」页的企业筛选（仅平台超管），extra 为选中项下标，0 为全部企业
        case kIdFsEnterprise:
            if (m_fileSharesScreen != nullptr)
            {
                m_fileSharesScreen->OnEnterpriseSelected(static_cast<int>(n->extra));
            }
            break;
        //「文件共享」页的分享者搜索框文字变化：界面自己做防抖
        case kIdFsSearch:
            if (m_fileSharesScreen != nullptr)
            {
                m_fileSharesScreen->OnSearchChanged();
            }
            break;
        //「文件共享」页的状态筛选，extra 为选中项下标
        case kIdFsState:
            if (m_fileSharesScreen != nullptr)
            {
                m_fileSharesScreen->OnStateChanged(static_cast<int>(n->extra));
            }
            break;
        //「文件共享」页的表格选中行变化，extra 为行下标
        case kIdFsTable:
            if (m_fileSharesScreen != nullptr)
            {
                m_fileSharesScreen->OnRowSelected(static_cast<int>(n->extra));
            }
            break;
        //「共享审计」页的企业筛选（仅平台超管），extra 为选中项下标，0 为全部企业
        case kIdFaEnterprise:
            if (m_fileShareAuditScreen != nullptr)
            {
                m_fileShareAuditScreen->OnEnterpriseSelected(static_cast<int>(n->extra));
            }
            break;
        //「共享审计」页的时间范围，extra 为选中项下标
        case kIdFaRange:
            if (m_fileShareAuditScreen != nullptr)
            {
                m_fileShareAuditScreen->OnRangeChanged(static_cast<int>(n->extra));
            }
            break;
        //「共享审计」页的操作人搜索框文字变化：界面自己做防抖
        case kIdFaSearch:
            if (m_fileShareAuditScreen != nullptr)
            {
                m_fileShareAuditScreen->OnSearchChanged();
            }
            break;
        //「共享审计」页的动作筛选，extra 为选中项下标，0 为全部动作
        case kIdFaAction:
            if (m_fileShareAuditScreen != nullptr)
            {
                m_fileShareAuditScreen->OnActionChanged(static_cast<int>(n->extra));
            }
            break;
        //「共享审计」页的共享 ID 输入框文字变化：界面自己做防抖
        case kIdFaShareId:
            if (m_fileShareAuditScreen != nullptr)
            {
                m_fileShareAuditScreen->OnShareIdChanged();
            }
            break;
        //「共享审计」页的表格选中行变化，extra 为行下标
        case kIdFaTable:
            if (m_fileShareAuditScreen != nullptr)
            {
                m_fileShareAuditScreen->OnRowSelected(static_cast<int>(n->extra));
            }
            break;
        default:
            bHandled = FALSE;
            break;
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

LRESULT AdminFrame::OnNavigate(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
{
    // 此处已脱离 DUI 事件派发栈，可安全变动控件树 / 可见性。
    bHandled = TRUE;
    if (wParam == kNavLogout)
    {
        HandleLogout();
    }
    else if (wParam == kNavShowModal)
    {
        ShowNewEnterpriseModal();
    }
    else if (wParam == kNavHideModal)
    {
        HideNewEnterpriseModal();
    }
    else if (wParam == kNavSubmitModal)
    {
        SubmitNewEnterprise();
    }
    else if (wParam == kNavShowNodeModal)
    {
        ShowNodeModal();
    }
    else if (wParam == kNavHideNodeModal)
    {
        HideNodeModal();
    }
    else if (wParam == kNavShowGroupModal)
    {
        ShowGroupModal();
    }
    else if (wParam == kNavHideGroupModal)
    {
        HideGroupModal();
    }
    else if (wParam == kNavSubmitGroupModal)
    {
        SubmitGroupModal();
    }
    else if (wParam == kNavShowCvModal)
    {
        // 先扫一次升级包目录，拿到候选包再开弹窗（见 OnPackagesScanned）
        StartScanPackages();
    }
    else if (wParam == kNavHideCvModal)
    {
        HideClientVersionModal();
    }
    else if (wParam == kNavSubmitCvDraft)
    {
        SubmitClientVersionModal(false);
    }
    else if (wParam == kNavSubmitCvPublish)
    {
        SubmitClientVersionModal(true);
    }
    else if (wParam == kNavRescanPackages)
    {
        StartScanPackages();
    }
    else if (wParam == kNavSwUiRequests)
    {
        HandleSensitiveUiRequests();
    }
    else if (wParam == kNavSwHideModal)
    {
        HideSensitiveWordModal();
    }
    else if (wParam == kNavSwSubmitModal)
    {
        SubmitSensitiveWordModal();
    }
    else if (wParam == kNavSwHideImport)
    {
        HideSensitiveImportModal();
    }
    else if (wParam == kNavSwSubmitImport)
    {
        SubmitSensitiveImportModal();
    }
    else if (wParam == kNavSwImportReadFile)
    {
        ReadSensitiveImportFile();
    }
    else if (wParam == kNavFsUiRequests)
    {
        HandleFileShareUiRequests();
    }
    else if (wParam == kNavQaUiRequests)
    {
        HandleQaUiRequests();
    }
    else if (wParam == kNavQaHideModal)
    {
        HideQaModal();
    }
    else if (wParam == kNavQaSubmitModal)
    {
        SubmitQaModal();
    }
    else if (wParam == kNavBotUiRequests)
    {
        HandleBotUiRequests();
    }
    else if (wParam == kNavBotHideModal)
    {
        HideBotModal();
    }
    else if (wParam == kNavBotSubmitModal)
    {
        SubmitBotModal();
    }
    else if (wParam == kNavBotAddRule)
    {
        OpenBotRuleModal(true);
    }
    else if (wParam == kNavBotEditRule)
    {
        OpenBotRuleModal(false);
    }
    else if (wParam == kNavBotCloseRule)
    {
        CloseBotRuleModal();
    }
    else if (wParam == kNavBotSubmitRule)
    {
        SubmitBotRuleModal();
    }
    else if (wParam == kNavBotHideDomains)
    {
        HideBotDomainsModal();
    }
    else if (wParam == kNavBotSubmitDomains)
    {
        SubmitBotDomainsModal();
    }
    // kNavShowConfirm / kNavHideConfirm / kNavConfirmOk 历史 navigate 分支
    // 在此处理,ConfirmDialog 改独立顶层窗口后,按钮事件 dialog 自己接管。
    else if (wParam == kNavShowServiceModal)
    {
        ShowServiceModal();
    }
    else if (wParam == kNavHideServiceModal)
    {
        HideServiceModal();
    }
    else if (wParam == kNavSubmitServiceModal)
    {
        SubmitServiceModal();
    }
    else if (wParam == kNavToggleService)
    {
        ToggleServiceStatus();
    }
    else if (wParam == kNavShowBindingModal)
    {
        ShowBindingModal();
    }
    else if (wParam == kNavHideBindingModal)
    {
        HideBindingModal();
    }
    else if (wParam == kNavSaveBindingModal)
    {
        SaveBindingModal();
    }
    else if (wParam == kNavShowCompose)
    {
        ShowComposeModal();
    }
    else if (wParam == kNavHideCompose)
    {
        HideComposeModal();
    }
    else if (wParam == kNavSubmitCompose)
    {
        SubmitComposeModal();
    }
    else if (wParam == kNavExportAnnouncements)
    {
        ExportAnnouncementsCsv();
    }
    else if (wParam == kNavToggleSidebar)
    {
        ToggleSidebar();
    }
    else if (wParam == kNavShowNetwork)
    {
        ShowNetworkSettings();
    }
    else if (wParam == kNavBackToLogin)
    {
        BackToLoginFromNetwork();
    }
    else if (wParam == kNavSaveNetwork)
    {
        SaveNetworkSettings();
    }
    else if (wParam == kNavShowEntMenu)
    {
        ShowEnterpriseMenu();
    }
    else if (wParam == kNavShowDeptMenu)
    {
        ShowDeptPickerMenu();
    }
    else if (wParam == kNavHideEntSettings)
    {
        HideEnterpriseSettings();
    }
    else if (wParam >= kNavScreenBase)
    {
        SwitchScreen(static_cast<Screen>(wParam - kNavScreenBase));
    }
    return 0;
}

LRESULT AdminFrame::OnNetTestResult(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
{
    bHandled = TRUE;
    m_netTestBusy = false;
    if (m_networkScreen == nullptr)
    {
        return 0;
    }
    const ProbeResult r = static_cast<ProbeResult>(wParam);
    switch (r)
    {
    case ProbeResult::Ok:
        m_networkScreen->SetStatus(_T("连接成功 · 服务器可达"),
                                   theme::kColorOk);
        break;
    case ProbeResult::Refused:
        m_networkScreen->SetStatus(_T("连接失败 · 服务器拒绝连接或地址有误"),
                                   theme::kColorDanger);
        break;
    case ProbeResult::Timeout:
        m_networkScreen->SetStatus(_T("连接超时 · 服务器无响应"),
                                   theme::kColorDanger);
        break;
    case ProbeResult::DnsFail:
    default:
        m_networkScreen->SetStatus(_T("无法解析该主机名"),
                                   theme::kColorDanger);
        break;
    }
    FA_LOGI(_T("网络测试结果：%d"), static_cast<int>(r));
    return 0;
}

LRESULT AdminFrame::OnLoginResult(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    m_loginBusy = false;

    // 接管工作线程 new 出来的 HttpResult，函数结束时由 guard 自动 delete。
    HttpResult* http = reinterpret_cast<HttpResult*>(lParam);
    if (http == nullptr)
        return 0;
    std::unique_ptr<HttpResult> guard(http);

    // 在 UI 线程解析应答（JSON 解析仅微秒级，不会卡界面）。
    const LoginResult login = adminapi::ParseLoginResponse(*http);
    if (login.Succeeded())
    {
        // 保存会话信息，供后续所有 admin 接口取 token 使用。
        AdminSession::Instance().Set(login.m_token, login.m_adminId,
                                     login.m_adminName, login.m_adminEmail,
                                     login.m_enterpriseId);
        FA_LOGI(_T("登录成功：adminid=%lld name=%s 范围=%s"),
                login.m_adminId, login.m_adminName.GetString(),
                AdminSession::Instance().IsPlatformAdmin()
                    ? _T("平台超管") : _T("企业管理员"));

        // 记住本次登录：账号总是记（下次打开自动填好），口令仅在用户勾了
        // "记住密码"时才记，且落盘前经 DPAPI 加密（见 Util/DpapiSecret.h）。
        // 只在登录成功这一支保存 —— 密码错了还给它存下来，下次打开又是错的。
        LoginConfig toSave = m_pendingLogin;
        if (!toSave.m_rememberPassword)
        {
            toSave.m_password.Empty();
        }
        AppConfig::Instance().SetLogin(toSave);
        AppConfig::Instance().Save();

        // 建立到管理端口的长连接（FlamingoLink.dll）：主机沿用 HTTP 的主机，端口与公钥取登录应答。
        // DLL 不可用时什么都不做；服务器没下发端口时 DLL 不连接，页面提示服务器未开启。
        LinkBridge::Instance().Start(AppConfig::Instance().Server().m_host, login.m_linkPort,
                                     login.m_linkPubkey, login.m_token);

        EnterShell();
    }
    else
    {
        // 失败：用中文提示告知用户，停留在登录界面。
        ShowToast(login.m_message, ToastKind::Error);
        FA_LOGW(_T("登录失败：%s"), login.m_message.GetString());
    }
    return 0;
}

LRESULT AdminFrame::OnDeleteEmpEditDialog(UINT, WPARAM wParam, LPARAM,
                                          BOOL& bHandled)
{
    bHandled = TRUE;
    EmployeeEditDialog* dlg = reinterpret_cast<EmployeeEditDialog*>(wParam);
    delete dlg;
    return 0;
}

LRESULT AdminFrame::OnDeleteConfirmDialog(UINT, WPARAM wParam, LPARAM,
                                          BOOL& bHandled)
{
    bHandled = TRUE;
    ConfirmDialog* dlg = reinterpret_cast<ConfirmDialog*>(wParam);
    delete dlg;
    return 0;
}

LRESULT AdminFrame::OnDeleteNodeModal(UINT, WPARAM wParam, LPARAM,
                                      BOOL& bHandled)
{
    bHandled = TRUE;
    NodeModal* dlg = reinterpret_cast<NodeModal*>(wParam);
    delete dlg;
    return 0;
}

LRESULT AdminFrame::OnOrgFilter(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    // 延迟执行的组织树过滤:读企业详情屏搜索框当前文本,按部门名 / 员工姓名
    // 过滤组织树。延后一拍执行(见 WM_APP_ORG_FILTER 说明),避免盖掉搜索框
    // 刚输入的文字。
    bHandled = TRUE;
    if (m_enterpriseDetail != nullptr)
    {
        m_enterpriseDetail->OnOrgSearchChanged();
    }
    return 0;
}

LRESULT AdminFrame::OnApiResult(UINT, WPARAM wParam, LPARAM lParam,
                                BOOL& bHandled)
{
    bHandled = TRUE;

    // 接管工作线程 new 出来的 HttpResult，函数结束时由 guard 自动 delete。
    HttpResult* http = reinterpret_cast<HttpResult*>(lParam);
    if (http == nullptr)
    {
        return 0;
    }
    std::unique_ptr<HttpResult> guard(http);
    const ApiTag tag = static_cast<ApiTag>(wParam);

    // 集中处理会话失效：任一业务接口返回 code=2，都清会话、退回登录页。
    if (adminapi::PeekBusinessCode(*http) == kErrCodeSessionExpired)
    {
        HandleSessionExpired();
        return 0;
    }

    // 按标签把结果派发到对应处理逻辑。企业列表（读）交由 EnterprisesScreen
    // 自己消化；企业写操作（涉及弹窗 / 文件对话框 / 列表刷新）由本窗口处理。
    switch (tag)
    {
    case kApiTagEntList:
    case kApiTagEntStats:
        if (m_enterprises != nullptr)
        {
            m_enterprises->OnApiResult(tag, *http);
        }
        break;
    //企业问答界面的全部接口：交给界面自己处理
    case kApiTagQaList:
    case kApiTagQaSave:
    case kApiTagQaDelete:
    case kApiTagQaGroups:
    case kApiTagQaEnterprises:
        if (m_qaScreen != nullptr)
        {
            m_qaScreen->OnApiResult(tag, *http);
        }
        break;
    //机器人界面的全部接口：交给界面自己处理
    case kApiTagBotList:
    case kApiTagBotDetail:
    case kApiTagBotSave:
    case kApiTagBotDelete:
    case kApiTagBotSetStatus:
    case kApiTagBotGroups:
    case kApiTagBotTry:
    case kApiTagBotResetToken:
    case kApiTagBotDomains:
    case kApiTagBotSetDomains:
    case kApiTagBotEnterprises:
        if (m_botScreen != nullptr)
        {
            m_botScreen->OnApiResult(tag, *http);
        }
        break;
    //敏感词界面的全部接口：交给界面自己处理
    case kApiTagSwList:
    case kApiTagSwAdd:
    case kApiTagSwUpdate:
    case kApiTagSwDelete:
    case kApiTagSwImport:
    case kApiTagSwExport:
    case kApiTagSwSetEnabled:
    case kApiTagSwHits:
    case kApiTagSwEnterprises:
        if (m_sensitiveWordsScreen != nullptr)
        {
            m_sensitiveWordsScreen->OnApiResult(tag, *http);
        }
        break;
    //企业详情「功能开关」页里十三个开关与云端录像空间面板读 / 改设置的应答：按标签交给对应的面板
    case kApiTagRegisterSwitchGet:
    case kApiTagRegisterSwitchSet:
    case kApiTagInviteConfirmGet:
    case kApiTagInviteConfirmSet:
    case kApiTagSecretChatSwitchGet:
    case kApiTagSecretChatSwitchSet:
    case kApiTagThreadGroupSwitchGet:
    case kApiTagThreadGroupSwitchSet:
    case kApiTagFeatureGet:
    case kApiTagFeatureSet:
    case kApiTagP2PSwitchGet:
    case kApiTagP2PSwitchSet:
    case kApiTagGroupCallSwitchGet:
    case kApiTagGroupCallSwitchSet:
    case kApiTagCaptionSwitchGet:
    case kApiTagCaptionSwitchSet:
    case kApiTagCloudRecordSwitchGet:
    case kApiTagCloudRecordSwitchSet:
    case kApiTagCloudQuotaGet:
    case kApiTagCloudQuotaSet:
    case kApiTagBotSwitchGet:
    case kApiTagBotSwitchSet:
    case kApiTagBotGroupSwitchGet:
    case kApiTagBotGroupSwitchSet:
    case kApiTagBotPersonalSwitchGet:
    case kApiTagBotPersonalSwitchSet:
    case kApiTagBotExternalSwitchGet:
    case kApiTagBotExternalSwitchSet:
        if (m_enterpriseDetail != nullptr && m_enterpriseDetail->GetFeaturesPage() != nullptr)
        {
            m_enterpriseDetail->GetFeaturesPage()->OnApiResult(tag, *http);
        }
        break;
    //「文件共享」页的全部接口：交给界面自己处理
    case kApiTagFsList:
    case kApiTagFsMembers:
    case kApiTagFsCancel:
    case kApiTagFsEnterprises:
        if (m_fileSharesScreen != nullptr)
        {
            m_fileSharesScreen->OnApiResult(tag, *http);
        }
        break;
    //「共享审计」页的全部接口：交给界面自己处理
    case kApiTagFsAudit:
    case kApiTagFsAuditEnterprises:
        if (m_fileShareAuditScreen != nullptr)
        {
            m_fileShareAuditScreen->OnApiResult(tag, *http);
        }
        break;
    case kApiTagDashStats:
        OnDashboardStatsLoaded(*http);
        break;
    case kApiTagDashEnterprises:
        OnDashboardEnterprisesLoaded(*http);
        break;
    case kApiTagDashAnnouncements:
        OnDashboardAnnouncementsLoaded(*http);
        break;
    case kApiTagDashAudit:
        OnDashboardAuditLoaded(*http);
        break;
    case kApiTagDashTrend:
        OnDashboardTrendLoaded(*http);
        break;
    case kApiTagAuditList:
        OnAuditListLoaded(*http);
        break;
    case kApiTagAuditStats:
        OnAuditStatsLoaded(*http);
        break;
    case kApiTagAuditExport:
        OnAuditExported(*http);
        break;
    case kApiTagProfile:
        OnProfileLoaded(*http);
        break;
    case kApiTagUpdateProfile:
        OnProfileUpdated(*http);
        break;
    case kApiTagChangePassword:
        OnPasswordChanged(*http);
        break;
    case kApiTagAnnList:
        OnAnnouncementsLoaded(*http);
        break;
    case kApiTagAnnDetail:
        OnAnnouncementDetailLoaded(*http);
        break;
    case kApiTagAnnRecall:
        OnAnnouncementActionDone(*http, _T("公告已撤回"));
        break;
    case kApiTagAnnDelete:
        OnAnnouncementActionDone(*http, _T("公告已删除"));
        break;
    case kApiTagAnnSetPinned:
        OnAnnouncementActionDone(*http, _T("置顶状态已更新"));
        break;
    case kApiTagAnnSave:
        OnAnnSaved(*http);
        break;
    case kApiTagAnnSend:
        OnAnnSent(*http);
        break;
    case kApiTagComposeTree:
        OnComposeTreeLoaded(*http);
        break;
    case kApiTagAnnEditFetch:
        OnAnnEditFetchLoaded(*http);
        break;
    //公告中心「复制」拉取的原公告详情：按其内容打开新建公告
    case kApiTagAnnCopyFetch:
        OnAnnCopyFetchLoaded(*http);
        break;
    //公告中心「重新推送」的应答：提示推送人数
    case kApiTagAnnRepush:
        OnAnnRepushed(*http);
        break;
    case kApiTagEntCreate:
        OnEnterpriseCreated(*http);
        break;
    case kApiTagEntSetStatus:
        OnEnterpriseActionDone(*http, _T("企业状态已更新"));
        break;
    case kApiTagEntDelete:
        OnEnterpriseActionDone(*http, _T("企业已删除"));
        break;
    case kApiTagEntExport:
        OnEnterpriseExported(*http);
        break;
    case kApiTagEntDetail:
    case kApiTagDeptTree:
    case kApiTagSvcList:
        OnDetailPartLoaded(tag, *http);
        break;
    case kApiTagDeptCreate:
        OnDeptCreated(*http);
        break;
    case kApiTagUserCreate:
        OnUserCreated(*http);
        break;
    case kApiTagGroupCreate:
        OnGroupCreated(*http);
        break;
    case kApiTagCvList:
        OnClientVersionsLoaded(*http);
        break;
    case kApiTagCvScanPackages:
        OnPackagesScanned(*http);
        break;
    case kApiTagCvCreate:
    case kApiTagCvUpdate:
    case kApiTagCvSetEnabled:
        OnClientVersionActionDone(*http);
        break;
    case kApiTagOrgNodeDelete:
        OnOrgNodeDeleted(*http);
        break;
    case kApiTagOrgTreeReload:
        OnOrgTreeReloaded(*http);
        break;
    case kApiTagSvcSave:
        OnServiceSaved(*http);
        break;
    case kApiTagSvcSetStatus:
        OnServiceActionDone(*http, _T("服务号状态已更新"));
        break;
    case kApiTagSvcDelete:
        OnServiceActionDone(*http, _T("服务号已删除"));
        break;
    case kApiTagSvcSetBindings:
        OnServiceBindingsSaved(*http);
        break;
    case kApiTagSvcListReload:
        OnServiceListReloaded(*http);
        break;
    //顶栏「最新审计」滚动提示单独拉取的最近几条审计
    case kApiTagTickerAudit:
        OnTickerAuditLoaded(*http);
        break;
    //服务号面板切换选中项后读取的绑定范围（只为显示）
    case kApiTagSvcDetail:
        OnServiceDetailLoaded(*http, false);
        break;
    //点「修改绑定」后读取的当前绑定：读到才打开绑定弹窗
    case kApiTagSvcDetailForBinding:
        OnServiceDetailLoaded(*http, true);
        break;
    case kApiTagUserGetPerms:
        OnUserPermissionsLoaded(*http);
        break;
    case kApiTagUserSetRole:
        OnUserRoleSet(*http);
        break;
    case kApiTagUserSetPerms:
        OnUserPermsSet(*http);
        break;
    case kApiTagUserLoginRecords:
        OnUserLoginRecordsLoaded(*http);
        break;
    case kApiTagUserActivity:
        OnUserActivityLoaded(*http);
        break;
    case kApiTagUserMsgStats:
        OnUserMessageStatsLoaded(*http);
        break;
    case kApiTagUserSetStatus:
        OnUserStatusSet(*http);
        break;
    case kApiTagUserResetPwd:
        OnUserPasswordReset(*http);
        break;
    case kApiTagUserKickDevice:
        OnUserDevicesKicked(*http);
        break;
    case kApiTagUserDetail:
        OnUserDetailLoaded(*http);
        break;
    case kApiTagUserUpdate:
        OnUserUpdated(*http);
        break;
    case kApiTagUserMove:
        OnUserMoved(*http);
        break;
    case kApiTagEntAdmins:
        OnEnterpriseAdminsLoaded(*http);
        break;
    case kApiTagEntUpdate:
        OnEnterpriseUpdated(*http);
        break;
    case kApiTagEntAddAdmin:
        OnEnterpriseAdminAdded(*http);
        break;
    case kApiTagEntRemoveAdmin:
        OnEnterpriseAdminRemoved(*http);
        break;
    default:
        break;
    }
    return 0;
}

LRESULT AdminFrame::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
    // 默认不拦截左键 —— 列表选中、双击等仍交由 DUI 正常处理。本处理只附加
    // 一件事：在企业管理界面点中某行「操作」列的三个点时，弹出行操作菜单。
    //（双击进详情已改由 balloonui 双击合成上报 DUIN_DBLCLK，见 OnDuiNotify。）
    bHandled = FALSE;

    if (!m_loggedIn || m_route != Screen::Enterprises || m_enterprises == nullptr)
    {
        return 0;
    }

    // WM_LBUTTONDOWN 的 lParam 为客户区坐标；DuiFrameWindow 的 DUI 内容铺满
    // 整个客户区，故该坐标即 host-客户区坐标，可直接用于命中测试。
    const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (!m_enterprises->HitTestActionDotsAt(pt))
    {
        return 0;   // 没点在「操作」列的三个点上
    }

    // 命中三个点：菜单在光标处弹出。延迟到本消息处理结束后再弹，确保此时
    // 左键已松开，菜单不会被同一次操作的抬起事件立即关掉。拦下本消息
    //（bHandled=TRUE）不再下传 DUI，避免点三个点又触发行选中 / 双击合成。
    POINT screenPt = pt;
    ::ClientToScreen(m_hWnd, &screenPt);
    m_entMenuPt = screenPt;
    ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowEntMenu, 0);
    bHandled = TRUE;
    return 0;
}

void AdminFrame::ToggleDebugConsole()
{
    // 调试控制台窗口是单例，懒创建与显隐切换都由 Toggle 内部完成。
    flamingoadmin::DebugConsoleWindow::Instance().Toggle(m_hWnd);
    FA_LOGI(_T("切换调试控制台窗口"));
}

void AdminFrame::EnterShell()
{
    m_loggedIn = true;

    // 首次登录时构建主界面外壳并常驻；之后登录直接复用。
    if (m_shellPage == nullptr)
    {
        auto sidebar = std::make_unique<Sidebar>();
        m_sidebar = sidebar.get();

        auto topbar = std::make_unique<TopBar>();
        m_topBar = topbar.get();

        // 内容区屏宿主：装入全部 5 个主界面，全部常驻。
        auto screenHost = std::make_unique<ScreenHost>();
        m_screenHost = screenHost.get();

        auto dashboard = std::make_unique<DashboardScreen>();
        m_dashboard = dashboard.get();
        m_screens[static_cast<int>(Screen::Dashboard)] = dashboard.get();
        m_screenHost->AddScreen(std::move(dashboard));

        auto enterprises = std::make_unique<EnterprisesScreen>();
        m_enterprises = enterprises.get();
        // 注入主窗口句柄：企业管理界面据此把联网结果回传到本窗口。
        m_enterprises->SetHostWindow(m_hWnd);
        m_screens[static_cast<int>(Screen::Enterprises)] = enterprises.get();
        m_screenHost->AddScreen(std::move(enterprises));

        // 企业详情屏（企业管理的子屏，点企业行进入）。
        auto detail = std::make_unique<EnterpriseDetailScreen>();
        m_enterpriseDetail = detail.get();
        m_screens[static_cast<int>(Screen::EnterpriseDetail)] = detail.get();
        m_screenHost->AddScreen(std::move(detail));

        // 企业详情的「功能开关」页：各面板不直接接触网络与对话框，请求出口与界面请求都接到本窗口
        m_enterpriseDetail->GetFeaturesPage()->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
        m_enterpriseDetail->GetFeaturesPage()->SetUiRequestHandler(&AdminFrame::FileShareUiRequestThunk, this);

        // FlamingoLink.dll 可用时：企业详情屏多一个「在线用户」子 tab，DLL 的页面窗口建为本窗口的
        // 子窗口（初始隐藏），占位控件排版时把位置转交 DLL。
        if (LinkBridge::Instance().IsAvailable())
        {
            m_enterpriseDetail->GetOnlineSlot()->SetBoundsCallback(&AdminFrame::LinkSlotBoundsThunk, this);
            LinkBridge::Instance().CreatePage(m_hWnd);
            m_enterpriseDetail->SetOnlineTabAvailable(true);
        }

        // 公告中心（真实界面）。
        auto annScreen = std::make_unique<AnnouncementsScreen>();
        m_announcementsScreen = annScreen.get();
        m_screens[static_cast<int>(Screen::Announcements)] = annScreen.get();
        m_screenHost->AddScreen(std::move(annScreen));

        // 审计日志（真实界面）。
        auto auditScreen = std::make_unique<AuditScreen>();
        m_auditScreen = auditScreen.get();
        m_screens[static_cast<int>(Screen::Audit)] = auditScreen.get();
        m_screenHost->AddScreen(std::move(auditScreen));

        // 平台设置（真实界面）。
        auto settingsScreen = std::make_unique<SettingsScreen>();
        m_settingsScreen = settingsScreen.get();
        m_screens[static_cast<int>(Screen::Settings)] = settingsScreen.get();
        m_screenHost->AddScreen(std::move(settingsScreen));

        // 客户端版本（真实界面）。企业级管理员看不到入口，但屏本身照建 ——
        // m_screens 按 Screen 枚举下标寻址，留空会让下标错位。
        auto cvScreen = std::make_unique<ClientVersionsScreen>();
        m_clientVersionsScreen = cvScreen.get();
        m_screens[static_cast<int>(Screen::ClientVersions)] = cvScreen.get();
        m_screenHost->AddScreen(std::move(cvScreen));

        // 敏感词：界面本身不接触网络与对话框，请求出口与界面请求都接到本窗口；
        // 主窗口句柄用于挂搜索防抖定时器。
        auto swScreen = std::make_unique<SensitiveWordsScreen>();
        m_sensitiveWordsScreen = swScreen.get();
        m_sensitiveWordsScreen->SetHostWindow(m_hWnd);
        m_sensitiveWordsScreen->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
        m_sensitiveWordsScreen->SetUiRequestHandler(&AdminFrame::SensitiveUiRequestThunk, this);
        m_screens[static_cast<int>(Screen::SensitiveWords)] = swScreen.get();
        m_screenHost->AddScreen(std::move(swScreen));

        // 企业问答：与敏感词界面相同，请求出口与界面请求都接到本窗口。
        auto qaScreen = std::make_unique<EnterpriseQaScreen>();
        m_qaScreen = qaScreen.get();
        m_qaScreen->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
        m_qaScreen->SetUiRequestHandler(&AdminFrame::QaUiRequestThunk, this);
        m_screens[static_cast<int>(Screen::EnterpriseQa)] = qaScreen.get();
        m_screenHost->AddScreen(std::move(qaScreen));

        // 机器人：与企业问答相同，请求出口与界面请求都接到本窗口。
        std::unique_ptr<EnterpriseBotScreen> botScreen(new EnterpriseBotScreen());
        m_botScreen = botScreen.get();
        m_botScreen->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
        m_botScreen->SetUiRequestHandler(&AdminFrame::BotUiRequestThunk, this);
        m_screens[static_cast<int>(Screen::EnterpriseBot)] = botScreen.get();
        m_screenHost->AddScreen(std::move(botScreen));

        // 文件共享页（「文件共享」「共享审计」两个页签）：与敏感词界面相同，请求出口与界面请求都接到本窗口；
        // 主窗口句柄用于挂输入防抖定时器。两个页签的控件通知、定时器与接口应答仍直接交给各自的页签。
        std::unique_ptr<FileShareCenterScreen> fsCenter(new FileShareCenterScreen());
        m_fileShareCenter = fsCenter.get();
        m_fileShareCenter->SetHostWindow(m_hWnd);
        m_fileShareCenter->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
        m_fileShareCenter->SetUiRequestHandler(&AdminFrame::FileShareUiRequestThunk, this);
        m_fileSharesScreen = m_fileShareCenter->SharesPage();
        m_fileShareAuditScreen = m_fileShareCenter->AuditPage();
        // 「共享审计」页签没有自己的界面：两个路由指向同一个页面
        m_screens[static_cast<int>(Screen::FileShares)] = fsCenter.get();
        m_screens[static_cast<int>(Screen::FileShareAudit)] = fsCenter.get();
        m_screenHost->AddScreen(std::move(fsCenter));

        auto mainColumn = std::make_unique<DuiVBox>();
        mainColumn->AddChild(std::move(topbar),     Hint().Fixed(theme::kTopBarHeight));
        mainColumn->AddChild(std::move(screenHost), Hint().Weight(1));

        auto shell = std::make_unique<DuiHBox>();
        shell->AddChild(std::move(sidebar),    Hint().Fixed(theme::kSidebarWidth));
        shell->AddChild(std::move(mainColumn), Hint().Weight(1));

        m_shellPage = shell.get();
        m_root->AddPage(std::move(shell));
    }

    m_root->ShowPage(m_shellPage);

    // 外壳只在第一次登录时构建，换账号登录时身份与导航要按当前登录者重新刷新。
    const AdminSession& session = AdminSession::Instance();
    m_topBar->SetAccount(session.Name(), session.Email(), session.IsPlatformAdmin());
    m_sidebar->ApplyRole(session.IsPlatformAdmin());
    m_dashboard->SetPlatformAdminMode(session.IsPlatformAdmin());
    m_dashboard->SetGreeting(session.Name());
    m_enterpriseDetail->SetPlatformAdminMode(session.IsPlatformAdmin());
    FetchTickerAudits();

    // 默认进入仪表板。
    SwitchScreen(Screen::Dashboard);

    // 启动顶栏审计滚动提示定时器（登出时停掉）。
    SetTimer(kTickerTimerId, kTickerIntervalMs);

    FA_LOGI(_T("登录成功，进入主界面外壳"));
}

void AdminFrame::SwitchScreen(Screen screen)
{
    // 企业管理员只管辖一个企业，没有企业列表：侧栏「本企业」等一切通向企业列表的入口
    // 都改为加载本企业的详情，加载完成后由 OnDetailPartLoaded 切到企业详情屏。
    const bool platformAdmin = AdminSession::Instance().IsPlatformAdmin();
    if (screen == Screen::Enterprises && !platformAdmin)
    {
        CString ownEntId;
        ownEntId.Format(_T("%lld"), AdminSession::Instance().EnterpriseId());
        FA_LOGI(_T("enterprise admin opens own enterprise detail: %s"), ownEntId.GetString());
        StartLoadEnterpriseDetail(ownEntId);
        return;
    }

    m_route = screen;
    if (m_screenHost == nullptr)
    {
        return;
    }

    // 切换前刷新目标界面的数据，确保统计 / 列表反映最新状态。
    if (screen == Screen::Dashboard && m_dashboard != nullptr)
    {
        StartLoadDashboard();
    }
    else if (screen == Screen::Enterprises && m_enterprises != nullptr)
    {
        m_enterprises->Refresh();
    }
    else if (screen == Screen::Announcements && m_announcementsScreen != nullptr)
    {
        StartLoadAnnouncements();
    }
    else if (screen == Screen::Audit && m_auditScreen != nullptr)
    {
        StartLoadAudit();
        StartLoadAuditStats();
    }
    else if (screen == Screen::Settings && m_settingsScreen != nullptr)
    {
        // 功能开关已移到企业详情的「功能开关」子 tab（2026-10-08），这里只有个人资料与修改密码
        StartLoadProfile();
    }
    else if (screen == Screen::ClientVersions && m_clientVersionsScreen != nullptr)
    {
        StartLoadClientVersions();
    }
    else if (screen == Screen::SensitiveWords && m_sensitiveWordsScreen != nullptr)
    {
        m_sensitiveWordsScreen->OnEnter();
    }
    else if (screen == Screen::EnterpriseQa && m_qaScreen != nullptr)
    {
        m_qaScreen->OnEnter();
    }
    else if (screen == Screen::EnterpriseBot && m_botScreen != nullptr)
    {
        m_botScreen->OnEnter();
    }
    else if (screen == Screen::FileShares && m_fileShareCenter != nullptr)
    {
        // 停在上次看的页签，并刷新它
        m_fileShareCenter->OnEnter();
    }
    else if (screen == Screen::FileShareAudit && m_fileShareCenter != nullptr)
    {
        // 直接打开「共享审计」页签
        m_fileShareCenter->SelectTab(FileShareCenterScreen::kTabAudit);
    }

    // 切换可见性（屏常驻、不销毁）。
    m_screenHost->ShowScreen(m_screens[static_cast<int>(screen)]);

    // 同步侧栏高亮与顶栏面包屑。企业管理员的企业详情屏在侧栏里叫「本企业」，面包屑随之改名。
    ScreenInfo info = GetScreenInfo(screen);
    if (!platformAdmin && info.m_navId == kIdNavEnterprises)
    {
        info.m_name = _T("本企业");
    }
    if (m_sidebar != nullptr)
    {
        m_sidebar->SetActiveNav(info.m_navId);
    }
    if (m_topBar != nullptr)
    {
        m_topBar->SetBreadcrumb(info.m_name);
    }

    // 离开或进入企业详情屏：FlamingoLink.dll 的「在线用户」页随之隐藏或显示
    SyncLinkPage();

    FA_LOGI(_T("切换到界面：%s"), info.m_name);
}

void AdminFrame::StartLoadDashboard()
{
    if (m_dashboard == nullptr)
    {
        return;
    }
    const ServerConfig& server = AppConfig::Instance().Server();
    const CString       token  = AdminSession::Instance().Token();

    // 「活跃企业」「最新公告」两张卡只对平台超管显示；企业管理员看不到它们，也不发这两个请求。
    const bool platformAdmin = m_dashboard->IsPlatformAdminMode();

    // 卡片先置为「加载中」，各自的应答回来后再按成败置为「已加载」或「加载失败」，
    // 不再显示缓存里上一次（可能是别的账号）的数据。
    m_dashboard->SetCardState(DashboardScreen::Card::Trend, DashboardScreen::CardState::Loading);
    m_dashboard->SetCardState(DashboardScreen::Card::Enterprises, DashboardScreen::CardState::Loading);
    m_dashboard->SetCardState(DashboardScreen::Card::Announcements, DashboardScreen::CardState::Loading);
    m_dashboard->SetCardState(DashboardScreen::Card::Audit, DashboardScreen::CardState::Loading);

    // 统计卡：dashboard/stats.do。
    HttpRequest reqStats = adminapi::MakeDashboardStatsRequest(server, token);
    reqStats.m_replyHwnd = m_hWnd;
    reqStats.m_replyMsg  = WM_APP_API_RESULT;
    reqStats.m_tag       = kApiTagDashStats;
    HttpWorker::Instance().Post(reqStats);

    if (platformAdmin)
    {
        // 活跃企业列表：复用 enterprise/list.do，但用专属 tag（kApiTagDashEnterprises）
        // 与企业管理界面的列表请求区分，避免结果被路由到 EnterprisesScreen。
        HttpRequest reqEnts = adminapi::MakeEnterpriseListRequest(
            server, token, CString(), kDashEntStatusAll, 1, kDashEntPageSize);
        reqEnts.m_replyHwnd = m_hWnd;
        reqEnts.m_replyMsg  = WM_APP_API_RESULT;
        reqEnts.m_tag       = kApiTagDashEnterprises;
        HttpWorker::Instance().Post(reqEnts);

        // 「最新公告」卡：announcement/list.do（拉一小页）。
        HttpRequest reqAnn = adminapi::MakeAnnListRequest(
            server, token, kAnnStatusAll, 1, kDashAnnPageSize);
        reqAnn.m_replyHwnd = m_hWnd;
        reqAnn.m_replyMsg  = WM_APP_API_RESULT;
        reqAnn.m_tag       = kApiTagDashAnnouncements;
        HttpWorker::Instance().Post(reqAnn);
    }

    // 「近期审计」卡：audit/list.do（拉一小页，不按操作者 / 级别筛选）。
    HttpRequest reqAud = adminapi::MakeAuditListRequest(
        server, token, 1, kDashAuditPageSize, -1, -1);
    reqAud.m_replyHwnd = m_hWnd;
    reqAud.m_replyMsg  = WM_APP_API_RESULT;
    reqAud.m_tag       = kApiTagDashAudit;
    HttpWorker::Instance().Post(reqAud);

    // 「近 14 天消息量」柱状图：dashboard/messagetrend.do。
    HttpRequest reqTrend = adminapi::MakeMessageTrendRequest(server, token);
    reqTrend.m_replyHwnd = m_hWnd;
    reqTrend.m_replyMsg  = WM_APP_API_RESULT;
    reqTrend.m_tag       = kApiTagDashTrend;
    HttpWorker::Instance().Post(reqTrend);

    FA_LOGI(_T("dashboard load started: platformAdmin=%d"), platformAdmin ? 1 : 0);
}

void AdminFrame::OnDashboardStatsLoaded(const HttpResult& http)
{
    if (m_dashboard == nullptr)
    {
        return;
    }
    const DashboardStatsResult r = adminapi::ParseDashboardStatsResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        m_dashboard->SetStatsFailed();
        FA_LOGW(_T("仪表盘统计加载失败：%s"), r.m_message.GetString());
        return;
    }
    m_dashboard->SetStats(r);
    FA_LOGI(_T("仪表盘统计已更新"));
}

void AdminFrame::OnDashboardEnterprisesLoaded(const HttpResult& http)
{
    if (m_dashboard == nullptr)
    {
        return;
    }
    EnterpriseListResult r = adminapi::ParseEnterpriseListResponse(http);
    if (!r.Succeeded())
    {
        // 活跃企业卡加载失败不弹提示（统计卡可能仍成功），卡片里显示「加载失败」。
        m_dashboard->SetCardState(DashboardScreen::Card::Enterprises, DashboardScreen::CardState::Failed);
        FA_LOGW(_T("仪表盘活跃企业加载失败：%s"), r.m_message.GetString());
        return;
    }
    // 服务端未给 logo 颜色时按企业名兜底（与 EnterprisesScreen::OnListResult 一致）。
    for (size_t i = 0; i < r.m_list.size(); ++i)
    {
        if (r.m_list[i].m_logoColor == RGB(0, 0, 0))
        {
            r.m_list[i].m_logoColor = ui::AvatarColor(r.m_list[i].m_name);
        }
    }
    DataStore::Instance().SetEnterprises(r.m_list);
    m_dashboard->SetCardState(DashboardScreen::Card::Enterprises, DashboardScreen::CardState::Loaded);
    FA_LOGI(_T("仪表盘活跃企业已更新，%d 家"),
            static_cast<int>(r.m_list.size()));
}

void AdminFrame::OnDashboardAnnouncementsLoaded(const HttpResult& http)
{
    if (m_dashboard == nullptr)
    {
        return;
    }
    const AnnouncementListResult r = adminapi::ParseAnnListResponse(http);
    if (!r.Succeeded())
    {
        // 卡片加载失败不弹提示（其它卡可能仍成功），卡片里显示「加载失败」。
        m_dashboard->SetCardState(DashboardScreen::Card::Announcements, DashboardScreen::CardState::Failed);
        FA_LOGW(_T("仪表盘最新公告加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetAnnouncements(r.m_list);
    m_dashboard->SetCardState(DashboardScreen::Card::Announcements, DashboardScreen::CardState::Loaded);
}

void AdminFrame::OnDashboardAuditLoaded(const HttpResult& http)
{
    if (m_dashboard == nullptr)
    {
        return;
    }
    const AuditListResult r = adminapi::ParseAuditListResponse(http);
    if (!r.Succeeded())
    {
        m_dashboard->SetCardState(DashboardScreen::Card::Audit, DashboardScreen::CardState::Failed);
        FA_LOGW(_T("仪表盘近期审计加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetAuditLogs(r.m_list);
    m_dashboard->SetCardState(DashboardScreen::Card::Audit, DashboardScreen::CardState::Loaded);
    // 顺带刷新顶栏滚动提示：仪表板这一页就是最新的几条审计。
    if (m_topBar != nullptr)
    {
        m_topBar->SetTickerAudits(r.m_list);
    }
}

void AdminFrame::OnDashboardTrendLoaded(const HttpResult& http)
{
    if (m_dashboard == nullptr)
    {
        return;
    }
    const MessageTrendResult r = adminapi::ParseMessageTrendResponse(http);
    if (!r.Succeeded())
    {
        m_dashboard->SetCardState(DashboardScreen::Card::Trend, DashboardScreen::CardState::Failed);
        FA_LOGW(_T("仪表盘消息趋势加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetMessageTrend(r.m_points);
    m_dashboard->SetCardState(DashboardScreen::Card::Trend, DashboardScreen::CardState::Loaded);
}

void AdminFrame::StartLoadAudit()
{
    if (m_auditScreen == nullptr)
    {
        return;
    }
    // 服务端分页 + 服务端筛选：页码与筛选下标都取自审计界面的当前状态。
    HttpRequest request = adminapi::MakeAuditListRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        m_auditScreen->GetPage(), m_auditScreen->GetPageSize(),
        AuditFilterToServer(m_auditScreen->GetActorFilter()),
        AuditFilterToServer(m_auditScreen->GetSevFilter()));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAuditList;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("审计日志：发起 list 加载（第 %d 页）"), m_auditScreen->GetPage());
}

void AdminFrame::StartLoadAuditStats()
{
    if (m_auditScreen == nullptr)
    {
        return;
    }
    HttpRequest request = adminapi::MakeAuditStatsRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAuditStats;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnAuditListLoaded(const HttpResult& http)
{
    if (m_auditScreen == nullptr)
    {
        return;
    }
    const AuditListResult r = adminapi::ParseAuditListResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("审计日志加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetAuditLogs(r.m_list);
    m_auditScreen->SetPageData(r.m_total);
    FA_LOGI(_T("审计日志：本页 %d 条，共 %d 条"),
            static_cast<int>(r.m_list.size()), r.m_total);
}

void AdminFrame::OnAuditStatsLoaded(const HttpResult& http)
{
    if (m_auditScreen == nullptr)
    {
        return;
    }
    const AuditStatsResult r = adminapi::ParseAuditStatsResponse(http);
    if (!r.Succeeded())
    {
        // 统计卡加载失败不打扰用户（列表可能仍成功），仅记日志。
        FA_LOGW(_T("审计统计加载失败：%s"), r.m_message.GetString());
        return;
    }
    m_auditScreen->SetStats(r.m_today, r.m_total, r.m_warn, r.m_danger);
}

void AdminFrame::StartAuditExport()
{
    if (m_auditExportBusy)
    {
        ShowToast(_T("正在导出，请稍候…"));
        return;
    }
    if (m_auditScreen == nullptr)
    {
        return;
    }
    m_auditExportBusy = true;
    // 按审计界面当前筛选导出。
    HttpRequest request = adminapi::MakeAuditExportRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        AuditFilterToServer(m_auditScreen->GetActorFilter()),
        AuditFilterToServer(m_auditScreen->GetSevFilter()));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAuditExport;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在导出审计日志…"));
}

void AdminFrame::OnAuditExported(const HttpResult& http)
{
    m_auditExportBusy = false;
    // 导出应答是 CSV 文本，复用企业导出的 CSV 信封解析。
    const EnterpriseExportResult result =
        adminapi::ParseEnterpriseExportResponse(http);
    if (!result.Succeeded())
    {
        ShowToast(result.m_message, ToastKind::Error);
        FA_LOGW(_T("导出审计日志失败：%s"), result.m_message.GetString());
        return;
    }
    SaveTextToFile(result.m_csvUtf8, filesave::CsvFileSpec(_T("审计日志.csv"), _T("审计日志")));
}

void AdminFrame::StartLoadProfile()
{
    if (m_settingsScreen == nullptr)
    {
        return;
    }
    HttpRequest request = adminapi::MakeProfileRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagProfile;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnProfileLoaded(const HttpResult& http)
{
    if (m_settingsScreen == nullptr)
    {
        return;
    }
    const ProfileResult r = adminapi::ParseProfileResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("加载管理员资料失败：%s"), r.m_message.GetString());
        return;
    }
    // scope："platform" 平台超管 / 其余按企业级管理员展示。
    const CString scopeText = (r.m_scope == _T("platform"))
                                  ? CString(_T("平台超级管理员"))
                                  : CString(_T("企业级管理员"));
    m_settingsScreen->SetProfile(r.m_name, r.m_email, scopeText);
    FA_LOGI(_T("平台设置：管理员资料已加载"));
}

void AdminFrame::SubmitUpdateProfile()
{
    if (m_settingsScreen == nullptr)
    {
        return;
    }
    const CString name = m_settingsScreen->GetName();
    if (name.IsEmpty())
    {
        ShowToast(_T("请填写姓名"), ToastKind::Warn);
        return;
    }
    if (m_updateProfileBusy)
    {
        ShowToast(_T("正在保存，请稍候…"));
        return;
    }
    m_updateProfileBusy = true;
    HttpRequest request = adminapi::MakeUpdateProfileRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(), name);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagUpdateProfile;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在保存个人资料…"));
}

void AdminFrame::OnProfileUpdated(const HttpResult& http)
{
    m_updateProfileBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("保存个人资料失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("个人资料已保存"));
}

void AdminFrame::SubmitChangePassword()
{
    if (m_settingsScreen == nullptr)
    {
        return;
    }
    const CString oldPw  = m_settingsScreen->GetOldPassword();
    const CString newPw  = m_settingsScreen->GetNewPassword();
    const CString confPw = m_settingsScreen->GetConfirmPassword();
    if (oldPw.IsEmpty() || newPw.IsEmpty() || confPw.IsEmpty())
    {
        ShowToast(_T("请填写当前密码、新密码与确认密码"), ToastKind::Warn);
        return;
    }
    if (newPw != confPw)
    {
        ShowToast(_T("两次输入的新密码不一致"), ToastKind::Warn);
        return;
    }
    if (m_changePwBusy)
    {
        ShowToast(_T("正在修改，请稍候…"));
        return;
    }
    m_changePwBusy = true;
    m_pendingNewPassword = newPw;   // 应答成功后据此更新「记住密码」保存的口令
    HttpRequest request = adminapi::MakeChangePasswordRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        oldPw, newPw);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagChangePassword;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在修改密码…"));
}

void AdminFrame::OnPasswordChanged(const HttpResult& http)
{
    m_changePwBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);   // 失败保留已填内容，便于改正
        m_pendingNewPassword.Empty();               // 暂存的新口令是明文，用不上就立即清掉
        FA_LOGW(_T("修改密码失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("登录密码已修改"));
    if (m_settingsScreen != nullptr)
    {
        m_settingsScreen->ClearPasswordFields();
    }
    // 勾了「记住密码」的，把保存的口令换成新密码，否则下次打开会预填旧密码、登录失败。
    const LoginConfig& saved = AppConfig::Instance().Login();
    const LoginConfig updated = AppConfig::ApplyPasswordChange(
        saved, m_pendingLogin.m_account, m_pendingNewPassword);
    if (updated.m_password != saved.m_password)
    {
        AppConfig::Instance().SetLogin(updated);
        AppConfig::Instance().Save();
        FA_LOGI(_T("remembered password updated after password change"));
    }
    m_pendingLogin.m_password = m_pendingNewPassword;
    m_pendingNewPassword.Empty();
}

void AdminFrame::StartLoadAnnouncements()
{
    if (m_announcementsScreen == nullptr)
    {
        return;
    }
    HttpRequest request = adminapi::MakeAnnListRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        kAnnStatusAll, 1, kAnnFetchPageSize);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnList;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("公告中心：发起 list 加载"));
}

void AdminFrame::OnAnnouncementsLoaded(const HttpResult& http)
{
    if (m_announcementsScreen == nullptr)
    {
        return;
    }
    const AnnouncementListResult r = adminapi::ParseAnnListResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("公告列表加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetAnnouncements(r.m_list);
    m_announcementsScreen->Refresh();
    FA_LOGI(_T("公告列表已载入 %d 条"), static_cast<int>(r.m_list.size()));
}

void AdminFrame::StartLoadAnnouncementDetail(const CString& annId)
{
    if (annId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeAnnDetailRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(annId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnDetail;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnAnnouncementDetailLoaded(const HttpResult& http)
{
    if (m_announcementsScreen == nullptr)
    {
        return;
    }
    const AnnouncementDetailResult r = adminapi::ParseAnnDetailResponse(http);
    if (!r.Succeeded())
    {
        // 详情加载失败不打扰用户（列表仍可用），仅记日志。
        FA_LOGW(_T("公告详情加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().UpdateAnnouncement(r.m_announcement);
    m_announcementsScreen->Refresh();
}

void AdminFrame::SubmitAnnouncementSetPinned()
{
    if (m_announcementsScreen == nullptr)
    {
        return;
    }
    if (m_annActionBusy)
    {
        ShowToast(_T("正在处理，请稍候…"));
        return;
    }
    const CString annId = m_announcementsScreen->GetSelectedId();
    if (annId.IsEmpty())
    {
        return;
    }
    // 取当前置顶状态，提交其反值。
    bool curPinned = false;
    for (const Announcement& a : DataStore::Instance().Announcements())
    {
        if (a.m_id == annId)
        {
            curPinned = a.m_pinned;
            break;
        }
    }
    m_annActionBusy = true;
    HttpRequest request = adminapi::MakeAnnSetPinnedRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(annId), !curPinned);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnSetPinned;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnAnnouncementActionDone(const HttpResult& http, LPCTSTR okText)
{
    m_annActionBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("公告写操作失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(okText);
    StartLoadAnnouncements();   // 重拉列表反映最新状态
}

void AdminFrame::HandleLogout()
{
    // 通知服务端注销会话：拼好登出请求交 HTTP 工作线程发送。登出是「尽力
    // 而为」—— 不设 m_replyMsg，工作线程发完即丢弃结果，不回传、不等待；
    // 即便失败也只是服务端会话记录残留至自然过期，不影响本地登出。
    AdminSession& session = AdminSession::Instance();
    if (session.IsLoggedIn())
    {
        HttpRequest request = adminapi::MakeLogoutRequest(
            AppConfig::Instance().Server(), session.Token());
        HttpWorker::Instance().Post(request);
        FA_LOGI(_T("已提交登出请求，通知服务端注销会话"));
    }
    session.Clear();

    m_loggedIn = false;
    ResetSessionState();
    if (m_root != nullptr && m_loginPage != nullptr)
    {
        m_root->ShowPage(m_loginPage);   // 只切显隐，不销毁外壳
    }
    FA_LOGI(_T("user logged out"));
}

void AdminFrame::ResetSessionState()
{
    KillTimer(kTickerTimerId);   // 停掉顶栏审计滚动提示

    // 断开 FlamingoLink.dll 的长连接并清空在线名单；页面的显隐随之后的页面切换由 SyncLinkPage 处理
    LinkBridge::Instance().Stop();

    // 关闭叠层弹窗与独立对话框。独立对话框经 WM_CLOSE 关闭，销毁回调里会清掉各自的指针。
    if (m_root != nullptr)
    {
        m_root->HideModal();
    }
    if (m_confirmDialog != nullptr)
    {
        m_confirmDialog->RequestClose();
    }
    if (m_nodeModal != nullptr)
    {
        m_nodeModal->RequestClose();
    }
    if (m_employeeEditDialog != nullptr)
    {
        m_employeeEditDialog->RequestClose();
    }

    // 复位全部「请求进行中」标志与待处理状态，避免换账号后操作被卡住。
    m_loginBusy           = false;
    m_entCreateBusy       = false;
    m_entExportBusy       = false;
    m_auditExportBusy     = false;
    m_updateProfileBusy   = false;
    m_changePwBusy        = false;
    m_annActionBusy       = false;
    m_composeSubmitBusy   = false;
    m_nodeSubmitBusy      = false;
    m_svcSubmitBusy       = false;
    m_svcBindingFetchBusy = false;
    m_groupSubmitBusy     = false;
    m_empEditBusy         = false;
    m_entUpdateBusy       = false;
    m_addAdminBusy        = false;
    m_orgTreeReloading    = false;
    m_entSettingsOpen     = false;
    m_detailLoadEntId.Empty();          // 放弃可能在途的企业详情加载
    m_pendingBindingsSvcId.Empty();
    m_pendingBindings.clear();
    m_bindingModalSvcId.Empty();
    m_pendingNewPassword.Empty();
    m_confirmDeleteNodeId.Empty();
    m_confirmTarget = 0;

    // 先清空缓存，再让各界面重新取一次指针：企业详情页按空 ID 重新设置后，企业、组织树与
    // 员工面板持有的指针都变为空，迟到的接口应答不会再访问已释放的数据。
    DataStore::Instance().Clear();
    if (m_enterpriseDetail != nullptr)
    {
        m_enterpriseDetail->SetEnterprise(CString());
    }
    if (m_enterprises != nullptr)
    {
        m_enterprises->ResetState();
    }
    if (m_topBar != nullptr)
    {
        m_topBar->SetTickerAudits(std::vector<AuditLog>());
    }
    FA_LOGI(_T("session state reset"));
}

void AdminFrame::FetchTickerAudits()
{
    if (!AdminSession::Instance().IsLoggedIn())
    {
        return;
    }
    HttpRequest request = adminapi::MakeAuditListRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        1, kTickerAuditCount, -1, -1);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagTickerAudit;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnTickerAuditLoaded(const HttpResult& http)
{
    const AuditListResult r = adminapi::ParseAuditListResponse(http);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("load ticker audits failed: %s"), r.m_message.GetString());
        return;
    }
    if (m_topBar != nullptr)
    {
        m_topBar->SetTickerAudits(r.m_list);
    }
}

void AdminFrame::HandleSessionExpired()
{
    // 与 HandleLogout 类似，但不通知服务端（token 本就已失效），并给出提示。
    AdminSession::Instance().Clear();
    m_loggedIn = false;
    ResetSessionState();
    if (m_root != nullptr && m_loginPage != nullptr)
    {
        m_root->ShowPage(m_loginPage);
    }
    ShowToast(_T("登录已过期，请重新登录"), ToastKind::Error);
    FA_LOGW(_T("会话失效（code=2），已退回登录页"));
}

LRESULT AdminFrame::OnLinkExpired(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    bHandled = TRUE;
    //已经退出登录（例如 HTTP 接口先发现了会话过期）时，这条迟到的通知不再处理
    if (m_loggedIn)
    {
        FA_LOGW(_T("FlamingoLink: token invalid on the admin link, back to login"));
        HandleSessionExpired();
    }
    return 0;
}

void AdminFrame::SyncLinkPage()
{
    LinkBridge& link = LinkBridge::Instance();
    if (!link.IsAvailable() || m_enterpriseDetail == nullptr)
    {
        return;
    }

    link.SetEnterprise(m_enterpriseDetail->GetEnterpriseId(), m_enterpriseDetail->GetEnterpriseName());
    link.SetPageBounds(m_enterpriseDetail->GetOnlineSlot()->LastBounds());

    //企业详情屏「确实可见」：它与它的每一层父控件都可见（外壳页隐藏时它自己的可见标志仍是 true）
    bool detailVisible = true;
    for (const balloonwjui::DuiControl* c = m_enterpriseDetail; c != nullptr; c = c->GetParent())
    {
        if (!c->IsVisible())
        {
            detailVisible = false;
            break;
        }
    }

    const bool modalVisible = (m_root != nullptr) && m_root->IsModalVisible();
    link.ApplyPageState(DecideLinkPageState(m_loggedIn, detailVisible, m_enterpriseDetail->IsOnlineTab(),
                                            modalVisible));
}

void AdminFrame::LoadFeaturesPage()
{
    if (m_enterpriseDetail == nullptr || m_enterpriseDetail->GetFeaturesPage() == nullptr)
    {
        return;
    }
    const long long enterpriseId = FeatureSwitchesPage::EnterpriseIdForLoad(
        AdminSession::Instance().IsPlatformAdmin(), m_enterpriseDetail->GetEnterpriseId());
    FA_LOGI(_T("enterprise detail: open feature switches tab, enterprise %s, request enterprise %lld"),
            m_enterpriseDetail->GetEnterpriseId().GetString(), enterpriseId);
    m_enterpriseDetail->GetFeaturesPage()->Load(enterpriseId);
}

void AdminFrame::LinkLayerChangedThunk(void* user)
{
    static_cast<AdminFrame*>(user)->SyncLinkPage();
}

void AdminFrame::LinkSlotBoundsThunk(void* user, const RECT& rc)
{
    (void)user;
    LinkBridge::Instance().SetPageBounds(rc);
}

void AdminFrame::ToggleSidebar()
{
    if (m_shellPage == nullptr || m_sidebar == nullptr)
    {
        return;
    }
    m_sidebarCollapsed = !m_sidebarCollapsed;
    const int width = m_sidebarCollapsed ? theme::kSidebarWidthCompact
                                         : theme::kSidebarWidth;
    // 改外层 HBox 给侧边栏的宽度 hint，再令外壳重新布局。
    static_cast<balloonwjui::DuiLayout*>(m_shellPage)->SetHint(
        m_sidebar, balloonwjui::DuiLayout::Hint().Fixed(width));
    m_sidebar->SetCollapsed(m_sidebarCollapsed);
    m_shellPage->ForceLayout(m_shellPage->GetRect());
    m_shellPage->Invalidate();
    FA_LOGI(_T("侧边栏切换为%s态"),
            m_sidebarCollapsed ? _T("紧凑") : _T("标准"));
}

void AdminFrame::ShowNetworkSettings()
{
    if (m_root == nullptr || m_networkScreen == nullptr)
    {
        return;
    }
    m_networkScreen->LoadFromConfig();   // 进入前把当前配置载入表单
    m_root->ShowPage(m_networkPage);
    FA_LOGI(_T("进入网络设置页"));
}

void AdminFrame::BackToLoginFromNetwork()
{
    if (m_root == nullptr || m_loginPage == nullptr)
    {
        return;
    }
    m_root->ShowPage(m_loginPage);
    FA_LOGI(_T("从网络设置页返回登录"));
}

void AdminFrame::SaveNetworkSettings()
{
    if (m_networkScreen == nullptr || m_root == nullptr)
    {
        return;
    }
    // 校验：主机非空、端口为 1 - 65535。
    const CString host     = m_networkScreen->GetHost();
    const CString portText = m_networkScreen->GetPortText();
    if (host.IsEmpty())
    {
        m_networkScreen->SetStatus(_T("请输入服务器地址"), theme::kColorDanger);
        return;
    }
    const int port = _ttoi(portText);
    if (port < 1 || port > 65535)
    {
        m_networkScreen->SetStatus(_T("端口须为 1 - 65535 之间的数字"),
                                   theme::kColorDanger);
        return;
    }

    // 写入配置并落盘。
    ServerConfig cfg;
    cfg.m_host   = host;
    cfg.m_port   = port;
    cfg.m_useTls = m_networkScreen->GetUseTls();
    AppConfig::Instance().SetServer(cfg);
    AppConfig::Instance().Save();

    // 刷新登录页「连接到 …」标签。
    if (m_loginPage != nullptr)
    {
        DuiLabel* lbl = dynamic_cast<DuiLabel*>(
            m_loginPage->FindCtrlById(kIdLoginConnInfo));
        if (lbl != nullptr)
        {
            CString connText;
            connText.Format(_T("连接到 %s:%d"),
                            static_cast<LPCTSTR>(cfg.m_host), cfg.m_port);
            lbl->SetText(connText);
        }
    }

    m_root->ShowPage(m_loginPage);
    ShowToast(_T("网络设置已保存"));
    FA_LOGI(_T("网络设置已保存：%s:%d tls=%d"),
            cfg.m_host.GetString(), cfg.m_port, cfg.m_useTls ? 1 : 0);
}

void AdminFrame::StartNetTest()
{
    if (m_networkScreen == nullptr)
    {
        return;
    }
    if (m_netTestBusy)
    {
        m_networkScreen->SetStatus(_T("连接测试进行中，请稍候…"),
                                   theme::kInk400);
        return;
    }
    // 校验：主机非空、端口合法。
    const CString host     = m_networkScreen->GetHost();
    const CString portText = m_networkScreen->GetPortText();
    if (host.IsEmpty())
    {
        m_networkScreen->SetStatus(_T("请先输入服务器地址"),
                                   theme::kColorDanger);
        return;
    }
    const int port = _ttoi(portText);
    if (port < 1 || port > 65535)
    {
        m_networkScreen->SetStatus(_T("端口须为 1 - 65535 之间的数字"),
                                   theme::kColorDanger);
        return;
    }

    // 起工作线程做 TCP 探测，避免阻塞 UI；结果经 WM_APP_NETTEST_RESULT 回传。
    m_netTestBusy = true;
    m_networkScreen->SetStatus(_T("正在测试连接…"), theme::kColorInfo);
    std::wstring hostW(host.GetString());
    std::thread worker(&RunNetTest, m_hWnd, hostW, port);
    worker.detach();
    FA_LOGI(_T("开始测试连接 %s:%d"), host.GetString(), port);
}

void AdminFrame::TrySubmitLogin()
{
    // 读取账户 / 密码输入框的当前文字（读取不销毁控件，安全）。
    DuiEdit* account = m_loginPage != nullptr
        ? dynamic_cast<DuiEdit*>(m_loginPage->FindCtrlById(kIdLoginAccount))  : nullptr;
    DuiEdit* password = m_loginPage != nullptr
        ? dynamic_cast<DuiEdit*>(m_loginPage->FindCtrlById(kIdLoginPassword)) : nullptr;

    CString accountText  = account  != nullptr ? account->GetText()  : CString();
    CString passwordText = password != nullptr ? password->GetText() : CString();
    accountText.Trim();
    passwordText.Trim();

    if (accountText.IsEmpty() || passwordText.IsEmpty())
    {
        ShowToast(_T("请输入账户与密码"), ToastKind::Warn);
        FA_LOGW(_T("登录校验未通过：账户或密码为空"));
        return;
    }

    // 已有登录请求在途，避免重复提交。
    if (m_loginBusy)
    {
        ShowToast(_T("正在登录，请稍候…"));
        return;
    }
    m_loginBusy = true;

    // 把本次提交用的账号 / 口令 / "记住密码"勾选状态暂存下来，登录成功后由
    // OnLoginResult 落盘。之所以在这里存而不是等到应答回来再读控件：登录是异步的，
    // 这期间用户完全可能又去改输入框，那时读到的就不是这次登录用的凭据了。
    m_pendingLogin.m_account  = accountText;
    m_pendingLogin.m_password = passwordText;
    m_pendingLogin.m_rememberPassword = false;
    if (m_loginPage != nullptr)
    {
        if (DuiButton* remember = dynamic_cast<DuiButton*>(
                m_loginPage->FindCtrlById(kIdLoginRememberPwd)))
        {
            m_pendingLogin.m_rememberPassword = remember->IsChecked();
        }
    }

    // 拼好登录请求，交 HTTP 工作线程异步发送 —— Post 后本函数立即返回，
    // 界面不阻塞；应答经 WM_APP_LOGIN_RESULT 回到 OnLoginResult。
    const ServerConfig& server = AppConfig::Instance().Server();
    HttpRequest request = adminapi::MakeLoginRequest(server, accountText,
                                                     passwordText);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_LOGIN_RESULT;
    HttpWorker::Instance().Post(request);

    ShowToast(_T("正在登录…"));
    // 仅记录账户，不记录密码。
    FA_LOGI(_T("提交登录请求：account=%s server=%s:%d"),
            accountText.GetString(), server.m_host.GetString(),
            server.m_port);
}

void AdminFrame::ShowNewEnterpriseModal()
{
    if (m_modal == nullptr || m_root == nullptr)
    {
        return;
    }
    m_modal->Reset();
    // 隐藏外壳内的输入框。这是早先内嵌真 Win32 输入框子窗口时的必要动作
    // （那时输入框会绘制在半透明遮罩之上），现在输入框是无窗口控件、由库统一
    // 绘制并受遮罩覆盖，该前提已不成立，此处保留只是尚未清理。
    SetEditsVisible(m_shellPage, false);
    // ShowModal 会把弹窗 SetVisible(true)，弹窗内的输入框随之一并显示。
    m_root->ShowModal(m_modal);
    FA_LOGI(_T("打开新增企业弹窗"));
}

void AdminFrame::HideNewEnterpriseModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);   // 恢复外壳内被隐藏的输入框
    FA_LOGI(_T("关闭新增企业弹窗"));
}

void AdminFrame::SubmitNewEnterprise()
{
    if (m_modal == nullptr)
    {
        return;
    }
    if (!m_modal->Validate())
    {
        ShowToast(_T("请填写企业全称、管理员姓名与邮箱"), ToastKind::Warn);
        FA_LOGW(_T("新增企业校验未通过：必填项为空"));
        return;
    }
    if (m_entCreateBusy)
    {
        ShowToast(_T("正在创建，请稍候…"));
        return;
    }

    // 从表单取字段。简称留空时回退用全称。
    CString name = m_modal->GetEnterpriseName();
    name.Trim();
    CString shortName = m_modal->GetShortName();
    shortName.Trim();
    if (shortName.IsEmpty())
    {
        shortName = name;
    }
    CString region = m_modal->GetRegion();
    region.Trim();
    CString adminName = m_modal->GetAdminName();
    adminName.Trim();
    CString adminEmail = m_modal->GetContact();
    adminEmail.Trim();
    const CString industry = m_modal->GetIndustry();
    const int     plan     = PlanCodeForNewEnterprise(m_modal->GetPlan());

    // 发起 create.do；弹窗暂不关闭，待应答回来在 OnEnterpriseCreated 处理。
    m_entCreateBusy = true;
    HttpRequest request = adminapi::MakeEnterpriseCreateRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        name, shortName, industry, region, plan, adminName, adminEmail);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagEntCreate;
    HttpWorker::Instance().Post(request);

    ShowToast(_T("正在创建企业…"));
    FA_LOGI(_T("提交新增企业：%s"), name.GetString());
}

void AdminFrame::OnEnterpriseCreated(const HttpResult& http)
{
    m_entCreateBusy = false;
    const EnterpriseCreateResult result =
        adminapi::ParseEnterpriseCreateResponse(http);
    if (!result.Succeeded())
    {
        // 失败：保留弹窗，让用户修改后重试。
        ShowToast(result.m_message, ToastKind::Error);
        FA_LOGW(_T("新增企业失败：%s"), result.m_message.GetString());
        return;
    }

    HideNewEnterpriseModal();
    if (m_enterprises != nullptr)
    {
        m_enterprises->Refresh();
    }

    // 用应用内风格一致的信息弹窗展示初始密码（单按钮）。信息弹窗不涉及删除，
    // 先清空待删除项，使「知道了」只关闭弹窗（见 OnConfirmDialogConfirmed）。
    CString msg;
    msg.Format(_T("企业已创建成功。\n初始管理员密码：%s\n")
               _T("请妥善保存并转交，此密码仅显示一次。"),
               static_cast<LPCTSTR>(result.m_initialPassword));
    m_confirmDeleteNodeId.Empty();
    ConfirmDialogSpec spec;
    spec.m_infoMode = true;
    spec.m_title    = _T("企业创建成功");
    spec.m_message  = msg;
    spec.m_okText   = _T("知道了");
    spec.m_copyText = result.m_initialPassword;
    ShowConfirmDialog(spec);
    FA_LOGI(_T("新增企业成功：enterpriseid=%lld adminid=%lld"),
            result.m_enterpriseId, result.m_adminId);
}

void AdminFrame::OnEnterpriseActionDone(const HttpResult& http, LPCTSTR okText)
{
    const EnterpriseActionResult result =
        adminapi::ParseEnterpriseActionResponse(http);
    if (result.Succeeded())
    {
        ShowToast(okText);
        if (m_enterprises != nullptr)
        {
            m_enterprises->Refresh();   // 写操作后重拉列表，反映最新状态
        }
        FA_LOGI(_T("企业写操作成功：%s"), okText);
    }
    else
    {
        ShowToast(result.m_message, ToastKind::Error);
        FA_LOGW(_T("企业写操作失败：%s"), result.m_message.GetString());
    }
}

void AdminFrame::StartEnterpriseExport()
{
    if (m_entExportBusy)
    {
        ShowToast(_T("正在导出，请稍候…"));
        return;
    }
    m_entExportBusy = true;
    // 按企业列表当前的筛选条件导出（搜索框、状态分段与「更多筛选」），导出的就是界面上筛出的企业。
    const EnterpriseListFilter filter = (m_enterprises != nullptr) ? m_enterprises->GetFilter()
                                                                   : EnterpriseListFilter();
    HttpRequest request = adminapi::MakeEnterpriseExportRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(), filter);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagEntExport;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在导出企业列表…"));
}

void AdminFrame::OnEnterpriseExported(const HttpResult& http)
{
    m_entExportBusy = false;
    const EnterpriseExportResult result =
        adminapi::ParseEnterpriseExportResponse(http);
    if (!result.Succeeded())
    {
        ShowToast(result.m_message, ToastKind::Error);
        FA_LOGW(_T("导出企业列表失败：%s"), result.m_message.GetString());
        return;
    }
    SaveTextToFile(result.m_csvUtf8, filesave::CsvFileSpec(_T("企业列表.csv"), _T("企业列表")));
}

void AdminFrame::SaveTextToFile(const std::string& content, const filesave::SaveFileSpec& spec)
{
    // 弹「另存为」对话框，预填调用方给的默认文件名。
    TCHAR pathBuf[MAX_PATH] = {};
    lstrcpyn(pathBuf, spec.m_defaultName, MAX_PATH);
    std::vector<TCHAR> filter;
    filesave::BuildFilterBuffer(spec, filter);
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = &filter[0];
    ofn.lpstrFile   = pathBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = spec.m_defExt;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!::GetSaveFileName(&ofn))
    {
        // 用户取消保存对话框，不算失败。
        return;
    }

    HANDLE file = ::CreateFile(pathBuf, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        ShowToast(_T("无法写入所选文件"), ToastKind::Error);
        FA_LOGW(_T("export: cannot open target file, error: %lu"), ::GetLastError());
        return;
    }
    // 是否补 UTF-8 BOM 由规格决定：CSV 要补，Excel 等工具据此识别中文；内容已带 BOM 时不重复补。
    const std::string bytes = filesave::BuildFileBytes(spec, content);
    DWORD written = 0;
    BOOL ok = TRUE;
    if (!bytes.empty())
    {
        ok = ::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }
    ::CloseHandle(file);
    if (!ok || written != static_cast<DWORD>(bytes.size()))
    {
        ShowToast(_T("写入文件失败，文件可能不完整"), ToastKind::Error);
        FA_LOGW(_T("export: write failed, %lu of %d bytes written"), written, static_cast<int>(bytes.size()));
        return;
    }

    ShowToast(spec.m_successToast);
    FA_LOGI(_T("export: saved %d bytes, default name: %s"), static_cast<int>(bytes.size()),
            spec.m_defaultName.GetString());
}

void AdminFrame::ShowEnterpriseMenu()
{
    if (m_enterprises == nullptr)
    {
        return;
    }
    const Enterprise* entPtr = m_enterprises->GetClickedEnterprise();
    if (entPtr == nullptr)
    {
        return;
    }
    // 整条企业记录拷出来：TrackPopup 跑嵌套消息循环，其间列表可能被刷新、
    // entPtr 可能失效，故后续一律用这份拷贝。
    const Enterprise ent = *entPtr;

    // 「打开详情」子菜单：组织架构 / 服务号 / 功能开关三个子 tab（「在线用户」要等详情打开后才有意义，不列）。
    // 子菜单为借用，须在整个 TrackPopup 期间保活，故作为本函数局部变量。
    DuiMenu detailSub;
    detailSub.AppendItem(kMenuEntDetailOrg, _T("组织架构"));
    detailSub.AppendItem(kMenuEntDetailSvc, _T("服务号"));
    detailSub.AppendItem(kMenuEntDetailFeatures, _T("功能开关"));

    DuiMenu menu;
    menu.AppendSubMenu(kMenuEntDetailSub, _T("打开详情"), &detailSub);
    menu.AppendItem(kMenuEntSettings, _T("企业设置…"));
    menu.AppendSeparator();
    // 按企业当前状态给出唯一可用的状态动作。
    if (ent.m_status == EnterpriseStatus::Pending)
    {
        menu.AppendItem(kMenuEntApprove, _T("审核通过"));
    }
    else if (ent.m_status == EnterpriseStatus::Active)
    {
        menu.AppendItem(kMenuEntSuspend, _T("停用企业"));
    }
    else   // EnterpriseStatus::Suspended
    {
        menu.AppendItem(kMenuEntActivate, _T("启用企业"));
    }
    menu.AppendItem(kMenuEntDelete, _T("删除企业"));
    menu.AppendSeparator();
    menu.AppendItem(kMenuEntCopyId, _T("复制企业 ID"));
    menu.AppendItem(kMenuEntExportOne, _T("导出该企业为 CSV"));

    const UINT chosen = menu.TrackPopup(m_entMenuPt.x, m_entMenuPt.y, m_hWnd);
    if (chosen == 0)
    {
        return;   // 用户未选任何项
    }

    switch (chosen)
    {
    case kMenuEntDetailOrg:
        m_detailInitialTab = kDetailTabOrg;
        StartLoadEnterpriseDetail(ent.m_id);
        break;
    case kMenuEntDetailSvc:
        m_detailInitialTab = kDetailTabService;
        StartLoadEnterpriseDetail(ent.m_id);
        break;
    //打开详情并定位到「功能开关」子 tab：切过去时由子 tab 切换通知读取该企业的设置
    case kMenuEntDetailFeatures:
        m_detailInitialTab = kDetailTabFeatures;
        StartLoadEnterpriseDetail(ent.m_id);
        break;
    case kMenuEntSettings:
        OpenEnterpriseSettings(ent);
        break;
    case kMenuEntCopyId:
        // CopyTextToClipboard 见本文件前部的匿名命名空间助手（返回 void）。
        CopyTextToClipboard(m_hWnd, ent.m_id);
        ShowToast(_T("企业 ID 已复制到剪贴板"));
        break;
    case kMenuEntExportOne:
        ExportSingleEnterprise(ent);
        break;
    case kMenuEntDelete:
    {
        // 删除不可逆，先弹确认框（复用通用确认弹窗）。
        CString msg;
        msg.Format(_T("确定要删除企业「%s」吗？该企业及其组织、成员等数据将")
                   _T("被一并物理删除，此操作不可恢复。"),
                   static_cast<LPCTSTR>(ent.m_name));
        m_confirmDeleteNodeId = ent.m_id;
        m_confirmTarget       = kConfirmTargetEnterprise;
        ConfirmDialogSpec spec;
        spec.m_title         = _T("删除企业");
        spec.m_message       = msg;
        spec.m_okText        = _T("删除");
        spec.m_confirmDanger = true;
        ShowConfirmDialog(spec);
        break;
    }
    case kMenuEntApprove:
    case kMenuEntActivate:
    case kMenuEntSuspend:
    {
        // 改状态：审核通过 / 启用 → 活跃，直接提交；停用会让该企业全员下线，先确认。
        if (chosen != kMenuEntSuspend)
        {
            SubmitEnterpriseStatus(ent.m_id, kEntStatusActive);
            break;
        }
        CString msg;
        msg.Format(_T("确定要停用企业「%s」吗？停用后该企业全体员工会被立即下线，")
                   _T("在恢复为活跃之前都无法登录。"),
                   static_cast<LPCTSTR>(ent.m_name));
        m_confirmDeleteNodeId = ent.m_id;
        m_confirmTarget       = kConfirmTargetEntSuspend;
        ConfirmDialogSpec spec;
        spec.m_title         = _T("停用企业");
        spec.m_message       = msg;
        spec.m_okText        = _T("停用");
        spec.m_confirmDanger = true;
        ShowConfirmDialog(spec);
        break;
    }
    default:
        break;
    }
}

void AdminFrame::SubmitEnterpriseStatus(const CString& enterpriseId, int newStatus)
{
    HttpRequest request = adminapi::MakeEnterpriseSetStatusRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(enterpriseId), newStatus);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagEntSetStatus;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在更新企业状态…"));
    FA_LOGI(_T("enterprise %s: submit set status -> %d"), enterpriseId.GetString(), newStatus);
}

void AdminFrame::SubmitRemoveAdmin(long long adminId)
{
    HttpRequest req = adminapi::MakeEnterpriseRemoveAdminRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(), adminId);
    req.m_replyHwnd = m_hWnd;
    req.m_replyMsg  = WM_APP_API_RESULT;
    req.m_tag       = kApiTagEntRemoveAdmin;
    HttpWorker::Instance().Post(req);
    ShowToast(_T("正在移除管理员…"));
    FA_LOGI(_T("submit remove enterprise admin: adminid=%lld"), adminId);
}

void AdminFrame::SubmitKickDevices(const CString& userId)
{
    m_permUserId = userId;
    HttpRequest req = adminapi::MakeUserKickDeviceRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(), _ttoi64(userId),
        CString());
    req.m_replyHwnd = m_hWnd;
    req.m_replyMsg  = WM_APP_API_RESULT;
    req.m_tag       = kApiTagUserKickDevice;
    HttpWorker::Instance().Post(req);
    ShowToast(_T("正在强制下线全部设备…"));
    FA_LOGI(_T("submit kick all devices: userid=%s"), userId.GetString());
}

void AdminFrame::SubmitUserRole(const CString& userId)
{
    OrgDetailPanel* panel = (m_enterpriseDetail != nullptr)
        ? m_enterpriseDetail->GetOrgDetailPanel() : nullptr;
    if (panel == nullptr || userId.IsEmpty())
    {
        return;
    }
    m_permUserId = userId;
    HttpRequest req = adminapi::MakeUserSetRoleRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(), _ttoi64(userId),
        panel->GetUserRole());
    req.m_tag       = kApiTagUserSetRole;
    req.m_replyHwnd = m_hWnd;
    req.m_replyMsg  = WM_APP_API_RESULT;
    HttpWorker::Instance().Post(req);
    FA_LOGI(_T("submit user role: userid=%s role=%d"), userId.GetString(), panel->GetUserRole());
}

void AdminFrame::SubmitServiceStatus(const CString& serviceId, int target)
{
    HttpRequest request = adminapi::MakeServiceSetStatusRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(serviceId), target);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagSvcSetStatus;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在更新服务号状态…"));
    FA_LOGI(_T("submit service status: service=%s target=%d"), serviceId.GetString(), target);
}

void AdminFrame::ExportSingleEnterprise(const Enterprise& ent)
{
    // 本地拼一行 CSV（无需联网）：表头 + 一行数据。
    CString csv;
    csv += _T("企业ID,企业全称,企业简称,行业,地区,成员数,部门数,套餐,")
           _T("状态,主管理员,管理员邮箱,开通日期\r\n");

    CString members;
    members.Format(_T("%d"), ent.m_members);
    CString depts;
    depts.Format(_T("%d"), ent.m_depts);

    csv += CsvCell(ent.m_id);
    csv += _T(",");
    csv += CsvCell(ent.m_name);
    csv += _T(",");
    csv += CsvCell(ent.m_shortName);
    csv += _T(",");
    csv += CsvCell(ent.m_industry);
    csv += _T(",");
    csv += CsvCell(ent.m_region);
    csv += _T(",");
    csv += members;
    csv += _T(",");
    csv += depts;
    csv += _T(",");
    csv += CsvCell(ent.m_plan);
    csv += _T(",");
    csv += CsvCell(EnterpriseStatusText(ent.m_status));
    csv += _T(",");
    csv += CsvCell(ent.m_admin);
    csv += _T(",");
    csv += CsvCell(ent.m_contact);
    csv += _T(",");
    csv += CsvCell(ent.m_createdAt);
    csv += _T("\r\n");

    CString defaultName;
    defaultName.Format(_T("企业-%s.csv"), static_cast<LPCTSTR>(ent.m_name));
    SaveTextToFile(Utf8FromWide(csv), filesave::CsvFileSpec(defaultName, _T("企业信息")));
    FA_LOGI(_T("已导出单个企业 CSV：%s"), ent.m_id.GetString());
}

void AdminFrame::ExportAnnouncementsCsv()
{
    // 公告模块无服务端导出接口 —— 直接把当前已载入的公告列表导成 CSV。
    const std::vector<Announcement>& list = DataStore::Instance().Announcements();
    if (list.empty())
    {
        ShowToast(_T("当前没有可导出的公告"), ToastKind::Warn);
        return;
    }
    CString csv;
    csv += _T("标题,级别,受众,状态,发布人,发送时间,已读数\r\n");
    for (size_t i = 0; i < list.size(); ++i)
    {
        const Announcement& a = list[i];
        CString read;
        read.Format(_T("%d"), a.m_read);
        csv += CsvCell(a.m_title);
        csv += _T(",");
        csv += CsvCell(AnnSeverityText(a.m_severity));
        csv += _T(",");
        csv += CsvCell(a.m_audience);
        csv += _T(",");
        csv += CsvCell(AnnStatusText(a.m_status));
        csv += _T(",");
        csv += CsvCell(a.m_sender);
        csv += _T(",");
        csv += CsvCell(a.m_sentAt);
        csv += _T(",");
        csv += read;
        csv += _T("\r\n");
    }
    SaveTextToFile(Utf8FromWide(csv), filesave::CsvFileSpec(_T("公告列表.csv"), _T("公告列表")));
    FA_LOGI(_T("已导出公告 CSV，%d 条"), static_cast<int>(list.size()));
}

void AdminFrame::StartLoadEnterpriseDetail(const CString& enterpriseId)
{
    if (!m_detailLoadEntId.IsEmpty())
    {
        ShowToast(_T("正在加载企业详情，请稍候…"));
        return;
    }
    m_detailLoadEntId     = enterpriseId;
    m_detailLoadRemaining = 3;          // detail.do + dept/tree.do + service/list.do
    m_detailLoadFailed    = false;

    const ServerConfig& server = AppConfig::Instance().Server();
    const CString       token  = AdminSession::Instance().Token();
    const long long     entId  = _ttoi64(enterpriseId);

    // 三个请求并发发出，结果各自经 WM_APP_API_RESULT 回到 OnDetailPartLoaded。
    HttpRequest reqDetail = adminapi::MakeEnterpriseDetailRequest(server, token, entId);
    reqDetail.m_replyHwnd = m_hWnd;
    reqDetail.m_replyMsg  = WM_APP_API_RESULT;
    reqDetail.m_tag       = kApiTagEntDetail;
    HttpWorker::Instance().Post(reqDetail);

    HttpRequest reqTree = adminapi::MakeDeptTreeRequest(server, token, entId);
    reqTree.m_replyHwnd = m_hWnd;
    reqTree.m_replyMsg  = WM_APP_API_RESULT;
    reqTree.m_tag       = kApiTagDeptTree;
    HttpWorker::Instance().Post(reqTree);

    HttpRequest reqSvc = adminapi::MakeServiceListRequest(server, token, entId);
    reqSvc.m_replyHwnd = m_hWnd;
    reqSvc.m_replyMsg  = WM_APP_API_RESULT;
    reqSvc.m_tag       = kApiTagSvcList;
    HttpWorker::Instance().Post(reqSvc);

    ShowToast(_T("正在加载企业详情…"));
    FA_LOGI(_T("开始加载企业详情：%s"), enterpriseId.GetString());
}

void AdminFrame::OnDetailPartLoaded(ApiTag tag, const HttpResult& http)
{
    // 该次详情加载若已被放弃（如会话失效），忽略迟到的结果。
    if (m_detailLoadEntId.IsEmpty())
    {
        return;
    }

    bool ok = false;
    if (tag == kApiTagEntDetail)
    {
        const EnterpriseDetailResult r = adminapi::ParseEnterpriseDetailResponse(http);
        ok = r.Succeeded();
        if (ok)
        {
            m_detailEnt = r.m_enterprise;
        }
    }
    else if (tag == kApiTagDeptTree)
    {
        // 这里只判断请求本身是否成功，真正的解析推迟到三个请求都回来之后：
        // 组装组织树要用到 detail.do 给出的公司根部门 ID，而它可能还没回来。
        m_detailTreeHttp = http;
        ok = http.m_transportOk;
    }
    else if (tag == kApiTagSvcList)
    {
        const ServiceListResult r = adminapi::ParseServiceListResponse(http);
        ok = r.Succeeded();
        if (ok)
        {
            m_detailServices = r.m_list;
        }
    }
    if (!ok)
    {
        m_detailLoadFailed = true;
    }

    // 还有请求未回来，继续等。
    if (--m_detailLoadRemaining > 0)
    {
        return;
    }

    // 三个请求都回来了。
    const CString entId = m_detailLoadEntId;
    m_detailLoadEntId.Empty();

    if (m_detailLoadFailed)
    {
        ShowToast(_T("加载企业详情失败，请重试"), ToastKind::Error);
        FA_LOGW(_T("加载企业详情失败：%s"), entId.GetString());
        return;
    }

    // 三个请求都齐了，此时才知道该企业的公司根部门 ID，可以组装组织树了。
    const DeptTreeResult treeResult = adminapi::ParseDeptTreeResponse(
        m_detailTreeHttp, _ttoi64(m_detailEnt.m_rootDeptId), m_detailEnt.m_name);
    if (!treeResult.Succeeded())
    {
        ShowToast(_T("加载企业详情失败，请重试"), ToastKind::Error);
        FA_LOGW(_T("解析组织架构树失败：%s"), treeResult.m_message.GetString());
        return;
    }
    m_detailTree = treeResult.m_root;

    // 把真实数据注入 DataStore，再打开详情屏（详情屏各控件仍读 DataStore）。
    DataStore::Instance().InjectEnterpriseDetail(m_detailEnt, m_detailTree,
                                                 m_detailServices);
    if (m_enterpriseDetail != nullptr)
    {
        m_enterpriseDetail->SetEnterprise(entId);
        // 行菜单「打开详情 → 服务号」会先把 m_detailInitialTab 置为服务号；
        // SetEnterprise 默认停在组织架构 tab，这里按需切到目标 tab。
        if (m_detailInitialTab != kDetailTabOrg)
        {
            m_enterpriseDetail->SelectSubTab(m_detailInitialTab);
        }
        SwitchScreen(Screen::EnterpriseDetail);
        // 服务号列表不带绑定：为默认选中的服务号读一次绑定，供面板显示。
        FetchSelectedServiceBindingsIfNeeded();
    }
    m_detailInitialTab = kDetailTabOrg;   // 用后复位
    FA_LOGI(_T("企业详情已加载并打开：%s"), entId.GetString());
}

void AdminFrame::ShowNodeModal()
{
    // 已经开着就不再开第二个（连点「新增员工」两次会走到这里）。
    if (m_nodeModal != nullptr)
    {
        return;
    }

    // 对话框已改成独立顶层窗口：它自带真模态（EnableWindow(主窗, FALSE)），
    // 顶层窗口天然盖得住主窗口的内容，故不需要像 overlay 弹窗那样先
    // SetEditsVisible(m_shellPage, false) 去隐藏外壳里的输入框。附带说明：
    // 输入框已是无窗口控件，那道隐藏动作对 overlay 弹窗也不再必要。
    m_nodeModal = new NodeModal(
        this, m_hWnd,
        m_nodeModalPendingIsDept ? NodeModal::Mode::AddDept
                                 : NodeModal::Mode::AddUser,
        m_nodeModalParentName);
    if (m_nodeModalPendingIsDept && m_enterpriseDetail != nullptr)
    {
        // 负责人从本企业员工里选：候选人取自已加载的组织树（树未加载时只有「不设置」）。
        m_nodeModal->SetLeaderCandidates(orgtree::CollectLeaderCandidates(
            DataStore::Instance().OrgTree(m_enterpriseDetail->GetEnterpriseId())));
    }
    if (!m_nodeModal->CreateAndShow())
    {
        delete m_nodeModal;
        m_nodeModal = nullptr;
        ShowToast(_T("打开对话框失败"), ToastKind::Warn);
        return;
    }
    m_nodeSubmitBusy = false;
    FA_LOGI(_T("打开组织节点对话框（%s）"),
            m_nodeModalPendingIsDept ? _T("新增部门") : _T("新增员工"));
}

void AdminFrame::HideNodeModal()
{
    // 关闭走对话框自己的 WM_CLOSE → WM_DESTROY，收口在 OnNodeModalDestroyed：
    // 那里恢复主窗口 Enable、清指针、安排延迟 delete。本方法只负责发起。
    if (m_nodeModal != nullptr)
    {
        m_nodeModal->RequestClose();
    }
    FA_LOGI(_T("关闭组织节点对话框"));
}

void AdminFrame::ShowGroupModal()
{
    if (m_groupModal == nullptr || m_root == nullptr)
    {
        return;
    }
    // 企业与成员列表已在 OnDuiNotify 里经 SetEnterprise 装好。隐藏外壳内的
    // 输入框，再显示弹窗；输入框改为无窗口控件之后这道隐藏已无必要，此处保留
    // 只是尚未清理。
    SetEditsVisible(m_shellPage, false);
    m_root->ShowModal(m_groupModal);
    FA_LOGI(_T("打开新建群弹窗"));
}

void AdminFrame::HideGroupModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
    FA_LOGI(_T("关闭新建群弹窗"));
}

void AdminFrame::SubmitGroupModal()
{
    if (m_groupModal == nullptr || m_enterpriseDetail == nullptr)
    {
        return;
    }
    if (!m_groupModal->Validate())
    {
        ShowToast(_T("请填写群名称并指定群主"));
        FA_LOGW(_T("新建群弹窗校验未通过"));
        return;
    }
    if (m_groupSubmitBusy)
    {
        ShowToast(_T("正在提交，请稍候…"));
        return;
    }

    const ServerConfig& server = AppConfig::Instance().Server();
    const CString       token  = AdminSession::Instance().Token();
    const long long     entId  = _ttoi64(m_groupModal->GetEnterpriseId());
    CString name = m_groupModal->GetGroupName();
    name.Trim();

    m_groupSubmitBusy = true;
    HttpRequest request = adminapi::MakeGroupCreateRequest(
        server, token, entId, name, m_groupModal->GetOwnerId(),
        m_groupModal->GetAdminIds(), m_groupModal->GetMemberIds());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagGroupCreate;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在创建群…"));
    FA_LOGI(_T("提交新建群：%s"), name.GetString());
}

void AdminFrame::OnGroupCreated(const HttpResult& http)
{
    m_groupSubmitBusy = false;
    const GroupCreateResult r = adminapi::ParseGroupCreateResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);   // 失败保留弹窗，让用户改后重试
        FA_LOGW(_T("新建群失败：%s"), r.m_message.GetString());
        return;
    }
    HideGroupModal();
    ShowToast(_T("群已创建，成员将在自己的群组界面看到该群"));
    FA_LOGI(_T("新建群成功：groupid=%lld"), r.m_groupId);
}

void AdminFrame::OnEmployeeEditDialogSubmit(EmployeeEditDialog* dlg)
{
    // dialog 内已经做过姓名 + 邮箱必填校验,本方法不再重复校验,只在 busy /
    // 重复点击场景下做幂等保护。
    if (dlg == nullptr || dlg != m_employeeEditDialog)
    {
        return;
    }
    if (m_empEditBusy)
    {
        ShowToast(_T("正在提交,请稍候…"));
        return;
    }
    m_empEditBusy = true;

    HttpRequest request = adminapi::MakeUserUpdateRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(dlg->GetUserId()),
        dlg->GetName(),       dlg->GetTitle(),
        dlg->GetEmployeeNo(), dlg->GetJoinDate(),
        dlg->GetLandline(),   dlg->GetWorkstation(),
        dlg->GetPhone(),      dlg->GetEmail(),
        dlg->GetSignature(),  dlg->GetGenderCode());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagUserUpdate;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在保存…"));
    FA_LOGI(_T("提交修改员工资料"));
}

void AdminFrame::OnEmployeeEditDialogDestroyed(EmployeeEditDialog* dlg)
{
    // dialog WM_DESTROY 时调用 —— 不论用户取消、点 X 关闭、还是提交成功后
    // RequestClose,都会走到这里。只清自己持有的 raw 指针(dialog 的 delete
    // 由 OnUserUpdated 的成功路径处理;取消 / X 关闭路径下我们在此 delete)。
    if (dlg != nullptr && dlg == m_employeeEditDialog)
    {
        m_employeeEditDialog = nullptr;
        // 取消 / X 关闭路径:不会再有 OnUserUpdated 来 delete,在此自杀(this dlg)。
        // 通过 PostMessage 把 delete 推迟到当前 WM_DESTROY 栈展开后,避免在
        // dialog 自己的 message handler 里就把对象析构掉。
        ::PostMessage(m_hWnd, WM_APP_DELETE_EMP_EDIT_DIALOG,
                      reinterpret_cast<WPARAM>(dlg), 0);
    }
}

void AdminFrame::OnUserDetailLoaded(const HttpResult& http)
{
    const UserDetailResult r = adminapi::ParseUserDetailResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("加载员工资料失败：%s"), r.m_message.GetString());
        return;
    }
    // 重复打开保护:已经有 dialog 存在则直接忽略本次拉详情结果(简化语义,
    // 生产路径几乎不出现并发拉详情)。
    if (m_employeeEditDialog != nullptr)
    {
        FA_LOGW(_T("已存在打开的修改员工资料对话框,忽略本次拉详情结果"));
        return;
    }
    // on-demand 创建独立顶层对话框 —— 真模态启用(主窗被禁用),居中显示。
    // dialog 自己持有 9 个 EDIT + 标题栏 + 关闭按钮;balloonui 负责整套
    // paint,不再有 modal overlay 时序问题。
    std::unique_ptr<EmployeeEditDialog> dlg(
        new EmployeeEditDialog(this, m_hWnd, r.m_user));
    if (!dlg->CreateAndShow())
    {
        FA_LOGW(_T("修改员工资料对话框 CreateAndShow 失败"));
        return;
    }
    // ownership 转给 m_employeeEditDialog;清空与 delete 由
    // OnEmployeeEditDialogDestroyed / OnUserUpdated 处理。
    m_employeeEditDialog = dlg.release();
    FA_LOGI(_T("打开修改员工资料对话框"));
}

void AdminFrame::OnUserUpdated(const HttpResult& http)
{
    m_empEditBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);   // 失败保留弹窗
        if (m_employeeEditDialog != nullptr)
        {
            m_employeeEditDialog->SetBusy(false);   // 解除"保存中"
        }
        FA_LOGW(_T("修改员工资料失败：%s"), r.m_message.GetString());
        return;
    }
    // 成功:RequestClose 让 dialog 走完 WM_CLOSE → WM_DESTROY 闭环;
    // OnEmployeeEditDialogDestroyed 会把 m_employeeEditDialog 清 nullptr,
    // 并经 WM_APP_DELETE_EMP_EDIT_DIALOG 异步把 dialog 实例 delete 掉。
    if (m_employeeEditDialog != nullptr)
    {
        m_employeeEditDialog->RequestClose();
    }
    ShowToast(_T("员工资料已保存"));
    // 姓名 / 职位 等变化反映到组织树 → 重拉刷新。
    ReloadOrgTree();
}

void AdminFrame::ShowDeptPickerMenu()
{
    if (m_enterpriseDetail == nullptr || m_moveUserId.IsEmpty())
    {
        return;
    }
    const CString entId = m_enterpriseDetail->GetEnterpriseId();
    const OrgNode* root = DataStore::Instance().OrgTree(entId);
    if (root == nullptr)
    {
        return;
    }
    // 员工当前所属部门：用于从候选里排除，并作为 move.do 的 deptid。
    const CString curDeptId = FindParentDeptId(root, m_moveUserId);

    // 收集所有可调入部门（按层级缩进，排除当前部门）。
    std::vector<DeptPick> depts;
    CollectDepts(root, 0, curDeptId, depts);
    if (depts.empty())
    {
        ShowToast(_T("没有其它可调入的部门"), ToastKind::Warn);
        return;
    }

    // 用弹出菜单挑选目标部门；菜单项 ID 取 1..N，对应 depts 下标 + 1。
    DuiMenu menu;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        menu.AppendItem(static_cast<UINT>(i + 1), depts[i].m_name);
    }
    const UINT chosen = menu.TrackPopup(m_deptMenuPt.x, m_deptMenuPt.y, m_hWnd);
    if (chosen == 0 || chosen > depts.size())
    {
        return;   // 用户未选
    }
    const CString targetDeptId = depts[chosen - 1].m_id;

    HttpRequest request = adminapi::MakeUserMoveRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(m_moveUserId), _ttoi64(curDeptId), _ttoi64(targetDeptId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagUserMove;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在调整部门…"));
}

void AdminFrame::OnUserMoved(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("调整员工部门失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("员工部门已调整"));
    // 员工所属部门变化 → 重拉组织树刷新。
    ReloadOrgTree();
}

void AdminFrame::OpenEnterpriseSettings(const Enterprise& ent)
{
    if (m_enterpriseSettingsModal == nullptr)
    {
        return;
    }
    // 记下目标企业（企业列表行菜单 / 企业详情屏头部共用此入口），先拉
    // 管理员列表，应答回来后在 OnEnterpriseAdminsLoaded 里填充并打开弹窗。
    m_entSettingsEnt  = ent;
    m_entSettingsOpen = false;
    FireEnterpriseAdmins();
}

void AdminFrame::HideEnterpriseSettings()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_entSettingsOpen = false;
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
    FA_LOGI(_T("关闭企业设置弹窗"));
}

void AdminFrame::FireEnterpriseAdmins()
{
    // 目标企业在 OpenEnterpriseSettings 里已写入 m_entSettingsEnt。
    const CString entId = m_entSettingsEnt.m_id;
    if (entId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeEnterpriseAdminsRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(entId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagEntAdmins;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnEnterpriseAdminsLoaded(const HttpResult& http)
{
    if (m_enterpriseSettingsModal == nullptr)
    {
        return;
    }
    const EnterpriseAdminsResult r =
        adminapi::ParseEnterpriseAdminsResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("加载企业管理员失败：%s"), r.m_message.GetString());
        return;
    }
    if (m_entSettingsOpen)
    {
        // 弹窗已打开（增 / 删管理员后的刷新）：仅更新列表。
        m_enterpriseSettingsModal->SetAdmins(r.m_list);
        return;
    }
    // 首次打开：用 m_entSettingsEnt（行菜单 / 详情屏入口都已填好）连同
    // 管理员列表填充并显示弹窗。
    m_enterpriseSettingsModal->SetData(m_entSettingsEnt, r.m_list);
    SetEditsVisible(m_shellPage, false);
    m_root->ShowModal(m_enterpriseSettingsModal);
    m_entSettingsOpen = true;
    FA_LOGI(_T("打开企业设置弹窗"));
}

void AdminFrame::SubmitEnterpriseUpdate()
{
    if (m_enterpriseSettingsModal == nullptr)
    {
        return;
    }
    if (!m_enterpriseSettingsModal->ValidateEnterprise())
    {
        ShowToast(_T("请填写企业全称"), ToastKind::Warn);
        return;
    }
    if (!m_enterpriseSettingsModal->ValidateAccountPrefix())
    {
        ShowToast(_T("账号前缀只能由字母和点号组成，1～10 个字符"), ToastKind::Warn);
        return;
    }
    if (m_entUpdateBusy)
    {
        ShowToast(_T("正在提交，请稍候…"));
        return;
    }
    m_entUpdateBusy = true;
    // 记下这次提交的新值：保存成功后写回企业缓存（OnEnterpriseUpdated），详情屏与下次打开设置窗口都显示新值
    m_entUpdatePending = m_entSettingsEnt;
    m_entUpdatePending.m_name      = m_enterpriseSettingsModal->GetName();
    m_entUpdatePending.m_shortName = m_enterpriseSettingsModal->GetShortName();
    m_entUpdatePending.m_industry  = m_enterpriseSettingsModal->GetIndustry();
    m_entUpdatePending.m_region    = m_enterpriseSettingsModal->GetRegion();
    m_entUpdatePending.m_plan      = adminapi::PlanNameFromInt(m_enterpriseSettingsModal->GetPlan());
    adminapi::NormalizeAccountPrefix(m_enterpriseSettingsModal->GetAccountPrefix(), m_entUpdatePending.m_accountPrefix);
    HttpRequest request = adminapi::MakeEnterpriseUpdateRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(m_enterpriseSettingsModal->GetEnterpriseId()),
        m_enterpriseSettingsModal->GetName(),
        m_enterpriseSettingsModal->GetShortName(),
        m_enterpriseSettingsModal->GetIndustry(),
        m_enterpriseSettingsModal->GetRegion(),
        m_enterpriseSettingsModal->GetPlan(),
        m_enterpriseSettingsModal->GetAccountPrefix());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagEntUpdate;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在保存企业信息…"));
}

void AdminFrame::OnEnterpriseUpdated(const HttpResult& http)
{
    m_entUpdateBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);   // 失败保留弹窗
        FA_LOGW(_T("保存企业信息失败：%s"), r.m_message.GetString());
        return;
    }
    HideEnterpriseSettings();
    ShowToast(_T("企业信息已保存"));
    // 新值写回企业缓存：此前从企业详情屏保存后缓存里仍是旧记录，详情屏头部与再次打开设置窗口都显示旧值
    m_entSettingsEnt = m_entUpdatePending;
    if (DataStore::Instance().UpdateEnterpriseRecord(m_entUpdatePending) && m_root != nullptr)
    {
        m_root->Invalidate();
    }
    // 若企业设置是从企业列表打开的，刷新列表以反映改后的名称 / 套餐等。
    if (m_route == Screen::Enterprises && m_enterprises != nullptr)
    {
        m_enterprises->Refresh();
    }
}

void AdminFrame::SubmitAddAdmin()
{
    if (m_enterpriseSettingsModal == nullptr)
    {
        return;
    }
    if (!m_enterpriseSettingsModal->ValidateNewAdmin())
    {
        ShowToast(_T("请填写管理员姓名与邮箱"), ToastKind::Warn);
        return;
    }
    if (m_addAdminBusy)
    {
        ShowToast(_T("正在提交，请稍候…"));
        return;
    }
    m_addAdminBusy = true;
    HttpRequest request = adminapi::MakeEnterpriseAddAdminRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(m_enterpriseSettingsModal->GetEnterpriseId()),
        m_enterpriseSettingsModal->GetNewAdminName(),
        m_enterpriseSettingsModal->GetNewAdminEmail());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagEntAddAdmin;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在新增管理员…"));
}

void AdminFrame::OnEnterpriseAdminAdded(const HttpResult& http)
{
    m_addAdminBusy = false;
    const EnterpriseAddAdminResult r =
        adminapi::ParseEnterpriseAddAdminResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);   // 失败保留弹窗
        FA_LOGW(_T("新增管理员失败：%s"), r.m_message.GetString());
        return;
    }
    // 单弹窗槽位：先关企业设置弹窗，再用信息弹窗展示初始密码。
    HideEnterpriseSettings();
    CString msg;
    msg.Format(_T("管理员已新增。\n初始登录密码：%s\n")
               _T("请妥善保存并转交，此密码仅显示一次。"),
               static_cast<LPCTSTR>(r.m_initialPassword));
    m_confirmDeleteNodeId.Empty();
    ConfirmDialogSpec spec;
    spec.m_infoMode = true;
    spec.m_title    = _T("管理员新增成功");
    spec.m_message  = msg;
    spec.m_okText   = _T("知道了");
    spec.m_copyText = r.m_initialPassword;
    ShowConfirmDialog(spec);
}

void AdminFrame::OnEnterpriseAdminRemoved(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("移除管理员失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("管理员已移除"));
    // 弹窗仍打开 → 重拉管理员列表刷新。
    FireEnterpriseAdmins();
}

void AdminFrame::OnNodeModalSubmit(NodeModal* dlg)
{
    // 必填校验已由对话框在 SubmitOrShake 里做过（不通过就不会调到这里），
    // 本方法只做幂等保护与实际的 HTTP 提交。
    if (dlg == nullptr || dlg != m_nodeModal || m_enterpriseDetail == nullptr)
    {
        return;
    }

    if (m_nodeSubmitBusy)
    {
        ShowToast(_T("正在提交，请稍候…"));
        return;
    }

    const bool isDept = (m_nodeModal->GetMode() == NodeModal::Mode::AddDept);

    const ServerConfig& server = AppConfig::Instance().Server();
    const CString       token  = AdminSession::Instance().Token();
    const long long     entId  = _ttoi64(m_enterpriseDetail->GetEnterpriseId());
    // 父节点 ID。企业完成迁移后，组织树的根是代表公司本身的真实部门，这里拿到的
    // 就是一个可直接发给服务端的部门 ID。只有尚未迁移、树根还是历史合成节点时才
    // 会取到 kSyntheticRootId，此时发 0：服务端会把 0 落到该企业的公司根部门，
    // 建不出根部门时明确报错，不会再像过去那样静默建出无部门归属的员工。
    const long long parentId =
        (m_nodeModalParentId == CString(orgtree::kSyntheticRootId))
            ? 0 : _ttoi64(m_nodeModalParentId);

    m_nodeSubmitBusy = true;
    if (isDept)
    {
        CString name = m_nodeModal->GetDeptName();
        name.Trim();
        // deptdesc / deptorder 当前弹窗未提供，留空 / 0；负责人 0 表示不设置
        // （无法确定为某一名员工的情况已在弹窗的 Validate 里拦下）。
        const long long leaderId = m_nodeModal->GetDeptLeaderUserId();
        HttpRequest request = adminapi::MakeDeptCreateRequest(
            server, token, entId, parentId, name, CString(),
            (leaderId > 0) ? leaderId : 0, 0);
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagDeptCreate;
        HttpWorker::Instance().Post(request);
        ShowToast(_T("正在创建部门…"));
        FA_LOGI(_T("提交新增部门：%s"), name.GetString());
    }
    else
    {
        CString name = m_nodeModal->GetUserName();
        name.Trim();
        // employeeno / joindate 当前弹窗未提供，留空由服务端处理。
        HttpRequest request = adminapi::MakeUserCreateRequest(
            server, token, entId, parentId, name,
            m_nodeModal->GetUserTitle(), CString(),
            m_nodeModal->GetUserEmail(), m_nodeModal->GetUserPhone(),
            CString(), m_nodeModal->GetUserGenderCode());
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagUserCreate;
        HttpWorker::Instance().Post(request);
        ShowToast(_T("正在创建员工…"));
        FA_LOGI(_T("提交新增员工：%s"), name.GetString());
    }
}

void AdminFrame::OnNodeModalDestroyed(NodeModal* dlg)
{
    // 对话框 WM_DESTROY 时调用 —— 不论取消、点 X、还是创建成功后 RequestClose，
    // 都会走到这里。清掉自己持有的裸指针，并把 delete 推迟到当前 WM_DESTROY
    // 栈展开之后，避免在对话框自己的消息处理里就把对象析构掉。
    if (dlg != nullptr && dlg == m_nodeModal)
    {
        m_nodeModal      = nullptr;
        //对话框没了，提交中的标志一并复位，否则下次开对话框会被误判成"正在提交"。
        m_nodeSubmitBusy = false;
        ::PostMessage(m_hWnd, WM_APP_DELETE_NODE_MODAL,
                      reinterpret_cast<WPARAM>(dlg), 0);
    }
}

void AdminFrame::OnDeptCreated(const HttpResult& http)
{
    m_nodeSubmitBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        //失败保留对话框让用户改后重试，但要把「创建中…」按钮恢复成可点，
        //否则用户改完了也按不动。
        if (m_nodeModal != nullptr)
        {
            m_nodeModal->SetBusy(false);
        }
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("新增部门失败：%s"), r.m_message.GetString());
        return;
    }
    HideNodeModal();
    ShowToast(_T("部门已创建"));
    ReloadOrgTree();
}

void AdminFrame::OnUserCreated(const HttpResult& http)
{
    m_nodeSubmitBusy = false;
    const UserCreateResult r = adminapi::ParseUserCreateResponse(http);
    if (!r.Succeeded())
    {
        //同 OnDeptCreated：失败保留对话框，但把提交按钮恢复成可点。
        if (m_nodeModal != nullptr)
        {
            m_nodeModal->SetBusy(false);
        }
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("新增员工失败：%s"), r.m_message.GetString());
        return;
    }
    HideNodeModal();
    ReloadOrgTree();

    // 展示员工登录账号 + 初始密码（与新增企业一致：信息弹窗 + 复制按钮）。
    CString msg;
    msg.Format(_T("员工已创建。\n登录账号：%s\n初始登录密码：%s\n")
               _T("请妥善保存并转交，此密码仅显示一次。"),
               static_cast<LPCTSTR>(r.m_loginAccount),
               static_cast<LPCTSTR>(r.m_initialPassword));
    m_confirmDeleteNodeId.Empty();
    ConfirmDialogSpec spec;
    spec.m_infoMode = true;
    spec.m_title    = _T("员工创建成功");
    spec.m_message  = msg;
    spec.m_okText   = _T("知道了");
    spec.m_copyText = r.m_initialPassword;
    ShowConfirmDialog(spec);
    FA_LOGI(_T("新增员工成功：userid=%lld"), r.m_userId);
}

void AdminFrame::OnOrgNodeDeleted(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("删除组织节点失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("已删除"));
    ReloadOrgTree();
}

void AdminFrame::ReloadOrgTree(bool showToastOnDone)
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    const CString entId = m_enterpriseDetail->GetEnterpriseId();
    if (entId.IsEmpty())
    {
        return;
    }
    m_orgTreeReloading   = true;
    m_orgTreeReloadToast = showToastOnDone;

    HttpRequest request = adminapi::MakeDeptTreeRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(entId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagOrgTreeReload;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("重新拉取企业组织树：%s"), entId.GetString());
}

void AdminFrame::OnOrgTreeReloaded(const HttpResult& http)
{
    // 无论成功失败都先解除在途标记，否则一次失败会把「刷新」按钮永久卡住。
    const bool showToast = m_orgTreeReloadToast;
    m_orgTreeReloading   = false;
    m_orgTreeReloadToast = false;

    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    // 组装组织树要用到该企业的公司根部门 ID 与企业名，先从 DataStore 取出来。
    const CString entId = m_enterpriseDetail->GetEnterpriseId();
    const Enterprise* ent = DataStore::Instance().FindEnterprise(entId);
    long long rootDeptId = 0;
    CString entName;
    if (ent != nullptr)
    {
        rootDeptId = _ttoi64(ent->m_rootDeptId);
        entName = ent->m_name;
    }

    const DeptTreeResult r = adminapi::ParseDeptTreeResponse(http, rootDeptId, entName);
    if (!r.Succeeded())
    {
        ShowToast(_T("刷新组织架构失败"), ToastKind::Error);
        FA_LOGW(_T("刷新组织树失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetOrgTree(entId, r.m_root);
    m_enterpriseDetail->Refresh();
    if (showToast)
    {
        ShowToast(_T("组织架构已刷新"));
    }
}

bool AdminFrame::HandleEscapeKey()
{
    if (m_root == nullptr)
    {
        return false;
    }
    DuiControl* modal = m_root->GetVisibleModal();
    if (modal == nullptr || HasOpenComboPopup(modal))
    {
        return false;   // 没有弹窗；或弹窗里有下拉框展开着，Esc 交给下拉框自己收起列表
    }
    UINT closeId = 0;
    if (modal == m_modal)
    {
        closeId = kIdModalClose;
    }
    else if (modal == m_serviceModal)
    {
        closeId = kIdSvcClose;
    }
    else if (modal == m_bindingModal)
    {
        closeId = kIdBindClose;
    }
    else if (modal == m_composeModal)
    {
        // 已经写了标题或正文时不关闭：长文一按 Esc 就没了，代价太大。
        if (!m_composeModal->GetTitle().IsEmpty() || !m_composeModal->GetBody().IsEmpty())
        {
            FA_LOGI(_T("escape ignored: compose modal has content"));
            return true;
        }
        closeId = kIdComposeClose;
    }
    else if (modal == m_groupModal)
    {
        closeId = kIdGroupClose;
    }
    else if (modal == m_clientVersionModal)
    {
        closeId = kIdCvModalClose;
    }
    else if (modal == m_swModal)
    {
        closeId = kIdSwModalClose;
    }
    else if (modal == m_qaModal)
    {
        closeId = kIdQaModalClose;
    }
    else if (modal == m_botModal)
    {
        closeId = kIdBotModalClose;
    }
    else if (modal == m_botRuleModal)
    {
        closeId = kIdBotRuleClose;
    }
    else if (modal == m_botDomainsModal)
    {
        closeId = kIdBotDomainsClose;
    }
    else if (modal == m_swImportModal)
    {
        closeId = kIdSwImpClose;
    }
    else if (modal == m_enterpriseSettingsModal)
    {
        closeId = kIdEntSetClose;
    }
    if (closeId == 0)
    {
        return false;
    }
    DuiControl* btn = modal->FindCtrlById(closeId);
    if (btn == nullptr || !btn->IsVisible() || !btn->IsEnabled())
    {
        return true;   // 关闭按钮此刻不可用（例如正在提交），按键吃掉、不做别的
    }
    FA_LOGI(_T("escape closes modal via button %u"), closeId);
    btn->NotifyParent(DUIN_CLICK);
    return true;
}

bool AdminFrame::HandleEditEnter(const DuiNotify* n)
{
    UINT buttonId = 0;
    const size_t ruleCount = sizeof(kEnterSubmitRules) / sizeof(kEnterSubmitRules[0]);
    for (size_t i = 0; i < ruleCount; ++i)
    {
        if (kEnterSubmitRules[i].m_editId == n->ctrlId)
        {
            buttonId = kEnterSubmitRules[i].m_buttonId;
            break;
        }
    }
    // 只认对照表里的输入框，且发送者必须真是输入框：别的控件的同值通知码不归这里处理。
    if (buttonId == 0 || dynamic_cast<DuiEdit*>(static_cast<DuiControl*>(n->pCtrl)) == nullptr)
    {
        return false;
    }
    ::PostMessage(m_hWnd, WM_APP_CLICK_BUTTON, static_cast<WPARAM>(buttonId), 0);
    return true;
}

LRESULT AdminFrame::OnClickButtonMsg(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
{
    bHandled = TRUE;
    const UINT buttonId = static_cast<UINT>(wParam);
    // 按钮只在当前可见的弹窗或登录页里找，避免点到藏在后面的同 ID 按钮。
    DuiControl* container = (m_root != nullptr) ? m_root->GetVisibleModal() : nullptr;
    if (container == nullptr && m_loginPage != nullptr && m_loginPage->IsVisible())
    {
        container = m_loginPage;
    }
    if (container == nullptr)
    {
        return 0;
    }
    DuiControl* btn = container->FindCtrlById(buttonId);
    if (btn != nullptr && btn->IsVisible() && btn->IsEnabled())
    {
        FA_LOGI(_T("enter submits via button %u"), buttonId);
        btn->NotifyParent(DUIN_CLICK);
    }
    return 0;
}

void AdminFrame::ShowConfirmDialog(const ConfirmDialogSpec& spec)
{
    // 重复打开保护:已有 dialog 存在则先把旧的 RequestClose,本次请求当作
    // 用户新意图(简化语义,生产路径几乎不出现并发确认)。
    if (m_confirmDialog != nullptr)
    {
        m_confirmDialog->RequestClose();
        // 旧 dialog 走 WM_DESTROY 时会清 m_confirmDialog,这里立刻清以便新
        // 实例直接占位。旧实例 PostMessage 异步 delete 由其 OnDestroyMsg 排队。
        m_confirmDialog = nullptr;
    }
    std::unique_ptr<ConfirmDialog> dlg(
        new ConfirmDialog(this, m_hWnd, spec));
    if (!dlg->CreateAndShow())
    {
        FA_LOGW(_T("确认对话框 CreateAndShow 失败"));
        return;
    }
    m_confirmDialog = dlg.release();
    FA_LOGI(_T("打开确认对话框:%s"), spec.m_title.GetString());
}

void AdminFrame::OnConfirmDialogDestroyed(ConfirmDialog* dlg)
{
    // dialog WM_DESTROY 时调用 —— 取消 / X 关闭 / 确认后 RequestClose 都
    // 走到这里。清 raw 指针 + PostMessage 异步 delete 实例。
    if (dlg != nullptr && dlg == m_confirmDialog)
    {
        m_confirmDialog = nullptr;

        // 敏感词的两种确认框：点了确认时 OnConfirmDialogConfirmed 已清空 m_confirmDeleteNodeId；
        // 走到这里仍非空，说明是取消或直接关掉，告诉敏感词界面（关闭总开关被取消时开关要拨回去）
        //文件共享管理的确认框：走到这里仍有待确认的操作，说明是取消或直接关掉，告诉发起方（开关要拨回去）
        if (m_confirmTarget == kConfirmTargetFileShare && !m_confirmDeleteNodeId.IsEmpty())
        {
            m_confirmDeleteNodeId.Empty();
            fsui::ConfirmTarget* target = m_fsConfirmTarget;
            m_fsConfirmTarget = nullptr;
            if (target != nullptr)
            {
                target->OnConfirmResult(false);
            }
        }
        if (m_confirmTarget == kConfirmTargetSetRole && !m_confirmDeleteNodeId.IsEmpty())
        {
            const CString userId = m_confirmDeleteNodeId;
            m_confirmDeleteNodeId.Empty();
            FireGetPermissions(userId);
            FA_LOGI(_T("role change cancelled, reload permissions: userid=%s"), userId.GetString());
        }
        if (m_confirmTarget == kConfirmTargetQaDelete && !m_confirmDeleteNodeId.IsEmpty())
        {
            m_confirmDeleteNodeId.Empty();
            if (m_qaScreen != nullptr)
            {
                m_qaScreen->OnConfirmDelete(false);
            }
        }
        if ((m_confirmTarget == kConfirmTargetSwDelete || m_confirmTarget == kConfirmTargetSwDisable)
            && !m_confirmDeleteNodeId.IsEmpty())
        {
            const int kind = (m_confirmTarget == kConfirmTargetSwDelete) ? SensitiveWordsScreen::kUiConfirmDelete
                                                                         : SensitiveWordsScreen::kUiConfirmDisable;
            m_confirmDeleteNodeId.Empty();
            if (m_sensitiveWordsScreen != nullptr)
            {
                m_sensitiveWordsScreen->OnConfirmResult(kind, false);
            }
        }
    }
    if (dlg != nullptr)
    {
        ::PostMessage(m_hWnd, WM_APP_DELETE_CONFIRM_DIALOG,
                      reinterpret_cast<WPARAM>(dlg), 0);
    }
}

void AdminFrame::OnConfirmDialogConfirmed(ConfirmDialog* dlg)
{
    // 用户点「确认 / 知道了」:先让 dialog RequestClose(WM_CLOSE 入队),
    // 再按 m_confirmTarget 状态机执行待确认操作。dialog 的 WM_DESTROY 会
    // 在当前栈展开后异步处理,不会跟下面的逻辑互掐。
    if (dlg == nullptr || dlg != m_confirmDialog)
    {
        return;
    }
    dlg->RequestClose();

    if (m_confirmDeleteNodeId.IsEmpty())
    {
        return;
    }
    const CString id = m_confirmDeleteNodeId;
    m_confirmDeleteNodeId.Empty();

    // 文件共享管理：切换功能开关、强制取消共享，交给发起确认的面板或界面发请求
    if (m_confirmTarget == kConfirmTargetFileShare)
    {
        fsui::ConfirmTarget* target = m_fsConfirmTarget;
        m_fsConfirmTarget = nullptr;
        if (target != nullptr)
        {
            target->OnConfirmResult(true);
        }
        return;
    }

    // 企业问答：删除一条，交给企业问答界面发请求（待删的问答由界面自己记着）
    if (m_confirmTarget == kConfirmTargetQaDelete)
    {
        m_confirmDeleteNodeId.Empty();
        if (m_qaScreen != nullptr)
        {
            m_qaScreen->OnConfirmDelete(true);
        }
        return;
    }

    // 敏感词：删除词条 / 关闭总开关，交给敏感词界面发请求（待删的词条 ID 由界面自己记着）
    if (m_confirmTarget == kConfirmTargetSwDelete || m_confirmTarget == kConfirmTargetSwDisable)
    {
        if (m_sensitiveWordsScreen != nullptr)
        {
            m_sensitiveWordsScreen->OnConfirmResult(
                (m_confirmTarget == kConfirmTargetSwDelete) ? SensitiveWordsScreen::kUiConfirmDelete
                                                            : SensitiveWordsScreen::kUiConfirmDisable,
                true);
        }
        return;
    }

    if (m_confirmTarget == 2)        // 公告：按状态走撤回 / 删除接口
    {
        if (m_annActionBusy)
        {
            ShowToast(_T("正在处理，请稍候…"));
            return;
        }
        // 已发布(status==1)→撤回；草稿 / 已撤回→删除。
        int annStatus = 0;
        for (const Announcement& a : DataStore::Instance().Announcements())
        {
            if (a.m_id == id)
            {
                annStatus = a.m_status;
                break;
            }
        }
        m_annActionBusy = true;
        HttpRequest request;
        if (annStatus == 1)
        {
            request = adminapi::MakeAnnRecallRequest(
                AppConfig::Instance().Server(),
                AdminSession::Instance().Token(), _ttoi64(id));
            request.m_tag = kApiTagAnnRecall;
        }
        else
        {
            request = adminapi::MakeAnnDeleteRequest(
                AppConfig::Instance().Server(),
                AdminSession::Instance().Token(), _ttoi64(id));
            request.m_tag = kApiTagAnnDelete;
        }
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        HttpWorker::Instance().Post(request);
        ShowToast(annStatus == 1 ? _T("正在撤回公告…") : _T("正在删除公告…"));
        return;
    }

    if (m_confirmTarget == kConfirmTargetAnnDelete)    // 公告：任意状态物理删除
    {
        if (m_annActionBusy)
        {
            ShowToast(_T("正在处理，请稍候…"));
            return;
        }
        m_annActionBusy = true;
        HttpRequest request = adminapi::MakeAnnDeleteRequest(
            AppConfig::Instance().Server(),
            AdminSession::Instance().Token(), _ttoi64(id));
        request.m_tag       = kApiTagAnnDelete;
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        HttpWorker::Instance().Post(request);
        ShowToast(_T("正在删除公告…"));
        return;
    }

    if (m_confirmTarget == kConfirmTargetEntSuspend)
    {
        SubmitEnterpriseStatus(id, kEntStatusSuspended);
        return;
    }
    if (m_confirmTarget == kConfirmTargetRemoveAdmin)
    {
        SubmitRemoveAdmin(_ttoi64(id));
        return;
    }
    if (m_confirmTarget == kConfirmTargetKick)
    {
        SubmitKickDevices(id);
        return;
    }
    if (m_confirmTarget == kConfirmTargetSetRole)
    {
        SubmitUserRole(id);
        return;
    }
    if (m_confirmTarget == kConfirmTargetSvcDisable)
    {
        SubmitServiceStatus(id, 1);
        return;
    }
    if (m_confirmTarget == kConfirmTargetAnnRepush)
    {
        SubmitAnnouncementRepush(id);
        return;
    }
    if (m_confirmTarget == kConfirmTargetCvPublish || m_confirmTarget == kConfirmTargetCvDisable)
    {
        // 客户端版本的发布 / 撤回：确认后才真正发请求。
        const CvRowAction action = (m_confirmTarget == kConfirmTargetCvPublish)
                                       ? CvRowAction::Publish : CvRowAction::Disable;
        StartClientVersionAction(static_cast<int>(action), _ttoi64(id));
        return;
    }
    if (m_confirmTarget == kConfirmTargetEnterprise)   // 企业（物理删除）
    {
        HttpRequest request = adminapi::MakeEnterpriseDeleteRequest(
            AppConfig::Instance().Server(), AdminSession::Instance().Token(),
            _ttoi64(id));
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagEntDelete;
        HttpWorker::Instance().Post(request);
        ShowToast(_T("正在删除企业…"));
        return;
    }

    if (m_confirmTarget == kConfirmTargetResetPwd)     // 重置员工登录密码
    {
        HttpRequest request = adminapi::MakeUserResetPasswordRequest(
            AppConfig::Instance().Server(), AdminSession::Instance().Token(),
            _ttoi64(id));
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagUserResetPwd;
        HttpWorker::Instance().Post(request);
        ShowToast(_T("正在重置密码…"));
        return;
    }

    if (m_confirmTarget == kConfirmTargetSetStatus)    // 启用 / 停用员工
    {
        HttpRequest request = adminapi::MakeUserSetStatusRequest(
            AppConfig::Instance().Server(), AdminSession::Instance().Token(),
            _ttoi64(id), m_confirmStatusTarget);
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagUserSetStatus;
        HttpWorker::Instance().Post(request);
        ShowToast(m_confirmStatusTarget == 1 ? _T("正在停用账号…")
                                             : _T("正在启用账号…"));
        return;
    }

    // 组织节点 / 服务号 删除都依赖企业详情屏。
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    const CString entId = m_enterpriseDetail->GetEnterpriseId();
    if (m_confirmTarget == 1)        // 服务号
    {
        HttpRequest request = adminapi::MakeServiceDeleteRequest(
            AppConfig::Instance().Server(), AdminSession::Instance().Token(),
            _ttoi64(id));
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagSvcDelete;
        HttpWorker::Instance().Post(request);
        ShowToast(_T("正在删除服务号…"));
    }
    else                            // 组织节点（部门 / 员工）
    {
        // 区分部门与员工，分别调 dept/delete.do 或 user/delete.do。
        const OrgNode* node = DataStore::Instance().FindOrgNode(entId, id);
        const bool isDept =
            (node != nullptr && node->m_kind == OrgNodeKind::Dept);
        const ServerConfig& server = AppConfig::Instance().Server();
        const CString       token  = AdminSession::Instance().Token();
        HttpRequest request;
        if (isDept)
            request = adminapi::MakeDeptDeleteRequest(server, token, _ttoi64(id));
        else
            request = adminapi::MakeUserDeleteRequest(server, token, _ttoi64(id));
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagOrgNodeDelete;
        HttpWorker::Instance().Post(request);
        ShowToast(isDept ? _T("正在删除部门…") : _T("正在删除员工…"));
    }
}

void AdminFrame::PresentComposeModal()
{
    if (m_composeModal == nullptr || m_root == nullptr)
    {
        return;
    }
    // 预拉企业列表，供目标选择器使用（与仪表盘活跃企业同口径）。
    HttpRequest entReq = adminapi::MakeEnterpriseListRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        CString(), kDashEntStatusAll, 1, kDashEntPageSize);
    entReq.m_replyHwnd = m_hWnd;
    entReq.m_replyMsg  = WM_APP_API_RESULT;
    entReq.m_tag       = kApiTagDashEnterprises;
    HttpWorker::Instance().Post(entReq);
    SetEditsVisible(m_shellPage, false);
    m_root->ShowModal(m_composeModal);
}

void AdminFrame::ShowComposeModal()
{
    // 预设的企业只用这一次：先取出并清空，弹窗打不开时也不会留到下一次新建。
    const CString presetEntId = m_composePresetEntId;
    m_composePresetEntId.Empty();
    if (m_composeModal == nullptr || m_root == nullptr)
    {
        return;
    }
    if (presetEntId.IsEmpty())
    {
        m_composeModal->Reset();
    }
    else
    {
        // 企业详情的「发布公告」：受众预设为「指定企业：本企业」（内部同样先复位）。
        m_composeModal->PresetEnterpriseAudience(_ttoi64(presetEntId));
    }
    PresentComposeModal();
    FA_LOGI(_T("打开新建公告弹窗"));
}

void AdminFrame::HideComposeModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
    FA_LOGI(_T("关闭新建公告弹窗"));
}

void AdminFrame::SubmitComposeModal()
{
    if (m_composeModal == nullptr)
    {
        return;
    }
    CString title = m_composeModal->GetTitle();
    title.Trim();
    CString body = m_composeModal->GetBody();
    body.Trim();
    if (title.IsEmpty() || body.IsEmpty())
    {
        ShowToast(_T("请填写公告标题与正文"), ToastKind::Warn);
        return;
    }
    // 受众为指定企业(1)/部门(2)/员工(3)时必须至少选一个投放目标。
    const int aud = m_composeModal->GetAudienceType();
    if (aud >= 1 && aud <= 3 && m_composeModal->GetTargets().empty())
    {
        ShowToast(_T("请至少选择一个投放目标"), ToastKind::Warn);
        return;
    }
    if (m_composeSubmitBusy)
    {
        ShowToast(_T("正在提交，请稍候…"));
        return;
    }
    m_composeSubmitBusy = true;
    // 第一步：save.do 落库（announcementid 为 0 新建 / 非 0 编辑）。发布在
    // OnAnnSaved 接力。
    HttpRequest request = adminapi::MakeAnnSaveRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        m_composeModal->GetEditAnnouncementId(), title, m_composeModal->GetBody(),
        m_composeModal->GetSeverityCode(), aud, m_composeModal->GetTargets(),
        m_composeModal->GetPinned());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnSave;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在发布公告…"));
}

void AdminFrame::OnAnnSaved(const HttpResult& http)
{
    if (m_composeModal == nullptr)
    {
        m_composeSubmitBusy = false;
        return;
    }
    const AnnSaveResult r = adminapi::ParseAnnSaveResponse(http);
    if (!r.Succeeded())
    {
        m_composeSubmitBusy = false;
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("保存公告失败：%s"), r.m_message.GetString());
        return;
    }
    // 第二步：save 成功 → send.do 发布（保持 busy，直到 send 应答）。
    HttpRequest request = adminapi::MakeAnnSendRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        r.m_announcementId, m_composeModal->GetScheduled(),
        m_composeModal->GetScheduledTime());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnSend;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnAnnSent(const HttpResult& http)
{
    m_composeSubmitBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        // 公告已落库为草稿、但发布失败 —— 关弹窗、刷列表（草稿可见），并提示。
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("发布公告失败：%s"), r.m_message.GetString());
        HideComposeModal();
        StartLoadAnnouncements();
        return;
    }
    ShowToast(_T("公告已发布"));
    HideComposeModal();
    StartLoadAnnouncements();
}

void AdminFrame::OnComposeTreeLoaded(const HttpResult& http)
{
    if (m_composeModal == nullptr)
    {
        return;
    }
    // 组装组织树要用到该企业的公司根部门 ID 与企业名，先从 DataStore 取出来。
    const Enterprise* ent = DataStore::Instance().FindEnterprise(m_composeTreeEntId);
    long long rootDeptId = 0;
    CString entName;
    if (ent != nullptr)
    {
        rootDeptId = _ttoi64(ent->m_rootDeptId);
        entName = ent->m_name;
    }

    const DeptTreeResult r = adminapi::ParseDeptTreeResponse(http, rootDeptId, entName);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("公告目标选择器加载组织树失败：%s"), r.m_message.GetString());
        return;
    }
    m_composeModal->SetOrgTree(m_composeTreeEntId, r.m_root);
}

void AdminFrame::StartAnnouncementEdit(const CString& annId)
{
    if (annId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeAnnDetailRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(annId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnEditFetch;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnAnnEditFetchLoaded(const HttpResult& http)
{
    if (m_composeModal == nullptr)
    {
        return;
    }
    const AnnouncementDetailResult r = adminapi::ParseAnnDetailResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("加载待编辑公告失败：%s"), r.m_message.GetString());
        return;
    }
    // 只有「草稿 + 定时」才是尚未到点的定时公告。服务端定时发出之后不清 scheduled 标志，
    // 已发布的公告即便带着它，也不应再回填成「定时」。
    const bool pendingSchedule = r.m_announcement.m_scheduled
                              && r.m_announcement.m_status == kAnnStatusDraft;
    m_composeModal->LoadForEdit(
        _ttoi64(r.m_announcement.m_id), r.m_announcement.m_title,
        r.m_announcement.m_body, r.m_severityCode, r.m_audienceType,
        r.m_announcement.m_pinned, r.m_targets,
        pendingSchedule, r.m_scheduledTime);
    PresentComposeModal();
    FA_LOGI(_T("打开编辑公告弹窗：%s"), r.m_announcement.m_id.GetString());
}

void AdminFrame::ConfirmAnnouncementRepush()
{
    if (m_announcementsScreen == nullptr)
    {
        return;
    }
    const CString annId = m_announcementsScreen->GetSelectedId();
    if (annId.IsEmpty())
    {
        return;
    }
    const Announcement* ann = nullptr;
    for (const Announcement& a : DataStore::Instance().Announcements())
    {
        if (a.m_id == annId)
        {
            ann = &a;
            break;
        }
    }
    // 按钮在非已发布状态下已置灰；列表刷新与点击交错时仍可能走到这里，按服务端规则提示。
    if (ann == nullptr || ann->m_status != kAnnStatusPublished)
    {
        ShowToast(_T("只有已发布的公告可以重新推送"), ToastKind::Warn);
        return;
    }
    CString msg;
    msg.Format(_T("确定把公告「%s」重新推送给受众里当前在线、还没读过它的成员吗？")
               _T("他们会再收到一次新公告提示。"),
               static_cast<LPCTSTR>(ann->m_title));
    m_confirmDeleteNodeId = annId;
    m_confirmTarget       = kConfirmTargetAnnRepush;
    ConfirmDialogSpec spec;
    spec.m_title         = _T("重新推送公告");
    spec.m_message       = msg;
    spec.m_okText        = _T("重新推送");
    spec.m_confirmDanger = false;
    ShowConfirmDialog(spec);
}

void AdminFrame::SubmitAnnouncementRepush(const CString& annId)
{
    if (annId.IsEmpty())
    {
        return;
    }
    if (m_annActionBusy)
    {
        ShowToast(_T("正在处理，请稍候…"));
        return;
    }
    m_annActionBusy = true;
    HttpRequest request = adminapi::MakeAnnRepushRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(), _ttoi64(annId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnRepush;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在重新推送…"));
    FA_LOGI(_T("repush announcement: %s"), annId.GetString());
}

void AdminFrame::OnAnnRepushed(const HttpResult& http)
{
    m_annActionBusy = false;
    const AnnRepushResult r = adminapi::ParseAnnRepushResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("repush announcement failed: %s"), r.m_message.GetString());
        return;
    }
    CString text;
    if (r.m_pushed > 0)
    {
        text.Format(_T("已推送给 %d 名在线未读成员"), r.m_pushed);
    }
    else
    {
        text = _T("当前没有在线且未读的成员，没有推送");
    }
    ShowToast(text);
    FA_LOGI(_T("repush announcement done: pushed=%d"), r.m_pushed);
}

void AdminFrame::StartAnnouncementCopy(const CString& annId)
{
    if (annId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeAnnDetailRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(annId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagAnnCopyFetch;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("copy announcement: fetch detail of %s"), annId.GetString());
}

void AdminFrame::OnAnnCopyFetchLoaded(const HttpResult& http)
{
    if (m_composeModal == nullptr)
    {
        return;
    }
    const AnnouncementDetailResult r = adminapi::ParseAnnDetailResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("copy announcement: load detail failed: %s"), r.m_message.GetString());
        return;
    }
    m_composeModal->LoadForCopy(r.m_announcement.m_title, r.m_announcement.m_body,
                                r.m_severityCode, r.m_audienceType,
                                r.m_announcement.m_pinned, r.m_targets);
    PresentComposeModal();
    FA_LOGI(_T("copy announcement: compose opened from %s"), r.m_announcement.m_id.GetString());
}

void AdminFrame::ShowServiceModal()
{
    if (m_serviceModal == nullptr || m_root == nullptr)
    {
        return;
    }
    m_serviceModal->Reset();
    SetEditsVisible(m_shellPage, false);
    m_root->ShowModal(m_serviceModal);
    FA_LOGI(_T("打开新建服务号弹窗"));
}

void AdminFrame::HideServiceModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
    FA_LOGI(_T("关闭新建服务号弹窗"));
}

void AdminFrame::SubmitServiceModal()
{
    if (m_serviceModal == nullptr || m_enterpriseDetail == nullptr)
    {
        return;
    }
    if (!m_serviceModal->Validate())
    {
        ShowToast(_T("请填写服务号名称与头像文字"), ToastKind::Warn);
        FA_LOGW(_T("新建服务号校验未通过"));
        return;
    }

    if (m_svcSubmitBusy)
    {
        ShowToast(_T("正在提交，请稍候…"));
        return;
    }
    m_svcSubmitBusy = true;
    // serviceid 传 0 表示新建；allowunsubscribe 暂默认 true。
    HttpRequest request = adminapi::MakeServiceSaveRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        0, _ttoi64(m_enterpriseDetail->GetEnterpriseId()),
        m_serviceModal->GetName(), m_serviceModal->GetKind(),
        m_serviceModal->GetDesc(), m_serviceModal->GetColor(),
        m_serviceModal->GetGlyph(), true);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagSvcSave;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在创建服务号…"));
    FA_LOGI(_T("提交新建服务号"));
}

void AdminFrame::ToggleServiceStatus()
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    ServicesPanel* sp = m_enterpriseDetail->GetServicesPanel();
    if (sp == nullptr)
    {
        return;
    }
    const CString svcId = sp->GetSelectedServiceId();
    if (svcId.IsEmpty())
    {
        return;
    }
    // 查当前状态以决定目标状态。
    ServiceStatus cur = ServiceStatus::Active;
    for (const Service& s : DataStore::Instance().Services(sp->GetEnterpriseId()))
    {
        if (s.m_id == svcId)
        {
            cur = s.m_status;
            break;
        }
    }
    const int target = (cur == ServiceStatus::Disabled) ? 0 : 1;  // 0 正常 / 1 停用
    if (target == 0)
    {
        SubmitServiceStatus(svcId, target);   // 重新启用，直接提交
        return;
    }
    CString name = svcId;
    for (const Service& s : DataStore::Instance().Services(sp->GetEnterpriseId()))
    {
        if (s.m_id == svcId)
        {
            name = s.m_name;
            break;
        }
    }
    CString msg;
    msg.Format(_T("确定要停用服务号「%s」吗？停用后它不再向员工推送消息，历史消息保留，")
               _T("之后可以重新启用。"),
               static_cast<LPCTSTR>(name));
    m_confirmDeleteNodeId = svcId;
    m_confirmTarget       = kConfirmTargetSvcDisable;
    ConfirmDialogSpec spec;
    spec.m_title         = _T("停用服务号");
    spec.m_message       = msg;
    spec.m_okText        = _T("停用");
    spec.m_confirmDanger = true;
    ShowConfirmDialog(spec);
}

void AdminFrame::ShowBindingModal()
{
    if (m_bindingModal == nullptr || m_root == nullptr)
    {
        return;
    }
    // 弹窗内容已在 OnServiceDetailLoaded 里按服务端读到的当前绑定经 Configure 配置好。
    SetEditsVisible(m_shellPage, false);
    m_root->ShowModal(m_bindingModal);
    FA_LOGI(_T("打开服务号绑定弹窗"));
}

void AdminFrame::HideBindingModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
    FA_LOGI(_T("关闭服务号绑定弹窗"));
}

void AdminFrame::SaveBindingModal()
{
    if (m_bindingModal == nullptr || m_enterpriseDetail == nullptr)
    {
        return;
    }
    if (m_bindingModalSvcId.IsEmpty())
    {
        HideBindingModal();
        return;
    }
    // 用打开弹窗时记下的服务号 ID，而不是面板的当前选中项：两者在正常操作下相同，
    // 但以弹窗自己记录的为准，绑定才不会写到别的服务号上。
    m_pendingBindingsSvcId = m_bindingModalSvcId;
    m_pendingBindings      = m_bindingModal->GetBindings();
    HttpRequest request = adminapi::MakeServiceSetBindingsRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(m_pendingBindingsSvcId), m_pendingBindings);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagSvcSetBindings;
    HttpWorker::Instance().Post(request);
    HideBindingModal();
    ShowToast(_T("正在保存绑定范围…"));
    FA_LOGI(_T("submit service bindings: service=%s count=%u"),
            m_pendingBindingsSvcId.GetString(),
            static_cast<unsigned>(m_pendingBindings.size()));
}

void AdminFrame::OnServiceBindingsSaved(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    const CString svcId = m_pendingBindingsSvcId;
    std::vector<ServiceBinding> saved;
    saved.swap(m_pendingBindings);
    m_pendingBindingsSvcId.Empty();

    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("save service bindings failed: service=%s msg=%s"),
                svcId.GetString(), r.m_message.GetString());
        return;
    }

    // 列表接口不带绑定，重拉列表时 CarryOverLoadedBindings 会沿用缓存里的旧绑定，
    // 所以先把刚保存成功的绑定写进缓存。
    if (m_enterpriseDetail != nullptr && !svcId.IsEmpty())
    {
        const CString entId = m_enterpriseDetail->GetEnterpriseId();
        std::vector<Service> list = DataStore::Instance().Services(entId);
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].m_id == svcId)
            {
                list[i].m_bindings       = saved;
                list[i].m_bindingsLoaded = true;
                list[i].m_subs           = DataStore::Instance().ComputeReach(entId, saved);
                DataStore::Instance().SetServices(entId, list);
                break;
            }
        }
    }
    ShowToast(_T("绑定范围已保存"));
    FA_LOGI(_T("service bindings saved: service=%s count=%u"),
            svcId.GetString(), static_cast<unsigned>(saved.size()));
    ReloadServiceList();
}

void AdminFrame::FetchServiceDetail(const CString& serviceId, bool forBinding)
{
    if (serviceId.IsEmpty())
    {
        return;
    }
    if (forBinding)
    {
        if (m_svcBindingFetchBusy)
        {
            return;   // 上一次读取还没回来，忽略连点
        }
        m_svcBindingFetchBusy = true;
    }
    HttpRequest request = adminapi::MakeServiceDetailRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(serviceId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = forBinding ? kApiTagSvcDetailForBinding : kApiTagSvcDetail;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("fetch service detail: service=%s forBinding=%d"),
            serviceId.GetString(), forBinding ? 1 : 0);
}

void AdminFrame::FetchSelectedServiceBindingsIfNeeded()
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    ServicesPanel* sp = m_enterpriseDetail->GetServicesPanel();
    if (sp == nullptr)
    {
        return;
    }
    const CString svcId = sp->GetSelectedServiceId();
    const std::vector<Service>& list = DataStore::Instance().Services(sp->GetEnterpriseId());
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].m_id == svcId)
        {
            if (!list[i].m_bindingsLoaded)
            {
                FetchServiceDetail(svcId, false);
            }
            return;
        }
    }
}

void AdminFrame::OnServiceDetailLoaded(const HttpResult& http, bool forBinding)
{
    if (forBinding)
    {
        m_svcBindingFetchBusy = false;
    }
    const ServiceDetailResult r = adminapi::ParseServiceDetailResponse(http);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("load service detail failed: forBinding=%d msg=%s"),
                forBinding ? 1 : 0, r.m_message.GetString());
        if (forBinding)
        {
            // 读不到当前绑定就不打开弹窗：从空集合开始保存会清掉原有绑定。
            ShowToast(CString(_T("读取当前绑定失败，未打开编辑：")) + r.m_message,
                      ToastKind::Error);
        }
        return;
    }

    // 应答回来时用户可能已离开企业详情或换了企业：只处理当前企业里仍存在的服务号。
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    const CString entId = m_enterpriseDetail->GetEnterpriseId();
    const CString svcId = r.m_service.m_id;
    std::vector<Service> list = DataStore::Instance().Services(entId);
    size_t index = list.size();
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].m_id == svcId)
        {
            index = i;
            break;
        }
    }
    if (index == list.size())
    {
        FA_LOGI(_T("service detail arrived for a service no longer shown: %s"),
                svcId.GetString());
        return;
    }

    const Enterprise* ent = DataStore::Instance().FindEnterprise(entId);
    const CString shortName = (ent != nullptr) ? ent->m_shortName : CString();
    list[index].m_bindings = adminapi::ResolveServiceBindings(
        r.m_bindings, DataStore::Instance().OrgTree(entId), shortName);
    list[index].m_bindingsLoaded = true;
    // 覆盖人数按绑定与组织树重算，与「绑定范围」页签的触达人数一致。
    list[index].m_subs = DataStore::Instance().ComputeReach(entId, list[index].m_bindings);
    const Service loaded = list[index];
    DataStore::Instance().SetServices(entId, list);

    ServicesPanel* sp = m_enterpriseDetail->GetServicesPanel();
    if (sp != nullptr)
    {
        sp->Refresh();
    }
    FA_LOGI(_T("service detail loaded: service=%s bindings=%u forBinding=%d"),
            svcId.GetString(), static_cast<unsigned>(loaded.m_bindings.size()),
            forBinding ? 1 : 0);

    if (!forBinding || m_bindingModal == nullptr)
    {
        return;
    }
    // 读取期间用户若已在面板上改选了别的服务号，就不再弹出，免得编辑的不是眼前这个。
    if (sp == nullptr || sp->GetSelectedServiceId() != svcId)
    {
        FA_LOGI(_T("selection changed while loading bindings, modal not opened: %s"),
                svcId.GetString());
        return;
    }
    m_bindingModalSvcId = svcId;
    m_bindingModal->Configure(loaded.m_name, entId, loaded.m_bindings);
    ::PostMessage(m_hWnd, WM_APP_NAVIGATE, kNavShowBindingModal, 0);
}

void AdminFrame::OnServiceSaved(const HttpResult& http)
{
    m_svcSubmitBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);   // 失败保留弹窗
        FA_LOGW(_T("新建服务号失败：%s"), r.m_message.GetString());
        return;
    }
    HideServiceModal();
    ShowToast(_T("服务号已创建"));
    ReloadServiceList();
}

void AdminFrame::OnServiceActionDone(const HttpResult& http, LPCTSTR okText)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (r.Succeeded())
    {
        ShowToast(okText);
        ReloadServiceList();
        FA_LOGI(_T("服务号写操作成功：%s"), okText);
    }
    else
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("服务号写操作失败：%s"), r.m_message.GetString());
    }
}

void AdminFrame::ReloadServiceList()
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    const CString entId = m_enterpriseDetail->GetEnterpriseId();
    if (entId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeServiceListRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(entId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagSvcListReload;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnServiceListReloaded(const HttpResult& http)
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    ServiceListResult r = adminapi::ParseServiceListResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(_T("刷新服务号列表失败"), ToastKind::Error);
        FA_LOGW(_T("刷新服务号列表失败：%s"), r.m_message.GetString());
        return;
    }
    // 列表接口不带绑定：把已经读到的绑定带到新列表上，否则一刷新就清空了。
    const CString entId = m_enterpriseDetail->GetEnterpriseId();
    adminapi::CarryOverLoadedBindings(DataStore::Instance().Services(entId), r.m_list);
    DataStore::Instance().SetServices(entId, r.m_list);
    m_enterpriseDetail->Refresh();
    FetchSelectedServiceBindingsIfNeeded();
}

void AdminFrame::FireGetPermissions(const CString& userId)
{
    if (userId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeUserGetPermissionsRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(userId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagUserGetPerms;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnUserPermissionsLoaded(const HttpResult& http)
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    OrgDetailPanel* panel = m_enterpriseDetail->GetOrgDetailPanel();
    if (panel == nullptr)
    {
        return;
    }
    const UserPermissionsResult r = adminapi::ParseUserPermissionsResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("取员工权限矩阵失败：%s"), r.m_message.GetString());
        return;
    }
    panel->SetPermissionData(r.m_role, r.m_items);
}

void AdminFrame::OnUserRoleSet(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("设置员工角色失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("角色已更新"));
    // 改角色会清空该员工的逐人权限覆盖 → 重新拉取权限矩阵刷新显示。
    FireGetPermissions(m_permUserId);
}

void AdminFrame::OnUserPermsSet(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (r.Succeeded())
    {
        ShowToast(_T("权限已保存"));
        return;
    }
    ShowToast(r.m_message, ToastKind::Error);
    FA_LOGW(_T("设置员工权限失败：%s"), r.m_message.GetString());
    // 失败时本地是乐观翻转后的状态，与服务端不一致 → 重拉权限矩阵纠正。
    FireGetPermissions(m_permUserId);
}

void AdminFrame::FireLoginRecords(const CString& userId)
{
    if (userId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeUserLoginRecordsRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(userId), 1, kLoginRecordsPageSize);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagUserLoginRecords;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::FireActivity(const CString& userId)
{
    if (userId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeUserActivityRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(userId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagUserActivity;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnUserLoginRecordsLoaded(const HttpResult& http)
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    OrgDetailPanel* panel = m_enterpriseDetail->GetOrgDetailPanel();
    if (panel == nullptr)
    {
        return;
    }
    const UserLoginRecordsResult r =
        adminapi::ParseUserLoginRecordsResponse(http);
    if (!r.Succeeded())
    {
        // 设备登录是次要 tab，失败只记日志、不打扰用户。
        FA_LOGW(_T("取员工登录记录失败：%s"), r.m_message.GetString());
        return;
    }
    panel->SetLoginData(r.m_devices, r.m_records);
}

void AdminFrame::OnUserActivityLoaded(const HttpResult& http)
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    OrgDetailPanel* panel = m_enterpriseDetail->GetOrgDetailPanel();
    if (panel == nullptr)
    {
        return;
    }
    const UserActivityResult r = adminapi::ParseUserActivityResponse(http);
    if (!r.Succeeded())
    {
        // 操作历史是次要 tab，失败只记日志、不打扰用户。
        FA_LOGW(_T("取员工操作历史失败：%s"), r.m_message.GetString());
        return;
    }
    panel->SetActivityData(r.m_items);
}

void AdminFrame::FireMessageStats(const CString& userId)
{
    if (userId.IsEmpty())
    {
        return;
    }
    HttpRequest request = adminapi::MakeUserMessageStatsRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(userId));
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagUserMsgStats;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::OnUserMessageStatsLoaded(const HttpResult& http)
{
    if (m_enterpriseDetail == nullptr)
    {
        return;
    }
    OrgDetailPanel* panel = m_enterpriseDetail->GetOrgDetailPanel();
    if (panel == nullptr)
    {
        return;
    }
    const UserMessageStatsResult r =
        adminapi::ParseUserMessageStatsResponse(http);
    if (!r.Succeeded())
    {
        // 消息与会话是次要 tab，失败只记日志、不打扰用户。
        FA_LOGW(_T("取员工消息统计失败：%s"), r.m_message.GetString());
        return;
    }
    panel->SetMessageStats(r.m_sent, r.m_received, r.m_conversations, r.m_trend);
}

void AdminFrame::OnUserStatusSet(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("启用/停用员工失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("员工状态已更新"));
    // 状态变化体现在组织树成员的 status 上 → 重拉组织树刷新。
    ReloadOrgTree();
}

void AdminFrame::OnUserPasswordReset(const HttpResult& http)
{
    const UserResetPwdResult r = adminapi::ParseUserResetPasswordResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("重置员工密码失败：%s"), r.m_message.GetString());
        return;
    }
    // 用信息弹窗展示新密码（与新增企业 / 员工一致：单按钮 + 复制）；有设备被强制下线时一并说明（2026-10-07）。
    const CString msg = adminapi::FormatResetPasswordNotice(r.m_newPassword, r.m_kicked);
    m_confirmDeleteNodeId.Empty();
    ConfirmDialogSpec spec;
    spec.m_infoMode = true;
    spec.m_title    = _T("密码已重置");
    spec.m_message  = msg;
    spec.m_okText   = _T("知道了");
    spec.m_copyText = r.m_newPassword;
    ShowConfirmDialog(spec);
}

void AdminFrame::OnUserDevicesKicked(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("强制下线设备失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("已强制下线全部设备"));
    // 在线设备列表已变 → 重拉登录记录刷新设备登录 tab。
    FireLoginRecords(m_permUserId);
}

void AdminFrame::ShowToast(LPCTSTR text, ToastKind kind)
{
    if (m_toast == nullptr)
    {
        FA_LOGI(_T("提示：%s"), text);
        return;
    }

    // ---- 配色:浅色语义底 + 同色系深字/图标 ----
    // info / warn / error 各用一套"浅底 + 同色系深字"配色, 三色一眼区分、
    // 克制不刺眼;图标也用对应深色, 形状上 info 对勾、warn 三角感叹号、
    // error 叉号 —— 颜色 + 形状双重区分。
    const int kToastRadius = 12;        // 圆角(px)
    const int kToastIconPx = 16;        // 图标边长(px)

    // 三档显示时长(ms):info 只是反馈"做了什么"故短;warn / error 需要用户
    // 看清故依次更长。
    const int kDurInfoMs  = 3000;
    const int kDurWarnMs  = 5000;
    const int kDurErrorMs = 8000;

    // 三套语义配色(底色 / 前景色)。前景色同时用作文字色与图标色。
    const COLORREF kInfoBg  = RGB(236, 248, 240);   // 淡绿底
    const COLORREF kInfoFg  = RGB( 28, 138,  74);   // 深绿字 / 图标
    const COLORREF kWarnBg  = RGB(254, 247, 232);   // 淡黄橙底
    const COLORREF kWarnFg  = RGB(193, 130,  18);   // 深橙字 / 图标
    const COLORREF kErrorBg = RGB(253, 238, 238);   // 淡红底
    const COLORREF kErrorFg = RGB(198,  50,  62);   // 深红字 / 图标

    // 语义图标位图按 kind 缓存:一个 kind 一份 32bpp PARGB HBITMAP, 进程级
    // 生命期(退出由 OS 回收), 避免每次 Show 重新渲染。颜色取同色系深色。
    static HBITMAP s_iconInfo  =
        ui::RenderIconToBitmap(ui::Icon::Check,   kToastIconPx, kInfoFg);
    static HBITMAP s_iconWarn  =
        ui::RenderIconToBitmap(ui::Icon::Warning, kToastIconPx, kWarnFg);
    static HBITMAP s_iconError =
        ui::RenderIconToBitmap(ui::Icon::Close,   kToastIconPx, kErrorFg);

    COLORREF bg         = kInfoBg;
    COLORREF fg         = kInfoFg;
    HBITMAP  icon       = s_iconInfo;
    int      durationMs = kDurInfoMs;
    switch (kind)
    {
    case ToastKind::Warn:
        bg         = kWarnBg;
        fg         = kWarnFg;
        icon       = s_iconWarn;
        durationMs = kDurWarnMs;
        break;
    case ToastKind::Error:
        bg         = kErrorBg;
        fg         = kErrorFg;
        icon       = s_iconError;
        durationMs = kDurErrorMs;
        break;
    case ToastKind::Info:
    default:
        break;
    }

    m_toast->SetBgColor(bg);
    m_toast->SetTextColor(fg);
    m_toast->SetCornerRadius(kToastRadius);
    m_toast->SetIcon(icon);                    // nullptr(渲染失败)时 DuiToast 退化为无图标
    m_toast->SetIconSize(kToastIconPx);
    m_toast->SetDurationMs(durationMs);
    // 浅底配很淡的柔和阴影:DuiToast 默认阴影不透明度 90 在浅底上偏重, 这里
    // 调到 42, 让 toast 只是轻轻"浮起"而不显脏。
    m_toast->SetShadowAlpha(42);
    m_toast->SetMaxWidth(0);                   // 0 = 不限, 自按图标 + 文字 + padding 算宽
    m_toast->Show(text);
    // DuiToast 的渐入 → 停留 → 渐出全程由 DuiAnimMgr 内置的共享脉冲定时器
    // 推进(见 DuiAnimation.cpp 的 DuiAnimMgr::PulseProc),这里无需再 SetTimer。
    FA_LOGI(_T("提示：%s"), text);
}

void AdminFrame::PrefillLogin()
{
    // 回填登录框。输入框是无窗口控件，构造完即可写入，没有前置条件。
    // 只用 ini 里记下的上次登录信息；本机从未登录过时两个框都留空（不再预填测试账号）。
    if (m_loginPage == nullptr)
    {
        return;
    }

    const LoginConfig& saved = AppConfig::Instance().Login();

    CString accountText;
    CString passwordText;
    logintext::ResolvePrefill(saved, accountText, passwordText);

    if (DuiEdit* account = dynamic_cast<DuiEdit*>(
            m_loginPage->FindCtrlById(kIdLoginAccount)))
    {
        account->SetText(accountText);
    }
    if (DuiEdit* password = dynamic_cast<DuiEdit*>(
            m_loginPage->FindCtrlById(kIdLoginPassword)))
    {
        password->SetText(passwordText);
    }
    if (DuiButton* remember = dynamic_cast<DuiButton*>(
            m_loginPage->FindCtrlById(kIdLoginRememberPwd)))
    {
        // notify 传 false：这是程序回填，不该触发用户手动勾选才走的通知。
        remember->SetCheck(saved.m_rememberPassword, false);
    }
}

void AdminFrame::SetEditsVisible(DuiControl* node, bool visible)
{
    if (node == nullptr)
    {
        return;
    }

    // 员工详情面板自己管这 11 个只读值字段的显隐 —— 它每帧绘制时都会按本帧
    // 标记表 reconcile 一次(OrgDetailPanel::ApplyBasicEditVisibility)。若在这里
    // 逐个 SetVisible,面板的下一次重绘就会把这次设置覆盖回去。改为拨它的
    // "外部强制隐藏"开关,并【跳过其子树】—— 子树递归下去只会与该面板的
    // reconcile 互相打架:恢复时递归会把 11 个字段全设为可见,而其中一部分本就
    // 不该显示(空字段 / 当前不在基本信息 tab),要等下一帧 reconcile 才纠正,
    // 中间会闪一帧错误内容。
    if (OrgDetailPanel* orgPanel = dynamic_cast<OrgDetailPanel*>(node))
    {
        orgPanel->SetBasicEditsSuppressed(!visible);
        return;
    }

    if (DuiEdit* edit = dynamic_cast<DuiEdit*>(node))
    {
        edit->SetVisible(visible);
    }
    for (const auto& child : node->Children())
    {
        SetEditsVisible(child.get(), visible);
    }

    // 企业详情屏的组织树搜索框只在「组织架构」子 tab 下可见：上面的递归恢复时把它也设成了可见，
    // 这里按当前子 tab 纠正，否则它会从「在线用户」页面左上角的边距里露出来
    if (visible)
    {
        if (EnterpriseDetailScreen* detail = dynamic_cast<EnterpriseDetailScreen*>(node))
        {
            detail->ApplySubTabEditVisibility();
        }
    }
}


// ===========================================================================
// 客户端版本（模块 10，仅平台超管）
// ===========================================================================

void AdminFrame::StartLoadClientVersions()
{
    if (m_clientVersionsScreen == nullptr)
    {
        return;
    }

    // platform 传 0 表示不按平台过滤：目前只有 PC 端在发版，但 Android 将来
    // 也用同一张表，列表页一次全列出来更省事。
    HttpRequest request = adminapi::MakeClientVersionListRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(), 0);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagCvList;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("客户端版本：发起列表加载"));
}

void AdminFrame::StartScanPackages()
{
    HttpRequest request = adminapi::MakeScanPackagesRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagCvScanPackages;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("客户端版本：发起升级包目录扫描"));
}

void AdminFrame::StartClientVersionAction(int action, long long id)
{
    if (id <= 0)
    {
        return;
    }

    HttpRequest request;
    const ServerConfig& server = AppConfig::Instance().Server();
    const CString& token = AdminSession::Instance().Token();

    switch (static_cast<CvRowAction>(action))
    {
    case CvRowAction::Publish:
        request = adminapi::MakeClientVersionSetEnabledRequest(server, token, id, true);
        break;
    case CvRowAction::Disable:
        request = adminapi::MakeClientVersionSetEnabledRequest(server, token, id, false);
        break;
    default:
        // Edit 不走这里（它开弹窗，不直接发请求）
        return;
    }

    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagCvSetEnabled;
    HttpWorker::Instance().Post(request);
    FA_LOGI(_T("客户端版本：发起行内操作 action=%d id=%lld"), action, id);
}

void AdminFrame::OnClientVersionsLoaded(const HttpResult& http)
{
    const adminapi::ClientVersionListResult r =
        adminapi::ParseClientVersionListResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("客户端版本：列表加载失败 —— %s"), r.m_message.GetString());
        return;
    }

    if (m_clientVersionsScreen != nullptr)
    {
        m_clientVersionsScreen->SetVersions(r.m_list);
        m_clientVersionsScreen->Invalidate();
    }
    FA_LOGI(_T("客户端版本：列表加载成功，共 %d 条"), static_cast<int>(r.m_list.size()));
}

void AdminFrame::OnPackagesScanned(const HttpResult& http)
{
    const adminapi::ScanPackagesResult r = adminapi::ParseScanPackagesResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("客户端版本：扫描升级包目录失败 —— %s"), r.m_message.GetString());
        return;
    }

    // 目录先回填到界面提示条上，管理员即便不建版本也能看到该往哪儿放包
    if (m_clientVersionsScreen != nullptr)
    {
        m_clientVersionsScreen->SetPackageDir(r.m_dir);
        m_clientVersionsScreen->Invalidate();
    }

    // 目录里一个可用的包都没有：与其开一个空下拉的弹窗让人困惑，不如直说
    int usable = 0;
    for (size_t i = 0; i < r.m_list.size(); ++i)
    {
        if (r.m_list[i].m_isZip)
        {
            ++usable;
        }
    }
    if (usable == 0)
    {
        CString tip;
        tip.Format(_T("升级包目录 %s 下没有可用的 zip，请先把升级包放进去"),
                   r.m_dir.GetString());
        ShowToast(tip, ToastKind::Warn);
        FA_LOGW(_T("客户端版本：升级包目录里没有可用的 zip（共 %d 个文件）"),
                static_cast<int>(r.m_list.size()));
        return;
    }

    ShowClientVersionModal(r);
    FA_LOGI(_T("客户端版本：扫描到 %d 个候选包（其中可用 %d 个）"),
            static_cast<int>(r.m_list.size()), usable);
}

void AdminFrame::OnClientVersionActionDone(const HttpResult& http)
{
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message, ToastKind::Error);
        FA_LOGW(_T("客户端版本：操作失败 —— %s"), r.m_message.GetString());
        return;
    }

    ShowToast(_T("操作已生效"), ToastKind::Info);
    // 发布 / 撤回都会改变"当前生效"是哪一条，必须重拉列表才能把
    // 状态列与徽标刷新对，否则界面显示的还是操作前的样子。
    StartLoadClientVersions();
}


void AdminFrame::HandleClientVersionRowAction()
{
    if (m_clientVersionsScreen == nullptr)
    {
        return;
    }

    const CvPendingAction pending = m_clientVersionsScreen->TakePendingAction();
    if (pending.m_action == CvRowAction::None)
    {
        return;
    }

    // 「编辑」不发请求，而是把该条记录预填进弹窗，只让人改最低版本与更新说明。
    if (pending.m_action == CvRowAction::Edit)
    {
        if (m_clientVersionModal != nullptr && m_root != nullptr)
        {
            m_clientVersionModal->SetEditTarget(m_clientVersionsScreen->GetSelected());
            SetEditsVisible(m_shellPage, false);
            m_root->ShowModal(m_clientVersionModal);
            FA_LOGI(_T("打开编辑客户端版本弹窗，id=%lld"), pending.m_id);
        }
        return;
    }

    // 发布 / 撤回影响全部客户端的升级提示与登录，先弹确认框，确认后再发请求。
    const bool publish = (pending.m_action == CvRowAction::Publish);
    if (!publish && pending.m_action != CvRowAction::Disable)
    {
        return;
    }
    const adminapi::ClientVersionItem& sel = m_clientVersionsScreen->GetSelected();
    CString versionText;
    if (sel.m_id == pending.m_id)
    {
        versionText = sel.m_version;
    }
    else
    {
        versionText.Format(_T("#%lld"), pending.m_id);
    }
    ConfirmDialogSpec spec;
    CString msg;
    if (publish)
    {
        msg.Format(_T("确定要发布版本 %s 吗？发布后，低于它的客户端检查更新时都会收到升级提示%s。"),
                   versionText.GetString(),
                   (sel.m_id == pending.m_id && sel.m_forced) ? _T("，并且必须升级后才能继续使用")
                                                              : _T(""));
        spec.m_title  = _T("发布版本");
        spec.m_okText = _T("发布");
        m_confirmTarget = kConfirmTargetCvPublish;
    }
    else
    {
        msg.Format(_T("确定要撤回版本 %s 吗？撤回后客户端不再收到它的升级提示，检查更新时")
                   _T("改为提示此前发布的最新版本。"),
                   versionText.GetString());
        spec.m_title         = _T("撤回版本");
        spec.m_okText        = _T("撤回");
        spec.m_confirmDanger = true;
        m_confirmTarget = kConfirmTargetCvDisable;
    }
    spec.m_message = msg;
    m_confirmDeleteNodeId.Format(_T("%lld"), pending.m_id);
    ShowConfirmDialog(spec);
}

void AdminFrame::ShowClientVersionModal(const adminapi::ScanPackagesResult& scan)
{
    if (m_clientVersionModal == nullptr || m_root == nullptr)
    {
        return;
    }

    m_clientVersionModal->SetPackages(scan);
    // 隐藏外壳内的输入框。早先内嵌真 Win32 输入框子窗口时不隐藏会透过半透明
    // 遮罩显形，改为无窗口控件之后该前提已不成立，此处保留只是尚未清理。
    SetEditsVisible(m_shellPage, false);
    m_root->ShowModal(m_clientVersionModal);
    FA_LOGI(_T("打开新建客户端版本弹窗"));
}

void AdminFrame::HideClientVersionModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
    FA_LOGI(_T("关闭新建客户端版本弹窗"));
}

void AdminFrame::SubmitClientVersionModal(bool publish)
{
    if (m_clientVersionModal == nullptr)
    {
        return;
    }

    if (!m_clientVersionModal->Validate())
    {
        ShowToast(_T("请选择一个升级包并填写版本号"), ToastKind::Warn);
        return;
    }
    // 新建时校验版本号格式；最低版本不论新建还是编辑都要校验格式与「不高于版本号」，
    // 填错会把这个版本自己的客户端也挡在登录之外。
    if (!m_clientVersionModal->IsEditing()
        && !adminapi::IsValidVersionText(m_clientVersionModal->GetVersion()))
    {
        ShowToast(_T("版本号格式不正确：应为 2～4 段数字、用点分隔，例如 3.3.0"), ToastKind::Warn);
        return;
    }
    const CString versionForCheck = m_clientVersionModal->IsEditing()
                                        ? m_clientVersionModal->GetEditingVersion()
                                        : m_clientVersionModal->GetVersion();
    const CString minVersionError =
        adminapi::CheckMinVersion(m_clientVersionModal->GetMinVersion(), versionForCheck);
    if (!minVersionError.IsEmpty())
    {
        ShowToast(minVersionError, ToastKind::Warn);
        return;
    }

    // 编辑态走 update.do：只改最低版本与更新说明，包与版本号不动。
    if (m_clientVersionModal->IsEditing())
    {
        const long long editingId = m_clientVersionModal->GetEditingId();
        HttpRequest updateReq = adminapi::MakeClientVersionUpdateRequest(
            AppConfig::Instance().Server(), AdminSession::Instance().Token(),
            editingId,
            m_clientVersionModal->GetMinVersion(),
            m_clientVersionModal->GetReleaseNote(),
            m_clientVersionModal->IsForced());
        updateReq.m_replyHwnd = m_hWnd;
        updateReq.m_replyMsg  = WM_APP_API_RESULT;
        updateReq.m_tag       = kApiTagCvUpdate;
        HttpWorker::Instance().Post(updateReq);

        HideClientVersionModal();
        FA_LOGI(_T("客户端版本：提交修改 id=%lld"), editingId);
        return;
    }

    // 平台固定为 PC：目前只有 PC 客户端做自动更新，Android 端将来接入时
    // 这里再按界面上的选择传。
    const int kPlatformPc = 1;
    HttpRequest request = adminapi::MakeClientVersionCreateRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        kPlatformPc,
        m_clientVersionModal->GetVersion(),
        m_clientVersionModal->GetMinVersion(),
        m_clientVersionModal->GetReleaseNote(),
        m_clientVersionModal->GetSelectedMd5(),
        m_clientVersionModal->IsForced(),
        publish);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagCvCreate;
    HttpWorker::Instance().Post(request);

    // 请求已发出就先关弹窗：应答回来会重拉列表，成败都有 toast 提示。
    HideClientVersionModal();
    FA_LOGI(_T("客户端版本：提交新建 version=%s publish=%d"),
            m_clientVersionModal->GetVersion().GetString(), publish ? 1 : 0);
}

// ====================================================================
// 敏感词（模块 11）
// ====================================================================

void AdminFrame::SensitiveRequestThunk(void* user, HttpRequest& request)
{
    AdminFrame* self = static_cast<AdminFrame*>(user);
    //界面按空的 ServerConfig 构造请求，这里补上服务器地址与结果回传目标
    const ServerConfig server = AppConfig::Instance().Server();
    request.m_host      = server.m_host.GetString();
    request.m_port      = server.m_port;
    request.m_useTls    = server.m_useTls;
    request.m_replyHwnd = self->m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    HttpWorker::Instance().Post(request);
}

void AdminFrame::SensitiveUiRequestThunk(void* user, const SensitiveWordsScreen::UiRequest& request)
{
    AdminFrame* self = static_cast<AdminFrame*>(user);
    //队列原本为空时才投递一次：同一轮里连着来的几条（如「关闭弹窗」与「已添加」提示）一起处理
    const bool wasEmpty = self->m_swUiQueue.empty();
    self->m_swUiQueue.push_back(request);
    if (wasEmpty)
    {
        ::PostMessage(self->m_hWnd, WM_APP_NAVIGATE, kNavSwUiRequests, 0);
    }
}

void AdminFrame::FileShareUiRequestThunk(void* user, const fsui::UiRequest& request)
{
    AdminFrame* self = static_cast<AdminFrame*>(user);
    //队列原本为空时才投递一次：同一轮里连着来的几条一起处理
    const bool wasEmpty = self->m_fsUiQueue.empty();
    self->m_fsUiQueue.push_back(request);
    if (wasEmpty)
    {
        ::PostMessage(self->m_hWnd, WM_APP_NAVIGATE, kNavFsUiRequests, 0);
    }
}

void AdminFrame::HandleFileShareUiRequests()
{
    std::vector<fsui::UiRequest> queue;
    queue.swap(m_fsUiQueue);
    for (size_t i = 0; i < queue.size(); ++i)
    {
        const fsui::UiRequest& req = queue[i];
        switch (req.m_kind)
        {
        //提示
        case fsui::kUiToast:
            ShowToast(req.m_text, req.m_isError ? ToastKind::Error : ToastKind::Info);
            break;

        //确认框：结果经 m_fsConfirmTarget 回告发起方
        case fsui::kUiConfirm:
        {
            //上一个文件共享确认框还没有结果（ShowConfirmDialog 会关掉旧的）：先按取消回告它的发起方
            if (m_confirmTarget == kConfirmTargetFileShare && m_fsConfirmTarget != nullptr
                && !m_confirmDeleteNodeId.IsEmpty())
            {
                fsui::ConfirmTarget* previous = m_fsConfirmTarget;
                m_fsConfirmTarget = nullptr;
                m_confirmDeleteNodeId.Empty();
                previous->OnConfirmResult(false);
            }
            //确认框没有对象 ID，这里只要一个非空值表示「有待确认的操作」
            m_confirmDeleteNodeId = _T("fileshare");
            m_confirmTarget = kConfirmTargetFileShare;
            m_fsConfirmTarget = req.m_target;
            ConfirmDialogSpec spec;
            spec.m_title = req.m_title;
            spec.m_message = req.m_text;
            spec.m_okText = req.m_okText;
            spec.m_confirmDanger = req.m_danger;
            ShowConfirmDialog(spec);
            FA_LOGI(_T("file share admin: confirm dialog shown: %s"), req.m_title.GetString());
            break;
        }

        //没有其它种类
        default:
            break;
        }
    }
}

void AdminFrame::HandleSensitiveUiRequests()
{
    std::vector<SensitiveWordsScreen::UiRequest> queue;
    queue.swap(m_swUiQueue);
    for (size_t i = 0; i < queue.size(); ++i)
    {
        const SensitiveWordsScreen::UiRequest& req = queue[i];
        switch (req.m_kind)
        {
        //打开「添加敏感词」弹窗
        case SensitiveWordsScreen::kUiOpenAddModal:
            if (m_swModal != nullptr && m_root != nullptr)
            {
                m_swModal->ShowForAdd();
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_swModal);
                FA_LOGI(_T("sensitive words: open add modal"));
            }
            break;

        //打开「编辑敏感词」弹窗，预填所选词条
        case SensitiveWordsScreen::kUiOpenEditModal:
            if (m_swModal != nullptr && m_root != nullptr)
            {
                m_swModal->ShowForEdit(req.m_word);
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_swModal);
                FA_LOGI(_T("sensitive words: open edit modal, id: %lld"), req.m_word.m_id);
            }
            break;

        //删除词条前的确认框
        case SensitiveWordsScreen::kUiConfirmDelete:
        {
            m_confirmDeleteNodeId.Format(_T("%lld"), req.m_word.m_id);
            m_confirmTarget = kConfirmTargetSwDelete;
            ConfirmDialogSpec spec;
            spec.m_title = _T("删除敏感词");
            spec.m_message.Format(_T("确定删除敏感词「%s」吗？删除后立即生效，含有它的内容将不再被拦截。"),
                                  req.m_word.m_word.GetString());
            spec.m_okText = _T("删除");
            spec.m_confirmDanger = true;
            ShowConfirmDialog(spec);
            break;
        }

        //关闭总开关前的确认框
        case SensitiveWordsScreen::kUiConfirmDisable:
        {
            //总开关没有对象 ID，这里只要一个非空值表示「有待确认的操作」
            m_confirmDeleteNodeId = _T("disable");
            m_confirmTarget = kConfirmTargetSwDisable;
            ConfirmDialogSpec spec;
            spec.m_title = _T("暂停敏感词过滤");
            spec.m_message = _T("暂停后，本企业的消息、资料、群资料与文件名都不再检查敏感词，直到重新打开。确定暂停吗？");
            spec.m_okText = _T("暂停");
            spec.m_confirmDanger = true;
            ShowConfirmDialog(spec);
            break;
        }

        //弹窗里的提交成功：关闭弹窗
        case SensitiveWordsScreen::kUiCloseModal:
            HideSensitiveWordModal();
            break;

        //弹窗里的提交失败：弹窗不关，显示原因，「保存」恢复可用
        case SensitiveWordsScreen::kUiModalError:
            if (m_swModal != nullptr)
            {
                m_swModal->SetBusy(false);
                m_swModal->SetError(req.m_text);
            }
            break;

        //提示
        case SensitiveWordsScreen::kUiToast:
            ShowToast(req.m_text, req.m_isError ? ToastKind::Error : ToastKind::Info);
            break;

        //打开「批量导入」弹窗
        case SensitiveWordsScreen::kUiOpenImportModal:
            if (m_swImportModal != nullptr && m_root != nullptr)
            {
                m_swImportModal->ShowFresh();
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_swImportModal);
                FA_LOGI(_T("sensitive words: open import modal"));
            }
            break;

        //导入进行中：在弹窗里显示进度
        case SensitiveWordsScreen::kUiImportProgress:
            if (m_swImportModal != nullptr)
            {
                m_swImportModal->SetStatus(req.m_text, false);
            }
            break;

        //导入结束：在弹窗里显示汇总
        case SensitiveWordsScreen::kUiImportDone:
            if (m_swImportModal != nullptr)
            {
                m_swImportModal->ShowResult(req.m_text, req.m_isError);
            }
            break;

        //导出的内容已取到：弹「另存为」对话框写盘
        case SensitiveWordsScreen::kUiSaveExport:
            SaveTextToFile(req.m_bytes, filesave::TextFileSpec(req.m_text, _T("敏感词词库")));
            break;

        //不认识的请求种类：忽略
        default:
            break;
        }
    }
}

void AdminFrame::QaUiRequestThunk(void* user, const EnterpriseQaScreen::UiRequest& request)
{
    AdminFrame* self = static_cast<AdminFrame*>(user);
    //队列原本为空时才投递一次：同一轮里连着来的几条（如「关闭弹窗」与「已保存」提示）一起处理
    const bool wasEmpty = self->m_qaUiQueue.empty();
    self->m_qaUiQueue.push_back(request);
    if (wasEmpty)
    {
        ::PostMessage(self->m_hWnd, WM_APP_NAVIGATE, kNavQaUiRequests, 0);
    }
}

void AdminFrame::HandleQaUiRequests()
{
    std::vector<EnterpriseQaScreen::UiRequest> queue;
    queue.swap(m_qaUiQueue);
    for (size_t i = 0; i < queue.size(); ++i)
    {
        const EnterpriseQaScreen::UiRequest& req = queue[i];
        switch (req.m_kind)
        {
        //打开「新增企业问答」弹窗
        case EnterpriseQaScreen::kUiOpenAddModal:
            if (m_qaModal != nullptr && m_root != nullptr && m_qaScreen != nullptr)
            {
                m_qaModal->ShowForAdd(m_qaScreen->Groups());
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_qaModal);
                FA_LOGI(_T("enterprise qa: open add modal"));
            }
            break;

        //打开「编辑企业问答」弹窗，预填所选问答
        case EnterpriseQaScreen::kUiOpenEditModal:
            if (m_qaModal != nullptr && m_root != nullptr && m_qaScreen != nullptr)
            {
                m_qaModal->ShowForEdit(req.m_item, m_qaScreen->Groups());
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_qaModal);
                FA_LOGI(_T("enterprise qa: open edit modal, id: %lld"), req.m_item.m_id);
            }
            break;

        //删除前的确认框
        case EnterpriseQaScreen::kUiConfirmDelete:
        {
            m_confirmDeleteNodeId.Format(_T("%lld"), req.m_item.m_id);
            m_confirmTarget = kConfirmTargetQaDelete;
            ConfirmDialogSpec spec;
            spec.m_title = _T("删除企业问答");
            spec.m_message.Format(_T("确定删除问答「%s」吗？删除后，应用到的群里将不再显示它。"),
                                  req.m_item.m_question.GetString());
            spec.m_okText = _T("删除");
            spec.m_confirmDanger = true;
            ShowConfirmDialog(spec);
            break;
        }

        //弹窗里的提交成功：关闭弹窗
        case EnterpriseQaScreen::kUiCloseModal:
            HideQaModal();
            break;

        //弹窗里的提交失败：弹窗不关，显示原因，「保存」恢复可用
        case EnterpriseQaScreen::kUiModalError:
            if (m_qaModal != nullptr)
            {
                m_qaModal->SetBusy(false);
                m_qaModal->SetError(req.m_text);
            }
            break;

        //提示
        case EnterpriseQaScreen::kUiToast:
            ShowToast(req.m_text, req.m_isError ? ToastKind::Error : ToastKind::Info);
            break;

        //本企业的群到了：弹窗开着时补上群列表（保留已勾选的群）
        case EnterpriseQaScreen::kUiGroupsLoaded:
            if (m_qaModal != nullptr && m_qaScreen != nullptr)
            {
                m_qaModal->SetGroups(m_qaScreen->Groups());
            }
            break;

        //未知请求：不处理
        default:
            break;
        }
    }
}

void AdminFrame::HideQaModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
}

void AdminFrame::SubmitQaModal()
{
    if (m_qaModal == nullptr || m_qaScreen == nullptr || m_qaModal->IsBusy())
    {
        return;
    }
    CString error;
    if (!m_qaModal->Validate(&error))
    {
        m_qaModal->SetError(error);
        return;
    }
    m_qaModal->SetError(CString());
    m_qaModal->SetBusy(true);
    m_qaScreen->SubmitQa(m_qaModal->GetItem());
}

void AdminFrame::BotUiRequestThunk(void* user, const EnterpriseBotScreen::UiRequest& request)
{
    AdminFrame* self = static_cast<AdminFrame*>(user);
    //队列原本为空时才投递一次：同一轮里连着来的几条（如「关闭编辑窗」与「已保存」提示）一起处理
    const bool wasEmpty = self->m_botUiQueue.empty();
    self->m_botUiQueue.push_back(request);
    if (wasEmpty)
    {
        ::PostMessage(self->m_hWnd, WM_APP_NAVIGATE, kNavBotUiRequests, 0);
    }
}

void AdminFrame::HandleBotUiRequests()
{
    std::vector<EnterpriseBotScreen::UiRequest> queue;
    queue.swap(m_botUiQueue);
    for (size_t i = 0; i < queue.size(); ++i)
    {
        const EnterpriseBotScreen::UiRequest& req = queue[i];
        switch (req.m_kind)
        {
        //打开「新建机器人」编辑窗
        case EnterpriseBotScreen::kUiOpenAddModal:
            if (m_botModal != nullptr && m_root != nullptr && m_botScreen != nullptr)
            {
                m_botModal->SetServer(AppConfig::Instance().Server());
                m_botModal->SetDomains(m_botScreen->Domains(), m_botScreen->DomainsLoaded());
                m_botModal->ShowForAdd(m_botScreen->Groups());
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_botModal);
                FA_LOGI(_T("enterprise bot: open add modal"));
            }
            break;

        //打开「编辑机器人」编辑窗，预填详情（带规则与密钥）
        case EnterpriseBotScreen::kUiOpenEditModal:
            if (m_botModal != nullptr && m_root != nullptr && m_botScreen != nullptr)
            {
                m_botModal->SetServer(AppConfig::Instance().Server());
                m_botModal->SetDomains(m_botScreen->Domains(), m_botScreen->DomainsLoaded());
                m_botModal->ShowForEdit(req.m_bot, m_botScreen->Groups());
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_botModal);
                FA_LOGI(_T("enterprise bot: open edit modal, botid: %lld, rules: %u"), req.m_bot.m_botId,
                        static_cast<unsigned>(req.m_bot.m_rules.size()));
            }
            break;

        //编辑窗里的保存成功：关闭编辑窗
        case EnterpriseBotScreen::kUiCloseModal:
            HideBotModal();
            break;

        //编辑窗里的保存失败：编辑窗不关，显示原因，「保存」恢复可用
        case EnterpriseBotScreen::kUiModalError:
            if (m_botModal != nullptr)
            {
                m_botModal->SetBusy(false);
                m_botModal->SetError(req.m_text);
            }
            break;

        //提示
        case EnterpriseBotScreen::kUiToast:
            ShowToast(req.m_text, req.m_isError ? ToastKind::Error : ToastKind::Info);
            break;

        //本企业的群到了：编辑窗与规则编辑窗补上群列表（保留已勾选的群）
        case EnterpriseBotScreen::kUiGroupsLoaded:
            if (m_botScreen != nullptr && m_botModal != nullptr)
            {
                m_botModal->SetGroups(m_botScreen->Groups());
            }
            if (m_botScreen != nullptr && m_botRuleModal != nullptr)
            {
                m_botRuleModal->SetGroups(m_botScreen->Groups());
            }
            break;

        //确认框（删除机器人、重新生成令牌）：沿用文件共享管理的确认框通道，结果经 OnConfirmResult 回到机器人界面
        case EnterpriseBotScreen::kUiConfirm:
        {
            fsui::UiRequest confirm;
            confirm.m_kind = fsui::kUiConfirm;
            confirm.m_title = req.m_title;
            confirm.m_text = req.m_text;
            confirm.m_okText = req.m_okText;
            confirm.m_danger = req.m_danger;
            confirm.m_target = m_botScreen;
            FileShareUiRequestThunk(this, confirm);
            break;
        }

        //「试一试」的结果：显示在编辑窗里
        case EnterpriseBotScreen::kUiTryResult:
            if (m_botModal != nullptr)
            {
                m_botModal->SetTryResult(req.m_text, req.m_isError);
            }
            break;

        //令牌与签名密钥已重新生成：编辑窗正在编辑这个机器人时更新显示
        case EnterpriseBotScreen::kUiSecretsReset:
            if (m_botModal != nullptr)
            {
                m_botModal->SetSecrets(req.m_bot.m_botId, req.m_bot.m_webhookToken, req.m_bot.m_secret);
            }
            break;

        //打开「外部服务域名白名单」窗
        case EnterpriseBotScreen::kUiOpenDomainsModal:
            if (m_botDomainsModal != nullptr && m_root != nullptr)
            {
                m_botDomainsModal->ShowWithDomains(req.m_domains);
                SetEditsVisible(m_shellPage, false);
                m_root->ShowModal(m_botDomainsModal);
                FA_LOGI(_T("enterprise bot: open domains modal, count: %u"), static_cast<unsigned>(req.m_domains.size()));
            }
            break;

        //白名单已保存：关闭白名单窗
        case EnterpriseBotScreen::kUiCloseDomainsModal:
            HideBotDomainsModal();
            break;

        //白名单保存失败：窗口不关，显示原因，「保存」恢复可用
        case EnterpriseBotScreen::kUiDomainsModalError:
            if (m_botDomainsModal != nullptr)
            {
                m_botDomainsModal->SetBusy(false);
                m_botDomainsModal->SetError(req.m_text);
            }
            break;

        //白名单拉到了或改了：编辑窗与规则编辑窗本地校验外部服务地址时用新的白名单
        case EnterpriseBotScreen::kUiDomainsLoaded:
            if (m_botScreen != nullptr && m_botModal != nullptr)
            {
                m_botModal->SetDomains(m_botScreen->Domains(), m_botScreen->DomainsLoaded());
            }
            if (m_botScreen != nullptr && m_botRuleModal != nullptr)
            {
                m_botRuleModal->SetDomains(m_botScreen->Domains(), m_botScreen->DomainsLoaded());
            }
            break;

        //未知请求：不处理
        default:
            break;
        }
    }
}

void AdminFrame::HideBotModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
}

void AdminFrame::SubmitBotModal()
{
    if (m_botModal == nullptr || m_botScreen == nullptr || m_botModal->IsBusy())
    {
        return;
    }
    CString error;
    if (!m_botModal->Validate(&error))
    {
        m_botModal->SetError(error);
        return;
    }
    m_botModal->SetError(CString());
    m_botModal->SetBusy(true);
    m_botScreen->SubmitBot(m_botModal->GetBot());
}

void AdminFrame::OpenBotRuleModal(bool isNew)
{
    if (m_botModal == nullptr || m_botRuleModal == nullptr || m_botScreen == nullptr || m_root == nullptr
        || !m_botModal->IsVisible())
    {
        return;
    }
    botapi::BotRule rule;
    const bool ready = isNew ? m_botModal->BeginAddRule(&rule) : m_botModal->BeginEditRule(&rule);
    if (!ready)
    {
        return;
    }
    m_botModal->SetError(CString());
    m_botRuleModal->SetDomains(m_botScreen->Domains(), m_botScreen->DomainsLoaded());
    m_botRuleModal->ShowForRule(rule, isNew, m_botScreen->Groups());
    m_root->ShowModal(m_botRuleModal);
    FA_LOGI(_T("enterprise bot: open rule modal, new: %d, trigger: %d"), isNew ? 1 : 0, rule.m_trigger.m_type);
}

void AdminFrame::CloseBotRuleModal()
{
    if (m_root == nullptr || m_botModal == nullptr)
    {
        return;
    }
    //回到机器人编辑窗：它只是被隐藏，内容还在
    m_root->ShowModal(m_botModal);
}

void AdminFrame::SubmitBotRuleModal()
{
    if (m_botRuleModal == nullptr || m_botModal == nullptr || m_root == nullptr)
    {
        return;
    }
    CString error;
    if (!m_botRuleModal->Validate(&error))
    {
        m_botRuleModal->SetError(error);
        return;
    }
    m_botModal->ApplyRuleEdit(m_botRuleModal->GetRule());
    m_root->ShowModal(m_botModal);
}

void AdminFrame::RunBotTry()
{
    if (m_botModal == nullptr || m_botScreen == nullptr)
    {
        return;
    }
    const botapi::BotTryInput input = m_botModal->GetTryInput();
    if (input.m_text.IsEmpty())
    {
        m_botModal->SetTryResult(_T("请先输入一句要试的消息"), true);
        return;
    }
    m_botModal->SetTryResult(_T("正在判定…"), false);
    m_botScreen->RequestTry(m_botModal->GetBot(), input);
}

void AdminFrame::HideBotDomainsModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
}

void AdminFrame::SubmitBotDomainsModal()
{
    if (m_botDomainsModal == nullptr || m_botScreen == nullptr || m_botDomainsModal->IsBusy())
    {
        return;
    }
    CString error;
    if (!m_botDomainsModal->Validate(&error))
    {
        m_botDomainsModal->SetError(error);
        return;
    }
    m_botDomainsModal->SetError(CString());
    m_botDomainsModal->SetBusy(true);
    m_botScreen->SubmitDomains(m_botDomainsModal->GetDomains());
}

void AdminFrame::HideSensitiveWordModal()
{
    if (m_root == nullptr)
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
}

void AdminFrame::SubmitSensitiveWordModal()
{
    if (m_swModal == nullptr || m_sensitiveWordsScreen == nullptr || m_swModal->IsBusy())
    {
        return;
    }
    CString error;
    if (!m_swModal->Validate(&error))
    {
        m_swModal->SetError(error);
        return;
    }
    m_swModal->SetError(CString());
    m_swModal->SetBusy(true);
    m_sensitiveWordsScreen->SubmitWord(m_swModal->EditingId(), m_swModal->GetWord(),
                                       m_swModal->GetCategory(), m_swModal->GetRemark());
}

void AdminFrame::HideSensitiveImportModal()
{
    //导入进行中不关闭：批次还在依次提交，关掉之后结果没有地方显示（按钮此时本就不可用）
    if (m_root == nullptr || (m_swImportModal != nullptr && m_swImportModal->IsImporting()))
    {
        return;
    }
    m_root->HideModal();
    SetEditsVisible(m_shellPage, true);
}

void AdminFrame::SubmitSensitiveImportModal()
{
    if (m_swImportModal == nullptr || m_sensitiveWordsScreen == nullptr || m_swImportModal->IsImporting())
    {
        return;
    }
    //已经显示结果：主按钮是「完成」，关闭弹窗
    if (m_swImportModal->IsDone())
    {
        HideSensitiveImportModal();
        return;
    }
    CString error;
    if (!m_sensitiveWordsScreen->StartImport(m_swImportModal->GetText(), m_swImportModal->GetDefaultCategory(),
                                             &error))
    {
        m_swImportModal->SetStatus(error, true);
        return;
    }
    //StartImport 已同步发出第一批的进度请求，它排在界面请求队列里，稍后才到弹窗；这里先切到导入中
    m_swImportModal->SetImporting(true);
}

void AdminFrame::ReadSensitiveImportFile()
{
    if (m_swImportModal == nullptr || m_swImportModal->IsImporting() || m_swImportModal->IsDone())
    {
        return;
    }
    TCHAR pathBuf[MAX_PATH] = {};
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = _T("文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0");
    ofn.lpstrFile   = pathBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!::GetOpenFileName(&ofn))
    {
        // 用户取消对话框，不算失败。
        return;
    }

    HANDLE file = ::CreateFile(pathBuf, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        m_swImportModal->SetStatus(_T("无法打开所选文件"), true);
        FA_LOGW(_T("sensitive words: open import file failed, error: %lu"), ::GetLastError());
        return;
    }
    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart > static_cast<LONGLONG>(swtext::kImportFileMaxBytes))
    {
        ::CloseHandle(file);
        CString error;
        error.Format(_T("文件不能超过 %d MB"), static_cast<int>(swtext::kImportFileMaxBytes / (1024 * 1024)));
        m_swImportModal->SetStatus(error, true);
        FA_LOGW(_T("sensitive words: import file too large, bytes: %lld"), size.QuadPart);
        return;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = bytes.empty() ? TRUE
                                  : ::ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr);
    ::CloseHandle(file);
    if (!ok || read != static_cast<DWORD>(bytes.size()))
    {
        m_swImportModal->SetStatus(_T("读取文件失败"), true);
        FA_LOGW(_T("sensitive words: read import file failed, %lu of %d bytes"), read,
                static_cast<int>(bytes.size()));
        return;
    }

    const CString text = swtext::DecodeImportFile(bytes, nullptr);
    m_swImportModal->SetText(text);
    const std::vector<std::string> lines = swtext::SplitLines(text);
    CString status;
    status.Format(_T("已读取 %d 行，其中非空行 %d 行；确认无误后点「开始导入」"),
                  static_cast<int>(lines.size()), swtext::CountNonBlankLines(lines));
    m_swImportModal->SetStatus(status, false);
    FA_LOGI(_T("sensitive words: import file read, bytes: %d, lines: %d"), static_cast<int>(bytes.size()),
            static_cast<int>(lines.size()));
}

} // namespace flamingoadmin
