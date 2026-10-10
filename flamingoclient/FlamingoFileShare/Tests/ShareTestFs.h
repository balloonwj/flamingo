/**
 *  文件共享单元测试共用的文件系统辅助函数（只有头文件），ShareTestFs.h
 *
 *  ShareFileOps、SharePathGuard、ShareProvider 的用例要在测试程序目录下建真实的文件、目录、目录联接，其中有的
 *  路径故意超过 MAX_PATH。这里集中提供建目录、写文件、建目录联接、删整棵目录树的函数，全部经 \\?\ 前缀访问，
 *  删除时不进入目录联接内部，避免误删联接指向的共享根之外的目录。
 *
 *  典型用法：
 *      const std::wstring base = sharetestfs::ExeDir() + L"\\sfo_test";
 *      sharetestfs::DeleteTree(base);
 *      REQUIRE(sharetestfs::MakeDirs(base + L"\\a\\b"));
 *      REQUIRE(sharetestfs::WriteFileBytes(base + L"\\a\\x.txt", 100));
 *      ...
 *      sharetestfs::DeleteTree(base);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "../Engine/ShareWinPath.h"

namespace sharetestfs {

//写测试文件时每次写入的块大小，单位：字节
const size_t kWriteChunkBytes = 64 * 1024;

//测试程序所在目录（不带结尾反斜杠）
inline std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s(buf);
    const size_t pos = s.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? s : s.substr(0, pos);
}

/**
 *  逐级建目录（已存在的跳过），支持超过 MAX_PATH 的路径。
 *  @param path 本机绝对路径（盘符开头，'\' 分隔）
 *  @return 最终目录存在返回 true
 */
inline bool MakeDirs(const std::wstring& path)
{
    size_t pos = path.find(L'\\', 3);
    while (true)
    {
        const std::wstring part = (pos == std::wstring::npos) ? path : path.substr(0, pos);
        if (!::CreateDirectoryW(fileshare::ShareToLongPath(part).c_str(), NULL) && ::GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        if (pos == std::wstring::npos)
            break;
        pos = path.find(L'\\', pos + 1);
    }
    return true;
}

/**
 *  建一个指定字节数的文件（内容为重复的字母），已存在时覆盖。
 *  @param path  本机绝对路径
 *  @param bytes 文件字节数
 *  @return 成功返回 true
 */
inline bool WriteFileBytes(const std::wstring& path, size_t bytes)
{
    HANDLE h = ::CreateFileW(fileshare::ShareToLongPath(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    std::vector<char> chunk(kWriteChunkBytes, 'x');
    size_t left = bytes;
    bool ok = true;
    while (left > 0 && ok)
    {
        const DWORD n = static_cast<DWORD>(left < chunk.size() ? left : chunk.size());
        DWORD written = 0;
        ok = ::WriteFile(h, &chunk[0], n, &written, NULL) && written == n;
        left -= n;
    }
    ::CloseHandle(h);
    return ok;
}

/**
 *  执行一条 cmd 命令并等它结束（建目录联接用）。
 *  @param command cmd /c 之后的命令行
 *  @return cmd 的退出码；进程建不起来返回 -1
 */
inline int RunCmd(const std::wstring& command)
{
    std::wstring line = L"cmd.exe /c " + command;
    STARTUPINFOW si;
    ::ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    ::ZeroMemory(&pi, sizeof(pi));
    if (!::CreateProcessW(NULL, &line[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

/**
 *  在任意深度建一个目录联接：先用 mklink /J 在 shortTemp 建好（mklink 不支持超长路径），再整体移动到 linkPath。
 *  @param linkPath  联接最终所在的本机绝对路径（可以超过 MAX_PATH），父目录须已存在
 *  @param target    联接指向的目录
 *  @param shortTemp 一个不超过 MAX_PATH、尚不存在的临时路径，与 linkPath 在同一个卷上
 *  @return 成功返回 true
 */
inline bool MakeJunction(const std::wstring& linkPath, const std::wstring& target, const std::wstring& shortTemp)
{
    if (RunCmd(L"mklink /J \"" + shortTemp + L"\" \"" + target + L"\"") != 0)
        return false;
    return ::MoveFileW(fileshare::ShareToLongPath(shortTemp).c_str(), fileshare::ShareToLongPath(linkPath).c_str()) != FALSE;
}

/**
 *  删除整棵目录树（支持超过 MAX_PATH 的路径）。遇到目录联接、符号链接只删链接本身，不进入其内部；
 *  先去掉只读、隐藏、系统属性再删。路径不存在时直接返回。
 *  @param path 本机绝对路径
 */
inline void DeleteTree(const std::wstring& path)
{
    const std::wstring longPath = fileshare::ShareToLongPath(path);
    const DWORD attrs = ::GetFileAttributesW(longPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return;
    ::SetFileAttributesW(longPath.c_str(), FILE_ATTRIBUTE_NORMAL);

    //普通文件
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        ::DeleteFileW(longPath.c_str());
        return;
    }
    //目录联接或指向目录的符号链接：只删链接
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        ::RemoveDirectoryW(longPath.c_str());
        return;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((longPath + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring name(fd.cFileName);
            if (name != L"." && name != L"..")
                DeleteTree(path + L"\\" + name);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    ::RemoveDirectoryW(longPath.c_str());
}

/**
 *  拼一条超过 MAX_PATH 的子路径：count 段、每段 segChars 个字符（段名形如 seg00aaaa…）。
 *  @param base     起点目录
 *  @param count    段数
 *  @param segChars 每段的字符数，至少 6
 *  @return base 之下的深层路径
 */
inline std::wstring DeepPath(const std::wstring& base, int count, int segChars)
{
    std::wstring path = base;
    for (int i = 0; i < count; ++i)
    {
        wchar_t prefix[8] = { 0 };
        ::wsprintfW(prefix, L"seg%02d", i);
        std::wstring seg(prefix);
        seg.append(static_cast<size_t>(segChars) - seg.size(), L'a');
        path += L"\\" + seg;
    }
    return path;
}

} // namespace sharetestfs
