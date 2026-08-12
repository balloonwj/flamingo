/**
 *  「清理本地缓存」磁盘操作层的实现，CacheCleanExecutor.cpp
 *  职责边界与失败处理见 CacheCleanExecutor.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h，工程里标 PrecompiledHeader = NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "CacheCleanExecutor.h"
#include "CacheCleanPlan.h"      // IsSafeToDeleteCacheDir（删除前的边界检查）

#include <windows.h>
#include <tchar.h>

#include "../IULog.h"

namespace balloonsettings {

namespace {

//给目录补上结尾反斜杠，便于往下拼子项。空串原样返回。
std::wstring EnsureSep(const std::wstring& dir)
{
    if (dir.empty() || dir[dir.size() - 1] == L'\\')
    {
        return dir;
    }
    return dir + L'\\';
}

//跳过 FindFirstFile 一定会枚举出来的 "." 与 ".." 两个伪条目。
bool IsDotEntry(const wchar_t* name)
{
    if (name == NULL)
    {
        return true;
    }
    if (name[0] == L'.' && name[1] == L'\0')
    {
        return true;
    }
    if (name[0] == L'.' && name[1] == L'.' && name[2] == L'\0')
    {
        return true;
    }
    return false;
}

} // anonymous namespace

unsigned long long ScanDirSizeBytes(const std::wstring& dir)
{
    if (dir.empty())
    {
        return 0;
    }

    const std::wstring base = EnsureSep(dir);
    WIN32_FIND_DATAW fd;
    ::ZeroMemory(&fd, sizeof(fd));

    HANDLE hFind = ::FindFirstFileW((base + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        //目录不存在（清过一次之后就是这样）属于常态，不记日志、不报错。
        return 0;
    }

    unsigned long long total = 0;
    do
    {
        if (IsDotEntry(fd.cFileName))
        {
            continue;
        }

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            //重解析点（联接 / 符号链接）不跟进：跟进去会把链接指向的别处也算进来，
            //甚至绕成环。缓存目录本不该有这种东西，防的是有人手工造过。
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                continue;
            }
            total += ScanDirSizeBytes(base + fd.cFileName);
        }
        else
        {
            //文件大小是 64 位，分高低两半给的，拼回去。
            ULARGE_INTEGER size;
            size.HighPart = fd.nFileSizeHigh;
            size.LowPart  = fd.nFileSizeLow;
            total += size.QuadPart;
        }
    } while (::FindNextFileW(hFind, &fd) != 0);

    ::FindClose(hFind);
    return total;
}

bool DeleteDirTree(const std::wstring& dir, const std::wstring& dataRoot)
{
    //删除不可恢复，先过边界检查。计划是本模块自己算的、本该安全，但真正动手前
    //再核一遍，将来若有别处传路径进来也拦得住。
    if (!IsSafeToDeleteCacheDir(dir, dataRoot))
    {
        LOG_ERROR(_T("清理缓存：目录未通过安全检查，已拒绝删除：%s"), dir.c_str());
        return false;
    }

    const std::wstring base = EnsureSep(dir);
    WIN32_FIND_DATAW fd;
    ::ZeroMemory(&fd, sizeof(fd));

    HANDLE hFind = ::FindFirstFileW((base + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        //目录本来就不存在：视为已经是干净的，不算失败。
        return true;
    }

    bool allDeleted = true;
    do
    {
        if (IsDotEntry(fd.cFileName))
        {
            continue;
        }

        const std::wstring full = base + fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            //重解析点只删链接本身，不跟进去删目标 —— 跟进去可能删到链接指向的别处。
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                if (!::RemoveDirectoryW(full.c_str()))
                {
                    allDeleted = false;
                }
                continue;
            }
            if (!DeleteDirTree(full, dataRoot))
            {
                allDeleted = false;
            }
        }
        else
        {
            //只读属性会让 DeleteFile 失败，先摘掉再删。被占用（典型是正在写入的
            //下载文件）时跳过它继续删其余的，最后如实报告"没删干净"。
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
            {
                ::SetFileAttributesW(full.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            }
            if (!::DeleteFileW(full.c_str()))
            {
                LOG_ERROR(_T("清理缓存：删除文件失败（错误码 %u）：%s"),
                          ::GetLastError(), full.c_str());
                allDeleted = false;
            }
        }
    } while (::FindNextFileW(hFind, &fd) != 0);

    ::FindClose(hFind);

    //子项都清掉之后再删目录本身。有文件没删掉时它必然非空，此处的失败是预期内的，
    //不再重复记一条日志。
    if (allDeleted && !::RemoveDirectoryW(base.c_str()))
    {
        LOG_ERROR(_T("清理缓存：删除目录失败（错误码 %u）：%s"),
                  ::GetLastError(), base.c_str());
        allDeleted = false;
    }
    return allDeleted;
}

} // namespace balloonsettings
