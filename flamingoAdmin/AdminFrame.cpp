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
#include "Screens/SettingsScreen.h"
#include "Screens/NetworkSettingsScreen.h"
#include "Mock/DataStore.h"
#include "Util/AppConfig.h"
#include "Util/NetProbe.h"
#include "Util/HttpClient.h"
#include "Util/HttpWorker.h"
#include "Util/AdminApi.h"
#include "Util/OrgTreeBuilder.h"   // kSyntheticRootId（尚未迁移的存量数据兜底）
#include "Util/DebugConsoleWindow.h"

#include "Controls/Basic/DuiLabel.h"

#include <thread>
#include "Util/Logger.h"

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Input/DuiEditHost.h"
#include "Controls/Basic/DuiButton.h"   // 「记住密码」复选框（StyleCheckbox）的读写
#include "Controls/List/DuiMenu.h"

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

// ---- 企业详情子 tab 序号（与 EnterpriseDetailScreen 内部序号一致）----
const int kDetailTabOrg     = 0;   // 组织架构 tab
const int kDetailTabService = 1;   // 服务号 tab

// 员工登录历史一次拉取的条数（设备登录 tab 无翻页，一次取够近期记录）。
const int kLoginRecordsPageSize = 50;

// ---- 仪表盘「活跃企业」列表的拉取参数 ----
const int kDashEntStatusAll = -1;     // status=-1：不按状态筛选
const int kDashEntPageSize  = 1000;   // 一次拉够（与企业管理界面同口径）

// ---- 公告列表的拉取参数 ----
const int kAnnStatusAll     = -1;     // status=-1：不按状态筛选
const int kAnnFetchPageSize = 1000;   // 一次拉够（公告中心无分页 UI）

// 仪表盘「最新公告 / 近期审计」卡只展示开头几条，拉一小页即可。
const int kDashAnnPageSize   = 10;    // 仪表盘最新公告拉取条数
const int kDashAuditPageSize = 10;    // 仪表盘近期审计拉取条数

// ---- 企业状态 / 套餐编码（与 docs/Admin控制台接口文档.md 模块 3 一致）----
const int kEntStatusActive    = 1;   // 企业状态：活跃
const int kEntStatusSuspended = 2;   // 企业状态：停用
const int kPlanProfessional   = 1;   // 套餐：专业版
const int kPlanEnterprise     = 2;   // 套餐：企业版
const int kPlanUltimate       = 3;   // 套餐：旗舰版

// 确认弹窗 m_confirmTarget 取值：3=删除企业，4=重置员工密码，5=启用/停用员工。
const int kConfirmTargetEnterprise = 3;
const int kConfirmTargetResetPwd   = 4;
const int kConfirmTargetSetStatus  = 5;
const int kConfirmTargetAnnDelete  = 6;   // 公告物理删除(任意状态,区别于 2=撤回)

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
// SaveCsvToFile 写盘（该函数约定接收 UTF-8）。
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

// 公告级别（客户端 Severity）→ 中文文案（用于 CSV 导出）。
LPCTSTR AnnSeverityText(Severity sev)
{
    switch (sev)
    {
    case Severity::Warn:
        return _T("提醒");
    case Severity::Danger:
        return _T("警告");
    default:
        return _T("普通");
    }
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
    case Screen::EnterpriseDetail:
    default:
        // 企业详情是「企业管理」的子屏：侧栏高亮 / 面包屑沿用企业管理。
        return { kIdNavEnterprises, ui::Icon::Building, _T("企业管理"),
                 _T("企业入驻、组织架构与服务号管理") };
    }
}

