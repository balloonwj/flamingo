/**
 *  ScreenshotPrefs.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ScreenshotPrefs.h"

#include "ScreenshotHotkey.h"   // kDefaultScreenshotHotkey

namespace balloonscreenshot {

namespace {

// 全局配置文件相对程序目录的路径
const wchar_t* const kGlobalIniRelPath = L"config\\flamingo.ini";

// 读取一个键值的缓冲区长度（字符）：DWORD 的十进制最多 10 位，留出余量以便识别过长的值
const DWORD kValueChars = 32;

// DWORD 的十进制最多几位
const size_t kMaxDecimalDigits = 10;

// 十进制
const unsigned long long kDecimalBase = 10;

// 「隐藏当前窗口」两个合法取值的文字
const wchar_t* const kHideWindowOn = L"1";
const wchar_t* const kHideWindowOff = L"0";

// 读一个键的文字；键不存在或值为空时返回空串
std::wstring ReadValue(const wchar_t* iniPath, const wchar_t* key)
{
    wchar_t buf[kValueChars] = { 0 };
    ::GetPrivateProfileStringW(kShotPrefsSection, key, L"", buf, kValueChars, iniPath);
    return std::wstring(buf);
}

// 把十进制非负整数文字解析成 DWORD；空串、带符号、夹带其它字符、超出 DWORD 范围时返回 false
bool ParseDecimal(const std::wstring& text, DWORD& out)
{
    if (text.empty() || text.size() > kMaxDecimalDigits)
        return false;
    unsigned long long value = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        if (c < L'0' || c > L'9')
            return false;
        value = value * kDecimalBase + (unsigned long long)(c - L'0');
    }
    if (value > (unsigned long long)MAXDWORD)
        return false;
    out = (DWORD)value;
    return true;
}

} // anonymous namespace

ScreenshotPrefs::ScreenshotPrefs()
    : m_hotkey(kDefaultScreenshotHotkey)
    , m_hideWindow(true)
{
}

std::wstring GlobalConfigIniPath(const wchar_t* homePath)
{
    return std::wstring(homePath) + kGlobalIniRelPath;
}

ScreenshotPrefs LoadScreenshotPrefs(const wchar_t* iniPath)
{
    ScreenshotPrefs prefs;
    DWORD hotkey = 0;
    if (ParseDecimal(ReadValue(iniPath, kShotPrefsHotkeyKey), hotkey))
        prefs.m_hotkey = hotkey;
    const std::wstring hide = ReadValue(iniPath, kShotPrefsHideWindowKey);
    if (hide == kHideWindowOff)
        prefs.m_hideWindow = false;
    else if (hide == kHideWindowOn)
        prefs.m_hideWindow = true;
    return prefs;
}

bool HasSavedScreenshotPrefs(const wchar_t* iniPath)
{
    DWORD hotkey = 0;
    return ParseDecimal(ReadValue(iniPath, kShotPrefsHotkeyKey), hotkey);
}

bool SaveScreenshotPrefs(const wchar_t* iniPath, const ScreenshotPrefs& prefs)
{
    const std::wstring hotkey = std::to_wstring((unsigned long long)prefs.m_hotkey);
    const BOOL okHotkey = ::WritePrivateProfileStringW(kShotPrefsSection, kShotPrefsHotkeyKey, hotkey.c_str(), iniPath);
    const BOOL okHide = ::WritePrivateProfileStringW(kShotPrefsSection, kShotPrefsHideWindowKey,
                                                     prefs.m_hideWindow ? kHideWindowOn : kHideWindowOff, iniPath);
    return okHotkey != FALSE && okHide != FALSE;
}

bool MigrateScreenshotPrefs(const wchar_t* iniPath, const ScreenshotPrefs& accountPrefs)
{
    if (HasSavedScreenshotPrefs(iniPath))
        return false;
    return SaveScreenshotPrefs(iniPath, accountPrefs);
}

} // namespace balloonscreenshot
