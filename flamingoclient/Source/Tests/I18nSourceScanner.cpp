/**
 *  多语言守卫用例使用的 C++ 源码扫描器的实现，见 I18nSourceScanner.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "I18nSourceScanner.h"

#include <windows.h>
#include <string.h>
#include <algorithm>
#include <set>

namespace i18nscan {

namespace {

//词法单元的种类
enum TokenKind
{
    tok_ident  = 0,     //标识符
    tok_string = 1,     //字符串字面量（已还原转义的 UTF-8 内容）
    tok_punct  = 2,     //括号、逗号、分号、花括号
};

struct Token
{
    int         m_kind;     //取值见 enum TokenKind
    std::string m_value;    //标识符名、字符串内容或标点字符
    int         m_line;     //所在行号（从 1 起）
    bool        m_raw;      //字符串是否为原始字符串字面量
};

//标记宏：名称、上下文参数下标（-1 表示没有）、原文参数下标
struct MarkMacro
{
    const char* m_name;
    int         m_contextArg;
    int         m_sourceArg;
};

const MarkMacro kMarkMacros[] =
{
    { "TR",      -1, 0 },
    { "TRC",      0, 1 },
    { "TRF",     -1, 0 },
    { "TR_NOOP", -1, 0 },
    { "TR8",     -1, 0 },
    { "TRC8",     0, 1 },
    { "TRF8",    -1, 0 },
};

//调用参数里的字面量不算界面文字的函数与宏：日志与调试输出
const char* const kLogCalls[] =
{
    "LOG_INFO", "LOG_WARNING", "LOG_ERROR", "LOG_DEBUG", "LOG_FATAL",
    "LOGI", "LOGW", "LOGE", "LOGD", "LOGF",
    "OutputDebugString", "OutputDebugStringA", "OutputDebugStringW",
    "ATLTRACE", "TRACE", "DUI_TRACE",
};

//内嵌 XML 里需要翻译的属性
const char* const kXmlTextAttrs[] = { "text", "placeholder", "title", "tooltip" };

//XML 实体，按与 i18n_extract.py 相同的顺序依次替换
const char* const kXmlEntities[] = { "&lt;", "&gt;", "&quot;", "&apos;", "&amp;" };
const char* const kXmlEntityChars[] = { "<", ">", "\"", "'", "&" };

//同一行写了这个标记的中文字面量视为有意不翻译
const char* const kIgnoreMarker = "i18n-ignore";

//区块豁免：从写着开始标记的那一行到写着结束标记的那一行（含）之间的中文字面量都视为有意不翻译，
//用于夹在生产代码里的调试用模拟数据（假人名、假消息）
const char* const kIgnoreBeginMarker = "i18n-ignore-begin";
const char* const kIgnoreEndMarker = "i18n-ignore-end";

//扫描的目录与要跳过的子目录（与 i18n_extract.py 一致）
const char* const kScanRoots[] = { "Source", "FlamingoMedia", "FlamingoFileShare", "FlamingoUpdater", "FlamingoLock",
                                   "FlamingoCaption", "FlamingoThreadGroup", "FlamingoScreenshot", "FlamingoBot" };
const char* const kSkipDirs[] = { "Tests", "third-party", "Debug", "Release", "Debug_x64", "Release_x64" };
const char* const kSourceExts[] = { ".cpp", ".h", ".inl" };

//UTF-8 BOM
const char kUtf8Bom[] = "\xEF\xBB\xBF";
const size_t kUtf8BomBytes = 3;

bool IsIdentStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentChar(char c)
{
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}

bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

//十六进制
const int kHexBase = 16;
//\u 与 \U 转义后面的十六进制位数
const size_t kShortUcnDigits = 4;
const size_t kLongUcnDigits = 8;

//text 从 start 起是否恰有 width 个十六进制数字
bool IsHexRun(const std::string& text, size_t start, size_t width)
{
    if (start + width > text.size())
        return false;
    for (size_t k = start; k < start + width; ++k)
    {
        if (!IsHexDigit(text[k]))
            return false;
    }
    return true;
}

//把一个码位按 UTF-8 编码追加到 out
void AppendUtf8(unsigned long cp, std::string& out)
{
    if (cp < 0x80)
    {
        out.push_back((char)cp);
    }
    else if (cp < 0x800)
    {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

//还原普通字符串字面量里的转义（规则同 i18n_extract.py 的 decode_c_escapes）
std::string DecodeEscapes(const std::string& body)
{
    std::string out;
    size_t i = 0;
    const size_t n = body.size();
    while (i < n)
    {
        char c = body[i];
        if (c != '\\' || i + 1 >= n)
        {
            out.push_back(c);
            ++i;
            continue;
        }
        char e = body[i + 1];
        const char* simple = strchr("ntr\"'\\abfv?", e);
        if (simple != NULL && e != '\0')
        {
            static const char kFrom[] = "ntr\"'\\abfv?";
            static const char kTo[] = "\n\t\r\"'\\\a\b\f\v?";
            out.push_back(kTo[strchr(kFrom, e) - kFrom]);
            i += 2;
        }
        else if (e == 'x' && i + 2 < n && IsHexDigit(body[i + 2]))
        {
            size_t j = i + 2;
            while (j < n && IsHexDigit(body[j]))
                ++j;
            unsigned long cp = strtoul(body.substr(i + 2, j - i - 2).c_str(), NULL, kHexBase);
            AppendUtf8(cp, out);
            i = j;
        }
        else if ((e == 'u' || e == 'U') && IsHexRun(body, i + 2, (e == 'u') ? kShortUcnDigits : kLongUcnDigits))
        {
            size_t width = (e == 'u') ? kShortUcnDigits : kLongUcnDigits;
            unsigned long cp = strtoul(body.substr(i + 2, width).c_str(), NULL, kHexBase);
            AppendUtf8(cp, out);
            i += 2 + width;
        }
        else if (e >= '0' && e <= '7')
        {
            size_t j = i + 1;
            unsigned long cp = 0;
            while (j < n && j < i + 4 && body[j] >= '0' && body[j] <= '7')
            {
                cp = cp * 8 + (unsigned long)(body[j] - '0');
                ++j;
            }
            AppendUtf8(cp, out);
            i = j;
        }
        else
        {
            out.push_back(e);
            i += 2;
        }
    }
    return out;
}

//#include 指令
const char* const kIncludeDirective = "#include";

//pos 位于行首（前面只有空白）时返回 true
bool AtLineStart(const std::string& text, size_t pos)
{
    size_t i = pos;
    while (i > 0)
    {
        char c = text[i - 1];
        if (c == '\n')
            return true;
        if (c != ' ' && c != '\t')
            return false;
        --i;
    }
    return true;
}

int CountNewlines(const std::string& text, size_t begin, size_t end)
{
    return (int)std::count(text.begin() + (std::ptrdiff_t)begin, text.begin() + (std::ptrdiff_t)end, '\n');
}

//匹配字符串字面量的前缀（u8 / u / U / L，可再跟 R）与左引号；匹配时返回前缀长度（含引号），否则返回 0
size_t MatchStringPrefix(const std::string& text, size_t i, bool& raw)
{
    static const char* const kPrefixes[] = { "u8R\"", "uR\"", "UR\"", "LR\"", "R\"", "u8\"", "u\"", "U\"", "L\"", "\"" };
    for (size_t k = 0; k < sizeof(kPrefixes) / sizeof(kPrefixes[0]); ++k)
    {
        size_t len = strlen(kPrefixes[k]);
        if (text.compare(i, len, kPrefixes[k]) == 0)
        {
            raw = (strchr(kPrefixes[k], 'R') != NULL);
            return len;
        }
    }
    return 0;
}

void Tokenize(const std::string& text, std::vector<Token>& tokens)
{
    size_t i = 0;
    const size_t n = text.size();
    int line = 1;
    while (i < n)
    {
        char c = text[i];
        if (c == '\n')
        {
            ++line;
            ++i;
            continue;
        }
        if (text.compare(i, 2, "//") == 0)
        {
            size_t j = text.find('\n', i);
            i = (j == std::string::npos) ? n : j;
            continue;
        }
        if (text.compare(i, 2, "/*") == 0)
        {
            size_t j = text.find("*/", i + 2);
            j = (j == std::string::npos) ? n : j + 2;
            line += CountNewlines(text, i, j);
            i = j;
            continue;
        }
        //预处理指令照常扫描（规则同 i18n_extract.py）：宏定义里的 TR_NOOP(L"...") 要收录，未标记的中文宏也要计入。
        //只有 #include 整行跳过：引号里是文件路径，不是代码里的字面量，路径里的反斜杠也不是转义
        if (text.compare(i, strlen(kIncludeDirective), kIncludeDirective) == 0 && AtLineStart(text, i))
        {
            size_t j = text.find('\n', i);
            i = (j == std::string::npos) ? n : j;
            continue;
        }
        bool raw = false;
        size_t prefixLen = MatchStringPrefix(text, i, raw);
        if (prefixLen > 0 && (i == 0 || !IsIdentChar(text[i - 1])))
        {
            Token t;
            t.m_kind = tok_string;
            t.m_line = line;
            t.m_raw = raw;
            if (raw)
            {
                size_t delimBegin = i + prefixLen;
                size_t open = text.find('(', delimBegin);
                if (open == std::string::npos)
                    open = n;
                std::string closeMark = ")" + text.substr(delimBegin, open - delimBegin) + "\"";
                size_t end = text.find(closeMark, open);
                if (end == std::string::npos)
                    end = n;
                t.m_value = text.substr(open + 1, end - open - 1);
                line += CountNewlines(text, open, end);
                tokens.push_back(t);
                i = (end >= n) ? n : end + closeMark.size();
                continue;
            }
            size_t j = i + prefixLen;
            while (j < n && text[j] != '"' && text[j] != '\n')
                j += (text[j] == '\\') ? 2 : 1;
            t.m_value = DecodeEscapes(text.substr(i + prefixLen, j - i - prefixLen));
            tokens.push_back(t);
            i = j + 1;
            continue;
        }
        if (c == '\'')
        {
            size_t j = i + 1;
            while (j < n && text[j] != '\'' && text[j] != '\n')
                j += (text[j] == '\\') ? 2 : 1;
            i = j + 1;
            continue;
        }
        if (IsIdentStart(c))
        {
            size_t j = i + 1;
            while (j < n && IsIdentChar(text[j]))
                ++j;
            Token t;
            t.m_kind = tok_ident;
            t.m_value = text.substr(i, j - i);
            t.m_line = line;
            t.m_raw = false;
            tokens.push_back(t);
            i = j;
            continue;
        }
        if (c >= '0' && c <= '9')
        {
            size_t j = i + 1;
            while (j < n && (IsIdentChar(text[j]) || text[j] == '.' || text[j] == '\''))
                ++j;
            i = j;
            continue;
        }
        if (strchr("(),;{}", c) != NULL && c != '\0')
        {
            Token t;
            t.m_kind = tok_punct;
            t.m_value = std::string(1, c);
            t.m_line = line;
            t.m_raw = false;
            tokens.push_back(t);
        }
        ++i;
    }
}

