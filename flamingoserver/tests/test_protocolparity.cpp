/**
 *  两份 v2 协议头一致性守卫单元测试（Suite PP），test_protocolparity.cpp
 *
 *  v2 协议的 cmd 编号与错误码有两份手工维护的副本：服务端
 *  flamingoserver/chatserversrc/Protocol.h（协议的权威定义）与客户端
 *  flamingoclient/Source/net/Protocol.h。两份靠人工同步，一侧改了另一侧漏改
 *  不会产生任何编译期或运行期报错 —— 客户端只是把服务端下发的编号解读成别的
 *  含义，表现为提示文案与实际情况不符，或者走进错误的分支。
 *
 *  本组用例在运行期把两个头文件当纯文本读进来，解析 enum msg_type 与
 *  enum error_code 的每一条定义，断言四件事：
 *    - PP1 同名符号在两侧的取值必须相同；
 *    - PP2 各自内部不得有两个符号取同一个值（错误码是一个扁平的、跨进程传输的
 *          命名空间，客户端收到 308 无从分辨服务端指的是哪一种）；
 *    - PP3 两侧的符号集合差异必须逐条登记在白名单里并写明理由；
 *    - PP4 错误码的取值必须落在它所处的号段横幅（形如 //==== 群组 300-399 ====）
 *          划定的区间内，即 docs/协议设计-v2.md 第 8 节那份错误码号段规约；
 *    - PP5 消息内容类型 enum message_type 与文件共享新增的七个枚举，以及群接龙、屏幕共享、
 *          消息提醒模式、P2P 通信的业务枚举（完整清单见 kExtraGuardedEnums），逐个做 PP1~PP3 那三项检查。
 *
 *  找不到、打不开任何一个头文件都直接判失败，而不是跳过 —— 守卫悄悄不干活，
 *  正是本组用例要防的那类问题本身。
 *
 *  典型用法：Tests.exe --test-case="PP*"
 *
 *  balloonwj@qq.com   2026-08-16
 */

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "../utils/AppPath.h"

