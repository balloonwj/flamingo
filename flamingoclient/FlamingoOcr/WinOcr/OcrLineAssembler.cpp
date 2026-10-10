/**
 *  拼行整理的实现，说明见 OcrLineAssembler.h。算法与第 0 期试验脚本（tmp/ocrspike/ocrlib.py 的 merge_line、
 *  line_height）逐条对应，阈值相同。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrLineAssembler.h"

#include <algorithm>

namespace flamingoocr {

namespace {

//按汉字对待的 Unicode 区段（闭区间）
struct CodeRange
{
    wchar_t m_first;   //区段起点
    wchar_t m_last;    //区段终点
};

const CodeRange kCjkRanges[] =
{
    { 0x2E80, 0x2FFF },   //中日韩部首补充、康熙部首、表意文字描述字符
    { 0x3000, 0x303F },   //中日韩符号和标点（。、「」等）
    { 0x3040, 0x30FF },   //平假名、片假名
    { 0x3100, 0x312F },   //注音符号
    { 0x3400, 0x4DBF },   //中日韩统一表意文字扩展 A
    { 0x4E00, 0x9FFF },   //中日韩统一表意文字
    { 0xAC00, 0xD7AF },   //谚文音节
    { 0xD800, 0xDFFF },   //UTF-16 代理项：扩展 B 及以后的汉字由两个代理项组成
    { 0xF900, 0xFAFF },   //中日韩兼容表意文字
    { 0xFE30, 0xFE4F },   //中日韩兼容形式（竖排标点）
    { 0xFF00, 0xFFEF },   //全角与半角形式（，：（）等）
};

//中位数：个数为奇数时取正中间的一个，为偶数时取中间两个的平均值；values 会被排序
float MedianOf(std::vector<float>& values)
{
    if (values.empty())
        return 0.0f;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1)
        return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0f;
}

//词里是否含有按汉字对待的字符
bool ContainsCjk(const std::wstring& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (IsCjkChar(text[i]))
            return true;
    }
    return false;
}

//按横坐标比较两个词，供稳定排序使用
bool LessByX(const OcrWordBox& a, const OcrWordBox& b)
{
    return a.m_rect.m_x < b.m_rect.m_x;
}

} // namespace

OcrWordBox::OcrWordBox()
{
    m_rect.m_x = 0;
    m_rect.m_y = 0;
    m_rect.m_w = 0;
    m_rect.m_h = 0;
}

bool IsCjkChar(wchar_t ch)
{
    for (size_t i = 0; i < sizeof(kCjkRanges) / sizeof(kCjkRanges[0]); ++i)
    {
        if (ch >= kCjkRanges[i].m_first && ch <= kCjkRanges[i].m_last)
            return true;
    }
    return false;
}

float LineHeightOf(const std::vector<OcrWordBox>& words)
{
    std::vector<float> cjkHeights;
    std::vector<float> allHeights;
    for (size_t i = 0; i < words.size(); ++i)
    {
        allHeights.push_back(words[i].m_rect.m_h);
        if (ContainsCjk(words[i].m_text))
            cjkHeights.push_back(words[i].m_rect.m_h);
    }
    return cjkHeights.empty() ? MedianOf(allHeights) : MedianOf(cjkHeights);
}

float MedianLineHeight(const std::vector<std::vector<OcrWordBox>>& lines)
{
    std::vector<float> heights;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (!lines[i].empty())
            heights.push_back(LineHeightOf(lines[i]));
    }
    return MedianOf(heights);
}

std::vector<OcrWordBox> SortWordsByX(const std::vector<OcrWordBox>& words)
{
    std::vector<OcrWordBox> sorted(words);
    std::stable_sort(sorted.begin(), sorted.end(), LessByX);
    return sorted;
}

std::wstring AssembleLineText(const std::vector<OcrWordBox>& words)
{
    std::vector<OcrWordBox> sorted;
    std::vector<OcrWordBox> all = SortWordsByX(words);
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (!all[i].m_text.empty())
            sorted.push_back(all[i]);
    }
    if (sorted.empty())
        return std::wstring();

    float height = LineHeightOf(sorted);
    if (height <= 0.0f)
        height = 1.0f;   //词框高度都为 0 时按 1 像素算，避免比例阈值失效

    std::wstring out = sorted[0].m_text;
    for (size_t i = 1; i < sorted.size(); ++i)
    {
        const OcrWordBox& prev = sorted[i - 1];
        const OcrWordBox& cur = sorted[i];
        const float gap = cur.m_rect.m_x - (prev.m_rect.m_x + prev.m_rect.m_w);
        const bool prevCjk = IsCjkChar(prev.m_text[prev.m_text.size() - 1]);
        const bool curCjk = IsCjkChar(cur.m_text[0]);

        bool space = false;
        if (prevCjk && curCjk)
            space = false;
        else if (prevCjk || curCjk)
            space = gap > kMixedSpaceRatio * height;
        else
            space = gap > kLatinSpaceRatio * height;

        if (space)
            out += L' ';
        out += cur.m_text;
    }
    return out;
}

OcrRect UnionRect(const std::vector<OcrWordBox>& words)
{
    OcrRect out;
    out.m_x = 0;
    out.m_y = 0;
    out.m_w = 0;
    out.m_h = 0;
    if (words.empty())
        return out;

    float left = words[0].m_rect.m_x;
    float top = words[0].m_rect.m_y;
    float right = words[0].m_rect.m_x + words[0].m_rect.m_w;
    float bottom = words[0].m_rect.m_y + words[0].m_rect.m_h;
    for (size_t i = 1; i < words.size(); ++i)
    {
        const OcrRect& r = words[i].m_rect;
        left = (std::min)(left, r.m_x);
        top = (std::min)(top, r.m_y);
        right = (std::max)(right, r.m_x + r.m_w);
        bottom = (std::max)(bottom, r.m_y + r.m_h);
    }
    out.m_x = left;
    out.m_y = top;
    out.m_w = right - left;
    out.m_h = bottom - top;
    return out;
}

} // namespace flamingoocr
