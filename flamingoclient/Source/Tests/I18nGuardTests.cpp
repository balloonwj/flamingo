/**
 *  多语言的仓库守卫用例：I18G1 ~ I18G4（方案见 docs/多语言方案.md 第 10 节）。
 *
 *    - I18G1 [i18n-scan] 扫描客户端生产代码里没有包进 TR 系列、也不在日志调用里的中文字面量。全部的条数只报告
 *            （WARN），「已改造」清单 kConvertedPaths 里的文件则必须一条都没有。第 2 期结束（2g 步）时清单改为
 *            全部生产代码。有意不翻译的字面量在同一行写 //i18n-ignore: 理由 豁免；夹在生产代码里的模拟数据用
 *            i18n-ignore-begin / i18n-ignore-end 区块豁免；只给调试入口提供模拟数据的文件列在 kExemptPaths。
 *    - I18G2 [i18n-scan] 已改造的文件里，凡有含中文文字属性的内嵌 XML，都必须调用 i18n::TranslateXml。
 *    - I18G3 [langpack]  代码里标记的文字与四个 .po 双向一致：新标记的文字都已收进 .po，.po 里也没有代码中已删除的
 *            条目。不一致说明忘了运行 python flamingoclient/tools/i18n_extract.py。
 *    - I18G4 [langpack]  每条译文与原文的 {n} 占位符集合相同、printf 格式符序列（个数、顺序、类型）相同；
 *            原文含 & 助记符时译文也含 &。各语言的翻译进度只报告。
 *
 *  源码扫描规则与提取脚本一致，见 I18nSourceScanner.h。源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件
 *  单独加了 /FC），退路是从测试程序所在目录往上找客户端根目录。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <ctype.h>
#include <stdio.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "I18nSourceScanner.h"

namespace {

//客户端根目录（flamingoclient）的判定标志：其下有语言包工程文件
const char* const kRootMarker = "FlamingoLang\\FlamingoLang.vcxproj";

//向上回溯查找客户端根目录时允许的最大层级数
const int kMaxParentWalkDepth = 10;

//读文件时每次读入的字节数
const size_t kReadChunkBytes = 4096;

//报告未标记字面量时最多列出的条数
const size_t kMaxListed = 30;

//四种语言的 .po
const char* const kPoLanguages[] = { "zh-TW", "en-US", "ja-JP", "ko-KR" };

/**
 *  已完成多语言改造的源文件或目录（相对 flamingoclient，正斜杠，目录以 / 结尾）。这里列出的文件不得再有未标记的
 *  中文字面量。每改造完一个模块就把它加进来；第 2 期结束时改为整个生产代码。
 */
const char* const kConvertedPaths[] =
{
    //第 2 期 2g 步（2026-10-03）起为主程序的全部生产代码；此前依次是 Common/I18n、1e 试点的登录窗 / 注册窗 /
    //网络设置窗。第 3 期（2026-10-04）起加上两个插件 DLL 与更新器，至此扫描范围内的生产代码全部受检。
    //锁屏 DLL（2026-10-06，docs/锁屏方案.md）新建时即按多语言规则编写，一并受检；
    //语音字幕 DLL（2026-10-06，docs/语音字幕方案.md）同样新建时即按多语言规则编写；
    //帖子群 DLL（2026-10-07，docs/帖子群方案.md）的界面文字同样新建时即按多语言规则编写；
    //截图 DLL（2026-10-07，docs/截图独立模块方案.md）的界面代码由 Source/UI/BalloonScreenshot 迁入，原本就已受检；
    //机器人 DLL（2026-10-08，docs/机器人方案.md 8.2）的界面文字新建时即按多语言规则编写
    "Source/",
    "FlamingoMedia/",
    "FlamingoFileShare/",
    "FlamingoUpdater/",
    "FlamingoLock/",
    "FlamingoCaption/",
    "FlamingoThreadGroup/",
    "FlamingoScreenshot/",
    "FlamingoBot/",
};

std::string GetParentDirectory(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}

