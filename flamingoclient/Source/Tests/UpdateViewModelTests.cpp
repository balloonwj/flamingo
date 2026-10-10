/**
 *  升级窗纯逻辑（Update/UpdateViewModel.h）单元测试（Suite UVM），
 *  UpdateViewModelTests.cpp
 *
 *  覆盖三组纯函数，范围与 docs/客户端自动更新方案.md 第 9 节里已确认的 12 条一致：
 *    · BuildUpdateViewState：有更新 / 强制升级 / 已是最新 / 检查失败，外加"服务端报告
 *      有更新但包信息不全"这条最容易漏的分支；
 *    · ComputeDownloadPercent：常规换算、总数为 0 不除零、已收超总数钳到 100、
 *      3GiB 级别大包不溢出；
 *    · ShouldBlockLogin：只有强制升级才拦，检查失败一律放行。
 *    · UVM13：有可用新版本时视图状态带上更新说明，其余形态为空。
 *
 *  界面本身不测（不创建窗口 / 不依赖消息循环），纯内存断言。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Feature/Update/UpdateViewModel.h"

using namespace update;

namespace {

//造一个"服务端正常应答、有可用新版本"的结果，各用例在此基础上改一两个字段。
//直接写 UpdateTaskResult 而不去跑真实网络：本套测的是判断逻辑，不是传输。
UpdateTaskResult MakeHasUpdate()
{
    UpdateTaskResult r;
    r.m_valid         = true;
    r.m_transportOk   = true;
    r.m_code          = 0;
    r.m_hasUpdate     = true;
    r.m_packageUsable = true;
    r.m_forced        = false;
    r.m_latestVersion = "3.3.0";
    r.m_releaseNote   = "新增群接龙；修复若干问题";
    r.m_size          = 38219776;
    return r;
}

} // namespace

// =====================================================================
// UVM. 升级窗该显示什么
// =====================================================================

TEST_CASE("UVM1 有更新且非强制：两个按钮都在，窗口可关，给下载入口", "[update]")
{
    const UpdateViewState st = BuildUpdateViewState(MakeHasUpdate(), L"3.2.1");

    CHECK(st.kind == update_view_has_update);
    CHECK(st.showLaterButton);
    CHECK(st.closable);
    CHECK(st.canDownload);
    CHECK(st.title == L"发现新版本 3.3.0");
    //版本行要把"从哪升到哪"说清楚，只写最新版本用户不知道自己现在是什么版本
    CHECK(st.versionLine == L"当前版本 3.2.1 → 最新版本 3.3.0");
}

TEST_CASE("UVM2 强制升级：隐藏「稍后」且窗口不可关", "[update]")
{
    UpdateTaskResult r = MakeHasUpdate();
    r.m_forced = true;

    const UpdateViewState st = BuildUpdateViewState(r, L"3.0.0");

    CHECK(st.kind == update_view_forced);
    //这两条是强制升级的关键：只要留下任一关闭途径，用户都会停留在登录窗反复重试，
    //因为服务端的 cmd_login 同样会以 error_code_version_too_old 拒绝该客户端
    CHECK_FALSE(st.showLaterButton);
    CHECK_FALSE(st.closable);
    CHECK(st.canDownload);
    CHECK(st.versionLine == L"当前版本 3.0.0 → 最新版本 3.3.0");
}

TEST_CASE("UVM3 无更新：文案为「当前已是最新版本 3.3.0」", "[update]")
{
    UpdateTaskResult r;
    r.m_valid         = true;
    r.m_transportOk   = true;
    r.m_code          = 0;
    r.m_hasUpdate     = false;
    r.m_latestVersion = "3.3.0";

    const UpdateViewState st = BuildUpdateViewState(r, L"3.3.0");

    CHECK(st.kind == update_view_latest);
    CHECK(st.title == L"当前已是最新版本 3.3.0");
    CHECK_FALSE(st.canDownload);
    CHECK(st.closable);

    //服务器未发布过任何版本时不下发 latestversion，文案不能带一个空版本号
    UpdateTaskResult none = r;
    none.m_latestVersion.clear();
    CHECK(BuildUpdateViewState(none, L"3.3.0").title == L"当前已是最新版本");
}

TEST_CASE("UVM4 检查失败：报失败文案且窗口可关", "[update]")
{
    //① 本次请求完全未完成（超时 / 未连接）
    UpdateTaskResult timeout;
    timeout.m_valid       = true;
    timeout.m_transportOk = false;
    timeout.m_errmsg      = "check-update timeout";

    UpdateViewState st = BuildUpdateViewState(timeout, L"3.2.1");
    CHECK(st.kind == update_view_failed);
    CHECK(st.closable);
    CHECK_FALSE(st.canDownload);
    CHECK(st.title == L"检查更新失败，请稍后重试");
    CHECK(st.versionLine.empty());

    //② 往返完成了，但服务端回了业务错误码（如请求非法）
    UpdateTaskResult bad;
    bad.m_valid       = true;
    bad.m_transportOk = true;
    bad.m_code        = 3;
    bad.m_errmsg      = "bad request";

    st = BuildUpdateViewState(bad, L"3.2.1");
    CHECK(st.kind == update_view_failed);
    CHECK_FALSE(st.canDownload);
}

TEST_CASE("UVM5 服务端报告有更新但包信息不全：按无更新处理，不提供下载入口", "[update]")
{
    // 这是最容易漏的一条：应答本身是正常的（code=0、hasupdate=true），
    // 但发布侧记录不完整，没给齐下载令牌 / MD5 / 大小。此时包下下来也校验不过，
    // 此时提供「立即更新」按钮，等于让用户执行一个必然失败的操作。
    UpdateTaskResult r = MakeHasUpdate();
    r.m_packageUsable = false;

    const UpdateViewState st = BuildUpdateViewState(r, L"3.2.1");

    CHECK(st.kind == update_view_latest);
    CHECK_FALSE(st.canDownload);
    CHECK(st.closable);

    //强制升级标志也不能使其恢复下载流程：升级包不可用时，强制升级同样无法进行
    UpdateTaskResult forcedBroken = r;
    forcedBroken.m_forced = true;
    const UpdateViewState st2 = BuildUpdateViewState(forcedBroken, L"3.2.1");
    CHECK_FALSE(st2.canDownload);
    CHECK(st2.closable);
}

// =====================================================================
// UVM. 下载进度百分比
// =====================================================================

TEST_CASE("UVM6 进度百分比常规换算", "[update]")
{
    CHECK(ComputeDownloadPercent(0, 100) == 0);
    CHECK(ComputeDownloadPercent(50, 100) == 50);
    CHECK(ComputeDownloadPercent(100, 100) == 100);
    //整除不尽时取整（向下），不四舍五入：进度条宁可略滞后也别提前显示 100%
    CHECK(ComputeDownloadPercent(1, 3) == 33);
}

TEST_CASE("UVM7 总字节为 0 时返回 0，不除零", "[update]")
{
    CHECK(ComputeDownloadPercent(0, 0) == 0);
    CHECK(ComputeDownloadPercent(1024, 0) == 0);
    //负数是不该出现的输入，但也不能崩或返回负百分比
    CHECK(ComputeDownloadPercent(1024, -1) == 0);
    CHECK(ComputeDownloadPercent(-1, 1024) == 0);
}

TEST_CASE("UVM8 已收超过总数时钳到 100", "[update]")
{
    CHECK(ComputeDownloadPercent(101, 100) == 100);
    CHECK(ComputeDownloadPercent(1000000, 100) == 100);
}

TEST_CASE("UVM9 3GiB 级别的大包不溢出", "[update]")
{
    // 防回归：中间量若用 32 位算，received * 100 会溢出成负数，进度条往回跳。
    const long long kThreeGiB = 3221225472LL;

    CHECK(ComputeDownloadPercent(kThreeGiB / 2, kThreeGiB) == 50);
    CHECK(ComputeDownloadPercent(kThreeGiB, kThreeGiB) == 100);
    //已收远超 int32 上界的中间值
    CHECK(ComputeDownloadPercent(kThreeGiB / 4, kThreeGiB) == 25);
}

// =====================================================================
// UVM. 要不要拦住登录
// =====================================================================

TEST_CASE("UVM10 强制升级拦住登录", "[update]")
{
    UpdateTaskResult r = MakeHasUpdate();
    r.m_forced = true;

    CHECK(ShouldBlockLogin(r));
}

TEST_CASE("UVM11 非强制放行", "[update]")
{
    CHECK_FALSE(ShouldBlockLogin(MakeHasUpdate()));

    //无更新自然也放行
    UpdateTaskResult latest;
    latest.m_valid       = true;
    latest.m_transportOk = true;
    latest.m_hasUpdate   = false;
    CHECK_FALSE(ShouldBlockLogin(latest));
}

TEST_CASE("UVM12 检查失败或超时一律放行", "[update]")
{
    // 检查更新链路自身故障时不应阻断登录，否则客户端自身的故障将直接影响用户使用。
    // 若确实存在版本过低的问题，服务端在 cmd_login 一步仍会再次拒绝，不会遗漏。
    UpdateTaskResult timeout;
    timeout.m_valid       = true;
    timeout.m_transportOk = false;
    CHECK_FALSE(ShouldBlockLogin(timeout));

    UpdateTaskResult bad;
    bad.m_valid       = true;
    bad.m_transportOk = true;
    bad.m_code        = 3;
    CHECK_FALSE(ShouldBlockLogin(bad));

    //即便服务端在错误应答中误带了 forced=true，也不能据此阻断登录
    UpdateTaskResult weird;
    weird.m_valid       = true;
    weird.m_transportOk = true;
    weird.m_code        = 3;
    weird.m_hasUpdate   = true;
    weird.m_forced      = true;
    CHECK_FALSE(ShouldBlockLogin(weird));
}

//关于页「检查更新」的结果由升级窗自己的结果处理函数呈现，那条路径只应用了视图状态、没有填更新说明，
//说明框一直是空的。2026-10-10 起更新说明并入视图状态，两条呈现路径都经 ApplyViewState 填写。
TEST_CASE("UVM13 有可用新版本时视图状态带上更新说明，其余形态为空", "[update]")
{
    //管理台保存的多行说明行间只有 CR，原样带出，由界面交给富文本控件显示
    UpdateTaskResult r = MakeHasUpdate();
    r.m_releaseNote = "1. 新增群接龙\r2. 修复若干问题\r3. 优化启动速度";
    CHECK(BuildUpdateViewState(r, L"3.2.1").releaseNoteUtf8 == r.m_releaseNote);

    //强制升级同样显示说明
    r.m_forced = true;
    CHECK(BuildUpdateViewState(r, L"3.2.1").releaseNoteUtf8 == r.m_releaseNote);

    //包信息不全按无更新处理：不显示说明
    UpdateTaskResult unusable = MakeHasUpdate();
    unusable.m_packageUsable = false;
    CHECK(BuildUpdateViewState(unusable, L"3.2.1").releaseNoteUtf8.empty());

    //已是最新、检查失败：不显示说明
    UpdateTaskResult latest = MakeHasUpdate();
    latest.m_hasUpdate = false;
    CHECK(BuildUpdateViewState(latest, L"3.3.0").releaseNoteUtf8.empty());
    UpdateTaskResult failed = MakeHasUpdate();
    failed.m_transportOk = false;
    CHECK(BuildUpdateViewState(failed, L"3.2.1").releaseNoteUtf8.empty());
}
