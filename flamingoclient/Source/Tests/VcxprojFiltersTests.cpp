/**
 *  VcxprojFiltersTests.cpp
 *
 *  工程分组文件（.vcxproj.filters）的守卫测试。运行期解析 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 各自的 .filters，断言三件事：工程里的每个条目在分组文件
 *  里都有一条对应记录；分组文件里没有工程已经删掉的残留条目；每个条目所在的分组
 *  与它在磁盘上的目录一致。
 *
 *  这组用例守的是一类不报错、不影响编译、只影响可读性的漂移：.filters 只决定
 *  「解决方案资源管理器」里的目录树长什么样，加了文件却忘了同步它，文件在 VS 里
 *  就根本看不见。2026-08-05 曾出现过 vcxproj 有 773 个源文件、filters 里只有 245 个
 *  的局面；2026-08-18 重排目录之前也还有 21 个条目没有分组、散落在工程根节点下。
 *
 *  分组规则与 flamingoclient\tools\gen_vcxproj_filters.py 保持一致：
 *    1. 位于子目录的文件按目录路径分组，目录层级原样映射成分组层级；
 *    2. 路径以 .. 开头的（测试工程引用被测代码就是这种写法）先去掉全部 ..\ 前缀，
 *       再按剩下的目录分组；
 *    3. 位于工程目录下的文件，主工程不分组、测试工程统一归入 Tests 组。
 *
 *  用例失败时的修法是重跑生成器，而不是手工去改 .filters：
 *      cd flamingoclient\tools
 *      python gen_vcxproj_filters.py
 *
 *  典型用法：FlamingoUnit.exe "[filters]"
 *
 *  balloonwj@qq.com   2026-08-18
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>

#include <map>
#include <string>
#include <vector>

namespace {

//向上回溯查找客户端根目录时最多走的层数，够覆盖 Source\Tests 到仓库根的距离。
const int kMaxParentWalkDepth = 10;

//用来认定「这一层就是客户端根目录」的标志文件，写成相对该目录的路径。
const char* const kClientRootMarker = "Source\\Session\\UserSessionData.h";

//一个被守卫的工程。
struct ProjectUnderGuard
{
    const char* m_projectPath;   //工程文件路径，相对 Source\ 目录
    const char* m_rootFilter;    //工程目录下的文件该进的分组；NULL 表示不分组
};

//被守卫的两个工程。分组规则与生成器里的 TARGETS 表一一对应。
const ProjectUnderGuard kGuardedProjects[] =
{
    { "Flamingo.vcxproj", NULL },
    { "Tests\\FlamingoUnit.vcxproj", "Tests" },
};

const size_t kGuardedProjectCount =
    sizeof(kGuardedProjects) / sizeof(kGuardedProjects[0]);

//会出现在 .filters 里的条目类型。ProjectReference / ProjectConfiguration 是
//工程文件独有的，不参与分组，解析时要排除掉。
const char* const kItemTypes[] =
{
    "ClCompile", "ClInclude", "ResourceCompile", "Image", "Text", "None",
};

const size_t kItemTypeCount = sizeof(kItemTypes) / sizeof(kItemTypes[0]);

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

    char buffer[4096];
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
//绝对路径。退路是从测试 exe 自身的位置往上回溯 —— 万一将来 /FC 被去掉，用例仍能
//找到源码，而不是静默失去守卫能力。
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

//把路径里的正斜杠统一成反斜杠，便于逐字比较。
std::string NormalizeSeparators(const std::string& path)
{
    std::string out = path;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == '/')
            out[i] = '\\';
    }
    return out;
}

//按分组规则算出某个条目应当落在哪个分组。返回空串表示不分组、直接挂在工程节点下。
std::string ExpectedFilter(const std::string& include, const char* rootFilter)
{
    std::string path = NormalizeSeparators(include);

    //去掉全部 ..\ 前缀：测试工程引用被测代码用的就是这种写法，
    //去掉之后正好与主工程里的分组同名。
    while (path.size() >= 3 && path.compare(0, 3, "..\\") == 0)
        path = path.substr(3);

    std::string directory = GetParentDirectory(path);
    if (!directory.empty())
        return directory;

    return (rootFilter != NULL) ? std::string(rootFilter) : std::string();
}

//从 xml 里取出某个标签的全部 Include 属性值。
//
//  xml     文件内容
//  tag     条目类型名，如 "ClCompile"
//  out     追加输出 [(Include 路径, 该条目的 <Filter> 内容)]；没有 <Filter> 子元素
//          时第二项为空串
void CollectItems(const std::string& xml, const char* tag,
                  std::vector<std::pair<std::string, std::string> >& out)
{
    const std::string open = std::string("<") + tag;
    const std::string close = std::string("</") + tag + ">";

    size_t pos = 0;
    while (true)
    {
        size_t start = xml.find(open, pos);
        if (start == std::string::npos)
            break;

        //紧跟标签名的必须是空白，否则 <ClCompile> 会误匹配到 <ClCompileXxx>。
        size_t afterTag = start + open.size();
        if (afterTag >= xml.size()
            || (xml[afterTag] != ' ' && xml[afterTag] != '\t'
                && xml[afterTag] != '\r' && xml[afterTag] != '\n'))
        {
            pos = afterTag;
            continue;
        }

        size_t attrPos = xml.find("Include=\"", afterTag);
        size_t elementEnd = xml.find('>', afterTag);
        if (attrPos == std::string::npos || elementEnd == std::string::npos
            || attrPos > elementEnd)
        {
            pos = afterTag;
            continue;
        }

        size_t valueStart = attrPos + strlen("Include=\"");
        size_t valueEnd = xml.find('"', valueStart);
        if (valueEnd == std::string::npos)
            break;

        std::string include = xml.substr(valueStart, valueEnd - valueStart);
        std::string filter;

        //自闭合写法 <ClCompile Include="x" /> 没有 <Filter> 子元素。
        if (xml[elementEnd - 1] != '/')
        {
            size_t bodyEnd = xml.find(close, elementEnd);
            if (bodyEnd != std::string::npos)
            {
                std::string body = xml.substr(elementEnd + 1, bodyEnd - elementEnd - 1);
                size_t filterStart = body.find("<Filter>");
                size_t filterEnd = body.find("</Filter>");
                if (filterStart != std::string::npos && filterEnd != std::string::npos
                    && filterEnd > filterStart)
                {
                    filterStart += strlen("<Filter>");
                    filter = body.substr(filterStart, filterEnd - filterStart);
                }
            }
            pos = (bodyEnd == std::string::npos) ? elementEnd : bodyEnd + close.size();
        }
        else
        {
            pos = elementEnd + 1;
        }

        out.push_back(std::make_pair(include, filter));
    }
}

//取出一个工程 / 分组文件里的全部条目。
std::vector<std::pair<std::string, std::string> > CollectAllItems(const std::string& xml)
{
    std::vector<std::pair<std::string, std::string> > out;
    for (size_t i = 0; i < kItemTypeCount; ++i)
        CollectItems(xml, kItemTypes[i], out);
    return out;
}

} // namespace

TEST_CASE("FLT1 每个工程条目在分组文件里都有一条对应记录", "[filters]")
{
    const std::string sourceDir = ResolveSourceDirectory();
    REQUIRE_FALSE(sourceDir.empty());

    for (size_t i = 0; i < kGuardedProjectCount; ++i)
    {
        const std::string projPath = sourceDir + "\\" + kGuardedProjects[i].m_projectPath;
        const std::string filtersPath = projPath + ".filters";

        INFO("project = " << kGuardedProjects[i].m_projectPath);

        std::string projXml;
        std::string filtersXml;
        REQUIRE(ReadWholeFile(projPath, projXml));
        REQUIRE(ReadWholeFile(filtersPath, filtersXml));

        const std::vector<std::pair<std::string, std::string> > projItems =
            CollectAllItems(projXml);
        const std::vector<std::pair<std::string, std::string> > filterItems =
            CollectAllItems(filtersXml);

        std::map<std::string, std::string> filterByInclude;
        for (size_t k = 0; k < filterItems.size(); ++k)
            filterByInclude[NormalizeSeparators(filterItems[k].first)] = filterItems[k].second;

        REQUIRE(projItems.size() > 0);

        for (size_t k = 0; k < projItems.size(); ++k)
        {
            const std::string include = NormalizeSeparators(projItems[k].first);
            INFO("include = " << include);
            //漏登记的表现是该文件在解决方案资源管理器里完全看不见。
            CHECK(filterByInclude.find(include) != filterByInclude.end());
        }
    }
}

TEST_CASE("FLT2 分组文件里没有工程已经删掉的残留条目", "[filters]")
{
    const std::string sourceDir = ResolveSourceDirectory();
    REQUIRE_FALSE(sourceDir.empty());

    for (size_t i = 0; i < kGuardedProjectCount; ++i)
    {
        const std::string projPath = sourceDir + "\\" + kGuardedProjects[i].m_projectPath;
        const std::string filtersPath = projPath + ".filters";

        INFO("project = " << kGuardedProjects[i].m_projectPath);

        std::string projXml;
        std::string filtersXml;
        REQUIRE(ReadWholeFile(projPath, projXml));
        REQUIRE(ReadWholeFile(filtersPath, filtersXml));

        const std::vector<std::pair<std::string, std::string> > projItems =
            CollectAllItems(projXml);
        const std::vector<std::pair<std::string, std::string> > filterItems =
            CollectAllItems(filtersXml);

        std::map<std::string, bool> inProject;
        for (size_t k = 0; k < projItems.size(); ++k)
            inProject[NormalizeSeparators(projItems[k].first)] = true;

        for (size_t k = 0; k < filterItems.size(); ++k)
        {
            const std::string include = NormalizeSeparators(filterItems[k].first);
            INFO("include = " << include);
            //残留条目会在资源管理器里显示成一个打不开的文件。
            CHECK(inProject.find(include) != inProject.end());
        }
    }
}

TEST_CASE("FLT3 每个条目所在的分组与它在磁盘上的目录一致", "[filters]")
{
    const std::string sourceDir = ResolveSourceDirectory();
    REQUIRE_FALSE(sourceDir.empty());

    for (size_t i = 0; i < kGuardedProjectCount; ++i)
    {
        const std::string filtersPath =
            sourceDir + "\\" + kGuardedProjects[i].m_projectPath + ".filters";

        INFO("project = " << kGuardedProjects[i].m_projectPath);

        std::string filtersXml;
        REQUIRE(ReadWholeFile(filtersPath, filtersXml));

        const std::vector<std::pair<std::string, std::string> > filterItems =
            CollectAllItems(filtersXml);
        REQUIRE(filterItems.size() > 0);

        for (size_t k = 0; k < filterItems.size(); ++k)
        {
            const std::string include = NormalizeSeparators(filterItems[k].first);
            const std::string expected =
                ExpectedFilter(include, kGuardedProjects[i].m_rootFilter);

            INFO("include = " << include);
            INFO("expected filter = " << expected);
            INFO("actual filter = " << filterItems[k].second);
            //分组与目录对不上，说明有人手工改过 .filters 而没有重跑生成器。
            CHECK(NormalizeSeparators(filterItems[k].second) == expected);
        }
    }
}

TEST_CASE("FLT4 分组文件为每一级父分组都写了定义", "[filters]")
{
    const std::string sourceDir = ResolveSourceDirectory();
    REQUIRE_FALSE(sourceDir.empty());

    for (size_t i = 0; i < kGuardedProjectCount; ++i)
    {
        const std::string filtersPath =
            sourceDir + "\\" + kGuardedProjects[i].m_projectPath + ".filters";

        INFO("project = " << kGuardedProjects[i].m_projectPath);

        std::string filtersXml;
        REQUIRE(ReadWholeFile(filtersPath, filtersXml));

        //收集全部 <Filter Include="..."> 定义。
        std::map<std::string, bool> defined;
        size_t pos = 0;
        while (true)
        {
            size_t start = filtersXml.find("<Filter Include=\"", pos);
            if (start == std::string::npos)
                break;
            size_t valueStart = start + strlen("<Filter Include=\"");
            size_t valueEnd = filtersXml.find('"', valueStart);
            if (valueEnd == std::string::npos)
                break;
            defined[filtersXml.substr(valueStart, valueEnd - valueStart)] = true;
            pos = valueEnd;
        }

        const std::vector<std::pair<std::string, std::string> > filterItems =
            CollectAllItems(filtersXml);

        for (size_t k = 0; k < filterItems.size(); ++k)
        {
            const std::string filter = NormalizeSeparators(filterItems[k].second);
            if (filter.empty())
                continue;

            //VS 要求 A、A\B、A\B\C 每一级都有定义，缺了哪一级，那一支就整个不显示。
            size_t sep = filter.find('\\');
            while (sep != std::string::npos)
            {
                const std::string parent = filter.substr(0, sep);
                INFO("filter = " << filter);
                INFO("missing parent = " << parent);
                CHECK(defined.find(parent) != defined.end());
                sep = filter.find('\\', sep + 1);
            }

            INFO("filter = " << filter);
            CHECK(defined.find(filter) != defined.end());
        }
    }
}
