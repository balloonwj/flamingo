/**
 *  升级包落盘安装的实现，Installer.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "Installer.h"

#include "UpdateLog.h"
#include "ZipInflate.h"
#include "ZipPathSafety.h"
#include "ZipReader.h"
#include "../Source/Common/I18n/I18n.h"   // TR / TRF：给用户看的错误原因的多语言查找

namespace iupdate {

namespace {

//升级包大小上限（字节）。整包一次性读进内存，得有个上限挡住异常大的文件，
//免得内存直接被撑爆。解压时还要再为单个条目分配一份内存，所以内存峰值约为「包体 + 最大条目解压后的大小」。
//2026-10-08 按 Debug|x64 产物估算：带上全部 exe、DLL 与资源的完整包压缩后约 152 MiB，其中字幕模型
//FlamingoCaptionModel.dll 一个就占约 124 MiB（解压后约 162 MiB，模型数据几乎不能压缩）。模型没有变化时
//可以不放进包里（见 docs/客户端自动更新方案.md 7.1），包体约 28 MiB。
const long long kMaxPackageBytes = 500LL * 1024 * 1024;

//备份目录相对安装目录的位置。
const wchar_t* const kBackupSubDir = L"update\\backup";

//路径拼接：把相对路径接到目录后面，自动补分隔符。
std::wstring JoinPath(const std::wstring& dir, const std::wstring& rel)
{
    if (dir.empty())
    {
        return rel;
    }
    std::wstring result = dir;
    const wchar_t last = result[result.size() - 1];
    if (last != L'\\' && last != L'/')
    {
        result += L'\\';
    }
    result += rel;
    return result;
}

//取一个路径的父目录；没有分隔符时返回空串。
std::wstring ParentDirOf(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return std::wstring();
    }
    return path.substr(0, pos);
}

//文件是否存在（且不是目录）。
bool FileExists(const std::wstring& path)
{
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

//递归删除一个目录及其内容。用于每次安装前清掉上一次遗留的备份。
//失败只返回 false，由调用方决定要不要在意。
bool RemoveDirectoryRecursive(const std::wstring& dir)
{
    std::wstring pattern = JoinPath(dir, L"*");

    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..")
            {
                continue;
            }
            const std::wstring child = JoinPath(dir, name);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                RemoveDirectoryRecursive(child);
            }
            else
            {
                //只读属性会让 DeleteFile 失败，先清掉
                ::SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                ::DeleteFileW(child.c_str());
            }
        }
        while (::FindNextFileW(hFind, &fd));
        ::FindClose(hFind);
    }

    return ::RemoveDirectoryW(dir.c_str()) != FALSE;
}

} // namespace

Installer::Installer()
{
}

Installer::~Installer()
{
}

bool Installer::EnsureDirectory(const std::wstring& dir)
{
    if (dir.empty())
    {
        return false;
    }

    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES)
    {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    //先建父目录再建自己，这样多层路径一次到位
    const std::wstring parent = ParentDirOf(dir);
    if (!parent.empty() && parent != dir)
    {
        EnsureDirectory(parent);
    }

    if (::CreateDirectoryW(dir.c_str(), NULL))
    {
        return true;
    }
    //并发或竞态下别人先建好了，同样算成功
    return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

bool Installer::EnsureParentDirectory(const std::wstring& filePath)
{
    const std::wstring parent = ParentDirOf(filePath);
    if (parent.empty())
    {
        return true;
    }
    return EnsureDirectory(parent);
}

bool Installer::ReadWholeFile(const std::wstring& path, std::vector<unsigned char>& out,
                              std::wstring& errmsg)
{
    out.clear();

    HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        errmsg = TR(L"打不开升级包文件。");
        UpdateLog::Instance().WriteWithError(L"Failed to open the package: " + path, ::GetLastError());
        return false;
    }

    LARGE_INTEGER size;
    size.QuadPart = 0;
    if (!::GetFileSizeEx(hFile, &size))
    {
        ::CloseHandle(hFile);
        errmsg = TR(L"读取升级包大小失败。");
        return false;
    }

    if (size.QuadPart <= 0)
    {
        ::CloseHandle(hFile);
        errmsg = TR(L"升级包是空文件。");
        return false;
    }
    if (size.QuadPart > kMaxPackageBytes)
    {
        ::CloseHandle(hFile);
        errmsg = TR(L"升级包异常地大，已中止。");
        return false;
    }

    out.resize((size_t)size.QuadPart);

    //大文件一次 ReadFile 可能读不满，循环到读完为止
    size_t total = 0;
    while (total < out.size())
    {
        DWORD read = 0;
        const DWORD toRead = (DWORD)((out.size() - total > 0x10000000)
                                     ? 0x10000000 : (out.size() - total));
        if (!::ReadFile(hFile, &out[total], toRead, &read, NULL) || read == 0)
        {
            ::CloseHandle(hFile);
            out.clear();
            errmsg = TR(L"读取升级包内容失败。");
            UpdateLog::Instance().WriteWithError(L"Failed to read the package", ::GetLastError());
            return false;
        }
        total += read;
    }

    ::CloseHandle(hFile);
    return true;
}

bool Installer::ValidateEntries(const std::vector<unsigned char>& zip,
                                const std::wstring& targetDir, std::wstring& errmsg)
{
    std::vector<ZipEntry> entries;
    if (!ParseCentralDirectory(&zip[0], zip.size(), entries, errmsg))
    {
        UpdateLog::Instance().Write(L"Failed to parse the package: " + errmsg);
        return false;
    }

    if (entries.empty())
    {
        errmsg = TR(L"升级包里没有任何文件。");
        UpdateLog::Instance().Write(errmsg);
        return false;
    }

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const ZipEntry& e = entries[i];

        std::wstring rel;
        if (!NormalizeZipEntryName(e.m_name, rel))
        {
            //包里出现穿越路径说明这个包不可信，整包中止而不是跳过这一条
            errmsg = TRF(L"升级包里含有非法的文件路径，已中止：{0}", e.m_name);
            UpdateLog::Instance().Write(errmsg);
            return false;
        }

        if (e.m_isDirectory)
        {
            continue;
        }

        //目标文件已存在且只读时，后面覆盖必然失败，提前发现
        const std::wstring full = JoinPath(targetDir, rel);
        const DWORD attr = ::GetFileAttributesW(full.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY) != 0)
        {
            errmsg = TRF(L"目标文件是只读的，无法覆盖：{0}", rel);
            UpdateLog::Instance().Write(errmsg);
            return false;
        }
    }

    UpdateLog::Instance().Write(L"Validation passed, entries: " + std::to_wstring(entries.size()));
    return true;
}

bool Installer::BackupExistingFile(const std::wstring& fullPath, const std::wstring& relPath,
                                   std::wstring& errmsg)
{
    if (!FileExists(fullPath))
    {
        //全新增加的文件没有可备份的旧版本，回滚时直接删掉即可。
        //这里不登记，回滚只负责把旧文件搬回来 —— 多出来的新文件不会让程序起不来。
        return true;
    }

    const std::wstring backupPath = JoinPath(m_backupDir, relPath);
    if (!EnsureParentDirectory(backupPath))
    {
        errmsg = TR(L"创建备份目录失败。");
        return false;
    }

    //同名备份可能是上一次残留，先删掉
    ::SetFileAttributesW(backupPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    ::DeleteFileW(backupPath.c_str());

    //用移动而不是复制：同盘移动是改目录项，几乎不耗时也不占额外空间
    if (!::MoveFileW(fullPath.c_str(), backupPath.c_str()))
    {
        errmsg = TRF(L"备份旧文件失败：{0}", relPath);
        UpdateLog::Instance().WriteWithError(L"Backup failed: " + relPath, ::GetLastError());
        return false;
    }

    m_backups.push_back(std::make_pair(fullPath, backupPath));
    return true;
}

void Installer::Rollback()
{
    UpdateLog::Instance().Write(L"Rolling back, files to restore: "
                                + std::to_wstring(m_backups.size()) + L".");

    //按相反顺序搬回
    for (size_t i = m_backups.size(); i-- > 0; )
    {
        const std::wstring& original = m_backups[i].first;
        const std::wstring& backup   = m_backups[i].second;

        //已经写上去的新文件先删掉，否则 MoveFile 会因目标存在而失败
        ::SetFileAttributesW(original.c_str(), FILE_ATTRIBUTE_NORMAL);
        ::DeleteFileW(original.c_str());

        if (!::MoveFileW(backup.c_str(), original.c_str()))
        {
            //回滚本身失败：已经没有更好的补救手段，如实留痕比中断更有价值
            UpdateLog::Instance().WriteWithError(L"Rollback failed, file not restored: " + original,
                                                 ::GetLastError());
        }
    }

    m_backups.clear();
    UpdateLog::Instance().Write(L"Rollback finished.");
}

bool Installer::ApplyEntries(const std::vector<unsigned char>& zip,
                             const std::wstring& targetDir, std::wstring& errmsg)
{
    std::vector<ZipEntry> entries;
    if (!ParseCentralDirectory(&zip[0], zip.size(), entries, errmsg))
    {
        return false;
    }

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const ZipEntry& e = entries[i];

        std::wstring rel;
        if (!NormalizeZipEntryName(e.m_name, rel))
        {
            errmsg = TRF(L"升级包里含有非法的文件路径：{0}", e.m_name);
            return false;
        }

        const std::wstring full = JoinPath(targetDir, rel);

        if (e.m_isDirectory)
        {
            if (!EnsureDirectory(full))
            {
                errmsg = TRF(L"创建目录失败：{0}", rel);
                UpdateLog::Instance().WriteWithError(L"Failed to create directory: " + rel, ::GetLastError());
                return false;
            }
            continue;
        }

        //取出条目内容
        size_t dataOffset = 0;
        if (!LocateEntryData(&zip[0], zip.size(), e, dataOffset, errmsg))
        {
            return false;
        }

        std::vector<unsigned char> content;
        if (e.m_method == zip_method_store)
        {
            if (e.m_compressedSize != e.m_uncompressedSize)
            {
                errmsg = TRF(L"升级包条目声明为不压缩，但两个长度对不上：{0}", rel);
                return false;
            }
            content.assign(zip.begin() + dataOffset,
                           zip.begin() + dataOffset + e.m_compressedSize);
        }
        else
        {
            if (!InflateRaw(&zip[dataOffset], e.m_compressedSize, e.m_uncompressedSize,
                            content, errmsg))
            {
                UpdateLog::Instance().Write(L"Decompression failed: " + rel + L", " + errmsg);
                return false;
            }
        }

        //CRC 校验：能发现传输或解压过程中的损坏。zip 里 CRC 为 0 的条目（空文件，
        //或某些打包工具对 store 条目不写 CRC）跳过校验。
        if (e.m_crc32 != 0)
        {
            const uint32_t actual = content.empty()
                                  ? 0 : ComputeCrc32(&content[0], content.size());
            if (actual != e.m_crc32)
            {
                errmsg = TRF(L"文件校验失败，升级包已损坏：{0}", rel);
                UpdateLog::Instance().Write(errmsg);
                return false;
            }
        }

        //写之前先把旧文件备份走
        if (!BackupExistingFile(full, rel, errmsg))
        {
            return false;
        }

        if (!EnsureParentDirectory(full))
        {
            errmsg = TRF(L"创建目录失败：{0}", rel);
            return false;
        }

        HANDLE hOut = ::CreateFileW(full.c_str(), GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut == INVALID_HANDLE_VALUE)
        {
            errmsg = TRF(L"写入文件失败：{0}", rel);
            UpdateLog::Instance().WriteWithError(L"Failed to create file: " + rel, ::GetLastError());
            return false;
        }

        bool written = true;
        if (!content.empty())
        {
            DWORD n = 0;
            if (!::WriteFile(hOut, &content[0], (DWORD)content.size(), &n, NULL)
                || n != content.size())
            {
                written = false;
                UpdateLog::Instance().WriteWithError(L"Failed to write file: " + rel, ::GetLastError());
            }
        }
        ::CloseHandle(hOut);

        if (!written)
        {
            errmsg = TRF(L"写入文件失败：{0}", rel);
            return false;
        }
    }

    return true;
}

bool Installer::Install(const std::wstring& zipPath, const std::wstring& targetDir,
                        std::wstring& errmsg)
{
    errmsg.clear();
    m_backups.clear();

    UpdateLog& log = UpdateLog::Instance();
    log.Write(L"Installing: package = " + zipPath + L", target dir = " + targetDir);

    if (!EnsureDirectory(targetDir))
    {
        errmsg = TR(L"安装目录不存在且无法创建。");
        log.Write(errmsg);
        return false;
    }

    //每次安装前清掉上一次遗留的备份，免得回滚时把更早版本的文件搬回来
    m_backupDir = JoinPath(targetDir, kBackupSubDir);
    RemoveDirectoryRecursive(m_backupDir);
    if (!EnsureDirectory(m_backupDir))
    {
        errmsg = TR(L"创建备份目录失败。");
        log.Write(errmsg);
        return false;
    }

    std::vector<unsigned char> zip;
    if (!ReadWholeFile(zipPath, zip, errmsg))
    {
        return false;
    }

    //第一遍：只校验
    if (!ValidateEntries(zip, targetDir, errmsg))
    {
        return false;
    }

    //第二遍：真正落盘。任何一步失败都回滚到升级前的状态。
    if (!ApplyEntries(zip, targetDir, errmsg))
    {
        log.Write(L"Install failed: " + errmsg);
        Rollback();
        return false;
    }

    //全部成功，备份不再需要
    RemoveDirectoryRecursive(m_backupDir);
    m_backups.clear();

    log.Write(L"Install succeeded.");
    return true;
}

} // namespace iupdate
