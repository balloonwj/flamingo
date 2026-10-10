/**
 *  主面板搜索下拉「消息」「文件」标签结果行的文案计算（纯函数，不依赖窗口与控件）。
 *
 *  搜索下拉的「消息」标签每个会话占一行：主行是会话名，右侧是最近一次命中的时间，
 *  副行在只命中一条时直接显示那条消息并把关键字标黄，命中多条时显示「N 条相关聊天记录」。
 *  这些文案与高亮区间都在这里算好，SearchDropdown 只负责把结果交给 MainPanelListItem，
 *  因此规则可以脱离界面单独测试（Tests/GlobalSearchTextTests.cpp）。
 *
 *  典型用法：
 *      balloonsearch::MessageHitTexts t =
 *          balloonsearch::BuildMessageHitTexts(hit.m_hitCount, latestTextW, keywordW);
 *      item->SetSubText(t.m_sub.c_str());
 *      item->SetSubHighlight(t.m_subHiStart, t.m_subHiLen);
 *
 *  本文件不 include stdafx.h，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>   // SYSTEMTIME

#include <stddef.h>    // size_t
#include <stdint.h>    // int64_t
#include <map>
#include <string>

namespace balloonsearch {

// 副行片段在关键字前面最多保留的字符数（UTF-16 单元）。关键字之前的文字超过这个长度时，
// 截去开头、改以省略号起头，保证关键字落在副行可见范围内：主面板宽度下副行约能显示
// 十几个汉字，留 6 个字的上文既能看出语境，又给关键字本身留出足够的位置。
const int kSnippetLeadChars = 6;

// 副行片段的最大长度（UTF-16 单元，不含开头的省略号）。合并转发记录的搜索文本是全部子消息
// 拼起来的，可能有上千字，而副行只显示一行、超出部分本来就以省略号结尾，截短后每次绘制
// 测量文字的开销不会随消息长度增长。
const int kSnippetMaxChars = 120;

/**
 *  在 text 里找 keyword 第一次出现的位置。ASCII 字母不区分大小写，其余字符按原样比较 ——
 *  与本地库 SQLite LIKE 的匹配规则一致，保证库里查出来的命中在这里也能找到位置。
 *    text：被查找的文本（UTF-16）。
 *    keyword：关键字（UTF-16）。
 *  @return 首次出现处的下标（UTF-16 单元）；keyword 为空或找不到时返回 -1。
 */
int FindKeywordNoCase(const std::wstring& text, const std::wstring& keyword);

// 一段带高亮区间的单行文字。
struct HighlightedText
{
    std::wstring m_text;     // 显示的文字
    int          m_hiStart;  // 高亮起始下标（UTF-16 单元）；-1 表示没有高亮
    int          m_hiLen;    // 高亮的字符数（UTF-16 单元）；没有高亮时为 0

    HighlightedText()
        : m_hiStart(-1)
        , m_hiLen(0)
    {
    }
};

/**
 *  从一条消息的搜索文本里截出副行要显示的片段，并给出关键字的高亮区间。
 *    1. 换行、制表符等控制字符一律换成空格（副行只有一行，换行会让后半截文字消失）；
 *    2. 关键字前面的文字超过 leadChars 时截去开头，以「…」起头；截断点不落在代理对中间；
 *    3. 片段超过 kSnippetMaxChars 时截去尾部（关键字总在保留范围内）；
 *    4. 找不到关键字时返回文字的开头部分，不带高亮。正常情况下库里按 LIKE 命中的文字
 *       一定找得到关键字，这一条是防御性处理（例如 UTF-8 文本里有非法字节、转成 UTF-16
 *       后与关键字对不上）。
 *    text：消息的搜索文本（UTF-16）。
 *    keyword：关键字（UTF-16），调用方已去掉首尾空白。
 *    leadChars：关键字前面最多保留几个字符，传 kSnippetLeadChars；小于 0 时按 0 处理。
 *  @return 片段及其高亮区间。
 */
HighlightedText MakeHitSnippet(const std::wstring& text, const std::wstring& keyword, int leadChars);