bool IsPunct(const Token& t, char c)
{
    return t.m_kind == tok_punct && t.m_value.size() == 1 && t.m_value[0] == c;
}

/**
 *  从左括号起按顶层逗号切分实参，每个实参记为词法单元下标的列表。
 *  @return 找到匹配的右括号时返回 true；closeIndex 为右括号的下标
 */
bool SplitCallArgs(const std::vector<Token>& tokens, size_t openIndex, std::vector<std::vector<size_t> >& args,
                   size_t& closeIndex)
{
    args.clear();
    args.push_back(std::vector<size_t>());
    int depth = 0;
    for (size_t i = openIndex; i < tokens.size(); ++i)
    {
        const Token& t = tokens[i];
        if (IsPunct(t, '('))
        {
            ++depth;
            if (depth > 1)
                args.back().push_back(i);
        }
        else if (IsPunct(t, ')'))
        {
            --depth;
            if (depth == 0)
            {
                closeIndex = i;
                return true;
            }
            args.back().push_back(i);
        }
        else if (IsPunct(t, ',') && depth == 1)
        {
            args.push_back(std::vector<size_t>());
        }
        else if (IsPunct(t, ';'))
        {
            return false;
        }
        else
        {
            args.back().push_back(i);
        }
    }
    return false;
}

