/**
 *  传输子会话的清单与落点规划 ShareTransferPlan 的实现，见 ShareTransferPlan.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareTransferPlan.h"

#include <windows.h>
#include <set>

#include "ShareBrowseModel.h"
#include "ShareFileOps.h"
#include "SharePathGuard.h"
#include "ShareWinPath.h"
#include "../../Source/net/Protocol.h"

namespace fileshare {

namespace {

//候选名里序号的缓冲区字符数
const size_t kSuffixBufChars = 16;

//去掉结尾的反斜杠；盘符根目录（"X:\"）保留
std::wstring TrimSlash(const std::wstring& path)
{
    std::wstring s = path;
    while (s.size() > 3 && s[s.size() - 1] == L'\\')
        s.erase(s.size() - 1);
    return s;
}

//本机路径的上一级目录与最后一段
void SplitLocalPath(const std::wstring& path, std::wstring& parent, std::wstring& leaf)
{
    const std::wstring p = TrimSlash(path);
    const size_t pos = p.find_last_of(L'\\');
    if (pos == std::wstring::npos)
    {
        parent.clear();
        leaf = p;
        return;
    }
    parent = (pos == 2 && p.size() > 1 && p[1] == L':') ? p.substr(0, pos + 1) : p.substr(0, pos);
    leaf = p.substr(pos + 1);
}

//把一个已存在的条目（文件或目录）加进清单；name 为它在清单里的名字（顶层）
int AddItem(const std::wstring& absPath, const std::string& name, bool includeHidden,
            foldertransfer::FolderManifest& manifest)
{
    ShareEntry entry;
    const int code = ShareFileOps::StatEntry(absPath, entry);
    if (code != error_code_ok)
        return code;
    if (!entry.m_isDir)
    {
        foldertransfer::FolderManifestFile file;
        file.m_relativePath = name;
        file.m_sizeBytes = entry.m_size;
        file.m_mtimeMs = entry.m_mtime;
        manifest.m_files.push_back(file);
        return error_code_ok;
    }
    manifest.m_dirs.push_back(name);
    return ShareFileOps::ScanTree(absPath, name, includeHidden, manifest.m_dirs, manifest.m_files);
}

} // namespace

bool CShareDiskProbe::Exists(const std::wstring& fullPath)
{
    return ::GetFileAttributesW(ShareToLongPath(TrimSlash(fullPath)).c_str()) != INVALID_FILE_ATTRIBUTES;
}

int ShareTransferPlan::BuildDownloadManifest(const std::wstring& shareRoot, int kind, bool includeHidden,
                                             const std::vector<std::string>& items,
                                             foldertransfer::FolderManifest& manifest, std::wstring& sourceRoot)
{
    manifest = foldertransfer::FolderManifest();
    sourceRoot.clear();
    if (items.empty())
        return error_code_share_path_invalid;

    //单文件共享：唯一的条目是共享的那个文件本身
    if (kind == share_kind_file)
    {
        if (items.size() != 1 || !items[0].empty())
            return error_code_share_kind_mismatch;
        if (!ShareFileOps::IsRootAvailable(shareRoot, kind))
            return error_code_share_path_not_found;
        std::wstring leaf;
        SplitLocalPath(shareRoot, sourceRoot, leaf);
        return AddItem(TrimSlash(shareRoot), ShareWideToUtf8(leaf), includeHidden, manifest);
    }

    //全部条目必须在同一个目录下：清单不带根文件夹，读源文件时以这个目录为根
    const std::string parent = ShareBrowseModel::ParentPath(items[0]);
    std::set<std::string> seen;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].empty() || ShareBrowseModel::ParentPath(items[i]) != parent)
            return error_code_share_path_invalid;
        if (!seen.insert(items[i]).second)
            continue;

        std::wstring abs;
        if (!SharePathGuard::ResolveUnderRoot(shareRoot, items[i], false, abs) || !SharePathGuard::IsReparseSafe(shareRoot, abs))
            return error_code_share_path_invalid;
        if (!includeHidden && ShareFileOps::HasHiddenComponent(shareRoot, abs))
            return error_code_share_path_not_found;

        const std::vector<std::string> parts = ShareBrowseModel::SplitPath(items[i]);
        const int code = AddItem(abs, parts.back(), includeHidden, manifest);
        if (code != error_code_ok)
            return code;
    }

    if (!SharePathGuard::ResolveUnderRoot(shareRoot, parent, false, sourceRoot))
        return error_code_share_path_invalid;
    return error_code_ok;
}

int ShareTransferPlan::BuildUploadManifest(const std::vector<std::wstring>& localItems,
                                           foldertransfer::FolderManifest& manifest, std::wstring& sourceRoot)
{
    manifest = foldertransfer::FolderManifest();
    sourceRoot.clear();
    if (localItems.empty())
        return error_code_share_path_invalid;

    std::set<std::wstring> seen;
    for (size_t i = 0; i < localItems.size(); ++i)
    {
        std::wstring parent;
        std::wstring leaf;
        SplitLocalPath(localItems[i], parent, leaf);
        if (leaf.empty() || parent.empty())
            return error_code_share_path_invalid;
        if (i == 0)
            sourceRoot = parent;
        else if (::CompareStringOrdinal(parent.c_str(), -1, sourceRoot.c_str(), -1, TRUE) != CSTR_EQUAL)
            return error_code_share_path_invalid;
        if (!seen.insert(leaf).second)
            continue;

        const std::string name = ShareWideToUtf8(leaf);
        if (!SharePathGuard::IsValidRelativePath(name, false))
            return error_code_share_path_invalid;
        const int code = AddItem(TrimSlash(localItems[i]), name, true, manifest);
        if (code != error_code_ok)
            return code;
    }
    return error_code_ok;
}

std::vector<std::string> ShareTransferPlan::TopLevelNames(const std::vector<std::string>& dirs,
                                                          const std::vector<foldertransfer::FolderRelayFileEntry>& files)
{
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (size_t i = 0; i < dirs.size() + files.size(); ++i)
    {
        const std::string& path = (i < dirs.size()) ? dirs[i] : files[i - dirs.size()].m_relpath;
        const std::string top = path.substr(0, path.find('/'));
        if (!top.empty() && seen.insert(top).second)
            names.push_back(top);
    }
    return names;
}

std::string ShareTransferPlan::ReplaceTopLevel(const std::string& relPath, const std::string& from, const std::string& to)
{
    const size_t slash = relPath.find('/');
    const std::string top = relPath.substr(0, slash);
    if (top != from)
        return relPath;
    return (slash == std::string::npos) ? to : to + relPath.substr(slash);
}

bool ShareTransferPlan::ChooseFreeName(const std::wstring& dir, const std::wstring& name, bool isDir,
                                       IShareNameProbe& probe, std::wstring& out)
{
    std::wstring base = name;
    std::wstring ext;
    //文件按最后一个点拆出扩展名（点在开头的隐藏文件名如 ".profile" 不拆）
    if (!isDir)
    {
        const size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0)
        {
            base = name.substr(0, dot);
            ext = name.substr(dot);
        }
    }

    const std::wstring prefix = (dir.empty() || dir[dir.size() - 1] == L'\\') ? dir : dir + L"\\";
    for (int attempt = 1; attempt <= kShareRenameMaxAttempts; ++attempt)
    {
        std::wstring candidate = name;
        if (attempt > 1)
        {
            //第 2 次尝试用序号 kShareRenameFirstSuffix，之后每次加 1
            const int number = kShareRenameFirstSuffix + (attempt - 2);
            wchar_t suffix[kSuffixBufChars] = { 0 };
            _snwprintf_s(suffix, kSuffixBufChars, _TRUNCATE, L"(%d)", number);
            candidate = base + suffix + ext;
        }
        if (!probe.Exists(prefix + candidate))
        {
            out = candidate;
            return true;
        }
    }
    return false;
}

std::string ShareTransferPlan::FindTopLevelConflict(const std::wstring& targetDir, const std::vector<std::string>& topNames,
                                                    IShareNameProbe& probe)
{
    const std::wstring prefix = (targetDir.empty() || targetDir[targetDir.size() - 1] == L'\\') ? targetDir
                                                                                                  : targetDir + L"\\";
    for (size_t i = 0; i < topNames.size(); ++i)
    {
        std::wstring wide;
        if (!ShareUtf8ToWide(topNames[i], wide) || probe.Exists(prefix + wide))
            return topNames[i];
    }
    return std::string();
}

} // namespace fileshare
