/**
 *  CodeFormatter.cpp —— 代码片段的格式化，见 CodeFormatter.h（docs/代码片段方案.md 第 10.4 节）。
 *
 *  整体结构：
 *    · 通用整理（CleanupLines）对所有语言都做；
 *    · 括号语言（ReindentBraces）借 CodeLexer 的着色区间判断哪些括号在字符串、注释里；新一行的缩进按「打开最内层
 *      未闭合括号的那一行」再缩一级，所以一行里同时打开圆括号与花括号（如 foo(function() {）时里面只缩一级；
 *    · XML / HTML（ReindentMarkup）用一个按行推进的小状态机数标签层数；
 *    · JSON（PrettyJson）边解析边输出，解析失败即视为不合法；
 *    · SQL（FormatSql）先借 CodeLexer 把关键字转大写，再按语句逐个处理：SELECT / INSERT / UPDATE / DELETE / WITH
 *      开头的语句按子句重新分行，其它语句保留原有的换行。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CodeFormatter.h"

#include <wchar.h>
#include <wctype.h>
#include <vector>

#include "CodeLexer.h"

namespace codesnippet {

namespace {

// ---- 通用整理 ----
const size_t kCollapseBlankRunMin = 3;      //连续这么多行（含）以上的空行压成一行

// ---- 缩进单位 ----
const int kDefaultIndentSpaces = 4;          //判断不出时的缩进：4 个空格
const int kSmallIndentSpaces   = 2;          //也认可的另一种空格缩进：2 个空格

// ---- 统计变化的行数 ----
const size_t kDiffLimitCells = 4000000;      //按行做最长公共子序列比较的规模上限（新旧行数之积），超过时按位置逐行比较

// ---- SQL ----
const size_t kSqlSelectWrapWidth = 80;       //SELECT 的字段列表排在一行时超过这么多字符，就改为每个字段一行

//空格或 Tab
bool IsSpaceChar(wchar_t c)
{
    return c == L' ' || c == L'\t';
}

//按 CRLF、CR、LF 拆成行（不含换行符）；空串得到一个空行
std::vector<std::wstring> SplitLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        if (c == L'\r' || c == L'\n')
        {
            lines.push_back(cur);
            cur.clear();
            if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
                ++i;
            continue;
        }
        cur += c;
    }
    lines.push_back(cur);
    return lines;
}

//用 LF 把各行接起来
std::wstring JoinLines(const std::vector<std::wstring>& lines)
{
    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            out += L'\n';
        out += lines[i];
    }
    return out;
}

//行首的空白（空格与 Tab）
std::wstring LeadingWhitespace(const std::wstring& line)
{
    size_t n = 0;
    while (n < line.size() && IsSpaceChar(line[n]))
        ++n;
    return line.substr(0, n);
}

//去掉行尾空白
std::wstring TrimRight(const std::wstring& s)
{
    size_t end = s.size();
    while (end > 0 && IsSpaceChar(s[end - 1]))
        --end;
    return s.substr(0, end);
}

//去掉首尾空白（含换行）
std::wstring TrimBoth(const std::wstring& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (IsSpaceChar(s[begin]) || s[begin] == L'\r' || s[begin] == L'\n'))
        ++begin;
    while (end > begin && (IsSpaceChar(s[end - 1]) || s[end - 1] == L'\r' || s[end - 1] == L'\n'))
        --end;
    return s.substr(begin, end - begin);
}

//只有空白的行
bool IsBlank(const std::wstring& line)
{
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (!IsSpaceChar(line[i]))
            return false;
    }
    return true;
}

//把一级缩进重复若干次
std::wstring RepeatUnit(const std::wstring& unit, int count)
{
    std::wstring out;
    for (int i = 0; i < count; ++i)
        out += unit;
    return out;
}

//两个非负整数的最大公约数
int Gcd(int a, int b)
{
    while (b != 0)
    {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

//ASCII 字母转大写（只认 ASCII，免得改动其它文字）
std::wstring AsciiUpper(const std::wstring& s)
{
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= L'a' && out[i] <= L'z')
            out[i] = (wchar_t)(out[i] - L'a' + L'A');
    }
    return out;
}

//text 从 pos 起是否以 prefix 开头（ASCII 不区分大小写）
bool StartsWithNoCase(const std::wstring& text, size_t pos, const wchar_t* prefix)
{
    const size_t n = wcslen(prefix);
    if (pos + n > text.size())
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        wchar_t a = text[pos + i];
        wchar_t b = prefix[i];
        if (a >= L'A' && a <= L'Z')
            a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z')
            b = (wchar_t)(b - L'A' + L'a');
        if (a != b)
            return false;
    }
    return true;
}

//通用整理：去掉行尾空白、开头与末尾的空白行，连续多行空行压成一行，去掉所有非空行共同的前导空白
void CleanupLines(std::vector<std::wstring>& lines)
{
    for (size_t i = 0; i < lines.size(); ++i)
        lines[i] = TrimRight(lines[i]);

    size_t first = 0;
    while (first < lines.size() && lines[first].empty())
        ++first;
    size_t last = lines.size();
    while (last > first && lines[last - 1].empty())
        --last;

    std::vector<std::wstring> kept;
    size_t i = first;
    while (i < last)
    {
        if (!lines[i].empty())
        {
            kept.push_back(lines[i]);
            ++i;
            continue;
        }
        //一段连续的空行：太长时只留一行
        size_t run = 0;
        while (i + run < last && lines[i + run].empty())
            ++run;
        const size_t keep = (run >= kCollapseBlankRunMin) ? 1 : run;
        for (size_t k = 0; k < keep; ++k)
            kept.push_back(std::wstring());
        i += run;
    }

    //共同的前导空白：逐个非空行取公共前缀
    bool haveCommon = false;
    std::wstring common;
    for (size_t k = 0; k < kept.size(); ++k)
    {
        if (kept[k].empty())
            continue;
        const std::wstring ws = LeadingWhitespace(kept[k]);
        if (!haveCommon)
        {
            common = ws;
            haveCommon = true;
            continue;
        }
        size_t n = 0;
        while (n < common.size() && n < ws.size() && common[n] == ws[n])
            ++n;
        common = common.substr(0, n);
    }
    if (!common.empty())
    {
        for (size_t k = 0; k < kept.size(); ++k)
        {
            if (!kept[k].empty())
                kept[k] = kept[k].substr(common.size());
        }
    }
    lines.swap(kept);
}

//变了的行数：max(新行数, 旧行数) 减去两者按行的最长公共子序列长度；规模太大时按位置逐行比较
int CountChangedLines(const std::vector<std::wstring>& before, const std::vector<std::wstring>& after)
{
    const size_t n = before.size();
    const size_t m = after.size();
    const size_t longer = (n > m) ? n : m;
    if (n == 0 || m == 0)
        return (int)longer;
    if (n * m > kDiffLimitCells)
    {
        size_t same = 0;
        const size_t shorter = (n < m) ? n : m;
        for (size_t i = 0; i < shorter; ++i)
        {
            if (before[i] == after[i])
                ++same;
        }
        return (int)(longer - same);
    }
    std::vector<int> prev(m + 1, 0);
    std::vector<int> cur(m + 1, 0);
    for (size_t i = 1; i <= n; ++i)
    {
        for (size_t j = 1; j <= m; ++j)
        {
            if (before[i - 1] == after[j - 1])
                cur[j] = prev[j - 1] + 1;
            else
                cur[j] = (prev[j] > cur[j - 1]) ? prev[j] : cur[j - 1];
        }
        prev.swap(cur);
    }
    return (int)(longer - (size_t)prev[m]);
}

//续行的缩进随起始行一起调整：起始行的行首空白由 oldWs 改成了 newWs，续行以 oldWs 开头时同样替换，否则原样保留
std::wstring AdjustIndent(const std::wstring& line, const std::wstring& oldWs, const std::wstring& newWs)
{
    if (line.compare(0, oldWs.size(), oldWs) == 0)
        return newWs + line.substr(oldWs.size());
    return line;
}

//按括号重新缩进的语言
bool IsBraceLanguage(int lang)
{
    return lang == code_lang_cpp || lang == code_lang_csharp || lang == code_lang_java
        || lang == code_lang_javascript || lang == code_lang_go || lang == code_lang_rust || lang == code_lang_php;
}

//有 # 预处理行的语言
bool HasPreprocessor(int lang)
{
    return lang == code_lang_cpp || lang == code_lang_csharp;
}

//有 switch / case 标签的语言：标签与 switch 的花括号同一级
bool HasCaseLabels(int lang)
{
    return lang == code_lang_cpp || lang == code_lang_csharp || lang == code_lang_java
        || lang == code_lang_javascript || lang == code_lang_go || lang == code_lang_php;
}

//以 word 开头、其后不是标识符字符
bool StartsWithWord(const std::wstring& s, const wchar_t* word)
{
    const size_t n = wcslen(word);
    if (s.compare(0, n, word) != 0)
        return false;
    if (s.size() == n)
        return true;
    const wchar_t next = s[n];
    return !(iswalnum(next) || next == L'_');
}

//是 case / default 标签行（已去掉行首空白）
bool IsCaseLabel(const std::wstring& trimmed)
{
    if (StartsWithWord(trimmed, L"case"))
        return true;
    if (!StartsWithWord(trimmed, L"default"))
        return false;
    //default 后面紧跟冒号才是标签（排除 C++ 的 = default、Java 的 default 方法）
    size_t i = wcslen(L"default");
    while (i < trimmed.size() && IsSpaceChar(trimmed[i]))
        ++i;
    return i < trimmed.size() && trimmed[i] == L':';
}

//行尾是续行符 \（预处理指令跨行）
bool EndsWithBackslash(const std::wstring& line)
{
    return !line.empty() && line[line.size() - 1] == L'\\';
}

//左括号
bool IsOpener(wchar_t c)
{
    return c == L'{' || c == L'(' || c == L'[';
}

//右括号
bool IsCloser(wchar_t c)
{
    return c == L'}' || c == L')' || c == L']';
}

/**
 *  一个尚未闭合的左括号。
 */
