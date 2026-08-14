/**
 *  DuiButtonFocusRectTests.cpp
 *
 *  DuiButton::ShouldDrawFocusRect 的测试：验证"Checkbox / Radio 默认不画 Win32
 *  点状焦点框，PushButton / Icon 在获焦时仍画"这一行为契约（2026-06-04 应业务
 *  反馈把复选框点击后那圈虚线框去掉）。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯逻辑、不触 UI。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

using balloonwjui::DuiButton;

TEST_CASE("DBF1 未获焦点任何风格都不画焦点框", "[focusrect]")
{
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StylePushButton, false) == false);
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StyleCheckbox,   false) == false);
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StyleRadio,      false) == false);
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StyleIcon,       false) == false);
}

TEST_CASE("DBF2 Checkbox / Radio 获焦也不画焦点框", "[focusrect]")
{
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StyleCheckbox, true) == false);
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StyleRadio,    true) == false);
}

TEST_CASE("DBF3 PushButton / Icon 获焦时仍画焦点框", "[focusrect]")
{
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StylePushButton, true) == true);
    CHECK(DuiButton::ShouldDrawFocusRect(DuiButton::StyleIcon,       true) == true);
}
