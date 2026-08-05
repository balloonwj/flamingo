/**
 *  ClosePromptDecisionTests.cpp
 *
 *  主面板"关闭确认"对话框决策 DecideOnConfirm / DefaultSelection 的测试：
 *  覆盖"最小化 / 退出" × "记住 / 不记住" 四种组合的动作与两个偏好位，以及由当前
 *  偏好推默认选中项。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯逻辑、不触 UI。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/ClosePromptDecision.h"

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
