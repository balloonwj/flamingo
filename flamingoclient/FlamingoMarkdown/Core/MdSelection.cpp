/**
 *  Markdown 文档命中测试与取文字的实现，说明见 MdSelection.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdSelection.h"

#include <algorithm>
#include <climits>

namespace flamingomd {

namespace {

//行的远近评分里纵向距离的权重：纵向差一个像素比横向差很多像素更远，保证先按纵向选行
const long long kVerticalWeight = 1000000;

//点到区间 [lo, hi) 的距离；落在区间里为 0
int DistanceToRange(int v, int lo, int hi)
{
    if (v < lo)
        return lo - v;
    if (v >= hi)
        return v - hi + 1;
    return 0;
}

//片段内离 relX 最近的字符边界（0 ~ 字符数）
int CharIndexInFragment(const MdFragment& frag, int relX)
{
    int prevRight = frag.m_padLeft;
    for (size_t i = 0; i < frag.m_caret.size(); ++i)
    {
        const int right = frag.m_caret[i];
        if (relX < (prevRight + right) / 2)
            return static_cast<int>(i);
        prevRight = right;
    }
    return static_cast<int>(frag.m_caret.size());
}

} // namespace

void HitTestMdLayout(const MdLayoutResult& layout, int x, int y, MdHitInfo& out)
{
    out = MdHitInfo();
    POINT pt = { x, y };

    //所在代码块与「复制」按钮
    for (size_t i = 0; i < layout.m_codeRects.size(); ++i)
    {
        if (::PtInRect(&layout.m_codeRects[i], pt))
        {
            out.m_codeBlockIndex = static_cast<int>(i);
            if (::PtInRect(&layout.m_copyButtons[i], pt))
                out.m_kind = md_hit_code_copy;
            break;
        }
    }

    //找行
    int best = -1;
    long long bestScore = LLONG_MAX;
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const MdLine& line = layout.m_lines[i];
        const int dy = DistanceToRange(y, line.m_top, line.m_bottom);
        const int dx = DistanceToRange(x, line.m_left, line.m_right);
        const long long score = dy * kVerticalWeight + dx;
        if (score < bestScore)
        {
            bestScore = score;
            best = static_cast<int>(i);
            if (score == 0)
                break;
        }
    }
    if (best < 0)
        return;

    const MdLine& line = layout.m_lines[best];
    const bool inLineY = (y >= line.m_top && y < line.m_bottom);
    if (line.m_fragmentCount <= 0)
    {
        out.m_charPos = line.m_flatBegin;
        return;
    }

    //行内找片段
    const int firstIdx = line.m_firstFragment;
    const int lastIdx = line.m_firstFragment + line.m_fragmentCount - 1;
    const MdFragment& first = layout.m_fragments[firstIdx];
    if (x < first.m_x)
    {
        out.m_charPos = first.m_flatBegin;
        return;
    }
    for (int f = firstIdx; f <= lastIdx; ++f)
    {
        const MdFragment& frag = layout.m_fragments[f];
        if (x < frag.m_x + frag.m_width)
        {
            if (x < frag.m_x)
            {
                //落在两个片段之间的空隙（换行处丢掉的空格不会出现在行中间，这里只是保险）
                out.m_charPos = frag.m_flatBegin;
                return;
            }
            out.m_charPos = frag.m_flatBegin + CharIndexInFragment(frag, x - frag.m_x);
            if (inLineY && out.m_kind != md_hit_code_copy)
            {
                out.m_kind = (frag.m_linkIndex >= 0) ? md_hit_link : md_hit_text;
                out.m_linkIndex = frag.m_linkIndex;
            }
            return;
        }
    }
    out.m_charPos = line.m_flatEnd;
}

std::wstring ToCrLf(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + text.size() / 8);
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (ch == L'\n' && (i == 0 || text[i - 1] != L'\r'))
            out.push_back(L'\r');
        out.push_back(ch);
    }
    return out;
}

std::wstring GetMdTextRange(const MdModel& model, int begin, int end)
{
    if (begin > end)
        std::swap(begin, end);
    const int length = static_cast<int>(model.m_flatText.size());
    begin = (std::max)(begin, 0);
    end = (std::min)(end, length);
    if (begin >= end)
        return std::wstring();
    return ToCrLf(model.m_flatText.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin)));
}

} // namespace flamingomd