bool FileExists(const std::string& path)
{
    return ::GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string WalkUpToClientRoot(const std::string& startDir)
{
    std::string dir = startDir;
    for (int depth = 0; depth < kMaxParentWalkDepth && !dir.empty(); ++depth)
    {
        if (FileExists(dir + "\\" + kRootMarker))
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

/**
 *  整个文件都不参与「未标记」检查的源文件：只给调试入口（--mainpanel、--chatwindow 等）提供模拟数据的文件，
 *  里面的中文是假的人名、消息与文件名，不是界面文字。夹在生产代码里的零星模拟数据用 i18n-ignore-begin / end 区块豁免。
 */
const char* const kExemptPaths[] =
{
    "Source/UI/BalloonMain/MainPanelMockData.cpp",
    "Source/UI/BalloonMain/MainPanelStressData.cpp",
    "Source/UI/BalloonChatWnd/ChatMockData.cpp",
    "Source/App/DebugEntryPoints.cpp",
    //「代码片段」效果演示窗，只由调试入口 --codesnippet 打开（界面文字与示例代码不做多语言处理）
    "Source/UI/BalloonCode/CodeSnippetDemoWindow.cpp",
    //FlamingoMedia 里只由调试入口引用、给开发者自测用的文件（第 3a 步）
    "FlamingoMedia/Call/AudioLoopback.cpp",
    "FlamingoMedia/Call/CallDebugEntries.cpp",
    "FlamingoMedia/Call/CallRelaySelfTest.cpp",
    "FlamingoMedia/Call/CallRelayTestFrame.cpp",
    "FlamingoMedia/Call/CallVideoUiDebug.cpp",
    "FlamingoMedia/Call/CameraDisplayTest.cpp",
    "FlamingoMedia/Call/CameraPreviewDebug.cpp",
    "FlamingoMedia/Call/H264SelfTest.cpp",
    "FlamingoMedia/Call/VideoLoopbackDebug.cpp",
    "FlamingoMedia/RemoteDesktop/RdDebugEntries.cpp",
    "FlamingoMedia/RemoteDesktop/RdRelaySelfTest.cpp",
    "FlamingoMedia/RemoteDesktop/RdSelfTestWindow.cpp",
    "FlamingoMedia/Share/ShareDebugEntries.cpp",
};

bool IsExempt(const std::string& rel)
{
    for (size_t k = 0; k < sizeof(kExemptPaths) / sizeof(kExemptPaths[0]); ++k)
    {
        if (rel == kExemptPaths[k])
            return true;
    }
    return false;
}

bool IsConverted(const std::string& rel)
{
    if (IsExempt(rel))
        return false;
    for (size_t k = 0; k < sizeof(kConvertedPaths) / sizeof(kConvertedPaths[0]); ++k)
    {
        std::string p = kConvertedPaths[k];
        bool isDir = !p.empty() && p[p.size() - 1] == '/';
        if (isDir ? rel.compare(0, p.size(), p) == 0 : rel == p)
            return true;
    }
    return false;
}

//「上下文 + 原文」组成的键
typedef std::pair<std::string, std::string> MessageKey;

/**
 *  .po 里的一个有效条目（不含作废条目与文件头），含译文为空的条目。
 */
struct PoMessage
{
    std::string m_context;
    std::string m_msgid;
    std::string m_msgstr;
    bool        m_review;   //是否带「待审校」注释
};

//取一行里第一对引号之间的内容并还原转义
std::string PoUnquote(const std::string& line)
{
    size_t open = line.find('"');
    size_t close = line.rfind('"');
    if (open == std::string::npos || close <= open)
        return std::string();
    std::string body = line.substr(open + 1, close - open - 1);
    std::string out;
    for (size_t i = 0; i < body.size(); ++i)
    {
        if (body[i] == '\\' && i + 1 < body.size())
        {
            char e = body[++i];
            out.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e);
        }
        else
        {
            out.push_back(body[i]);
        }
    }
    return out;
}

//读出 .po 的全部有效条目
std::vector<PoMessage> ReadPoMessages(const std::string& text)
{
    std::vector<PoMessage> result;
    std::istringstream in(text);
    std::string line;
    PoMessage cur;
    cur.m_review = false;
    bool hasMsgid = false;
    std::string* target = NULL;
    bool pendingReview = false;
    while (true)
    {
        bool more = static_cast<bool>(std::getline(in, line));
        if (more && !line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        bool blank = !more || line.find_first_not_of(" \t") == std::string::npos;
        if (blank)
        {
            if (hasMsgid && !cur.m_msgid.empty())
                result.push_back(cur);
            cur = PoMessage();
            cur.m_review = false;
            hasMsgid = false;
            target = NULL;
            pendingReview = false;
            if (!more)
                break;
            continue;
        }
        if (line.compare(0, 2, "#~") == 0)
            continue;
        if (line[0] == '#')
        {
            if (line.find("待审校") != std::string::npos)
                pendingReview = true;
            continue;
        }
        if (line.compare(0, 7, "msgctxt") == 0)
        {
            cur.m_context = PoUnquote(line);
            target = &cur.m_context;
        }
        else if (line.compare(0, 5, "msgid") == 0)
        {
            cur.m_msgid = PoUnquote(line);
            cur.m_review = pendingReview;
            hasMsgid = true;
            target = &cur.m_msgid;
        }
        else if (line.compare(0, 6, "msgstr") == 0)
        {
            cur.m_msgstr = PoUnquote(line);
            target = &cur.m_msgstr;
        }
        else if (line[0] == '"' && target != NULL)
        {
            *target += PoUnquote(line);
        }
    }
    return result;
}

//取文字里全部 {n} 占位符（n 为一位数字）
std::set<std::string> CollectPlaceholders(const std::string& text)
{
    std::set<std::string> result;
    for (size_t i = 0; i + 2 < text.size(); ++i)
    {
        if (text[i] == '{' && text[i + 1] >= '0' && text[i + 1] <= '9' && text[i + 2] == '}')
            result.insert(text.substr(i, 3));
    }
    return result;
}

/**
 *  取文字里 printf 风格格式符的序列（如 "%d" "%s" "%02d" "%.1f"），"%%" 不计入。存量代码的格式串保留 printf 写法、
 *  直接包进 TR，译文必须按原样的个数、顺序与类型使用格式符，否则运行时参数会被按错误的类型读取。
 */
std::vector<std::string> CollectPrintfSpecs(const std::string& text)
{
    std::vector<std::string> specs;
    const std::string flags = "-+ #0";
    const std::string lengths = "hlLIjzt0123456789";
    const std::string types = "diouxXeEfgGcsSpaA";
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '%')
            continue;
        if (i + 1 < text.size() && text[i + 1] == '%')
        {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < text.size() && flags.find(text[j]) != std::string::npos)
            ++j;
        while (j < text.size() && (isdigit((unsigned char)text[j]) || text[j] == '.' || text[j] == '*'))
            ++j;
        while (j < text.size() && lengths.find(text[j]) != std::string::npos)
            ++j;
        if (j < text.size() && types.find(text[j]) != std::string::npos)
        {
            specs.push_back(text.substr(i, j - i + 1));
            i = j;
        }
    }
    return specs;
}

/**
 *  扫描全部生产代码，返回 相对路径 -> 扫描结果。一千多个文件扫一遍在 Debug 下要二十来秒，三条用例共用一次结果：
 *  第一次调用时扫描并缓存，之后直接返回缓存（同一进程内源码不会变）。
 */
const std::map<std::string, i18nscan::FileScanResult>& ScanAll(const std::string& root)
{
    static std::map<std::string, i18nscan::FileScanResult> s_results;
    static bool s_scanned = false;
    if (s_scanned)
        return s_results;
    s_scanned = true;
    std::map<std::string, i18nscan::FileScanResult>& results = s_results;
    std::vector<std::string> files = i18nscan::ListSourceFiles(root);
    for (size_t i = 0; i < files.size(); ++i)
    {
        std::string path = root + "\\" + files[i];
        for (size_t k = 0; k < path.size(); ++k)
        {
            if (path[k] == '/')
                path[k] = '\\';
        }
        std::string text;
        if (!ReadWholeFile(path, text))
            continue;
        i18nscan::ScanSource(text, results[files[i]]);
    }
    return results;
}

} // namespace