namespace {

//向上回溯查找仓库根目录时允许的最大层级数。实测最深的一条起点是 Windows 上的
//flamingoserver\windowsproj\x64\Debug\Tests.exe，回到仓库根需要 4 层，取 10 留余量。
const int kMaxParentWalkDepth = 10;

//两份被比对的协议头，写成相对仓库根目录的路径。它们同时充当「这一层就是仓库根目录」
//的判定标志：只有同时含有这两个文件的目录才是仓库根。
const char* const kServerProtocolRelativePath = "flamingoserver/chatserversrc/Protocol.h";
const char* const kClientProtocolRelativePath = "flamingoclient/Source/net/Protocol.h";

//PP1~PP4 比对的两个枚举名。
const char* const kEnumMsgType = "msg_type";
const char* const kEnumErrorCode = "error_code";

//PP5 另行比对的枚举名。其余枚举（presence_type 等）取值范围小、改动少，且一侧改错时
//客户端界面会立刻显出来，暂不纳入守卫。
//
//message_type 原先也不纳入，2026-10-02 起纳入：文件共享与屏幕共享两个功能并行往里追加取值，
//一侧漏改时客户端会把一种卡片当成另一种来解析，界面上未必立刻看得出来。其余七个是文件共享
//（docs/文件共享方案.md 5.7）新增的枚举，服务端与客户端的 FlamingoFileShare.dll 都按它们解读
//卡片状态、权限位与审计动作，取值一旦两侧不一致，表现为卡片状态或权限判断错位。
const char* const kExtraGuardedEnums[] =
{
    "message_type",
    "share_kind",
    "share_perm",
    "share_state",
    "share_unavailable_reason",
    "share_invalid_reason",
    "share_audit_action",
    "share_audit_source",
    "relay_state",          //群接龙的接龙状态（docs/群接龙方案.md §4.1）
    "screenshare_track",          //屏幕共享房间中转帧头的轨道类型（docs/屏幕共享与录制方案.md 5.4）
    "screenshare_frame_flag",     //屏幕共享房间中转帧头的标志位
    "screenshare_control_type",   //屏幕共享控制帧的类型
    "screenshare_seat",           //屏幕共享座位号的特殊取值
    "screenshare_source_state",   //屏幕共享控制帧里的共享内容状态
    "conversation_notify_mode",   //会话的消息提醒模式（docs/消息提醒模式与消息盒子方案.md §3）
    "p2p_role",                   //P2P 会话里本方的角色（docs/P2P通信方案.md 第 5 节）
    "p2p_path",                   //P2P 链路最终选定的路径
    "p2p_close_reason",           //P2P 会话结束的原因
    "memo_repeat_type",           //备忘的重复方式（docs/备忘与提醒方案.md 第 3 节）
    "memo_style",                 //备忘的提醒方式
    "memo_change_type",           //备忘变更推送的类型
    "group_invite_state",         //入群邀请卡片的状态（docs/邀请入群方案.md §4.3）
    "cloudrecord_source",         //云端录像的来源（docs/屏幕共享与录制方案.md 12.6）
    "cloudrecord_status",         //云端录像的状态
    "cloudrecord_control_action", //共享中云端录制的开始 / 暂停 / 继续 / 停止
    "cloudrecord_op_type",        //对一条云端录像的操作
    "cloudrecord_notify_type",    //云端录像推送的类型
    "group_type",                 //群类型：普通群 / 帖子群（docs/帖子群方案.md 第 5 节）
    "thread_reply_kind",          //帖子动态卡片的种类：留言、回复、楼主补充
    "thread_target_type",         //帖子群互动的目标类型
    "thread_change_type",         //帖子群内容变化推送的类型
    "thread_sort",                //帖子列表的排序方式
    "secret_state",               //密聊会话状态（docs/密聊方案.md 5.2）
    "secret_end_reason",          //密聊结束原因
    "secret_protect_level",       //本机的防截图级别
};

const size_t kExtraGuardedEnumCount = sizeof(kExtraGuardedEnums) / sizeof(kExtraGuardedEnums[0]);

//号段横幅的识别标记。协议头里每个功能段前都有一行形如
//"    //==== 群组 300-399 ====" 的注释，本守卫据此取出该段的取值区间。
const char* const kSegmentBannerMark = "====";

//号段区间未知时 m_segmentLow / m_segmentHigh 取的哨兵值。枚举里的取值一律非负，
//故用 -1 表示「本条之前还没出现过号段横幅」。
const long kNoSegment = -1;

//一条被解析出来的枚举定义。
struct EnumEntry
{
    std::string m_name;        //枚举符号名，如 error_code_sensitive_word
    long        m_value;       //该符号的取值
    int         m_line;        //定义所在行号，从 1 起，仅用于失败时定位
    long        m_segmentLow;  //所属号段的下界；本条之前没有号段横幅时取 kNoSegment
    long        m_segmentHigh; //所属号段的上界；本条之前没有号段横幅时取 kNoSegment

    EnumEntry()
        : m_value(0)
        , m_line(0)
        , m_segmentLow(kNoSegment)
        , m_segmentHigh(kNoSegment)
    {
    }
};

//——两侧符号集合差异的白名单——
//
//协议头是客户端与服务端共用的契约，正常情况下两侧应当逐条一致。只有当某个符号
//「确实只在服务端内部流转、永远不会经任何应答或推送下发给客户端」时，才允许客户端
//不定义它；反方向（客户端独有）同理。这类豁免必须登记在本表里并写明理由。
//
//没有理由的豁免等于把守卫关掉，故每一行都必须填 m_reason；m_reason 取 NULL 的那行
//是数组的结束标记，不是豁免项。留结束标记是为了让数组永远不退化成零长度数组 ——
//C++ 不允许零长度数组。
struct NameDifferenceAllowance
{
    const char* m_enumName;    //所属枚举名，取 kEnumMsgType、kEnumErrorCode 或 kExtraGuardedEnums 中的一个
    const char* m_symbolName;  //允许只出现在一侧的符号名
    bool        m_serverOnly;  //true 表示允许只在服务端出现，false 表示允许只在客户端出现
    const char* m_reason;      //允许的理由；结束标记行取 NULL
};

const NameDifferenceAllowance kNameDifferenceAllowances[] =
{
    //结束标记。2026-08-16 补齐客户端缺失的 error_code_at_all_denied /
    //error_code_cannot_operate_owner / error_code_sensitive_word 之后，两侧符号集合
    //已完全一致，本表一度为空（之后只有上面那条临时豁免）。这三条都会经应答下发给客户端（前两条走群管理与群发言校验，
    //第三条走敏感词拦截），不属于「服务端内部错误码」，故不能靠登记豁免了事。
    { NULL, NULL, false, NULL },
};

const size_t kNameDifferenceAllowanceCount =
    sizeof(kNameDifferenceAllowances) / sizeof(kNameDifferenceAllowances[0]);

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
    std::ifstream fin(path.c_str(), std::ios::binary);
    return fin.is_open();
}

