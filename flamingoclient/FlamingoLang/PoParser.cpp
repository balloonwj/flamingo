/**
 *  gettext .po 译文文件解析的实现，见 PoParser.h。
 *
 *  按行解析。每个条目由若干关键字行（msgctxt / msgid / msgstr）与续行（以引号开头）组成，条目之间以空行、
 *  注释行或下一条目的关键字行分隔。条目一旦出现任何问题即标记为坏条目：仍按关键字判断条目边界，但不再取值，
 *  到条目结束时整条计入错误数。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "PoParser.h"

#include <string.h>

namespace flamingolang {

namespace {

//UTF-8 BOM 的三个字节
const char kUtf8Bom[] = "\xEF\xBB\xBF";
//UTF-8 BOM 的字节数
const size_t kUtf8BomBytes = 3;

//条目里最近一次赋值的字段，续行拼接到它上面
enum PoField
{
    po_field_none    = 0,   //还没有读到任何关键字，此时出现续行属于格式错误
    po_field_context = 1,   //最近读到的是 msgctxt
    po_field_msgid   = 2,   //最近读到的是 msgid
    po_field_msgstr  = 3,   //最近读到的是 msgstr
};

/**
 *  正在解析的条目。
 */
struct PendingEntry
{
    bool        m_started;      //是否已读到属于本条目的任何一行
    bool        m_broken;       //是否已发现格式错误
    bool        m_hasContext;   //是否读到过 msgctxt
    bool        m_hasMsgid;     //是否读到过 msgid
    bool        m_hasMsgstr;    //是否读到过 msgstr（含复数形式的 msgstr[n]）
    std::string m_context;      //msgctxt 的值
    std::string m_msgid;        //msgid 的值
    std::string m_msgstr;       //msgstr 的值
    PoField     m_lastField;    //最近一次赋值的字段
    uint32_t    m_line;         //条目第一行的行号

    PendingEntry()
    {
        Reset();
    }

    void Reset()
    {
        m_started = false;
        m_broken = false;
        m_hasContext = false;
        m_hasMsgid = false;
        m_hasMsgstr = false;
        m_context.clear();
        m_msgid.clear();
        m_msgstr.clear();
        m_lastField = po_field_none;
        m_line = 0;
    }

    //标记本条目从第 line 行开始（已开始的条目不变）
    void StartAt(uint32_t line)
    {
        if (!m_started)
        {
            m_started = true;
            m_line = line;
        }
    }
};

//判断字符是否为行内空白
bool IsBlank(char c)
{
    return c == ' ' || c == '\t';
}

/**
 *  解析一个带引号的字符串，并还原转义。引号之后只允许有空白。
 *  @param p   指向左引号
 *  @param end 本行末尾（不含换行符）
 *  @param out [出参] 还原转义后的内容，追加在原有内容之后
 *  @return 格式正确时返回 true
 */
bool ParseQuoted(const char* p, const char* end, std::string& out)
{
    if (p >= end || *p != '"')
        return false;
    ++p;
    while (p < end)
    {
        char c = *p;
        if (c == '"')
        {
            ++p;
            while (p < end && IsBlank(*p))
                ++p;
            return p == end;
        }
        if (c == '\\')
        {
            ++p;
            if (p >= end)
                return false;
            switch (*p)
            {
            //换行
            case 'n':
                out.push_back('\n');
                break;
            //制表符
            case 't':
                out.push_back('\t');
                break;
            //回车
            case 'r':
                out.push_back('\r');
                break;
            //双引号本身
            case '"':
                out.push_back('"');
                break;
            //反斜杠本身
            case '\\':
                out.push_back('\\');
                break;
            //其它转义本项目不使用，按格式错误处理，避免把译者的笔误静默吞掉
            default:
                return false;
            }
            ++p;
            continue;
        }
        out.push_back(c);
        ++p;
    }
    //一直到行尾都没有右引号
    return false;
}

/**
 *  判断一行是否以某个关键字开头，且关键字之后紧跟空白（不会把 msgid_plural 当成 msgid）。
 *  @param p       行首（已跳过前导空白）
 *  @param end     行尾
 *  @param keyword 关键字
 *  @param valueAt [出参] 匹配时指向关键字之后第一个非空白字符
 */
bool MatchKeyword(const char* p, const char* end, const char* keyword, const char*& valueAt)
{
    size_t n = strlen(keyword);
    if ((size_t)(end - p) <= n || memcmp(p, keyword, n) != 0 || !IsBlank(p[n]))
        return false;
    p += n;
    while (p < end && IsBlank(*p))
        ++p;
    valueAt = p;
    return true;
}

//判断一行是否以 prefix 开头（不要求其后是空白）
bool StartsWith(const char* p, const char* end, const char* prefix)
{
    size_t n = strlen(prefix);
    return (size_t)(end - p) >= n && memcmp(p, prefix, n) == 0;
}

/**
 *  结束当前条目：有效条目放进结果，坏条目计入错误数，然后把 entry 复位。
 */
