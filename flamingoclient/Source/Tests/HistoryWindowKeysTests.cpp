/**
 *  消息记录窗键盘快捷键判定的单元测试（Suite HK，标签 [historykeys]），HistoryWindowKeysTests.cpp
 *
 *  覆盖（用例已与用户确认）：
 *    HK1  Esc：焦点在搜索框且框里有字时清空搜索，其他情况关闭窗口。
 *    HK2  PageUp / PageDown：上一页 / 下一页，焦点在搜索框时也生效。
 *    HK3  Ctrl+Home / Ctrl+End：第一页 / 最后一页；焦点在搜索框时不处理，留给输入框移动光标。
 *    HK4  Ctrl+F：焦点移到搜索框。
 *    HK5  焦点在搜索框时按回车：立即搜索；焦点不在搜索框时不处理。
 *    HK6  按住 Alt 的组合键，以及没有列出的键：不处理。
 *
 *  框架 Catch2 v3。纯逻辑，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/HistoryWindowKeys.h"

using namespace balloonhistory;

TEST_CASE("HK1 Esc：搜索框有焦点且有字时清空搜索，其他情况关闭窗口", "[historykeys]")
{
    CHECK(DecideHistoryKey(VK_ESCAPE, false, false, true, true) == kHistoryKeyClearSearch);
    CHECK(DecideHistoryKey(VK_ESCAPE, false, false, true, false) == kHistoryKeyClose);
    CHECK(DecideHistoryKey(VK_ESCAPE, false, false, false, true) == kHistoryKeyClose);
    CHECK(DecideHistoryKey(VK_ESCAPE, false, false, false, false) == kHistoryKeyClose);
}

TEST_CASE("HK2 PageUp / PageDown 翻上一页 / 下一页，搜索框有焦点时也生效", "[historykeys]")
{
    CHECK(DecideHistoryKey(VK_PRIOR, false, false, false, false) == kHistoryKeyPrevPage);
    CHECK(DecideHistoryKey(VK_NEXT, false, false, false, false) == kHistoryKeyNextPage);
    CHECK(DecideHistoryKey(VK_PRIOR, false, false, true, true) == kHistoryKeyPrevPage);
    CHECK(DecideHistoryKey(VK_NEXT, false, false, true, true) == kHistoryKeyNextPage);
}

TEST_CASE("HK3 Ctrl+Home / Ctrl+End 跳到第一页 / 最后一页，搜索框有焦点时不处理", "[historykeys]")
{
    CHECK(DecideHistoryKey(VK_HOME, true, false, false, false) == kHistoryKeyFirstPage);
    CHECK(DecideHistoryKey(VK_END, true, false, false, false) == kHistoryKeyLastPage);
    CHECK(DecideHistoryKey(VK_HOME, true, false, true, true) == kHistoryKeyNone);
    CHECK(DecideHistoryKey(VK_END, true, false, true, false) == kHistoryKeyNone);
    // 不带 Ctrl 的 Home / End 不翻页
    CHECK(DecideHistoryKey(VK_HOME, false, false, false, false) == kHistoryKeyNone);
    CHECK(DecideHistoryKey(VK_END, false, false, false, false) == kHistoryKeyNone);
}

TEST_CASE("HK4 Ctrl+F 把焦点移到搜索框", "[historykeys]")
{
    CHECK(DecideHistoryKey('F', true, false, false, false) == kHistoryKeyFocusSearch);
    CHECK(DecideHistoryKey('F', true, false, true, true) == kHistoryKeyFocusSearch);
    // 不带 Ctrl 的 F 是普通输入
    CHECK(DecideHistoryKey('F', false, false, true, false) == kHistoryKeyNone);
}

TEST_CASE("HK5 搜索框有焦点时按回车立即搜索", "[historykeys]")
{
    CHECK(DecideHistoryKey(VK_RETURN, false, false, true, true) == kHistoryKeySearchNow);
    CHECK(DecideHistoryKey(VK_RETURN, false, false, true, false) == kHistoryKeySearchNow);
    CHECK(DecideHistoryKey(VK_RETURN, false, false, false, false) == kHistoryKeyNone);
}

TEST_CASE("HK6 按住 Alt 的组合键与没有列出的键都不处理", "[historykeys]")
{
    CHECK(DecideHistoryKey(VK_ESCAPE, false, true, false, false) == kHistoryKeyNone);
    CHECK(DecideHistoryKey(VK_NEXT, false, true, false, false) == kHistoryKeyNone);
    CHECK(DecideHistoryKey('F', true, true, false, false) == kHistoryKeyNone);
    CHECK(DecideHistoryKey('A', false, false, false, false) == kHistoryKeyNone);
    CHECK(DecideHistoryKey(VK_UP, false, false, false, false) == kHistoryKeyNone);
    // Ctrl+C 由窗口自己判断（要看列表里有没有选区），本函数不处理
    CHECK(DecideHistoryKey('C', true, false, false, false) == kHistoryKeyNone);
}
