/**
 *  机器人管理界面的字符串小工具，见 BotStrings.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotStrings.h"

#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "BotTypes.h"

namespace flamingobot {

namespace {

//一小时的分钟数
const int kMinutesPerHour = 60;
//一天的小时数
const int kHoursPerDay = 24;
//「HH:MM」里分钟固定两位
const size_t kMinuteDigits = 2;
//「HH:MM」里小时最多两位
const size_t kMaxHourDigits = 2;
//十进制的基数
const int kDecimalBase = 10;
//十六进制的基数
const int kHexBase = 16;
//格式化「HH:MM」的缓冲区长度（含结尾 0）
const size_t kHhMmBufferChars = 8;
//格式化 "#RRGGBB" 的缓冲区长度（含结尾 0）
const size_t kColorBufferChars = 8;
//颜色一个分量占的位数，也是 COLORREF 里绿色分量的移位
const int kColorChannelBits = 8;
//COLORREF 里蓝色分量的移位（两个分量的位数），也是 RRGGBB 里红色分量的移位
const int kColorHighShift = 16;
//颜色一个分量的掩码
const uint32_t kColorChannelMask = 0xFF;

//UTF-8 里后续字节的特征：10xxxxxx
const unsigned char kUtf8ContinuationMask = 0xC0;
const unsigned char kUtf8ContinuationBits = 0x80;

//UTF-16 高代理项的范围
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast = 0xDBFF;

//全角空格、全角逗号、全角分号、顿号（写成码点，避免源文件里出现全角字符）
const wchar_t kIdeographicSpace = 0x3000;
const wchar_t kFullwidthComma = 0xFF0C;
const wchar_t kFullwidthSemicolon = 0xFF1B;
const wchar_t kIdeographicComma = 0x3001;

//是否为空白字符（半角空格、全角空格、制表符、换行）
bool IsSpaceChar(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == kIdeographicSpace;
}

//是否为关键词的分隔符
bool IsKeywordSeparator(wchar_t ch)
{
    return ch == L'\r' || ch == L'\n' || ch == L',' || ch == L';' || ch == kFullwidthComma
           || ch == kFullwidthSemicolon || ch == kIdeographicComma;
}

//是否全是十进制数字（空串返回 false）
bool AllDigits(const std::string& text)
{
    if (text.empty())
        return false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    return true;
}

//一个十六进制字符的值；不是十六进制字符时返回 -1
int HexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + kDecimalBase;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + kDecimalBase;
    return -1;
}

} // namespace

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
    if (chars <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], chars);
    return out;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return std::string();
    std::string out(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], bytes, NULL, NULL);
    return out;
}

int Utf8CharCount(const std::string& text)
{
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        //每个码点恰有一个不是后续字节的字节
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if ((ch & kUtf8ContinuationMask) != kUtf8ContinuationBits)
            ++count;
    }
    return count;
}

int WideCharCount(const std::wstring& text)
{
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        //高代理项与后面的低代理项合起来算一个字
        if (text[i] >= kHighSurrogateFirst && text[i] <= kHighSurrogateLast && i + 1 < text.size())
            ++i;
        ++count;
    }
    return count;
}

std::wstring TrimWide(const std::wstring& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && IsSpaceChar(text[begin]))
        ++begin;
    while (end > begin && IsSpaceChar(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

std::string TrimUtf8(const std::string& text)
{
    return WideToUtf8(TrimWide(Utf8ToWide(text)));
}

bool ParseHhMm(const std::string& text, int& minute)
{
    minute = 0;
    const size_t colon = text.find(':');
    if (colon == std::string::npos || colon == 0)
        return false;
    const std::string hours = text.substr(0, colon);
    const std::string minutes = text.substr(colon + 1);
    if (hours.size() > kMaxHourDigits || minutes.size() != kMinuteDigits || !AllDigits(hours) || !AllDigits(minutes))
        return false;
    const int h = atoi(hours.c_str());
    const int m = atoi(minutes.c_str());
    if (h >= kHoursPerDay || m >= kMinutesPerHour)
        return false;
    minute = h * kMinutesPerHour + m;
    return true;
}

std::string FormatHhMm(int minute)
{
    if (minute < 0)
        minute = 0;
    minute %= kMinutesPerDay;
    char buf[kHhMmBufferChars] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02d:%02d", minute / kMinutesPerHour, minute % kMinutesPerHour);
    return buf;
}

std::vector<std::string> SplitKeywords(const std::wstring& text)
{
    std::vector<std::string> out;
    std::wstring current;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        const bool atEnd = (i == text.size());
        if (!atEnd && !IsKeywordSeparator(text[i]))
        {
            current += text[i];
            continue;
        }
        const std::string item = WideToUtf8(TrimWide(current));
        current.clear();
        if (item.empty())
            continue;
        bool duplicated = false;
        for (size_t k = 0; k < out.size(); ++k)
        {
            if (out[k] == item)
                duplicated = true;
        }
        if (!duplicated)
            out.push_back(item);
    }
    return out;
}

std::wstring JoinKeywords(const std::vector<std::string>& keywords)
{
    std::wstring out;
    for (size_t i = 0; i < keywords.size(); ++i)
    {
        if (i > 0)
            out += L"\r\n";
        out += Utf8ToWide(keywords[i]);
    }
    return out;
}

bool ParseNonNegativeInt(const std::wstring& text, int& value)
{
    value = 0;
    const std::wstring trimmed = TrimWide(text);
    if (trimmed.empty())
        return false;
    long long total = 0;
    for (size_t i = 0; i < trimmed.size(); ++i)
    {
        if (trimmed[i] < L'0' || trimmed[i] > L'9')
            return false;
        total = total * kDecimalBase + (trimmed[i] - L'0');
        if (total > INT_MAX)
            return false;
    }
    value = static_cast<int>(total);
    return true;
}

bool ParseHexColor(const std::string& text, uint32_t& color)
{
    color = 0;
    if (static_cast<int>(text.size()) != kBotColorLength || text[0] != '#')
        return false;
    uint32_t rgb = 0;
    for (size_t i = 1; i < text.size(); ++i)
    {
        const int v = HexValue(text[i]);
        if (v < 0)
            return false;
        rgb = rgb * kHexBase + static_cast<uint32_t>(v);
    }
    //文字是 RRGGBB，COLORREF 的排列是 0x00BBGGRR
    const uint32_t r = (rgb >> kColorHighShift) & kColorChannelMask;
    const uint32_t g = (rgb >> kColorChannelBits) & kColorChannelMask;
    const uint32_t b = rgb & kColorChannelMask;
    color = r | (g << kColorChannelBits) | (b << kColorHighShift);
    return true;
}

std::string FormatHexColor(uint32_t color)
{
    const unsigned r = color & kColorChannelMask;
    const unsigned g = (color >> kColorChannelBits) & kColorChannelMask;
    const unsigned b = (color >> kColorHighShift) & kColorChannelMask;
    char buf[kColorBufferChars] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "#%02X%02X%02X", r, g, b);
    return buf;
}

} // namespace flamingobot
