/**
 *  文件共享 DLL 的源码守卫（Suite FDG，标签 [filesharedll]），FileShareSourceGuardTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 3 步，2026-10-02 与用户确认的用例）：
 *    - FDG1 FlamingoFileShare.vcxproj 编译的每一个 Source/ 下的文件（与 exe 共用的源文件），
 *           都不得直接包含主程序专属的头文件（IUSocket.h、UserSessionData.h、FlamingoClient.h、
 *           IniFile.h、RemoteDesktopProtocolClient.h）。
 *
 *  为什么要守：DLL 与 exe 编译同一份源文件（方案 2.6「公共代码」），而这些头文件背后是主程序的
 *  全局对象（网络连接单例、全局路径、配置文件）。一旦有人往共用的源文件里加了这类依赖，DLL 要么
 *  链接失败，要么编进一份从未初始化的全局对象、运行期才出问题。本用例让这种改动在单元测试里就报出来。
 *
 *  只检查直接包含（#include 行），不追踪间接包含；被检查的文件清单直接取自 DLL 的工程文件，
 *  DLL 新增共用源文件时不必改本用例。
 *
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径），
 *  退路是从测试程序所在目录往上找客户端根目录。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

//客户端根目录（flamingoclient）的判定标志：其下有本 DLL 的工程文件
const char* const kDllProjectRelativePath = "FlamingoFileShare\\FlamingoFileShare.vcxproj";

//向上回溯查找客户端根目录时允许的最大层级数（测试程序在 Debug_Win32\Tests，回到根要 2 层）
const int kMaxParentWalkDepth = 10;

//工程文件里与 exe 共用的源文件，其 Include 路径都以此开头
const char* const kSharedSourcePrefix = "..\\Source\\";

//共用源文件不得直接包含的主程序专属头文件（按文件名匹配，不区分所在目录）
const char* const kForbiddenHeaders[] =
{
    "IUSocket.h",
    "UserSessionData.h",
    "FlamingoClient.h",
    "IniFile.h",
    "RemoteDesktopProtocolClient.h",
};

//第 3 步起 DLL 至少编译这么多个共用源文件（含头文件）；少于它说明清单没取到，守卫等于没干活
const size_t kMinSharedSourceFiles = 10;

//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;

//取路径里最后一个目录分隔符之前的部分；没有分隔符时返回空串
std::string GetParentDirectory(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
        return std::string();
    return path.substr(0, pos);
}

//判断文件是否存在
bool FileExists(const std::string& path)
{
    return ::GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

//从 startDir 起逐级向上找客户端根目录，找不到返回空串
std::string WalkUpToClientRoot(const std::string& startDir)
{
    std::string dir = startDir;
    for (int depth = 0; depth < kMaxParentWalkDepth; ++depth)
    {
        if (dir.empty())
            break;
        if (FileExists(dir + "\\" + kDllProjectRelativePath))
            return dir;
        std::string parent = GetParentDirectory(dir);
        if (parent == dir)
            break;
        dir = parent;
    }
    return std::string();
}

//定位客户端根目录：先按 __FILE__，再按测试程序所在目录
std::string ResolveClientRoot()
{
    std::string root = WalkUpToClientRoot(GetParentDirectory(std::string(__FILE__)));
    if (!root.empty())
        return root;

    char moduleName[MAX_PATH] = { 0 };
    DWORD len = ::GetModuleFileNameA(NULL, moduleName, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return std::string();
    return WalkUpToClientRoot(GetParentDirectory(std::string(moduleName)));
}

//把整个文件读进 out，打不开返回 false
bool ReadWholeFile(const std::string& path, std::string& out)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
        return false;
    out.clear();
    char buf[kReadChunkBytes];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        out.append(buf, n);
    fclose(fp);
    return true;
}

//从工程文件文本里取出所有以 kSharedSourcePrefix 开头的 Include 路径
std::vector<std::string> CollectSharedSources(const std::string& project)
{
    std::vector<std::string> result;
    const std::string key = "Include=\"";
    size_t pos = 0;
    while ((pos = project.find(key, pos)) != std::string::npos)
    {
        pos += key.size();
        size_t end = project.find('"', pos);
        if (end == std::string::npos)
            break;
        std::string include = project.substr(pos, end - pos);
        if (include.compare(0, strlen(kSharedSourcePrefix), kSharedSourcePrefix) == 0)
            result.push_back(include);
        pos = end;
    }
    return result;
}

//去掉一行首尾的空白
std::string Trim(const std::string& text)
{
    size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string::npos)
        return std::string();
    size_t end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

//在一份源文件里找直接包含了禁用头文件的行，返回每处违规的说明
std::vector<std::string> FindViolations(const std::string& fileName, const std::string& source)
{
    std::vector<std::string> violations;
    size_t lineStart = 0;
    int lineNo = 0;
    while (lineStart <= source.size())
    {
        size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = source.size();
        ++lineNo;
        std::string line = Trim(source.substr(lineStart, lineEnd - lineStart));
        //只看行尾注释之前的部分：注释里提到某个头文件名（如「UserSessionData.h 需要」）不算包含
        size_t commentPos = line.find("//");
        if (commentPos != std::string::npos)
            line = Trim(line.substr(0, commentPos));
        if (line.compare(0, strlen("#include"), "#include") == 0)
        {
            for (size_t i = 0; i < sizeof(kForbiddenHeaders) / sizeof(kForbiddenHeaders[0]); ++i)
            {
                if (line.find(kForbiddenHeaders[i]) != std::string::npos)
                {
                    char lineText[32];
                    sprintf_s(lineText, "%d", lineNo);
                    violations.push_back(fileName + "(" + lineText + "): " + line);
                }
            }
        }
        lineStart = lineEnd + 1;
    }
    return violations;
}

} // namespace

TEST_CASE("FDG1 文件共享 DLL 编译的共用源文件不直接包含主程序专属头文件", "[filesharedll]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());

    std::string project;
    REQUIRE(ReadWholeFile(root + "\\" + kDllProjectRelativePath, project));

    std::vector<std::string> sources = CollectSharedSources(project);
    CHECK(sources.size() >= kMinSharedSourceFiles);

    for (size_t i = 0; i < sources.size(); ++i)
    {
        //工程文件里的路径相对 FlamingoFileShare 目录，形如 ..\Source\net\FrameCodec.cpp
        std::string path = root + "\\FlamingoFileShare\\" + sources[i];
        std::string source;
        INFO("shared source: " << sources[i]);
        REQUIRE(ReadWholeFile(path, source));

        std::vector<std::string> violations = FindViolations(sources[i], source);
        for (size_t v = 0; v < violations.size(); ++v)
            UNSCOPED_INFO(violations[v]);
        CHECK(violations.empty());
    }
}