/**
 *  算「消息」标签一行的副行：只命中一条时显示那条消息的片段并标出关键字；命中多条（或那条
 *  的文字为空）时显示「N 条相关聊天记录」，不带高亮。
 *    hitCount：该会话里命中的条数（ConversationHit::m_hitCount），不大于 0 时按 1 处理。
 *    latestText：最近一条命中消息的搜索文本（UTF-16）。
 *    keyword：关键字（UTF-16），调用方已去掉首尾空白。
 *  @return 副行文字及其高亮区间。
 */
HighlightedText BuildMessageHitSubText(int hitCount, const std::wstring& latestText,
                                       const std::wstring& keyword);

// 「文件」标签一行结果的来源。
enum FileHitKind
{
    // 聊天里发送的文件（本地库里 msgtype 为 message_type_file 的消息）
    kFileHitChatFile  = 0,
    // 文件夹传输记录（msgtype 为 message_type_folder），名字是文件夹名
    kFileHitFolder    = 1,
    // 聊天里的文件共享卡片（msgtype 为 message_type_fileshare），名字是共享的显示名
    kFileHitFileShare = 2,
    // 群文件库里的文件（服务端 cmd_search_group_files 的搜索结果）
    kFileHitGroupFile = 3,
};

// 「文件」标签一行的文案。
struct FileHitTexts
{
    HighlightedText m_name;  // 主行：文件名 / 文件夹名 / 共享名，关键字标黄
    std::wstring    m_sub;   // 副行：来源种类、所在会话（或群）与大小
};

// 文件名在关键字前面最多保留的字符数（UTF-16 单元）。主行字号比副行大、又要给右侧
// 时间让位，可见宽度比副行少，保留的上文与副行相同即可。
const int kFileNameLeadChars = kSnippetLeadChars;

/**
 *  算「文件」标签一行的主行与副行：
 *    主行是名字，关键字靠后时截去开头（规则同 MakeHitSnippet）并标黄；
 *    副行依来源分别是「会话名 · 大小」「文件夹 · 会话名 · 大小」「共享 · 会话名」
 *    「群文件 · 群名 · 大小」，缺哪一段就少哪一段，不留多余的分隔点。
 *    kind：来源，取值见 FileHitKind；不认识的取值按聊天文件处理。
 *    name：文件名（UTF-16）。
 *    sourceName：所在会话名或群名（UTF-16），可为空。
 *    sizeText：已格式化的大小文字（如 "1.8 MB"），未知时传空串；共享卡片不显示大小。
 *    keyword：关键字（UTF-16），调用方已去掉首尾空白。
 *  @return 主行与副行文案。
 */
FileHitTexts BuildFileHitTexts(int kind, const std::wstring& name, const std::wstring& sourceName,
                               const std::wstring& sizeText, const std::wstring& keyword);

// 本地库里一条「文件」标签消息（文件、文件夹传输记录、文件共享卡片）在结果行上要用的信息。
struct LocalFileHitInfo
{
    int         m_kind;       // 来源，取值见 FileHitKind
    std::string m_nameUtf8;   // 主行显示的名字（UTF-8）：文件名 / 文件夹名 / 共享名
    long long   m_sizeBytes;  // 大小（字节）：文件为文件大小，文件夹为总字节数；共享卡片与未知时为 0
    int         m_iconKind;   // 行首系统图标的类型，取值同 shellicon::ItemKind（1 磁盘、2 文件夹、3 文件）

    LocalFileHitInfo()
        : m_kind(kFileHitChatFile)
        , m_sizeBytes(0)
        , m_iconKind(3)
    {
    }
};

/**
 *  从本地库一条消息的类型与归一化内容 JSON 里取出「文件」标签结果行要用的信息。
 *  名字取不到（内容 JSON 缺字段或解析失败）时退回该消息的搜索文本，保证主行不空、
 *  关键字也能在里面找到。
 *    msgtype：消息类型（取值见 net/Protocol.h 的 message_type）；文件、文件夹、文件共享以外
 *             的类型按聊天文件处理。
 *    contentJson：归一化内容 JSON（HistoryRecord::m_content）。
 *    searchTextUtf8：该消息的搜索文本（HistoryRecord::m_searchText）。
 *  @return 结果行信息。
 */
