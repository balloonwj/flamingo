/**
 *  ChatTextSelection.cpp
 *
 *  聊天气泡内选中文字的纯逻辑实现 —— 见同名头文件的说明。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "ChatTextSelection.h"

#include <cstddef>

namespace balloonchatwnd {

namespace {

// 把 v 夹到 [lo, hi]。
int Clamp(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

// 取某段内第 k 个字符的左边界 x。第 0 个用段左边界，其余用前一个字符的右边界。
int CharLeftInRun(const SelRun& run, int k)
{
    if (k <= 0)
    {
        return run.m_left;
    }
    const std::size_t idx = static_cast<std::size_t>(k - 1);
    if (idx < run.m_charRights.size())
    {
        return run.m_charRights[idx];
    }
    return run.m_right;
}

// 取某段内第 k 个字符的右边界 x。越界时退回段右边界。
int CharRightInRun(const SelRun& run, int k)
{
    const std::size_t idx = static_cast<std::size_t>(k);
    if (k >= 0 && idx < run.m_charRights.size())
    {
        return run.m_charRights[idx];
    }
    return run.m_right;
}

// 一行的首字符下标；空行退回 fallback。
int LineBeginChar(const SelLine& line, int fallback)
{
    if (line.m_runs.empty())
    {
        return fallback;
    }
    return line.m_runs.front().m_charBegin;
}

// 一行的末尾下标（最后一个字符之后）；空行退回 fallback。
int LineEndChar(const SelLine& line, int fallback)
{
    if (line.m_runs.empty())
    {
        return fallback;
    }
    const SelRun& last = line.m_runs.back();
    return last.m_charBegin + last.m_charCount;
}

// 取全文下标 idx 在某行内对应的 x（用于算选区矩形的左右边界）。
// idx 在行首之前取行左边界，在行尾之后取行右边界。
int XForCharInLine(const SelLine& line, int idx)
{
    if (line.m_runs.empty())
    {
        return 0;
    }
    if (idx <= line.m_runs.front().m_charBegin)
    {
        return line.m_runs.front().m_left;
    }
    const SelRun& last = line.m_runs.back();
    if (idx >= last.m_charBegin + last.m_charCount)
    {
        return last.m_right;
    }
    for (std::size_t i = 0; i < line.m_runs.size(); ++i)
    {
        const SelRun& run = line.m_runs[i];
        if (idx >= run.m_charBegin && idx < run.m_charBegin + run.m_charCount)
        {
            return CharLeftInRun(run, idx - run.m_charBegin);
        }
    }
    return last.m_right;
}

// 是不是 ASCII 词字符（字母 / 数字 / 下划线）—— 双击选词时按"词"整体选中。
bool IsAsciiWordChar(wchar_t c)
{
    if (c >= L'a' && c <= L'z')
    {
        return true;
    }
    if (c >= L'A' && c <= L'Z')
    {
        return true;
    }
    if (c >= L'0' && c <= L'9')
    {
        return true;
    }
    return c == L'_';
}

// 是不是空白（含换行）。
bool IsSpaceChar(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

} // anonymous namespace

int CharIndexFromPoint(const SelLayout& layout, int x, int y)
{
    if (layout.m_lines.empty())
    {
        return 0;
    }

    // 首行之上 → 全文开头；末行之下 → 全文末尾。
    if (y < layout.m_lines.front().m_top)
    {
        return LineBeginChar(layout.m_lines.front(), 0);
    }
    if (y >= layout.m_lines.back().m_bottom)
    {
        return layout.m_totalChars;
    }

    // 找到 y 落在哪一行；落在行间空隙时归到其上方那一行（视觉上更贴近用户意图）。
    std::size_t lineIdx = layout.m_lines.size() - 1;
    for (std::size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        if (y < layout.m_lines[i].m_bottom)
        {
            lineIdx = i;
            break;
        }
    }
    const SelLine& line = layout.m_lines[lineIdx];
    if (line.m_runs.empty())
    {
        return LineBeginChar(line, 0);
    }

    // 行首之左 / 行尾之右。行尾取"最后一个字符之后"，不含换行符本身。
    if (x < line.m_runs.front().m_left)
    {
        return line.m_runs.front().m_charBegin;
    }
    const SelRun& lastRun = line.m_runs.back();
    if (x >= lastRun.m_right)
    {
        return lastRun.m_charBegin + lastRun.m_charCount;
    }

    // 逐段找 x 落在哪一段，再在段内按"字符中线"取舍。
    for (std::size_t i = 0; i < line.m_runs.size(); ++i)
    {
        const SelRun& run = line.m_runs[i];
        if (x < run.m_left || x >= run.m_right)
        {
            continue;
        }
        for (int k = 0; k < run.m_charCount; ++k)
        {
            const int l = CharLeftInRun(run, k);
            const int r = CharRightInRun(run, k);
            if (x >= r)
            {
                continue;
            }
            // 落在左半边取该字符之前的位置，右半边取之后 —— 常规文本选择手感。
            const int mid = l + (r - l) / 2;
            return run.m_charBegin + k + ((x < mid) ? 0 : 1);
        }
        return run.m_charBegin + run.m_charCount;
    }

    // x 落在两段之间的空隙：归到下一段的段首。
    for (std::size_t i = 0; i < line.m_runs.size(); ++i)
    {
        if (x < line.m_runs[i].m_left)
        {
            return line.m_runs[i].m_charBegin;
        }
    }
    return lastRun.m_charBegin + lastRun.m_charCount;
}

void NormalizeSelRange(int anchor, int caret, int& startOut, int& endOut)
{
    if (anchor <= caret)
    {
        startOut = anchor;
        endOut   = caret;
    }
    else
    {
        startOut = caret;
        endOut   = anchor;
    }
}

std::vector<SelRect> BuildSelectionRects(const SelLayout& layout, int start, int end)
{
    std::vector<SelRect> out;

    const int total = layout.m_totalChars;
    start = Clamp(start, 0, total);
    end   = Clamp(end, 0, total);
    if (start >= end)
    {
        return out;   // 空选区
    }

    for (std::size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const SelLine& line = layout.m_lines[i];
        if (line.m_runs.empty())
        {
            continue;
        }
        const int lineBegin = LineBeginChar(line, 0);
        const int lineEnd   = LineEndChar(line, 0);

        // 与本行的交集为空则跳过。
        const int segBegin = (start > lineBegin) ? start : lineBegin;
        const int segEnd   = (end   < lineEnd)   ? end   : lineEnd;
        if (segBegin >= segEnd)
        {
            continue;
        }

        SelRect rc;
        rc.m_left   = XForCharInLine(line, segBegin);
        rc.m_right  = XForCharInLine(line, segEnd);
        rc.m_top    = line.m_top;
        rc.m_bottom = line.m_bottom;
        if (rc.m_right > rc.m_left)
        {
            out.push_back(rc);
        }
    }

    return out;
}

std::wstring ExtractSelectedText(const std::wstring& flatText, int start, int end,
                                 const std::wstring& faceLabel)
{
    const int total = static_cast<int>(flatText.size());
    start = Clamp(start, 0, total);
    end   = Clamp(end, 0, total);
    if (start >= end)
    {
        return std::wstring();
    }

    std::wstring out;
    out.reserve(static_cast<std::size_t>(end - start));
    for (int i = start; i < end; ++i)
    {
        const wchar_t c = flatText[static_cast<std::size_t>(i)];
        if (c == kSelFacePlaceholder)
        {
            out += faceLabel;   // 表情占位 → 可读文案
        }
        else
        {
            out += c;
        }
    }
    return out;
}

void WordRangeAt(const std::wstring& flatText, int index, int& startOut, int& endOut)
{
    const int total = static_cast<int>(flatText.size());
    startOut = 0;
    endOut   = 0;
    if (total <= 0)
    {
        return;
    }

    // 下标夹到最后一个字符上 —— 在末尾之后双击时选中最后一个词更符合预期。
    index = Clamp(index, 0, total - 1);
    const wchar_t c = flatText[static_cast<std::size_t>(index)];

    if (IsAsciiWordChar(c))
    {
        int s = index;
        while (s > 0 && IsAsciiWordChar(flatText[static_cast<std::size_t>(s - 1)]))
        {
            --s;
        }
        int e = index + 1;
        while (e < total && IsAsciiWordChar(flatText[static_cast<std::size_t>(e)]))
        {
            ++e;
        }
        startOut = s;
        endOut   = e;
        return;
    }

    if (IsSpaceChar(c))
    {
        int s = index;
        while (s > 0 && IsSpaceChar(flatText[static_cast<std::size_t>(s - 1)]))
        {
            --s;
        }
        int e = index + 1;
        while (e < total && IsSpaceChar(flatText[static_cast<std::size_t>(e)]))
        {
            ++e;
        }
        startOut = s;
        endOut   = e;
        return;
    }

    // 汉字 / 标点 / 表情占位：只选这一个字符。中文没有词边界，逐字选是通行做法。
    startOut = index;
    endOut   = index + 1;
}

int HitTestCharRanges(const SelLayout& layout,
                      const std::vector<std::pair<int, int> >& ranges,
                      int x, int y)
{
    for (std::size_t i = 0; i < ranges.size(); ++i)
    {
        const int begin = ranges[i].first;
        const int end   = ranges[i].second;
        if (end <= begin)
        {
            continue;   // 空区间不可能命中
        }
        // 复用选区矩形的算法：区间跨行时每行一块，逐块判包含即可，不必自己按行拆。
        const std::vector<SelRect> rects = BuildSelectionRects(layout, begin, end);
        for (std::size_t r = 0; r < rects.size(); ++r)
        {
            if (x >= rects[r].m_left && x < rects[r].m_right
                && y >= rects[r].m_top && y < rects[r].m_bottom)
            {
                return (int)i;
            }
        }
    }
    return -1;
}

} // namespace balloonchatwnd
