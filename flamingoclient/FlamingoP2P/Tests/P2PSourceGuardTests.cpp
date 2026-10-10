/**
 *  P2P DLL 的源码与工程文件守卫（Suite P2PG，标签 [p2pdll]），P2PSourceGuardTests.cpp
 *  覆盖（对应 docs/P2P通信方案.md 第 1 期第 1b 步，2026-10-03 与用户确认的用例）：
 *    - P2PG1 FlamingoP2P.vcxproj 编译的每一个共用源文件（Source/ 与 FlamingoMedia/ 下、与别的工程共用的文件）
 *            都不得直接包含主程序专属的头文件（IUSocket.h、UserSessionData.h、FlamingoClient.h、IniFile.h、
 *            RemoteDesktopProtocolClient.h），做法与文件共享的 FDG1 相同；
 *    - P2PG2 FlamingoP2P.vcxproj 恰有四种配置（Debug / Release × Win32 / x64），每种配置的 C++ 编译选项
 *            （<ClCompile> 段，不是 <Midl> 或 <ResourceCompile> 段）都定义 WIN32 与 SODIUM_STATIC，
 *            CRT 都是静态版（Debug 为 MultiThreadedDebug，Release 为 MultiThreaded）。
 *
 *  为什么要守：
 *    - P2PG1：共用源文件背后若引入主程序的全局对象（网络连接单例、全局路径、配置文件），DLL 要么链接失败，
 *      要么编进一份从未初始化的全局对象、运行期才出问题。
 *    - P2PG2：开发约定文档记录的两个易错点——仓库里所有代码判断的是 WIN32 而不是 MSVC 的 _WIN32，漏定义时
 *      x64 配置会走进 POSIX 分支；SODIUM_STATIC 漏定义时 libsodium 的函数按 DLL 导入声明，静态链接失败，
 *      且宏加在 <Midl> 段里不报任何错。CRT 必须是静态版，exe 与各 DLL 才都不依赖 VC 运行库。
 *
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径），
 *  退路是从测试程序所在目录往上找客户端根目录。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

//客户端根目录（flamingoclient）的判定标志：其下有本 DLL 的工程文件
const char* const kDllProjectRelativePath = "FlamingoP2P\\FlamingoP2P.vcxproj";

//向上回溯查找客户端根目录时允许的最大层级数（测试程序在 Debug_Win32\Tests，回到根要 2 层）
const int kMaxParentWalkDepth = 10;

//工程文件里与别的工程共用的文件，其 Include 路径以这两个前缀之一开头
const char* const kSharedSourcePrefixes[] =
{
    "..\\Source\\",
    "..\\FlamingoMedia\\",
};

//共用源文件不得直接包含的主程序专属头文件（按文件名匹配，不区分所在目录）
const char* const kForbiddenHeaders[] =
{
    "IUSocket.h",
    "UserSessionData.h",
    "FlamingoClient.h",
    "IniFile.h",
    "RemoteDesktopProtocolClient.h",
};

//第 1b 步起 DLL 至少编译这么多个共用文件（日志接口 IULog.h、日志格式化 LogFormat.h / .cpp）；
//少于它说明清单没取到，守卫等于没干活
const size_t kMinSharedSourceFiles = 3;

//工程必须恰有的四种配置，与各自应使用的 CRT
struct ExpectedConfig
{
    const char* m_condition;      //ItemDefinitionGroup 的 Condition 里的「配置|平台」
    const char* m_runtimeLibrary; //应使用的 CRT
};

const ExpectedConfig kExpectedConfigs[] =
{
    { "Debug|Win32",   "MultiThreadedDebug" },
    { "Release|Win32", "MultiThreaded" },
    { "Debug|x64",     "MultiThreadedDebug" },
    { "Release|x64",   "MultiThreaded" },
};

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

//判断工程文件里的一个 Include 路径是否指向共用文件
bool IsSharedSource(const std::string& include)
{
    for (size_t i = 0; i < sizeof(kSharedSourcePrefixes) / sizeof(kSharedSourcePrefixes[0]); ++i)
    {
        const char* prefix = kSharedSourcePrefixes[i];
        if (include.compare(0, strlen(prefix), prefix) == 0)
            return true;
    }
    return false;
}

//从工程文件文本里取出所有指向共用文件的 Include 路径
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
        if (IsSharedSource(include))
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
        //只看行尾注释之前的部分：注释里提到某个头文件名不算包含
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

//取 open 与 close 两个标记之间的文本（从 from 开始找第一组）；找不到返回空串
std::string Between(const std::string& text, size_t from, const std::string& open, const std::string& close)
{
    size_t begin = text.find(open, from);
    if (begin == std::string::npos)
        return std::string();
    begin += open.size();
    size_t end = text.find(close, begin);
    if (end == std::string::npos)
        return std::string();
    return text.substr(begin, end - begin);
}

//把以 ';' 分隔的宏清单拆开后，判断其中是否有 name 这一项（整项匹配，WIN32 不会被 _WIN32 冒充）
bool HasDefinition(const std::string& definitions, const std::string& name)
{
    size_t pos = 0;
    while (pos <= definitions.size())
    {
        size_t end = definitions.find(';', pos);
        if (end == std::string::npos)
            end = definitions.size();
        if (Trim(definitions.substr(pos, end - pos)) == name)
            return true;
        pos = end + 1;
    }
    return false;
}

//工程文件里 <ProjectConfiguration Include="..."> 的条数
size_t CountProjectConfigurations(const std::string& project)
{
    const std::string key = "<ProjectConfiguration Include=\"";
    size_t n = 0;
    size_t pos = 0;
    while ((pos = project.find(key, pos)) != std::string::npos)
    {
        ++n;
        pos += key.size();
    }
    return n;
}

} // namespace

TEST_CASE("P2PG1 P2P DLL 编译的共用源文件不直接包含主程序专属头文件", "[p2pdll]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());

    std::string project;
    REQUIRE(ReadWholeFile(root + "\\" + kDllProjectRelativePath, project));

    std::vector<std::string> sources = CollectSharedSources(project);
    CHECK(sources.size() >= kMinSharedSourceFiles);

    for (size_t i = 0; i < sources.size(); ++i)
    {
        //工程文件里的路径相对 FlamingoP2P 目录，形如 ..\Source\Common\Log\IULog.h
        std::string path = root + "\\FlamingoP2P\\" + sources[i];
        std::string source;
        INFO("shared source: " << sources[i]);
        REQUIRE(ReadWholeFile(path, source));

        std::vector<std::string> violations = FindViolations(sources[i], source);
        for (size_t v = 0; v < violations.size(); ++v)
            UNSCOPED_INFO(violations[v]);
        CHECK(violations.empty());
    }
}

TEST_CASE("P2PG2 P2P DLL 工程四种配置都定义 WIN32 与 SODIUM_STATIC 并使用静态 CRT", "[p2pdll]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());

    std::string project;
    REQUIRE(ReadWholeFile(root + "\\" + kDllProjectRelativePath, project));

    const size_t expectedCount = sizeof(kExpectedConfigs) / sizeof(kExpectedConfigs[0]);
    CHECK(CountProjectConfigurations(project) == expectedCount);

    for (size_t i = 0; i < expectedCount; ++i)
    {
        const ExpectedConfig& cfg = kExpectedConfigs[i];
        INFO("configuration: " << cfg.m_condition);

        //这种配置的 ItemDefinitionGroup 段
        std::string condition = std::string("<ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='")
                                + cfg.m_condition + "'\">";
        size_t groupPos = project.find(condition);
        REQUIRE(groupPos != std::string::npos);
        std::string group = Between(project, groupPos, condition, "</ItemDefinitionGroup>");
        REQUIRE_FALSE(group.empty());

        //只看其中的 <ClCompile> 段：宏写在 <Midl> 或 <ResourceCompile> 段里对 C++ 编译不生效
        std::string clCompile = Between(group, 0, "<ClCompile>", "</ClCompile>");
        REQUIRE_FALSE(clCompile.empty());
        std::string definitions = Between(clCompile, 0, "<PreprocessorDefinitions>", "</PreprocessorDefinitions>");
        CHECK(HasDefinition(definitions, "WIN32"));
        CHECK(HasDefinition(definitions, "SODIUM_STATIC"));

        std::string runtime = Between(clCompile, 0, "<RuntimeLibrary>", "</RuntimeLibrary>");
        CHECK(runtime == cfg.m_runtimeLibrary);
    }
}
