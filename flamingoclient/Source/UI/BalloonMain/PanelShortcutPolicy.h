/**
 *  PanelShortcutPolicy.h
 *
 *  主面板快捷键的判定：不碰窗口、不读系统状态的纯函数。系统设置「快捷键」页列出的「快速搜索
 *  Ctrl + K」此前没有实现，搜索框里的 Esc 也只会收起下拉、不会清空文字。主面板收到 WM_KEYDOWN 时，把按键与
 *  搜索框的状态交给 DecidePanelShortcut，再按返回的动作执行。
 *
 *  判定规则（2026-10-10 用户确认）：
 *    · Ctrl+K（不带 Shift / Alt）：把光标移到搜索框；
 *    · Esc（不带 Ctrl / Alt）：搜索下拉开着时先收起下拉（文字保留，可以接着改词）；下拉已收起、搜索框里还有文字时
 *      清空文字；两者都没有时不处理，交回原来的流程 —— Esc 不隐藏主面板；
 *    · 其余按键一律不处理。
 *
 *  本模块被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程共用，在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。单元测试见 Tests/PanelShortcutPolicyTests.cpp（标签 [panelkey]）。
 *
 *  典型用法：
 *      balloonmain::PanelShortcutInput in;
 *      in.m_vk = (UINT)wParam;
 *      in.m_ctrl = ...;
 *      switch (balloonmain::DecidePanelShortcut(in)) { ... }
 *
 *  balloonwj@qq.com   2026-10-10
 */

#pragma once

#include <windows.h>

namespace balloonmain {

// 主面板对一次按键要做的事
enum PanelShortcutAction
{
    kPanelKeyNone           = 0,   // 不处理，交回原来的流程（基类把按键送给获得焦点的控件）
    kPanelKeyFocusSearch    = 1,   // Ctrl+K：把光标移到搜索框
    kPanelKeyCloseDropdown  = 2,   // Esc，搜索下拉开着：收起下拉，搜索框文字保留
    kPanelKeyClearSearch    = 3,   // Esc，下拉已收起、搜索框里有文字：清空搜索框
};

// 判定一次按键所需的输入：按键本身与当时搜索框的状态
struct PanelShortcutInput
{
    UINT m_vk;               // 虚拟键码（WM_KEYDOWN 的 wParam）
    bool m_ctrl;             // 按下时 Ctrl 是否按着
    bool m_shift;            // 按下时 Shift 是否按着
    bool m_alt;              // 按下时 Alt 是否按着
    bool m_dropdownShown;    // 搜索结果下拉（含最近搜索）此刻是否在显示
    bool m_searchHasText;    // 搜索框去掉首尾空白后是否还有文字

    PanelShortcutInput()
        : m_vk(0)
        , m_ctrl(false)
        , m_shift(false)
        , m_alt(false)
        , m_dropdownShown(false)
        , m_searchHasText(false)
    {
    }
};

/**
 *  按上面的规则判定主面板对这次按键要做的事。
 *    in：按键与搜索框的状态。
 *  @return 要执行的动作；kPanelKeyNone 表示不处理
 */
PanelShortcutAction DecidePanelShortcut(const PanelShortcutInput& in);

} // namespace balloonmain
