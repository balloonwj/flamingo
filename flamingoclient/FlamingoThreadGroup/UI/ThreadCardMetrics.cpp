/**
 *  帖子卡片的排版计算，见 ThreadCardMetrics.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadCardMetrics.h"

#include <vector>

namespace threadgroup {

namespace {

//测量折行文字用的 DrawText 选项：按词折行、不解释 & 前缀、按编辑框的规则处理最后一行（与标签控件绘制时一致）
const UINT kWrapFlags = DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_LEFT;

//截短时加在末尾的省略号
const wchar_t* const kEllipsis = L"\x2026";

//单行文字的高度（用一个字母测量），单位：像素
int SingleLineHeight(HDC hdc)
{
    RECT rc = { 0, 0, 0, 0 };
    ::DrawTextW(hdc, L"A", 1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    return rc.bottom - rc.top;
}

//文字里可以截断的位置（不拆开 UTF-16 代理对）：0、每个字符之后
std::vector<size_t> CutPoints(const std::wstring& text)
{
    std::vector<size_t> points;
    points.push_back(0);
    size_t i = 0;
    while (i < text.size())
    {
        const bool high = text[i] >= 0xD800 && text[i] <= 0xDBFF;
        i += (high && i + 1 < text.size()) ? 2 : 1;
        points.push_back(i);
    }
    return points;
}

//去掉末尾的空白与换行（截短后接省略号之前用，免得省略号单独落到下一行）
std::wstring TrimRight(const std::wstring& text)
{
    size_t end = text.size();
    while (end > 0 && (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r' || text[end - 1] == L'\n'
                       || text[end - 1] == 0x3000))
        --end;
    return text.substr(0, end);
}

} // namespace

int MeasureWrappedHeight(HDC hdc, const std::wstring& text, int width)
{
    if (text.empty() || width <= 0)
        return 0;
    RECT rc = { 0, 0, width, 0 };
    ::DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, kWrapFlags);
    return rc.bottom - rc.top;
}

std::wstring FitTextToLines(HDC hdc, const std::wstring& text, int width, int maxLines, bool& cut)
{
    cut = false;
    if (text.empty() || width <= 0)
        return text;
    const int lines = (maxLines > 0) ? maxLines : 1;
    const int maxHeight = SingleLineHeight(hdc) * lines;
    if (MeasureWrappedHeight(hdc, text, width) <= maxHeight)
        return text;
    //二分查找放得下的最长前缀（加上省略号之后仍不超过 maxLines 行）
    cut = true;
    const std::vector<size_t> points = CutPoints(text);
    size_t lo = 0;
    size_t hi = points.size() - 1;
    while (lo < hi)
    {
        const size_t mid = (lo + hi + 1) / 2;
        const std::wstring candidate = TrimRight(text.substr(0, points[mid])) + kEllipsis;
        if (MeasureWrappedHeight(hdc, candidate, width) <= maxHeight)
            lo = mid;
        else
            hi = mid - 1;
    }
    return TrimRight(text.substr(0, points[lo])) + kEllipsis;
}

PostCardMetrics MeasurePostCard(HDC hdc, HFONT bodyFont, HFONT titleFont, int innerWidth, const std::wstring& title,
                                const std::wstring& displayText, int imageCount, bool hasFoot)
{
    PostCardMetrics m;
    const int width = (innerWidth > 0) ? innerWidth : 1;
    int height = kCardPadY + kCardMetaHeight;

    if (!title.empty())
    {
        HGDIOBJ old = ::SelectObject(hdc, titleFont);
        m.m_titleHeight = SingleLineHeight(hdc);
        ::SelectObject(hdc, old);
        height += kCardGap + m.m_titleHeight;
    }

    if (!displayText.empty())
    {
        HGDIOBJ old = ::SelectObject(hdc, bodyFont);
        m.m_text = FitTextToLines(hdc, displayText, width, kCardMaxTextLines, m.m_textCut);
        m.m_textHeight = MeasureWrappedHeight(hdc, m.m_text, width);
        ::SelectObject(hdc, old);
        height += kCardGap + m.m_textHeight;
    }

    if (imageCount > 0)
    {
        m.m_thumbCount = (imageCount < kCardMaxThumbs) ? imageCount : kCardMaxThumbs;
        m.m_moreCount = (imageCount > kCardMaxThumbs) ? imageCount - kCardMaxThumbs : 0;
        height += kCardGap + kCardThumbPx;
    }

    m.m_hasFoot = hasFoot;
    if (hasFoot)
        height += kCardGap + kCardFootHeight;
    height += kCardPadY;
    m.m_height = height;
    return m;
}

} // namespace threadgroup
