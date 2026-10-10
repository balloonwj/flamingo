/**
 *  flamingoAdmin —— 表格表头（共用控件）。
 *
 *  按一组列定义（标题、宽度、对齐方式）画出列标题与底部分隔线。列宽的计算单独做成静态函数
 *  ComputeColumnBounds，表格的绘行回调也用它算每个单元格的左右边界，保证表头与数据行对齐。
 *  允许把某一列标成「占满剩余宽度」，窗口变宽变窄时由这一列伸缩。
 *  敏感词页首先使用（2026-10-02）；企业管理页的表头仍是页面内部的实现，以后可以换成本控件。
 *  单元测试见 Tests/SharedTableWidgetsTests.cpp 的 CH-1、CH-2。
 *
 *  典型用法：
 *      std::vector<ui::ColumnSpec> cols;
 *      cols.push_back(ui::ColumnSpec(_T("词条"), 200));
 *      cols.push_back(ui::ColumnSpec(_T("备注"), 0, true));   //占满剩余宽度
 *      header->SetColumns(cols);
 *      //绘行回调里
 *      std::vector<ui::ColumnBound> bounds;
 *      ui::ColumnHeader::ComputeColumnBounds(cols, rowRect.left, rowRect.right, bounds);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "DuiControl.h"

#include <vector>

namespace flamingoadmin { namespace ui {

// 一列的定义
struct ColumnSpec
{
    CString m_title;  // 列标题
    int     m_width;  // 列宽（像素）；m_fill 为 true 时忽略
    bool    m_fill;   // 是否占满剩余宽度：有多列时平分剩余宽度，除不尽的像素给最后一列
    UINT    m_align;  // 标题与单元格内容的水平对齐：DT_LEFT / DT_CENTER / DT_RIGHT

    ColumnSpec()
        : m_width(0), m_fill(false), m_align(DT_LEFT)
    {
    }

    ColumnSpec(LPCTSTR title, int width, bool fill = false, UINT align = DT_LEFT)
        : m_title(title), m_width(width), m_fill(fill), m_align(align)
    {
    }
};

// 一列的左右边界（宿主客户区坐标），右边界不含
struct ColumnBound
{
    int m_left;   // 左边界
    int m_right;  // 右边界

    ColumnBound()
        : m_left(0), m_right(0)
    {
    }
};

// 表格表头：画列标题与底部分隔线。
class ColumnHeader : public balloonwjui::DuiControl
{
public:
    ColumnHeader();

    // 设置列定义并重绘。
    void SetColumns(const std::vector<ColumnSpec>& columns);

    // 当前的列定义。
    const std::vector<ColumnSpec>& Columns() const { return m_columns; }

    // 设置单元格左右内边距（像素），标题按「列边界向内缩进内边距」后的矩形绘制。默认 14。
    void SetCellPadding(int px);

    // 单元格左右内边距（像素）。
    int CellPadding() const { return m_cellPad; }

    // 设置右侧留出的宽度（像素）并重绘：各列按「控件右边减去该宽度」排列，底部分隔线仍画满整宽。
    // 下方表格出现竖直滚动条时，数据行会让出滚动条的宽度，表头传入同样的值才能与数据行对齐。
    //   px：像素，小于 0 时按 0 处理。默认 0。
    void SetTrailingInset(int px);

    // 右侧留出的宽度（像素）。
    int TrailingInset() const { return m_trailingInset; }

    // 按当前矩形与右侧留出的宽度算出各列的左右边界，与绘制时使用的边界相同。
    //   out：[出参] 与列定义一一对应的边界，调用前会被清空。
    void GetColumnBounds(std::vector<ColumnBound>& out) const;

    // 按列定义算出各列的左右边界。固定列按定义的宽度依次相接；占满列平分 right - left 减去
    // 固定列宽度之和后的剩余宽度，剩余宽度不足时占满列宽度为 0（不出现负值，固定列仍保持原宽度）。
    //   columns：列定义。left / right：整行的左右边界。
    //   out：[出参] 与 columns 一一对应的边界，调用前会被清空。
    static void ComputeColumnBounds(const std::vector<ColumnSpec>& columns, int left, int right,
                                    std::vector<ColumnBound>& out);

    // ---- DuiControl 覆写 ----
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    std::vector<ColumnSpec> m_columns;        // 列定义
    int                     m_cellPad;        // 单元格左右内边距（像素）
    int                     m_trailingInset;  // 右侧留出的宽度（像素），通常等于下方表格滚动条的宽度
};

}} // namespace flamingoadmin::ui
