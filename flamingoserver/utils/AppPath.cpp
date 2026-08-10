/**
 *  可执行文件路径工具实现，AppPath.cpp
 *  balloonwj@qq.com   2026-08-10
 */
#include "AppPath.h"

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace apppath {

//Windows 盘符前缀的长度，形如 "X:" 共两个字符。判定绝对路径时要先够这个长度才能看第二个字符。
#define DRIVE_PREFIX_LEN   2

//"./" 或 ".\" 这种当前目录前缀的长度，跳过它时按此长度前进。
#define DOT_SLASH_PREFIX_LEN   2

//本平台拼接路径时使用的分隔符。两种分隔符在 Windows 上都能用，
//但拼出来的路径会进日志，统一成平台惯用的那个，读起来不别扭。
#ifdef WIN32
#define PATH_SEPARATOR_CHAR   '\\'
#else
#define PATH_SEPARATOR_CHAR   '/'
#endif

#ifndef WIN32
//Linux 下读取自身路径的 procfs 入口。
#define SELF_EXE_LINK   "/proc/self/exe"

//某些平台没有定义 PATH_MAX（如部分 musl 环境），这里给一个足够用的兜底值。
#ifndef PATH_MAX
#define PATH_MAX   4096
#endif
#endif

//判断一个字符是不是路径分隔符。Windows 两种都认，Linux 上只有 '/' 是分隔符，
//但把 '\\' 一并当分隔符处理也无害 —— Linux 文件名里几乎不会出现反斜杠。
static bool IsPathSeparator(char ch)
{
    return ch == '/' || ch == '\\';
}

//挑选拼接时使用的分隔符：跟随基准目录里已经在用的那一种，基准目录里一个分隔符都没有
//（例如只有盘符 "E:"）时才用平台默认的。
//这样拼出来的路径分隔符风格统一，不会出现 "/opt/flamingo/bin\data" 这种混搭 ——
//混搭虽然在 Windows 上照样能打开文件，但这条路径会进日志、也会出现在报错信息里，
//风格统一读起来才不费劲。
static char PickSeparator(const std::string& baseDir)
{
    size_t lastSep = baseDir.find_last_of("/\\");
    if (lastSep != std::string::npos)
        return baseDir[lastSep];

    return PATH_SEPARATOR_CHAR;
}

std::string GetExecutableDirectory()
{
    std::string fullPath;

#ifdef WIN32
    char buf[MAX_PATH] = { 0 };
    //第一个参数传 NULL 表示取当前进程自身的可执行文件路径
    DWORD len = ::GetModuleFileNameA(NULL, buf, MAX_PATH);
    //返回 0 是失败；返回值等于缓冲区大小说明路径被截断了，截断的路径不能用
    if (len == 0 || len >= MAX_PATH)
        return std::string();

    fullPath.assign(buf, len);
#else
    char buf[PATH_MAX] = { 0 };
    //readlink 不会补结尾的 '\0'，所以少读一个字节，留出位置自己补
    ssize_t len = ::readlink(SELF_EXE_LINK, buf, sizeof(buf) - 1);
    if (len <= 0)
        return std::string();

    buf[len] = '\0';
    fullPath.assign(buf, static_cast<size_t>(len));
#endif

    //砍掉文件名，只留目录部分；结果不带结尾分隔符
    size_t lastSep = fullPath.find_last_of("/\\");
    if (lastSep == std::string::npos)
        return std::string();

    //形如 "/chatserver"：父目录就是根目录，此时保留这个分隔符，否则会退化成空串
    if (lastSep == 0)
        return fullPath.substr(0, 1);

    return fullPath.substr(0, lastSep);
}

bool IsAbsolutePath(const std::string& path)
{
    if (path.empty())
        return false;

    //POSIX 的 "/dir/file"；在 Windows 上则涵盖 UNC 的 "\\server\share" 与当前盘根目录的 "\dir"
    if (IsPathSeparator(path[0]))
        return true;

    //Windows 盘符："X:\dir\file"、"X:/dir/file"，以及少见的 "X:dir"（盘符相对路径）。
    //后者严格说不是绝对路径，但它同样不该被拼到别的目录后面，故一并按绝对路径处理。
    if (path.size() >= DRIVE_PREFIX_LEN && path[1] == ':')
        return true;

    return false;
}

std::string ResolveAgainstDirectory(const std::string& baseDir, const std::string& path)
{
    //空路径没有可解析的内容，原样返回，由上层去回落到默认值
    if (path.empty())
        return path;

    //已经是绝对路径的，说明部署方明确指定了位置，不要再改动它
    if (IsAbsolutePath(path))
        return path;

    //拿不到基准目录时回落到原有行为：路径仍相对进程工作目录解析
    if (baseDir.empty())
        return path;

    std::string result = baseDir;
    //基准目录可能自带结尾分隔符（如根目录 "/"），避免拼出 "//" 这种双分隔符
    if (!IsPathSeparator(result[result.size() - 1]))
        result += PickSeparator(baseDir);

    //去掉开头的 "./" 或 ".\"，可能有连续多段。留着不影响打开文件，
    //但拼出来的路径会进日志，夹着无意义的片段不好读。
    size_t start = 0;
    while (start + 1 < path.size() && path[start] == '.' && IsPathSeparator(path[start + 1]))
    {
        start += DOT_SLASH_PREFIX_LEN;
    }

    result += path.substr(start);
    return result;
}

} // namespace apppath