//实参若只由相邻的字符串字面量组成，拼接后写入 value 并返回 true
bool LiteralValue(const std::vector<Token>& tokens, const std::vector<size_t>& arg, std::string& value)
{
    if (arg.empty())
        return false;
    value.clear();
    for (size_t k = 0; k < arg.size(); ++k)
    {
        if (tokens[arg[k]].m_kind != tok_string)
            return false;
        value += tokens[arg[k]].m_value;
    }
    return true;
}

const MarkMacro* FindMarkMacro(const std::string& name)
{
    for (size_t k = 0; k < sizeof(kMarkMacros) / sizeof(kMarkMacros[0]); ++k)
    {
        if (name == kMarkMacros[k].m_name)
            return &kMarkMacros[k];
    }
    return NULL;
}

bool IsLogCall(const std::string& name)
{
    for (size_t k = 0; k < sizeof(kLogCalls) / sizeof(kLogCalls[0]); ++k)
    {
        if (name == kLogCalls[k])
            return true;
    }
    return false;
}

std::string DecodeXmlEntities(const std::string& value)
{
    std::string out = value;
    for (size_t k = 0; k < sizeof(kXmlEntities) / sizeof(kXmlEntities[0]); ++k)
    {
        const std::string from = kXmlEntities[k];
        const std::string to = kXmlEntityChars[k];
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos)
        {
            out.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return out;
}

bool IsXmlNameChar(char c)
{
    return IsIdentChar(c) || c == '-' || c == ':' || c == '.';
}

//收集一段内嵌 XML 里文字属性中的中文，firstLine 是这段原始字符串开始的行号
void CollectXmlTexts(const std::string& body, int firstLine, FileScanResult& result)
{
    //注释换成同样多的换行，保持行号不变
    std::string xml;
    size_t i = 0;
    while (i < body.size())
    {
        if (body.compare(i, 4, "<!--") == 0)
        {
            size_t end = body.find("-->", i + 4);
            end = (end == std::string::npos) ? body.size() : end + 3;
            xml.append((size_t)CountNewlines(body, i, end), '\n');
            i = end;
            continue;
        }
        xml.push_back(body[i]);
        ++i;
    }

    for (size_t a = 0; a < sizeof(kXmlTextAttrs) / sizeof(kXmlTextAttrs[0]); ++a)
    {
        const std::string name = kXmlTextAttrs[a];
        size_t pos = 0;
        while ((pos = xml.find(name, pos)) != std::string::npos)
        {
            size_t at = pos;
            pos += name.size();
            if (at > 0 && IsXmlNameChar(xml[at - 1]))
                continue;
            size_t j = pos;
            while (j < xml.size() && (xml[j] == ' ' || xml[j] == '\t' || xml[j] == '\r' || xml[j] == '\n'))
                ++j;
            if (j >= xml.size() || xml[j] != '=')
                continue;
            ++j;
            while (j < xml.size() && (xml[j] == ' ' || xml[j] == '\t' || xml[j] == '\r' || xml[j] == '\n'))
                ++j;
            if (j >= xml.size() || (xml[j] != '"' && xml[j] != '\''))
                continue;
            char quote = xml[j];
            size_t end = xml.find(quote, j + 1);
            if (end == std::string::npos)
                continue;
            std::string value = DecodeXmlEntities(xml.substr(j + 1, end - j - 1));
            if (ContainsCjk(value))
            {
                MarkedText m;
                m.m_source = value;
                m.m_line = firstLine + CountNewlines(xml, 0, at);
                result.m_marked.push_back(m);
                result.m_hasXmlText = true;
            }
        }
    }
}

//记下每一行的起始偏移，lineStarts[k] 为第 k + 1 行的起点
void BuildLineIndex(const std::string& text, std::vector<size_t>& lineStarts)
{
    lineStarts.clear();
    lineStarts.push_back(0);
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
            lineStarts.push_back(i + 1);
    }
}