// 把套餐名映射为服务端套餐编号（与 docs/Admin控制台接口文档.md 约定一致）。
//   planName：新增企业弹窗套餐下拉框的当前文本。
//   返回：套餐编号；未知文本回退为「专业版」。
int PlanCodeFromName(const CString& planName)
{
    if (planName == _T("企业版"))
    {
        return kPlanEnterprise;
    }
    if (planName == _T("旗舰版"))
    {
        return kPlanUltimate;
    }
    return kPlanProfessional;
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
    // OnLButtonDblClk —— balloonui 窗口类未注册 CS_DBLCLKS、系统不发
    // WM_LBUTTONDBLCLK，须经此运行期接口让 DuiHost 自行合成双击。详见
    // DuiHost::EnableDoubleClick 注释。
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
        // 顶栏审计滚动提示前进一条。
        if (m_topBar != nullptr)
        {
            m_topBar->AdvanceTicker();
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
            ShowToast(_T("更多筛选将在后续阶段开放"), ToastKind::Warn);
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
            ShowToast(_T("发布公告将在 Phase 5 接入"), ToastKind::Warn);
            break;
        case kIdDetailPrimary:
            // 组织架构 tab：新增员工到默认部门；服务号 tab：Phase 4b。
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
                ShowToast(_T("新建服务号将在 Phase 4b 接入"), ToastKind::Warn);
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
                CString msg;
                msg.Format(_T("确定要删除「%s」吗？该节点及其下属子节点将被")
                           _T("一并移除，此操作不可撤销。"),
                           static_cast<LPCTSTR>(name));
                m_confirmDeleteNodeId = nodeId;
                m_confirmTarget       = 0;   // 0 = 组织节点
                ConfirmDialogSpec spec;
                spec.m_title         = _T("删除组织节点");
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
                    HttpRequest req;
                    if (pend == EnterpriseDetailScreen::Pending::SetRole)
                    {
                        req = adminapi::MakeUserSetRoleRequest(
                            server, token, uid, panel->GetUserRole());
                        req.m_tag = kApiTagUserSetRole;
                    }
                    else
                    {
                        req = adminapi::MakeUserSetPermissionsRequest(
                            server, token, uid, panel->GetPermOverrides());
                        req.m_tag = kApiTagUserSetPerms;
                    }
                    req.m_replyHwnd = m_hWnd;
                    req.m_replyMsg  = WM_APP_API_RESULT;
                    HttpWorker::Instance().Post(req);
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
                spec.m_message       = _T("确定重置该员工的登录密码吗?重置后将生成新密码,")
                                       _T("该员工的旧密码立即失效。");
                spec.m_okText        = _T("重置");
                spec.m_confirmDanger = true;
                ShowConfirmDialog(spec);
            }
            else if (pend == EnterpriseDetailScreen::Pending::KickDevices)
            {
                // 强制下线全部设备：deviceid 传空。
                m_permUserId = nodeId;
                HttpRequest req = adminapi::MakeUserKickDeviceRequest(
                    AppConfig::Instance().Server(),
                    AdminSession::Instance().Token(), _ttoi64(nodeId),
                    CString());
                req.m_replyHwnd = m_hWnd;
                req.m_replyMsg  = WM_APP_API_RESULT;
                req.m_tag       = kApiTagUserKickDevice;
                HttpWorker::Instance().Post(req);
                ShowToast(_T("正在强制下线全部设备…"));
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
        case kIdEntSettings:
            // 弹窗自身上报：某管理员行点了「移除」。
            if (m_enterpriseSettingsModal != nullptr)
            {
                const long long aid =
                    m_enterpriseSettingsModal->GetPendingRemoveAdminId();
                if (aid != 0)
                {
                    HttpRequest req = adminapi::MakeEnterpriseRemoveAdminRequest(
                        AppConfig::Instance().Server(),
                        AdminSession::Instance().Token(), aid);
                    req.m_replyHwnd = m_hWnd;
                    req.m_replyMsg  = WM_APP_API_RESULT;
                    req.m_tag       = kApiTagEntRemoveAdmin;
                    HttpWorker::Instance().Post(req);
                    ShowToast(_T("正在移除管理员…"));
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
                // 打开绑定范围弹窗，载入该服务号当前绑定。
                const CString svcId = sp->GetSelectedServiceId();
                const std::vector<Service>& list =
                    DataStore::Instance().Services(sp->GetEnterpriseId());
                for (const Service& s : list)
                {
                    if (s.m_id == svcId)
                    {
                        m_bindingModal->Configure(s.m_name,
                                                  sp->GetEnterpriseId(), s.m_bindings);
                        ::PostMessage(m_hWnd, WM_APP_NAVIGATE,
                                      kNavShowBindingModal, 0);
                        break;
                    }
                }
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
        case kIdAnnPreviewRepush:
        {
            // 复制 / 重新推送:与原 PaintPreview 时代 Pending::Misc 行为一致 ——
            // 后续阶段开放,目前先 Toast 提示。
            ShowToast(_T("该功能将在后续阶段开放"), ToastKind::Warn);
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
            // 企业详情组织树搜索框文字变化 → 延后一拍再过滤组织树。若在此处(即
            // EN_CHANGE 同一消息周期)同步过滤会触发树重绘,导致搜索框 EDIT 刚输入
            // 的文字画不出来(须再点一下);PostMessage 延迟到本轮派发结束后执行,
            // 让按键这一周期只走 EDIT 自身刷新,文字即可正常显示。
            ::PostMessage(m_hWnd, WM_APP_ORG_FILTER, 0, 0);
            break;
        case kIdEntSeg:
            if (m_enterprises != nullptr)
            {
                m_enterprises->OnSegmentChanged(static_cast<int>(n->extra));
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

        EnterShell();
    }
    else
    {
        // 失败：用中文提示告知用户，停留在登录界面。
        ShowToast(login.m_message);
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
        OnServiceActionDone(*http, _T("绑定范围已保存"));
        break;
    case kApiTagSvcListReload:
        OnServiceListReloaded(*http);
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
    DataStore::Instance().PushAudit(_T("login.success"), _T("-"),
                                    Severity::Info, _T("通过邮箱密码登录"));

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

    // 默认进入仪表板。
    SwitchScreen(Screen::Dashboard);

    // 启动顶栏审计滚动提示定时器（登出时停掉）。
    SetTimer(kTickerTimerId, kTickerIntervalMs);

    FA_LOGI(_T("登录成功，进入主界面外壳"));
}

void AdminFrame::SwitchScreen(Screen screen)
{
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
        StartLoadProfile();
    }
    else if (screen == Screen::ClientVersions && m_clientVersionsScreen != nullptr)
    {
        StartLoadClientVersions();
    }

    // 切换可见性（屏常驻、不销毁）。
    m_screenHost->ShowScreen(m_screens[static_cast<int>(screen)]);

    // 同步侧栏高亮与顶栏面包屑。
    const ScreenInfo info = GetScreenInfo(screen);
    if (m_sidebar != nullptr)
    {
        m_sidebar->SetActiveNav(info.m_navId);
    }
    if (m_topBar != nullptr)
    {
        m_topBar->SetBreadcrumb(info.m_name);
    }

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

    // 统计卡：dashboard/stats.do。
    HttpRequest reqStats = adminapi::MakeDashboardStatsRequest(server, token);
    reqStats.m_replyHwnd = m_hWnd;
    reqStats.m_replyMsg  = WM_APP_API_RESULT;
    reqStats.m_tag       = kApiTagDashStats;
    HttpWorker::Instance().Post(reqStats);

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

    FA_LOGI(_T("仪表盘：发起 stats + 活跃企业 + 公告 + 审计 + 消息趋势 加载"));
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
        ShowToast(r.m_message);
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
        // 活跃企业卡加载失败不打扰用户（统计卡可能仍成功），仅记日志。
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
    m_dashboard->Invalidate();
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
        // 卡片加载失败不打扰用户（其它卡可能仍成功），仅记日志。
        FA_LOGW(_T("仪表盘最新公告加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetAnnouncements(r.m_list);
    m_dashboard->Invalidate();
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
        FA_LOGW(_T("仪表盘近期审计加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetAuditLogs(r.m_list);
    m_dashboard->Invalidate();
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
        FA_LOGW(_T("仪表盘消息趋势加载失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetMessageTrend(r.m_points);
    m_dashboard->Invalidate();
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
        ShowToast(r.m_message);
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
        ShowToast(result.m_message);
        FA_LOGW(_T("导出审计日志失败：%s"), result.m_message.GetString());
        return;
    }
    SaveCsvToFile(result.m_csvUtf8, _T("审计日志.csv"));
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);   // 失败保留已填内容，便于改正
        FA_LOGW(_T("修改密码失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(_T("登录密码已修改"));
    if (m_settingsScreen != nullptr)
    {
        m_settingsScreen->ClearPasswordFields();
    }
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
        FA_LOGW(_T("公告写操作失败：%s"), r.m_message.GetString());
        return;
    }
    ShowToast(okText);
    StartLoadAnnouncements();   // 重拉列表反映最新状态
}

void AdminFrame::HandleLogout()
{
    DataStore::Instance().PushAudit(_T("logout"), _T("-"),
                                    Severity::Info, _T("用户主动登出"));

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
    KillTimer(kTickerTimerId);            // 停掉顶栏审计滚动提示
    if (m_root != nullptr && m_loginPage != nullptr)
    {
        m_root->ShowPage(m_loginPage);   // 只切显隐，不销毁外壳
    }
    FA_LOGI(_T("用户登出"));
}

void AdminFrame::HandleSessionExpired()
{
    // 与 HandleLogout 类似，但不通知服务端（token 本就已失效），并给出提示。
    AdminSession::Instance().Clear();
    m_loggedIn = false;
    m_detailLoadEntId.Empty();   // 放弃可能在途的企业详情加载
    // 复位各「请求进行中」标志，避免会话失效后卡住后续操作。
    m_entCreateBusy   = false;
    m_entExportBusy   = false;
    m_auditExportBusy = false;
    m_updateProfileBusy = false;
    m_changePwBusy    = false;
    m_annActionBusy   = false;
    m_composeSubmitBusy = false;
    m_nodeSubmitBusy  = false;
    m_svcSubmitBusy   = false;
    m_empEditBusy     = false;
    m_entUpdateBusy   = false;
    m_addAdminBusy    = false;
    m_entSettingsOpen = false;
    KillTimer(kTickerTimerId);
    if (m_root != nullptr && m_loginPage != nullptr)
    {
        m_root->ShowPage(m_loginPage);
    }
    ShowToast(_T("登录已过期，请重新登录"), ToastKind::Error);
    FA_LOGW(_T("会话失效（code=2），已退回登录页"));
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
    DuiEditHost* account = m_loginPage != nullptr
        ? dynamic_cast<DuiEditHost*>(m_loginPage->FindCtrlById(kIdLoginAccount))  : nullptr;
    DuiEditHost* password = m_loginPage != nullptr
        ? dynamic_cast<DuiEditHost*>(m_loginPage->FindCtrlById(kIdLoginPassword)) : nullptr;

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
    // 外壳内的真 EDIT 子窗会绘制在半透明遮罩之上，须先隐藏。
    SetEditsVisible(m_shellPage, false);
    // ShowModal 会把弹窗 SetVisible(true)，触发 HWND 可见性同步把弹窗内
    // 5 个 EDIT 子窗显示出来（此刻主窗口已显示，同步可靠）。
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
    SetEditsVisible(m_shellPage, true);   // 恢复外壳内的真 EDIT 子窗
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
    const int     plan     = PlanCodeFromName(m_modal->GetPlan());

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
        ShowToast(result.m_message);
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
        ShowToast(result.m_message);
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
    // keyword 空、status 取 -1：导出全部企业。
    HttpRequest request = adminapi::MakeEnterpriseExportRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        CString(), -1);
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
        ShowToast(result.m_message);
        FA_LOGW(_T("导出企业列表失败：%s"), result.m_message.GetString());
        return;
    }
    SaveCsvToFile(result.m_csvUtf8, _T("企业列表.csv"));
}

void AdminFrame::SaveCsvToFile(const std::string& csvUtf8, LPCTSTR defaultName)
{
    // 弹「另存为」对话框，预填调用方给的默认文件名。
    TCHAR pathBuf[MAX_PATH] = {};
    lstrcpyn(pathBuf, defaultName, MAX_PATH);
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = _T("CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0");
    ofn.lpstrFile   = pathBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = _T("csv");
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
        FA_LOGW(_T("导出企业列表：打开目标文件失败"));
        return;
    }
    // 先写 UTF-8 BOM，确保 Excel 等工具正确识别中文。
    const BYTE kUtf8Bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    ::WriteFile(file, kUtf8Bom, sizeof(kUtf8Bom), &written, nullptr);
    if (!csvUtf8.empty())
    {
        ::WriteFile(file, csvUtf8.data(),
                    static_cast<DWORD>(csvUtf8.size()), &written, nullptr);
    }
    ::CloseHandle(file);

    ShowToast(_T("企业列表已导出"));
    FA_LOGI(_T("企业列表已导出到文件"));
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

    // 「打开详情」子菜单：组织架构 / 服务号两个子 tab。子菜单为借用，须在
    // 整个 TrackPopup 期间保活，故作为本函数局部变量。
    DuiMenu detailSub;
    detailSub.AppendItem(kMenuEntDetailOrg, _T("组织架构"));
    detailSub.AppendItem(kMenuEntDetailSvc, _T("服务号"));

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
        // 改状态：审核通过 / 启用 → 活跃；停用 → 停用。
        const int newStatus = (chosen == kMenuEntSuspend) ? kEntStatusSuspended
                                                          : kEntStatusActive;
        HttpRequest request = adminapi::MakeEnterpriseSetStatusRequest(
            AppConfig::Instance().Server(), AdminSession::Instance().Token(),
            _ttoi64(ent.m_id), newStatus);
        request.m_replyHwnd = m_hWnd;
        request.m_replyMsg  = WM_APP_API_RESULT;
        request.m_tag       = kApiTagEntSetStatus;
        HttpWorker::Instance().Post(request);
        ShowToast(_T("正在更新企业状态…"));
        FA_LOGI(_T("企业 %s：提交改状态 -> %d"),
                ent.m_id.GetString(), newStatus);
        break;
    }
    default:
        break;
    }
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
    SaveCsvToFile(Utf8FromWide(csv), defaultName);
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
    SaveCsvToFile(Utf8FromWide(csv), _T("公告列表.csv"));
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
    // 顶层 HWND 天然盖得住主窗口里的任何子窗口，故【不再】需要像 overlay 弹窗
    // 那样先 SetEditsVisible(m_shellPage, false) 去藏外壳里的真 EDIT 子窗。
    m_nodeModal = new NodeModal(
        this, m_hWnd,
        m_nodeModalPendingIsDept ? NodeModal::Mode::AddDept
                                 : NodeModal::Mode::AddUser,
        m_nodeModalParentName);
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
    // 企业与成员列表已在 OnDuiNotify 里经 SetEnterprise 装好。隐藏外壳内
    // 真 EDIT 子窗，再显示弹窗。
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
        ShowToast(r.m_message);   // 失败保留弹窗，让用户改后重试
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
        dlg->GetSignature());
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);   // 失败保留弹窗
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
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
    if (m_entUpdateBusy)
    {
        ShowToast(_T("正在提交，请稍候…"));
        return;
    }
    m_entUpdateBusy = true;
    HttpRequest request = adminapi::MakeEnterpriseUpdateRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(m_enterpriseSettingsModal->GetEnterpriseId()),
        m_enterpriseSettingsModal->GetName(),
        m_enterpriseSettingsModal->GetShortName(),
        m_enterpriseSettingsModal->GetIndustry(),
        m_enterpriseSettingsModal->GetRegion(),
        m_enterpriseSettingsModal->GetPlan());
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
        ShowToast(r.m_message);   // 失败保留弹窗
        FA_LOGW(_T("保存企业信息失败：%s"), r.m_message.GetString());
        return;
    }
    HideEnterpriseSettings();
    ShowToast(_T("企业信息已保存"));
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
        ShowToast(r.m_message);   // 失败保留弹窗
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
        ShowToast(r.m_message);
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
        // deptdesc / leaderuserid / deptorder 当前弹窗未提供，留空 / 0。
        HttpRequest request = adminapi::MakeDeptCreateRequest(
            server, token, entId, parentId, name, CString(), 0, 0);
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
            CString());
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
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
    if (m_composeModal == nullptr || m_root == nullptr)
    {
        return;
    }
    m_composeModal->Reset();
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
        FA_LOGW(_T("加载待编辑公告失败：%s"), r.m_message.GetString());
        return;
    }
    m_composeModal->LoadForEdit(
        _ttoi64(r.m_announcement.m_id), r.m_announcement.m_title,
        r.m_announcement.m_body, r.m_severityCode, r.m_audienceType,
        r.m_announcement.m_pinned, r.m_targets);
    PresentComposeModal();
    FA_LOGI(_T("打开编辑公告弹窗：%s"), r.m_announcement.m_id.GetString());
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
    HttpRequest request = adminapi::MakeServiceSetStatusRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(svcId), target);
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagSvcSetStatus;
    HttpWorker::Instance().Post(request);
    ShowToast(_T("正在更新服务号状态…"));
}

