/**
 *  本地文件枚举与元信息读取实现，FileEnum.cpp
 *  balloonwj@qq.com   2026-08-11
 */
#include "FileEnum.h"

#include <algorithm>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "MD5.h"

namespace fileenum {

//目录分隔符。Windows 的 API 同时接受 '/' 与 '\\'，统一用 '/' 拼接，
//这样两个平台上的路径字符串形态一致，日志与测试断言都好写。
static const char PATH_SEPARATOR = '/';

std::string WithTrailingSeparator(const std::string& dir)
{
    if (dir.empty())
        return dir;

    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\')
        return dir;

    return dir + PATH_SEPARATOR;
}

void ListFileNames(const std::string& dir, std::vector<std::string>& out)
{
    out.clear();
    if (dir.empty())
        return;

#ifdef _WIN32
    const std::string pattern = WithTrailingSeparator(dir) + "*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = ::FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        //子目录一律跳过：本模块只列一层
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;

        out.push_back(std::string(fd.cFileName));
    } while (::FindNextFileA(hFind, &fd) != FALSE);

    ::FindClose(hFind);
#else
    DIR* dp = opendir(dir.c_str());
    if (dp == NULL)
        return;

    const std::string base = WithTrailingSeparator(dir);
    struct dirent* entry = NULL;
    while ((entry = readdir(dp)) != NULL)
    {
        const std::string name(entry->d_name);
        if (name == "." || name == "..")
            continue;

        //是不是普通文件交给 StatFile64 判定：某些文件系统上 d_type 恒为 DT_UNKNOWN，
        //只认 d_type 会把真实存在的文件漏掉
        int64_t size = 0;
        int64_t mtime = 0;
        if (!StatFile64(base + name, size, mtime))
            continue;

        out.push_back(name);
    }

    closedir(dp);
#endif

    //排序：让同一目录在不同平台、不同文件系统上得到一致且可复现的顺序，
    //否则"同内容多份文件保留先遇到的那个"这类行为会随环境漂移
    std::sort(out.begin(), out.end());
}

bool StatFile64(const std::string& path, int64_t& outSize, int64_t& outMtime)
{
    if (path.empty())
        return false;

#ifdef _WIN32
    struct __stat64 st;
    if (_stat64(path.c_str(), &st) != 0)
        return false;
    if ((st.st_mode & _S_IFREG) == 0)
        return false;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    if (!S_ISREG(st.st_mode))
        return false;
#endif

    outSize  = static_cast<int64_t>(st.st_size);
    outMtime = static_cast<int64_t>(st.st_mtime);
    return true;
}

std::string ComputeFileMd5(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return std::string();

    //MD5 的 ifstream 重载内部按块读，不会把整个文件读进内存；
    //其 toString 输出的就是小写十六进制（见 utils/MD5.cpp 的字符表），无需再转换
    MD5 md5(in);
    return md5.toString();
}

} // namespace fileenum
