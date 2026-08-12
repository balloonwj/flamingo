/**
 *  按登录账号隔离的本地缓存目录口径实现，UserCachePath.cpp
 *
 *  纯逻辑，只依赖 STL，不碰磁盘、不读全局状态。与 FlamingoUnit 测试工程共用，
 *  因此不 include stdafx.h，在 Flamingo.vcxproj 里 PCH 须设 NotUsing。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#include "UserCachePath.h"

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

} // namespace net