LocalFileHitInfo DescribeLocalFileRecord(int msgtype, const std::string& contentJson,
                                         const std::string& searchTextUtf8);

// 「文件」标签结果行悬停时提供的快捷操作。
struct FileHitActions
{
    bool m_openFile;          // 显示「打开文件」按钮：用系统关联的程序打开本机文件
    bool m_openFolder;        // 显示「打开所在文件夹」按钮
    bool m_folderIsTarget;    // 「打开所在文件夹」打开的是路径本身（文件夹传输记录）时为 true；
                              // 为 false 时打开文件的父目录并选中该文件

    FileHitActions()
        : m_openFile(false)
        , m_openFolder(false)
        , m_folderIsTarget(false)
    {
    }
};

/**
 *  按结果行的来源与本机是否找得到它，决定悬停时提供哪些快捷操作：
 *    聊天文件且本机有文件：「打开文件」与「打开所在文件夹」（选中该文件）；
 *    文件夹传输记录且本机有那个文件夹：只有「打开所在文件夹」，打开的是文件夹本身；
 *    文件共享卡片、群文件、本机找不到的：都不提供（点行本身照常打开出处）。
 *    kind：来源，取值见 FileHitKind。
 *    hasLocal：本机是否找到了可用的文件或文件夹（balloonhistory::ResolveRecordLocalPath 的结果）。
 *  @return 要显示的按钮。
 */
FileHitActions PickFileHitActions(int kind, bool hasLocal);

// 部门上级路径最多往上找几层。组织架构一般不超过十层，这个上限只防数据里出现环时无限循环。
const int kDepartmentPathMaxDepth = 16;

/**
 *  「部门」一段结果行的副行：部门的上级路径（不含部门自己），从顶级部门到直接上级，名字之间用
 *  「 / 」连接，与联系人结果行副行的部门路径同一写法。顶级部门、或上级在列表里查不到时返回空串；
 *  上级链出现环或超过 kDepartmentPathMaxDepth 层时停在那里。
 *    deptid：部门 id。
 *    parentOf：部门 id → 上级部门 id（0 表示顶级）。
 *    nameOf：部门 id → 部门名；名字为空的上级跳过不写。
 *  @return 上级路径。
 */
std::wstring BuildDepartmentParentPath(int64_t deptid, const std::map<int64_t, int64_t>& parentOf,
                                       const std::map<int64_t, std::wstring>& nameOf);

// 「备忘」一段一行的文案。
struct MemoHitTexts
{
    bool            m_matched;  // 标题或备注里有关键字（英文字母不区分大小写）
    HighlightedText m_name;     // 主行：标题；关键字在标题里时标出
    HighlightedText m_sub;      // 副行：归属（群名或「个人备忘」）；备注里有关键字时再加「 · 」与备注片段，片段里的关键字标出

    MemoHitTexts()
        : m_matched(false)
    {
    }
};

/**
 *  判断一条备忘是否命中关键字，并生成结果行的文案。
 *    title / note：备忘的标题与备注（UTF-16）。
 *    ownerText：归属文字，群备忘传群名，个人备忘传「个人备忘」。
 *    keyword：关键字（UTF-16，已去掉首尾空白）。
 *  @return 文案；m_matched 为 false 时其余字段无意义。
 */
MemoHitTexts BuildMemoHitTexts(const std::wstring& title, const std::wstring& note,
                               const std::wstring& ownerText, const std::wstring& keyword);

/**
 *  把备忘的首次事件时间（十进制 yyyymmddhhmm，本地时间）换算成 SYSTEMTIME，供 FormatHitTime 显示。
 *    start：十进制 yyyymmddhhmm。
 *    out：[出参] 本地时间；秒与毫秒为 0。
 *  @return 年月日时分都在合法范围内返回 true，否则 false（out 不变）。
 */
bool MemoStartToSystemTime(long long start, SYSTEMTIME& out);

