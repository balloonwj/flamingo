/**
 *  语音字幕宿主的纯逻辑部分，见 CaptionHostPolicy.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionHostPolicy.h"

#include <windows.h>

#include <vector>

namespace captionhost {

const wchar_t* const kCaptionSettingsFileName = L"caption_settings.json";

namespace {

//UTF-8 BOM 的三个字节
const unsigned char kUtf8Bom[] = { 0xEF, 0xBB, 0xBF };
const size_t kUtf8BomBytes = sizeof(kUtf8Bom);

//临时文件名的后缀：与目标文件同目录，替换时才能保证是同一卷上的改名
const wchar_t* const kTempSuffix = L".tmp";

} // namespace

bool IsCaptionAvailable(const CaptionGateState& state)
{
    return state.m_loggedIn && state.m_featureEnabled && !state.m_loadFailed;
}

bool ShouldLoadCaptionDll(const CaptionGateState& state, bool isLoaded)
{
    return IsCaptionAvailable(state) && !isLoaded;
}

std::wstring SettingsFilePath(const std::wstring& personalDir)
{
    if (personalDir.empty())
        return std::wstring();
    std::wstring path = personalDir;
    if (path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
        path += L'\\';
    path += kCaptionSettingsFileName;
    return path;
}

bool ReadSettingsFile(const std::wstring& path, std::string& text)
{
    text.clear();
    if (path.empty())
        return false;
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    size.QuadPart = 0;
    bool ok = ::GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 &&
              size.QuadPart <= static_cast<LONGLONG>(kCaptionSettingsMaxBytes);
    std::vector<char> buf;
    if (ok && size.QuadPart > 0)
    {
        buf.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ::ReadFile(file, &buf[0], static_cast<DWORD>(buf.size()), &read, NULL) != FALSE &&
             read == static_cast<DWORD>(buf.size());
    }
    ::CloseHandle(file);
    if (!ok)
        return false;
    size_t start = 0;
    if (buf.size() >= kUtf8BomBytes && memcmp(&buf[0], kUtf8Bom, kUtf8BomBytes) == 0)
        start = kUtf8BomBytes;
    if (buf.size() > start)
        text.assign(&buf[start], buf.size() - start);
    return true;
}

bool WriteSettingsFile(const std::wstring& path, const std::string& text)
{
    if (path.empty())
        return false;
    const std::wstring temp = path + kTempSuffix;
    HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = true;
    if (!text.empty())
    {
        ok = ::WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, NULL) != FALSE &&
             written == static_cast<DWORD>(text.size());
    }
    if (ok)
        ok = ::FlushFileBuffers(file) != FALSE;
    ::CloseHandle(file);
    if (ok)
        ok = ::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok)
        ::DeleteFileW(temp.c_str());
    return ok;
}

} // namespace captionhost
