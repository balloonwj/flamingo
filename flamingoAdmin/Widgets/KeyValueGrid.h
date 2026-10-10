/**
 *  flamingoAdmin —— 只读的「名称：取值」网格（共用控件），用于详情卡片。
 *
 *  若干条目按行优先排成 N 列，每个条目左边是灰色的名称、右边是取值；取值过长时以省略号结尾。
 *  路径、摘要这类长取值可以用 AddWideItem 独占一整行并折成多行显示（长路径中间没有空格也会折行）。
 *  整个控件由 balloonui 的 DuiHBox / DuiLabel 组合而成，不自绘。「文件共享」页与「共享审计」页的
 *  详情卡首先使用（2026-10-03），以后其它详情区也应复用本控件。单元测试见 Tests/FileShareScreensTests.cpp 的 KV 系列。
 *
 *  典型用法：
 *      auto grid = std::make_unique<ui::KeyValueGrid>();
 *      grid->SetColumnCount(2);
 *      const int owner = grid->AddItem(_T("分享者"));
 *      const int conv  = grid->AddItem(_T("所在会话"));
 *      const int path  = grid->AddWideItem(_T("显示路径"), 2);
 *      grid->EndRow();
 *      parent->AddChild(std::move(grid), Hint().Fixed(grid->ContentHeight()));   //先记下高度再 move
 *      //选中某条记录后
 *      grid->SetValue(owner, _T("张三"));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"

#include <vector>

namespace balloonwjui { class DuiLabel; class DuiHBox; }

namespace flamingoadmin { namespace ui {

// 只读的「名称：取值」网格。
class KeyValueGrid : public balloonwjui::DuiVBox
{
public:
    KeyValueGrid();

    // 设置列数，至少为 1。须在添加条目之前调用。
    void SetColumnCount(int count);

    // 设置名称部分的宽度（像素）。须在添加条目之前调用。
    void SetCaptionWidth(int px);

    // 设置单行的高度（像素），多行条目按「行数 × 单行高度」占位。须在添加条目之前调用。
    void SetLineHeight(int px);

    // 追加一个单行条目，接在当前行后面；当前行已满时另起一行。
    //   caption：名称，如「分享者」。
    //   返回：条目下标，SetValue / GetValue 用它指定条目。
    int AddItem(const CString& caption);

    // 追加一个独占一整行的条目，取值按 lines 行折行显示（超出的部分以省略号结尾）。当前行未满时先结束当前行。
    //   caption：名称。lines：行数，小于 1 时按 1 处理。
    //   返回：条目下标。
    int AddWideItem(const CString& caption, int lines);

    // 结束当前行：剩下的列留空，下一个条目从新的一行开始。全部条目添加完后调用一次，让最后一行的各列
    // 与上面各行对齐。
    void EndRow();

    // 设置条目的取值。空串显示为「-」。index 越界时不做任何事。
    void SetValue(int index, const CString& value);

    // 条目当前显示的取值（单元测试用）；index 越界时返回空串。
    CString GetValue(int index) const;

    // 条目取值部分的矩形（宿主客户区坐标，单元测试核对各列对齐用）；index 越界时返回空矩形。
    RECT ValueRect(int index) const;

    // 把全部条目的取值恢复为「-」。
    void ClearValues();

    // 条目个数。
    int ItemCount() const { return static_cast<int>(m_values.size()); }

    // 全部行加起来的高度（像素），宿主据此给本控件分配固定高度。
    int ContentHeight() const { return m_contentHeight; }

private:
    // 新开一行，返回这一行的容器（所有权在本控件的子控件树中）。
    //   lines：这一行占几个单行高度。
    balloonwjui::DuiHBox* StartRow(int lines);

    // 在 row 里追加一个「名称 + 取值」单元，返回取值标签。
    //   weight：单元在行内所占的份数。wrap：取值是否折行显示。
    balloonwjui::DuiLabel* AddCell(balloonwjui::DuiHBox* row, const CString& caption, int weight, bool wrap);

    int                                  m_columns;        // 列数，至少为 1
    int                                  m_captionWidth;   // 名称部分的宽度（像素）
    int                                  m_lineHeight;     // 单行的高度（像素）
    balloonwjui::DuiHBox*                m_currentRow;     // 正在追加条目的那一行，为空表示需要另起一行
    int                                  m_currentUsed;    // 当前行已占用的列数
    int                                  m_contentHeight;  // 已有各行的高度之和（像素）
    std::vector<balloonwjui::DuiLabel*>  m_values;         // 各条目的取值标签，下标即条目下标（所有权在控件树中）
};

}} // namespace flamingoadmin::ui
