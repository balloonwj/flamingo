/**
 *  按登录账号隔离的本地缓存目录口径实现，UserCachePath.cpp
 *
 *  除 EnsureUserEssentialDirs 会用 Win32 API 建目录外，其余都是纯逻辑，只依赖 STL，
 *  不碰磁盘、不读全局状态。与 FlamingoUnit 测试工程共用，因此不 include stdafx.h，
 *  在 Flamingo.vcxproj 里 PCH 须设 NotUsing。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#include "UserCachePath.h"

#include <windows.h>   //EnsureUserEssentialDirs：CreateDirectoryW / GetFileAttributesW

namespace net {

//Windows 文件名里不允许出现的保留字符。账号被拼进目录名，含这些字符会让路径
//要么非法、要么跑到别的目录去（'\' 与 '/' 尤其危险，':' 能改盘符）。
static const wchar_t* const kInvalidNameChars = L"\\/:*?\"<>|";

//控制字符的上界：小于该值的字符（含 0x00-0x1F）都不能出现在文件名里。
const wchar_t kFirstPrintableChar = 0x20;

std::wstring SanitizeAccountForPath(const std::wstring& account)
{
    std::wstring cleaned;
    cleaned.reserve(account.size());

    for (size_t i = 0; i < account.size(); ++i)
    {
        const wchar_t ch = account[i];

        //控制字符：一律换成下划线。
        if (ch < kFirstPrintableChar)
        {
            cleaned.push_back(L'_');
            continue;
        }

        //保留字符：一律换成下划线。这一步顺带把 ".." 之外的穿越手法也堵死了 ——
        //没有分隔符，".." 就只是个普通目录名，跳不出去。
        bool invalid = false;
        for (const wchar_t* p = kInvalidNameChars; *p != L'\0'; ++p)
        {
            if (ch == *p)
            {
                invalid = true;
                break;
            }
        }
        cleaned.push_back(invalid ? L'_' : ch);
    }

    //去掉首尾的点与空白。Windows 建目录时会把结尾的点和空格默默吃掉，留着会让
    //本函数拼出来的路径与磁盘上真实存在的目录名对不上，缓存于是"每次都找不到"。
    size_t begin = 0;
    while (begin < cleaned.size()
           && (cleaned[begin] == L'.' || cleaned[begin] == L' ' || cleaned[begin] == L'\t'))
    {
        ++begin;
    }

    size_t end = cleaned.size();
    while (end > begin
           && (cleaned[end - 1] == L'.' || cleaned[end - 1] == L' ' || cleaned[end - 1] == L'\t'))
    {
        --end;
    }

    return cleaned.substr(begin, end - begin);
}

std::wstring UserCacheDir(const std::wstring& homeDir, const std::wstring& account,
                          const wchar_t* subDir)
{
    //主目录拿不到就没法拼绝对路径；账号拿不到（多见于尚未登录）同样拼不出来。
    //两种情形都返回空串，由调用方按"没有缓存"处理，而不是退化成相对路径乱写。
    if (homeDir.empty())
    {
        return std::wstring();
    }

    const std::wstring safeAccount = SanitizeAccountForPath(account);
    if (safeAccount.empty())
    {
        return std::wstring();
    }

    std::wstring dir = homeDir;
    //主目录按约定以反斜杠结尾；调用方万一没带，这里补上，免得拼出 "BinUsers\"。
    if (dir[dir.size() - 1] != L'\\')
    {
        dir += L'\\';
    }

    dir += L"Users\\";
    dir += safeAccount;
    dir += L'\\';

    if (subDir != NULL && subDir[0] != L'\0')
    {
        dir += subDir;
        dir += L'\\';
    }

    return dir;
}

std::wstring UserConfigFilePath(const std::wstring& homeDir, const std::wstring& account)
{
    //目录部分（含空入参的判断与账号净化）完全交给 UserCacheDir，保证配置文件与各类缓存
    //落在同一个账号目录下
    const std::wstring dir = UserCacheDir(homeDir, account, NULL);
    if (dir.empty())
    {
        return std::wstring();
    }
    return dir + SanitizeAccountForPath(account) + L".ini";
}

//账号目录下登录后要事先建好的子目录。沿用原先登录流程建在程序目录下的那两个：
//ChatImage 是聊天图片缓存，UserThumb 是自定义头像目录（CUserMgr::GetCustomUserThumbFolder）。
static const wchar_t* const kEssentialSubDirs[] = { L"ChatImage", L"UserThumb" };

std::vector<std::wstring> UserEssentialDirs(const std::wstring& dataRoot, const std::wstring& account)
{
    std::vector<std::wstring> dirs;

    //空入参的判断与账号净化交给 UserCacheDir，保证与配置文件、各类缓存同一个账号目录
    const std::wstring accountDir = UserCacheDir(dataRoot, account, NULL);
    if (accountDir.empty())
    {
        return dirs;
    }

    dirs.push_back(accountDir);
    for (size_t i = 0; i < sizeof(kEssentialSubDirs) / sizeof(kEssentialSubDirs[0]); ++i)
    {
        dirs.push_back(UserCacheDir(dataRoot, account, kEssentialSubDirs[i]));
    }
    return dirs;
}

//判断某个目录是否存在。
static bool DirectoryExists(const std::wstring& dir)
{
    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

//逐级创建一个目录：CreateDirectoryW 只建最后一级，故沿路径上的每个反斜杠依次调用。
//中间层（盘符根、已存在的目录）创建失败是正常的，一律忽略，最后只看目标目录是否存在。
static bool CreateDirectoryTree(const std::wstring& dir)
{
    for (size_t pos = dir.find(L'\\', 1); pos != std::wstring::npos; pos = dir.find(L'\\', pos + 1))
    {
        ::CreateDirectoryW(dir.substr(0, pos).c_str(), NULL);
    }
    ::CreateDirectoryW(dir.c_str(), NULL);
    return DirectoryExists(dir);
}

bool EnsureUserEssentialDirs(const std::wstring& dataRoot, const std::wstring& account)
{
    const std::vector<std::wstring> dirs = UserEssentialDirs(dataRoot, account);
    if (dirs.empty())
    {
        return false;
    }

    bool allExist = true;
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        if (!CreateDirectoryTree(dirs[i]))
        {
            allExist = false;
        }
    }
    return allExist;
}

} // namespace net
