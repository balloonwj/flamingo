/**
 *  PC 客户端各 exe / dll 版本信息的守卫用例，FileVersionInfoTests.cpp（标签 [verinfo]）。
 *
 *    - VER1 已编译出的 15 个文件（四个输出目录 Debug、Release、Debug_Win32、Release_Win32 里存在的那些）都带版本资源：
 *           文件说明非空、没有版权一项、产品版本与 ClientVersion.h 一致、原始文件名与实际文件名一致
 *    - VER2 15 个工程都把各自的 <名称>Version.rc 登记为资源文件并带 /c65001；这些 .rc 是 CRLF 换行、包含共用模板
 *           Source/FileVersionInfo.rc2、定义了文件说明与原始文件名；同一工程的其他 .rc 里没有版本块；模板本身是 CRLF
 *           换行，不写版权（2026-10-10 按用户要求去掉了原来的 cppguide.cn，模板里不得再出现这个字样）
 *
 *  为什么要守：
 *    - 版本信息是给人在文件属性里查看的，缺了或写错不影响任何功能，编译也照样通过，只能靠用例发现；
 *    - rc.exe 只认 CRLF 换行，.rc 被改成 LF 后整个文件会被当成一行、开头的 // 注释吞掉全部内容，版本信息悄无声息地消失；
 *    - 同一个文件里出现两个版本块（例如有人在 flamingo.rc 里重新加回 VERSIONINFO）时链接报资源重复；
 *    - 新增插件 DLL 时容易忘记加版本信息，VER2 的工程表与 tools/version_rc.py 的 PROJECTS 表一一对应。
 *
 *  客户端根目录按 __FILE__ 或测试程序所在目录逐级向上查找（以 FlamingoClient.sln 为标志）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-09
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "../ClientVersion.h"   // FLAMINGO_CLIENT_VERSION_RC_STR：版本资源里的产品版本应与它一致

#pragma comment(lib, "version.lib")   // GetFileVersionInfoW / VerQueryValueW

namespace {

//客户端根目录（flamingoclient）的判定标志
const char* const kClientRootMarker = "FlamingoClient.sln";

//向上回溯查找客户端根目录时允许的最大层级数（测试程序在 Debug_Win32\Tests，回到根要 2 层）
const int kMaxParentWalkDepth = 10;

//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;

//已去掉的旧版权字样：共用模板里不得再出现（2026-10-10 按用户要求去掉）
const char* const kRemovedCopyright = "cppguide";

//共用模板相对客户端根目录的位置
const char* const kTemplateRelativePath = "Source\\FileVersionInfo.rc2";

//四个输出目录（相对客户端根目录），与各工程的 OutDir 一致
const char* const kOutputDirs[] = { "Debug", "Release", "Debug_Win32", "Release_Win32" };

//一个 exe / dll 的工程与产物信息，与 tools/version_rc.py 的 PROJECTS 表一一对应
struct VersionedFile
{
    const char* m_projectDir;   //工程目录（相对客户端根目录），版本 .rc 也在这里
    const char* m_projectFile;  //工程文件名
    const char* m_name;         //产物文件名（不含扩展名），版本 .rc 名为 <m_name>Version.rc
    const char* m_ext;          //产物扩展名：exe 或 dll
};

const VersionedFile kFiles[] =
{
    { "Source",               "Flamingo.vcxproj",             "Flamingo",             "exe" },
    { "FlamingoUpdater",      "FlamingoUpdater.vcxproj",      "FlamingoUpdater",      "exe" },
    { "FlamingoMedia",        "FlamingoMedia.vcxproj",        "FlamingoMedia",        "dll" },
    { "FlamingoP2P",          "FlamingoP2P.vcxproj",          "FlamingoP2P",          "dll" },
    { "FlamingoLang",         "FlamingoLang.vcxproj",         "FlamingoLang",         "dll" },
    { "FlamingoOcr",          "FlamingoOcr.vcxproj",          "FlamingoOcr",          "dll" },
    { "FlamingoMarkdown",     "FlamingoMarkdown.vcxproj",     "FlamingoMarkdown",     "dll" },
    { "FlamingoLock",         "FlamingoLock.vcxproj",         "FlamingoLock",         "dll" },
    { "FlamingoScreenshot",   "FlamingoScreenshot.vcxproj",   "FlamingoScreenshot",   "dll" },
    { "FlamingoFileShare",    "FlamingoFileShare.vcxproj",    "FlamingoFileShare",    "dll" },
    { "FlamingoSecretChat",   "FlamingoSecretChat.vcxproj",   "FlamingoSecretChat",   "dll" },
    { "FlamingoThreadGroup",  "FlamingoThreadGroup.vcxproj",  "FlamingoThreadGroup",  "dll" },
    { "FlamingoBot",          "FlamingoBot.vcxproj",          "FlamingoBot",          "dll" },
    { "FlamingoCaption",      "FlamingoCaption.vcxproj",      "FlamingoCaption",      "dll" },
    { "FlamingoCaptionModel", "FlamingoCaptionModel.vcxproj", "FlamingoCaptionModel", "dll" },
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
        if (FileExists(dir + "\\" + kClientRootMarker))
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

//文件内容里是否有不带 \r 的换行（rc.exe 只认 CRLF）
bool HasBareLineFeed(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r'))
            return true;
    }
    return false;
}

//ASCII 字符串转宽字符串（只用于文件名、版本号这类纯 ASCII 文本）
std::wstring AsciiToWide(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

//从版本资源里读一个字符串字段；读不到返回空串。language 为 \VarFileInfo\Translation 的第一项（语言 + 代码页）
std::wstring QueryVersionString(const std::vector<BYTE>& data, DWORD language, const wchar_t* field)
{
    wchar_t key[MAX_PATH] = { 0 };
    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", LOWORD(language), HIWORD(language), field);
    LPVOID value = NULL;
    UINT chars = 0;
    if (!::VerQueryValueW(&data[0], key, &value, &chars) || value == NULL || chars == 0)
        return std::wstring();
    return std::wstring(static_cast<const wchar_t*>(value));
}

//读出一个文件的整块版本资源与它的第一种语言；没有版本资源时返回 false
bool LoadVersionInfo(const std::wstring& path, std::vector<BYTE>& data, DWORD& language)
{
    DWORD handle = 0;
    DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0)
        return false;
    data.assign(size, 0);
    if (!::GetFileVersionInfoW(path.c_str(), 0, size, &data[0]))
        return false;
    LPVOID translation = NULL;
    UINT bytes = 0;
    if (!::VerQueryValueW(&data[0], L"\\VarFileInfo\\Translation", &translation, &bytes) || bytes < sizeof(DWORD))
        return false;
    language = *static_cast<const DWORD*>(translation);
    return true;
}

//取工程文件里 <ResourceCompile Include="xxx"> 条目的文件名（可带或不带子元素）
std::vector<std::string> ResourceFilesOf(const std::string& vcxproj)
{
    std::vector<std::string> files;
    const std::string tag = "<ResourceCompile Include=\"";
    size_t pos = 0;
    while ((pos = vcxproj.find(tag, pos)) != std::string::npos)
    {
        pos += tag.size();
        size_t end = vcxproj.find('"', pos);
        if (end == std::string::npos)
            break;
        files.push_back(vcxproj.substr(pos, end - pos));
        pos = end;
    }
    return files;
}

//取工程文件里某个 ResourceCompile 条目从开始标签到 </ResourceCompile>（或自闭合的 />）之间的文本
std::string ResourceItemText(const std::string& vcxproj, const std::string& rcFile)
{
    const std::string start = "<ResourceCompile Include=\"" + rcFile + "\"";
    size_t pos = vcxproj.find(start);
    if (pos == std::string::npos)
        return std::string();
    size_t selfClose = vcxproj.find("/>", pos);
    size_t close = vcxproj.find("</ResourceCompile>", pos);
    size_t open = vcxproj.find('>', pos);
    //开始标签本身就以 /> 结束：没有子元素
    if (selfClose != std::string::npos && selfClose < open + 1)
        return vcxproj.substr(pos, selfClose + 2 - pos);
    if (close == std::string::npos)
        return std::string();
    return vcxproj.substr(pos, close - pos);
}

} // namespace

TEST_CASE("VER1 已编译出的各 exe / dll 都带版本信息：文件说明、产品版本、原始文件名，没有版权一项", "[verinfo]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());

    const std::wstring expectedVersion = AsciiToWide(FLAMINGO_CLIENT_VERSION_RC_STR);
    size_t checked = 0;
    for (size_t d = 0; d < sizeof(kOutputDirs) / sizeof(kOutputDirs[0]); ++d)
    {
        for (size_t i = 0; i < sizeof(kFiles) / sizeof(kFiles[0]); ++i)
        {
            const std::string fileName = std::string(kFiles[i].m_name) + "." + kFiles[i].m_ext;
            const std::string path = root + "\\" + kOutputDirs[d] + "\\" + fileName;
            if (!FileExists(path))
                continue;
            INFO("file: " << path);

            std::vector<BYTE> data;
            DWORD language = 0;
            REQUIRE(LoadVersionInfo(AsciiToWide(path), data, language));
            CHECK_FALSE(QueryVersionString(data, language, L"FileDescription").empty());
            CHECK(QueryVersionString(data, language, L"LegalCopyright").empty());
            CHECK(QueryVersionString(data, language, L"ProductVersion") == expectedVersion);
            CHECK(QueryVersionString(data, language, L"OriginalFilename") == AsciiToWide(fileName));
            ++checked;
        }
    }
    //一个产物都没有时（只编了单元测试工程）照常通过，但留下提示
    if (checked == 0)
        WARN("no client exe / dll found in the four output directories, nothing checked");
}

TEST_CASE("VER2 各工程都登记版本 .rc（/c65001），.rc 为 CRLF 并包含共用模板，其他 .rc 里没有版本块", "[verinfo]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());

    //共用模板：CRLF 换行，不写版权，也不再出现旧的版权字样
    std::string tmpl;
    REQUIRE(ReadWholeFile(root + "\\" + kTemplateRelativePath, tmpl));
    CHECK_FALSE(HasBareLineFeed(tmpl));
    CHECK(tmpl.find(kRemovedCopyright) == std::string::npos);
    CHECK(tmpl.find("LegalCopyright") == std::string::npos);
    CHECK(tmpl.find("VERSIONINFO") != std::string::npos);

    for (size_t i = 0; i < sizeof(kFiles) / sizeof(kFiles[0]); ++i)
    {
        const VersionedFile& f = kFiles[i];
        const std::string projectDir = root + "\\" + f.m_projectDir;
        const std::string rcName = std::string(f.m_name) + "Version.rc";
        INFO("project: " << f.m_projectFile);

        std::string vcxproj;
        REQUIRE(ReadWholeFile(projectDir + "\\" + f.m_projectFile, vcxproj));
        const std::string item = ResourceItemText(vcxproj, rcName);
        CHECK_FALSE(item.empty());
        CHECK(item.find("/c65001") != std::string::npos);

        std::string rc;
        REQUIRE(ReadWholeFile(projectDir + "\\" + rcName, rc));
        CHECK_FALSE(HasBareLineFeed(rc));
        CHECK(rc.find("#include \"..\\Source\\FileVersionInfo.rc2\"") != std::string::npos);
        CHECK(rc.find("#define FLAMINGO_FILE_DESCRIPTION    \"FlamingoIM ") != std::string::npos);
        CHECK(rc.find(std::string("#define FLAMINGO_FILE_ORIGINAL_NAME  \"") + f.m_name + "." + f.m_ext + "\"")
              != std::string::npos);

        //同一工程登记的其他 .rc 里不能再有版本块，否则链接时版本资源重复
        std::vector<std::string> others = ResourceFilesOf(vcxproj);
        for (size_t k = 0; k < others.size(); ++k)
        {
            if (others[k] == rcName)
                continue;
            INFO("other resource file: " << others[k]);
            std::string text;
            REQUIRE(ReadWholeFile(projectDir + "\\" + others[k], text));
            CHECK(text.find("VERSIONINFO") == std::string::npos);
        }
    }
}
