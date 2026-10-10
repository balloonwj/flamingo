/**
 *  机器人 DLL 的工程文件与源码守卫（标签 [botdll]，做法与帖子群的 TGG1~TGG4 相同）：
 *    - BOTG1 FlamingoBot.vcxproj 与模块目录（Host、Core、UI）一致：每个 .h / .cpp 都列在工程里，工程里列出的
 *            这些目录下的文件也都存在（工程文件由 tools/gen_bot_vcxproj.py 产出，加了文件忘记重新生成时这里失败）；
 *    - BOTG2 工程恰有四种配置，每种配置都定义 WIN32，CRT 都是静态版；
 *    - BOTG3 FlamingoBot.def 导出 FlamingoBot_Create，与接口头文件的导出名一致；
 *    - BOTG4 工程编译的共用源文件（..\Source\、..\FlamingoMedia\ 下）不直接包含主程序专属的头文件。
 *
 *  源文件位置首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC），退路是从测试程序所在目录往上找客户端根目录。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-08
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>

#include "../BotApi.h"

namespace {

//客户端根目录（flamingoclient）的判定标志：其下有本 DLL 的工程文件
const char* const kDllProjectRelativePath = "FlamingoBot\\FlamingoBot.vcxproj";
const char* const kDllDefRelativePath = "FlamingoBot\\FlamingoBot.def";

//向上回溯查找客户端根目录时允许的最大层级数
const int kMaxParentWalkDepth = 10;

//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;

//工程按目录收集源文件的模块目录（与生成脚本的 MODULE_DIRS 相同）
const char* const kModuleDirs[] = { "Host", "Core", "UI" };

//共用文件的 Include 路径前缀
const char* const kSharedSourcePrefixes[] = { "..\\Source\\", "..\\FlamingoMedia\\" };

//共用源文件不得直接包含的主程序专属头文件（按文件名匹配）
const char* const kForbiddenHeaders[] =
{
    "IUSocket.h",
    "UserSessionData.h",
    "FlamingoClient.h",
    "IniFile.h",
    "BalloonMainPanel.h",
};

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

std::string GetParentDirectory(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
        return std::string();
    return path.substr(0, pos);
}

bool FileExists(const std::string& path)
{
    return ::GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

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

std::string Trim(const std::string& text)
{
    size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string::npos)
        return std::string();
    size_t end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

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

//工程文件里所有 Include="..." 的路径
std::vector<std::string> CollectIncludes(const std::string& project)
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
        result.push_back(project.substr(pos, end - pos));
        pos = end;
    }
    return result;
}

bool StartsWith(const std::string& text, const char* prefix)
{
    return text.compare(0, strlen(prefix), prefix) == 0;
}

bool EndsWith(const std::string& text, const char* suffix)
{
    const size_t n = strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

//模块目录下实际存在的 .h / .cpp（相对 FlamingoBot 目录）
std::set<std::string> ListModuleFiles(const std::string& dllDir)
{
    std::set<std::string> files;
    for (size_t i = 0; i < sizeof(kModuleDirs) / sizeof(kModuleDirs[0]); ++i)
    {
        const std::string dir = std::string(kModuleDirs[i]);
        WIN32_FIND_DATAA fd;
        HANDLE h = ::FindFirstFileA((dllDir + "\\" + dir + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                continue;
            const std::string name(fd.cFileName);
            if (EndsWith(name, ".h") || EndsWith(name, ".cpp"))
                files.insert(dir + "\\" + name);
        } while (::FindNextFileA(h, &fd));
        ::FindClose(h);
    }
    return files;
}

//工程里列出的、位于模块目录下的文件
std::set<std::string> ListProjectModuleFiles(const std::string& project)
{
    std::set<std::string> files;
    const std::vector<std::string> includes = CollectIncludes(project);
    for (size_t i = 0; i < includes.size(); ++i)
    {
        for (size_t d = 0; d < sizeof(kModuleDirs) / sizeof(kModuleDirs[0]); ++d)
        {
            if (StartsWith(includes[i], (std::string(kModuleDirs[d]) + "\\").c_str()))
                files.insert(includes[i]);
        }
    }
    return files;
}

//一份源文件里直接包含了禁用头文件的行
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
        size_t commentPos = line.find("//");
        if (commentPos != std::string::npos)
            line = Trim(line.substr(0, commentPos));
        if (StartsWith(line, "#include"))
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

TEST_CASE("BOTG1 机器人工程文件与模块目录一致（加了文件要重新运行 gen_bot_vcxproj.py）", "[botdll]")
{
    const std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    std::string project;
    REQUIRE(ReadWholeFile(root + "\\" + kDllProjectRelativePath, project));
    const std::set<std::string> onDisk = ListModuleFiles(root + "\\FlamingoBot");
    const std::set<std::string> inProject = ListProjectModuleFiles(project);
    CHECK(onDisk.size() > 0);
    for (std::set<std::string>::const_iterator it = onDisk.begin(); it != onDisk.end(); ++it)
    {
        INFO("file on disk: " << *it);
        CHECK(inProject.count(*it) == 1);
    }
    for (std::set<std::string>::const_iterator it = inProject.begin(); it != inProject.end(); ++it)
    {
        INFO("file in project: " << *it);
        CHECK(onDisk.count(*it) == 1);
    }
}

TEST_CASE("BOTG2 机器人工程四种配置都定义 WIN32 并使用静态 CRT", "[botdll]")
{
    const std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    std::string project;
    REQUIRE(ReadWholeFile(root + "\\" + kDllProjectRelativePath, project));
    const size_t expectedCount = sizeof(kExpectedConfigs) / sizeof(kExpectedConfigs[0]);
    size_t configs = 0;
    size_t pos = 0;
    const std::string key = "<ProjectConfiguration Include=\"";
    while ((pos = project.find(key, pos)) != std::string::npos)
    {
        ++configs;
        pos += key.size();
    }
    CHECK(configs == expectedCount);
    for (size_t i = 0; i < expectedCount; ++i)
    {
        const ExpectedConfig& cfg = kExpectedConfigs[i];
        INFO("configuration: " << cfg.m_condition);
        const std::string condition = std::string("<ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='") +
                                      cfg.m_condition + "'\">";
        const size_t groupPos = project.find(condition);
        REQUIRE(groupPos != std::string::npos);
        const std::string group = Between(project, groupPos, condition, "</ItemDefinitionGroup>");
        const std::string clCompile = Between(group, 0, "<ClCompile>", "</ClCompile>");
        REQUIRE_FALSE(clCompile.empty());
        const std::string definitions = Between(clCompile, 0, "<PreprocessorDefinitions>", "</PreprocessorDefinitions>");
        CHECK(HasDefinition(definitions, "WIN32"));
        CHECK(Between(clCompile, 0, "<RuntimeLibrary>", "</RuntimeLibrary>") == cfg.m_runtimeLibrary);
    }
}

TEST_CASE("BOTG3 .def 导出 FlamingoBot_Create，与接口头文件的导出名一致", "[botdll]")
{
    const std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    std::string def;
    REQUIRE(ReadWholeFile(root + "\\" + kDllDefRelativePath, def));
    const size_t exportsAt = def.find("EXPORTS");
    REQUIRE(exportsAt != std::string::npos);
    CHECK(def.find(FLAMINGO_BOT_FACTORY_NAME, exportsAt) != std::string::npos);
    CHECK(std::string(FLAMINGO_BOT_FACTORY_NAME) == "FlamingoBot_Create");
    CHECK(def.find("LIBRARY FlamingoBot") != std::string::npos);
}

TEST_CASE("BOTG4 机器人 DLL 编译的共用源文件不直接包含主程序专属头文件", "[botdll]")
{
    const std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    std::string project;
    REQUIRE(ReadWholeFile(root + "\\" + kDllProjectRelativePath, project));
    const std::vector<std::string> includes = CollectIncludes(project);
    size_t shared = 0;
    for (size_t i = 0; i < includes.size(); ++i)
    {
        bool isShared = false;
        for (size_t p = 0; p < sizeof(kSharedSourcePrefixes) / sizeof(kSharedSourcePrefixes[0]); ++p)
            isShared = isShared || StartsWith(includes[i], kSharedSourcePrefixes[p]);
        if (!isShared)
            continue;
        ++shared;
        std::string source;
        INFO("shared source: " << includes[i]);
        REQUIRE(ReadWholeFile(root + "\\FlamingoBot\\" + includes[i], source));
        const std::vector<std::string> violations = FindViolations(includes[i], source);
        for (size_t v = 0; v < violations.size(); ++v)
            UNSCOPED_INFO(violations[v]);
        CHECK(violations.empty());
    }
    //日志接口、日志格式化、多语言与 jsoncpp 至少这几份
    CHECK(shared >= 3);
}
