/**
 *  ClosePromptDecisionTests.cpp
 *
 *  主面板"关闭确认"对话框决策 DecideOnConfirm / DefaultSelection 的测试：
 *  覆盖"最小化 / 退出" × "记住 / 不记住" 四种组合的动作与两个偏好位，以及由当前
 *  偏好推默认选中项。
 *
 *  另覆盖 ClosePromptHostGuard —— 对话框模态显示期间主面板"禁用 — 恢复"的判定，
 *  以及一条 Win32 语义：给 WS_OVERLAPPEDWINDOW 顶层窗口设 owner 不改变 ::GetParent 的结果
 *  （CPD15）。2026-10-14 之前 balloonui 的 DuiHost::SendNotify 按 ::GetParent 决定通知目标，
 *  该模态方案靠这条语义让通知回到对话框自身；此后只有 WS_CHILD 宿主的通知发给父窗口
 *  （开发约定文档 UI 约定第 11 条），通知去向已不取决于这条语义。
 *
 *  用 Catch2 v3（amalgamated）作框架，除最后一条外均为纯逻辑、不触 UI。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/ClosePromptDecision.h"

using namespace balloonmain;

TEST_CASE("CPD1 最小化 + 记住 → 动作最小化、下次不提示、记住最小化", "[closeprompt]")
{
    CloseDecision d = DecideOnConfirm(kCloseActionMinimize, true);
    CHECK(d.action == kCloseActionMinimize);
    CHECK(d.enablePrompt == false);
    CHECK(d.exitWhenClose == false);
}

TEST_CASE("CPD2 最小化 + 不记住 → 动作最小化、下次仍提示", "[closeprompt]")
{
    CloseDecision d = DecideOnConfirm(kCloseActionMinimize, false);
    CHECK(d.action == kCloseActionMinimize);
    CHECK(d.enablePrompt == true);
    CHECK(d.exitWhenClose == false);
}

TEST_CASE("CPD3 退出 + 记住 → 动作退出、下次不提示、记住退出", "[closeprompt]")
{
    CloseDecision d = DecideOnConfirm(kCloseActionExit, true);
    CHECK(d.action == kCloseActionExit);
    CHECK(d.enablePrompt == false);
    CHECK(d.exitWhenClose == true);
}

TEST_CASE("CPD4 退出 + 不记住 → 动作退出、下次仍提示", "[closeprompt]")
{
    CloseDecision d = DecideOnConfirm(kCloseActionExit, false);
    CHECK(d.action == kCloseActionExit);
    CHECK(d.enablePrompt == true);
    CHECK(d.exitWhenClose == true);
}

TEST_CASE("CPD5 越界选中项按最小化处理", "[closeprompt]")
{
    // 传入非法值（既非最小化也非退出）应归一化为最小化，避免误退出。
    CloseDecision d = DecideOnConfirm(999, true);
    CHECK(d.action == kCloseActionMinimize);
    CHECK(d.exitWhenClose == false);
}

TEST_CASE("CPD6 默认选中项由偏好推出", "[closeprompt]")
{
    CHECK(DefaultSelection(true) == kCloseActionExit);
    CHECK(DefaultSelection(false) == kCloseActionMinimize);
}

// =======================================================================
// ClosePromptHostGuard —— 宿主窗（主面板）"禁用 — 恢复"判定
// =======================================================================

TEST_CASE("CPD7 打开时禁用宿主窗，关闭时恢复", "[closeprompt]")
{
    ClosePromptHostGuard guard;
    CHECK(guard.IsHostDisabled() == false);

    // 宿主窗有效且当前可操作 —— 这是最常见的一次打开。
    CHECK(guard.OnOpen(true, true) == true);
    CHECK(guard.IsHostDisabled() == true);

    CHECK(guard.OnClose(true) == true);
    CHECK(guard.IsHostDisabled() == false);
}

TEST_CASE("CPD8 恢复幂等：多条结束路径只恢复一次", "[closeprompt]")
{
    // 对话框可能先在关闭收尾里恢复一次，随后 WM_DESTROY 兜底再走一次；
    // 第二次起必须返回 false，否则会把别处的禁用状态误恢复。
    ClosePromptHostGuard guard;
    REQUIRE(guard.OnOpen(true, true) == true);

    CHECK(guard.OnClose(true) == true);
    CHECK(guard.OnClose(true) == false);
    CHECK(guard.OnClose(true) == false);
    CHECK(guard.IsHostDisabled() == false);
}

TEST_CASE("CPD9 没打开过就走关闭收尾，不恢复任何东西", "[closeprompt]")
{
    ClosePromptHostGuard guard;
    CHECK(guard.OnClose(true) == false);
    CHECK(guard.IsHostDisabled() == false);
}

TEST_CASE("CPD10 宿主窗句柄无效时不禁用，关闭时也不恢复", "[closeprompt]")
{
    ClosePromptHostGuard guard;
    CHECK(guard.OnOpen(false, true) == false);
    CHECK(guard.IsHostDisabled() == false);
    CHECK(guard.OnClose(true) == false);
}

TEST_CASE("CPD11 宿主窗已被别的窗口禁用时不接管", "[closeprompt]")
{
    // 若这里禁用，关闭时的恢复会把别人的禁用状态一并抹掉。
    ClosePromptHostGuard guard;
    CHECK(guard.OnOpen(true, false) == false);
    CHECK(guard.IsHostDisabled() == false);
    CHECK(guard.OnClose(true) == false);
}

TEST_CASE("CPD12 对话框已显示时重复打开，不重复禁用且只恢复一次", "[closeprompt]")
{
    ClosePromptHostGuard guard;
    REQUIRE(guard.OnOpen(true, true) == true);

    // 第二次打开时宿主窗已被本类禁用，故 hostIsEnabled 传 false 更贴近实机。
    CHECK(guard.OnOpen(true, false) == false);
    CHECK(guard.IsHostDisabled() == true);

    CHECK(guard.OnClose(true) == true);
    CHECK(guard.OnClose(true) == false);
}

TEST_CASE("CPD13 宿主窗在对话框显示期间被销毁", "[closeprompt]")
{
    // 主面板被销毁会连带销毁本对话框（owner 关系），此时不必也不能再调
    // ::EnableWindow，但待恢复标记必须清掉，免得下一轮打开被误判成"已禁用"。
    ClosePromptHostGuard guard;
    REQUIRE(guard.OnOpen(true, true) == true);

    CHECK(guard.OnClose(false) == false);
    CHECK(guard.IsHostDisabled() == false);
}

TEST_CASE("CPD14 一轮结束后可再次打开", "[closeprompt]")
{
    // 对话框对象由主面板长期持有，窗口反复创建销毁，状态机要能跨次复用。
    ClosePromptHostGuard guard;
    REQUIRE(guard.OnOpen(true, true) == true);
    REQUIRE(guard.OnClose(true) == true);

    CHECK(guard.OnOpen(true, true) == true);
    CHECK(guard.IsHostDisabled() == true);
    CHECK(guard.OnClose(true) == true);
}

// =======================================================================
// 模态方案所依赖的 Win32 语义
// =======================================================================

TEST_CASE("CPD15 给顶层窗口设 owner 不改变 GetParent 的结果", "[closeprompt]")
{
    // 本用例验证一条 Win32 语义：WS_OVERLAPPEDWINDOW 的顶层窗口即便带 owner，
    // ::GetParent 仍为 NULL（owner 要用 ::GetWindow(GW_OWNER) 取）。
    // 关闭确认对话框改用"owner = 主面板"时，balloonui 的 DuiHost::SendNotify 按
    // ::GetParent(m_hWnd) 找通知投递目标，对话框里卡片 / 复选框 / 按钮的通知靠这条
    // 语义才回到对话框自己。2026-10-14 起 SendNotify 只把 WS_CHILD 宿主的通知发给
    // 父窗口，其余一律发回宿主自身（开发约定文档 UI 约定第 11 条），通知去向已不依赖
    // 这条语义。
    HINSTANCE hInst = ::GetModuleHandle(NULL);

    // 用系统 STATIC 类建窗，省去注册窗口类；本用例只关心窗口关系，与类无关。
    // 两个窗口都不显示（无 WS_VISIBLE），不会在屏幕上闪出来。
    HWND hOwner = ::CreateWindowEx(0, _T("STATIC"), _T("CPD15Owner"),
                                   WS_OVERLAPPEDWINDOW,
                                   0, 0, 100, 100,
                                   NULL, NULL, hInst, NULL);
    REQUIRE(hOwner != NULL);

    HWND hOwned = ::CreateWindowEx(0, _T("STATIC"), _T("CPD15Owned"),
                                   WS_OVERLAPPEDWINDOW,
                                   0, 0, 100, 100,
                                   hOwner, NULL, hInst, NULL);
    REQUIRE(hOwned != NULL);

    CHECK(::GetWindow(hOwned, GW_OWNER) == hOwner);   // owner 关系确实建立了
    CHECK(::GetParent(hOwned) == NULL);               // 但 ::GetParent 仍取不到父窗口

    ::DestroyWindow(hOwned);
    ::DestroyWindow(hOwner);
}
