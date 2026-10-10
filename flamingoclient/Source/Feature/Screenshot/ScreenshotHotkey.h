/**
 *  ScreenshotHotkey.h
 *
 *  截图全局快捷键的纯逻辑部分：快捷键在配置里的编码、两种显示文字、组合规则校验、
 *  由一次按键事件合成快捷键，以及剪刀按钮悬停提示 / 截图菜单第一项的文字。不调用任何
 *  注册热键的系统接口，便于 FlamingoUnit 直接断言（用例 SK1~SK5）；注册与注销见
 *  ScreenshotEntry.h。方案见 docs/截图快捷键与隐藏当前窗口方案.md。
 *
 *  编码：高 16 位为 MOD_CONTROL / MOD_ALT / MOD_SHIFT 的组合，低 16 位为虚拟键码；0 表示停用。
 *
 *  典型用法：
 *      DWORD hk = cfg.GetScreenshotHotkey();
 *      CString cap = FormatHotkey(hk, kHotkeyTextKeyCap);       // "Alt + Z"
 *      CString tip = ScreenshotCommandText(hk);                  // "截图（Alt+Z）"
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>

namespace balloonscreenshot {

// 截图全局快捷键的标识，登录后注册在主面板窗口上（1001 为「显示 / 隐藏主面板」，1002 为录屏）
const int kScreenshotHotKeyId = 1003;

// 快捷键编码里修饰键所在的位移：高 16 位放 MOD_* 组合
const int kHotkeyModShift = 16;

// 快捷键编码里虚拟键码所占的位
const DWORD kHotkeyVkMask = 0xFFFF;

// 本模块认可的修饰键（不含 Win 键：Win 加字母多被系统占用）
const UINT kHotkeyModMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT;

// 截图快捷键的默认值：Alt+Z（2026-10-05 与用户确认）
const DWORD kDefaultScreenshotHotkey = ((DWORD)MOD_ALT << kHotkeyModShift) | (DWORD)'Z';

// 快捷键显示文字的格式
enum HotkeyTextStyle
{
    kHotkeyTextKeyCap = 0,   // 设置页键帽：各键之间用「 + 」分隔，如 "Ctrl + Alt + A"
    kHotkeyTextCompact,      // 悬停提示与菜单：不加空格，如 "Ctrl+Alt+A"
};

// 快捷键组合的校验结果
enum HotkeyCheckResult
{
    kHotkeyOk = 0,           // 可以使用
    kHotkeyDisabled,         // 编码为 0：停用快捷键（也是合法的设置）
    kHotkeyNeedModifier,     // 组合里没有 Ctrl 或 Alt（只有 Shift 或没有修饰键会妨碍正常打字）
    kHotkeyUnsupportedKey,   // 主键不在 A~Z、0~9、F1~F12 之内
    kHotkeyReserved,         // 与本程序其它全局快捷键重复（见 ReservedHotkey 表）
};

// 本程序已占用的一个全局快捷键（校验时用来查重）
struct ReservedHotkey
{
    DWORD          m_hotkey;   // 编码后的组合
    const wchar_t* m_name;     // 功能名称原文（TR_NOOP 标记），提示「与某某重复」时使用
};

/**
 *  把修饰键与虚拟键码编码成一个快捷键值。
 *  @param mods MOD_CONTROL / MOD_ALT / MOD_SHIFT 的组合，其余位被忽略
 *  @param vk   虚拟键码（低 16 位有效）
 *  @return 编码后的值；vk 为 0 时返回 0
 */
DWORD MakeHotkey(UINT mods, UINT vk);

// 取编码里的修饰键组合（MOD_*，已按 kHotkeyModMask 过滤）
UINT HotkeyModifiers(DWORD hotkey);

// 取编码里的虚拟键码
UINT HotkeyVirtualKey(DWORD hotkey);

// 虚拟键码是否为可作快捷键主键的键：A~Z、0~9、F1~F12
bool IsSupportedHotkeyVk(UINT vk);