//判断某一层目录是不是仓库根：两份协议头都在它下面才算。
bool IsRepoRoot(const std::string& dir)
{
    if (dir.empty())
        return false;

    if (!FileExists(dir + "/" + kServerProtocolRelativePath))
        return false;

    return FileExists(dir + "/" + kClientProtocolRelativePath);
}

//从 startDir 起逐级向上找仓库根目录。找不到返回空串。
std::string WalkUpToRepoRoot(const std::string& startDir)
{
    std::string dir = startDir;
    for (int depth = 0; depth < kMaxParentWalkDepth; ++depth)
    {
        if (dir.empty())
            break;

        if (IsRepoRoot(dir))
            return dir;

        std::string parent = GetParentDirectory(dir);
        if (parent == dir)
            break;
        dir = parent;
    }
    return std::string();
}

//定位仓库根目录的绝对路径，失败返回空串。
//
//首选 __FILE__ 所在目录：CMake 生成的编译命令里源文件路径是绝对的，这条一次就中。
//MSBuild 默认不加 /FC，__FILE__ 拿到的是形如 "..\tests\test_protocolparity.cpp"
//的相对路径，此时改走可执行文件所在目录往上回溯 —— 两条路都留着，免得将来换了
//构建方式，用例静默地找不到源码、也就静默地失去守卫能力。
std::string ResolveRepoRoot()
{
    std::string fileDir = GetParentDirectory(std::string(__FILE__));
    if (apppath::IsAbsolutePath(fileDir))
    {
        std::string root = WalkUpToRepoRoot(fileDir);
        if (!root.empty())
            return root;
    }

    return WalkUpToRepoRoot(apppath::GetExecutableDirectory());
}

//判断一个字符是不是十进制数字。不用 isdigit，免受当前区域设置影响。
bool IsDecimalDigit(char c)
{
    return c >= '0' && c <= '9';
}

//标识符允许的字符：字母、数字、下划线。
bool IsIdentifierChar(char c)
{
    if (c >= 'a' && c <= 'z')
        return true;
    if (c >= 'A' && c <= 'Z')
        return true;
    if (IsDecimalDigit(c))
        return true;
    return c == '_';
}

