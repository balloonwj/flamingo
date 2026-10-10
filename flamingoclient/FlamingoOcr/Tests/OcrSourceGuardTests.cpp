/**
 *  FlamingoOcr.dll 的工程守卫，用例 OCRG1、OCRG2（docs/图片文字识别方案.md 第 10.1 节）。
 *
 *    - OCRG1 FlamingoOcr.vcxproj 恰有四种配置（Debug / Release × Win32 / x64），每种配置的 C++ 编译选项都定义
 *            WIN32、使用静态 CRT（Debug 为 MultiThreadedDebug，Release 为 MultiThreaded）、带 /utf-8；
 *            工程与 DLL 源码都没有链接 WinRT 的导入库（runtimeobject.lib、windowsapp.lib）
 *    - OCRG2 已编译出的 FlamingoOcr.dll（四个输出目录里存在的那些）导入表里没有 combase.dll，
 *            也没有 api-ms-win-core-winrt-* —— 有了它们，DLL 在 Win7 上连加载都会失败
 *
 *  为什么要守：
 *    - OCRG1：仓库代码判断的是 WIN32 而不是 MSVC 的 _WIN32；CRT 必须是静态版，exe 与各 DLL 才都不依赖 VC 运行库；
 *      缺了 /utf-8 时中文注释会被当成 GBK，可能吞掉下一行代码。
 *    - OCRG2：Windows.Media.Ocr 是 Win10 才有的接口，只要有一处代码直接调用了 WinRT 函数（或链接了它的导入库），
 *      编译照样通过，问题却要到 Win7 用户那里才暴露，表现为「识别入口没有出现」，很难察觉。
 *
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径），
 *  退路是从测试程序所在目录往上找客户端根目录。框架 Catch2 v3，标签 [ocrdll]。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {

//客户端根目录（flamingoclient）的判定标志：其下有本 DLL 的工程文件
const char* const kDllProjectRelativePath = "FlamingoOcr\\FlamingoOcr.vcxproj";

//向上回溯查找客户端根目录时允许的最大层级数（测试程序在 Debug_Win32\Tests，回到根要 2 层）
const int kMaxParentWalkDepth = 10;

//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;

//DLL 的四个输出位置（相对客户端根目录），与 FlamingoOcr.vcxproj 的 OutDir 一致
const char* const kDllOutputs[] =
{
    "Debug\\FlamingoOcr.dll",                //Debug|x64
    "Release\\FlamingoOcr.dll",              //Release|x64
    "Debug_Win32\\FlamingoOcr.dll",          //Debug|Win32
    "Release_Win32\\FlamingoOcr.dll",        //Release|Win32
};

//DLL 源码所在的子目录（相对 FlamingoOcr 目录），检查其中有没有 #pragma comment(lib, ...) 链接 WinRT 导入库
const char* const kSourceDirs[] = { "", "Core\\", "WinOcr\\" };

//不允许出现的 WinRT 导入库名（小写比较）
const char* const kForbiddenLibs[] = { "runtimeobject", "windowsapp" };

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

//转小写（只处理 ASCII）
std::string ToLower(std::string text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] >= 'A' && text[i] <= 'Z')
            text[i] = static_cast<char>(text[i] - 'A' + 'a');
    }
    return text;
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

//text 里是否出现了任一禁止的 WinRT 导入库名
bool MentionsForbiddenLib(const std::string& text)
{
    const std::string lower = ToLower(text);
    for (size_t i = 0; i < sizeof(kForbiddenLibs) / sizeof(kForbiddenLibs[0]); ++i)
    {
        if (lower.find(kForbiddenLibs[i]) != std::string::npos)
            return true;
    }
    return false;
}

//把 RVA 换算成文件偏移；不落在任何节里时返回 false
bool RvaToOffset(const std::string& image, size_t sectionTable, unsigned sectionCount, DWORD rva, size_t& offset)
{
    for (unsigned i = 0; i < sectionCount; ++i)
    {
        size_t pos = sectionTable + i * sizeof(IMAGE_SECTION_HEADER);
        if (pos + sizeof(IMAGE_SECTION_HEADER) > image.size())
            return false;
        const IMAGE_SECTION_HEADER* sec = reinterpret_cast<const IMAGE_SECTION_HEADER*>(image.data() + pos);
        DWORD span = (std::max)(sec->Misc.VirtualSize, sec->SizeOfRawData);
        if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + span)
        {
            offset = static_cast<size_t>(rva - sec->VirtualAddress + sec->PointerToRawData);
            return offset < image.size();
        }
    }
    return false;
}

/**
 *  读出 PE 文件的导入 DLL 名（转小写）。32 位与 64 位的 PE 都能解析，不加载 DLL。
 *  @return 解析成功返回 true
 */