//取第 line 行（从 1 起）的原文
std::string GetLine(const std::string& text, const std::vector<size_t>& lineStarts, int line)
{
    if (line < 1 || (size_t)line > lineStarts.size())
        return std::string();
    size_t begin = lineStarts[(size_t)line - 1];
    size_t end = ((size_t)line < lineStarts.size()) ? lineStarts[(size_t)line] : text.size();
    return text.substr(begin, end - begin);
}

//标出落在豁免区块（i18n-ignore-begin 到 i18n-ignore-end 之间，含两行本身）内的行；没有结束标记时一直到文件末尾
void BuildIgnoredLines(const std::string& text, const std::vector<size_t>& lineStarts, std::vector<bool>& ignored)
{
    ignored.assign(lineStarts.size(), false);
    bool inside = false;
    for (size_t k = 0; k < lineStarts.size(); ++k)
    {
        std::string line = GetLine(text, lineStarts, (int)k + 1);
        if (line.find(kIgnoreBeginMarker) != std::string::npos)
            inside = true;
        ignored[k] = inside;
        if (line.find(kIgnoreEndMarker) != std::string::npos)
            inside = false;
    }
}

bool HasSuffix(const std::string& s, const char* suffix)
{
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

void ListDirectory(const std::string& absDir, const std::string& relDir, std::vector<std::string>& out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = ::FindFirstFileA((absDir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        std::string name = fd.cFileName;
        if (name == "." || name == "..")
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            bool skip = false;
            for (size_t k = 0; k < sizeof(kSkipDirs) / sizeof(kSkipDirs[0]); ++k)
            {
                if (name == kSkipDirs[k])
                    skip = true;
            }
            if (!skip)
                ListDirectory(absDir + "\\" + name, relDir + "/" + name, out);
            continue;
        }
        for (size_t k = 0; k < sizeof(kSourceExts) / sizeof(kSourceExts[0]); ++k)
        {
            if (HasSuffix(name, kSourceExts[k]))
            {
                out.push_back(relDir + "/" + name);
                break;
            }
        }
    } while (::FindNextFileA(h, &fd));
    ::FindClose(h);
}

} // namespace