//去掉一行文本首尾的空白（空格、制表符、回车）。以二进制方式读文件时，
//Windows 换行会在行尾留下 '\r'，一并在这里去掉。
std::string TrimAscii(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size()
           && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
    {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin
           && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
    {
        --end;
    }

    return text.substr(begin, end - begin);
}

//截掉一行里 "//" 之后的注释部分。被解析的是枚举体，其中不含字符串字面量，
//故按首个 "//" 截断即可，不必处理引号内的双斜杠。
std::string StripLineComment(const std::string& line)
{
    size_t pos = line.find("//");
    if (pos == std::string::npos)
        return line;
    return line.substr(0, pos);
}

//解析一行号段横幅，取出它划定的取值区间。
//
//横幅形如 "    //==== 群组 300-399 ===="：先认出等号标记，再从其后的文本里找出
//「数字串 + 连字符 + 数字串」这一对。找不到返回 false，调用方据此当普通注释跳过。
bool ParseSegmentBanner(const std::string& line, long& low, long& high)
{
    size_t bannerPos = line.find(kSegmentBannerMark);
    if (bannerPos == std::string::npos)
        return false;

    size_t pos = bannerPos + strlen(kSegmentBannerMark);
    while (pos < line.size())
    {
        if (!IsDecimalDigit(line[pos]))
        {
            ++pos;
            continue;
        }

        size_t firstBegin = pos;
        while (pos < line.size() && IsDecimalDigit(line[pos]))
            ++pos;
        size_t firstEnd = pos;

        //数字串后面必须紧跟连字符，否则这串数字不是号段的下界（例如段名里带的年份）
        if (pos >= line.size() || line[pos] != '-')
            continue;
        ++pos;

        size_t secondBegin = pos;
        while (pos < line.size() && IsDecimalDigit(line[pos]))
            ++pos;
        if (pos == secondBegin)
            continue;

        low = strtol(line.substr(firstBegin, firstEnd - firstBegin).c_str(), NULL, 10);
        high = strtol(line.substr(secondBegin, pos - secondBegin).c_str(), NULL, 10);
        return true;
    }

    return false;
}

//解析一行枚举定义，形如 "error_code_ok = 0," 或 "cmd_login = 1003,"。
//识别不出来返回 false（空行、纯注释行、闭合花括号等）。
bool ParseEnumEntryLine(const std::string& line, std::string& name, long& value)
{
    std::string code = TrimAscii(StripLineComment(line));
    if (code.empty())
        return false;

    size_t pos = 0;
    while (pos < code.size() && IsIdentifierChar(code[pos]))
        ++pos;
    if (pos == 0)
        return false;

    name = code.substr(0, pos);

    while (pos < code.size() && (code[pos] == ' ' || code[pos] == '\t'))
        ++pos;
    if (pos >= code.size() || code[pos] != '=')
        return false;
    ++pos;

    while (pos < code.size() && (code[pos] == ' ' || code[pos] == '\t'))
        ++pos;

    //基数取 0，与 C 编译器一致地识别十进制与 0x 十六进制两种写法
    const char* begin = code.c_str() + pos;
    char* end = NULL;
    long parsed = strtol(begin, &end, 0);
    if (end == begin)
        return false;

    value = parsed;
    return true;
}

//把一个头文件里指定枚举的全部定义解析进 out，并给每条记上它所处的号段区间。
//
//@param path      头文件的绝对路径
//@param enumName  枚举名，取 kEnumMsgType 或 kEnumErrorCode
//@param out       解析结果，函数内部先清空再填充
//@return 文件打得开、且枚举体确实被找到并解析出至少一条定义时返回 true。
bool ParseEnumFromFile(const std::string& path, const char* enumName, std::vector<EnumEntry>& out)
{
    out.clear();

    std::ifstream fin(path.c_str(), std::ios::binary);
    if (!fin.is_open())
        return false;

    std::string enumHeader = std::string("enum ") + enumName;
    bool inEnum = false;
    long segmentLow = kNoSegment;
    long segmentHigh = kNoSegment;
    std::string line;
    int lineNo = 0;

    while (std::getline(fin, line))
    {
        ++lineNo;
        std::string trimmed = TrimAscii(line);

        if (!inEnum)
        {
            //枚举体的起点：整行就是 "enum msg_type" 这样的声明。写成全等比较，
            //免得 "enum message_type" 之类前缀相同的枚举被误认
            if (trimmed == enumHeader)
            {
                inEnum = true;
                segmentLow = kNoSegment;
                segmentHigh = kNoSegment;
            }
            continue;
        }

        //枚举体的终点
        if (trimmed == "};")
            break;

        long bannerLow = 0;
        long bannerHigh = 0;
        if (ParseSegmentBanner(trimmed, bannerLow, bannerHigh))
        {
            segmentLow = bannerLow;
            segmentHigh = bannerHigh;
            continue;
        }

        std::string name;
        long value = 0;
        if (!ParseEnumEntryLine(line, name, value))
            continue;

        EnumEntry entry;
        entry.m_name = name;
        entry.m_value = value;
        entry.m_line = lineNo;
        entry.m_segmentLow = segmentLow;
        entry.m_segmentHigh = segmentHigh;
        out.push_back(entry);
    }

    return !out.empty();
}

//在一组枚举定义里按符号名查找，找到返回该条的下标，找不到返回 entries.size()。
size_t FindEntryByName(const std::vector<EnumEntry>& entries, const std::string& name)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].m_name == name)
            return i;
    }
    return entries.size();
}

