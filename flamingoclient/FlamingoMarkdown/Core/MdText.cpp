/**
 *  FlamingoMarkdown.dll 文字工具的实现，用法见 MdText.h。
 *
 *  UTF 转换自己实现而不调用 MultiByteToWideChar，是为了让本文件不依赖 Windows 头文件，并对非法序列给出确定的
 *  结果（替换为 U+FFFD），单元测试可以逐字节核对。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdText.h"

#include <stdint.h>
#include <string.h>

namespace flamingomd {

namespace {

//替换非法序列用的字符 U+FFFD
const uint32_t kReplacementChar = 0xFFFD;

//Unicode 码点的上限
const uint32_t kMaxCodePoint = 0x10FFFF;

//基本多文种平面（BMP）的上限，超过它要用代理对表示
const uint32_t kMaxBmpCodePoint = 0xFFFF;

//代理项区间，单独出现时不是合法字符
const uint32_t kSurrogateFirst = 0xD800;
const uint32_t kSurrogateLast = 0xDFFF;

//代理对换算用的常量：码点减去 0x10000 后，高 10 位进高位代理、低 10 位进低位代理
const uint32_t kSupplementaryBase = 0x10000;
const uint32_t kHighSurrogateBase = 0xD800;
const uint32_t kLowSurrogateBase = 0xDC00;
const uint32_t kSurrogateBits = 10;
const uint32_t kSurrogateMask = 0x3FF;

//UTF-8 各长度序列能表示的最小码点，小于它的属于「过长编码」，按非法处理
const uint32_t kMin2ByteCodePoint = 0x80;
const uint32_t kMin3ByteCodePoint = 0x800;
const uint32_t kMin4ByteCodePoint = 0x10000;

//UTF-8 编码的位模式：单字节（ASCII）上限、后续字节 10xxxxxx 的掩码与标记、每个后续字节携带的位数与掩码
const uint32_t kAsciiLimit = 0x80;
const unsigned char kContinuationMask = 0xC0;
const unsigned char kContinuationTag = 0x80;
const uint32_t kContinuationBits = 6;
const uint32_t kContinuationPayload = 0x3F;

//UTF-8 首字节：2 字节序列 110xxxxx、3 字节序列 1110xxxx、4 字节序列 11110xxx 的掩码、标记与携带位的掩码
const unsigned char kLead2Mask = 0xE0;
const unsigned char kLead2Tag = 0xC0;
const uint32_t kLead2Payload = 0x1F;
const unsigned char kLead3Mask = 0xF0;
const unsigned char kLead3Tag = 0xE0;
const uint32_t kLead3Payload = 0x0F;
const unsigned char kLead4Mask = 0xF8;
const unsigned char kLead4Tag = 0xF0;
const uint32_t kLead4Payload = 0x07;

//UTF-8 各长度序列的字节数
const size_t kSeq2Bytes = 2;
const size_t kSeq3Bytes = 3;
const size_t kSeq4Bytes = 4;

//数字实体最多允许的数字位数，防止数值溢出
const size_t kMaxEntityDigits = 8;

//十进制与十六进制的基数
const uint32_t kDecimalBase = 10;
const uint32_t kHexBase = 16;
const uint32_t kHexLetterOffset = 10;

//一个命名实体及其对应的字符
struct NamedEntity
{
    const wchar_t* m_name;   //名称，不含 & 与 ;
    wchar_t        m_char;   //对应的字符
};

//聊天里可能出现的常用命名实体；不在表里的按原样显示
const NamedEntity kNamedEntities[] =
{
    { L"amp",    L'&' },
    { L"lt",     L'<' },
    { L"gt",     L'>' },
    { L"quot",   L'"' },
    { L"apos",   L'\'' },
    { L"nbsp",   0x00A0 },
    { L"copy",   0x00A9 },
    { L"reg",    0x00AE },
    { L"trade",  0x2122 },
    { L"hellip", 0x2026 },
    { L"mdash",  0x2014 },
    { L"ndash",  0x2013 },
    { L"laquo",  0x00AB },
    { L"raquo",  0x00BB },
    { L"middot", 0x00B7 },
    { L"times",  0x00D7 },
    { L"divide", 0x00F7 },
    { L"deg",    0x00B0 },
    { L"plusmn", 0x00B1 },
    { L"yen",    0x00A5 },
    { L"euro",   0x20AC },
    { L"larr",   0x2190 },
    { L"rarr",   0x2192 },
    { L"uarr",   0x2191 },
    { L"darr",   0x2193 },
};

//把一个码点追加为 UTF-16（必要时拆成代理对）；非法码点追加 U+FFFD
void AppendCodePoint(std::wstring& out, uint32_t cp)
{
    if (cp > kMaxCodePoint || (cp >= kSurrogateFirst && cp <= kSurrogateLast))
        cp = kReplacementChar;
    if (cp <= kMaxBmpCodePoint)
    {
        out.push_back(static_cast<wchar_t>(cp));
        return;
    }
    const uint32_t v = cp - kSupplementaryBase;
    out.push_back(static_cast<wchar_t>(kHighSurrogateBase + (v >> kSurrogateBits)));
    out.push_back(static_cast<wchar_t>(kLowSurrogateBase + (v & kSurrogateMask)));
}

//判断字节是否为 UTF-8 的后续字节 10xxxxxx
bool IsContinuation(unsigned char b)
{
    return (b & kContinuationMask) == kContinuationTag;
}

//取码点右移 shift 位后的低 6 位，组成一个后续字节
char ContinuationByte(uint32_t cp, uint32_t shift)
{
    return static_cast<char>(kContinuationTag | ((cp >> shift) & kContinuationPayload));
}

//把一个码点按 UTF-8 追加到 out
void AppendUtf8(std::string& out, uint32_t cp)
{
    const uint32_t shift1 = kContinuationBits;
    const uint32_t shift2 = kContinuationBits * 2;
    const uint32_t shift3 = kContinuationBits * 3;
    if (cp < kAsciiLimit)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < kMin3ByteCodePoint)
    {
        out.push_back(static_cast<char>(kLead2Tag | (cp >> shift1)));
        out.push_back(ContinuationByte(cp, 0));
    }
    else if (cp < kMin4ByteCodePoint)
    {
        out.push_back(static_cast<char>(kLead3Tag | (cp >> shift2)));
        out.push_back(ContinuationByte(cp, shift1));
        out.push_back(ContinuationByte(cp, 0));
    }
    else
    {
        out.push_back(static_cast<char>(kLead4Tag | (cp >> shift3)));
        out.push_back(ContinuationByte(cp, shift2));
        out.push_back(ContinuationByte(cp, shift1));
        out.push_back(ContinuationByte(cp, 0));
    }
}

//解析一个十进制或十六进制数字串；位数过多或含非法字符时返回 false
bool ParseNumber(const std::wstring& digits, uint32_t base, uint32_t& value)
{
    if (digits.empty() || digits.size() > kMaxEntityDigits)
        return false;
    value = 0;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        const wchar_t ch = digits[i];
        uint32_t d = 0;
        if (ch >= L'0' && ch <= L'9')
            d = static_cast<uint32_t>(ch - L'0');
        else if (base == kHexBase && ch >= L'a' && ch <= L'f')
            d = static_cast<uint32_t>(ch - L'a') + kHexLetterOffset;
        else if (base == kHexBase && ch >= L'A' && ch <= L'F')
            d = static_cast<uint32_t>(ch - L'A') + kHexLetterOffset;
        else
            return false;
        value = value * base + d;
    }
    return true;
}

} // namespace

std::wstring Utf8ToWide(const char* utf8, size_t bytes)
{
    std::wstring out;
    if (utf8 == NULL || bytes == 0)
        return out;
    out.reserve(bytes);

    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8);
    size_t i = 0;
    while (i < bytes)
    {
        const unsigned char b0 = p[i];
        if (b0 < kAsciiLimit)
        {
            out.push_back(static_cast<wchar_t>(b0));
            ++i;
            continue;
        }

        //按首字节确定序列长度与首字节携带的位
        size_t len = 0;
        uint32_t cp = 0;
        uint32_t minCp = 0;
        if ((b0 & kLead2Mask) == kLead2Tag)
        {
            len = kSeq2Bytes;
            cp = b0 & kLead2Payload;
            minCp = kMin2ByteCodePoint;
        }
        else if ((b0 & kLead3Mask) == kLead3Tag)
        {
            len = kSeq3Bytes;
            cp = b0 & kLead3Payload;
            minCp = kMin3ByteCodePoint;
        }
        else if ((b0 & kLead4Mask) == kLead4Tag)
        {
            len = kSeq4Bytes;
            cp = b0 & kLead4Payload;
            minCp = kMin4ByteCodePoint;
        }
        else
        {
            //孤立的后续字节或非法首字节：替换后只跳过这一个字节
            out.push_back(static_cast<wchar_t>(kReplacementChar));
            ++i;
            continue;
        }

        bool valid = (i + len <= bytes);
        for (size_t k = 1; valid && k < len; ++k)
        {
            if (!IsContinuation(p[i + k]))
                valid = false;
            else
                cp = (cp << kContinuationBits) | (p[i + k] & kContinuationPayload);
        }
        if (!valid || cp < minCp || cp > kMaxCodePoint || (cp >= kSurrogateFirst && cp <= kSurrogateLast))
        {
            out.push_back(static_cast<wchar_t>(kReplacementChar));
            ++i;
            continue;
        }
        AppendCodePoint(out, cp);
        i += len;
    }
    return out;
}

std::string WideToUtf8(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size() * 3);
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (IsHighSurrogate(ch))
        {
            if (i + 1 < text.size() && IsLowSurrogate(text[i + 1]))
            {
                const uint32_t hi = static_cast<uint32_t>(ch) - kHighSurrogateBase;
                const uint32_t lo = static_cast<uint32_t>(text[i + 1]) - kLowSurrogateBase;
                AppendUtf8(out, kSupplementaryBase + (hi << kSurrogateBits) + lo);
                ++i;
            }
            else
            {
                AppendUtf8(out, kReplacementChar);
            }
            continue;
        }
        if (IsLowSurrogate(ch))
        {
            AppendUtf8(out, kReplacementChar);
            continue;
        }
        AppendUtf8(out, static_cast<uint32_t>(ch));
    }
    return out;
}

bool DecodeEntity(const std::wstring& entity, std::wstring& out)
{
    //最短的合法实体形如 &lt;（4 个字符），且必须以 & 开头、以 ; 结尾
    const size_t kMinEntityChars = 4;
    if (entity.size() < kMinEntityChars || entity[0] != L'&' || entity[entity.size() - 1] != L';')
        return false;
    const std::wstring body = entity.substr(1, entity.size() - 2);

    if (body[0] == L'#')
    {
        uint32_t value = 0;
        bool ok = false;
        if (body.size() > 1 && (body[1] == L'x' || body[1] == L'X'))
            ok = ParseNumber(body.substr(2), kHexBase, value);
        else
            ok = ParseNumber(body.substr(1), kDecimalBase, value);
        if (!ok)
            return false;
        //CommonMark 规定 0 号字符与非法码点替换为 U+FFFD
        if (value == 0)
            value = kReplacementChar;
        std::wstring decoded;
        AppendCodePoint(decoded, value);
        out = decoded;
        return true;
    }

    for (size_t i = 0; i < sizeof(kNamedEntities) / sizeof(kNamedEntities[0]); ++i)
    {
        if (body == kNamedEntities[i].m_name)
        {
            out = std::wstring(1, kNamedEntities[i].m_char);
            return true;
        }
    }
    return false;
}

std::wstring ExpandTabs(const std::wstring& text, int tabWidth)
{
    if (tabWidth <= 0 || text.find(L'\t') == std::wstring::npos)
        return text;
    std::wstring out;
    out.reserve(text.size() + text.size() / 4);
    int column = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (ch == L'\t')
        {
            const int spaces = tabWidth - (column % tabWidth);
            out.append(static_cast<size_t>(spaces), L' ');
            column += spaces;
        }
        else if (ch == L'\n')
        {
            out.push_back(ch);
            column = 0;
        }
        else
        {
            out.push_back(ch);
            ++column;
        }
    }
    return out;
}

} // namespace flamingomd