struct OpenBracket
{
    wchar_t m_ch;           //括号字符
    int     m_lineLevel;    //打开它的那一行的缩进级数
};

//括号语言：按括号重新缩进
std::vector<std::wstring> ReindentBraces(const std::vector<std::wstring>& lines, int lang, const std::wstring& unit)
{
    const std::wstring text = JoinLines(lines);
    std::vector<CodeToken> tokens;
    TokenizeCode(text, lang, tokens);

    //字符串与注释里的字符不参与括号计数
    std::vector<bool> skip(text.size(), false);
    for (size_t t = 0; t < tokens.size(); ++t)
    {
        if (tokens[t].m_kind != code_token_string && tokens[t].m_kind != code_token_comment)
            continue;
        for (int k = 0; k < tokens[t].m_length; ++k)
            skip[(size_t)(tokens[t].m_begin + k)] = true;
    }

    //各行在全文里的起点
    std::vector<size_t> starts;
    size_t offset = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        starts.push_back(offset);
        offset += lines[i].size() + 1;
    }

    //从中间开始的行：所在的字符串或注释区间起于更早的行时，记下那一行
    std::vector<int> continuationOf(lines.size(), -1);
    for (size_t t = 0; t < tokens.size(); ++t)
    {
        if (tokens[t].m_kind != code_token_string && tokens[t].m_kind != code_token_comment)
            continue;
        const size_t begin = (size_t)tokens[t].m_begin;
        const size_t end = begin + (size_t)tokens[t].m_length;
        int beginLine = -1;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (starts[i] <= begin)
                beginLine = (int)i;
            else
                break;
        }
        for (size_t i = (size_t)(beginLine + 1); i < lines.size() && starts[i] < end; ++i)
            continuationOf[i] = beginLine;
    }

    std::vector<std::wstring> out;
    std::vector<std::wstring> oldIndent(lines.size());
    std::vector<std::wstring> newIndent(lines.size());
    std::vector<int> levels(lines.size(), 0);
    std::vector<OpenBracket> stack;
    bool preprocessorContinuation = false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::wstring& line = lines[i];
        const std::wstring ws = LeadingWhitespace(line);
        oldIndent[i] = ws;
        if (line.empty())
        {
            out.push_back(line);
            continue;
        }
        const std::wstring trimmed = line.substr(ws.size());

        //跨行的预处理指令：续行原样保留，不参与括号计数
        if (preprocessorContinuation)
        {
            out.push_back(line);
            newIndent[i] = ws;
            preprocessorContinuation = EndsWithBackslash(line);
            continue;
        }

        size_t scanFrom = ws.size();
        int level = 0;
        std::wstring newLine;
        if (continuationOf[i] >= 0)
        {
            //从跨行注释或字符串的中间开始：随起始行保持相对缩进
            const size_t from = (size_t)continuationOf[i];
            newLine = AdjustIndent(line, oldIndent[from], newIndent[from]);
            level = levels[from];
        }
        else if (HasPreprocessor(lang) && trimmed[0] == L'#')
        {
            //预处理行顶格，不参与括号计数
            out.push_back(trimmed);
            newIndent[i] = std::wstring();
            preprocessorContinuation = EndsWithBackslash(trimmed);
            continue;
        }
        else
        {
            //行首连续的右括号：本行与被它们闭合的最外层那个左括号所在的行同一级
            size_t k = 0;
            size_t popped = 0;
            int closedLevel = -1;
            while (k < trimmed.size())
            {
                const wchar_t c = trimmed[k];
                if (IsSpaceChar(c))
                {
                    ++k;
                    continue;
                }
                if (!IsCloser(c) || skip[starts[i] + ws.size() + k] || popped >= stack.size())
                    break;
                closedLevel = stack[stack.size() - 1 - popped].m_lineLevel;
                ++popped;
                ++k;
            }
            if (popped > 0)
                level = closedLevel;
            else if (stack.empty())
                level = 0;
            else
            {
                const OpenBracket& top = stack.back();
                level = top.m_lineLevel + 1;
                //case / default 标签与 switch 的花括号同一级
                if (top.m_ch == L'{' && HasCaseLabels(lang) && IsCaseLabel(trimmed))
                    level = top.m_lineLevel;
            }
            newLine = RepeatUnit(unit, level) + trimmed;
        }
        out.push_back(newLine);
        newIndent[i] = LeadingWhitespace(newLine);
        levels[i] = level;

        //更新尚未闭合的括号：只数代码里的括号
        for (size_t k = scanFrom; k < line.size(); ++k)
        {
            if (skip[starts[i] + k])
                continue;
            const wchar_t c = line[k];
            if (IsOpener(c))
            {
                OpenBracket open;
                open.m_ch = c;
                open.m_lineLevel = level;
                stack.push_back(open);
            }
            else if (IsCloser(c) && !stack.empty())
            {
                stack.pop_back();
            }
        }
    }
    return out;
}

