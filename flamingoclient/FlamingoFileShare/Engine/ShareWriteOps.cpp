/**
 *  分享者一端写操作 ShareWriteOps 的实现，见 ShareWriteOps.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareWriteOps.h"

#include "ShareFileOps.h"
#include "SharePathGuard.h"
#include "ShareWinPath.h"
#include "../../Source/net/Protocol.h"   //error_code / share_kind

namespace fileshare {

namespace {

//相对路径的分隔符
const char kRelSep = '/';

//条目是否存在；isDir 返回它是不是目录
bool Exists(const std::wstring& absPath, bool& isDir)
{
    const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(absPath).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;
    isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

//把建目录、改名、移动失败时的系统错误归为错误码
int MapWin32Error(DWORD err)
{
    switch (err)
    {
    //目标位置已有同名条目
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return error_code_share_name_conflict;
    //源条目或上级目录不存在
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return error_code_share_path_not_found;
    //分享者电脑的磁盘已满
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return error_code_share_disk_full;
    //名字或路径不被文件系统接受（如超过文件系统的长度上限）
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_FILENAME_EXCED_RANGE:
        return error_code_share_path_invalid;
    //其余：被占用、无权限、跨卷移动等
    default:
        return error_code_share_io_fail;
    }
}

//递归累加目录下全部文件的字节数；depth 为当前层数
bool SumDirectory(const std::wstring& dir, int depth, int64_t& bytes)
{
    if (depth > kShareWriteSizeMaxDepth)
        return false;
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = ShareToLongPath(dir + L"\\*");
    HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
    {
        //空目录在某些文件系统上没有任何条目
        return ::GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    bool ok = true;
    do
    {
        const std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..")
            continue;
        //重解析点（目录联接、符号链接）只是一个链接，放进回收站时不会连带它指向的内容
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            continue;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if (!SumDirectory(dir + L"\\" + name, depth + 1, bytes))
            {
                ok = false;
                break;
            }
            continue;
        }
        bytes += (static_cast<int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return ok;
}

//单文件共享不支持写操作；其余类型返回 error_code_ok
int CheckKind(const ShareWriteContext& ctx)
{
    return (ctx.m_kind == share_kind_file) ? error_code_share_kind_mismatch : error_code_ok;
}

//条目所在目录之下有正在上传的半成品，或条目本身就是半成品：重命名、移动、删除它都会破坏那次上传
bool BusyWithUpload(const std::wstring& absPath)
{
    const CShareUploadRegistry& registry = CShareUploadRegistry::Instance();
    return registry.Contains(absPath) || registry.HasAnyUnder(absPath);
}

//在 results 末尾追加一条结果
void AddResult(std::vector<ShareOpItem>& results, const std::string& path, int code)
{
    ShareOpItem item;
    item.m_path = path;
    item.m_code = code;
    results.push_back(item);
}

//逐条结果里第一条失败的错误码；全部成功返回 error_code_ok
int FirstFailure(const std::vector<ShareOpItem>& results)
{
    for (size_t i = 0; i < results.size(); ++i)
    {
        if (results[i].m_code != error_code_ok)
            return results[i].m_code;
    }
    return error_code_ok;
}

} // namespace

int ShareWriteOps::ResolvePath(const ShareWriteContext& ctx, const std::string& relPath, std::wstring& absPath)
{
    const bool singleFile = (ctx.m_kind == share_kind_file);
    if (!SharePathGuard::ResolveUnderRoot(ctx.m_rootPath, relPath, singleFile, absPath))
        return error_code_share_path_invalid;
    if (!ShareFileOps::IsRootAvailable(ctx.m_rootPath, ctx.m_kind))
        return error_code_share_path_not_found;
    if (!SharePathGuard::IsReparseSafe(ctx.m_rootPath, absPath))
        return error_code_share_path_invalid;
    if (!ctx.m_includeHidden && ShareFileOps::HasHiddenComponent(ctx.m_rootPath, absPath))
        return error_code_share_path_not_found;
    //正在上传的半成品对访问者不可见
    if (CShareUploadRegistry::Instance().Contains(absPath))
        return error_code_share_path_not_found;
    return error_code_ok;
}

int ShareWriteOps::MakeDirectory(const ShareWriteContext& ctx, const std::string& relPath)
{
    int code = CheckKind(ctx);
    if (code != error_code_ok)
        return code;
    if (relPath.empty())
        return error_code_share_path_invalid;

    std::wstring parentAbs;
    code = ResolvePath(ctx, ParentOf(relPath), parentAbs);
    if (code != error_code_ok)
        return code;
    bool isDir = false;
    if (!Exists(parentAbs, isDir) || !isDir)
        return error_code_share_path_not_found;

    std::wstring absPath;
    code = ResolvePath(ctx, relPath, absPath);
    if (code != error_code_ok)
        return code;
    //同名的文件或文件夹（含隐藏的）已存在：一律不覆盖
    if (Exists(absPath, isDir))
        return error_code_share_name_conflict;
    if (!::CreateDirectoryW(ShareToLongPath(absPath).c_str(), NULL))
        return MapWin32Error(::GetLastError());
    return error_code_ok;
}

int ShareWriteOps::Rename(const ShareWriteContext& ctx, const std::string& relPath, const std::string& newName,
                          std::string& newRelPath)
{
    newRelPath.clear();
    int code = CheckKind(ctx);
    if (code != error_code_ok)
        return code;
    //共享根本身不能改名；新名字只能是一段
    if (relPath.empty() || newName.empty() || newName.find(kRelSep) != std::string::npos
        || newName.find('\\') != std::string::npos)
        return error_code_share_path_invalid;

    std::wstring srcAbs;
    code = ResolvePath(ctx, relPath, srcAbs);
    if (code != error_code_ok)
        return code;
    bool isDir = false;
    if (!Exists(srcAbs, isDir))
        return error_code_share_path_not_found;

    const std::string targetRel = Join(ParentOf(relPath), newName);
    std::wstring dstAbs;
    code = ResolvePath(ctx, targetRel, dstAbs);
    if (code != error_code_ok)
        return code;

    const std::string oldName = LastSegment(relPath);
    if (oldName == newName)
    {
        newRelPath = relPath;
        return error_code_ok;
    }
    //只改大小写时目标就是源本身，不算同名冲突
    bool targetIsDir = false;
    if (!EqualsIgnoreCase(oldName, newName) && Exists(dstAbs, targetIsDir))
        return error_code_share_name_conflict;
    if (BusyWithUpload(srcAbs))
        return error_code_share_io_fail;
    if (!::MoveFileExW(ShareToLongPath(srcAbs).c_str(), ShareToLongPath(dstAbs).c_str(), 0))
        return MapWin32Error(::GetLastError());
    newRelPath = targetRel;
    return error_code_ok;
}

int ShareWriteOps::Delete(const ShareWriteContext& ctx, const std::vector<std::string>& relPaths,
                          std::vector<ShareOpItem>& results)
{
    results.clear();
    const int kindCode = CheckKind(ctx);

    //第一遍：逐条校验，记下能删的条目与它们的字节数
    std::vector<size_t> candidates;
    std::vector<std::wstring> absPaths(relPaths.size());
    int64_t totalBytes = 0;
    bool sizeKnown = true;
    for (size_t i = 0; i < relPaths.size(); ++i)
    {
        int code = kindCode;
        //共享根本身不能删
        if (code == error_code_ok && relPaths[i].empty())
            code = error_code_share_path_invalid;
        if (code == error_code_ok)
            code = ResolvePath(ctx, relPaths[i], absPaths[i]);
        bool isDir = false;
        if (code == error_code_ok && !Exists(absPaths[i], isDir))
            code = error_code_share_path_not_found;
        if (code == error_code_ok && BusyWithUpload(absPaths[i]))
            code = error_code_share_io_fail;
        if (code == error_code_ok)
        {
            int64_t bytes = 0;
            if (EntryBytes(absPaths[i], bytes))
                totalBytes += bytes;
            else
                sizeKnown = false;
            candidates.push_back(i);
        }
        AddResult(results, relPaths[i], code);
    }
    if (candidates.empty())
        return FirstFailure(results);

    //第二步：按总字节数判定能否放进回收站；不能就一个也不删
    int recycleCode = error_code_share_recycle_unavailable;
    if (ctx.m_recycler != NULL && sizeKnown)
        recycleCode = ctx.m_recycler->CheckRecyclable(absPaths[candidates[0]], totalBytes);
    for (size_t k = 0; k < candidates.size(); ++k)
    {
        const size_t i = candidates[k];
        results[i].m_code = (recycleCode == error_code_ok) ? ctx.m_recycler->Recycle(absPaths[i]) : recycleCode;
    }
    return FirstFailure(results);
}

int ShareWriteOps::Move(const ShareWriteContext& ctx, const std::vector<std::string>& relPaths, const std::string& toDir,
                        std::vector<ShareOpItem>& results)
{
    results.clear();
    int dirCode = CheckKind(ctx);
    std::wstring dirAbs;
    if (dirCode == error_code_ok)
        dirCode = ResolvePath(ctx, toDir, dirAbs);
    bool isDir = false;
    if (dirCode == error_code_ok && (!Exists(dirAbs, isDir) || !isDir))
        dirCode = error_code_share_path_not_found;

    for (size_t i = 0; i < relPaths.size(); ++i)
    {
        const std::string& rel = relPaths[i];
        int code = dirCode;
        //共享根本身不能移动；也不能移到自己或自己的下级里
        if (code == error_code_ok && (rel.empty() || IsSameOrUnder(toDir, rel)))
            code = error_code_share_path_invalid;
        std::wstring srcAbs;
        if (code == error_code_ok)
            code = ResolvePath(ctx, rel, srcAbs);
        bool srcIsDir = false;
        if (code == error_code_ok && !Exists(srcAbs, srcIsDir))
            code = error_code_share_path_not_found;
        //已经在目标目录里：什么也不做，按成功处理
        if (code == error_code_ok && EqualsIgnoreCase(ParentOf(rel), toDir))
        {
            AddResult(results, rel, error_code_ok);
            continue;
        }
        std::wstring dstAbs;
        if (code == error_code_ok)
            code = ResolvePath(ctx, Join(toDir, LastSegment(rel)), dstAbs);
        bool dstIsDir = false;
        if (code == error_code_ok && Exists(dstAbs, dstIsDir))
            code = error_code_share_name_conflict;
        if (code == error_code_ok && BusyWithUpload(srcAbs))
            code = error_code_share_io_fail;
        //不带 MOVEFILE_COPY_ALLOWED：只在同一个卷里改目录项，不做复制再删除
        if (code == error_code_ok && !::MoveFileExW(ShareToLongPath(srcAbs).c_str(), ShareToLongPath(dstAbs).c_str(), 0))
            code = MapWin32Error(::GetLastError());
        AddResult(results, rel, code);
    }
    return FirstFailure(results);
}

bool ShareWriteOps::EntryBytes(const std::wstring& absPath, int64_t& bytes)
{
    bytes = 0;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(ShareToLongPath(absPath).c_str(), GetFileExInfoStandard, &data))
        return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        bytes = (static_cast<int64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        return true;
    }
    //目录本身是重解析点时只是一个链接
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return true;
    return SumDirectory(absPath, 1, bytes);
}

std::string ShareWriteOps::ParentOf(const std::string& relPath)
{
    const size_t pos = relPath.rfind(kRelSep);
    return (pos == std::string::npos) ? std::string() : relPath.substr(0, pos);
}

std::string ShareWriteOps::LastSegment(const std::string& relPath)
{
    const size_t pos = relPath.rfind(kRelSep);
    return (pos == std::string::npos) ? relPath : relPath.substr(pos + 1);
}

std::string ShareWriteOps::Join(const std::string& dir, const std::string& name)
{
    return dir.empty() ? name : dir + kRelSep + name;
}

bool ShareWriteOps::IsSameOrUnder(const std::string& relPath, const std::string& dir)
{
    if (dir.empty())
        return true;
    if (relPath.size() < dir.size())
        return false;
    if (!EqualsIgnoreCase(relPath.substr(0, dir.size()), dir))
        return false;
    //恰好相等，或下一个字符是分隔符（"ab" 不算 "a" 的下级）
    return relPath.size() == dir.size() || relPath[dir.size()] == kRelSep;
}

bool ShareWriteOps::EqualsIgnoreCase(const std::string& a, const std::string& b)
{
    std::wstring wa;
    std::wstring wb;
    ShareUtf8ToWide(a, wa);
    ShareUtf8ToWide(b, wb);
    if (wa.empty() || wb.empty())
        return wa.empty() && wb.empty();
    return ::CompareStringOrdinal(wa.c_str(), static_cast<int>(wa.size()), wb.c_str(), static_cast<int>(wb.size()), TRUE)
           == CSTR_EQUAL;
}

} // namespace fileshare
