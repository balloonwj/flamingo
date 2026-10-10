/**
 *  消息记录窗的键盘快捷键判定，HistoryWindowKeys.h
 *
 *  把"按了哪个键、焦点在不在搜索框、搜索框里有没有字"翻译成消息记录窗要执行的动作。纯逻辑，
 *  不碰窗口：CBalloonHistoryWindow::OnKeyDownMsg 先调本函数，再按结果执行；返回"不处理"时
 *  把按键交还给获得焦点的控件。单元测试见 flamingoclient/Source/Tests/HistoryWindowKeysTests.cpp
 *  （Suite HK，标签 [historykeys]）。
 *
 *  典型用法：
 *      const int action = DecideHistoryKey(wParam, ctrlDown, altDown, searchFocused, searchHasText);
 *      if (action == kHistoryKeyNextPage) { ... }
 *
 *  Ctrl+C 复制列表选区不在本函数里：它要看"最近一次左键是否按在列表里、有没有选区"，
 *  仍由窗口自己判断（见 CBalloonHistoryWindow::OnKeyDownMsg）。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <windows.h>   // UINT 与 VK_* 键码

namespace balloonhistory {

/**
 *  按键对应的动作。
 */
enum HistoryKeyAction
{
    kHistoryKeyNone        = 0,   // 不处理，按键交还给获得焦点的控件
    kHistoryKeyClose       = 1,   // Esc：关闭窗口（搜索框没有焦点，或有焦点但框里没有字）
    kHistoryKeyClearSearch = 2,   // Esc：焦点在搜索框且框里有字时，清空搜索
    kHistoryKeyPrevPage    = 3,   // PageUp：上一页
    kHistoryKeyNextPage    = 4,   // PageDown：下一页
    kHistoryKeyFirstPage   = 5,   // Ctrl+Home：第一页（焦点在搜索框时留给输入框移动光标）
    kHistoryKeyLastPage    = 6,   // Ctrl+End：最后一页（同上）
    kHistoryKeyFocusSearch = 7,   // Ctrl+F：焦点移到搜索框并选中框里全部文字
    kHistoryKeySearchNow   = 8,   // 焦点在搜索框时按回车：立即搜索，不等输入停顿
    kHistoryKeyNextHit     = 9,   // F3：时间轴模式下跳到下一处命中（不在时间轴模式时窗口不处理）
    kHistoryKeyPrevHit     = 10   // Shift+F3：时间轴模式下跳到上一处命中
};

/**
 *  判断一次按键要执行什么动作。
 *    vk：           按下的虚拟键码（WM_KEYDOWN 的 wParam）。
 *    ctrlDown：     Ctrl 是否按住。
 *    altDown：      Alt 是否按住；按住时一律不处理，留给系统菜单等。
 *    searchFocused：焦点是否在搜索框上。
 *    searchHasText：搜索框里是否有文字。
 *  @return 动作，取值见 HistoryKeyAction；没有对应动作时返回 kHistoryKeyNone。
 */
int DecideHistoryKey(UINT vk, bool ctrlDown, bool altDown, bool searchFocused, bool searchHasText);

/**
 *  同上，另外认 Shift：F3 与 Shift+F3 分别是下一处、上一处命中（docs/聊天记录管理器方案.md §11.7）。
 *  其余按键的结果与上一个函数相同（Shift 不影响它们）。
 *    shiftDown：Shift 是否按住。
 *  @return 动作，取值见 HistoryKeyAction。
 */
int DecideHistoryKey(UINT vk, bool ctrlDown, bool altDown, bool shiftDown, bool searchFocused, bool searchHasText);

} // namespace balloonhistory