// ======================= XML / HTML =======================

//标记语言扫描时所处的位置
enum MarkupState
{
    kMarkupText    = 0,   //标签之外的文字
    kMarkupTag     = 1,   //在一个标签里（< 与 > 之间），属性可能跨行
    kMarkupComment = 2,   //在 <!-- --> 注释里
    kMarkupCData   = 3,   //在 <![CDATA[ ]]> 里
    kMarkupPi      = 4,   //在 <? ?> 处理指令里
    kMarkupDecl    = 5,   //在 <!DOCTYPE ...> 这类声明里
    kMarkupOpaque  = 6,   //在 HTML 的 pre / script / style / textarea 内容里：保持原样，直到对应的结束标签
};

//HTML 的空元素：没有结束标签，不加层
bool IsHtmlVoidElement(const std::wstring& lowerName)
{
    static const wchar_t* const kVoid[] = { L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
                                            L"link", L"meta", L"param", L"source", L"track", L"wbr" };
    for (size_t i = 0; i < sizeof(kVoid) / sizeof(kVoid[0]); ++i)
    {
        if (lowerName == kVoid[i])
            return true;
    }
    return false;
}

//HTML 里内容要原样保留的元素
bool IsHtmlOpaqueElement(const std::wstring& lowerName)
{
    return lowerName == L"pre" || lowerName == L"script" || lowerName == L"style" || lowerName == L"textarea";
}

//看起来是 HTML（决定是否按 HTML 的空元素与原样保留元素处理）
bool LooksLikeHtml(const std::wstring& text)
{
    static const wchar_t* const kMarks[] = { L"<!doctype html", L"<html", L"<head", L"<body", L"<div", L"<span",
                                             L"<p>", L"<br", L"<table", L"<ul", L"<script", L"<style" };
    for (size_t pos = 0; pos < text.size(); ++pos)
    {
        if (text[pos] != L'<')
            continue;
        for (size_t i = 0; i < sizeof(kMarks) / sizeof(kMarks[0]); ++i)
        {
            if (StartsWithNoCase(text, pos, kMarks[i]))
                return true;
        }
    }
    return false;
}

//标签名字符
bool IsTagNameChar(wchar_t c)
{
    return iswalnum(c) || c == L'-' || c == L'_' || c == L':' || c == L'.';
}

//转小写（ASCII）
std::wstring AsciiLower(const std::wstring& s)
{
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= L'A' && out[i] <= L'Z')
            out[i] = (wchar_t)(out[i] - L'A' + L'a');
    }
    return out;
}

