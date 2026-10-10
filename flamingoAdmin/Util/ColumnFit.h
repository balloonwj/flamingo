/**
 *  flamingoAdmin —— 表格列宽按可用宽度分配的纯函数。
 *
 *  自绘表格（如企业列表）的各列有设计宽度；窗口不够宽时，按各列的压缩权重从可压缩的列里
 *  扣减宽度，扣到各自的最小宽度为止，使「操作」这类必须可点的列仍落在表格内。单元测试见
 *  Tests/ColumnFitTests.cpp（COL1）。
 *
 *  典型用法：
 *      std::vector<ColumnSpec> specs = ...;            // 每列的设计宽度、最小宽度、压缩权重
 *      std::vector<int> widths = FitColumnWidths(specs, availableWidth);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <vector>

namespace flamingoadmin {

// 一列的宽度规格（单位：像素）。
struct ColumnSpec
{
    int m_width;     // 设计宽度：可用宽度充足时取这个值
    int m_minWidth;  // 最小宽度：压缩时不会低于它；不应大于 m_width
    int m_weight;    // 压缩权重：可用宽度不足时按权重比例分摊缺口；0 表示这一列不压缩

    ColumnSpec()
        : m_width(0), m_minWidth(0), m_weight(0)
    {
    }
};

// 按可用宽度分配各列宽度。
//   specs：各列规格，顺序即列的顺序。
//   available：可用宽度（像素），可为负数（按极窄处理）。
// 返回：与 specs 一一对应的列宽。规则：
//   1. 设计宽度之和不超过 available 时，各列取设计宽度；
//   2. 否则只压缩权重大于 0 的列：每一轮把缺口按权重向下取整分摊，每列最多压到最小宽度，
//      因最小宽度限制没分掉的部分进入下一轮，由其余仍可压缩的列分摊；取整剩下的零头按列的
//      顺序每列 1 像素；
//   3. 可压缩的列都到了最小宽度仍不够时停止，此时合计会大于 available，由调用方裁剪。
std::vector<int> FitColumnWidths(const std::vector<ColumnSpec>& specs, int available);

} // namespace flamingoadmin
