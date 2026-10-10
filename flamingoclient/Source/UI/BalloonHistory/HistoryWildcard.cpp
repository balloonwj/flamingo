/**
 *  聊天记录管理器通配符搜索的纯逻辑实现，说明见 HistoryWildcard.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "HistoryWildcard.h"

#include <stdint.h>
#include <algorithm>
#include "V2MsgQuery.h"   // MakeLikePattern / MakeFtsPhrase / CanUseTrigramSearch
#include "HistorySearchSyntax.h"   // SearchTermsForHighlight：按搜索语法取出正向词

namespace balloonhistory {

namespace {

//LIKE 里匹配任意个字符的字符
const char kLikeAny = '%';
//LIKE 里匹配一个字符的字符
const char kLikeOne = '_';
//配合 ESCAPE '\' 使用的转义符
const char kLikeEscape = '\\';
//全文索引查询里连接各段的运算符（两侧带空格）
const char* const kFtsAnd = " AND ";

//高亮区间按起点排序，起点相同的长的在前
bool HighlightBeginLess(const HighlightRange& a, const HighlightRange& b)
{
    if (a.m_begin != b.m_begin)
    {
        return a.m_begin < b.m_begin;
    }
    return a.m_len > b.m_len;
}

//UTF-16 高位代理的取值范围（含两端）
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast  = 0xDBFF;
//UTF-16 低位代理的取值范围（含两端）
const wchar_t kLowSurrogateFirst  = 0xDC00;
const wchar_t kLowSurrogateLast   = 0xDFFF;
//由一对代理还原码点时，高位代理去掉 0xD800 后左移的位数，以及补上的基数 0x10000
const int      kSurrogateShift = 10;
const uint32_t kSurrogateBase  = 0x10000;

//通配符模式里的一个位置：要么是「恰好一个字符」，要么是一个具体字符
struct PatternElem
{
    bool     m_anyOne;     //true 表示 '?'：匹配任意一个字符
    uint32_t m_codePoint;  //m_anyOne 为 false 时要匹配的字符（Unicode 码点，已转成小写）

    PatternElem()
        : m_anyOne(false)
        , m_codePoint(0)
    {
    }
};

//两个 '*' 之间的一段模式，长度固定（每个位置恰好对应一个字符）
typedef std::vector<PatternElem> PatternToken;

//把一个码点转成小写，只对 ASCII 字母生效，与 SQLite LIKE 的大小写规则一致
uint32_t LowerAsciiCodePoint(uint32_t cp)
{
    if (cp >= (uint32_t)L'A' && cp <= (uint32_t)L'Z')
    {
        return cp - (uint32_t)L'A' + (uint32_t)L'a';
    }
    return cp;
}

//把 UTF-16 文字拆成码点，并记下每个码点在原文里的起始下标。
//outOffsets 比 outCodePoints 多一项，最后一项是原文长度，便于把码点区间换回 UTF-16 区间。
//孤立的代理（前后配不成对）按一个字符处理，不丢弃。
void DecodeUtf16(const std::wstring& text, std::vector<uint32_t>& outCodePoints, std::vector<int>& outOffsets)
{
    outCodePoints.clear();
    outOffsets.clear();
    outCodePoints.reserve(text.size());
    outOffsets.reserve(text.size() + 1);
    size_t i = 0;
    while (i < text.size())
    {
        const wchar_t ch = text[i];
        outOffsets.push_back((int)i);
        const bool isHigh = (ch >= kHighSurrogateFirst && ch <= kHighSurrogateLast);
        if (isHigh && i + 1 < text.size())
        {
            const wchar_t next = text[i + 1];
            if (next >= kLowSurrogateFirst && next <= kLowSurrogateLast)
            {
                const uint32_t cp = kSurrogateBase
                                  + ((uint32_t)(ch - kHighSurrogateFirst) << kSurrogateShift)
                                  + (uint32_t)(next - kLowSurrogateFirst);
                outCodePoints.push_back(cp);
                i += 2;
                continue;
            }
        }
        outCodePoints.push_back((uint32_t)ch);
        ++i;
    }
    outOffsets.push_back((int)text.size());
}

//把通配符模式按 '*' 切成若干段，丢掉空段（首尾的 '*'、连续的 '*' 都会产生空段）
std::vector<PatternToken> SplitPatternTokens(const std::wstring& pattern)
{
    std::vector<uint32_t> cps;
    std::vector<int> offsets;
    DecodeUtf16(pattern, cps, offsets);

    std::vector<PatternToken> tokens;
    PatternToken current;
    for (size_t i = 0; i < cps.size(); ++i)
    {
        if (cps[i] == (uint32_t)kWildcardAnyChar)
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        PatternElem elem;
        if (cps[i] == (uint32_t)kWildcardOneChar)
        {
            elem.m_anyOne = true;
        }
        else
        {
            elem.m_codePoint = LowerAsciiCodePoint(cps[i]);
        }
        current.push_back(elem);
    }
    if (!current.empty())
    {
        tokens.push_back(current);
    }
    return tokens;
}

//一段模式能否恰好从文字的第 start 个码点开始匹配上（文字已转成小写）
bool TokenMatchesAt(const PatternToken& token, const std::vector<uint32_t>& lowerText, size_t start)
{
    if (start + token.size() > lowerText.size())
    {
        return false;
    }
    for (size_t k = 0; k < token.size(); ++k)
    {
        if (!token[k].m_anyOne && token[k].m_codePoint != lowerText[start + k])
        {
            return false;
        }
    }
    return true;
}

//从文字的第 from 个码点起，找一段模式最早能匹配上的起点。
//@return 起点（码点下标）；找不到时返回 lowerText.size() + 1
size_t FindTokenFrom(const PatternToken& token, const std::vector<uint32_t>& lowerText, size_t from)
{
    const size_t notFound = lowerText.size() + 1;
    if (token.size() > lowerText.size())
    {
        return notFound;
    }
    for (size_t s = from; s + token.size() <= lowerText.size(); ++s)
    {
        if (TokenMatchesAt(token, lowerText, s))
        {
            return s;
        }
    }
    return notFound;
}

} // namespace

bool WildcardHasContent(const std::string& patternUtf8)
{
    for (size_t i = 0; i < patternUtf8.size(); ++i)
    {
        if (patternUtf8[i] != kWildcardAnyChar)
        {
            return true;
        }
    }
    return false;
}

std::string MakeWildcardLikePattern(const std::string& patternUtf8)
{
    std::string like;
    like.reserve(patternUtf8.size() * 2 + 2);
    like.push_back(kLikeAny);
    //结果末尾是不是一个起通配作用的 '%'（转义过的 "\%" 不算）。为 true 时再遇到 '*' 不必重复追加
    bool endsWithAny = true;
    for (size_t i = 0; i < patternUtf8.size(); ++i)
    {
        const char ch = patternUtf8[i];
        if (ch == kWildcardAnyChar)
        {
            if (!endsWithAny)
            {
                like.push_back(kLikeAny);
                endsWithAny = true;
            }
            continue;
        }
        if (ch == kWildcardOneChar)
        {
            like.push_back(kLikeOne);
        }
        else
        {
            //LIKE 自己的三个特殊字符在输入里按普通字符匹配
            if (ch == kLikeEscape || ch == kLikeAny || ch == kLikeOne)
            {
                like.push_back(kLikeEscape);
            }
            like.push_back(ch);
        }
        endsWithAny = false;
    }
    if (!endsWithAny)
    {
        like.push_back(kLikeAny);
    }
    return like;
}

std::string MakeSearchLikePattern(const std::string& keywordUtf8, bool wildcard)
{
    if (wildcard)
    {
        return MakeWildcardLikePattern(keywordUtf8);
    }
    return MakeLikePattern(keywordUtf8);
}

bool BuildWildcardFtsQuery(const std::string& patternUtf8, std::string& outQuery)
{
    outQuery.clear();
    std::string segment;
    //多出一轮 i == size 用来处理最后一段，循环体里把它当成遇到了分隔符
    for (size_t i = 0; i <= patternUtf8.size(); ++i)
    {
        const bool atEnd = (i == patternUtf8.size());
        const bool isSeparator = atEnd
                              || patternUtf8[i] == kWildcardAnyChar
                              || patternUtf8[i] == kWildcardOneChar;
        if (!isSeparator)
        {
            segment.push_back(patternUtf8[i]);
            continue;
        }
        //一段结束：够 3 个字才放进查询串，太短的段 trigram 索引查不了
        if (CanUseTrigramSearch(segment))
        {
            if (!outQuery.empty())
            {
                outQuery += kFtsAnd;
            }
            outQuery += MakeFtsPhrase(segment);
        }
        segment.clear();
    }
    return !outQuery.empty();
}

std::vector<HighlightRange> FindWildcardHighlightRanges(const std::wstring& text, const std::wstring& pattern)
{
    std::vector<HighlightRange> ranges;
    const std::vector<PatternToken> tokens = SplitPatternTokens(pattern);
    if (tokens.empty() || text.empty())
    {
        return ranges;
    }

    std::vector<uint32_t> lowerText;
    std::vector<int> offsets;
    DecodeUtf16(text, lowerText, offsets);
    for (size_t i = 0; i < lowerText.size(); ++i)
    {
        lowerText[i] = LowerAsciiCodePoint(lowerText[i]);
    }

    const size_t notFound = lowerText.size() + 1;
    size_t from = 0;
    while (from < lowerText.size())
    {
        //第一段决定这一处命中的起点：取最左边能匹配上的位置
        const size_t start = FindTokenFrom(tokens[0], lowerText, from);
        if (start == notFound)
        {
            break;
        }
        //其余各段依次取最早能匹配上的位置，这样从这个起点得到的命中最短。
        //某一段在后面找不到时，从更靠后的起点出发也一定找不到（要找的范围只会更小），整体结束
        size_t cursor = start + tokens[0].size();
        bool complete = true;
        for (size_t k = 1; k < tokens.size(); ++k)
        {
            const size_t at = FindTokenFrom(tokens[k], lowerText, cursor);
            if (at == notFound)
            {
                complete = false;
                break;
            }
            cursor = at + tokens[k].size();
        }
        if (!complete)
        {
            break;
        }

        HighlightRange r;
        r.m_begin = offsets[start];
        r.m_len = offsets[cursor] - offsets[start];
        ranges.push_back(r);
        //命中不重叠：从这一处末尾继续找
        from = cursor;
    }
    return ranges;
}

std::vector<HighlightRange> FindSearchHighlightRanges(const std::wstring& text, const std::wstring& keyword,
                                                      bool wildcard)
{
    //按搜索语法（HistorySearchSyntax.h）取出各正向词，分别找命中处；排除词与限定词不高亮
    const std::vector<std::wstring> terms = SearchTermsForHighlight(keyword, wildcard);
    std::vector<HighlightRange> all;
    for (size_t t = 0; t < terms.size(); ++t)
    {
        const std::vector<HighlightRange> one = wildcard ? FindWildcardHighlightRanges(text, terms[t])
                                                         : FindHighlightRanges(text, terms[t]);
        all.insert(all.end(), one.begin(), one.end());
    }
    return MergeHighlightRanges(all);
}

std::vector<HighlightRange> MergeHighlightRanges(const std::vector<HighlightRange>& ranges)
{
    std::vector<HighlightRange> sorted = ranges;
    std::sort(sorted.begin(), sorted.end(), HighlightBeginLess);
    std::vector<HighlightRange> merged;
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        if (sorted[i].m_len <= 0)
        {
            continue;
        }
        //与上一段重叠或首尾相接：并成一段
        if (!merged.empty() && sorted[i].m_begin <= merged.back().m_begin + merged.back().m_len)
        {
            const int end = sorted[i].m_begin + sorted[i].m_len;
            const int lastEnd = merged.back().m_begin + merged.back().m_len;
            if (end > lastEnd)
            {
                merged.back().m_len = end - merged.back().m_begin;
            }
            continue;
        }
        merged.push_back(sorted[i]);
    }
    return merged;
}

} // namespace balloonhistory