bool ContainsCjk(const std::string& utf8)
{
    size_t i = 0;
    while (i < utf8.size())
    {
        unsigned char c = (unsigned char)utf8[i];
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size())
        {
            unsigned long cp = ((unsigned long)(c & 0x0F) << 12) | ((unsigned long)(utf8[i + 1] & 0x3F) << 6)
                               | (unsigned long)(utf8[i + 2] & 0x3F);
            if ((cp >= 0x3400 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF))
                return true;
            i += 3;
            continue;
        }
        ++i;
    }
    return false;
}

void ScanSource(const std::string& input, FileScanResult& result)
{
    result = FileScanResult();
    std::string text = input;
    if (text.compare(0, kUtf8BomBytes, kUtf8Bom) == 0)
        text.erase(0, kUtf8BomBytes);

    std::vector<Token> tokens;
    Tokenize(text, tokens);

    //不参与「未标记」检查的字面量：标记宏的原文 / 上下文参数，以及日志调用里的一切字面量
    std::vector<bool> handled(tokens.size(), false);
    for (size_t i = 0; i + 1 < tokens.size(); ++i)
    {
        const Token& t = tokens[i];
        if (t.m_kind != tok_ident || !IsPunct(tokens[i + 1], '('))
            continue;
        if (t.m_value == "TranslateXml")
            result.m_callsTranslateXml = true;

        std::vector<std::vector<size_t> > args;
        size_t closeIndex = 0;
        if (IsLogCall(t.m_value))
        {
            if (SplitCallArgs(tokens, i + 1, args, closeIndex))
            {
                for (size_t k = i + 1; k <= closeIndex; ++k)
                    handled[k] = true;
            }
            continue;
        }
        const MarkMacro* macro = FindMarkMacro(t.m_value);
        if (macro == NULL || !SplitCallArgs(tokens, i + 1, args, closeIndex))
            continue;

        std::string source;
        if (macro->m_sourceArg < (int)args.size() && LiteralValue(tokens, args[(size_t)macro->m_sourceArg], source)
            && !source.empty())
        {
            MarkedText m;
            if (macro->m_contextArg >= 0 && macro->m_contextArg < (int)args.size())
                LiteralValue(tokens, args[(size_t)macro->m_contextArg], m.m_context);
            m.m_source = source;
            m.m_line = t.m_line;
            result.m_marked.push_back(m);
            for (size_t k = 0; k < args[(size_t)macro->m_sourceArg].size(); ++k)
                handled[args[(size_t)macro->m_sourceArg][k]] = true;
            if (macro->m_contextArg >= 0 && macro->m_contextArg < (int)args.size())
            {
                for (size_t k = 0; k < args[(size_t)macro->m_contextArg].size(); ++k)
                    handled[args[(size_t)macro->m_contextArg][k]] = true;
            }
        }
    }

    std::vector<size_t> lineStarts;
    //各行是否落在豁免区块内（下标为行号 - 1），第一次遇到中文字面量时再计算
    std::vector<bool> ignoredLines;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const Token& t = tokens[i];
        if (t.m_kind != tok_string || handled[i])
            continue;
        if (t.m_raw && t.m_value.find('<') != std::string::npos)
        {
            CollectXmlTexts(t.m_value, t.m_line, result);
            continue;
        }
        if (!ContainsCjk(t.m_value))
            continue;
        if (lineStarts.empty())
        {
            BuildLineIndex(text, lineStarts);
            BuildIgnoredLines(text, lineStarts, ignoredLines);
        }
        //同一行写了 i18n-ignore（区块的开始、结束标记也包含这个词），或落在豁免区块内
        if (GetLine(text, lineStarts, t.m_line).find(kIgnoreMarker) != std::string::npos)
            continue;
        if (t.m_line >= 1 && (size_t)t.m_line <= ignoredLines.size() && ignoredLines[(size_t)t.m_line - 1])
            continue;
        UnmarkedLiteral u;
        u.m_text = t.m_value;
        u.m_line = t.m_line;
        result.m_unmarked.push_back(u);
    }
}

std::vector<std::string> ListSourceFiles(const std::string& clientRoot)
{
    std::vector<std::string> files;
    for (size_t k = 0; k < sizeof(kScanRoots) / sizeof(kScanRoots[0]); ++k)
        ListDirectory(clientRoot + "\\" + kScanRoots[k], kScanRoots[k], files);
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace i18nscan
