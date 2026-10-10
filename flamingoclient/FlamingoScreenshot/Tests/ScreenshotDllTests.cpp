/**
 *  FlamingoScreenshot.dll 的用例（docs/截图独立模块方案.md 第 4 节，2026-10-07 与用户确认）：
 *    · SG1：工程守卫——工程文件与模块目录一致（加了文件要重新运行 gen_screenshot_vcxproj.py）、四种配置都定义
 *      WIN32 并使用静态 CRT、.def 导出 FlamingoScreenshot_Create 且与接口头文件的导出名一致；
 *    · SG2：工厂收到不一致的版本号时返回 NULL；Init 收到错误的参数时返回 false；初始化前与参数非法时 Run 返回 false。
 *
 *  模块对象（Host/ScreenshotModule.cpp）与日志出口直接编进 FlamingoUnit，不加载 DLL。测试用的宿主不报告界面语言，
 *  免得模块改动单测进程的语言与 balloonui 默认字体。本文件要带 /FC 编译，__FILE__ 才是完整路径，据此找到客户端根目录。
 *  用 Catch2 v3（amalgamated）。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>

#include "../ScreenshotApi.h"
#include "../Host/ScreenshotModule.h"

namespace {

//客户端根目录（flamingoclient）的判定标志：其下有本 DLL 的工程文件
const char* const kDllProjectRelativePath = "FlamingoScreenshot\\FlamingoScreenshot.vcxproj";
const char* const kDllDefRelativePath = "FlamingoScreenshot\\FlamingoScreenshot.def";

//向上回溯查找客户端根目录时允许的最大层级数
const int kMaxParentWalkDepth = 10;

//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;

//工程按目录收集源文件的模块目录（与 gen_screenshot_vcxproj.py 的 MODULE_DIRS 相同）
const char* const kModuleDirs[] = { "Host", "Capture" };

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

//客户端根目录：先从本文件所在目录向上找，找不到再从测试程序所在目录向上找
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

//text 里从 from 起第一对 open / close 之间的内容；找不到时返回空串
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

//分号分隔的宏定义列表里有没有 name
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

bool StartsWith(const std::string& text, const std::string& prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& text, const char* suffix)
{
    const size_t n = strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

//模块目录下实际存在的 .h / .cpp（相对 FlamingoScreenshot 目录，形如 Host\ScreenshotModule.cpp）
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
    const std::string key = "Include=\"";
    size_t pos = 0;
    while ((pos = project.find(key, pos)) != std::string::npos)
    {
        pos += key.size();
        size_t end = project.find('"', pos);
        if (end == std::string::npos)
            break;
        const std::string include = project.substr(pos, end - pos);
        for (size_t d = 0; d < sizeof(kModuleDirs) / sizeof(kModuleDirs[0]); ++d)
        {
            if (StartsWith(include, std::string(kModuleDirs[d]) + "\\"))
                files.insert(include);
        }
        pos = end;
    }
    return files;
}

//SG2 用的宿主：不报告界面语言（模块因此不改动单测进程的语言与 balloonui 默认字体），其余方法什么也不做
class CSg2Host : public flamingoshot::IScreenshotHost
{
public:
    void Log(int /*level*/, const char* /*utf8*/) override
    {
    }

    const wchar_t* Translate(const wchar_t* /*context*/, const wchar_t* source) override
    {
        return source;
    }

    bool GetUiLanguage(wchar_t* /*langBuf*/, uint32_t /*langChars*/, wchar_t* /*fontBuf*/, uint32_t /*fontChars*/) override
    {
        return false;
    }

    bool IsOcrAvailable() override
    {
        return false;
    }

    bool LaunchOcrForImage(HWND /*owner*/, const uint8_t* /*bgra*/, uint32_t /*width*/, uint32_t /*height*/) override
    {
        return false;
    }
};

} // namespace

