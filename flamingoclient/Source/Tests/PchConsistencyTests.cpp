/**
 *  PchConsistencyTests.cpp
 *
 *  主工程预编译头设置的守卫测试（标签 [pch]）。运行期解析 Flamingo.vcxproj，逐个读取
 *  设成不使用预编译头（<PrecompiledHeader>NotUsing</PrecompiledHeader>）的源文件，
 *  断言其中没有任何一个包含 stdafx.h。
 *
 *  为什么要守这一条：走预编译头的文件，编译器按 #include 的字面文本匹配 /Yu 的值，
 *  不去磁盘查找 stdafx.h；关掉预编译头之后，这一行就变成一次真实的文件查找。主工程的
 *  包含目录里没有 Source\ 本身，能找到的唯一一份 stdafx.h 是 balloonui 的那份，于是这类
 *  文件会在不报错、不告警的情况下用上另一个工程的 stdafx.h：客户端 stdafx.h 里的宏与
 *  包含项对它们缺失，balloonui 的 stdafx.h 改动反而会影响到它们。2026-10-01 修复前，
 *  BalloonHistory 下有 BalloonHistoryWindow、HistoryHighlightLabel、HistoryContentView、
 *  HistoryRow 四个文件处于这种状态。
 *
 *  只检查主工程。测试工程 FlamingoUnit 的包含目录里有 $(ProjectDir)，不走预编译头的
 *  文件包含 stdafx.h 时读到的是 Tests\stdafx.h，这是有意的设计，不在本用例范围内。
 *
 *  用例失败时的修法：要么让该文件走预编译头（去掉 vcxproj 里该条目的 NotUsing），
 *  要么从该文件里删掉 #include "stdafx.h"、改为显式包含它真正需要的头文件。
 *
 *  典型用法：FlamingoUnit.exe "[pch]"
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace {

//向上回溯查找客户端根目录时最多走的层数，够覆盖 Source\Tests 到仓库根的距离。
const int kMaxParentWalkDepth = 10;

//用来认定「这一层就是客户端根目录」的标志文件，写成相对该目录的路径。
const char* const kClientRootMarker = "Source\\Session\\UserSessionData.h";

//被检查的主工程文件，相对 Source\ 目录。
const char* const kMainProject = "Flamingo.vcxproj";

//读文件时每次读入的字节数。
const size_t kReadChunkBytes = 4096;

//UTF-8 BOM 的三个字节；源文件开头若带它，要先去掉再判断第一行。
const char kUtf8Bom[] = "\xEF\xBB\xBF";
const size_t kUtf8BomBytes = 3;

//取路径里最后一个目录分隔符的下标；两种分隔符都认。找不到返回 std::string::npos。
size_t FindLastSeparator(const std::string& path)
{
    size_t posBackslash = path.find_last_of('\\');
    size_t posSlash = path.find_last_of('/');
    if (posBackslash == std::string::npos)
        return posSlash;
    if (posSlash == std::string::npos)
        return posBackslash;
    return (posBackslash > posSlash) ? posBackslash : posSlash;
}

//去掉路径末段，返回它所在的目录。路径里没有分隔符时返回空串。
std::string GetParentDirectory(const std::string& path)
{
    size_t pos = FindLastSeparator(path);
    if (pos == std::string::npos)
        return std::string();
    return path.substr(0, pos);
}

//判断某个文件是否存在且可以按只读方式打开。
bool FileExists(const std::string& path)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

//把整个文件读进内存。读不到返回 false，内容清空。
bool ReadWholeFile(const std::string& path, std::string& content)
{
    content.clear();

    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
        return false;

    char buffer[kReadChunkBytes];
    size_t got = fread(buffer, 1, sizeof(buffer), fp);
    while (got > 0)
    {
        content.append(buffer, got);
        got = fread(buffer, 1, sizeof(buffer), fp);
    }

    fclose(fp);
    return true;
}

//从 startDir 起逐级向上找客户端根目录（含 kClientRootMarker 的那一层）。
//找不到返回空串。
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

//定位 flamingoclient\Source 目录的绝对路径，失败返回空串。
//
//首选 __FILE__：FlamingoUnit.vcxproj 单独给本文件加了 /FC，因此它在编译期就是
//绝对路径。退路是从测试 exe 自身的位置往上回溯。
std::string ResolveSourceDirectory()
{
    std::string root = WalkUpToClientRoot(GetParentDirectory(std::string(__FILE__)));
    if (!root.empty())
        return root + "\\Source";

    char moduleName[MAX_PATH];
    memset(moduleName, 0, sizeof(moduleName));
    DWORD nameLen = ::GetModuleFileNameA(NULL, moduleName, MAX_PATH);
    if (nameLen == 0 || nameLen >= MAX_PATH)
        return std::string();

    root = WalkUpToClientRoot(GetParentDirectory(std::string(moduleName)));
    if (!root.empty())
        return root + "\\Source";

    return std::string();
}

//工程里的一个编译条目。
struct CompileItem
{
    std::string m_include;   //Include 属性原文，路径相对 Source\ 目录
    bool        m_notUsing;  //该条目是否在任一配置下设成了不使用预编译头
};

//取出工程文件里全部带 Include 属性的 <ClCompile> 条目。ItemDefinitionGroup 里的
//<ClCompile>（不带 Include）是全工程默认设置，不在此列。
std::vector<CompileItem> CollectCompileItems(const std::string& xml)
{
    const std::string open = "<ClCompile Include=\"";
    const std::string close = "</ClCompile>";

    std::vector<CompileItem> items;
    size_t pos = 0;
    while (true)
    {
        size_t start = xml.find(open, pos);
        if (start == std::string::npos)
            break;

        size_t valueStart = start + open.size();
        size_t valueEnd = xml.find('"', valueStart);
        size_t elementEnd = xml.find('>', valueStart);
        if (valueEnd == std::string::npos || elementEnd == std::string::npos)
            break;

        CompileItem item;
        item.m_include = xml.substr(valueStart, valueEnd - valueStart);
        item.m_notUsing = false;

        //自闭合写法 <ClCompile Include="x" /> 没有子元素，沿用工程默认设置。
        if (xml[elementEnd - 1] == '/')
        {
            pos = elementEnd + 1;
        }
        else
        {
            size_t bodyEnd = xml.find(close, elementEnd);
            if (bodyEnd == std::string::npos)
                break;
            const std::string body = xml.substr(elementEnd + 1, bodyEnd - elementEnd - 1);
            //按配置分别设置时（带 Condition），只要有一个配置关掉了预编译头就算在内。
            item.m_notUsing =
                body.find("<PrecompiledHeader") != std::string::npos
                && body.find("NotUsing") != std::string::npos;
            pos = bodyEnd + close.size();
        }

        items.push_back(item);
    }
    return items;
}

//跳过字符串 s 从 pos 起的空格与制表符，返回第一个非空白字符的下标。
size_t SkipBlanks(const std::string& s, size_t pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
        ++pos;
    return pos;
}

//把字符串转成小写（只处理 ASCII 字母），用于不区分大小写地比较文件名。
std::string ToLowerAscii(const std::string& s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = (char)(out[i] - 'A' + 'a');
    }
    return out;
}

//判断一行是否为包含 stdafx.h 的预处理指令（引号与尖括号两种写法都认，不区分大小写）。
bool IsStdafxIncludeLine(const std::string& line)
{
    size_t pos = SkipBlanks(line, 0);
    if (pos >= line.size() || line[pos] != '#')
        return false;

    pos = SkipBlanks(line, pos + 1);
    const std::string keyword = "include";
    if (line.compare(pos, keyword.size(), keyword) != 0)
        return false;

    pos = SkipBlanks(line, pos + keyword.size());
    const std::string rest = ToLowerAscii(line.substr(pos));
    return rest.compare(0, strlen("\"stdafx.h\""), "\"stdafx.h\"") == 0
        || rest.compare(0, strlen("<stdafx.h>"), "<stdafx.h>") == 0;
}

//判断源文件内容里是否有一行（不在块注释内）包含 stdafx.h。
bool SourceIncludesStdafx(const std::string& content)
{
    std::string text = content;
    if (text.size() >= kUtf8BomBytes && text.compare(0, kUtf8BomBytes, kUtf8Bom) == 0)
        text = text.substr(kUtf8BomBytes);

    bool inBlockComment = false;
    size_t lineStart = 0;
    while (lineStart < text.size())
    {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        lineStart = lineEnd + 1;

        //块注释只做按行的粗略跟踪：注释开始的那一行与结束的那一行都整行跳过，
        //足以排除注释里举例写出的 #include 行。
        if (inBlockComment)
        {
            if (line.find("*/") != std::string::npos)
                inBlockComment = false;
            continue;
        }
        size_t open = line.find("/*");
        if (open != std::string::npos && line.find("*/", open + strlen("/*")) == std::string::npos)
        {
            inBlockComment = true;
            continue;
        }

        if (IsStdafxIncludeLine(line))
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("PCH1 主工程里不使用预编译头的源文件都不包含 stdafx.h", "[pch]")
{
    const std::string sourceDir = ResolveSourceDirectory();
    REQUIRE_FALSE(sourceDir.empty());

    std::string projXml;
    REQUIRE(ReadWholeFile(sourceDir + "\\" + kMainProject, projXml));

    //前提：工程默认走预编译头。若默认就不走，本用例检查的问题不存在，须重新审视。
    REQUIRE(projXml.find("<PrecompiledHeader>Use</PrecompiledHeader>") != std::string::npos);

    const std::vector<CompileItem> items = CollectCompileItems(projXml);
    //解析结果为空说明工程文件格式变了、解析失效，不能让用例因此空跑通过。
    REQUIRE(items.size() > 0);

    size_t notUsingCount = 0;
    std::vector<std::string> offenders;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].m_notUsing)
            continue;
        ++notUsingCount;

        std::string content;
        const bool readOk = ReadWholeFile(sourceDir + "\\" + items[i].m_include, content);
        INFO("source = " << items[i].m_include);
        CHECK(readOk);
        if (readOk && SourceIncludesStdafx(content))
            offenders.push_back(items[i].m_include);
    }
    REQUIRE(notUsingCount > 0);

    std::string offenderList;
    for (size_t i = 0; i < offenders.size(); ++i)
        offenderList += "\n  " + offenders[i];
    INFO("NotUsing sources that include stdafx.h:" << offenderList);
    //这些文件读到的不是客户端自己的 stdafx.h，见文件头说明。
    CHECK(offenders.empty());
}
