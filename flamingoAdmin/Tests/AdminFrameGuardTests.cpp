/**
 *  flamingoAdmin —— 主窗口 AdminFrame 的源码守卫用例。
 *
 *  AdminFrame 体量大、依赖全部界面，不在测试工程里编译，其中一些约定没法用常规
 *  单元测试覆盖，只能在运行期读它的源码检查写法：
 *    - TG1a：扫描函数本身能识别出违规写法（防止扫描规则失效后守卫静默通过）；
 *    - TG1 ：把服务端返回的错误文字弹成 Toast 时必须用 ToastKind::Error。早先
 *            34 处写成 ShowToast(r.m_message)，落到默认的 Info 样式，失败提示
 *            显示成绿色对勾，与「操作成功」无法区分。
 *
 *    - PG1 ：公告页、审计页的分页改用共用的 ui::PagerBar，分页文字只由
 *            PagerBar::FormatPageText 生成（早先公告页写「第 X / Y 页」、审计页与企业列表
 *            写「第 X / 共 Y 页」，三处各画一份）。读两个界面的源码，确认不再自己拼分页文字。
 *
 *    - RSP3：接口标签（Util/AdminApi.h 的 kApiTag*）与控件编号（UiIds.h 的 kId*）都是手工分配的数值，
 *            写重了编译期不报错，只表现为请求结果或控件通知被别处截走。读两个头文件，确认没有重复的值。
 *
 *  源文件位置：首选 __FILE__（flamingoAdminTests.vcxproj 给本文件单独加了 /FC，
 *  编译期即为绝对路径）；取不到时从测试程序所在目录逐级向上查找。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Widgets/PagerBar.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

// AdminFrame.cpp 的最少行数。读到的行数低于它说明找错了文件或文件被截断，
// 守卫应当判失败，而不是对着一个空文件报告「没有违规」。
const size_t kMinAdminFrameLines = 1000;

// 向上查找源码目录时最多回溯的层数。测试程序可能在 Bin\x64\ 或 tmp\<任务>\out\
// 下运行，距仓库根都在 4 层以内，取 8 层留足余量。
const int kMaxWalkUpLevels = 8;

// 取路径的父目录（去掉最后一个反斜杠或斜杠及其后的部分）。没有分隔符时返回空串。
//   path：文件或目录的路径。
std::string GetParentDirectory(const std::string& path)
{
    const size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
    {
        return std::string();
    }
    return path.substr(0, pos);
}

// 判断文件是否存在且不是目录。
//   path：文件路径（ANSI）。
bool FileExists(const std::string& path)
{
    const DWORD attr = ::GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 从 startDir 起逐级向上查找 AdminFrame.cpp：每一级依次检查「<目录>\AdminFrame.cpp」
// 与「<目录>\flamingoAdmin\AdminFrame.cpp」。找到返回完整路径，找不到返回空串。
//   startDir：起始目录。
std::string WalkUpToAdminFrame(std::string startDir)
{
    for (int level = 0; level < kMaxWalkUpLevels && !startDir.empty(); ++level)
    {
        const std::string direct = startDir + "\\AdminFrame.cpp";
        if (FileExists(direct))
        {
            return direct;
        }
        const std::string nested = startDir + "\\flamingoAdmin\\AdminFrame.cpp";
        if (FileExists(nested))
        {
            return nested;
        }
        startDir = GetParentDirectory(startDir);
    }
    return std::string();
}

// 定位 AdminFrame.cpp 的绝对路径，失败返回空串。先按 __FILE__（本文件在
// Tests\ 下，上一级即 flamingoAdmin\），再按测试程序自身的位置。
std::string ResolveAdminFramePath()
{
    std::string found = WalkUpToAdminFrame(GetParentDirectory(std::string(__FILE__)));
    if (!found.empty())
    {
        return found;
    }

    char moduleName[MAX_PATH] = { 0 };
    const DWORD nameLen = ::GetModuleFileNameA(NULL, moduleName, MAX_PATH);
    if (nameLen == 0 || nameLen >= MAX_PATH)
    {
        return std::string();
    }
    return WalkUpToAdminFrame(GetParentDirectory(std::string(moduleName)));
}

// 读入整个文本文件（按字节，不做编码转换）。失败返回 false。
//   path：文件路径；out：输出的文件内容。
bool ReadWholeFile(const std::string& path, std::string& out)
{
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in)
    {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// 扫描源码文本，找出「把服务端错误文字弹成 Toast 却没用 Error 样式」的行。
// 判定为违规的两种写法：
//   一、ShowToast(<变量>.m_message)：不带样式参数，落到默认的 Info；
//   二、ShowToast(<变量>.m_message, ToastKind::Info 或 ToastKind::Warn)。
// m_message 是各 ParseXxxResponse 结果里「失败时给用户看的中文提示」，出现在
// ShowToast 的第一个参数位置就意味着这是一条失败提示。
//   text：源码全文（UTF-8 字节）。
//   lineCount：输出参数，文本的总行数，供调用方判断是否读到了完整文件。
//   返回：违规所在的行号（从 1 起），没有违规时为空。
std::vector<int> FindApiErrorToastViolations(const std::string& text, size_t& lineCount)
{
    static const std::regex kNoKind("ShowToast\\(\\s*\\w+\\.m_message\\s*\\)");
    static const std::regex kWrongKind(
        "ShowToast\\(\\s*\\w+\\.m_message\\s*,\\s*ToastKind::(Info|Warn)\\s*\\)");

    std::vector<int> violations;
    std::istringstream lines(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(lines, line))
    {
        ++lineNo;
        if (std::regex_search(line, kNoKind) || std::regex_search(line, kWrongKind))
        {
            violations.push_back(lineNo);
        }
    }
    lineCount = static_cast<size_t>(lineNo);
    return violations;
}

// 把违规行号拼成「12, 34, 56」形式，供失败信息展示。
//   lines：行号列表。
CString JoinLineNumbers(const std::vector<int>& lines)
{
    CString joined;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        CString one;
        one.Format(i == 0 ? _T("%d") : _T(", %d"), lines[i]);
        joined += one;
    }
    return joined;
}

// TG1a：扫描函数能识别两种违规写法，且不误报正确写法与成功提示。
void TestScannerRecognizesViolations()
{
    BeginCase("TG1a 扫描函数识别违规的失败提示写法");

    const std::string sample =
        "ShowToast(r.m_message);\n"                               // 第 1 行：违规，缺样式
        "ShowToast(result.m_message, ToastKind::Warn);\n"         // 第 2 行：违规，样式不对
        "ShowToast(r.m_message, ToastKind::Error);\n"             // 第 3 行：正确
        "ShowToast(_T(\"已保存\"));\n"                             // 第 4 行：成功提示，不涉及
        "ShowToast( login.m_message ,ToastKind::Info );\n"        // 第 5 行：违规，带空白
        "FA_LOGW(_T(\"x %s\"), r.m_message.GetString());\n";      // 第 6 行：日志，不涉及

    size_t lineCount = 0;
    const std::vector<int> v = FindApiErrorToastViolations(sample, lineCount);
    FA_EXPECT_EQ(lineCount, 6);
    FA_EXPECT_EQ(v.size(), 3);
    if (v.size() == 3)
    {
        FA_EXPECT_EQ(v[0], 1);
        FA_EXPECT_EQ(v[1], 2);
        FA_EXPECT_EQ(v[2], 5);
    }
}

// TG1：AdminFrame.cpp 里所有服务端错误提示都用 ToastKind::Error。
void TestAdminFrameUsesErrorToastForApiFailures()
{
    BeginCase("TG1 AdminFrame 的接口失败提示一律使用错误样式");

    const std::string path = ResolveAdminFramePath();
    FA_EXPECT_FALSE(path.empty());
    if (path.empty())
    {
        return;
    }

    std::string text;
    FA_EXPECT_TRUE(ReadWholeFile(path, text));

    size_t lineCount = 0;
    const std::vector<int> v = FindApiErrorToastViolations(text, lineCount);
    FA_EXPECT_TRUE(lineCount >= kMinAdminFrameLines);

    CString detail;
    detail.Format(_T("AdminFrame.cpp 中以下行的失败提示未使用 ToastKind::Error：%s"),
                  JoinLineNumbers(v).GetString());
    Report(v.empty(), "AdminFrame.cpp has no API error toast without ToastKind::Error",
           detail, __FILE__, __LINE__);
}

// PG1：分页文字统一由 PagerBar 生成，公告页与审计页不再自己拼。
void TestScreensUseSharedPager()
{
    BeginCase("PG1 公告页、审计页改用共用分页条，分页文字统一为「第 X / 共 Y 页」");

    FA_EXPECT_STREQ(ui::PagerBar::FormatPageText(2, 5), _T("第 2 / 共 5 页"));
    FA_EXPECT_STREQ(ui::PagerBar::FormatPageText(1, 1), _T("第 1 / 共 1 页"));

    // 两个界面的源码与 AdminFrame.cpp 同在 flamingoAdmin\ 下的 Screens\ 目录。
    const std::string framePath = ResolveAdminFramePath();
    FA_EXPECT_FALSE(framePath.empty());
    if (framePath.empty())
    {
        return;
    }
    const std::string screensDir = GetParentDirectory(framePath) + "\\Screens\\";
    const char* const kScreens[] = { "AnnouncementsScreen.cpp", "AuditScreen.cpp" };
    // 分页文字的特征：格式串以「第 %d」开头。UTF-8 字节写成转义，避免依赖源文件编码。
    const std::string kPageFormat = "\xE7\xAC\xAC %d";
    for (size_t i = 0; i < sizeof(kScreens) / sizeof(kScreens[0]); ++i)
    {
        std::string text;
        const bool read = ReadWholeFile(screensDir + kScreens[i], text);
        FA_EXPECT_TRUE(read);
        if (!read)
        {
            continue;
        }
        FA_EXPECT_TRUE(text.find(kPageFormat) == std::string::npos);
        FA_EXPECT_TRUE(text.find("ui::PagerBar") != std::string::npos);
    }
}

// 从源码文本里取出「名字 = 数值」形式的枚举值，名字须以 prefix 开头（如 kApiTag、kId）。
//   text：源码全文；prefix：名字前缀。
//   返回：(名字, 数值) 列表，按出现顺序。
std::vector<std::pair<std::string, int> > CollectEnumValues(const std::string& text, const std::string& prefix)
{
    std::vector<std::pair<std::string, int> > out;
    const std::regex re("^\\s*(" + prefix + "\\w*)\\s*=\\s*(\\d+)");
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
    {
        std::smatch m;
        if (std::regex_search(line, m, re))
        {
            out.push_back(std::make_pair(m[1].str(), std::stoi(m[2].str())));
        }
    }
    return out;
}

// 找出取值重复的名字，拼成「名字=值」的列表；没有重复时返回空串。
//   values：CollectEnumValues 的结果。
std::string DescribeDuplicates(const std::vector<std::pair<std::string, int> >& values)
{
    std::string dups;
    for (size_t i = 0; i < values.size(); ++i)
    {
        for (size_t j = i + 1; j < values.size(); ++j)
        {
            if (values[i].second == values[j].second)
            {
                dups += values[i].first + "/" + values[j].first + "=" + std::to_string(values[i].second) + " ";
            }
        }
    }
    return dups;
}

// RSP3：接口标签与控件编号没有重复的值。
void TestApiTagsAndUiIdsUnique()
{
    BeginCase("RSP3 接口标签（ApiTag）与控件编号（UiIds.h）没有重复的值");

    const std::string framePath = ResolveAdminFramePath();
    FA_EXPECT_FALSE(framePath.empty());
    if (framePath.empty())
    {
        return;
    }
    const std::string rootDir = GetParentDirectory(framePath);

    std::string apiText;
    const bool apiRead = ReadWholeFile(rootDir + "\\Util\\AdminApi.h", apiText);
    FA_EXPECT_TRUE(apiRead);
    std::string idText;
    const bool idRead = ReadWholeFile(rootDir + "\\UiIds.h", idText);
    FA_EXPECT_TRUE(idRead);
    if (!apiRead || !idRead)
    {
        return;
    }

    const std::vector<std::pair<std::string, int> > tags = CollectEnumValues(apiText, "kApiTag");
    const std::vector<std::pair<std::string, int> > ids = CollectEnumValues(idText, "kId");
    //确认确实解析到了（防止解析规则失效后守卫静默通过），并且新加的几项都在
    FA_EXPECT_TRUE(tags.size() > 50);
    FA_EXPECT_TRUE(ids.size() > 100);
    FA_EXPECT_TRUE(apiText.find("kApiTagRegisterSwitchSet") != std::string::npos);
    FA_EXPECT_TRUE(idText.find("kIdFeatSwitchRegister") != std::string::npos);

    const std::string tagDups = DescribeDuplicates(tags);
    const std::string idDups = DescribeDuplicates(ids);
    FA_EXPECT_TRUE(tagDups.empty());
    FA_EXPECT_TRUE(idDups.empty());
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllAdminFrameGuardTests()
{
    TestScannerRecognizesViolations();
    TestAdminFrameUsesErrorToastForApiFailures();
    TestScreensUseSharedPager();
    TestApiTagsAndUiIdsUnique();
}

}} // namespace flamingoadmin::test
