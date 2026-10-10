/**
 *  主面板搜索下拉结果行文案计算的实现。整体说明见 GlobalSearchText.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "GlobalSearchText.h"

#include <stdio.h>   // _snwprintf_s

#include <memory>    // std::unique_ptr（jsoncpp 的 CharReader）
#include <vector>

#include "../BalloonHistory/HistoryView.h"            // ParseContentBrief：文件名、文件夹名与大小
#include "../../net/Protocol.h"                       // message_type_* / share_kind_*
#include "../../third-party/jsoncpp-1.9.0/json.h"     // 共享卡片内容里的共享类型
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonsearch {

namespace {

// 片段被截去开头时放在最前面的省略号（U+2026）。
const wchar_t* const kEllipsis = L"…";

// 格式化时间短文字用的缓冲区长度（字符数）。最长的是「YYYY-MM-DD」，远小于这个长度。
const int kFormatBufChars = 64;

// 一天的 FILETIME 计数（100ns 为单位）：86400 秒 × 10^7。
const ULONGLONG kFileTimePerDay = 864000000000ULL;

// 1970-01-01 00:00:00 UTC 对应的 FILETIME 值（自 1601-01-01 起的 100ns 数）。
const ULONGLONG kUnixEpochAsFileTime = 116444736000000000ULL;

// 一秒含的 100ns 数。
const ULONGLONG kFileTimePerSecond = 10000000ULL;

// 一秒的毫秒数。
const long long kMsPerSecond = 1000;

// ASCII 大写字母转小写，其余字符原样返回（与 SQLite LIKE 只对 ASCII 字母不区分大小写一致）。
wchar_t AsciiLower(wchar_t ch)
{
    if (ch >= L'A' && ch <= L'Z')
    {
        return (wchar_t)(ch - L'A' + L'a');
    }
    return ch;
}

// 是否为 UTF-16 代理对的后半（低代理，DC00～DFFF）。
bool IsLowSurrogate(wchar_t ch)
{
    return ch >= 0xDC00 && ch <= 0xDFFF;
}

// 是否为 UTF-16 代理对的前半（高代理，D800～DBFF）。
bool IsHighSurrogate(wchar_t ch)
{
    return ch >= 0xD800 && ch <= 0xDBFF;
}

// 把控制字符（换行、回车、制表符等 0x00～0x1F 以及 0x7F）逐个换成空格。一对一替换，
// 不增删字符，因此替换前后同一个字的下标不变。
std::wstring FlattenToSingleLine(const std::wstring& text)
{
    std::wstring out(text);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] < 0x20 || out[i] == 0x7F)
        {
            out[i] = L' ';
        }
    }
    return out;
}

// 从 flat 的 start 处取最多 maxLen 个字符；末尾若恰好停在代理对的前半，就少取一个，
// 避免显示出半个字。
std::wstring SafeSubstr(const std::wstring& flat, size_t start, size_t maxLen)
{
    if (start >= flat.size())
    {
        return std::wstring();
    }
    size_t len = flat.size() - start;
    if (len > maxLen)
    {
        len = maxLen;
        if (len > 0 && IsHighSurrogate(flat[start + len - 1]))
        {
            --len;
        }
    }
    return flat.substr(start, len);
}

// 判断两个本地时间是否为同一天。
bool IsSameDay(const SYSTEMTIME& a, const SYSTEMTIME& b)
{
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}

// 把本地时间往前推一天，得到"昨天"的日期。只用来按年月日比较，夏令时切换处的细微
// 偏差不影响结果。成功返回 true。
bool ComputeYesterday(const SYSTEMTIME& now, SYSTEMTIME& outYesterday)
{
    FILETIME ft;
    if (!::SystemTimeToFileTime(&now, &ft))
    {
        return false;
    }
    ULONGLONG ull = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    if (ull < kFileTimePerDay)
    {
        return false;
    }
    ull -= kFileTimePerDay;
    ft.dwLowDateTime  = (DWORD)(ull & 0xFFFFFFFFULL);
    ft.dwHighDateTime = (DWORD)(ull >> 32);
    return ::FileTimeToSystemTime(&ft, &outYesterday) != FALSE;
}

} // namespace

int FindKeywordNoCase(const std::wstring& text, const std::wstring& keyword)
{
    if (keyword.empty() || keyword.size() > text.size())
    {
        return -1;
    }

    std::wstring lowText(text);
    for (size_t i = 0; i < lowText.size(); ++i)
    {
        lowText[i] = AsciiLower(lowText[i]);
    }
    std::wstring lowKeyword(keyword);
    for (size_t i = 0; i < lowKeyword.size(); ++i)
    {
        lowKeyword[i] = AsciiLower(lowKeyword[i]);
    }

    const size_t pos = lowText.find(lowKeyword);
    if (pos == std::wstring::npos)
    {
        return -1;
    }
    return (int)pos;
}

HighlightedText MakeHitSnippet(const std::wstring& text, const std::wstring& keyword, int leadChars)
{
    if (leadChars < 0)
    {
        leadChars = 0;
    }

    HighlightedText out;
    const std::wstring flat = FlattenToSingleLine(text);
    const int pos = FindKeywordNoCase(flat, keyword);
    if (pos < 0)
    {
        //找不到关键字：只显示开头部分，不标高亮。
        out.m_text = SafeSubstr(flat, 0, (size_t)kSnippetMaxChars);
        return out;
    }

    //关键字前面的文字太长时截去开头。截断点若落在代理对的后半，往后挪一个字符，
    //免得片段以半个字开头。
    size_t start = 0;
    bool   cutHead = false;
    if (pos > leadChars)
    {
        start = (size_t)(pos - leadChars);
        if (IsLowSurrogate(flat[start]) && start < (size_t)pos)
        {
            ++start;
        }
        cutHead = true;
    }

    //片段长度不超过 kSnippetMaxChars，但关键字本身必须完整保留。
    size_t maxLen = (size_t)kSnippetMaxChars;
    const size_t keywordEnd = (size_t)pos - start + keyword.size();
    if (maxLen < keywordEnd)
    {
        maxLen = keywordEnd;
    }

    out.m_text = cutHead ? std::wstring(kEllipsis) : std::wstring();
    out.m_text += SafeSubstr(flat, start, maxLen);
    out.m_hiStart = (int)((size_t)pos - start) + (cutHead ? 1 : 0);
    out.m_hiLen   = (int)keyword.size();
    return out;
}

HighlightedText BuildMessageHitSubText(int hitCount, const std::wstring& latestText,
                                       const std::wstring& keyword)
{
    if (hitCount <= 0)
    {
        hitCount = 1;
    }

    //只命中一条：直接给出那条消息的片段，用户不点进去也能看到命中的是哪句话。
    if (hitCount == 1 && !latestText.empty())
    {
        return MakeHitSnippet(latestText, keyword, kSnippetLeadChars);
    }

    //带参数的整句按多语言规则用 TRF（占位符 {0}），不同语言里数字的位置可以不同。
    HighlightedText out;
    out.m_text = TRF(L"{0} 条相关聊天记录", hitCount);
    return out;
}

FileHitTexts BuildFileHitTexts(int kind, const std::wstring& name, const std::wstring& sourceName,
                               const std::wstring& sizeText, const std::wstring& keyword)
{
    FileHitTexts out;
    out.m_name = MakeHitSnippet(name, keyword, kFileNameLeadChars);

    //副行按"种类 · 来源 · 大小"拼，空的那段整段省掉。聊天文件不写种类：「文件」标签里
    //它是最常见的一类，每行都写「文件」只会挤占来源与大小的位置。
    std::wstring prefix;
    bool showSize = true;
    switch (kind)
    {
    //文件夹传输记录：写明是文件夹，大小是整个文件夹的总字节数
    case kFileHitFolder:
        prefix = TR(L"文件夹");
        break;
    //文件共享卡片：共享的内容会变化，卡片里记的大小没有参考意义，不显示
    case kFileHitFileShare:
        prefix   = TR(L"共享");
        showSize = false;
        break;
    //群文件库：写明出自群文件，与聊天里收发的文件区分开
    case kFileHitGroupFile:
        prefix = TR(L"群文件");
        break;
    //聊天里发送的文件，以及不认识的取值：不写种类
    default:
        break;
    }

    const wchar_t* const kSeparator = L" · ";
    std::wstring sub = prefix;
    if (!sourceName.empty())
    {
        if (!sub.empty())
        {
            sub += kSeparator;
        }
        sub += sourceName;
    }
    if (showSize && !sizeText.empty())
    {
        if (!sub.empty())
        {
            sub += kSeparator;
        }
        sub += sizeText;
    }
    out.m_sub = FlattenToSingleLine(sub);
    return out;
}

std::wstring BuildDepartmentParentPath(int64_t deptid, const std::map<int64_t, int64_t>& parentOf,
                                       const std::map<int64_t, std::wstring>& nameOf)
{
    //自下而上收集上级的名字，再倒过来拼
    std::vector<std::wstring> names;
    int64_t current = deptid;
    for (int depth = 0; depth < kDepartmentPathMaxDepth; ++depth)
    {
        std::map<int64_t, int64_t>::const_iterator itp = parentOf.find(current);
        if (itp == parentOf.end() || itp->second <= 0 || itp->second == deptid)
        {
            break;
        }
        current = itp->second;
        std::map<int64_t, std::wstring>::const_iterator itn = nameOf.find(current);
        if (itn == nameOf.end())
        {
            break;
        }
        if (!itn->second.empty())
        {
            names.push_back(itn->second);
        }
    }
    std::wstring path;
    for (size_t i = names.size(); i > 0; --i)
    {
        if (!path.empty())
        {
            path += L" / ";
        }
        path += names[i - 1];
    }
    return path;
}

MemoHitTexts BuildMemoHitTexts(const std::wstring& title, const std::wstring& note,
                               const std::wstring& ownerText, const std::wstring& keyword)
{
    MemoHitTexts out;
    if (keyword.empty())
    {
        return out;
    }
    const int inTitle = FindKeywordNoCase(title, keyword);
    const int inNote  = FindKeywordNoCase(note, keyword);
    out.m_matched = (inTitle >= 0 || inNote >= 0);
    if (!out.m_matched)
    {
        return out;
    }
    out.m_name.m_text = title;
    if (inTitle >= 0)
    {
        out.m_name.m_hiStart = inTitle;
        out.m_name.m_hiLen   = (int)keyword.size();
    }
    out.m_sub.m_text = ownerText;
    //备注里有关键字：副行再接一段备注，片段的高亮下标要加上前面那段文字的长度
    if (inNote >= 0)
    {
        const std::wstring kSeparator = L" \x00B7 ";
        const HighlightedText snippet = MakeHitSnippet(note, keyword, kSnippetLeadChars);
        const int offset = (int)(ownerText.size() + kSeparator.size());
        out.m_sub.m_text += kSeparator;
        out.m_sub.m_text += snippet.m_text;
        if (snippet.m_hiStart >= 0)
        {
            out.m_sub.m_hiStart = offset + snippet.m_hiStart;
            out.m_sub.m_hiLen   = snippet.m_hiLen;
        }
    }
    return out;
}

bool MemoStartToSystemTime(long long start, SYSTEMTIME& out)
{
    //十进制 yyyymmddhhmm 各段的权
    const long long kYearUnit  = 100000000LL;
    const long long kMonthUnit = 1000000LL;
    const long long kDayUnit   = 10000LL;
    const long long kHourUnit  = 100LL;
    const int kMinYear = 1970;
    const int kMaxYear = 9999;
    const int kMonthsPerYear = 12;
    const int kMaxDay = 31;
    const int kHoursPerDay = 24;
    const int kMinutesPerHour = 60;
    if (start <= 0)
    {
        return false;
    }
    const int year   = (int)(start / kYearUnit);
    const int month  = (int)((start / kMonthUnit) % 100);
    const int day    = (int)((start / kDayUnit) % 100);
    const int hour   = (int)((start / kHourUnit) % 100);
    const int minute = (int)(start % 100);
    if (year < kMinYear || year > kMaxYear || month < 1 || month > kMonthsPerYear || day < 1 || day > kMaxDay
        || hour >= kHoursPerDay || minute >= kMinutesPerHour)
    {
        return false;
    }
    SYSTEMTIME st;
    ::ZeroMemory(&st, sizeof(st));
    st.wYear   = (WORD)year;
    st.wMonth  = (WORD)month;
    st.wDay    = (WORD)day;
    st.wHour   = (WORD)hour;
    st.wMinute = (WORD)minute;
    out = st;
    return true;
}

FileHitActions PickFileHitActions(int kind, bool hasLocal)
{
    FileHitActions actions;
    if (!hasLocal)
    {
        return actions;
    }
    if (kind == kFileHitChatFile)
    {
        actions.m_openFile   = true;
        actions.m_openFolder = true;
    }
    else if (kind == kFileHitFolder)
    {
        actions.m_openFolder     = true;
        actions.m_folderIsTarget = true;
    }
    return actions;
}

LocalFileHitInfo DescribeLocalFileRecord(int msgtype, const std::string& contentJson,
                                         const std::string& searchTextUtf8)
{
    LocalFileHitInfo info;
    switch (msgtype)
    {
    //文件夹传输记录：名字是文件夹名，大小是整个文件夹的总字节数
    case ::message_type_folder:
    {
        const balloonhistory::HistoryContentBrief brief =
            balloonhistory::ParseContentBrief(msgtype, contentJson);
        info.m_kind      = kFileHitFolder;
        info.m_nameUtf8  = brief.m_folderName;
        info.m_sizeBytes = brief.m_folderTotalSize;
        info.m_iconKind  = ::share_kind_folder;
        break;
    }
    //文件共享卡片：名字就是搜索文本（入库时取的共享名），图标按共享的是磁盘、文件夹还是文件
    case ::message_type_fileshare:
    {
        info.m_kind     = kFileHitFileShare;
        info.m_nameUtf8 = searchTextUtf8;
        info.m_iconKind = ::share_kind_folder;   //内容里取不到类型时按文件夹显示
        Json::Value root;
        Json::CharReaderBuilder builder;
        JSONCPP_STRING errs;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (!contentJson.empty()
            && reader->parse(contentJson.c_str(), contentJson.c_str() + contentJson.size(), &root, &errs)
            && root.isObject() && root["share"].isObject() && root["share"]["kind"].isInt())
        {
            const int kind = root["share"]["kind"].asInt();
            if (kind == ::share_kind_disk || kind == ::share_kind_folder || kind == ::share_kind_file)
            {
                info.m_iconKind = kind;
            }
        }
        break;
    }
    //聊天文件，以及不认识的类型：名字是文件名，图标按扩展名取
    default:
    {
        const balloonhistory::HistoryContentBrief brief =
            balloonhistory::ParseContentBrief(::message_type_file, contentJson);
        info.m_kind      = kFileHitChatFile;
        info.m_nameUtf8  = brief.m_fileName;
        info.m_sizeBytes = brief.m_fileSize;
        info.m_iconKind  = ::share_kind_file;
        break;
    }
    }

    if (info.m_nameUtf8.empty())
    {
        info.m_nameUtf8 = searchTextUtf8;
    }
    return info;
}

AllTabSectionPick PickAllTabSection(size_t firstCount, size_t secondCount, size_t maxRows)
{
    AllTabSectionPick pick;
    pick.m_firstShown = (firstCount < maxRows) ? firstCount : maxRows;
    const size_t room = maxRows - pick.m_firstShown;
    pick.m_secondShown = (secondCount < room) ? secondCount : room;
    pick.m_showMore    = (firstCount + secondCount) > (pick.m_firstShown + pick.m_secondShown);
    return pick;
}

SearchTabCounts ComputeSearchTabCounts(int contacts, int groups, int messages, int localFiles,
                                       int groupFiles, bool messagesReady, bool filesReady,
                                       bool groupFilesDone)
{
    SearchTabCounts counts;
    const int localMessages = messagesReady ? messages : 0;
    const int localFileHits = filesReady ? localFiles : 0;
    const int groupFileHits = groupFilesDone ? groupFiles : 0;

    counts.m_messages     = localMessages;
    counts.m_files        = localFileHits + groupFileHits;
    counts.m_all          = contacts + groups + localMessages + localFileHits + groupFileHits;
    counts.m_showMessages = messagesReady;
    counts.m_showFiles    = filesReady;
    return counts;
}

std::wstring FormatHitTime(const SYSTEMTIME& msgLocal, const SYSTEMTIME& nowLocal)
{
    wchar_t buf[kFormatBufChars] = { 0 };

    //今天：时:分。
    if (IsSameDay(msgLocal, nowLocal))
    {
        _snwprintf_s(buf, kFormatBufChars, _TRUNCATE, L"%02d:%02d",
                     (int)msgLocal.wHour, (int)msgLocal.wMinute);
        return buf;
    }

    //昨天：把"现在"往前推一天再比较年月日，跨月、跨年都由系统换算处理。
    SYSTEMTIME yesterday;
    if (ComputeYesterday(nowLocal, yesterday) && IsSameDay(msgLocal, yesterday))
    {
        return TR(L"昨天");
    }

    //今年其余日子：月-日。
    if (msgLocal.wYear == nowLocal.wYear)
    {
        _snwprintf_s(buf, kFormatBufChars, _TRUNCATE, L"%02d-%02d",
                     (int)msgLocal.wMonth, (int)msgLocal.wDay);
        return buf;
    }

    //更早：年-月-日。
    _snwprintf_s(buf, kFormatBufChars, _TRUNCATE, L"%d-%02d-%02d",
                 (int)msgLocal.wYear, (int)msgLocal.wMonth, (int)msgLocal.wDay);
    return buf;
}

std::wstring FormatHitTimeFromUnixMs(long long timestampMs)
{
    if (timestampMs <= 0)
    {
        return std::wstring();
    }

    const ULONGLONG ull = kUnixEpochAsFileTime
                        + (ULONGLONG)(timestampMs / kMsPerSecond) * kFileTimePerSecond;
    FILETIME ftUtc;
    ftUtc.dwLowDateTime  = (DWORD)(ull & 0xFFFFFFFFULL);
    ftUtc.dwHighDateTime = (DWORD)(ull >> 32);

    FILETIME   ftLocal;
    SYSTEMTIME msgLocal;
    if (!::FileTimeToLocalFileTime(&ftUtc, &ftLocal) || !::FileTimeToSystemTime(&ftLocal, &msgLocal))
    {
        return std::wstring();
    }

    SYSTEMTIME nowLocal;
    ::GetLocalTime(&nowLocal);
    return FormatHitTime(msgLocal, nowLocal);
}

} // namespace balloonsearch
