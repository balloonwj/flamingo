/**
 *  分享者一端的文件系统操作 ShareFileOps 的实现，见 ShareFileOps.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareFileOps.h"

#include <algorithm>
#include <deque>

#include "SharePathGuard.h"
#include "ShareWinPath.h"
#include "../../Source/net/Protocol.h"   //error_code / share_kind

namespace fileshare {

namespace {

//1601-01-01 到 1970-01-01 之间的 100 纳秒数（FILETIME 与 Unix 时间的起点之差）
const int64_t kFileTimeUnixEpochDiff = 116444736000000000LL;

//FILETIME 的 1 毫秒等于多少个 100 纳秒
const int64_t kFileTimeTicksPerMs = 10000;

//64 位数拆成两个 32 位字段时，高位字段左移的位数
const int kHighDwordShift = 32;

//排序用的中间结构：保留 UTF-16 名字用于比较，避免在比较函数里反复转换编码
struct ListedEntry
{
    std::wstring m_wideName;   //条目名（UTF-16）
    ShareEntry   m_entry;      //回报给访问者的条目
};

//先目录后文件，同类按名字不区分大小写的序数比较；名字只差大小写时再按区分大小写比较，保证结果确定
struct ListedEntryLess
{
    bool operator()(const ListedEntry& a, const ListedEntry& b) const
    {
        if (a.m_entry.m_isDir != b.m_entry.m_isDir)
            return a.m_entry.m_isDir;
        const int cmp = ::CompareStringOrdinal(a.m_wideName.c_str(), static_cast<int>(a.m_wideName.size()),
                                               b.m_wideName.c_str(), static_cast<int>(b.m_wideName.size()), TRUE);
        if (cmp != CSTR_EQUAL)
            return cmp == CSTR_LESS_THAN;
        return a.m_wideName < b.m_wideName;
    }
};

//去掉结尾的反斜杠；盘符根目录（"X:\"）保留
std::wstring TrimTrailingSlash(const std::wstring& path)
{
    std::wstring s = path;
    while (s.size() > 3 && s[s.size() - 1] == L'\\')
        s.erase(s.size() - 1);
    return s;
}

//由属性与大小、时间字段填一个条目
void FillEntry(const std::wstring& name, DWORD attrs, DWORD sizeHigh, DWORD sizeLow, const FILETIME& mtime,
               ShareEntry& e)
{
    e.m_name = ShareWideToUtf8(name);
    e.m_isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e.m_size = e.m_isDir ? 0 : static_cast<int64_t>((static_cast<uint64_t>(sizeHigh) << kHighDwordShift) | sizeLow);
    e.m_mtime = ShareFileOps::FileTimeToUnixMs(mtime);
    e.m_attrs = static_cast<int>(attrs & kShareEntryAttrMask);
}

} // namespace

//==================== CShareUploadRegistry ====================

CShareUploadRegistry& CShareUploadRegistry::Instance()
{
    static CShareUploadRegistry s_instance;
    return s_instance;
}

void CShareUploadRegistry::Add(const std::wstring& partPath)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_paths.insert(Key(partPath));
}

void CShareUploadRegistry::Remove(const std::wstring& partPath)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_paths.erase(Key(partPath));
}

bool CShareUploadRegistry::Contains(const std::wstring& fullPath) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_paths.find(Key(fullPath)) != m_paths.end();
}

bool CShareUploadRegistry::HasAnyUnder(const std::wstring& dir) const
{
    std::wstring prefix = Key(dir);
    if (prefix.empty())
        return false;
    if (prefix[prefix.size() - 1] != L'\\')
        prefix += L'\\';
    std::lock_guard<std::mutex> guard(m_mutex);
    //集合按字典序排列：从第一个不小于前缀的键看起，一个是否以前缀开头即可判定
    std::set<std::wstring>::const_iterator it = m_paths.lower_bound(prefix);
    return it != m_paths.end() && it->compare(0, prefix.size(), prefix) == 0;
}

std::wstring CShareUploadRegistry::Key(const std::wstring& path)
{
    std::wstring s = path;
    if (!s.empty())
        ::CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

int ShareFileOps::ListDirectory(const std::wstring& absDir, bool includeHidden, std::vector<ShareEntry>& out)
{
    out.clear();
    const std::wstring dir = TrimTrailingSlash(absDir);
    const DWORD dirAttrs = ::GetFileAttributesW(ShareToLongPath(dir).c_str());
    if (dirAttrs == INVALID_FILE_ATTRIBUTES)
        return MapWin32Error(::GetLastError());
    if ((dirAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return error_code_share_path_not_found;

    //盘符根目录已带反斜杠，其余补一个
    std::wstring pattern = dir;
    if (pattern[pattern.size() - 1] != L'\\')
        pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileExW(ShareToLongPath(pattern).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL,
                                  FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
    {
        //空的盘符根目录没有 . 与 ..，FindFirstFile 直接报「找不到文件」，按空目录处理
        const DWORD err = ::GetLastError();
        return (err == ERROR_FILE_NOT_FOUND) ? error_code_ok : MapWin32Error(err);
    }

    std::vector<ListedEntry> listed;
    do
    {
        const std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..")
            continue;
        if (!includeHidden && (fd.dwFileAttributes & kShareHiddenAttrMask) != 0)
            continue;
        //正在上传的半成品：不让其他访问者看到
        if (CShareUploadRegistry::Instance().Contains(pattern.substr(0, pattern.size() - 1) + name))
            continue;

        ListedEntry item;
        item.m_wideName = name;
        FillEntry(name, fd.dwFileAttributes, fd.nFileSizeHigh, fd.nFileSizeLow, fd.ftLastWriteTime, item.m_entry);
        //访问者无法用这样的名字再发请求，不列出
        if (!SharePathGuard::IsValidRelativePath(item.m_entry.m_name, false))
            continue;
        listed.push_back(item);
    } while (::FindNextFileW(h, &fd));

    const DWORD lastErr = ::GetLastError();
    ::FindClose(h);
    if (lastErr != ERROR_NO_MORE_FILES)
        return MapWin32Error(lastErr);

    std::sort(listed.begin(), listed.end(), ListedEntryLess());
    out.reserve(listed.size());
    for (size_t i = 0; i < listed.size(); ++i)
        out.push_back(listed[i].m_entry);
    return error_code_ok;
}

int ShareFileOps::StatEntry(const std::wstring& absPath, ShareEntry& out)
{
    out = ShareEntry();
    const std::wstring path = TrimTrailingSlash(absPath);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(ShareToLongPath(path).c_str(), GetFileExInfoStandard, &data))
        return MapWin32Error(::GetLastError());

    //盘符根目录取 "X:"，其余取最后一段
    std::wstring name;
    if (path.size() == 3 && path[1] == L':')
    {
        name = path.substr(0, 2);
    }
    else
    {
        const size_t pos = path.find_last_of(L'\\');
        name = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
    }
    FillEntry(name, data.dwFileAttributes, data.nFileSizeHigh, data.nFileSizeLow, data.ftLastWriteTime, out);
    return error_code_ok;
}

bool ShareFileOps::HasHiddenComponent(const std::wstring& root, const std::wstring& absPath)
{
    const std::wstring base = TrimTrailingSlash(root);
    const std::wstring target = TrimTrailingSlash(absPath);
    if (!SharePathGuard::IsUnderRoot(base, target) || target.size() <= base.size())
        return false;

    //从共享根之后的第一段开始逐级检查；根本身以反斜杠结尾（盘符根目录）时第一段紧接其后
    size_t pos = base.size();
    if (target[pos] == L'\\')
        ++pos;
    while (pos <= target.size())
    {
        size_t next = target.find(L'\\', pos);
        if (next == std::wstring::npos)
            next = target.size();
        const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(target.substr(0, next)).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return false;
        if ((attrs & kShareHiddenAttrMask) != 0)
            return true;
        pos = next + 1;
    }
    return false;
}

int ShareFileOps::ScanTree(const std::wstring& absDir, const std::string& prefix, bool includeHidden,
                           std::vector<std::string>& dirs, std::vector<foldertransfer::FolderManifestFile>& files)
{
    //待扫描的目录：本机路径、相对路径、层数
    struct PendingDir
    {
        std::wstring m_abs;     //目录的本机绝对路径
        std::string  m_rel;     //目录在清单里的相对路径
        int          m_depth;   //相对起点的层数，起点为 0
    };

    const DWORD rootAttrs = ::GetFileAttributesW(ShareToLongPath(TrimTrailingSlash(absDir)).c_str());
    if (rootAttrs == INVALID_FILE_ATTRIBUTES)
        return MapWin32Error(::GetLastError());
    if ((rootAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0 || prefix.empty())
        return error_code_share_path_not_found;

    std::deque<PendingDir> queue;
    PendingDir first;
    first.m_abs = TrimTrailingSlash(absDir);
    first.m_rel = prefix;
    first.m_depth = 0;
    queue.push_back(first);

    while (!queue.empty())
    {
        const PendingDir current = queue.front();
        queue.pop_front();
        if (current.m_depth > kShareScanMaxDepth)
            return error_code_share_io_fail;

        std::wstring pattern = current.m_abs;
        if (pattern[pattern.size() - 1] != L'\\')
            pattern += L'\\';
        pattern += L'*';
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileExW(ShareToLongPath(pattern).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                      NULL, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE)
        {
            const DWORD err = ::GetLastError();
            if (err == ERROR_FILE_NOT_FOUND)
                continue;
            return MapWin32Error(err);
        }

        do
        {
            const std::wstring name(fd.cFileName);
            if (name == L"." || name == L"..")
                continue;
            //重解析点不跟进，也不传：它可能指向共享根之外，或绕回上级形成环
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                continue;
            if (!includeHidden && (fd.dwFileAttributes & kShareHiddenAttrMask) != 0)
                continue;
            const std::string utf8Name = ShareWideToUtf8(name);
            if (!SharePathGuard::IsValidRelativePath(utf8Name, false))
                continue;

            const std::string rel = current.m_rel + "/" + utf8Name;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                dirs.push_back(rel);
                PendingDir next;
                next.m_abs = current.m_abs + L"\\" + name;
                next.m_rel = rel;
                next.m_depth = current.m_depth + 1;
                queue.push_back(next);
            }
            else
            {
                foldertransfer::FolderManifestFile file;
                file.m_relativePath = rel;
                file.m_sizeBytes = static_cast<int64_t>((static_cast<uint64_t>(fd.nFileSizeHigh) << kHighDwordShift)
                                                        | fd.nFileSizeLow);
                file.m_mtimeMs = FileTimeToUnixMs(fd.ftLastWriteTime);
                files.push_back(file);
            }
        } while (::FindNextFileW(h, &fd));
        const DWORD lastErr = ::GetLastError();
        ::FindClose(h);
        if (lastErr != ERROR_NO_MORE_FILES)
            return MapWin32Error(lastErr);
    }
    return error_code_ok;
}

bool ShareFileOps::IsRootAvailable(const std::wstring& rootPath, int kind)
{
    if (rootPath.empty())
        return false;
    const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(TrimTrailingSlash(rootPath)).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;
    const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return (kind == share_kind_file) ? !isDir : isDir;
}

int64_t ShareFileOps::FileTimeToUnixMs(const FILETIME& ft)
{
    const int64_t ticks = static_cast<int64_t>((static_cast<uint64_t>(ft.dwHighDateTime) << kHighDwordShift)
                                               | ft.dwLowDateTime);
    if (ticks <= kFileTimeUnixEpochDiff)
        return 0;
    return (ticks - kFileTimeUnixEpochDiff) / kFileTimeTicksPerMs;
}

int ShareFileOps::MapWin32Error(DWORD err)
{
    switch (err)
    {
    //文件、路径、网络路径不存在，名字非法，驱动器未就绪（读卡器里没有卡）、路径指向的不是目录：都按不存在处理
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_BAD_NETPATH:
    case ERROR_INVALID_NAME:
    case ERROR_NOT_READY:
    case ERROR_DIRECTORY:
        return error_code_share_path_not_found;
    //其余（拒绝访问、共享冲突、设备错误等）：读写失败
    default:
        return error_code_share_io_fail;
    }
}

} // namespace fileshare