//XML / HTML：按标签层数重新缩进
std::vector<std::wstring> ReindentMarkup(const std::vector<std::wstring>& lines, const std::wstring& unit, bool html)
{
    std::vector<std::wstring> out;
    std::vector<std::wstring> oldIndent(lines.size());
    std::vector<std::wstring> newIndent(lines.size());
    int depth = 0;
    int state = kMarkupText;
    int stateStartLine = -1;       //注释、CDATA、原样保留内容等跨行状态起于哪一行
    int tagLineLevel = 0;          //当前标签所在行的缩进级数（属性跨行时续行再缩一级）
    bool tagIsClose = false;       //当前标签是结束标签
    std::wstring tagName;          //当前标签名（小写）
    wchar_t quote = 0;             //标签里正在哪种引号里；0 表示不在引号里
    std::wstring opaqueName;       //原样保留的元素名（小写）
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::wstring& line = lines[i];
        const std::wstring ws = LeadingWhitespace(line);
        oldIndent[i] = ws;
        if (line.empty())
        {
            out.push_back(line);
            continue;
        }
        const std::wstring trimmed = line.substr(ws.size());
        int level = depth;
        std::wstring newLine;
        const bool opaqueCloses = (state == kMarkupOpaque)
            && StartsWithNoCase(trimmed, 0, (L"</" + opaqueName).c_str());
        if ((state == kMarkupComment || state == kMarkupCData || state == kMarkupPi || state == kMarkupDecl
             || state == kMarkupOpaque) && !opaqueCloses && stateStartLine >= 0)
        {
            newLine = AdjustIndent(line, oldIndent[(size_t)stateStartLine], newIndent[(size_t)stateStartLine]);
        }
        else if (state == kMarkupTag)
        {
            level = tagLineLevel + 1;
            newLine = RepeatUnit(unit, level) + trimmed;
        }
        else
        {
            if (trimmed.compare(0, 2, L"</") == 0 && level > 0)
                --level;
            newLine = RepeatUnit(unit, level) + trimmed;
        }
        out.push_back(newLine);
        newIndent[i] = LeadingWhitespace(newLine);

        //逐字扫描，更新层数与跨行状态
        size_t k = 0;
        while (k < trimmed.size())
        {
            const wchar_t c = trimmed[k];
            switch (state)
            {
            //标签之外：遇到 < 判断是注释、CDATA、处理指令、声明还是开始 / 结束标签
            case kMarkupText:
                if (c != L'<')
                {
                    ++k;
                    break;
                }
                if (trimmed.compare(k, 4, L"<!--") == 0)
                {
                    state = kMarkupComment;
                    stateStartLine = (int)i;
                    k += 4;
                }
                else if (trimmed.compare(k, 9, L"<![CDATA[") == 0)
                {
                    state = kMarkupCData;
                    stateStartLine = (int)i;
                    k += 9;
                }
                else if (trimmed.compare(k, 2, L"<?") == 0)
                {
                    state = kMarkupPi;
                    stateStartLine = (int)i;
                    k += 2;
                }
                else if (trimmed.compare(k, 2, L"<!") == 0)
                {
                    state = kMarkupDecl;
                    stateStartLine = (int)i;
                    k += 2;
                }
                else
                {
                    tagIsClose = (k + 1 < trimmed.size() && trimmed[k + 1] == L'/');
                    k += tagIsClose ? 2 : 1;
                    size_t nameEnd = k;
                    while (nameEnd < trimmed.size() && IsTagNameChar(trimmed[nameEnd]))
                        ++nameEnd;
                    tagName = AsciiLower(trimmed.substr(k, nameEnd - k));
                    k = nameEnd;
                    tagLineLevel = level;
                    quote = 0;
                    state = kMarkupTag;
                }
                break;

            //标签里：跳过引号里的内容，遇到 > 时按开始 / 结束 / 自闭合标签调整层数
            case kMarkupTag:
                if (quote != 0)
                {
                    if (c == quote)
                        quote = 0;
                    ++k;
                    break;
                }
                if (c == L'"' || c == L'\'')
                {
                    quote = c;
                    ++k;
                    break;
                }
                if (c != L'>')
                {
                    ++k;
                    break;
                }
                state = kMarkupText;
                if (tagIsClose)
                {
                    if (depth > 0)
                        --depth;
                }
                else if (k > 0 && trimmed[k - 1] == L'/')
                {
                    //自闭合标签：不加层
                }
                else if (html && IsHtmlVoidElement(tagName))
                {
                    //HTML 空元素：不加层
                }
                else
                {
                    ++depth;
                    if (html && IsHtmlOpaqueElement(tagName))
                    {
                        state = kMarkupOpaque;
                        opaqueName = tagName;
                        stateStartLine = (int)i;
                    }
                }
                ++k;
                break;

            //注释：直到 -->
            case kMarkupComment:
                if (trimmed.compare(k, 3, L"-->") == 0)
                {
                    state = kMarkupText;
                    k += 3;
                }
                else
                    ++k;
                break;

            //CDATA：直到 ]]>
            case kMarkupCData:
                if (trimmed.compare(k, 3, L"]]>") == 0)
                {
                    state = kMarkupText;
                    k += 3;
                }
                else
                    ++k;
                break;

            //处理指令：直到 ?>
            case kMarkupPi:
                if (trimmed.compare(k, 2, L"?>") == 0)
                {
                    state = kMarkupText;
                    k += 2;
                }
                else
                    ++k;
                break;

            //声明：直到 >
            case kMarkupDecl:
                if (c == L'>')
                    state = kMarkupText;
                ++k;
                break;

            //原样保留的内容：直到对应的结束标签，结束标签照常处理
            case kMarkupOpaque:
                if (c == L'<' && StartsWithNoCase(trimmed, k, (L"</" + opaqueName).c_str()))
                    state = kMarkupText;
                else
                    ++k;
                break;

            //不会出现的状态：按标签之外处理
            default:
                state = kMarkupText;
                break;
            }
        }
    }
    return out;
}

// ======================= JSON =======================

/**
 *  JSON 美化：边按 RFC 8259 解析边输出，键的顺序与数字、字符串的原文都不变。
 */
class JsonPrinter
{
public:
    JsonPrinter(const std::wstring& text, const std::wstring& unit)
        : m_text(text)
        , m_unit(unit)
        , m_pos(0)
    {
    }

    //美化整段文字；不是合法 JSON 时返回 false
    bool Print(std::wstring& out)
    {
        SkipSpace();
        if (!Value(0))
            return false;
        SkipSpace();
        if (m_pos != m_text.size())
            return false;
        out = m_out;
        return true;
    }

private:
    void SkipSpace()
    {
        while (m_pos < m_text.size()
               && (m_text[m_pos] == L' ' || m_text[m_pos] == L'\t' || m_text[m_pos] == L'\r' || m_text[m_pos] == L'\n'))
            ++m_pos;
    }

    void NewLine(int level)
    {
        m_out += L'\n';
        m_out += RepeatUnit(m_unit, level);
    }

