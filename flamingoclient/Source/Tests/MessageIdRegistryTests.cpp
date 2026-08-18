/**
 *  MessageIdRegistryTests.cpp
 *
 *  客户端自定义窗口消息号的守卫测试。运行期读取四个消息定义头文件
 *  （Source\Session\UserSessionData.h、Source\App\CustomMsgDef.h、
 *   Source\UI\BalloonChatWnd\BalloonChatWindow.h、
 *   Source\UI\BalloonChatWnd\BalloonGroupChatWindow.h），把其中形如
 *  「#define NAME WM_USER + n」与「static const UINT kName = WM_APP + n」
 *  的定义全部解析出来，断言三件事：同一头文件内部不得有两条消息取同一个号；
 *  跨头文件取同号必须落在白名单里并写明理由；UserSessionData.h 里的消息号
 *  都落在 FMG_MSG_FIRST / FMG_MSG_LAST 这道代理窗收件闸门区间之内。
 *
 *  这组用例守的是一类没有任何编译期检查的错误：消息号是手工分配的宏，写重了
 *  既不报错也不崩溃，只表现为某个功能点了没反应 —— ATL 消息映射把消息派发给
 *  先注册的那个处理器，排在后面的处理器永远得不到执行。本仓库已经因此出过多次
 *  故障（群聊 @ 菜单走进 Pin 的处理函数、系统设置里的链接开关静默失效等），
 *  故用测试把约束固定下来。
 *
 *  典型用法：FlamingoUnit.exe "[msgid]"
 *
 *  balloonwj@qq.com   2026-08-16
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

//向上回溯查找客户端根目录时允许的最大层级数。实测最深的一条起点是测试 exe 所在的
//flamingoclient\Bin\Tests\<平台>\<配置>\，回到 flamingoclient\ 需要 5 层，取 10 留余量。
const int kMaxParentWalkDepth = 10;

//用来认定「这一层就是客户端根目录」的标志文件，写成相对该目录的路径。
const char* const kClientRootMarker = "Source\\Session\\UserSessionData.h";

//代理窗收件闸门的两个哨兵宏名。它们不是可投递的消息，参与不了重号判定，
//只用来给 UserSessionData.h 里的消息号划定合法区间。
const char* const kGateFirstName = "FMG_MSG_FIRST";
const char* const kGateLastName = "FMG_MSG_LAST";

//DescribeValue 拼字符串用的栈缓冲区大小，够放下 "WM_USER+4294967295" 这种最长形态。
const size_t kDescribeBufferSize = 64;

//一个被解析出来的消息定义。
struct MessageDefinition
{
    std::string  m_fileKey;        //定义所在头文件的短名，与 kGuardedHeaders 的 m_fileKey 一致
    std::string  m_name;           //消息符号名（宏名或类内常量名）
    unsigned int m_value;          //展开后的绝对消息号（WM_USER / WM_APP 基址已经加进去）
    int          m_line;           //定义所在行号，从 1 起，仅用于失败时定位
    bool         m_isGateSentinel; //是不是 FMG_MSG_FIRST / FMG_MSG_LAST 这两个闸门哨兵

    MessageDefinition()
        : m_value(0)
        , m_line(0)
        , m_isGateSentinel(false)
    {
    }
};

//一个被守卫的头文件。
struct HeaderUnderGuard
{
    const char* m_relativePath;  //相对 Source\ 的路径
    const char* m_fileKey;       //失败信息与豁免表里使用的短名
};

//被守卫的四个头文件。客户端所有以 WM_USER 为基址的自定义消息都定义在这四处。
//
//说明：BalloonLoginDlg.h / BalloonHistoryWindow.h 等另有若干 WM_APP 段的窗口私有
//消息，它们各自只在一个窗口内部收发、彼此撞号也无害，故不纳入本组守卫；纳入的话
//白名单会长到失去意义。
const HeaderUnderGuard kGuardedHeaders[] =
{
    { "Session\\UserSessionData.h", "UserSessionData.h" },
    { "App\\CustomMsgDef.h", "CustomMsgDef.h" },
    { "UI\\BalloonChatWnd\\BalloonChatWindow.h", "BalloonChatWindow.h" },
    { "UI\\BalloonChatWnd\\BalloonGroupChatWindow.h", "BalloonGroupChatWindow.h" },
};

const size_t kGuardedHeaderCount = sizeof(kGuardedHeaders) / sizeof(kGuardedHeaders[0]);

//——文件内重号的豁免表——
//
//每一行都是「当前确实存在、尚未整改完」的重号。整改推进过程中逐条删掉，
//全部删净后表里只剩结束标记那一行（m_reason 取 NULL）。留结束标记是为了让
//数组永远不退化成零长度数组 —— C++ 不允许零长度数组。
//
//新增行必须写明理由：没有理由的豁免等于把守卫关掉。
struct InFileDuplicateExemption
{
    const char*  m_fileKey;  //头文件短名
    unsigned int m_value;    //被豁免的绝对消息号
    const char*  m_reason;   //当前允许它重复的理由；结束标记行取 NULL
};

const InFileDuplicateExemption kInFileDuplicateExemptions[] =
{
    //结束标记。开工时存量的五处重号（UserSessionData.h 的 472 / 474 / 477 / 552，
    //以及 CustomMsgDef.h 的 WM_USER + 1）已于 2026-08-16 整改完毕，本表随之清空。
    { NULL, 0, NULL },
};

//——跨文件重号的白名单——
//
//跨文件取同号本身不一定是缺陷：只要两条消息的收件窗口不同，Windows 就不会把它们
//混起来。列进本表即表示「已确认收件窗口不同、故允许」，同样必须写明理由。
struct CrossFileDuplicateAllowance
{
    unsigned int m_value;   //允许跨文件重复的绝对消息号
    const char*  m_reason;  //允许的理由；结束标记行取 NULL
};

const CrossFileDuplicateAllowance kCrossFileDuplicateAllowances[] =
{
    //结束标记。两个聊天窗原先在 WM_USER + 0x601 起逐个同号，2026-08-16 已整体挪到
    //WM_APP 段（单聊窗 0x40 起、群聊窗 0x50 起），本表随之清空。
    { 0, NULL },
};

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

//从 startDir 起逐级向上找客户端根目录（含 Source\UserSessionData.h 的那一层）。
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

//把绝对消息号还原成人看得懂的形态，如 "WM_USER+472" / "WM_APP+64"。
std::string DescribeValue(unsigned int value)
{
    char buffer[kDescribeBufferSize];
    memset(buffer, 0, sizeof(buffer));
    if (value >= (unsigned int)WM_APP)
        sprintf(buffer, "WM_APP+%u", value - (unsigned int)WM_APP);
    else
        sprintf(buffer, "WM_USER+%u", value - (unsigned int)WM_USER);
    return std::string(buffer);
}

//跳过空格与制表符。
void SkipSpaces(const std::string& text, size_t& pos)
{
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;
}

//若 text 在 pos 处正好是 literal，则吃掉它并返回 true；否则 pos 不动、返回 false。
bool MatchLiteral(const std::string& text, size_t& pos, const char* literal)
{
    if (pos > text.size())
        return false;

    size_t len = strlen(literal);
    if (text.compare(pos, len, literal) != 0)
        return false;

    pos += len;
    return true;
}

//标识符允许的字符：字母、数字、下划线。
bool IsIdentifierChar(char c)
{
    if (c >= 'a' && c <= 'z')
        return true;
    if (c >= 'A' && c <= 'Z')
        return true;
    if (c >= '0' && c <= '9')
        return true;
    return c == '_';
}

//读一个标识符；一个字符都读不到时返回 false。
bool ReadIdentifier(const std::string& text, size_t& pos, std::string& out)
{
    size_t start = pos;
    while (pos < text.size() && IsIdentifierChar(text[pos]))
        ++pos;

    if (pos == start)
        return false;

    out = text.substr(start, pos - start);
    return true;
}

//解析形如 "WM_USER + 472"、"(WM_APP + 0x40)" 的取值表达式，把绝对消息号写进 value。
//识别不出来返回 false，调用方据此跳过该行。
bool ParseBaseExpression(const std::string& text, size_t pos, unsigned int& value)
{
    SkipSpaces(text, pos);

    //宏定义两种写法都有：带括号的 (WM_APP + 1) 与不带括号的 WM_USER + 472。
    MatchLiteral(text, pos, "(");
    SkipSpaces(text, pos);

    unsigned int base = 0;
    if (MatchLiteral(text, pos, "WM_USER"))
        base = (unsigned int)WM_USER;
    else if (MatchLiteral(text, pos, "WM_APP"))
        base = (unsigned int)WM_APP;
    else
        return false;

    SkipSpaces(text, pos);
    if (!MatchLiteral(text, pos, "+"))
        return false;

    SkipSpaces(text, pos);
    if (pos >= text.size())
        return false;

    //基数取 0，与 C 预处理器一致地识别 0x 十六进制、前导零的八进制与十进制。
    //本仓库里 FMG_MSG_FIRST 写的就是前导零形态（WM_USER + 001）。
    const char* begin = text.c_str() + pos;
    char* end = NULL;
    long offset = strtol(begin, &end, 0);
    if (end == begin)
        return false;

    value = base + (unsigned int)offset;
    return true;
}

//解析一行文本，识别出其中的消息定义。识别不出来返回 false。
//
//支持两种写法：
//  一、#define FMG_MSG_XXX  WM_USER + 472
//  二、static const UINT kRelayoutMsg = WM_APP + 0x40;（聊天窗把消息号写成类内常量）
//
//被注释掉的定义（行首是 //）自然不匹配，无需另作处理。
bool ParseLine(const std::string& line, std::string& name, unsigned int& value)
{
    size_t pos = 0;
    SkipSpaces(line, pos);

    //写法一：宏定义
    if (MatchLiteral(line, pos, "#define"))
    {
        SkipSpaces(line, pos);
        if (!ReadIdentifier(line, pos, name))
            return false;
        return ParseBaseExpression(line, pos, value);
    }

    //写法二：类内静态常量
    pos = 0;
    SkipSpaces(line, pos);
    if (!MatchLiteral(line, pos, "static"))
        return false;
    SkipSpaces(line, pos);
    if (!MatchLiteral(line, pos, "const"))
        return false;
    SkipSpaces(line, pos);
    if (!MatchLiteral(line, pos, "UINT"))
        return false;
    SkipSpaces(line, pos);
    if (!ReadIdentifier(line, pos, name))
        return false;
    SkipSpaces(line, pos);
    if (!MatchLiteral(line, pos, "="))
        return false;

    return ParseBaseExpression(line, pos, value);
}

//读一个头文件，把其中的消息定义追加进 out。文件打不开返回 false。
bool ParseHeaderFile(const std::string& path, const char* fileKey, std::vector<MessageDefinition>& out)
{
    std::ifstream fin(path.c_str(), std::ios::binary);
    if (!fin.is_open())
        return false;

    std::string line;
    int lineNo = 0;
    while (std::getline(fin, line))
    {
        ++lineNo;

        //以二进制方式读，Windows 换行会在行尾留下 '\r'，去掉免得干扰解析。
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        std::string name;
        unsigned int value = 0;
        if (!ParseLine(line, name, value))
            continue;

        MessageDefinition def;
        def.m_fileKey = fileKey;
        def.m_name = name;
        def.m_value = value;
        def.m_line = lineNo;
        def.m_isGateSentinel = (name == kGateFirstName || name == kGateLastName);
        out.push_back(def);
    }
    return true;
}

//读入四个被守卫头文件里的全部消息定义。
//
//任何一个头文件打不开、或解析不出任何定义，都直接让用例失败 —— 静默跳过等于守卫
//失效，而「守卫悄悄不干活」正是本组用例要根治的那类问题。
void LoadAllDefinitions(std::vector<MessageDefinition>& out)
{
    std::string sourceDir = ResolveSourceDirectory();
    if (sourceDir.empty())
    {
        FAIL("cannot locate flamingoclient Source directory; __FILE__ = " << __FILE__);
        return;
    }

    for (size_t i = 0; i < kGuardedHeaderCount; ++i)
    {
        std::string path = sourceDir + "\\" + kGuardedHeaders[i].m_relativePath;
        std::vector<MessageDefinition> entries;
        if (!ParseHeaderFile(path, kGuardedHeaders[i].m_fileKey, entries))
        {
            FAIL("cannot open message id header: " << path);
            return;
        }
        if (entries.empty())
        {
            FAIL("no message definition parsed from: " << path);
            return;
        }
        out.insert(out.end(), entries.begin(), entries.end());
    }
}

//查文件内重号豁免表。
bool IsInFileDuplicateExempted(const std::string& fileKey, unsigned int value)
{
    for (size_t i = 0; kInFileDuplicateExemptions[i].m_reason != NULL; ++i)
    {
        if (fileKey == kInFileDuplicateExemptions[i].m_fileKey
            && value == kInFileDuplicateExemptions[i].m_value)
        {
            return true;
        }
    }
    return false;
}

//查跨文件重号白名单。
bool IsCrossFileDuplicateAllowed(unsigned int value)
{
    for (size_t i = 0; kCrossFileDuplicateAllowances[i].m_reason != NULL; ++i)
    {
        if (value == kCrossFileDuplicateAllowances[i].m_value)
            return true;
    }
    return false;
}

//在已解析出的定义里按名字找一条，找不到返回 NULL。
const MessageDefinition* FindByName(const std::vector<MessageDefinition>& defs, const char* name)
{
    for (size_t i = 0; i < defs.size(); ++i)
    {
        if (defs[i].m_name == name)
            return &defs[i];
    }
    return NULL;
}

} // namespace

//守卫能不能干活，先由这条用例兜底：四个头文件必须都读得到、都解析得出定义。
//它失败通常意味着源码目录定位失败或某个头文件被改名 / 挪走了。
TEST_CASE("消息号守卫：四个消息定义头文件都能读到并解析出消息定义", "[msgid]")
{
    std::vector<MessageDefinition> defs;
    LoadAllDefinitions(defs);
    REQUIRE(defs.size() > 0);

    for (size_t i = 0; i < kGuardedHeaderCount; ++i)
    {
        size_t count = 0;
        for (size_t k = 0; k < defs.size(); ++k)
        {
            if (defs[k].m_fileKey == kGuardedHeaders[i].m_fileKey)
                ++count;
        }

        INFO("header = " << kGuardedHeaders[i].m_relativePath);
        REQUIRE(count > 0);
    }
}

//同一个头文件里两条消息取同一个号，几乎必然是缺陷：这类文件按「收件方」组织，
//同文件的消息往往投给同一个窗口，撞号会让靠后注册的处理器永远收不到消息。
TEST_CASE("消息号守卫：同一头文件内部的消息号不得重复", "[msgid]")
{
    std::vector<MessageDefinition> defs;
    LoadAllDefinitions(defs);

    for (size_t i = 0; i < defs.size(); ++i)
    {
        if (defs[i].m_isGateSentinel)
            continue;

        for (size_t k = i + 1; k < defs.size(); ++k)
        {
            if (defs[k].m_isGateSentinel)
                continue;
            if (defs[i].m_fileKey != defs[k].m_fileKey)
                continue;
            if (defs[i].m_value != defs[k].m_value)
                continue;
            if (IsInFileDuplicateExempted(defs[i].m_fileKey, defs[i].m_value))
                continue;

            FAIL_CHECK("duplicated message id inside " << defs[i].m_fileKey << ": "
                 << defs[i].m_name << " (line " << defs[i].m_line << ") and "
                 << defs[k].m_name << " (line " << defs[k].m_line << ") both take "
                 << DescribeValue(defs[i].m_value));
        }
    }

    SUCCEED("no unexpected duplicated message id inside any guarded header");
}

//跨头文件取同号只在「收件窗口确实不同」时才安全，而这个前提没有任何机制强制。
//因此一律要求列进白名单并写明理由，杜绝顺手取一个已被别处占用的号。
TEST_CASE("消息号守卫：跨头文件的重号必须列入白名单", "[msgid]")
{
    std::vector<MessageDefinition> defs;
    LoadAllDefinitions(defs);

    for (size_t i = 0; i < defs.size(); ++i)
    {
        if (defs[i].m_isGateSentinel)
            continue;

        for (size_t k = i + 1; k < defs.size(); ++k)
        {
            if (defs[k].m_isGateSentinel)
                continue;
            if (defs[i].m_fileKey == defs[k].m_fileKey)
                continue;
            if (defs[i].m_value != defs[k].m_value)
                continue;
            if (IsCrossFileDuplicateAllowed(defs[i].m_value))
                continue;

            FAIL_CHECK("message id " << DescribeValue(defs[i].m_value) << " is taken twice: "
                 << defs[i].m_fileKey << ":" << defs[i].m_line << " " << defs[i].m_name
                 << " and "
                 << defs[k].m_fileKey << ":" << defs[k].m_line << " " << defs[k].m_name);
        }
    }

    SUCCEED("no unexpected cross-header duplicated message id");
}

//UserSessionData.h 里的消息大多要经 CFlamingoClient::ProxyWndProc 中转，号段越过
//FMG_MSG_FIRST / FMG_MSG_LAST 就会在代理窗门口被静默丢弃，连 switch 都进不去，
//lParam 上的堆对象一并泄漏，且全程没有任何报错。本用例把这条闸门钉住。
TEST_CASE("消息号守卫：UserSessionData.h 的消息号都落在代理窗闸门区间内", "[msgid]")
{
    std::vector<MessageDefinition> defs;
    LoadAllDefinitions(defs);

    const MessageDefinition* first = FindByName(defs, kGateFirstName);
    const MessageDefinition* last = FindByName(defs, kGateLastName);
    REQUIRE(first != NULL);
    REQUIRE(last != NULL);
    REQUIRE(first->m_value <= last->m_value);

    for (size_t i = 0; i < defs.size(); ++i)
    {
        if (defs[i].m_isGateSentinel)
            continue;
        if (defs[i].m_fileKey != "UserSessionData.h")
            continue;

        INFO(defs[i].m_name << " (line " << defs[i].m_line << ") = "
             << DescribeValue(defs[i].m_value) << ", gate = ["
             << DescribeValue(first->m_value) << ", " << DescribeValue(last->m_value) << "]");
        REQUIRE(defs[i].m_value >= first->m_value);
        REQUIRE(defs[i].m_value <= last->m_value);
    }
}

//与上一条相反：CustomMsgDef.h 里那批 v1 遗留消息全是窗口之间直接投递的，本就不该
//由代理窗收，2026-08-16 把闸门下界从 WM_USER + 1 抬到 WM_USER + 50 正是为了把它们
//整段排除在收件范围之外。本用例把这个结果钉住 —— 下界要是被谁改回去，或者有人
//往 CustomMsgDef.h 里加了一条落进闸门的消息，都会在这里报出来。
TEST_CASE("消息号守卫：CustomMsgDef.h 的消息号都落在代理窗闸门区间之外", "[msgid]")
{
    std::vector<MessageDefinition> defs;
    LoadAllDefinitions(defs);

    const MessageDefinition* first = FindByName(defs, kGateFirstName);
    REQUIRE(first != NULL);

    for (size_t i = 0; i < defs.size(); ++i)
    {
        if (defs[i].m_fileKey != "CustomMsgDef.h")
            continue;

        INFO(defs[i].m_name << " (line " << defs[i].m_line << ") = "
             << DescribeValue(defs[i].m_value) << ", gate lower bound = "
             << DescribeValue(first->m_value));
        REQUIRE(defs[i].m_value < first->m_value);
    }
}