//查白名单：某个符号只出现在一侧是否已被登记豁免。
//
//说明：本文件里凡是要写进断言消息的字符串一律用 std::string，不用 const char*。
//doctest 的消息拼接把 const char* 当作普通指针打印出十六进制地址，只有 std::string
//才会被原样输出成文字，失败信息才看得懂。
bool IsNameDifferenceAllowed(const std::string& enumName, const std::string& symbolName,
                             bool serverOnly)
{
    for (size_t i = 0; i < kNameDifferenceAllowanceCount; ++i)
    {
        const NameDifferenceAllowance& allowance = kNameDifferenceAllowances[i];
        //结束标记行，其后没有真正的豁免项了
        if (allowance.m_reason == NULL)
            break;

        if (enumName != allowance.m_enumName)
            continue;
        if (symbolName != allowance.m_symbolName)
            continue;
        if (allowance.m_serverOnly != serverOnly)
            continue;

        return true;
    }
    return false;
}

//两份协议头里被守卫的全部枚举定义。
struct ProtocolHeaderPair
{
    std::vector<EnumEntry> m_serverMsgType;    //服务端 enum msg_type
    std::vector<EnumEntry> m_clientMsgType;    //客户端 enum msg_type
    std::vector<EnumEntry> m_serverErrorCode;  //服务端 enum error_code
    std::vector<EnumEntry> m_clientErrorCode;  //客户端 enum error_code
};

//读入两份协议头里的四个枚举。
//
//仓库根定位不到、文件打不开、或某个枚举一条也解析不出来，都直接让用例失败 ——
//静默跳过等于守卫失效，而「守卫悄悄不干活」正是本组用例要根治的问题。
//@return 全部解析成功返回 true；已在函数内部报过失败的情形返回 false，
//        调用方据此直接结束本用例。
bool LoadProtocolHeaders(ProtocolHeaderPair& pair)
{
    std::string root = ResolveRepoRoot();
    if (root.empty())
    {
        FAIL("cannot locate flamingo repository root; __FILE__ = " << __FILE__
             << ", exe dir = " << apppath::GetExecutableDirectory());
        return false;
    }

    std::string serverPath = root + "/" + kServerProtocolRelativePath;
    std::string clientPath = root + "/" + kClientProtocolRelativePath;

    if (!ParseEnumFromFile(serverPath, kEnumMsgType, pair.m_serverMsgType))
    {
        FAIL("cannot parse enum " << std::string(kEnumMsgType) << " from " << serverPath);
        return false;
    }
    if (!ParseEnumFromFile(clientPath, kEnumMsgType, pair.m_clientMsgType))
    {
        FAIL("cannot parse enum " << std::string(kEnumMsgType) << " from " << clientPath);
        return false;
    }
    if (!ParseEnumFromFile(serverPath, kEnumErrorCode, pair.m_serverErrorCode))
    {
        FAIL("cannot parse enum " << std::string(kEnumErrorCode) << " from " << serverPath);
        return false;
    }
    if (!ParseEnumFromFile(clientPath, kEnumErrorCode, pair.m_clientErrorCode))
    {
        FAIL("cannot parse enum " << std::string(kEnumErrorCode) << " from " << clientPath);
        return false;
    }

    return true;
}

