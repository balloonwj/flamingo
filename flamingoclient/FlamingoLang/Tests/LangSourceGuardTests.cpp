/**
 *  FlamingoLang.dll 的仓库文件守卫，用例 LD14、LD17。
 *
 *  用例清单（2026-10-03 与用户确认，见 docs/多语言方案.md 第 11 节 1a）：
 *    - LD14 仓库里的四个 .po 文件（FlamingoLang/lang/）都能解析、错误数为 0，文件头里的 Language 与文件名一致
 *    - LD17 FlamingoLang.vcxproj 恰有四种配置（Debug / Release × Win32 / x64），每种配置的 C++ 编译选项
 *           （<ClCompile> 段，不是 <ResourceCompile> 段）都定义 WIN32，CRT 都是静态版
 *           （Debug 为 MultiThreadedDebug，Release 为 MultiThreaded）
 *
 *  为什么要守：
 *    - LD14：.po 由人工与脚本共同维护，格式错误的条目会被静默跳过，界面上只表现为「这一条没翻译」，很难察觉。
 *    - LD17：开发约定文档记录的易错点——仓库代码判断的是 WIN32 而不是 MSVC 的 _WIN32；CRT 必须是静态版，
 *      exe 与各 DLL 才都不依赖 VC 运行库。
 *
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径），
 *  退路是从测试程序所在目录往上找客户端根目录。框架 Catch2 v3，标签 [langdll]。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "../PoParser.h"

namespace {

//客户端根目录（flamingoclient）的判定标志：其下有本 DLL 的工程文件
const char* const kDllProjectRelativePath = "FlamingoLang\\FlamingoLang.vcxproj";

//向上回溯查找客户端根目录时允许的最大层级数（测试程序在 Debug_Win32\Tests，回到根要 2 层）
const int kMaxParentWalkDepth = 10;

//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;

//语言包随附的四个 .po 文件，文件名即语言代码
const char* const kPoLanguages[] = { "zh-TW", "en-US", "ja-JP", "ko-KR" };

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

//去掉首尾的空白
std::string Trim(const std::string& text)
{
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
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

TEST_CASE("LD14 仓库里的四个 .po 文件都能无错解析", "[langdll]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());

    for (size_t i = 0; i < sizeof(kPoLanguages) / sizeof(kPoLanguages[0]); ++i)
    {
        std::string code = kPoLanguages[i];
        std::string path = root + "\\FlamingoLang\\lang\\" + code + ".po";
        INFO("file: " << path);

        std::string text;
        REQUIRE(ReadWholeFile(path, text));

        flamingolang::PoParseResult parsed;
        flamingolang::ParsePo(text.data(), text.size(), parsed);
        INFO("first error line: " << parsed.m_firstErrorLine);
        CHECK(parsed.m_errors == 0);

        //文件头里的 Language 与文件名一致，防止复制文件后忘了改
        CHECK(text.find("\"Language: " + code + "\\n\"") != std::string::npos);
    }
}

TEST_CASE("LD17 语言包工程四种配置都定义 WIN32 并使用静态 CRT", "[langdll]")
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

        //只看其中的 <ClCompile> 段：宏写在 <ResourceCompile> 段里对 C++ 编译不生效
        std::string clCompile = Between(group, 0, "<ClCompile>", "</ClCompile>");
        REQUIRE_FALSE(clCompile.empty());
        std::string definitions = Between(clCompile, 0, "<PreprocessorDefinitions>", "</PreprocessorDefinitions>");
        CHECK(HasDefinition(definitions, "WIN32"));

        std::string runtime = Trim(Between(clCompile, 0, "<RuntimeLibrary>", "</RuntimeLibrary>"));
        CHECK(runtime == cfg.m_runtimeLibrary);
    }
}
