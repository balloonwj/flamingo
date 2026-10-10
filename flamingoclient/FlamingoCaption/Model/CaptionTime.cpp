/**
 *  语音字幕的时间格式，实现见 CaptionTime.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionTime.h"

#include <stdio.h>

namespace caption {

namespace {

//时间单位换算
const int64_t kMsPerSecond = 1000;
const int64_t kSecondsPerMinute = 60;
const int64_t kMinutesPerHour = 60;
const int64_t kMsPerMinute = kMsPerSecond * kSecondsPerMinute;
const int64_t kMsPerHour = kMsPerMinute * kMinutesPerHour;
//十分之一秒的毫秒数
const int64_t kMsPerTenth = 100;
//毫秒部分最多 3 位数字；不足 3 位时按小数补位用的乘数
const int kMaxFractionDigits = 3;
const int kFractionScale[] = { 1, 100, 10, 1 };
//格式化缓冲区的大小（字符），足够容纳最长的时间文字
const size_t kTimeBufferChars = 32;

bool IsDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

//从 text[pos] 起读一段数字；读到至少一位返回 true，pos 移到数字之后
bool ReadNumber(const std::string& text, size_t& pos, int64_t& value, int& digits)
{
    value = 0;
    digits = 0;
    while (pos < text.size() && IsDigit(text[pos]))
    {
        value = value * 10 + (text[pos] - '0');
        ++digits;
        ++pos;
    }
    return digits > 0;
}

} // namespace

std::string FormatSrtTime(int64_t ms)
{
    if (ms < 0)
        ms = 0;
    const int64_t h = ms / kMsPerHour;
    const int64_t m = (ms % kMsPerHour) / kMsPerMinute;
    const int64_t s = (ms % kMsPerMinute) / kMsPerSecond;
    const int64_t f = ms % kMsPerSecond;
    char buf[kTimeBufferChars];
    sprintf_s(buf, "%02lld:%02lld:%02lld,%03lld", h, m, s, f);
    return buf;
}

bool ParseSrtTime(const std::string& text, int64_t* ms)
{
    size_t pos = 0;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;
    int64_t h = 0;
    int64_t m = 0;
    int64_t s = 0;
    int64_t f = 0;
    int digits = 0;
    if (!ReadNumber(text, pos, h, digits) || pos >= text.size() || text[pos] != ':')
        return false;
    ++pos;
    if (!ReadNumber(text, pos, m, digits) || m >= kMinutesPerHour || pos >= text.size() || text[pos] != ':')
        return false;
    ++pos;
    if (!ReadNumber(text, pos, s, digits) || s >= kSecondsPerMinute)
        return false;
    if (pos < text.size() && (text[pos] == ',' || text[pos] == '.'))
    {
        ++pos;
        if (!ReadNumber(text, pos, f, digits) || digits > kMaxFractionDigits)
            return false;
        f *= kFractionScale[digits];
    }
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r'))
        ++pos;
    if (pos != text.size())
        return false;
    *ms = h * kMsPerHour + m * kMsPerMinute + s * kMsPerSecond + f;
    return true;
}

std::wstring FormatClock(int64_t ms)
{
    if (ms < 0)
        ms = 0;
    const int64_t h = ms / kMsPerHour;
    const int64_t m = (ms % kMsPerHour) / kMsPerMinute;
    const int64_t s = (ms % kMsPerMinute) / kMsPerSecond;
    const int64_t d = (ms % kMsPerSecond) / kMsPerTenth;
    wchar_t buf[kTimeBufferChars];
    if (h > 0)
        swprintf_s(buf, L"%lld:%02lld:%02lld.%lld", h, m, s, d);
    else
        swprintf_s(buf, L"%lld:%02lld.%lld", m, s, d);
    return buf;
}

} // namespace caption