//比对一个枚举在两侧的取值：同名符号必须同值。
void CheckValuesMatch(const std::string& enumName,
                      const std::vector<EnumEntry>& serverEntries,
                      const std::vector<EnumEntry>& clientEntries)
{
    for (size_t i = 0; i < serverEntries.size(); ++i)
    {
        size_t pos = FindEntryByName(clientEntries, serverEntries[i].m_name);
        if (pos == clientEntries.size())
            continue;

        CHECK_MESSAGE(serverEntries[i].m_value == clientEntries[pos].m_value,
                      "enum " << enumName << ": " << serverEntries[i].m_name
                      << " is " << serverEntries[i].m_value << " on the server side (line "
                      << serverEntries[i].m_line << ") but " << clientEntries[pos].m_value
                      << " on the client side (line " << clientEntries[pos].m_line << ")");
    }
}

//检查一侧内部有没有两个符号取同一个值。
void CheckNoDuplicateValue(const std::string& enumName, const std::string& sideName,
                           const std::vector<EnumEntry>& entries)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        for (size_t j = i + 1; j < entries.size(); ++j)
        {
            if (entries[i].m_value != entries[j].m_value)
                continue;

            CHECK_MESSAGE(false,
                          "enum " << enumName << " on the " << sideName << " side: "
                          << entries[i].m_name << " (line " << entries[i].m_line << ") and "
                          << entries[j].m_name << " (line " << entries[j].m_line
                          << ") share the same value " << entries[i].m_value);
        }
    }
}

//检查一侧独有的符号是否都已登记豁免。
//@param serverOnly true 表示 lhs 是服务端、检查的是「只在服务端出现」的符号。
void CheckNameSetDifference(const std::string& enumName, bool serverOnly,
                            const std::vector<EnumEntry>& lhs,
                            const std::vector<EnumEntry>& rhs)
{
    std::string lhsSideName = serverOnly ? "server" : "client";
    std::string rhsSideName = serverOnly ? "client" : "server";

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (FindEntryByName(rhs, lhs[i].m_name) != rhs.size())
            continue;

        CHECK_MESSAGE(IsNameDifferenceAllowed(enumName, lhs[i].m_name, serverOnly),
                      "enum " << enumName << ": " << lhs[i].m_name << " = " << lhs[i].m_value
                      << " is defined on the " << lhsSideName << " side (line " << lhs[i].m_line
                      << ") but missing on the " << rhsSideName
                      << " side, and it is not listed in kNameDifferenceAllowances");
    }
}

//检查错误码是否落在它所处号段横幅划定的区间内。
void CheckSegmentRange(const std::string& sideName, const std::vector<EnumEntry>& entries)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        CHECK_MESSAGE(entries[i].m_segmentLow != kNoSegment,
                      "error code on the " << sideName << " side: " << entries[i].m_name
                      << " (line " << entries[i].m_line
                      << ") appears before any segment banner such as \"//==== 300-399 ====\"");

        if (entries[i].m_segmentLow == kNoSegment)
            continue;

        //先把区间判定算成一个 bool 再交给断言宏：doctest 会对断言表达式做拆解，
        //含 && 的复合表达式会触发它的 "Expression Too Complex" 静态断言
        bool inSegment = (entries[i].m_value >= entries[i].m_segmentLow)
                         && (entries[i].m_value <= entries[i].m_segmentHigh);

        CHECK_MESSAGE(inSegment,
                      "error code on the " << sideName << " side: " << entries[i].m_name
                      << " = " << entries[i].m_value << " (line " << entries[i].m_line
                      << ") falls outside its segment [" << entries[i].m_segmentLow
                      << ", " << entries[i].m_segmentHigh << "]");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// PP1  同名符号两侧取值一致
// ---------------------------------------------------------------------------

TEST_CASE("PP1 两份协议头里同名的 cmd 与错误码取值必须相同")
{
    ProtocolHeaderPair pair;
    if (!LoadProtocolHeaders(pair))
        return;

    CheckValuesMatch(kEnumMsgType, pair.m_serverMsgType, pair.m_clientMsgType);
    CheckValuesMatch(kEnumErrorCode, pair.m_serverErrorCode, pair.m_clientErrorCode);
}

// ---------------------------------------------------------------------------
// PP2  各自内部不得重号
// ---------------------------------------------------------------------------

