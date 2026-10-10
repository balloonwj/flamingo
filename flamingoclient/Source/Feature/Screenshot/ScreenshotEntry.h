/**
 *  ScreenshotEntry.h
 *
 *  exe 侧的截图快捷键入口（docs/截图快捷键与隐藏当前窗口方案.md 第 3 节）：
 *    · 按设置在主面板窗口上注册 / 注销截图全局快捷键，并记下最近一次的注册结果，供设置页
 *      提示「已被其他程序占用」；
 *    · 设置页录入新组合期间暂停本程序的三个全局快捷键（打开主面板、录屏、截图），录入结束后
 *      恢复——否则按下的组合恰好是这三个之一时，会被系统当作热键拦走，录入控件收不到按键；
 *    · 本程序已占用的全局快捷键表（校验新组合时查重）；
 *    · 在聊天窗之外按快捷键时发起不绑定会话的截图（操作栏为「完成」，结果放进剪贴板）。
 *  纯逻辑部分（编码、文字、校验）见 ScreenshotHotkey.h。只在 UI 线程调用。
 *
 *  典型用法：
 *      balloonscreenshot::ApplyScreenshotHotkey(m_hWnd, cfg.GetScreenshotHotkey());
 *      if (nHotKeyID == balloonscreenshot::kScreenshotHotKeyId) { ... }
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include "ScreenshotHotkey.h"
#include "ScreenshotPrefs.h"   // ScreenshotPrefs：全局配置里的截图两项设置
#include "ScreenshotRequestBuilder.h"   // ScreenshotOptions：发起截图的选项（聊天窗填写）

namespace balloonscreenshot {

// 最近一次 ApplyScreenshotHotkey 的结果
enum ScreenshotHotkeyState
{
    kShotHotkeyOff = 0,       // 已停用（编码为 0），或还没有注册过
    kShotHotkeyRegistered,    // 已注册成功
    kShotHotkeyFailed,        // 注册失败：多为被其他程序占用，或组合不合规
    kShotHotkeyUnavailable,   // 截图模块 FlamingoScreenshot.dll 没有加载成功，截图功能不可用，快捷键不注册（2026-10-07）
};

/**
 *  按设置注册截图快捷键：先注销，hotkey 非 0 且合规时再注册（带 MOD_NOREPEAT，按住不放不会连发）。
 *  @param panel  接收 WM_HOTKEY 的窗口（主面板），须是本线程创建的窗口
 *  @param hotkey 快捷键编码；0 表示停用
 *  @return 停用时返回 true；注册成功返回 true；组合不合规或被占用返回 false（已记日志）
 *  副作用：更新 GetScreenshotHotkeyState 的返回值
 */
bool ApplyScreenshotHotkey(HWND panel, DWORD hotkey);

// 最近一次 ApplyScreenshotHotkey 的结果（设置页据此显示提示）
ScreenshotHotkeyState GetScreenshotHotkeyState();

/**
 *  暂停本程序的三个全局快捷键（打开主面板、录屏、截图），设置页开始录入新组合时调用。
 *  @param panel 注册这些快捷键的窗口（主面板）
 */
void SuspendAppHotkeys(HWND panel);

/**
 *  恢复本程序的全局快捷键，设置页录入结束（无论成功、取消）后、解锁后调用。按 PlanAppHotkeys 的计划注册：
 *  未登录时只注册截图快捷键（2026-10-07 起，docs/截图独立模块方案.md 3.5 节）。
 *  @param panel            注册这些快捷键的窗口（主面板）
 *  @param loggedIn         是否已登录；false 时不注册「打开主面板」与录屏两个快捷键（并注销它们）
 *  @param recordEnabled    录屏快捷键是否开启（CUserConfig::IsEnableRecordHotkey）
 *  @param screenshotHotkey 截图快捷键编码（全局配置，LoadGlobalScreenshotPrefs），0 表示停用
 *  @return 截图快捷键的注册结果，同 ApplyScreenshotHotkey
 */
bool ResumeAppHotkeys(HWND panel, bool loggedIn, bool recordEnabled, DWORD screenshotHotkey);

/**
 *  本程序已占用的全局快捷键表：打开主面板 Ctrl+Alt+D、录屏 Ctrl+Alt+R。
 *  @param count 输出：表的项数
 *  @return 静态表首地址，调用方只读
 */
const ReservedHotkey* AppReservedHotkeys(int& count);

/**
 *  截图功能是否可用：截图模块 FlamingoScreenshot.dll 已加载并初始化（docs/截图独立模块方案.md 3.3 节）。
 *  为 false 时托盘菜单不出现「截图」、聊天窗的截图按钮隐藏、截图快捷键不注册。
 */
bool IsScreenshotAvailable();

/**
 *  发起一次截图，交给截图模块。模态运行，返回时截图已结束；已有截图在进行或模块不可用时直接返回 false。
 *  @param owner 发起截图的窗口（聊天窗），可为 NULL（在聊天窗之外发起时）
 *  @param opts  选项：要排除的窗口、是否以全屏选区开始、「发送」「发送到对话框」两个回调（都为 NULL 时不绑定会话）
 *  @return 用户选了「发送」或「发送到对话框」（已回调）返回 true；其它情况返回 false
 */
bool RunScreenshot(HWND owner, const ScreenshotOptions& opts);

/**
 *  发起一次不绑定会话的截图（在聊天窗之外按截图快捷键、托盘菜单「截图」）。模态运行，返回时截图已结束：
 *  「完成」「复制」放进剪贴板，「保存」存成文件。已有截图在进行或截图模块不可用时直接返回。
 */
void RunClipboardScreenshot();

/**
 *  读取全局配置（<程序目录>config\flamingo.ini 的 [screenshot] 段）里的截图快捷键与「截图时隐藏当前窗口」。
 *  2026-10-07 起这两项不再按账号保存，所有账号共用，未登录时也读得到（docs/截图独立模块方案.md 第 3.4 节）。
 *  @return 读到的设置；没有保存过或值无效时为默认值（Alt+Z、隐藏）
 */
ScreenshotPrefs LoadGlobalScreenshotPrefs();

/**
 *  把截图设置写进全局配置。
 *  @param prefs 要保存的设置
 *  @return 写成功返回 true（失败已记日志）
 */
bool SaveGlobalScreenshotPrefs(const ScreenshotPrefs& prefs);

/**
 *  登录成功、加载完账号配置之后调用：全局配置里还没有截图设置时，把这个账号配置里的两项迁移过去；
 *  已有时什么也不做（升级后第一次登录的那个账号的设置生效，之后各账号共用）。
 *  @param accountHotkey     账号配置里的截图快捷键（CUserConfig::GetScreenshotHotkey）
 *  @param accountHideWindow 账号配置里的「截图时隐藏当前窗口」（CUserConfig::IsScreenshotHideWindow）
 */
void MigrateAccountScreenshotPrefs(DWORD accountHotkey, bool accountHideWindow);

} // namespace balloonscreenshot
