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

    // 汇总并打印通过 / 失败数。
    const flamingoadmin::test::TestStats& stats = flamingoadmin::test::Stats();
    CString summary;
    summary.Format(_T("\n==== 测试结束：通过 %d，失败 %d ====\n"),
                   stats.m_passed, stats.m_failed);
    flamingoadmin::test::PrintLine(summary);

    // 有失败返回 1，全部通过返回 0。
    return stats.m_failed == 0 ? 0 : 1;
}
