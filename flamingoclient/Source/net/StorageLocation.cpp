/**
 *  本地数据根目录口径的实现，StorageLocation.cpp
 *  设计与归一规则见 StorageLocation.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "StorageLocation.h"

namespace net {

namespace {

//路径分隔符。Windows 下用反斜杠；正斜杠也能用，但既有代码（g_szHomePath、
//UserCacheDir）一律用反斜杠，此处保持一致，免得拼出混用两种分隔符的路径。
const wchar_t kSep = L'\\';

//盘符路径的最小长度："D:" 两个字符。
const size_t kDriveLetterPathLen = 2;

//给路径补上结尾的反斜杠（已经有就不动）。空串原样返回。
std::wstring EnsureTrailingSep(const std::wstring& path)
{
    if (path.empty())
    {
        return path;
    }
    if (path[path.size() - 1] == kSep)
    {
        return path;
    }
    return path + kSep;
}

//是否为绝对路径。认两种形式：盘符开头（"D:\..."）与 UNC（"\\\\server\\share"）。
//注意"\\Users"这种只有单个反斜杠开头的<u>不算</u>绝对路径 —— 它是"当前盘的根目录下"，
//盘符仍取决于进程的当前工作目录，同样会漂移。
bool IsAbsolutePath(const std::wstring& path)
{
    if (path.size() >= kDriveLetterPathLen
        && path[1] == L':'
        && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')))
    {
        return true;
    }
    //UNC：以两个反斜杠开头。
    if (path.size() >= kDriveLetterPathLen && path[0] == kSep && path[1] == kSep)
    {
        return true;
    }
    return false;
}

//路径里是否含有上跳（".."）。配置文件是纯文本、可能被手工改坏，含上跳的一律不接受。
bool ContainsDotDot(const std::wstring& path)
{
    return path.find(L"..") != std::wstring::npos;
}

} // anonymous namespace

std::wstring ResolveDataRoot(const std::wstring& configuredPath, const std::wstring& defaultRoot)
{
    const std::wstring fallback = EnsureTrailingSep(defaultRoot);

    //没配置过：用默认根。这是绝大多数用户的情形。
    if (configuredPath.empty())
    {
        return fallback;
    }

    //相对路径会随进程当前工作目录漂移，数据可能写到任何地方；含上跳的路径同理不可信。
    //两种情形都回落默认，而不是"尽力而为地凑一个路径"出来。
    if (!IsAbsolutePath(configuredPath) || ContainsDotDot(configuredPath))
    {
        return fallback;
    }

    //只给了盘符（"D:"）时补成 "D:\"：不补的话后续拼出来的是 "D:Users\..."，
    //那是相对于该盘<u>当前目录</u>的路径，同样会漂移。
    if (configuredPath.size() == kDriveLetterPathLen)
    {
        return configuredPath + kSep;
    }

    return EnsureTrailingSep(configuredPath);
}

} // namespace net
