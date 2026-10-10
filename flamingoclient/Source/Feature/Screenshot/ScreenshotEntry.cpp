/**
 *  ScreenshotEntry.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "ScreenshotEntry.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"                              // TR_NOOP：保留组合表里的功能名称
#include "../ScreenShare/RecordEntry.h"                          // 录屏快捷键 Ctrl+Alt+R 的标识与主键
#include "../../UI/BalloonMain/MainPanelSessionController.h"    // 打开主面板快捷键 Ctrl+Alt+D 的标识与主键
#include "ScreenshotHost.h"                                       // 截图交给 FlamingoScreenshot.dll
#include "../../Session/UserSessionData.h"                       // g_szHomePath：全局配置文件所在的程序目录

namespace balloonscreenshot {

namespace {

// RegisterHotKey 的修饰键标志：按住不放时只触发一次。老 SDK 头文件里可能没有这个宏
const UINT kModNoRepeat = 0x4000;

// 「打开主面板」与「录屏」两个全局快捷键都用 Ctrl+Alt 修饰
const UINT kAppHotkeyMods = MOD_CONTROL | MOD_ALT;

// 本程序已占用的全局快捷键表（顺序无关紧要；名称用于提示「与某某重复」）
const ReservedHotkey kAppReserved[] =
{
    { ((DWORD)kAppHotkeyMods << kHotkeyModShift) | (DWORD)balloonmain::kShowPanelHotKey, TR_NOOP(L"打开主面板") },
    { ((DWORD)kAppHotkeyMods << kHotkeyModShift) | (DWORD)balloonscreenshare::kRecordHotKey, TR_NOOP(L"录屏") },
};

// 最近一次 ApplyScreenshotHotkey 的结果。只在 UI 线程读写
ScreenshotHotkeyState g_hotkeyState = kShotHotkeyOff;

} // anonymous namespace

bool ApplyScreenshotHotkey(HWND panel, DWORD hotkey)
{
    if (panel == NULL || !::IsWindow(panel))
    {
        return false;
    }
    ::UnregisterHotKey(panel, kScreenshotHotKeyId);
    if (!IsScreenshotAvailable())
    {
        // 截图模块没有加载成功：按了也截不了图，不注册
        g_hotkeyState = kShotHotkeyUnavailable;
        LOG_WARNING("screenshot hotkey: not registered, the screenshot module is unavailable.");
        return false;
    }
    if (hotkey == 0)
    {
        g_hotkeyState = kShotHotkeyOff;
        LOG_INFO("screenshot hotkey: disabled.");
        return true;
    }

    int count = 0;
    const ReservedHotkey* reserved = AppReservedHotkeys(count);
    HotkeyCheckResult check = CheckHotkey(hotkey, reserved, count, NULL);
    if (check != kHotkeyOk)
    {
        // 配置文件被手工改坏，或与本程序其它快捷键重复：不注册
        g_hotkeyState = kShotHotkeyFailed;
        LOG_WARNING("screenshot hotkey: 0x%08X rejected, check result=%d.", (unsigned)hotkey, (int)check);
        return false;
    }

    UINT mods = HotkeyModifiers(hotkey) | kModNoRepeat;
    UINT vk = HotkeyVirtualKey(hotkey);
    CStringA keys(FormatHotkey(hotkey, kHotkeyTextCompact));
    if (!::RegisterHotKey(panel, kScreenshotHotKeyId, mods, vk))
    {
        g_hotkeyState = kShotHotkeyFailed;
        LOG_WARNING("screenshot hotkey: register %s failed, error=%u.", (LPCSTR)keys,
                    static_cast<unsigned>(::GetLastError()));
        return false;
    }
    g_hotkeyState = kShotHotkeyRegistered;
    LOG_INFO("screenshot hotkey: %s registered.", (LPCSTR)keys);
    return true;
}

ScreenshotHotkeyState GetScreenshotHotkeyState()
{
    return g_hotkeyState;
}

void SuspendAppHotkeys(HWND panel)
{
    if (panel == NULL || !::IsWindow(panel))
    {
        return;
    }
    ::UnregisterHotKey(panel, balloonmain::kShowPanelHotKeyId);
    ::UnregisterHotKey(panel, balloonscreenshare::kRecordHotKeyId);
    ::UnregisterHotKey(panel, kScreenshotHotKeyId);
    LOG_INFO("app hotkeys: suspended (settings page recording a new combination, or screen locked).");
}

bool ResumeAppHotkeys(HWND panel, bool loggedIn, bool recordEnabled, DWORD screenshotHotkey)
{
    if (panel == NULL || !::IsWindow(panel))
    {
        return false;
    }
    const AppHotkeyPlan plan = PlanAppHotkeys(loggedIn, recordEnabled, screenshotHotkey);
    // 与 MainPanelSessionController 登录成功后的注册方式保持一致；未登录时只注册截图快捷键
    ::UnregisterHotKey(panel, balloonmain::kShowPanelHotKeyId);
    if (plan.m_showPanel
        && !::RegisterHotKey(panel, balloonmain::kShowPanelHotKeyId, kAppHotkeyMods, balloonmain::kShowPanelHotKey))
    {
        LOG_WARNING("app hotkeys: re-register Ctrl+Alt+D failed, error=%u.", static_cast<unsigned>(::GetLastError()));
    }
    balloonscreenshare::ApplyRecordHotkey(panel, plan.m_record);
    bool ok = ApplyScreenshotHotkey(panel, plan.m_screenshot);
    LOG_INFO("app hotkeys: resumed (logged in=%d), screenshot hotkey state=%d.", loggedIn ? 1 : 0, (int)g_hotkeyState);
    return ok;
}

const ReservedHotkey* AppReservedHotkeys(int& count)
{
    count = (int)(sizeof(kAppReserved) / sizeof(kAppReserved[0]));
    return kAppReserved;
}

bool IsScreenshotAvailable()
{
    return CScreenshotHost::GetInstance().IsModuleReady();
}

bool RunScreenshot(HWND owner, const ScreenshotOptions& opts)
{
    CScreenshotHost& host = CScreenshotHost::GetInstance();
    if (!host.IsModuleReady())
    {
        LOG_WARNING("screenshot: requested but the screenshot module is unavailable.");
        return false;
    }
    if (host.IsRunning())
    {
        LOG_INFO("screenshot: a capture is already in progress, request ignored.");
        return false;
    }
    const flamingoshot::ScreenshotRequest request = MakeScreenshotRequest(owner, opts);
    CScreenshotCallbackSink sink(opts);
    return host.Run(request, sink.HasTarget() ? &sink : NULL);
}

void RunClipboardScreenshot()
{
    LOG_INFO("screenshot: start a clipboard-only capture (not bound to a conversation).");
    // 不绑定会话：两个回调都为空，操作栏为「重选 / 保存 / 取消 / 完成」，也不排除任何窗口
    ScreenshotOptions opts;
    RunScreenshot(NULL, opts);
}

ScreenshotPrefs LoadGlobalScreenshotPrefs()
{
    const std::wstring ini = GlobalConfigIniPath(g_szHomePath);
    return LoadScreenshotPrefs(ini.c_str());
}

bool SaveGlobalScreenshotPrefs(const ScreenshotPrefs& prefs)
{
    const std::wstring ini = GlobalConfigIniPath(g_szHomePath);
    if (!SaveScreenshotPrefs(ini.c_str(), prefs))
    {
        LOG_WARNING("screenshot prefs: write to global config failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        return false;
    }
    LOG_INFO("screenshot prefs: saved to global config, hotkey=0x%08X, hide window=%d.",
             (unsigned)prefs.m_hotkey, prefs.m_hideWindow ? 1 : 0);
    return true;
}

void MigrateAccountScreenshotPrefs(DWORD accountHotkey, bool accountHideWindow)
{
    ScreenshotPrefs account;
    account.m_hotkey = accountHotkey;
    account.m_hideWindow = accountHideWindow;
    const std::wstring ini = GlobalConfigIniPath(g_szHomePath);
    if (MigrateScreenshotPrefs(ini.c_str(), account))
    {
        LOG_INFO("screenshot prefs: migrated from account config, hotkey=0x%08X, hide window=%d.",
                 (unsigned)accountHotkey, accountHideWindow ? 1 : 0);
    }
}

} // namespace balloonscreenshot
