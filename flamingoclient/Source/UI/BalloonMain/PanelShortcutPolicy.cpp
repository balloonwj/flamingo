/**
 *  PanelShortcutPolicy.cpp —— 主面板快捷键的判定，说明见 PanelShortcutPolicy.h。
 *  本文件是纯逻辑，不 include stdafx.h（工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *  balloonwj@qq.com   2026-10-10
 */
#include "PanelShortcutPolicy.h"

namespace balloonmain {

namespace {

// 「快速搜索」快捷键的字母键 K 的虚拟键码（字母键的虚拟键码等于大写字母的 ASCII 码）
const UINT kVkSearchLetter = 'K';

} // anonymous namespace

PanelShortcutAction DecidePanelShortcut(const PanelShortcutInput& in)
{
    if (in.m_vk == kVkSearchLetter && in.m_ctrl && !in.m_shift && !in.m_alt)
    {
        return kPanelKeyFocusSearch;
    }
    if (in.m_vk == VK_ESCAPE && !in.m_ctrl && !in.m_alt)
    {
        //先收起下拉、再清空文字：下拉收起后文字保留，用户可以接着改词；再按一次 Esc 才清空
        if (in.m_dropdownShown)
        {
            return kPanelKeyCloseDropdown;
        }
        if (in.m_searchHasText)
        {
            return kPanelKeyClearSearch;
        }
    }
    return kPanelKeyNone;
}

} // namespace balloonmain
