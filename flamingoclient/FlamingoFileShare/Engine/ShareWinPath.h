/**
 *  文件共享 DLL 内部共用的编码转换与 Windows 路径辅助函数（只有头文件），ShareWinPath.h
 *
 *  访问者送来的路径与条目名在线上一律是 UTF-8，分享者访问文件系统用的是 UTF-16；共享根下的路径可能超过
 *  MAX_PATH，调用文件系统接口之前要加 \\?\ 前缀（docs/文件共享方案.md 9.6）。这几处转换在路径检查、列目录、
 *  传输等模块里都要用到，集中在这里，避免各写一份。
 *
 *  典型用法：
 *      std::wstring wide;
 *      if (!ShareUtf8ToWide(relPath, wide))
 *          ...（非法 UTF-8）
 *      HANDLE h = ::CreateFileW(ShareToLongPath(absPath).c_str(), ...);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <string>

namespace fileshare {

/**
 *  UTF-8 转 UTF-16。
 *  @param text UTF-8 文本，可以为空串
 *  @param out  [出参] 转换结果
 *  @return 成功返回 true；含非法 UTF-8 序列时返回 false
 */
inline bool ShareUtf8ToWide(const std::string& text, std::wstring& out)
{
    out.clear();
    if (text.empty())
        return true;
    const int len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                          NULL, 0);
    if (len <= 0)
        return false;
    out.resize(static_cast<size_t>(len));
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), &out[0],
                                 len) == len;
}

/**
 *  UTF-16 转 UTF-8。
 *  @param text UTF-16 文本，可以为空串
 *  @return 转换结果；转换失败时返回空串
 */
inline std::string ShareWideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    if (len <= 0)
        return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

/**
 *  给本机绝对路径加上长路径前缀，使文件系统接口不受 MAX_PATH 限制：
 *  "D:\a" 变成 "\\?\D:\a"，"\\server\share\a" 变成 "\\?\UNC\server\share\a"，已带前缀的原样返回。
 *  带前缀的路径不再经过规范化，调用方传入的必须已是规范的绝对路径（ResolveUnderRoot 的结果满足这一点）。
 *  @param absPath 本机绝对路径（'\' 分隔）
 *  @return 带前缀的路径；不是绝对路径时原样返回
 */
inline std::wstring ShareToLongPath(const std::wstring& absPath)
{
    //已经带了 \\?\ 前缀
    if (absPath.compare(0, 4, L"\\\\?\\") == 0)
        return absPath;
    //UNC 路径：\\server\share\...
    if (absPath.size() > 2 && absPath[0] == L'\\' && absPath[1] == L'\\')
        return L"\\\\?\\UNC\\" + absPath.substr(2);
    //盘符路径：X:\...
    if (absPath.size() >= 3 && absPath[1] == L':' && absPath[2] == L'\\')
        return L"\\\\?\\" + absPath;
    return absPath;
}

/**
 *  删除整棵目录树（或单个文件），支持超长路径；遇到重解析点只删链接本身，不进入它指向的目录。
 *  删不掉的条目（被占用等）留在原处，不报告。用于清理本机的临时目录（拖出的下载目录）。
 *  @param path 本机绝对路径；不存在时调用无效果
 */
inline void ShareDeleteTree(const std::wstring& path)
{
    const std::wstring longPath = ShareToLongPath(path);
    const DWORD attrs = ::GetFileAttributesW(longPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return;
    //只读属性会让删除失败：先去掉
    ::SetFileAttributesW(longPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        ::DeleteFileW(longPath.c_str());
        return;
    }
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW((longPath + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::wstring name(fd.cFileName);
                if (name != L"." && name != L"..")
                    ShareDeleteTree(path + L"\\" + name);
            } while (::FindNextFileW(h, &fd));
            ::FindClose(h);
        }
    }
    ::RemoveDirectoryW(longPath.c_str());
}

} // namespace fileshare