TEST_CASE("I18G1 已改造的文件里没有未标记的中文字面量", "[i18n-scan]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::map<std::string, i18nscan::FileScanResult>& results = ScanAll(root);
    REQUIRE(results.size() > 100);

    size_t total = 0;
    size_t files = 0;
    std::ostringstream violations;
    size_t violationCount = 0;
    for (std::map<std::string, i18nscan::FileScanResult>::const_iterator it = results.begin(); it != results.end(); ++it)
    {
        const std::vector<i18nscan::UnmarkedLiteral>& unmarked = it->second.m_unmarked;
        if (unmarked.empty())
            continue;
        total += unmarked.size();
        ++files;
        if (!IsConverted(it->first))
            continue;
        for (size_t k = 0; k < unmarked.size(); ++k)
        {
            ++violationCount;
            if (violationCount <= kMaxListed)
                violations << it->first << ":" << unmarked[k].m_line << "  " << unmarked[k].m_text << "\n";
        }
    }
    WARN("unmarked Chinese literals in production code: " << total << " in " << files << " files");
    INFO("unmarked literals in converted files (first " << kMaxListed << "):\n" << violations.str());
    CHECK(violationCount == 0);
}

TEST_CASE("I18G2 已改造文件里的内嵌 XML 都经过 TranslateXml", "[i18n-scan]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::map<std::string, i18nscan::FileScanResult>& results = ScanAll(root);
    for (std::map<std::string, i18nscan::FileScanResult>::const_iterator it = results.begin(); it != results.end(); ++it)
    {
        if (!IsConverted(it->first) || !it->second.m_hasXmlText)
            continue;
        INFO("file: " << it->first);
        CHECK(it->second.m_callsTranslateXml);
    }
}