void FlushEntry(PendingEntry& entry, PoParseResult& out)
{
    if (!entry.m_started)
        return;

    bool bad = entry.m_broken;
    //有 msgctxt 或 msgstr 却没有 msgid，或有 msgid 却没有 msgstr，都是不完整的条目
    if (!bad && (!entry.m_hasMsgid || !entry.m_hasMsgstr))
        bad = true;

    if (bad)
    {
        ++out.m_errors;
        if (out.m_firstErrorLine == 0)
            out.m_firstErrorLine = entry.m_line;
    }
    else if (entry.m_msgid.empty())
    {
        //msgid 为空的是文件头条目，不是译文
    }
    else if (entry.m_msgstr.empty())
    {
        ++out.m_emptyCount;
    }
    else
    {
        PoEntry item;
        item.m_context = entry.m_context;
        item.m_msgid = entry.m_msgid;
        item.m_msgstr = entry.m_msgstr;
        item.m_line = entry.m_line;
        out.m_entries.push_back(item);
    }
    entry.Reset();
}

/**
 *  处理一行内容（已去掉换行符与前导空白）。
 */
void ProcessLine(const char* p, const char* end, uint32_t lineNo, PendingEntry& entry, PoParseResult& out)
{
    //空行：结束当前条目
    if (p == end)
    {
        FlushEntry(entry, out);
        return;
    }

    //作废条目与注释：都意味着上一条目（如果已经读完 msgid）结束了；行本身忽略
    if (*p == '#')
    {
        if (entry.m_hasMsgid || entry.m_hasMsgstr || StartsWith(p, end, "#~"))
            FlushEntry(entry, out);
        return;
    }

    //续行：拼接到最近一次赋值的字段上
    if (*p == '"')
    {
        entry.StartAt(lineNo);
        if (entry.m_broken)
            return;
        std::string* target = NULL;
        switch (entry.m_lastField)
        {
        //续在 msgctxt 之后
        case po_field_context:
            target = &entry.m_context;
            break;
        //续在 msgid 之后
        case po_field_msgid:
            target = &entry.m_msgid;
            break;
        //续在 msgstr 之后
        case po_field_msgstr:
            target = &entry.m_msgstr;
            break;
        //前面没有关键字的续行不合格式
        default:
            break;
        }
        if (target == NULL || !ParseQuoted(p, end, *target))
            entry.m_broken = true;
        return;
    }

    const char* value = NULL;
    if (MatchKeyword(p, end, "msgctxt", value))
    {
        //msgctxt 总是条目的第一行：前面已有 msgid 或 msgstr 说明上一条目结束了
        if (entry.m_hasMsgid || entry.m_hasMsgstr)
            FlushEntry(entry, out);
        entry.StartAt(lineNo);
        if (entry.m_hasContext)
            entry.m_broken = true;
        entry.m_hasContext = true;
        entry.m_lastField = po_field_context;
        if (!entry.m_broken && !ParseQuoted(value, end, entry.m_context))
            entry.m_broken = true;
        return;
    }
    if (MatchKeyword(p, end, "msgid", value))
    {
        //同一条目只能有一个 msgid：已有 msgid（不论有没有 msgstr）或已有 msgstr，都说明上一条目结束了
        if (entry.m_hasMsgid || entry.m_hasMsgstr)
            FlushEntry(entry, out);
        entry.StartAt(lineNo);
        entry.m_hasMsgid = true;
        entry.m_lastField = po_field_msgid;
        if (!entry.m_broken && !ParseQuoted(value, end, entry.m_msgid))
            entry.m_broken = true;
        return;
    }
    if (MatchKeyword(p, end, "msgstr", value))
    {
        entry.StartAt(lineNo);
        if (!entry.m_hasMsgid || entry.m_hasMsgstr)
            entry.m_broken = true;
        entry.m_hasMsgstr = true;
        entry.m_lastField = po_field_msgstr;
        if (!entry.m_broken && !ParseQuoted(value, end, entry.m_msgstr))
            entry.m_broken = true;
        return;
    }
    if (StartsWith(p, end, "msgstr["))
    {
        //复数形式的译文：本项目不支持，整条按坏条目处理；记为已有 msgstr，以便正确判断条目边界
        entry.StartAt(lineNo);
        entry.m_hasMsgstr = true;
        entry.m_broken = true;
        return;
    }

    //msgid_plural、其它未知关键字与无法识别的内容
    entry.StartAt(lineNo);
    entry.m_broken = true;
}

} // namespace

void ParsePo(const char* data, size_t size, PoParseResult& out)
{
    out.m_entries.clear();
    out.m_errors = 0;
    out.m_emptyCount = 0;
    out.m_firstErrorLine = 0;
    if (data == NULL || size == 0)
        return;

    const char* p = data;
    const char* end = data + size;
    if (size >= kUtf8BomBytes && memcmp(p, kUtf8Bom, kUtf8BomBytes) == 0)
        p += kUtf8BomBytes;

    PendingEntry entry;
    uint32_t lineNo = 0;
    while (p < end)
    {
        ++lineNo;
        const char* lineEnd = (const char*)memchr(p, '\n', (size_t)(end - p));
        const char* next = (lineEnd != NULL) ? lineEnd + 1 : end;
        if (lineEnd == NULL)
            lineEnd = end;

        //去掉 CRLF 的回车与行首空白
        const char* contentEnd = lineEnd;
        if (contentEnd > p && contentEnd[-1] == '\r')
            --contentEnd;
        const char* contentBegin = p;
        while (contentBegin < contentEnd && IsBlank(*contentBegin))
            ++contentBegin;
        //行尾空白也去掉：空白行与空行同样结束条目
        while (contentEnd > contentBegin && IsBlank(contentEnd[-1]))
            --contentEnd;

        ProcessLine(contentBegin, contentEnd, lineNo, entry, out);
        p = next;
    }
    FlushEntry(entry, out);
}

} // namespace flamingolang
