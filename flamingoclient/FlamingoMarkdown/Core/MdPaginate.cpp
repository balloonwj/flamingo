/**
 *  已排版文档的按页切分，说明见 MdPaginate.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdPaginate.h"

#include <algorithm>
#include <utility>

namespace flamingomd {

namespace {

typedef std::pair<int, int> Span;   //一行占据的纵向区间：first 顶边，second 底边（不含）

/**
 *  把各行的区间合并成互不重叠的开区间：后一行的顶边严格小于前面的底边时视为重叠（同一表格行里的单元格、
 *  代码块与行内高度不同的行）。只是首尾相接的行不合并，接缝处正是可以切分的位置。
 */
void MergeSpans(const MdLayoutResult& layout, std::vector<Span>& merged)
{
    std::vector<Span> spans;
    spans.reserve(layout.m_lines.size());
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        if (layout.m_lines[i].m_bottom > layout.m_lines[i].m_top)
            spans.push_back(Span(layout.m_lines[i].m_top, layout.m_lines[i].m_bottom));
    }
    std::sort(spans.begin(), spans.end());
    merged.clear();
    for (size_t i = 0; i < spans.size(); ++i)
    {
        if (!merged.empty() && spans[i].first < merged.back().second)
            merged.back().second = (std::max)(merged.back().second, spans[i].second);
        else
            merged.push_back(spans[i]);
    }
}

//位置 y 是否严格落在某个合并后的区间里（即落在某一行的中间）
bool InsideMerged(const std::vector<Span>& merged, int y)
{
    //找第一个顶边不小于 y 的区间，y 只可能落在它前面那个区间里
    std::vector<Span>::const_iterator it =
        std::lower_bound(merged.begin(), merged.end(), Span(y, y));
    if (it == merged.begin())
        return false;
    --it;
    return it->first < y && y < it->second;
}

} // namespace

void PaginateMdLayout(const MdLayoutResult& layout, int pageHeight, std::vector<int>& starts)
{
    starts.clear();
    starts.push_back(0);
    if (pageHeight <= 0 || layout.m_height <= pageHeight)
        return;

    std::vector<Span> merged;
    MergeSpans(layout, merged);

    //候选切分位置：每一行的顶边与底边，去掉落在别的行中间的
    std::vector<int> valid;
    valid.reserve(layout.m_lines.size() * 2);
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const int edges[2] = { layout.m_lines[i].m_top, layout.m_lines[i].m_bottom };
        for (int k = 0; k < 2; ++k)
        {
            if (edges[k] > 0 && edges[k] < layout.m_height && !InsideMerged(merged, edges[k]))
                valid.push_back(edges[k]);
        }
    }
    std::sort(valid.begin(), valid.end());
    valid.erase(std::unique(valid.begin(), valid.end()), valid.end());

    //贪心：每页取不超过页面底边的最远一个候选位置；没有候选时从页面高度处硬切
    int pageStart = 0;
    while (pageStart + pageHeight < layout.m_height)
    {
        const int limit = pageStart + pageHeight;
        std::vector<int>::const_iterator it = std::upper_bound(valid.begin(), valid.end(), limit);
        int next = pageStart;
        if (it != valid.begin())
        {
            --it;
            if (*it > pageStart)
                next = *it;
        }
        if (next <= pageStart)
            next = limit;
        starts.push_back(next);
        pageStart = next;
    }
}

} // namespace flamingomd