TEST_CASE("PP2 单份协议头内部不得有两个符号取同一个值")
{
    ProtocolHeaderPair pair;
    if (!LoadProtocolHeaders(pair))
        return;

    CheckNoDuplicateValue(kEnumMsgType, "server", pair.m_serverMsgType);
    CheckNoDuplicateValue(kEnumMsgType, "client", pair.m_clientMsgType);
    CheckNoDuplicateValue(kEnumErrorCode, "server", pair.m_serverErrorCode);
    CheckNoDuplicateValue(kEnumErrorCode, "client", pair.m_clientErrorCode);
}

// ---------------------------------------------------------------------------
// PP3  符号集合差异必须登记在白名单里
// ---------------------------------------------------------------------------

TEST_CASE("PP3 两份协议头的符号集合差异必须登记在白名单里")
{
    ProtocolHeaderPair pair;
    if (!LoadProtocolHeaders(pair))
        return;

    CheckNameSetDifference(kEnumMsgType, true, pair.m_serverMsgType, pair.m_clientMsgType);
    CheckNameSetDifference(kEnumMsgType, false, pair.m_clientMsgType, pair.m_serverMsgType);
    CheckNameSetDifference(kEnumErrorCode, true, pair.m_serverErrorCode, pair.m_clientErrorCode);
    CheckNameSetDifference(kEnumErrorCode, false, pair.m_clientErrorCode, pair.m_serverErrorCode);
}

// ---------------------------------------------------------------------------
// PP4  错误码必须落在所属号段内
// ---------------------------------------------------------------------------

TEST_CASE("PP4 错误码的取值必须落在它所处号段横幅划定的区间内")
{
    ProtocolHeaderPair pair;
    if (!LoadProtocolHeaders(pair))
        return;

    //只对 error_code 做这项检查。msg_type 里有若干比功能段更窄的子横幅
    //（如 //==== 单聊文件夹传输 1810-1817 ====），其后还接着同一功能段内更大的编号
    //（cmd_file_upload = 1851），按横幅逐条卡区间会误报，故不纳入。
    CheckSegmentRange("server", pair.m_serverErrorCode);
    CheckSegmentRange("client", pair.m_clientErrorCode);
}

// ---------------------------------------------------------------------------
// PP5  消息内容类型与文件共享的各个枚举两侧一致
// ---------------------------------------------------------------------------

TEST_CASE("PP5 消息内容类型与文件共享的各个枚举在两份协议头里必须一致")
{
    std::string root = ResolveRepoRoot();
    if (root.empty())
    {
        FAIL("cannot locate flamingo repository root; __FILE__ = " << __FILE__
             << ", exe dir = " << apppath::GetExecutableDirectory());
        return;
    }

    std::string serverPath = root + "/" + kServerProtocolRelativePath;
    std::string clientPath = root + "/" + kClientProtocolRelativePath;

    for (size_t i = 0; i < kExtraGuardedEnumCount; ++i)
    {
        std::string enumName = kExtraGuardedEnums[i];
        std::vector<EnumEntry> serverEntries;
        std::vector<EnumEntry> clientEntries;

        //任一侧解析不出来都判失败而不是跳过，理由见 LoadProtocolHeaders 的注释；
        //继续检查下一个枚举，让一次运行就把缺失的枚举全部列出来
        bool serverParsed = ParseEnumFromFile(serverPath, kExtraGuardedEnums[i], serverEntries);
        bool clientParsed = ParseEnumFromFile(clientPath, kExtraGuardedEnums[i], clientEntries);
        CHECK_MESSAGE(serverParsed, "cannot parse enum " << enumName << " from " << serverPath);
        CHECK_MESSAGE(clientParsed, "cannot parse enum " << enumName << " from " << clientPath);
        if (!serverParsed || !clientParsed)
            continue;

        CheckValuesMatch(enumName, serverEntries, clientEntries);
        CheckNoDuplicateValue(enumName, "server", serverEntries);
        CheckNoDuplicateValue(enumName, "client", clientEntries);
        CheckNameSetDifference(enumName, true, serverEntries, clientEntries);
        CheckNameSetDifference(enumName, false, clientEntries, serverEntries);
    }
}