    bool Value(int level)
    {
        if (m_pos >= m_text.size())
            return false;
        const wchar_t c = m_text[m_pos];
        if (c == L'{')
            return Container(level, L'{', L'}', true);
        if (c == L'[')
            return Container(level, L'[', L']', false);
        if (c == L'"')
            return String();
        if (c == L'-' || (c >= L'0' && c <= L'9'))
            return Number();
        return Literal(L"true") || Literal(L"false") || Literal(L"null");
    }

    //对象或数组：空的写成 {} / []，否则每个成员一行
    bool Container(int level, wchar_t open, wchar_t close, bool isObject)
    {
        ++m_pos;
        SkipSpace();
        m_out += open;
        if (m_pos < m_text.size() && m_text[m_pos] == close)
        {
            ++m_pos;
            m_out += close;
            return true;
        }
        while (true)
        {
            NewLine(level + 1);
            SkipSpace();
            if (isObject)
            {
                if (m_pos >= m_text.size() || m_text[m_pos] != L'"' || !String())
                    return false;
                SkipSpace();
                if (m_pos >= m_text.size() || m_text[m_pos] != L':')
                    return false;
                ++m_pos;
                m_out += L": ";
                SkipSpace();
            }
            if (!Value(level + 1))
                return false;
            SkipSpace();
            if (m_pos >= m_text.size())
                return false;
            if (m_text[m_pos] == L',')
            {
                ++m_pos;
                m_out += L',';
                continue;
            }
            if (m_text[m_pos] != close)
                return false;
            ++m_pos;
            NewLine(level);
            m_out += close;
            return true;
        }
    }

    //字符串：原样复制，只检查转义与不能出现的控制字符
    bool String()
    {
        const size_t begin = m_pos;
        ++m_pos;
        while (m_pos < m_text.size())
        {
            const wchar_t c = m_text[m_pos];
            if (c == L'\\')
            {
                m_pos += 2;
                continue;
            }
            if (c == L'"')
            {
                ++m_pos;
                m_out += m_text.substr(begin, m_pos - begin);
                return true;
            }
            if (c < 0x20)
                return false;
            ++m_pos;
        }
        return false;
    }

    //数字：-?整数部分(.小数)?(e[+-]?指数)?，原样复制
    bool Number()
    {
        const size_t begin = m_pos;
        if (m_text[m_pos] == L'-')
            ++m_pos;
        if (!Digits())
            return false;
        if (m_pos < m_text.size() && m_text[m_pos] == L'.')
        {
            ++m_pos;
            if (!Digits())
                return false;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == L'e' || m_text[m_pos] == L'E'))
        {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == L'+' || m_text[m_pos] == L'-'))
                ++m_pos;
            if (!Digits())
                return false;
        }
        m_out += m_text.substr(begin, m_pos - begin);
        return true;
    }

    bool Digits()
    {
        const size_t begin = m_pos;
        while (m_pos < m_text.size() && m_text[m_pos] >= L'0' && m_text[m_pos] <= L'9')
            ++m_pos;
        return m_pos > begin;
    }

    bool Literal(const wchar_t* word)
    {
        const size_t n = wcslen(word);
        if (m_text.compare(m_pos, n, word) != 0)
            return false;
        m_pos += n;
        m_out += word;
        return true;
    }

private:
    std::wstring        m_text;   //原文（副本：调用方常传入临时串）
    std::wstring        m_unit;   //一级缩进
    size_t              m_pos;    //当前解析位置
    std::wstring        m_out;    //输出
};

// ======================= SQL =======================

//SQL 词元的类别
enum SqlTokenType
{
    kSqlWord         = 0,   //标识符或关键字（含 @变量、#临时表）
    kSqlNumber       = 1,   //数字
    kSqlString       = 2,   //单引号字符串（'' 表示一个引号）
    kSqlQuotedIdent  = 3,   //双引号、反引号或方括号括起的标识符
    kSqlLineComment  = 4,   //-- 行注释
    kSqlBlockComment = 5,   ///* */ 块注释
    kSqlOpenParen    = 6,   //左圆括号
    kSqlCloseParen   = 7,   //右圆括号
    kSqlComma        = 8,   //逗号
    kSqlSemicolon    = 9,   //分号：语句结束
    kSqlDot          = 10,  //点：表名与列名之间
    kSqlOperator     = 11,  //其它运算符与符号
};

/**
 *  一个 SQL 词元。
 */
struct SqlToken
{
    int          m_type;          //类别，取值见 SqlTokenType
    std::wstring m_text;          //原文
    bool         m_spaceBefore;   //原文里它前面紧挨着空白
};

//拆成词元（空白不输出，只记在下一个词元的 m_spaceBefore 上）
std::vector<SqlToken> TokenizeSql(const std::wstring& text)
{
    std::vector<SqlToken> out;
    size_t i = 0;
    bool space = false;
    while (i < text.size())
    {
        const wchar_t c = text[i];
        if (IsSpaceChar(c) || c == L'\r' || c == L'\n')
        {
            space = true;
            ++i;
            continue;
        }
        SqlToken tok;
        tok.m_spaceBefore = space;
        space = false;
        const size_t begin = i;
        if (c == L'-' && i + 1 < text.size() && text[i + 1] == L'-')
        {
            while (i < text.size() && text[i] != L'\r' && text[i] != L'\n')
                ++i;
            tok.m_type = kSqlLineComment;
        }
        else if (c == L'/' && i + 1 < text.size() && text[i + 1] == L'*')
        {
            const size_t end = text.find(L"*/", i + 2);
            i = (end == std::wstring::npos) ? text.size() : end + 2;
            tok.m_type = kSqlBlockComment;
        }
        else if (c == L'\'')
        {
            ++i;
            while (i < text.size())
            {
                if (text[i] == L'\'' && i + 1 < text.size() && text[i + 1] == L'\'')
                {
                    i += 2;
                    continue;
                }
                if (text[i] == L'\'')
                {
                    ++i;
                    break;
                }
                ++i;
            }
            tok.m_type = kSqlString;
        }
        else if (c == L'"' || c == L'`' || c == L'[')
        {
            const wchar_t close = (c == L'[') ? L']' : c;
            const size_t end = text.find(close, i + 1);
            i = (end == std::wstring::npos) ? text.size() : end + 1;
            tok.m_type = kSqlQuotedIdent;
        }
        else if (iswdigit(c))
        {
            while (i < text.size() && (iswalnum(text[i]) || text[i] == L'.'))
                ++i;
            tok.m_type = kSqlNumber;
        }
        else if (iswalpha(c) || c == L'_' || c == L'@' || c == L'#' || c == L'$' || c > 0x7F)
        {
            ++i;
            while (i < text.size() && (iswalnum(text[i]) || text[i] == L'_' || text[i] == L'$' || text[i] > 0x7F))
                ++i;
            tok.m_type = kSqlWord;
        }
        else
        {
            ++i;
            switch (c)
            {
            //括号、逗号、分号、点各自成为一个词元
            case L'(':
                tok.m_type = kSqlOpenParen;
                break;
            case L')':
                tok.m_type = kSqlCloseParen;
                break;
            case L',':
                tok.m_type = kSqlComma;
                break;
            case L';':
                tok.m_type = kSqlSemicolon;
                break;
            case L'.':
                tok.m_type = kSqlDot;
                break;

            //运算符：两个字符的比较与拼接运算符合成一个词元
            default:
                tok.m_type = kSqlOperator;
                if (i < text.size())
                {
                    const wchar_t n = text[i];
                    if ((c == L'<' && (n == L'=' || n == L'>')) || (c == L'>' && n == L'=') || (c == L'!' && n == L'=')
                        || (c == L'|' && n == L'|') || (c == L':' && n == L':'))
                        ++i;
                }
                break;
            }
        }
        tok.m_text = text.substr(begin, i - begin);
        out.push_back(tok);
    }
    return out;
}

