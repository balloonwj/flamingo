/**
 *  ScreenshotHotkey.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "ScreenshotHotkey.h"

#include <string>
#include "../../Common/I18n/I18n.h"   // TR / TRF：悬停提示与菜单文字的多语言查找

namespace balloonscreenshot {

namespace {

// 功能键 F1~F12 的个数
const UINT kFunctionKeyCount = 12;

// 键名（键帽上印的文字，不翻译）
const wchar_t* const kKeyNameCtrl  = L"Ctrl";
const wchar_t* const kKeyNameAlt   = L"Alt";
const wchar_t* const kKeyNameShift = L"Shift";

// 两种格式下键与键之间的分隔符
const wchar_t* const kSeparatorKeyCap  = L" + ";
const wchar_t* const kSeparatorCompact = L"+";

// 把主键的虚拟键码转成键名：字母与数字取字符本身，功能键为 "F1"~"F12"；其它键返回空串
CString KeyName(UINT vk)
{
    CString s;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    {
        s.Format(L"%c", (wchar_t)vk);
    }
    else if (vk >= VK_F1 && vk < VK_F1 + kFunctionKeyCount)
    {
        s.Format(L"F%u", vk - VK_F1 + 1);
    }
    return s;
}

} // anonymous namespace

DWORD MakeHotkey(UINT mods, UINT vk)
{
    if (vk == 0)
    {
        return 0;
    }
    return ((DWORD)(mods & kHotkeyModMask) << kHotkeyModShift) | ((DWORD)vk & kHotkeyVkMask);
}

UINT HotkeyModifiers(DWORD hotkey)
{
    return (UINT)(hotkey >> kHotkeyModShift) & kHotkeyModMask;
}

UINT HotkeyVirtualKey(DWORD hotkey)
{
    return (UINT)(hotkey & kHotkeyVkMask);
}

bool IsSupportedHotkeyVk(UINT vk)
{
    if (vk >= 'A' && vk <= 'Z')
    {
        return true;
    }
    if (vk >= '0' && vk <= '9')
    {
        return true;
    }
    return vk >= VK_F1 && vk < VK_F1 + kFunctionKeyCount;
}

bool IsModifierVk(UINT vk)
{
    switch (vk)
    {
    //Ctrl 及其左右两侧的键
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    //Alt（系统称 Menu 键）及其左右两侧的键
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    //Shift 及其左右两侧的键
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    //左右两个 Win 键
    case VK_LWIN:
    case VK_RWIN:
        return true;
    //其余都是普通键
    default:
        return false;
    }
}

DWORD ComposeHotkey(UINT vk, bool ctrl, bool alt, bool shift)
{
    if (vk == 0 || IsModifierVk(vk))
    {
        return 0;
    }
    UINT mods = 0;
    if (ctrl)
    {
        mods |= MOD_CONTROL;
    }
    if (alt)
    {
        mods |= MOD_ALT;
    }
    if (shift)
    {
        mods |= MOD_SHIFT;
    }
    return MakeHotkey(mods, vk);
}

CString FormatHotkey(DWORD hotkey, HotkeyTextStyle style)
{
    CString out;
    if (hotkey == 0)
    {
        return out;
    }
    const wchar_t* sep = (style == kHotkeyTextKeyCap) ? kSeparatorKeyCap : kSeparatorCompact;
    UINT mods = HotkeyModifiers(hotkey);
    // 修饰键按 Ctrl、Alt、Shift 的固定顺序排列，与 Windows 菜单的写法一致
    if ((mods & MOD_CONTROL) != 0)
    {
        out += kKeyNameCtrl;
        out += sep;
    }
    if ((mods & MOD_ALT) != 0)
    {
        out += kKeyNameAlt;
        out += sep;
    }
    if ((mods & MOD_SHIFT) != 0)
    {
        out += kKeyNameShift;
        out += sep;
    }
    CString key = KeyName(HotkeyVirtualKey(hotkey));
    if (key.IsEmpty())
    {
        // 不认识的主键按十六进制键码显示，便于排查配置文件被手工改坏的情况
        key.Format(L"0x%02X", HotkeyVirtualKey(hotkey));
    }
    out += key;
    return out;
}

HotkeyCheckResult CheckHotkey(DWORD hotkey, const ReservedHotkey* reserved, int reservedCount,
                              int* conflictIndex)
{
    if (conflictIndex != NULL)
    {
        *conflictIndex = -1;
    }
    if (hotkey == 0)
    {
        return kHotkeyDisabled;
    }
    UINT mods = HotkeyModifiers(hotkey);
    if ((mods & (MOD_CONTROL | MOD_ALT)) == 0)
    {
        return kHotkeyNeedModifier;
    }
    if (!IsSupportedHotkeyVk(HotkeyVirtualKey(hotkey)))
    {
        return kHotkeyUnsupportedKey;
    }
    // 只比较修饰键与主键，忽略编码里多余的位
    DWORD normalized = MakeHotkey(mods, HotkeyVirtualKey(hotkey));
    for (int i = 0; reserved != NULL && i < reservedCount; ++i)
    {
        if (reserved[i].m_hotkey == normalized)
        {
            if (conflictIndex != NULL)
            {
                *conflictIndex = i;
            }
            return kHotkeyReserved;
        }
    }
    return kHotkeyOk;
}

CString ScreenshotCommandText(DWORD hotkey)
{
    if (hotkey == 0)
    {
        return CString(TR(L"截图"));
    }
    CString keys = FormatHotkey(hotkey, kHotkeyTextCompact);
    std::wstring text = TRF(L"截图（{0}）", (const wchar_t*)keys);
    return CString(text.c_str());
}

CString TrayScreenshotLabel(DWORD hotkey, bool registered)
{
    return ScreenshotCommandText(registered ? hotkey : 0);
}

AppHotkeyPlan PlanAppHotkeys(bool loggedIn, bool recordEnabled, DWORD screenshotHotkey)
{
    AppHotkeyPlan plan;
    plan.m_showPanel = loggedIn;
    plan.m_record = loggedIn && recordEnabled;
    plan.m_screenshot = screenshotHotkey;
    return plan;
}

} // namespace balloonscreenshot
