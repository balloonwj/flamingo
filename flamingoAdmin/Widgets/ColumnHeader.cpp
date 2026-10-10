/**
 *  flamingoAdmin —— 表格表头实现，说明见 ColumnHeader.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Widgets/ColumnHeader.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"

namespace flamingoadmin { namespace ui {

namespace {

// 默认单元格左右内边距（像素）：与企业管理页表格相同
const int kDefaultCellPad = 14;

} // namespace

ColumnHeader::ColumnHeader()
    : m_cellPad(kDefaultCellPad)
    , m_trailingInset(0)
{
}

void ColumnHeader::SetColumns(const std::vector<ColumnSpec>& columns)
{
    m_columns = columns;
    Invalidate();
}

void ColumnHeader::SetCellPadding(int px)
{
    m_cellPad = (px < 0) ? 0 : px;
    Invalidate();
}

void ColumnHeader::SetTrailingInset(int px)
{
    m_trailingInset = (px < 0) ? 0 : px;
    Invalidate();
}

void ColumnHeader::GetColumnBounds(std::vector<ColumnBound>& out) const
{
    int right = m_rcItem.right - m_trailingInset;
    if (right < m_rcItem.left)
        right = m_rcItem.left;
    ComputeColumnBounds(m_columns, m_rcItem.left, right, out);
}

void ColumnHeader::ComputeColumnBounds(const std::vector<ColumnSpec>& columns, int left, int right,
                                       std::vector<ColumnBound>& out)
{
    out.clear();
    out.resize(columns.size());

    int fixedSum = 0;
    int fillCount = 0;
    int lastFill = -1;
    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (columns[i].m_fill)
        {
            ++fillCount;
            lastFill = static_cast<int>(i);
        }
        else if (columns[i].m_width > 0)
        {
            fixedSum += columns[i].m_width;
        }
    }

    //剩余宽度不足时占满列宽度为 0，固定列仍保持原宽度
    int remaining = (right - left) - fixedSum;
    if (remaining < 0)
        remaining = 0;
    const int perFill = (fillCount > 0) ? remaining / fillCount : 0;
    const int extra = (fillCount > 0) ? remaining - perFill * fillCount : 0;

    int x = left;
    for (size_t i = 0; i < columns.size(); ++i)
    {
        int width = 0;
        if (columns[i].m_fill)
            width = perFill + ((static_cast<int>(i) == lastFill) ? extra : 0);
        else if (columns[i].m_width > 0)
            width = columns[i].m_width;
        out[i].m_left = x;
        out[i].m_right = x + width;
        x += width;
    }
}

void ColumnHeader::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!m_bVisible)
        return;

    std::vector<ColumnBound> bounds;
    GetColumnBounds(bounds);
    HFONT font = UiFont(theme::kFontSizeSmall, false);
    for (size_t i = 0; i < m_columns.size(); ++i)
    {
        const RECT rc = { bounds[i].m_left + m_cellPad, m_rcItem.top, bounds[i].m_right - m_cellPad, m_rcItem.bottom };
        if (rc.right <= rc.left)
            continue;
        DrawTextLine(hdc, m_columns[i].m_title, rc, theme::kInk400, font,
                     m_columns[i].m_align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    //底部分隔线
    FillLine(hdc, m_rcItem.left, m_rcItem.bottom - 1, m_rcItem.right - m_rcItem.left, 1, theme::kInk100);
}

}} // namespace flamingoadmin::ui
