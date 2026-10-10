/**
 *  flamingoAdmin —— 单元测试程序入口（console）。
 *
 *  flamingoAdminTests 是与主程序并列的独立 console 工程，只编译 flamingoAdmin
 *  的数据层（Mock/ 与 Util/），不依赖 balloonui，用于跑 Mock 数据层单元测试。
 *  进程退出码：全部通过返回 0，有失败返回 1，便于 CI / 脚本判定。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"

// 满足 stdafx.h 中 `extern CAppModule _Module;` 的定义需求。测试进程只用到
// ATL CString 与 Win32 API，无需 _Module.Init()，定义一个实例即可。
CAppModule _Module;

namespace flamingoadmin { namespace test {

// 在 DataStoreTests.cpp 中定义：依次运行全部 Mock 数据层用例。
void RunAllDataStoreTests();

// 在 DebugConsoleTests.cpp 中定义：依次运行全部调试控制台数据层用例。
void RunAllDebugConsoleTests();

// 在 ScrollbarMetricsTests.cpp 中定义：运行自绘悬浮滚动条几何计算用例。
void RunAllScrollbarMetricsTests();

// 在 CommonWidgetsTests.cpp 中定义:运行 Widgets/CommonWidgets.cpp 里 UI
// 控件的非绘制路径用例(目前覆盖 StatTile 的 7 条)。
void RunAllCommonWidgetsTests();

// 在 AnnouncementPreviewPanelTests.cpp 中定义:运行公告中心右栏「公告详情」
// 面板(Screens/AnnouncementPreviewPanel)的 8 条用例,验证 SetAnnouncement
// 把 7 个★强项文字字段正确转写到 DuiLabel/DuiRichEdit 子控件,以及
// pinned/scheduled/status 切换时的可见性 + 按钮文字。
void RunAllAnnouncementPreviewPanelTests();

// 在 RichEditScrollTests.cpp 中定义:公告正文滚动机制集成测试(验证无窗口
// 富文本控件的滚动接口与滚轮行为;不需要任何窗口)。
void RunAllRichEditScrollTests();

// 在 ListPagingTests.cpp 中定义:列表分页纯函数(Util/ListPaging)单元测试。
void RunAllListPagingTests();

// 在 OrgTreeFilterTests.cpp 中定义:组织树 OrgTree 搜索过滤单元测试。
void RunAllOrgTreeFilterTests();

// 在 PinyinTests.cpp 中定义:拼音首字母工具(Util/Pinyin)单元测试。
void RunAllPinyinTests();

// 在 OrgTreeBuilderTests.cpp 中定义:组织架构树组装(Util/OrgTreeBuilder)单元
// 测试,覆盖"公司根部门"作为树根的识别、挂接与不可删除标记。
void RunAllOrgTreeBuilderTests();

// 在 OrgToolbarLayoutTests.cpp 中定义:组织树工具条几何计算
// (Util/OrgToolbarLayout)单元测试,覆盖搜索框与刷新 / 展开 / 折叠三个图标
// 按钮的摆位。
void RunAllOrgToolbarLayoutTests();

// 在 DpapiSecretTests.cpp 中定义:本地口令保护(Util/DpapiSecret)单元测试,
// 覆盖"记住密码"落盘前的 DPAPI 加解密往返、单行 Base64 格式约束,以及密文
// 被截断 / 篡改 / 非法时的安全失败。
void RunAllDpapiSecretTests();

// 在 ClientVersionApiTests.cpp 中定义：运行客户端版本管理 API
// （模块 10）的 7 条用例，覆盖 6 对「构造请求 / 解析应答」纯函数
// 与包大小格式化。
void RunAllClientVersionApiTests();

// 在 SensitiveWordTests.cpp 中定义：运行敏感词管理（docs/敏感词方案.md 第 7 节）的
// 纯文本逻辑与接口封装用例（SA 系列）。
void RunAllSensitiveWordTests();

// 在 EnterpriseQaTests.cpp 中定义：运行企业问答（docs/群问答方案.md）的接口封装、弹窗与界面用例（EQA 系列）。
void RunAllEnterpriseQaTests();

// 在 EnterpriseBotTests.cpp 中定义：运行企业机器人（docs/机器人方案.md 第 7 节）的接口封装、规则文字与校验、
// 机器人页、编辑窗、规则编辑窗、白名单窗与四个功能开关的用例（EBOT 系列）。
void RunAllEnterpriseBotTests();

// 在 SharedTableWidgetsTests.cpp 中定义：运行表格类共用控件（Widgets/PagerBar 分页条、
// Widgets/ActionTable 带操作链接的表格）的用例（PB、AT 系列）。
void RunAllSharedTableWidgetsTests();

// 在 SensitiveWordsScreenTests.cpp 中定义：运行敏感词管理界面的用例（SB 系列）。
void RunAllSensitiveWordsScreenTests();

// 在 SensitiveImportExportTests.cpp 中定义：运行敏感词批量导入、导出与导出文件存盘规格的用例
// （ST、SI、SE、SIM 系列）。
void RunAllSensitiveImportExportTests();

// 在 SensitiveHitsTests.cpp 中定义：运行敏感词界面「拦截记录」页签的用例（SH 系列）。
void RunAllSensitiveHitsTests();

// 在 FileShareAdminApiTests.cpp 中定义：运行文件共享管理（模块 12）的接口封装与显示文字用例（FA、FT 系列）。
void RunAllFileShareAdminApiTests();

// 在 FileShareScreensTests.cpp 中定义：运行文件共享管理界面（功能开关面板、「文件共享」页、「共享审计」页）
// 及其用到的共用控件的用例（AT-5、KV、FSP、FS、FAU 系列）。
void RunAllFileShareScreensTests();

// 在 FeatureSwitchesPageTests.cpp 中定义：运行企业详情「功能开关」子 tab（FeatureSwitchesPage）的用例（FSW 系列）。
void RunAllFeatureSwitchesPageTests();

// 在 ServiceBindingTests.cpp 中定义：服务号绑定范围的读取、翻译与保留（SVD 系列）。
void RunAllServiceBindingTests();

// 在 ComposeEditScheduleTests.cpp 中定义：编辑定时公告时回填定时状态（ANE 系列）。
void RunAllComposeEditScheduleTests();

// 在 ClientVersionValidateTests.cpp 中定义：客户端版本「最低版本」的格式与大小校验（CVV 系列）。
void RunAllClientVersionValidateTests();

// 在 EmployeeFormTests.cpp 中定义：员工性别编码与部门负责人选择（GEN、LDR 系列）。
void RunAllEmployeeFormTests();

// 在 RememberPasswordTests.cpp 中定义：修改登录密码后同步「记住密码」口令（RPW 系列）。
void RunAllRememberPasswordTests();

// 在 ShellSessionTests.cpp 中定义：外壳随登录者刷新（GRT、TBR、TCK、SHL 系列）。
void RunAllShellSessionTests();

// 在 LoginTextTests.cpp 中定义：登录页加密说明、版本行与账号密码回填（LGN 系列）。
void RunAllLoginTextTests();

// 在 AnnouncementRepushTests.cpp 中定义：公告重新推送的请求构造与应答解析（ARP3）。
void RunAllAnnouncementRepushTests();

// 在 EnterpriseFilterTests.cpp 中定义：企业列表「更多筛选」的请求构造与按钮文字（ELF5）。
void RunAllEnterpriseFilterTests();

// 在 ColumnFitTests.cpp 中定义：表格列宽按可用宽度分配（COL1）。
void RunAllColumnFitTests();

// 在 TimeTextTests.cpp 中定义：界面时间显示格式（FMT1）。
void RunAllTimeTextTests();

// 在 AdminFrameGuardTests.cpp 中定义：读 AdminFrame.cpp 源码检查写法约定的守卫用例（TG 系列）。
void RunAllAdminFrameGuardTests();

// 在 FlamingoLinkTests.cpp 中定义：FlamingoLink.dll 纯函数部分（报文、名单、界面文字、工程守卫，FLK1~FLK7、FLK9）。
void RunAllFlamingoLinkTests();

// 在 FlamingoLinkLiveTests.cpp 中定义：加载 FlamingoLink.dll 对本地假服务端的黑盒测试（FLK8）。
void RunAllFlamingoLinkLiveTests();

// 在 FlamingoLinkHostTests.cpp 中定义：控制台接入 FlamingoLink.dll（login.do 新字段、页面状态、DLL 装卸，FLK10~FLK12）。
void RunAllFlamingoLinkHostTests();

}} // namespace flamingoadmin::test

int main()
{
    // 控制台按 UTF-8 输出，保证测试信息里的中文不乱码。
    ::SetConsoleOutputCP(CP_UTF8);

    flamingoadmin::test::RunAllDataStoreTests();

    // 运行调试控制台数据层（DebugConsole）单元测试。
    flamingoadmin::test::RunAllDebugConsoleTests();

    // 运行自绘悬浮滚动条几何计算（ScrollbarMetrics）单元测试。
    flamingoadmin::test::RunAllScrollbarMetricsTests();

    // 运行 UI 控件层(CommonWidgets)单元测试 —— 静态链接 balloonui_static
    // 后,在无 host 下裸构造控件并断言"参数 → 状态"的转写正确。
    flamingoadmin::test::RunAllCommonWidgetsTests();

    // 运行公告中心「公告详情」面板单元测试 —— 在无 host 下裸构造 PreviewPanel,
    // 断言 SetAnnouncement 后 7 个★强项文字字段 / 双状态卡可见性 / 5 按钮
    // 文字切换正确;面板内的控件都是无窗口控件,不依赖任何窗口,构造完即可断言。
    flamingoadmin::test::RunAllAnnouncementPreviewPanelTests();

    // 运行公告正文滚动机制集成测试 —— 验证正文控件的滚动接口
    // (GetVScrollMetrics / SetVScrollPos)与滚轮行为(溢出时消费、
    // 装得下时不吞,让预览区整页滚动能接手)。
    flamingoadmin::test::RunAllRichEditScrollTests();

    // 运行列表分页纯函数(Util/ListPaging)单元测试。
    flamingoadmin::test::RunAllListPagingTests();

    // 运行组织树 OrgTree 搜索过滤单元测试。
    flamingoadmin::test::RunAllOrgTreeFilterTests();

    // 运行拼音首字母工具(Util/Pinyin)单元测试。
    flamingoadmin::test::RunAllPinyinTests();

    // 运行组织架构树组装单元测试 —— 验证公司根部门被正确识别为树根、顶级部门与
    // 成员各就各位,以及公司节点被标记为不可删除。
    flamingoadmin::test::RunAllOrgTreeBuilderTests();

    // 运行组织树工具条几何计算单元测试 —— 验证搜索框与刷新 / 展开 / 折叠
    // 三个图标按钮的摆位，含工具条过窄、按钮个数越界等边界情形。
    flamingoadmin::test::RunAllOrgToolbarLayoutTests();

    // 运行本地口令保护单元测试 —— 验证"记住密码"落盘前的 DPAPI 加解密往返、
    // 密文的单行 Base64 格式，以及密文被截断 / 篡改 / 非法时安全失败不崩。
    flamingoadmin::test::RunAllDpapiSecretTests();

    // 运行客户端版本管理 API 单元测试 —— 验证模块 10 六个接口的
    // 请求体字段与应答解析，含超过 2GiB 的包大小不被截断。
    flamingoadmin::test::RunAllClientVersionApiTests();

    // 运行敏感词管理单元测试 —— 导入文件解码、切行与分批、导出存盘格式、
    // 分类名与来源名（SA-3～SA-6），以及模块 11 八个接口的请求与应答（SA-1、SA-2）。
    flamingoadmin::test::RunAllSensitiveWordTests();

    // 运行企业问答单元测试 —— 五个接口的请求与应答、本地校验、编辑弹窗与企业问答页（EQA-1～EQA-4）。
    flamingoadmin::test::RunAllEnterpriseQaTests();

    // 运行企业机器人单元测试 —— 十个接口的请求与应答、规则文字与本地校验、机器人页、编辑窗、规则编辑窗、
    // 白名单窗与四个功能开关（EBOT-1～EBOT-8）。
    flamingoadmin::test::RunAllEnterpriseBotTests();

    // 运行表格类共用控件单元测试 —— 分页条的页数、可否翻页与点击（PB），
    // 带操作链接的表格的命中测试与点击回调（AT）。
    flamingoadmin::test::RunAllSharedTableWidgetsTests();

    // 运行敏感词管理界面单元测试 —— 角色与企业选择、列表结果、行内操作、翻页（SB）。
    flamingoadmin::test::RunAllSensitiveWordsScreenTests();

    // 运行敏感词批量导入、导出与导出文件存盘规格的单元测试（ST、SI、SE、SIM）。
    flamingoadmin::test::RunAllSensitiveImportExportTests();

    // 运行敏感词界面「拦截记录」页签的单元测试（SH）。
    flamingoadmin::test::RunAllSensitiveHitsTests();

    // 运行文件共享管理单元测试 —— 功能设置与共享列表、成员、强制取消、审计六个接口的请求与应答，
    // 以及动作名、权限、字节数等显示文字（FA、FT）。
    flamingoadmin::test::RunAllFileShareAdminApiTests();

    // 运行文件共享管理界面单元测试 —— 功能开关的确认与回退、共享列表与强制取消、审计筛选与详情（FSP、FS、FAU）。
    flamingoadmin::test::RunAllFileShareScreensTests();

    // 运行企业详情「功能开关」子 tab 的用例：结构、两栏 / 一栏排版、滚动、加载与分发、企业详情屏的第四个子 tab。
    flamingoadmin::test::RunAllFeatureSwitchesPageTests();

    // 运行服务号绑定范围单元测试 —— 详情解析、按组织树翻译显示名、重拉列表时保留（SVD）。
    flamingoadmin::test::RunAllServiceBindingTests();

    // 运行编辑定时公告单元测试 —— 详情带出定时时间、弹窗回填「定时」、新建时不残留（ANE）。
    flamingoadmin::test::RunAllComposeEditScheduleTests();

    // 运行客户端版本校验单元测试 —— 版本号格式、最低版本不高于版本号、留空通过（CVV）。
    flamingoadmin::test::RunAllClientVersionValidateTests();

    // 运行员工表单单元测试 —— 性别编码与请求字段、负责人候选人收集与选择换算（GEN、LDR）。
    flamingoadmin::test::RunAllEmployeeFormTests();

    // 运行记住密码同步单元测试 —— 同一账号且勾了记住密码时换成新口令，其余不动（RPW）。
    flamingoadmin::test::RunAllRememberPasswordTests();

    // 运行外壳刷新单元测试 —— 问候语、顶栏账户区与滚动提示、侧栏按角色显隐（GRT、TBR、TCK、SHL）。
    flamingoadmin::test::RunAllShellSessionTests();

    // 运行登录页文字单元测试 —— 加密说明、版本行、账号密码回填（LGN）。
    flamingoadmin::test::RunAllLoginTextTests();

    // 运行公告重新推送接口封装单元测试（ARP3）。
    flamingoadmin::test::RunAllAnnouncementRepushTests();

    // 运行企业列表「更多筛选」单元测试（ELF5）。
    flamingoadmin::test::RunAllEnterpriseFilterTests();

    // 运行表格列宽自适应单元测试（COL1）。
    flamingoadmin::test::RunAllColumnFitTests();

    // 运行时间显示格式单元测试（FMT1）。
    flamingoadmin::test::RunAllTimeTextTests();

    // 运行 AdminFrame 源码守卫用例 —— 接口失败提示必须使用错误样式（TG）。
    flamingoadmin::test::RunAllAdminFrameGuardTests();

    // 运行 FlamingoLink.dll 纯函数部分的单元测试（FLK1~FLK7、FLK9）。
    flamingoadmin::test::RunAllFlamingoLinkTests();

    // 运行 FlamingoLink.dll 的黑盒测试：加载同目录的 DLL 连本地假服务端（FLK8，约十几秒）。
    flamingoadmin::test::RunAllFlamingoLinkLiveTests();

    // 运行控制台接入 FlamingoLink.dll 的单元测试（FLK10~FLK12）。
    flamingoadmin::test::RunAllFlamingoLinkHostTests();

    // 汇总并打印通过 / 失败数。
    const flamingoadmin::test::TestStats& stats = flamingoadmin::test::Stats();
    CString summary;
    summary.Format(_T("\n==== 测试结束：通过 %d，失败 %d ====\n"),
                   stats.m_passed, stats.m_failed);
    flamingoadmin::test::PrintLine(summary);

    // 有失败返回 1，全部通过返回 0。
    return stats.m_failed == 0 ? 0 : 1;
}
