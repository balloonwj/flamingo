/**
 *  flamingoAdmin —— 只读的「名称：取值」网格实现，说明见 KeyValueGrid.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Widgets/KeyValueGrid.h"
#include "Theme/AdminTheme.h"

#include "Controls/Basic/DuiLabel.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin { namespace ui {

namespace {

using Hint = DuiLayout::Hint;

const int kDefaultCaptionWidth = 72;   // 名称部分的默认宽度（像素），够放 5 个汉字
const int kDefaultLineHeight   = 26;   // 默认单行高度（像素）
const int kCellGap             = 16;   // 同一行相邻两个单元之间的间距（像素）

// 取值为空时显示的文字
const TCHAR kEmptyValue[] = _T("-");

} // namespace

KeyValueGrid::KeyValueGrid()
    : m_columns(1)
    , m_captionWidth(kDefaultCaptionWidth)
    , m_lineHeight(kDefaultLineHeight)
    , m_currentRow(nullptr)
    , m_currentUsed(0)
    , m_contentHeight(0)
{
}

void KeyValueGrid::SetColumnCount(int count)
{
    m_columns = (count < 1) ? 1 : count;
}

void KeyValueGrid::SetCaptionWidth(int px)
{
    m_captionWidth = (px < 0) ? 0 : px;
}

void KeyValueGrid::SetLineHeight(int px)
{
    m_lineHeight = (px < 1) ? 1 : px;
}

DuiHBox* KeyValueGrid::StartRow(int lines)
{
    auto row = std::make_unique<DuiHBox>();
    row->SetGap(kCellGap);
    DuiHBox* raw = row.get();
    const int height = m_lineHeight * lines;
    AddChild(std::move(row), Hint().Fixed(height));
    m_contentHeight += height;
    m_currentRow = raw;
    m_currentUsed = 0;
    return raw;
}

DuiLabel* KeyValueGrid::AddCell(DuiHBox* row, const CString& caption, int weight, bool wrap)
{
    auto cell = std::make_unique<DuiHBox>();

    auto name = std::make_unique<DuiLabel>();
    name->SetText(caption);
    name->SetTextColor(theme::kInk400);
    name->SetTextPointSize(theme::kFontSizeNormal);
    //折行的条目名称与取值的第一行顶端对齐，单行条目垂直居中
    name->SetTextAlign(DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | (wrap ? DT_TOP : DT_VCENTER));
    cell->AddChild(std::move(name), Hint().Fixed(m_captionWidth));

    auto value = std::make_unique<DuiLabel>();
    value->SetText(kEmptyValue);
    value->SetTextColor(theme::kInk700);
    value->SetTextPointSize(theme::kFontSizeNormal);
    if (wrap)
    {
        //DT_EDITCONTROL 让没有空格的长路径也能在字符之间折行
        value->SetTextAlign(DT_LEFT | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS);
        value->SetWordWrap(true);
    }
    else
    {
        value->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    DuiLabel* raw = value.get();
    cell->AddChild(std::move(value), Hint().Weight(1));

    row->AddChild(std::move(cell), Hint().Weight(weight));
    return raw;
}

int KeyValueGrid::AddItem(const CString& caption)
{
    if (m_currentRow == nullptr || m_currentUsed >= m_columns)
        StartRow(1);
    m_values.push_back(AddCell(m_currentRow, caption, 1, false));
    ++m_currentUsed;
    return static_cast<int>(m_values.size()) - 1;
}

int KeyValueGrid::AddWideItem(const CString& caption, int lines)
{
    EndRow();
    DuiHBox* row = StartRow(lines < 1 ? 1 : lines);
    m_values.push_back(AddCell(row, caption, m_columns, lines > 1));
    //独占一整行：下一个条目从新的一行开始
    m_currentUsed = m_columns;
    return static_cast<int>(m_values.size()) - 1;
}

void KeyValueGrid::EndRow()
{
    if (m_currentRow != nullptr && m_currentUsed < m_columns)
    {
        //剩下的每一列各用一块空白占住（间距的个数也与满行相同），各列宽度才与上面的满行一致
        for (int i = m_currentUsed; i < m_columns; ++i)
            m_currentRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
    }
    m_currentRow = nullptr;
    m_currentUsed = 0;
}

void KeyValueGrid::SetValue(int index, const CString& value)
{
    if (index < 0 || index >= ItemCount())
        return;
    m_values[static_cast<size_t>(index)]->SetText(value.IsEmpty() ? CString(kEmptyValue) : value);
}

CString KeyValueGrid::GetValue(int index) const
{
    if (index < 0 || index >= ItemCount())
        return CString();
    return m_values[static_cast<size_t>(index)]->GetText();
}

RECT KeyValueGrid::ValueRect(int index) const
{
    if (index < 0 || index >= ItemCount())
    {
        RECT empty = { 0, 0, 0, 0 };
        return empty;
    }
    return m_values[static_cast<size_t>(index)]->GetRect();
}

void KeyValueGrid::ClearValues()
{
    for (size_t i = 0; i < m_values.size(); ++i)
        m_values[i]->SetText(kEmptyValue);
}

}} // namespace flamingoadmin::ui
