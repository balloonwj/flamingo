/**
 *  ScreenshotPrefsTests.cpp
 *
 *  截图全局配置的测试（docs/截图独立模块方案.md 第 3.4 节、第 4 节，2026-10-07 与用户确认）：
 *    · SP1：全局配置里没有 [screenshot] 段时读到默认值（Alt+Z、截图时隐藏当前窗口）；
 *    · SP2：写入后读回一致；值不是十进制非负整数时按默认值；快捷键 0 表示停用；
 *    · SP3：迁移——全局配置里还没有值时写入账号配置里的值，已有值时不覆盖，也不动其它段。
 *
 *  每条用例都用独立的临时配置文件，文件名带进程 id，两个测试进程同时跑也不会共用。
 *  用 Catch2 v3（amalgamated）。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>

#include "../Feature/Screenshot/ScreenshotPrefs.h"
#include "../Feature/Screenshot/ScreenshotHotkey.h"

using namespace balloonscreenshot;

namespace {

// Ctrl+Shift+A：SP2 写入的快捷键
const DWORD kCtrlShiftA = ((DWORD)(MOD_CONTROL | MOD_SHIFT) << kHotkeyModShift) | (DWORD)'A';
// Ctrl+Q：SP3 里账号配置的快捷键
const DWORD kCtrlQ = ((DWORD)MOD_CONTROL << kHotkeyModShift) | (DWORD)'Q';
// Alt+X：SP3 第二次迁移时账号配置的快捷键（不应生效）
const DWORD kAltX = ((DWORD)MOD_ALT << kHotkeyModShift) | (DWORD)'X';
// 读取原始键值的缓冲区长度（字符）
const DWORD kRawValueChars = 64;

// 临时配置文件：构造时生成带进程 id 与用例名的路径并删除旧文件，析构时删除
class CTempIni
{
public:
    explicit CTempIni(const wchar_t* tag)
    {
        wchar_t dir[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, dir);
        wchar_t name[MAX_PATH] = { 0 };
        swprintf_s(name, L"flamingo_shotprefs_%lu_%s.ini", ::GetCurrentProcessId(), tag);
        m_path = std::wstring(dir) + name;
        ::DeleteFileW(m_path.c_str());
    }

    ~CTempIni()
    {
        ::DeleteFileW(m_path.c_str());
    }

    const wchar_t* Path() const
    {
        return m_path.c_str();
    }

    // 直接写一个原始键值（模拟手工改坏的配置）
    void WriteRaw(const wchar_t* section, const wchar_t* key, const wchar_t* value) const
    {
        ::WritePrivateProfileStringW(section, key, value, m_path.c_str());
    }

    // 读一个原始键值，不存在时返回空串
    std::wstring ReadRaw(const wchar_t* section, const wchar_t* key) const
    {
        wchar_t buf[kRawValueChars] = { 0 };
        ::GetPrivateProfileStringW(section, key, L"", buf, kRawValueChars, m_path.c_str());
        return std::wstring(buf);
    }

private:
    std::wstring m_path;   // 临时配置文件的完整路径
};

} // namespace

TEST_CASE("SP1 全局配置里没有截图段时读到默认值：Alt+Z、截图时隐藏当前窗口", "[shotprefs][screenshot]")
{
    //文件不存在
    CTempIni ini(L"sp1");
    ScreenshotPrefs prefs = LoadScreenshotPrefs(ini.Path());
    CHECK(prefs.m_hotkey == kDefaultScreenshotHotkey);
    CHECK(prefs.m_hideWindow);
    CHECK_FALSE(HasSavedScreenshotPrefs(ini.Path()));

    //文件存在、只有别的段
    ini.WriteRaw(L"ui", L"language", L"en");
    prefs = LoadScreenshotPrefs(ini.Path());
    CHECK(prefs.m_hotkey == kDefaultScreenshotHotkey);
    CHECK(prefs.m_hideWindow);
    CHECK_FALSE(HasSavedScreenshotPrefs(ini.Path()));

    //默认构造的设置与读到的默认值相同
    ScreenshotPrefs def;
    CHECK(def.m_hotkey == kDefaultScreenshotHotkey);
    CHECK(def.m_hideWindow);
}

TEST_CASE("SP2 写入后读回一致；值不是十进制非负整数时按默认值；快捷键 0 表示停用", "[shotprefs][screenshot]")
{
    CTempIni ini(L"sp2");
    ScreenshotPrefs w;
    w.m_hotkey = kCtrlShiftA;
    w.m_hideWindow = false;
    REQUIRE(SaveScreenshotPrefs(ini.Path(), w));
    CHECK(HasSavedScreenshotPrefs(ini.Path()));
    ScreenshotPrefs r = LoadScreenshotPrefs(ini.Path());
    CHECK(r.m_hotkey == kCtrlShiftA);
    CHECK_FALSE(r.m_hideWindow);
    //写成十进制整数（flamingo.ini 只能是 ASCII）
    CHECK(ini.ReadRaw(kShotPrefsSection, kShotPrefsHotkeyKey) == std::to_wstring((unsigned long long)kCtrlShiftA));
    CHECK(ini.ReadRaw(kShotPrefsSection, kShotPrefsHideWindowKey) == L"0");

    //停用：0
    w.m_hotkey = 0;
    REQUIRE(SaveScreenshotPrefs(ini.Path(), w));
    CHECK(LoadScreenshotPrefs(ini.Path()).m_hotkey == 0);

    //手工改坏的值按默认值处理（GetPrivateProfileInt 会把非数字读成 0，那样就成了「停用」，这里不能那样）
    const wchar_t* const badValues[] = { L"abc", L"12ab", L"-5", L"", L" " };
    for (size_t i = 0; i < sizeof(badValues) / sizeof(badValues[0]); ++i)
    {
        ini.WriteRaw(kShotPrefsSection, kShotPrefsHotkeyKey, badValues[i]);
        ini.WriteRaw(kShotPrefsSection, kShotPrefsHideWindowKey, badValues[i]);
        r = LoadScreenshotPrefs(ini.Path());
        INFO("value=[" << CW2A(badValues[i]) << "]");
        CHECK(r.m_hotkey == kDefaultScreenshotHotkey);
        CHECK(r.m_hideWindow);
    }
    //「隐藏」只认 0 与 1
    ini.WriteRaw(kShotPrefsSection, kShotPrefsHideWindowKey, L"1");
    CHECK(LoadScreenshotPrefs(ini.Path()).m_hideWindow);
    ini.WriteRaw(kShotPrefsSection, kShotPrefsHideWindowKey, L"2");
    CHECK(LoadScreenshotPrefs(ini.Path()).m_hideWindow);
}

TEST_CASE("SP3 迁移：全局配置里还没有值时写入账号配置里的值，已有值时不覆盖，也不动其它段", "[shotprefs][screenshot]")
{
    CTempIni ini(L"sp3");
    ini.WriteRaw(L"ui", L"language", L"en");

    ScreenshotPrefs account;
    account.m_hotkey = kCtrlQ;
    account.m_hideWindow = false;
    CHECK(MigrateScreenshotPrefs(ini.Path(), account));
    ScreenshotPrefs r = LoadScreenshotPrefs(ini.Path());
    CHECK(r.m_hotkey == kCtrlQ);
    CHECK_FALSE(r.m_hideWindow);
    CHECK(HasSavedScreenshotPrefs(ini.Path()));

    //再登录另一个账号：全局配置已有值，不覆盖
    ScreenshotPrefs other;
    other.m_hotkey = kAltX;
    other.m_hideWindow = true;
    CHECK_FALSE(MigrateScreenshotPrefs(ini.Path(), other));
    r = LoadScreenshotPrefs(ini.Path());
    CHECK(r.m_hotkey == kCtrlQ);
    CHECK_FALSE(r.m_hideWindow);

    //账号停用了快捷键（0）也照样迁移
    CTempIni ini2(L"sp3b");
    ScreenshotPrefs off;
    off.m_hotkey = 0;
    CHECK(MigrateScreenshotPrefs(ini2.Path(), off));
    CHECK(LoadScreenshotPrefs(ini2.Path()).m_hotkey == 0);
    CHECK(HasSavedScreenshotPrefs(ini2.Path()));

    //其它段不受影响
    CHECK(ini.ReadRaw(L"ui", L"language") == L"en");
}
