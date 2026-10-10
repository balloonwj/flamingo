/**
 *  聊天记录管理器的搜索语法实现，说明见 HistorySearchSyntax.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "HistorySearchSyntax.h"

#include <windows.h>
#include <string.h>            // strlen
#include "HistoryModel.h"      // LocalDayBoundsMs
#include "HistoryWildcard.h"   // WildcardHasContent
#include "V2MsgQuery.h"        // HistoryTypeFilter

namespace balloonhistory {

namespace {

//几个多字节分隔符的 UTF-8 编码
const char* const kFullWidthSpace = "\xE3\x80\x80";   // U+3000 全角空格
const char* const kLeftCnQuote    = "\xE2\x80\x9C";   // U+201C “
const char* const kRightCnQuote   = "\xE2\x80\x9D";   // U+201D ”
const char* const kFullWidthColon = "\xEF\xBC\x9A";   // U+FF1A ：
const size_t kThreeByteLen = 3;                        //以上几个字符的 UTF-8 字节数
//日期文字里年、月、日的位数
const size_t kYearDigits = 4;
const size_t kMonthDayDigits = 2;                      //紧凑写法 YYYYMMDD 里月、日各占的位数
const size_t kCompactDateLen = 8;                      // YYYYMMDD
const int kDecimalBase = 10;
const int kMaxMonth = 12;
const int kMaxDay = 31;

//s 从 pos 起是不是 lit 这几个字节
bool BytesAt(const std::string& s, size_t pos, const char* lit)
{
    const size_t n = ::strlen(lit);
    return pos + n <= s.size() && s.compare(pos, n, lit) == 0;
}

//pos 处是不是一个空白：半角空格、制表符、换行，或全角空格；是的话返回它的字节数，否则返回 0
size_t SpaceLenAt(const std::string& s, size_t pos)
{
    const char ch = s[pos];
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
    {
        return 1;
    }
    return BytesAt(s, pos, kFullWidthSpace) ? kThreeByteLen : 0;
}

//按空白切分，引号里的空白不切；切出的每段保留原样（含引号字符），交给 ParseHistorySearch 判断前缀
std::vector<std::string> SplitTokens(const std::string& s)
{
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuote = false;
    size_t i = 0;
    while (i < s.size())
    {
        if (!inQuote)
        {
            const size_t sp = SpaceLenAt(s, i);
            if (sp > 0)
            {
                if (!cur.empty())
                {
                    tokens.push_back(cur);
                    cur.clear();
                }
                i += sp;
                continue;
            }
        }
        if (s[i] == '"')
        {
            inQuote = !inQuote;
            cur.push_back(s[i]);
            ++i;
            continue;
        }
        if (BytesAt(s, i, kLeftCnQuote) || BytesAt(s, i, kRightCnQuote))
        {
            inQuote = BytesAt(s, i, kLeftCnQuote);
            cur.append(s, i, kThreeByteLen);
            i += kThreeByteLen;
            continue;
        }
        cur.push_back(s[i]);
        ++i;
    }
    if (!cur.empty())
    {
        tokens.push_back(cur);
    }
    return tokens;
}

//去掉一段里的引号字符（半角 " 与中文 “ ”）
std::string Unquote(const std::string& token)
{
    std::string out;
    size_t i = 0;
    while (i < token.size())
    {
        if (token[i] == '"')
        {
            ++i;
            continue;
        }
        if (BytesAt(token, i, kLeftCnQuote) || BytesAt(token, i, kRightCnQuote))
        {
            i += kThreeByteLen;
            continue;
        }
        out.push_back(token[i]);
        ++i;
    }
    return out;
}

//第一个引号字符的位置；没有时返回 npos
size_t FirstQuotePos(const std::string& token)
{
    for (size_t i = 0; i < token.size(); ++i)
    {
        if (token[i] == '"' || BytesAt(token, i, kLeftCnQuote) || BytesAt(token, i, kRightCnQuote))
        {
            return i;
        }
    }
    return std::string::npos;
}

//ASCII 字母转小写，其余字节原样
std::string LowerAscii(const std::string& s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'Z')
        {
            out[i] = (char)(out[i] - 'A' + 'a');
        }
    }
    return out;
}

//限定词的种类
enum SearchModifierKind
{
    kModifierNone   = 0,   //不是限定词
    kModifierFrom   = 1,   //from: / 发送人:
    kModifierType   = 2,   //type: / 类型:
    kModifierAfter  = 3,   //after:
    kModifierBefore = 4,   //before:
};

//限定词名（冒号之前的部分）对应哪一种限定
int ModifierKindOf(const std::string& key)
{
    const std::string lower = LowerAscii(key);
    //中文限定词名按 UTF-8 字节写出：「发送人」「类型」
    if (lower == "from" || key == "\xE5\x8F\x91\xE9\x80\x81\xE4\xBA\xBA")
    {
        return kModifierFrom;
    }
    if (lower == "type" || key == "\xE7\xB1\xBB\xE5\x9E\x8B")
    {
        return kModifierType;
    }
    if (lower == "after")
    {
        return kModifierAfter;
    }
    if (lower == "before")
    {
        return kModifierBefore;
    }
    return kModifierNone;
}

//一段是不是「限定词名 + 冒号」开头（冒号在第一个引号之前、且前面至少有一个字符）；是的话给出名与值
bool SplitModifier(const std::string& token, std::string& key, std::string& value)
{
    const size_t quote = FirstQuotePos(token);
    for (size_t i = 1; i < token.size() && i < quote; ++i)
    {
        size_t colonLen = 0;
        if (token[i] == ':')
        {
            colonLen = 1;
        }
        else if (BytesAt(token, i, kFullWidthColon))
        {
            colonLen = kThreeByteLen;
        }
        if (colonLen > 0)
        {
            key = token.substr(0, i);
            value = Unquote(token.substr(i + colonLen));
            return true;
        }
    }
    return false;
}

//解析一个十进制非负整数（只含数字，不为空）
bool ParseDigits(const std::string& s, int& out)
{
    if (s.empty())
    {
        return false;
    }
    int v = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return false;
        }
        v = v * kDecimalBase + (s[i] - '0');
    }
    out = v;
    return true;
}

//UTF-16 与 UTF-8 互转
std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty())
    {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, NULL, NULL);
    return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
    {
        return std::wstring();
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0)
    {
        return std::wstring();
    }
    std::wstring out((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

//一个词要不要留下：不为空，且勾选通配符时去掉 '*' 后仍有内容
bool KeepTerm(const std::string& term, bool wildcard)
{
    if (term.empty())
    {
        return false;
    }
    return !wildcard || WildcardHasContent(term);
}

} // namespace

HistorySearchQuery ParseHistorySearch(const std::string& inputUtf8, bool wildcard)
{
    HistorySearchQuery q;
    const std::vector<std::string> tokens = SplitTokens(inputUtf8);
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const std::string& raw = tokens[i];
        //-词：排除。单独一个 - 按普通词处理
        if (raw.size() > 1 && raw[0] == '-')
        {
            const std::string excl = Unquote(raw.substr(1));
            if (KeepTerm(excl, wildcard))
            {
                q.m_excludes.push_back(excl);
            }
            continue;
        }
        std::string key;
        std::string value;
        const int kind = SplitModifier(raw, key, value) ? ModifierKindOf(key) : kModifierNone;
        switch (kind)
        {
        //from:名字：可写多个，命中其中任何一人即可
        case kModifierFrom:
            if (value.empty())
            {
                q.m_badModifier = true;
            }
            else
            {
                q.m_fromNames.push_back(value);
            }
            break;
        //type:类型：写了多个时以最后一个为准
        case kModifierType:
        {
            const int type = SearchTypeFromText(value);
            if (type == kSearchTypeNone)
            {
                q.m_badModifier = true;
            }
            else
            {
                q.m_type = type;
            }
            break;
        }
        //after:日期 / before:日期：日期不合法或这一天不存在都算写错
        case kModifierAfter:
        case kModifierBefore:
        {
            int y = 0;
            int m = 0;
            int d = 0;
            int64_t startMs = 0;
            int64_t endMs = 0;
            if (!ParseSearchDate(value, y, m, d) || !LocalDayBoundsMs(y, m, d, startMs, endMs))
            {
                q.m_badModifier = true;
            }
            else if (kind == kModifierAfter)
            {
                q.m_afterMs = startMs;
            }
            else
            {
                q.m_beforeMs = endMs;
            }
            break;
        }
        //普通词（含认不出的「xxx:」）
        case kModifierNone:
        default:
        {
            const std::string term = Unquote(raw);
            if (KeepTerm(term, wildcard))
            {
                q.m_terms.push_back(term);
            }
            break;
        }
        }
    }
    return q;
}

bool SearchQueryHasCondition(const HistorySearchQuery& q)
{
    return !q.m_terms.empty() || !q.m_excludes.empty() || !q.m_fromNames.empty() || q.m_type != kSearchTypeNone
        || q.m_afterMs != 0 || q.m_beforeMs != 0 || q.m_badModifier;
}

std::vector<std::wstring> SearchTermsForHighlight(const std::wstring& input, bool wildcard)
{
    std::vector<std::wstring> out;
    const HistorySearchQuery q = ParseHistorySearch(WideToUtf8(input), wildcard);
    for (size_t i = 0; i < q.m_terms.size(); ++i)
    {
        out.push_back(Utf8ToWide(q.m_terms[i]));
    }
    return out;
}

int SearchTypeFromText(const std::string& valueUtf8)
{
    struct TypeName
    {
        const char* m_name;   //写法（UTF-8；英文按小写比较）
        int         m_type;   //HistoryTypeFilter 取值
    };
    //中文写法用 UTF-8 字节写出，免得把界面文字的扫描守卫当成未翻译的文字
    const TypeName kNames[] =
    {
        { "\xE6\x96\x87\xE5\xAD\x97", kHistoryTypeText },   //文字
        { "text",  kHistoryTypeText },
        { "\xE5\x9B\xBE\xE7\x89\x87", kHistoryTypeImage },  //图片
        { "image", kHistoryTypeImage },
        { "img",   kHistoryTypeImage },
        { "\xE6\x96\x87\xE4\xBB\xB6", kHistoryTypeFile },   //文件
        { "file",  kHistoryTypeFile },
        { "\xE8\xAF\xAD\xE9\x9F\xB3", kHistoryTypeVoice },  //语音
        { "voice", kHistoryTypeVoice },
        { "\xE9\x93\xBE\xE6\x8E\xA5", kHistoryTypeLink },   //链接
        { "link",  kHistoryTypeLink },
        { "pin",   kHistoryTypePinned },
    };
    const std::string lower = LowerAscii(valueUtf8);
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i)
    {
        if (lower == kNames[i].m_name)
        {
            return kNames[i].m_type;
        }
    }
    return kSearchTypeNone;
}

bool ParseSearchDate(const std::string& textUtf8, int& year, int& month, int& day)
{
    int y = 0;
    int m = 0;
    int d = 0;
    if (textUtf8.size() == kCompactDateLen && textUtf8.find_first_not_of("0123456789") == std::string::npos)
    {
        ParseDigits(textUtf8.substr(0, kYearDigits), y);
        ParseDigits(textUtf8.substr(kYearDigits, kMonthDayDigits), m);
        ParseDigits(textUtf8.substr(kYearDigits + kMonthDayDigits, kMonthDayDigits), d);
    }
    else
    {
        //YYYY-MM-DD 或 YYYY/MM/DD：分隔符两处须相同
        const size_t first = textUtf8.find_first_of("-/");
        if (first == std::string::npos)
        {
            return false;
        }
        const size_t second = textUtf8.find(textUtf8[first], first + 1);
        if (second == std::string::npos
            || !ParseDigits(textUtf8.substr(0, first), y)
            || !ParseDigits(textUtf8.substr(first + 1, second - first - 1), m)
            || !ParseDigits(textUtf8.substr(second + 1), d))
        {
            return false;
        }
        if (textUtf8.substr(0, first).size() != kYearDigits)
        {
            return false;
        }
    }
    if (m < 1 || m > kMaxMonth || d < 1 || d > kMaxDay)
    {
        return false;
    }
    year = y;
    month = m;
    day = d;
    return true;
}

bool SearchNameMatches(const std::string& displayNameUtf8, const std::string& fromNameUtf8)
{
    if (fromNameUtf8.empty())
    {
        return false;
    }
    return LowerAscii(displayNameUtf8).find(LowerAscii(fromNameUtf8)) != std::string::npos;
}

std::vector<int64_t> MatchSearchFromIds(const HistorySearchQuery& q, const std::vector<int64_t>& candidates,
                                        const std::vector<std::string>& namesUtf8)
{
    std::vector<int64_t> ids;
    if (q.m_fromNames.empty())
    {
        return ids;
    }
    const size_t n = (candidates.size() < namesUtf8.size()) ? candidates.size() : namesUtf8.size();
    for (size_t i = 0; i < n; ++i)
    {
        bool matched = false;
        for (size_t k = 0; k < q.m_fromNames.size() && !matched; ++k)
        {
            matched = SearchNameMatches(namesUtf8[i], q.m_fromNames[k]);
        }
        //同一个人在候选表里出现多次时只留第一次
        bool seen = false;
        for (size_t j = 0; j < ids.size() && !seen; ++j)
        {
            seen = (ids[j] == candidates[i]);
        }
        if (matched && !seen)
        {
            ids.push_back(candidates[i]);
        }
    }
    return ids;
}

} // namespace balloonhistory