// 「全部」标签里一段结果（聊天记录或文件）的取舍：各来源列几条、段末要不要「查看更多」。
struct AllTabSectionPick
{
    size_t m_firstShown;   // 第一个来源（聊天记录段为聊天记录；文件段为本地聊天文件）列几条
    size_t m_secondShown;  // 第二个来源（文件段为群文件；聊天记录段恒为 0）列几条
    bool   m_showMore;     // 两个来源合计超过上限、段末要显示「查看更多」时为 true

    AllTabSectionPick()
        : m_firstShown(0)
        , m_secondShown(0)
        , m_showMore(false)
    {
    }
};

/**
 *  算「全部」标签里一段结果的取舍：先从第一个来源依次取，不够再从第二个来源取，合计不超过
 *  maxRows；两个来源合计多于实际列出的条数时段末显示「查看更多」。
 *    firstCount：第一个来源的条数（聊天记录段为会话数，文件段为本地聊天文件条数）。
 *    secondCount：第二个来源的条数（文件段为群文件条数，聊天记录段传 0）。
 *    maxRows：这一段最多列几条。
 *  @return 取舍结果。
 */
AllTabSectionPick PickAllTabSection(size_t firstCount, size_t secondCount, size_t maxRows);

// 搜索下拉各筛选标签上显示的计数。
struct SearchTabCounts
{
    int  m_all;           // 「全部」：联系人 + 群组 + 已计入的聊天记录、本地文件与群文件
    int  m_messages;      // 「消息」：命中的会话数
    int  m_files;         // 「文件」：本地聊天文件 + 已计入的群文件
    bool m_showMessages;  // 「消息」标签是否显示数字（聊天记录的后台查询结果回来之前不显示）
    bool m_showFiles;     // 「文件」标签是否显示数字（本地文件的后台查询结果回来之前不显示）

    SearchTabCounts()
        : m_all(0)
        , m_messages(0)
        , m_files(0)
        , m_showMessages(false)
        , m_showFiles(false)
    {
    }
};

/**
 *  算各标签的计数。聊天记录、本地文件各自在当前查询词的后台查询结果回来（messagesReady /
 *  filesReady）之后才计入；群文件在应答到达且成功（groupFilesDone）之后才计入。在此之前
 *  沿用的上一个关键字的结果都不计数。
 *    contacts / groups：命中的联系人数、群组数。
 *    messages：生成的聊天记录行数（会话数）。
 *    localFiles / groupFiles：生成的本地聊天文件行数、群文件行数。
 *    messagesReady：当前查询词的聊天记录结果是否已经回来。
 *    filesReady：当前查询词的本地文件结果是否已经回来。
 *    groupFilesDone：当前查询词的群文件应答是否已经成功到达。
 *  @return 各标签计数。
 */
SearchTabCounts ComputeSearchTabCounts(int contacts, int groups, int messages, int localFiles,
                                       int groupFiles, bool messagesReady, bool filesReady,
                                       bool groupFilesDone);

/**
 *  把命中消息的时间格式化成结果行右侧的短文字，规则与主面板会话列表相同
 *  （BalloonMainPanel.cpp 的 FormatConversationTime）：
 *    今天 → "HH:MM"；昨天 → "昨天"；今年其余日子 → "MM-DD"；更早 → "YYYY-MM-DD"。
 *    msgLocal：消息时间（本地时区）。
 *    nowLocal：当前时间（本地时区），由调用方传入，便于测试固定"现在"。
 *  @return 格式化后的文字。
 */
std::wstring FormatHitTime(const SYSTEMTIME& msgLocal, const SYSTEMTIME& nowLocal);

/**
 *  把 Unix 毫秒时间换算成本地时区后按 FormatHitTime 格式化，"现在"取系统当前本地时间。
 *    timestampMs：消息时间（Unix 毫秒）。
 *  @return 格式化后的文字；时间不大于 0 或换算失败时返回空串。
 */
std::wstring FormatHitTimeFromUnixMs(long long timestampMs);

} // namespace balloonsearch
