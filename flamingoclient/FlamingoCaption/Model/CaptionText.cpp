/**
 *  语音字幕的文字工具，实现见 CaptionText.h。
 *  中文标点一律写成 \u 转义，生产代码里不出现中文字面量（多语言守卫 I18G1）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionText.h"

#include <windows.h>

namespace caption {

namespace {

//UTF-16 高位代理的范围：与随后的低位代理组成一个扩展区字符
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast  = 0xDBFF;
//UTF-16 低位代理的范围
const wchar_t kLowSurrogateFirst  = 0xDC00;
const wchar_t kLowSurrogateLast   = 0xDFFF;

//中日韩全角标点：句号、感叹号、问号、逗号、分号、冒号、顿号
const wchar_t kFullStop      = 0x3002;
const wchar_t kFullExclaim   = 0xFF01;
const wchar_t kFullQuestion  = 0xFF1F;
const wchar_t kFullComma     = 0xFF0C;
const wchar_t kFullSemicolon = 0xFF1B;
const wchar_t kFullColon     = 0xFF1A;
const wchar_t kIdeoComma     = 0x3001;
//省略号（…），在中文排版里按全角处理
const wchar_t kEllipsis      = 0x2026;
//破折号（—）与中文引号（‘’“”），在中文排版里按全角处理
const wchar_t kEmDash        = 0x2014;
const wchar_t kQuoteFirst    = 0x2018;
const wchar_t kQuoteLast     = 0x201F;
//全角空格与不换行空格，整理时按普通空格对待
const wchar_t kIdeoSpace     = 0x3000;
const wchar_t kNoBreakSpace  = 0x00A0;

bool InRange(wchar_t ch, wchar_t first, wchar_t last)
{
    return ch >= first && ch <= last;
}

bool IsAsciiAlnum(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

wchar_t FoldAscii(wchar_t ch)
{
    if (ch >= L'A' && ch <= L'Z')
        return static_cast<wchar_t>(ch - L'A' + L'a');
    return ch;
}

bool IsSpaceChar(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == kIdeoSpace || ch == kNoBreakSpace;
}

//全角标点（含中日韩符号区、全角形式里的标点、中文引号与省略号）：其前后的空格在整理时去掉
bool IsWidePunct(wchar_t ch)
{
    return InRange(ch, 0x3001, 0x303F) || InRange(ch, 0xFF01, 0xFF0F) || InRange(ch, 0xFF1A, 0xFF20)
        || InRange(ch, 0xFF3B, 0xFF40) || InRange(ch, 0xFF5B, 0xFF65) || ch == kEmDash
        || InRange(ch, kQuoteFirst, kQuoteLast) || ch == kEllipsis;
}

bool MatchAt(const std::wstring& text, size_t pos, const std::wstring& needle, bool matchCase)
{
    if (pos + needle.size() > text.size())
        return false;
    for (size_t i = 0; i < needle.size(); ++i)
    {
        wchar_t a = text[pos + i];
        wchar_t b = needle[i];
        if (!matchCase)
        {
            a = FoldAscii(a);
            b = FoldAscii(b);
        }
        if (a != b)
            return false;
    }
    return true;
}

bool WordBoundaryOk(const std::wstring& text, size_t pos, const std::wstring& needle)
{
    if (IsAsciiAlnum(needle[0]) && pos > 0 && IsAsciiAlnum(text[pos - 1]))
        return false;
    const size_t end = pos + needle.size();
    if (IsAsciiAlnum(needle[needle.size() - 1]) && end < text.size() && IsAsciiAlnum(text[end]))
        return false;
    return true;
}

} // namespace

bool IsWideChar(wchar_t ch)
{
    return InRange(ch, 0x1100, 0x115F) || InRange(ch, 0x2E80, 0x303E) || InRange(ch, 0x3041, 0x33FF)
        || InRange(ch, 0x3400, 0x4DBF) || InRange(ch, 0x4E00, 0x9FFF) || InRange(ch, 0xA000, 0xA4CF)
        || InRange(ch, 0xAC00, 0xD7A3) || InRange(ch, 0xF900, 0xFAFF) || InRange(ch, 0xFE30, 0xFE4F)
        || InRange(ch, 0xFF00, 0xFF60) || InRange(ch, 0xFFE0, 0xFFE6) || ch == kEmDash
        || InRange(ch, kQuoteFirst, kQuoteLast) || ch == kEllipsis
        || InRange(ch, kHighSurrogateFirst, kHighSurrogateLast);
}

int DisplayUnits(const std::wstring& text)
{
    int units = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        //换行不占宽度；低位代理与前面的高位代理是同一个字符，已按宽字符计过
        if (ch == L'\n' || ch == L'\r' || InRange(ch, kLowSurrogateFirst, kLowSurrogateLast))
            continue;
        units += IsWideChar(ch) ? kWideCharUnits : kNarrowCharUnits;
    }
    return units;
}

bool IsSentenceEndPunct(wchar_t ch)
{
    return ch == kFullStop || ch == kFullExclaim || ch == kFullQuestion || ch == kEllipsis
        || ch == L'.' || ch == L'!' || ch == L'?';
}

bool IsBreakPunct(wchar_t ch)
{
    return IsSentenceEndPunct(ch) || ch == kFullComma || ch == kFullSemicolon || ch == kFullColon
        || ch == kIdeoComma || ch == L',' || ch == L';' || ch == L':';
}

size_t LeadingPunctLength(const std::wstring& text)
{
    size_t n = 0;
    while (n < text.size() && (IsBreakPunct(text[n]) || IsSpaceChar(text[n])))
        ++n;
    //开头只有空白、没有标点时不算
    for (size_t i = 0; i < n; ++i)
    {
        if (IsBreakPunct(text[i]))
            return n;
    }
    return 0;
}

std::wstring NormalizeText(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        wchar_t ch = text[i];
        if (ch == L'\r')
            continue;
        if (ch == L'\n')
        {
            //换行：去掉行尾空白，换行本身保留；连续的空行只留一个换行
            pendingSpace = false;
            while (!out.empty() && out[out.size() - 1] == L' ')
                out.erase(out.size() - 1);
            if (!out.empty() && out[out.size() - 1] != L'\n')
                out.push_back(L'\n');
            continue;
        }
        if (IsSpaceChar(ch))
        {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace)
        {
            pendingSpace = false;
            const wchar_t prev = out.empty() ? 0 : out[out.size() - 1];
            const bool atLineStart = out.empty() || prev == L'\n';
            const bool dropForPunct = IsWidePunct(prev) || IsWidePunct(ch);
            const bool dropBetweenWide = IsWideChar(prev) && IsWideChar(ch);
            if (!atLineStart && !dropForPunct && !dropBetweenWide)
                out.push_back(L' ');
        }
        out.push_back(ch);
    }
    while (!out.empty() && (out[out.size() - 1] == L' ' || out[out.size() - 1] == L'\n'))
        out.erase(out.size() - 1);
    return out;
}

std::wstring JoinText(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
        return right;
    if (right.empty())
        return left;
    if (IsAsciiAlnum(left[left.size() - 1]) && IsAsciiAlnum(right[0]))
        return left + L" " + right;
    return left + right;
}

size_t FindInText(const std::wstring& text, const std::wstring& needle, size_t from, bool matchCase, bool wholeWord)
{
    if (needle.empty() || needle.size() > text.size())
        return std::wstring::npos;
    for (size_t pos = from; pos + needle.size() <= text.size(); ++pos)
    {
        if (!MatchAt(text, pos, needle, matchCase))
            continue;
        if (wholeWord && !WordBoundaryOk(text, pos, needle))
            continue;
        return pos;
    }
    return std::wstring::npos;
}

std::wstring ReplaceInText(const std::wstring& text, const std::wstring& needle, const std::wstring& replacement,
                           bool matchCase, bool wholeWord, int* count)
{
    int n = 0;
    std::wstring out;
    size_t pos = 0;
    for (;;)
    {
        const size_t hit = FindInText(text, needle, pos, matchCase, wholeWord);
        if (hit == std::wstring::npos)
            break;
        out.append(text, pos, hit - pos);
        out.append(replacement);
        pos = hit + needle.size();
        ++n;
    }
    out.append(text, pos, std::wstring::npos);
    if (count != NULL)
        *count = n;
    return out;
}

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), NULL, 0, NULL, NULL);
    if (n <= 0)
        return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &out[0], n, NULL, NULL);
    return out;
}

} // namespace caption