/**
 *  一层括号：语句本身、子查询或普通括号（函数调用、IN 列表等）。
 */
struct SqlFrame
{
    bool         m_inline;          //普通括号：里面不分行
    int          m_level;           //子句所在的缩进级数
    int          m_openLineLevel;   //左括号所在行的缩进级数（子查询的右括号回到这一级）
    std::wstring m_clause;          //当前所在的子句（大写），如 SELECT、WHERE
    bool         m_selectWrap;      //SELECT 的字段列表每个字段一行
    bool         m_betweenPending;  //刚出现 BETWEEN：下一个 AND 属于 BETWEEN，不分行
};

/**
 *  按行组装 SQL 输出。
 */
class SqlWriter
{
public:
    explicit SqlWriter(const std::wstring& unit)
        : m_unit(unit)
        , m_level(0)
    {
    }

    //结束当前行，后面的内容从新的一行、按 level 级缩进开始
    void NewLine(int level)
    {
        if (!m_cur.empty())
            m_lines.push_back(RepeatUnit(m_unit, m_level) + m_cur);
        m_cur.clear();
        m_level = level;
    }

    //追加一个词元；space 为 true 时与前文之间空一格（行首不空）
    void Append(const std::wstring& text, bool space)
    {
        if (!m_cur.empty() && space)
            m_cur += L' ';
        m_cur += text;
    }

    //当前行的缩进级数
    int Level() const
    {
        return m_level;
    }

    //结束并取出全部行
    std::vector<std::wstring> Finish()
    {
        NewLine(m_level);
        return m_lines;
    }

private:
    std::wstring              m_unit;    //一级缩进
    std::vector<std::wstring> m_lines;   //已完成的行
    std::wstring              m_cur;     //正在组装的行（不含缩进）
    int                       m_level;   //正在组装的行的缩进级数
};

//下标 i 之后第一个不是注释的词元；没有时返回 -1
int NextSignificant(const std::vector<SqlToken>& toks, size_t i)
{
    for (size_t k = i + 1; k < toks.size(); ++k)
    {
        if (toks[k].m_type != kSqlLineComment && toks[k].m_type != kSqlBlockComment)
            return (int)k;
    }
    return -1;
}

//下标 i 之后第一个有意义的词元是否是 word（大写比较）
bool NextWordIs(const std::vector<SqlToken>& toks, size_t i, const wchar_t* word)
{
    const int k = NextSignificant(toks, i);
    return k >= 0 && toks[(size_t)k].m_type == kSqlWord && AsciiUpper(toks[(size_t)k].m_text) == word;
}

//连接修饰词：后面跟 JOIN 或 OUTER 时，在它们前面分行
bool IsJoinModifier(const std::wstring& upper)
{
    return upper == L"LEFT" || upper == L"RIGHT" || upper == L"FULL" || upper == L"INNER" || upper == L"CROSS"
        || upper == L"NATURAL" || upper == L"OUTER";
}

//SELECT 后面的字段列表排在一行时大约多长（到同一层的 FROM 或子句结束为止）
size_t SelectListWidth(const std::vector<SqlToken>& toks, size_t selectIndex)
{
    size_t width = 0;
    int depth = 0;
    for (size_t k = selectIndex + 1; k < toks.size(); ++k)
    {
        const SqlToken& t = toks[k];
        if (t.m_type == kSqlOpenParen)
            ++depth;
        else if (t.m_type == kSqlCloseParen)
        {
            if (depth == 0)
                break;
            --depth;
        }
        else if (t.m_type == kSqlSemicolon && depth == 0)
            break;
        else if (t.m_type == kSqlWord && depth == 0)
        {
            const std::wstring upper = AsciiUpper(t.m_text);
            if (upper == L"FROM" || upper == L"INTO" || upper == L"WHERE" || upper == L"UNION")
                break;
        }
        width += t.m_text.size() + 1;
    }
    return width;
}