bool ReadImportedDllNames(const std::string& image, std::vector<std::string>& names)
{
    names.clear();
    if (image.size() < sizeof(IMAGE_DOS_HEADER))
        return false;
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    size_t ntPos = static_cast<size_t>(dos->e_lfanew);
    if (ntPos + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + sizeof(WORD) > image.size())
        return false;
    if (*reinterpret_cast<const DWORD*>(image.data() + ntPos) != IMAGE_NT_SIGNATURE)
        return false;

    const IMAGE_FILE_HEADER* fileHeader = reinterpret_cast<const IMAGE_FILE_HEADER*>(image.data() + ntPos + sizeof(DWORD));
    size_t optPos = ntPos + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    WORD magic = *reinterpret_cast<const WORD*>(image.data() + optPos);

    DWORD importRva = 0;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        if (optPos + sizeof(IMAGE_OPTIONAL_HEADER32) > image.size())
            return false;
        const IMAGE_OPTIONAL_HEADER32* opt = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(image.data() + optPos);
        importRva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    }
    else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        if (optPos + sizeof(IMAGE_OPTIONAL_HEADER64) > image.size())
            return false;
        const IMAGE_OPTIONAL_HEADER64* opt = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(image.data() + optPos);
        importRva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    }
    else
    {
        return false;
    }
    if (importRva == 0)
        return true;

    size_t sectionTable = optPos + fileHeader->SizeOfOptionalHeader;
    size_t descPos = 0;
    if (!RvaToOffset(image, sectionTable, fileHeader->NumberOfSections, importRva, descPos))
        return false;

    for (;;)
    {
        if (descPos + sizeof(IMAGE_IMPORT_DESCRIPTOR) > image.size())
            return false;
        const IMAGE_IMPORT_DESCRIPTOR* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image.data() + descPos);
        if (desc->Name == 0)
            break;
        size_t namePos = 0;
        if (!RvaToOffset(image, sectionTable, fileHeader->NumberOfSections, desc->Name, namePos))
            return false;
        size_t nameEnd = image.find('\0', namePos);
        if (nameEnd == std::string::npos)
            return false;
        names.push_back(ToLower(image.substr(namePos, nameEnd - namePos)));
        descPos += sizeof(IMAGE_IMPORT_DESCRIPTOR);
    }
    return true;
}

} // namespace

TEST_CASE("OCRG1 识别引擎工程四种配置都定义 WIN32、使用静态 CRT、带 /utf-8，且不链接 WinRT 导入库", "[ocrdll]")
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

        std::string condition = std::string("<ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='")
                                + cfg.m_condition + "'\">";
        size_t groupPos = project.find(condition);
        REQUIRE(groupPos != std::string::npos);
        std::string group = Between(project, groupPos, condition, "</ItemDefinitionGroup>");
        REQUIRE_FALSE(group.empty());

        std::string clCompile = Between(group, 0, "<ClCompile>", "</ClCompile>");
        REQUIRE_FALSE(clCompile.empty());
        std::string definitions = Between(clCompile, 0, "<PreprocessorDefinitions>", "</PreprocessorDefinitions>");
        CHECK(HasDefinition(definitions, "WIN32"));

        std::string runtime = Trim(Between(clCompile, 0, "<RuntimeLibrary>", "</RuntimeLibrary>"));
        CHECK(runtime == cfg.m_runtimeLibrary);

        std::string options = Between(clCompile, 0, "<AdditionalOptions>", "</AdditionalOptions>");
        CHECK(options.find("/utf-8") != std::string::npos);

        std::string link = Between(group, 0, "<Link>", "</Link>");
        CHECK_FALSE(MentionsForbiddenLib(Between(link, 0, "<AdditionalDependencies>", "</AdditionalDependencies>")));
    }

    //源码里也不得用 #pragma comment(lib, ...) 偷偷链接 WinRT 导入库
    for (size_t d = 0; d < sizeof(kSourceDirs) / sizeof(kSourceDirs[0]); ++d)
    {
        std::string dir = root + "\\FlamingoOcr\\" + kSourceDirs[d];
        WIN32_FIND_DATAA fd;
        HANDLE find = ::FindFirstFileA((dir + "*.*").c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            std::string name = fd.cFileName;
            std::string lower = ToLower(name);
            bool isSource = (lower.size() > 4 && (lower.rfind(".cpp") == lower.size() - 4))
                            || (lower.size() > 2 && (lower.rfind(".h") == lower.size() - 2));
            if (!isSource || (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                continue;
            std::string text;
            REQUIRE(ReadWholeFile(dir + name, text));
            size_t pos = 0;
            while ((pos = text.find("#pragma comment", pos)) != std::string::npos)
            {
                size_t end = text.find('\n', pos);
                std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                INFO("file: " << dir << name << ", line: " << line);
                CHECK_FALSE(MentionsForbiddenLib(line));
                pos += 1;
            }
        } while (::FindNextFileA(find, &fd));
        ::FindClose(find);
    }
}

TEST_CASE("OCRG2 编译出的识别引擎 DLL 不静态导入任何 WinRT 函数", "[ocrdll]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());

    size_t checked = 0;
    for (size_t i = 0; i < sizeof(kDllOutputs) / sizeof(kDllOutputs[0]); ++i)
    {
        std::string path = root + "\\" + kDllOutputs[i];
        if (!FileExists(path))
            continue;
        INFO("dll: " << path);

        std::string image;
        REQUIRE(ReadWholeFile(path, image));
        std::vector<std::string> names;
        REQUIRE(ReadImportedDllNames(image, names));
        CHECK_FALSE(names.empty());   //至少导入了 kernel32，解析为空说明解析逻辑有误
        for (size_t n = 0; n < names.size(); ++n)
        {
            INFO("imported: " << names[n]);
            CHECK(names[n] != "combase.dll");
            CHECK(names[n].find("api-ms-win-core-winrt") != 0);
        }
        ++checked;
    }
    INFO("没有找到任何已编译的 FlamingoOcr.dll，请先编译 FlamingoOcr 工程");
    CHECK(checked > 0);
}