// 虚拟键码是否为修饰键本身（Ctrl / Alt / Shift / Win，含左右两侧的变体）
bool IsModifierVk(UINT vk);

/**
 *  由一次按键事件合成快捷键（设置页录入新组合时用）。
 *  @param vk    按下的虚拟键码
 *  @param ctrl  Ctrl 是否按着
 *  @param alt   Alt 是否按着
 *  @param shift Shift 是否按着
 *  @return 编码后的值；vk 为修饰键本身时返回 0（还在等主键）。不在这里判断组合是否合规，
 *          交给 CheckHotkey，以便给出具体原因
 */
DWORD ComposeHotkey(UINT vk, bool ctrl, bool alt, bool shift);

/**
 *  把快捷键转成显示文字。
 *  @param hotkey 编码后的值；0 时返回空串
 *  @param style  文字格式
 *  @return 如 "Ctrl + Alt + A" 或 "Ctrl+Alt+A"；键名不翻译（键帽上印的就是英文）
 */
CString FormatHotkey(DWORD hotkey, HotkeyTextStyle style);

/**
 *  校验快捷键组合。
 *  @param hotkey        编码后的值
 *  @param reserved      本程序已占用的组合表，可为 NULL
 *  @param reservedCount 表的项数
 *  @param conflictIndex 输出：结果为 kHotkeyReserved 时为冲突项在表里的下标，否则为 -1；可为 NULL
 *  @return 校验结果，按 停用 → 缺修饰键 → 主键不支持 → 与保留组合重复 的顺序判断
 */
HotkeyCheckResult CheckHotkey(DWORD hotkey, const ReservedHotkey* reserved, int reservedCount,
                              int* conflictIndex);

/**
 *  剪刀按钮的悬停提示，也是截图菜单第一项的文字。
 *  @param hotkey 当前的截图快捷键；0 表示已停用
 *  @return 有快捷键时为「截图（Alt+Z）」，停用时为「截图」（已按界面语言翻译）
 */
CString ScreenshotCommandText(DWORD hotkey);

/**
 *  托盘菜单「截图」一项的文字（docs/截图独立模块方案.md 3.6 节）。
 *  @param hotkey     全局配置里的截图快捷键；0 表示停用
 *  @param registered 快捷键这次是否注册成功（GetScreenshotHotkeyState() == kShotHotkeyRegistered）
 *  @return 注册成功时为「截图（Alt+Z）」；停用或注册失败（多为被其他程序占用）时只写「截图」，免得提示一个按了没反应的组合
 */
CString TrayScreenshotLabel(DWORD hotkey, bool registered);

// 本程序注册在主面板窗口上的全局快捷键，此刻该注册哪几个（docs/截图独立模块方案.md 3.5 节）
struct AppHotkeyPlan
{
    bool  m_showPanel;    // 「打开主面板」Ctrl+Alt+D：只在已登录时注册
    bool  m_record;       // 录屏 Ctrl+Alt+R：已登录且设置里开启时注册
    DWORD m_screenshot;   // 截图快捷键编码：不论是否登录都按全局配置注册；0 表示不注册
};

/**
 *  算出此刻该注册哪几个全局快捷键。截图快捷键 2026-10-07 起存在全局配置里，未登录时也注册；
 *  另外两个只在登录后才有意义。
 *  @param loggedIn         是否已登录（锁屏解锁时传 balloonlock::ShouldResumeHotkeysAfterUnlock 的结果）
 *  @param recordEnabled    录屏快捷键是否开启（CUserConfig::IsEnableRecordHotkey）；未登录时不看
 *  @param screenshotHotkey 截图快捷键编码（全局配置），0 表示停用
 *  @return 注册计划
 */
AppHotkeyPlan PlanAppHotkeys(bool loggedIn, bool recordEnabled, DWORD screenshotHotkey);

} // namespace balloonscreenshot
