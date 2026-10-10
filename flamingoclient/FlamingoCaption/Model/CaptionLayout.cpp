/**
 *  字幕的折行，实现见 CaptionLayout.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionLayout.h"

#include "CaptionText.h"

namespace caption {

namespace {

bool IsAsciiAlnum(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

//低位代理：不能在它前面断开（会拆开一个扩展区汉字）
bool IsLowSurrogate(wchar_t ch)
{
    return ch >= 0xDC00 && ch <= 0xDFFF;
}

//在 line[pos] 之前能否断开
bool CanBreakBefore(const std::wstring& line, size_t pos)
{
    if (pos == 0 || pos >= line.size())
        return false;
    const wchar_t prev = line[pos - 1];
    const wchar_t cur = line[pos];
    if (IsLowSurrogate(cur))
        return false;
    //英文单词内部不断开
    if (IsAsciiAlnum(prev) && IsAsciiAlnum(cur))
        return false;
    //断开处标点不放到行首
    if (IsBreakPunct(cur))
        return false;
    return true;
}

std::wstring TrimSpaces(const std::wstring& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && s[b] == L' ')
        ++b;
    while (e > b && s[e - 1] == L' ')
        --e;
    return s.substr(b, e - b);
}

//折一个不含换行符的段落
void WrapParagraph(const std::wstring& para, float maxWidth, CaptionMeasureFn measure, void* ctx,
                   std::vector<std::wstring>& out)
{
    std::wstring rest = TrimSpaces(para);
    while (!rest.empty())
    {
        if (measure(ctx, rest.c_str(), static_cast<int>(rest.size())) <= maxWidth)
        {
            out.push_back(rest);
            return;
        }
        //找能放下的最长前缀（逐字增加）
        size_t fit = 1;
        while (fit < rest.size() && measure(ctx, rest.c_str(), static_cast<int>(fit + 1)) <= maxWidth)
            ++fit;
        if (fit < rest.size() && IsLowSurrogate(rest[fit]))
            ++fit;
        //往回找断开处；找不到（一个单词比整行还宽）就在 fit 处硬断
        size_t cut = fit;
        while (cut > 0 && !CanBreakBefore(rest, cut))
            --cut;
        if (cut == 0)
            cut = fit;
        const std::wstring line = TrimSpaces(rest.substr(0, cut));
        if (!line.empty())
            out.push_back(line);
        rest = TrimSpaces(rest.substr(cut));
    }
}

} // namespace

std::vector<std::wstring> WrapCaptionText(const std::wstring& text, float maxWidth, CaptionMeasureFn measure, void* ctx)
{
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == L'\n')
        {
            WrapParagraph(text.substr(start, i - start), maxWidth, measure, ctx, out);
            start = i + 1;
        }
    }
    return out;
}

} // namespace caption
