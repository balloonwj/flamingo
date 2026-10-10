/**
 *  ScreenshotPrefs.h
 *
 *  截图的两项设置（全局快捷键、截图时隐藏当前窗口）在全局配置文件 config\flamingo.ini 里的读写与迁移
 *  （docs/截图独立模块方案.md 第 3.4 节）。2026-10-07 之前这两项存在账号配置（<账号>.ini 的 [Settings] 段），
 *  登录成功后才读得到；改为全局配置后，程序一启动就能按它注册快捷键，未登录时也能截图。
 *
 *  配置文件里的格式（flamingo.ini 只能是 ASCII，值写成十进制整数）：
 *      [screenshot]
 *      hotkey=65626        快捷键编码（见 ScreenshotHotkey.h），0 表示停用
 *      hidewindow=1        截图时隐藏发起截图的聊天窗：1 隐藏、0 不隐藏
 *
 *  全部是纯函数，参数是配置文件路径，FlamingoUnit 用临时文件直接验证（用例 SP1~SP3）。
 *
 *  典型用法：
 *      const std::wstring ini = GlobalConfigIniPath(g_szHomePath);
 *      ScreenshotPrefs prefs = LoadScreenshotPrefs(ini.c_str());
 *      ApplyScreenshotHotkey(panel, prefs.m_hotkey);
 *      //登录成功后把老账号配置里的值迁移过来（全局配置里已有值时什么也不做）
 *      MigrateScreenshotPrefs(ini.c_str(), accountPrefs);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <string>

namespace balloonscreenshot {

// 全局配置文件里截图设置所在的段名
const wchar_t* const kShotPrefsSection = L"screenshot";

// 快捷键编码的键名
const wchar_t* const kShotPrefsHotkeyKey = L"hotkey";

// 「截图时隐藏当前窗口」的键名
const wchar_t* const kShotPrefsHideWindowKey = L"hidewindow";

// 截图的两项设置
struct ScreenshotPrefs
{
    // 默认值：Alt+Z、截图时隐藏当前窗口（与账号配置里这两项的默认值相同）
    ScreenshotPrefs();

    DWORD m_hotkey;       // 快捷键编码，格式见 ScreenshotHotkey.h；0 表示停用
    bool  m_hideWindow;   // 截图时是否把发起截图的聊天窗排除在截图之外
};

/**
 *  全局配置文件的完整路径。
 *  @param homePath 程序所在目录，以反斜杠结尾（g_szHomePath），不可为 NULL
 *  @return <homePath>config\flamingo.ini
 */
std::wstring GlobalConfigIniPath(const wchar_t* homePath);

/**
 *  读取截图设置。
 *  @param iniPath 配置文件路径，不可为 NULL
 *  @return 读到的设置。文件或键不存在、值不是十进制非负整数时该项取默认值；「隐藏」只认 0 与 1，
 *          其它值同样取默认值。不能用 GetPrivateProfileInt 读：它把非数字读成 0，快捷键就成了「停用」
 */
ScreenshotPrefs LoadScreenshotPrefs(const wchar_t* iniPath);

/**
 *  配置文件里是否已经保存过有效的截图快捷键（迁移时据此判断是否需要写入）。
 *  @param iniPath 配置文件路径，不可为 NULL
 *  @return 有 [screenshot] hotkey 且是十进制非负整数时返回 true
 */
bool HasSavedScreenshotPrefs(const wchar_t* iniPath);

/**
 *  保存截图设置，两项都写。
 *  @param iniPath 配置文件路径，不可为 NULL；文件不存在时由系统创建
 *  @param prefs   要保存的设置
 *  @return 两项都写成功返回 true
 */
bool SaveScreenshotPrefs(const wchar_t* iniPath, const ScreenshotPrefs& prefs);

/**
 *  迁移：配置文件里还没有保存过截图快捷键时，写入账号配置里的两项；已有值时不覆盖。
 *  @param iniPath      配置文件路径，不可为 NULL
 *  @param accountPrefs 当前登录账号配置里的两项（CUserConfig::GetScreenshotHotkey / IsScreenshotHideWindow）
 *  @return 本次写入了返回 true；已有值不需要迁移、或写入失败时返回 false
 */
bool MigrateScreenshotPrefs(const wchar_t* iniPath, const ScreenshotPrefs& accountPrefs);

} // namespace balloonscreenshot
