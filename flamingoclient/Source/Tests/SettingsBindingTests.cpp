/**
 *  SettingsBindingTests.cpp
 *
 *  系统设置下拉/开关 ↔ CUserConfig 纯映射逻辑的测试：验证容易写反的转换
 *  （关闭行为、发送快捷键、提示音↔静音取反）方向正确、可来回 round-trip，
 *  以及选项文本表的项数 / 顺序 / 文本、索引夹取边界。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言。全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cwchar>          // wcscmp —— 比对下拉项中文文本

#include "../UI/BalloonSettings/SettingsBinding.h"

using namespace balloonsettings;

// =====================================================================
// BIND-CLOSE. 关闭主面板时（最小化到托盘 / 退出）
// =====================================================================

TEST_CASE("BIND-CLOSE1 选项表为「最小化到托盘 / 退出程序」两项", "[settings][binding]")
{
    REQUIRE(CloseActionOptionCount() == 2);
    const wchar_t* const* opts = CloseActionOptions();
    CHECK(::wcscmp(opts[kCloseActionMinimize], L"最小化到托盘") == 0);
    CHECK(::wcscmp(opts[kCloseActionExit], L"退出程序") == 0);
}

TEST_CASE("BIND-CLOSE2 配置布尔↔下拉索引方向正确且可 round-trip", "[settings][binding]")
{
    // FALSE(最小化) ↔ index 0；TRUE(退出) ↔ index 1
    CHECK(CloseActionIndexFromExitWhenClose(false) == kCloseActionMinimize);
    CHECK(CloseActionIndexFromExitWhenClose(true) == kCloseActionExit);
    CHECK(ExitWhenCloseFromCloseActionIndex(kCloseActionMinimize) == false);
    CHECK(ExitWhenCloseFromCloseActionIndex(kCloseActionExit) == true);

    // round-trip
    CHECK(ExitWhenCloseFromCloseActionIndex(CloseActionIndexFromExitWhenClose(true)) == true);
    CHECK(ExitWhenCloseFromCloseActionIndex(CloseActionIndexFromExitWhenClose(false)) == false);
}

// =====================================================================
// BIND-SENDKEY. 发送消息快捷键（Enter / Ctrl+Enter）
// =====================================================================

TEST_CASE("BIND-SENDKEY1 选项表为「Enter / Ctrl + Enter」两项", "[settings][binding]")
{
    REQUIRE(SendKeyOptionCount() == 2);
    const wchar_t* const* opts = SendKeyOptions();
    CHECK(::wcscmp(opts[kSendKeyEnter], L"Enter") == 0);
    CHECK(::wcscmp(opts[kSendKeyCtrlEnter], L"Ctrl + Enter") == 0);
}

TEST_CASE("BIND-SENDKEY2 配置布尔↔下拉索引方向正确且可 round-trip", "[settings][binding]")
{
    // TRUE(Enter 发送) ↔ index 0；FALSE(Ctrl+Enter) ↔ index 1
    CHECK(SendKeyIndexFromPressEnter(true) == kSendKeyEnter);
    CHECK(SendKeyIndexFromPressEnter(false) == kSendKeyCtrlEnter);
    CHECK(PressEnterFromSendKeyIndex(kSendKeyEnter) == true);
    CHECK(PressEnterFromSendKeyIndex(kSendKeyCtrlEnter) == false);

    CHECK(PressEnterFromSendKeyIndex(SendKeyIndexFromPressEnter(true)) == true);
    CHECK(PressEnterFromSendKeyIndex(SendKeyIndexFromPressEnter(false)) == false);
}

// =====================================================================
// BIND-LANG. 语言（本期仅一项占位）
// =====================================================================

TEST_CASE("BIND-LANG1 至少一项，含「简体中文」", "[settings][binding]")
{
    REQUIRE(LanguageOptionCount() >= 1);
    CHECK(::wcscmp(LanguageOptions()[0], L"简体中文") == 0);
}

TEST_CASE("BIND-LANG2 越界索引夹到 0", "[settings][binding]")
{
    CHECK(ClampLanguageIndex(0) == 0);
    CHECK(ClampLanguageIndex(-1) == 0);
    CHECK(ClampLanguageIndex(999) == 0);
    CHECK(ClampLanguageIndex(LanguageOptionCount()) == 0);   // 等于 count 也算越界
}

// =====================================================================
// BIND-FONT. 字体大小（小 / 中 / 大）
// =====================================================================

TEST_CASE("BIND-FONT1 选项表为「小 / 中 / 大」三项", "[settings][binding]")
{
    REQUIRE(FontScaleOptionCount() == 3);
    const wchar_t* const* opts = FontScaleOptions();
    CHECK(::wcscmp(opts[kFontScaleSmall], L"小") == 0);
    CHECK(::wcscmp(opts[kFontScaleMedium], L"中") == 0);
    CHECK(::wcscmp(opts[kFontScaleLarge], L"大") == 0);
}

TEST_CASE("BIND-FONT2 合法索引原样、越界回落到默认「中」", "[settings][binding]")
{
    CHECK(ClampFontScaleIndex(kFontScaleSmall) == kFontScaleSmall);
    CHECK(ClampFontScaleIndex(kFontScaleMedium) == kFontScaleMedium);
    CHECK(ClampFontScaleIndex(kFontScaleLarge) == kFontScaleLarge);
    CHECK(ClampFontScaleIndex(-1) == kFontScaleMedium);
    CHECK(ClampFontScaleIndex(3) == kFontScaleMedium);
    CHECK(ClampFontScaleIndex(999) == kFontScaleMedium);
}

// =====================================================================
// BIND-SOUND. 提示音 ↔ 静音（取反）
// =====================================================================

TEST_CASE("BIND-SOUND1 提示音开 = 未静音，可 round-trip", "[settings][binding]")
{
    CHECK(SoundOnFromMute(true) == false);    // 已静音 → 提示音关
    CHECK(SoundOnFromMute(false) == true);    // 未静音 → 提示音开
    CHECK(MuteFromSoundOn(true) == false);    // 提示音开 → 不静音
    CHECK(MuteFromSoundOn(false) == true);    // 提示音关 → 静音

    CHECK(SoundOnFromMute(MuteFromSoundOn(true)) == true);
    CHECK(SoundOnFromMute(MuteFromSoundOn(false)) == false);
}