void AdminFrame::ShowBindingModal()
{
    if (m_bindingModal == nullptr || m_root == nullptr)
    {
        return;
    }
    // 弹窗内容已在 OnDuiNotify 里经 Configure 配置好。
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
    ServicesPanel* sp = m_enterpriseDetail->GetServicesPanel();
    if (sp == nullptr)
    {
        HideBindingModal();
        return;
    }
    HttpRequest request = adminapi::MakeServiceSetBindingsRequest(
        AppConfig::Instance().Server(), AdminSession::Instance().Token(),
        _ttoi64(sp->GetSelectedServiceId()), m_bindingModal->GetBindings());
    request.m_replyHwnd = m_hWnd;
    request.m_replyMsg  = WM_APP_API_RESULT;
    request.m_tag       = kApiTagSvcSetBindings;
    HttpWorker::Instance().Post(request);
    HideBindingModal();
    ShowToast(_T("正在保存绑定范围…"));
}

void AdminFrame::OnServiceSaved(const HttpResult& http)
{
    m_svcSubmitBusy = false;
    const EnterpriseActionResult r =
        adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(r.m_message);   // 失败保留弹窗
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
        ShowToast(r.m_message);
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
    const ServiceListResult r = adminapi::ParseServiceListResponse(http);
    if (!r.Succeeded())
    {
        ShowToast(_T("刷新服务号列表失败"), ToastKind::Error);
        FA_LOGW(_T("刷新服务号列表失败：%s"), r.m_message.GetString());
        return;
    }
    DataStore::Instance().SetServices(m_enterpriseDetail->GetEnterpriseId(),
                                      r.m_list);
    m_enterpriseDetail->Refresh();
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
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
    ShowToast(r.m_message);
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
        ShowToast(r.m_message);
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
        ShowToast(r.m_message);
        FA_LOGW(_T("重置员工密码失败：%s"), r.m_message.GetString());
        return;
    }
    // 用信息弹窗展示新密码（与新增企业 / 员工一致：单按钮 + 复制）。
    CString msg;
    msg.Format(_T("员工登录密码已重置。\n新密码：%s\n")
               _T("请妥善保存并转交，此密码仅显示一次。"),
               static_cast<LPCTSTR>(r.m_newPassword));
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
        ShowToast(r.m_message);
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
    // 优先用 ini 里记下的上次登录信息；本机从未登录过时，才回退到测试数据库
    // （testdata.sql）里的平台超级管理员账户，保持联调演示的便利。
    if (m_loginPage == nullptr)
    {
        return;
    }

    const LoginConfig& saved = AppConfig::Instance().Login();

    CString accountText  = saved.m_account;
    // 没勾"记住密码"时 m_password 本就是空的，这里再判一次是为了让意图显式：
    // 密码只在用户明确要求记住时才回填。
    CString passwordText = saved.m_rememberPassword ? saved.m_password : CString();

    if (accountText.IsEmpty())
    {
        accountText  = _T("li@flamingo.com");
        passwordText = _T("admin123");
    }

    if (DuiEditHost* account = dynamic_cast<DuiEditHost*>(
            m_loginPage->FindCtrlById(kIdLoginAccount)))
    {
        account->SetText(accountText);
    }
    if (DuiEditHost* password = dynamic_cast<DuiEditHost*>(
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

    if (DuiEditHost* edit = dynamic_cast<DuiEditHost*>(node))
    {
        edit->SetVisible(visible);
    }
    for (const auto& child : node->Children())
    {
        SetEditsVisible(child.get(), visible);
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
        ShowToast(r.m_message, ToastKind::Warn);
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
        ShowToast(r.m_message, ToastKind::Warn);
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
        ShowToast(r.m_message, ToastKind::Warn);
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

    StartClientVersionAction(static_cast<int>(pending.m_action), pending.m_id);
}

void AdminFrame::ShowClientVersionModal(const adminapi::ScanPackagesResult& scan)
{
    if (m_clientVersionModal == nullptr || m_root == nullptr)
    {
        return;
    }

    m_clientVersionModal->SetPackages(scan);
    // 隐藏外壳内的真 EDIT 子窗，否则它们会透过半透明遮罩显形
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

} // namespace flamingoadmin
