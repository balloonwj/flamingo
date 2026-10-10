/**
 *  UrlProtocolRegistry.cpp —— 注册 flamingo:// 协议，说明见 UrlProtocolRegistry.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "UrlProtocolRegistry.h"

#include "../UI/BalloonMain/GroupLinkFlow.h"   // BuildProtocolOpenCommand：shell\open\command 的值
#include "../Common/Log/IULog.h"

namespace apputil {

namespace {

// 注册表里协议根键（当前用户）
const wchar_t* const kProtocolKey = L"Software\\Classes\\flamingo";
// 打开命令所在的子键
const wchar_t* const kCommandKey = L"Software\\Classes\\flamingo\\shell\\open\\command";
// 图标子键
const wchar_t* const kIconKey = L"Software\\Classes\\flamingo\\DefaultIcon";
// 协议的说明文字（注册表里的名字，不显示在本程序界面上）
const wchar_t* const kProtocolDesc = L"URL:Flamingo Protocol";
// 标明这是一个 URL 协议的值名
const wchar_t* const kUrlProtocolValue = L"URL Protocol";
// 读取命令行时的缓冲长度（字符）：exe 路径最长按 32K 算
const DWORD kMaxValueChars = 32768;

// 读一个键的默认值；读不到返回空串
std::wstring ReadDefaultValue(const wchar_t* subKey)
{
    HKEY hKey = NULL;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
    {
        return std::wstring();
    }
    std::wstring value(kMaxValueChars, L'\0');
    DWORD type = 0;
    DWORD bytes = kMaxValueChars * sizeof(wchar_t);
    const LONG rc = ::RegQueryValueExW(hKey, NULL, NULL, &type, (LPBYTE)&value[0], &bytes);
    ::RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || type != REG_SZ)
    {
        return std::wstring();
    }
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value[value.size() - 1] == L'\0')
    {
        value.resize(value.size() - 1);
    }
    return value;
}

// 建（或打开）一个键并写一个字符串值；valueName 为 NULL 时写默认值
bool WriteStringValue(const wchar_t* subKey, const wchar_t* valueName, const std::wstring& data)
{
    HKEY hKey = NULL;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL)
        != ERROR_SUCCESS)
    {
        return false;
    }
    const DWORD bytes = (DWORD)((data.size() + 1) * sizeof(wchar_t));
    const LONG rc = ::RegSetValueExW(hKey, valueName, 0, REG_SZ, (const BYTE*)data.c_str(), bytes);
    ::RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

} // anonymous namespace

bool RegisterFlamingoUrlProtocol(const std::wstring& exePath)
{
    if (exePath.empty())
    {
        return false;
    }
    const std::wstring command = balloonmain::BuildProtocolOpenCommand(exePath);
    if (ReadDefaultValue(kCommandKey) == command)
    {
        return true;
    }
    const bool ok = WriteStringValue(kProtocolKey, NULL, kProtocolDesc)
                    && WriteStringValue(kProtocolKey, kUrlProtocolValue, std::wstring())
                    && WriteStringValue(kIconKey, NULL, L"\"" + exePath + L"\",0")
                    && WriteStringValue(kCommandKey, NULL, command);
    if (ok)
    {
        LOG_INFO("url protocol: flamingo:// registered for the current exe.");
    }
    else
    {
        LOG_WARNING("url protocol: failed to register flamingo:// in HKCU.");
    }
    return ok;
}

} // namespace apputil
