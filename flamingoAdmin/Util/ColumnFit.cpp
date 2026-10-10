/**
 *  flamingoAdmin —— 表格列宽按可用宽度分配，规则见 ColumnFit.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Util/ColumnFit.h"

namespace flamingoadmin {

std::vector<int> FitColumnWidths(const std::vector<ColumnSpec>& specs, int available)
{
    std::vector<int> widths;
    widths.reserve(specs.size());
    int total = 0;
    for (size_t i = 0; i < specs.size(); ++i)
    {
        widths.push_back(specs[i].m_width);
        total += specs[i].m_width;
    }

    int deficit = total - available;
    while (deficit > 0)
    {
        // 本轮仍可压缩的列（权重大于 0 且还没到最小宽度）的权重之和。
        int weightSum = 0;
        for (size_t i = 0; i < specs.size(); ++i)
        {
            if (specs[i].m_weight > 0 && widths[i] > specs[i].m_minWidth)
            {
                weightSum += specs[i].m_weight;
            }
        }
        if (weightSum == 0)
        {
            // 可压缩的列都已到最小宽度：不再压缩，剩余缺口由调用方裁剪。
            break;
        }

        // 按权重向下取整分摊缺口，每列最多压到最小宽度。
        int cutTotal = 0;
        for (size_t i = 0; i < specs.size(); ++i)
        {
            if (specs[i].m_weight <= 0 || widths[i] <= specs[i].m_minWidth)
            {
                continue;
            }
            int cut = deficit * specs[i].m_weight / weightSum;
            const int room = widths[i] - specs[i].m_minWidth;
            if (cut > room)
            {
                cut = room;
            }
            widths[i] -= cut;
            cutTotal += cut;
        }

        if (cutTotal == 0)
        {
            // 缺口小于权重之和、向下取整全为 0：零头按列的顺序每列 1 像素。
            for (size_t i = 0; i < specs.size() && deficit > 0; ++i)
            {
                if (specs[i].m_weight > 0 && widths[i] > specs[i].m_minWidth)
                {
                    --widths[i];
                    --deficit;
                }
            }
            continue;
        }
        deficit -= cutTotal;
    }
    return widths;
}

} // namespace flamingoadmin