TEST_CASE("SG1 截图工程守卫：文件清单与模块目录一致、四种配置定义 WIN32 并用静态 CRT、.def 导出名与头文件一致",
          "[shotdll]")
{
    const std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    std::string project;
    REQUIRE(ReadWholeFile(root + "\\" + kDllProjectRelativePath, project));

    //文件清单：模块目录下的文件都在工程里，工程里的模块文件磁盘上都有
    const std::set<std::string> onDisk = ListModuleFiles(root + "\\FlamingoScreenshot");
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

    //四种配置：都定义 WIN32，CRT 为静态
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

    //.def：导出工厂函数，名称与接口头文件一致
    std::string def;
    REQUIRE(ReadWholeFile(root + "\\" + kDllDefRelativePath, def));
    const size_t exportsAt = def.find("EXPORTS");
    REQUIRE(exportsAt != std::string::npos);
    CHECK(def.find(FLAMINGO_SCREENSHOT_FACTORY_NAME, exportsAt) != std::string::npos);
    CHECK(std::string(FLAMINGO_SCREENSHOT_FACTORY_NAME) == "FlamingoScreenshot_Create");
    CHECK(def.find("LIBRARY FlamingoScreenshot") != std::string::npos);
}

TEST_CASE("SG2 截图模块：版本号不一致时工厂返回 NULL；Init 参数非法时返回 false；初始化前与请求非法时 Run 返回 false",
          "[shotdll]")
{
    using namespace flamingoshot;
    //工厂只认编译时的版本号
    CHECK(CreateScreenshotModule(kApiVersion + 1) == NULL);
    CHECK(CreateScreenshotModule(0) == NULL);
    IScreenshotModule* module = CreateScreenshotModule(kApiVersion);
    REQUIRE(module != NULL);
    CHECK(module == &CScreenshotModule::Instance());
    CHECK(module->GetApiVersion() == kApiVersion);
    module->Shutdown();   //单例：保证从未初始化的状态开始
    REQUIRE_FALSE(CScreenshotModule::Instance().IsInitialized());

    CSg2Host host;
    ScreenshotInitParams good;
    good.m_cbSize = sizeof(good);
    good.m_appDir = L"C:\\FlamingoUnitNoSuchDir";

    //初始化之前发起截图
    ScreenshotRequest request;
    request.m_cbSize = sizeof(request);
    request.m_owner = NULL;
    request.m_excludeHwnd = NULL;
    request.m_startFullScreen = false;
    CHECK_FALSE(module->Run(&request, NULL));
    CHECK_FALSE(module->IsRunning());

    //参数非法：宿主为空、参数为空、m_cbSize 不符、程序目录为空
    CHECK_FALSE(module->Init(NULL, &good));
    CHECK_FALSE(module->Init(&host, NULL));
    ScreenshotInitParams badSize = good;
    badSize.m_cbSize = sizeof(good) + 1;
    CHECK_FALSE(module->Init(&host, &badSize));
    ScreenshotInitParams noDir = good;
    noDir.m_appDir = NULL;
    CHECK_FALSE(module->Init(&host, &noDir));
    CHECK_FALSE(CScreenshotModule::Instance().IsInitialized());

    //参数合法：初始化成功，重复调用安全
    CHECK(module->Init(&host, &good));
    CHECK(CScreenshotModule::Instance().IsInitialized());
    CHECK(module->Init(&host, &good));

    //请求非法：为空、m_cbSize 不符（合法请求会真的弹出截图界面，这里不发）
    CHECK_FALSE(module->Run(NULL, NULL));
    ScreenshotRequest badRequest = request;
    badRequest.m_cbSize = sizeof(request) + 1;
    CHECK_FALSE(module->Run(&badRequest, NULL));
    CHECK_FALSE(module->IsRunning());

    //关闭后不可用，重复关闭安全
    module->Shutdown();
    CHECK_FALSE(CScreenshotModule::Instance().IsInitialized());
    module->Shutdown();
    CHECK_FALSE(module->Run(&request, NULL));
}