TEST_CASE("I18G3 代码里标记的文字与 .po 一致", "[langpack]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::map<std::string, i18nscan::FileScanResult>& results = ScanAll(root);

    std::set<MessageKey> inCode;
    for (std::map<std::string, i18nscan::FileScanResult>::const_iterator it = results.begin(); it != results.end(); ++it)
    {
        for (size_t k = 0; k < it->second.m_marked.size(); ++k)
            inCode.insert(MessageKey(it->second.m_marked[k].m_context, it->second.m_marked[k].m_source));
    }

    for (size_t l = 0; l < sizeof(kPoLanguages) / sizeof(kPoLanguages[0]); ++l)
    {
        std::string path = root + "\\FlamingoLang\\lang\\" + kPoLanguages[l] + ".po";
        INFO("file: " << path);
        std::string text;
        REQUIRE(ReadWholeFile(path, text));
        std::vector<PoMessage> messages = ReadPoMessages(text);
        std::set<MessageKey> inPo;
        for (size_t k = 0; k < messages.size(); ++k)
            inPo.insert(MessageKey(messages[k].m_context, messages[k].m_msgid));

        std::ostringstream missing;
        size_t missingCount = 0;
        for (std::set<MessageKey>::const_iterator it = inCode.begin(); it != inCode.end(); ++it)
        {
            if (inPo.count(*it) == 0 && ++missingCount <= kMaxListed)
                missing << "[" << it->first << "] " << it->second << "\n";
        }
        std::ostringstream stale;
        size_t staleCount = 0;
        for (std::set<MessageKey>::const_iterator it = inPo.begin(); it != inPo.end(); ++it)
        {
            if (inCode.count(*it) == 0 && ++staleCount <= kMaxListed)
                stale << "[" << it->first << "] " << it->second << "\n";
        }
        INFO("marked in code but missing from .po (run tools/i18n_extract.py):\n" << missing.str());
        INFO("in .po but no longer in code (run tools/i18n_extract.py):\n" << stale.str());
        CHECK(missingCount == 0);
        CHECK(staleCount == 0);
    }
}

TEST_CASE("I18G4 译文保留占位符与助记符", "[langpack]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    for (size_t l = 0; l < sizeof(kPoLanguages) / sizeof(kPoLanguages[0]); ++l)
    {
        std::string path = root + "\\FlamingoLang\\lang\\" + kPoLanguages[l] + ".po";
        std::string text;
        REQUIRE(ReadWholeFile(path, text));
        std::vector<PoMessage> messages = ReadPoMessages(text);

        size_t translated = 0;
        size_t review = 0;
        for (size_t k = 0; k < messages.size(); ++k)
        {
            const PoMessage& m = messages[k];
            if (m.m_msgstr.empty())
                continue;
            ++translated;
            if (m.m_review)
                ++review;
            INFO(kPoLanguages[l] << ": [" << m.m_context << "] " << m.m_msgid << " -> " << m.m_msgstr);
            CHECK(CollectPlaceholders(m.m_msgid) == CollectPlaceholders(m.m_msgstr));
            CHECK(CollectPrintfSpecs(m.m_msgid) == CollectPrintfSpecs(m.m_msgstr));
            if (m.m_msgid.find('&') != std::string::npos)
                CHECK(m.m_msgstr.find('&') != std::string::npos);
        }
        WARN(kPoLanguages[l] << ": " << translated << " of " << messages.size() << " translated, " << review
             << " awaiting review");
    }
}