//一条 SELECT / INSERT / UPDATE / DELETE / WITH 语句按子句分行
std::vector<std::wstring> FormatSqlStatement(const std::vector<SqlToken>& toks, const std::wstring& unit)
{
    SqlWriter writer(unit);
    std::vector<SqlFrame> frames;
    SqlFrame root;
    root.m_inline = false;
    root.m_level = 0;
    root.m_openLineLevel = 0;
    root.m_selectWrap = false;
    root.m_betweenPending = false;
    frames.push_back(root);
    const bool isUpdate = !toks.empty() && toks[0].m_type == kSqlWord && AsciiUpper(toks[0].m_text) == L"UPDATE";
    bool wrapPending = false;       //SELECT 之后的第一个字段要另起一行（DISTINCT / ALL 仍留在 SELECT 那一行）
    std::wstring prevWord;          //上一个词（大写）
    int prevType = -1;              //上一个有意义词元的类别
    for (size_t i = 0; i < toks.size(); ++i)
    {
        const SqlToken& t = toks[i];
        SqlFrame& frame = frames.back();
        bool space = true;
        if (t.m_type == kSqlComma || t.m_type == kSqlCloseParen || t.m_type == kSqlDot || t.m_type == kSqlSemicolon)
            space = false;
        if (prevType == kSqlOpenParen || prevType == kSqlDot)
            space = false;

        if (t.m_type == kSqlWord)
        {
            const std::wstring upper = AsciiUpper(t.m_text);
            bool breakBefore = false;
            int breakLevel = frame.m_level;
            if (!frame.m_inline)
            {
                if (upper == L"SELECT" || upper == L"WHERE" || upper == L"HAVING" || upper == L"LIMIT"
                    || upper == L"VALUES" || upper == L"INSERT" || upper == L"DELETE" || upper == L"UNION"
                    || upper == L"EXCEPT" || upper == L"INTERSECT" || (upper == L"UPDATE" && i == 0))
                    breakBefore = true;
                else if (upper == L"FROM")
                    breakBefore = (prevWord != L"DELETE");
                else if (upper == L"SET")
                    breakBefore = isUpdate;
                else if (upper == L"GROUP" || upper == L"ORDER")
                    breakBefore = NextWordIs(toks, i, L"BY");
                else if (upper == L"JOIN")
                    breakBefore = !IsJoinModifier(prevWord);
                else if (IsJoinModifier(upper) && upper != L"OUTER")
                    breakBefore = NextWordIs(toks, i, L"JOIN") || NextWordIs(toks, i, L"OUTER");
                else if ((upper == L"AND" || upper == L"OR")
                         && (frame.m_clause == L"WHERE" || frame.m_clause == L"ON" || frame.m_clause == L"HAVING"))
                {
                    if (upper == L"AND" && frame.m_betweenPending)
                        frame.m_betweenPending = false;
                    else
                    {
                        breakBefore = true;
                        breakLevel = frame.m_level + 1;
                    }
                }
            }
            //记下所在的子句
            if (breakBefore && upper != L"AND" && upper != L"OR")
            {
                frame.m_clause = upper;
                frame.m_selectWrap = false;
                if (upper == L"LEFT" || upper == L"RIGHT" || upper == L"FULL" || upper == L"INNER" || upper == L"CROSS"
                    || upper == L"NATURAL" || upper == L"JOIN")
                    frame.m_clause = L"JOIN";
            }
            if (upper == L"ON")
                frame.m_clause = L"ON";
            if (upper == L"BETWEEN")
                frame.m_betweenPending = true;

            if (breakBefore)
                writer.NewLine(breakLevel);
            else if (wrapPending && upper != L"DISTINCT" && upper != L"ALL")
            {
                writer.NewLine(frame.m_level + 1);
                wrapPending = false;
            }
            writer.Append(t.m_text, space);
            if (upper == L"SELECT" && !frame.m_inline && SelectListWidth(toks, i) > kSqlSelectWrapWidth)
            {
                frame.m_selectWrap = true;
                wrapPending = true;
            }
            prevWord = upper;
            prevType = t.m_type;
            continue;
        }

        if (wrapPending && t.m_type != kSqlLineComment && t.m_type != kSqlBlockComment)
        {
            writer.NewLine(frame.m_level + 1);
            wrapPending = false;
        }

        switch (t.m_type)
        {
        //左括号：后面是 SELECT / WITH 的是子查询，里面按子句分行；否则是普通括号，里面不分行
        case kSqlOpenParen:
        {
            //函数名与左括号之间按原文：原文紧挨着就不空格
            if (prevType == kSqlWord && !t.m_spaceBefore)
                space = false;
            writer.Append(t.m_text, space);
            SqlFrame inner;
            inner.m_openLineLevel = writer.Level();
            inner.m_selectWrap = false;
            inner.m_betweenPending = false;
            if (!frame.m_inline && (NextWordIs(toks, i, L"SELECT") || NextWordIs(toks, i, L"WITH")))
            {
                inner.m_inline = false;
                inner.m_level = writer.Level() + 1;
            }
            else
            {
                inner.m_inline = true;
                inner.m_level = frame.m_level;
            }
            frames.push_back(inner);
            break;
        }

        //右括号：子查询的右括号单独一行，回到左括号所在行的缩进
        case kSqlCloseParen:
            if (frames.size() > 1)
            {
                const SqlFrame closing = frames.back();
                frames.pop_back();
                if (!closing.m_inline)
                    writer.NewLine(closing.m_openLineLevel);
            }
            writer.Append(t.m_text, false);
            break;

        //逗号：SELECT 字段每个一行时逗号后换行
        case kSqlComma:
            writer.Append(t.m_text, false);
            if (!frame.m_inline && frame.m_selectWrap)
                writer.NewLine(frame.m_level + 1);
            break;

        //行注释：照写，后面强制换行
        case kSqlLineComment:
            writer.Append(t.m_text, true);
            writer.NewLine(writer.Level());
            break;

        //其它词元：按空格规则追加
        default:
            writer.Append(t.m_text, space);
            break;
        }
        if (t.m_type != kSqlLineComment && t.m_type != kSqlBlockComment)
            prevType = t.m_type;
        if (t.m_type != kSqlWord)
            prevWord.clear();
    }
    return writer.Finish();
}

//SQL：关键字转大写；SELECT / INSERT / UPDATE / DELETE / WITH 开头的语句按子句分行，其它语句保留原有的换行
std::vector<std::wstring> FormatSql(const std::vector<std::wstring>& lines, const std::wstring& unit)
{
    std::wstring text = JoinLines(lines);

    //关键字与类型名转大写：着色区间来自 CodeLexer 的 SQL 表（不区分大小写）
    std::vector<CodeToken> tokens;
    TokenizeCode(text, code_lang_sql, tokens);
    for (size_t t = 0; t < tokens.size(); ++t)
    {
        if (tokens[t].m_kind != code_token_keyword && tokens[t].m_kind != code_token_type)
            continue;
        const size_t begin = (size_t)tokens[t].m_begin;
        const size_t len = (size_t)tokens[t].m_length;
        text.replace(begin, len, AsciiUpper(text.substr(begin, len)));
    }

    //按顶层的分号拆成语句，各自处理
    const std::vector<SqlToken> all = TokenizeSql(text);
    std::vector<std::wstring> out;
    size_t begin = 0;
    size_t textPos = 0;   //上一条语句在原文里结束的位置（分号之后）
    while (begin < all.size())
    {
        size_t end = begin;
        while (end < all.size() && all[end].m_type != kSqlSemicolon)
            ++end;
        std::vector<SqlToken> stmt(all.begin() + (long)begin, all.begin() + (long)end);
        const bool hasSemicolon = (end < all.size());

        //这条语句在原文里的范围：按词元依次在原文里查找，得到结束位置（含分号）
        size_t stmtEnd = textPos;
        for (size_t k = begin; k <= end && k < all.size(); ++k)
        {
            const size_t found = text.find(all[k].m_text, stmtEnd);
            if (found != std::wstring::npos)
                stmtEnd = found + all[k].m_text.size();
        }
        const std::wstring raw = text.substr(textPos, stmtEnd - textPos);
        textPos = stmtEnd;

        //第一个不是注释的词决定这条语句是否按子句分行
        int first = -1;
        for (size_t k = 0; k < stmt.size(); ++k)
        {
            if (stmt[k].m_type != kSqlLineComment && stmt[k].m_type != kSqlBlockComment)
            {
                first = (int)k;
                break;
            }
        }
        const std::wstring firstWord = (first >= 0 && stmt[(size_t)first].m_type == kSqlWord)
                                     ? AsciiUpper(stmt[(size_t)first].m_text) : std::wstring();
        std::vector<std::wstring> stmtLines;
        if (firstWord == L"SELECT" || firstWord == L"INSERT" || firstWord == L"UPDATE" || firstWord == L"DELETE"
            || firstWord == L"WITH")
        {
            stmtLines = FormatSqlStatement(stmt, unit);
            if (hasSemicolon && !stmtLines.empty())
                stmtLines.back() += L';';
        }
        else
        {
            //不认识的语句：保留原有的换行与缩进（只去掉首尾空白）
            stmtLines = SplitLines(TrimBoth(raw));
            CleanupLines(stmtLines);
        }
        for (size_t k = 0; k < stmtLines.size(); ++k)
        {
            if (!stmtLines[k].empty() || !out.empty())
                out.push_back(stmtLines[k]);
        }
        begin = hasSemicolon ? end + 1 : end;
    }
    CleanupLines(out);
    return out;
}

} // anonymous namespace

std::wstring DetectIndentUnit(const std::wstring& code)
{
    const std::vector<std::wstring> lines = SplitLines(code);
    int tabLines = 0;
    int spaceLines = 0;
    int gcd = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::wstring& line = lines[i];
        if (line.empty() || IsBlank(line))
            continue;
        if (line[0] == L'\t')
        {
            ++tabLines;
            continue;
        }
        if (line[0] != L' ')
            continue;
        int spaces = 0;
        while ((size_t)spaces < line.size() && line[(size_t)spaces] == L' ')
            ++spaces;
        ++spaceLines;
        gcd = Gcd(gcd, spaces);
    }
    if (tabLines > 0 && tabLines >= spaceLines)
        return L"\t";
    if (gcd == kSmallIndentSpaces || gcd == kDefaultIndentSpaces)
        return std::wstring((size_t)gcd, L' ');
    return std::wstring((size_t)kDefaultIndentSpaces, L' ');
}

CodeFormatResult FormatCode(const std::wstring& code, int lang)
{
    CodeFormatResult result;
    const std::vector<std::wstring> before = SplitLines(code);
    std::vector<std::wstring> lines = before;
    CleanupLines(lines);
    if (lines.empty())
    {
        result.m_changedLines = CountChangedLines(before, lines);
        return result;
    }
    const std::wstring unit = DetectIndentUnit(JoinLines(lines));

    if (IsBraceLanguage(lang))
        lines = ReindentBraces(lines, lang, unit);
    else if (lang == code_lang_json)
    {
        std::wstring pretty;
        JsonPrinter printer(JoinLines(lines), unit);
        if (printer.Print(pretty))
            lines = SplitLines(pretty);
        else
            result.m_jsonInvalid = true;
    }
    else if (lang == code_lang_xml)
    {
        const std::wstring text = JoinLines(lines);
        lines = ReindentMarkup(lines, unit, LooksLikeHtml(text));
    }
    else if (lang == code_lang_sql)
        lines = FormatSql(lines, unit);

    result.m_text = JoinLines(lines);
    result.m_changedLines = CountChangedLines(before, lines);
    return result;
}

CodeFormatResult FormatCodeSelection(const std::wstring& lines, int lang)
{
    const std::vector<std::wstring> before = SplitLines(lines);

    //选区首尾的空行原样保留（通用整理会去掉它们，与上下文之间的空行就丢了）
    size_t leadingBlank = 0;
    while (leadingBlank < before.size() && IsBlank(before[leadingBlank]))
        ++leadingBlank;
    size_t trailingBlank = 0;
    while (trailingBlank < before.size() - leadingBlank && IsBlank(before[before.size() - 1 - trailingBlank]))
        ++trailingBlank;
    std::wstring base;
    if (leadingBlank < before.size())
        base = LeadingWhitespace(before[leadingBlank]);

    CodeFormatResult formatted = FormatCode(lines, lang);
    std::vector<std::wstring> out;
    for (size_t i = 0; i < leadingBlank; ++i)
        out.push_back(std::wstring());
    if (!formatted.m_text.empty())
    {
        const std::vector<std::wstring> body = SplitLines(formatted.m_text);
        for (size_t i = 0; i < body.size(); ++i)
            out.push_back(body[i].empty() ? body[i] : base + body[i]);
    }
    for (size_t i = 0; i < trailingBlank; ++i)
        out.push_back(std::wstring());

    CodeFormatResult result;
    result.m_text = JoinLines(out);
    result.m_jsonInvalid = formatted.m_jsonInvalid;
    result.m_changedLines = CountChangedLines(before, out);
    return result;
}

} // namespace codesnippet
