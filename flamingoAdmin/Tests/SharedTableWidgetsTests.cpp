/**
 *  flamingoAdmin —— 表格类共用控件单元测试（Widgets/PagerBar、Widgets/ActionTable、Widgets/ColumnHeader）。
 *
 *    - PB-1：分页条的页数与可否翻页（含 0 条、每页条数非法、页码与总数非法）；
 *    - PB-2：分页条的点击：只有可用的翻页按钮会触发回调，回调里是目标页码；按钮几何与
 *            原企业管理页表尾一致（外观不变）；
 *    - AT-1：带操作链接的表格的命中测试：链接所在行与操作下标正确，其它列、最后一行下方、
 *            显示覆盖层时都不算点中链接；
 *    - AT-2：在链接上松开鼠标时回调恰好触发一次，行号与操作号正确；
 *    - AT-3：先排版、后给数据时，行数超出可见范围后滚动条落在控件最右侧、宽度不为 0，链接画在
 *            滚动条左侧且点击范围与之一致；行数减少到不需要滚动时宽度回到 0；
 *    - AT-4：悬停提示回调：鼠标移到回调给出文字的位置时登记该文字，移开后取消；显示覆盖提示时不登记；
 *    - CH-1：表头列边界的计算（固定列、占满剩余宽度的列、剩余宽度不足、整体平移）；
 *    - CH-2：表头右侧留出滚动条宽度后，各列按让出后的右边界排列。
 *
 *  控件在没有宿主窗口的情况下直接构造、手工 Layout，不依赖任何窗口。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Widgets/PagerBar.h"
#include "Widgets/ActionTable.h"
#include "Widgets/ColumnHeader.h"
#include "Theme/AdminTheme.h"
#include "Controls/Window/DuiScrollBar.h"
#include "Controls/Feedback/DuiToolTip.h"

#include <vector>

namespace flamingoadmin { namespace test {

namespace {

// 分页条测试用的控件矩形：宽 800、高 42（与企业管理页表尾同高）
const RECT kPagerRect = { 0, 0, 800, 42 };

// 原企业管理页表尾的几何：左右内边距 14、按钮宽 60、按钮与页码文字间距 8、页码文字宽 120
const int kPad = 14;
const int kButtonW = 60;
const int kGap = 8;
const int kPageTextW = 120;

// 表格测试用的控件矩形、行高、行数与操作列宽度
const RECT kTableRect = { 0, 0, 800, 400 };
const int kRowH = 40;
const int kRows = 3;
const int kActionAreaW = 160;
// 超出可见范围的行数：表格高 400、行高 40，最多看到 10 行
const int kManyRows = 30;

// 记录回调被调用的情况
struct CallRecord
{
    int m_calls;    // 被调用的次数
    int m_page;     // 分页回调：最近一次的目标页码
    int m_row;      // 操作回调：最近一次的行下标
    int m_action;   // 操作回调：最近一次的操作下标
};

void OnPage(void* user, int newPage)
{
    CallRecord* rec = static_cast<CallRecord*>(user);
    ++rec->m_calls;
    rec->m_page = newPage;
}

void OnAction(void* user, int rowIndex, int actionIndex)
{
    CallRecord* rec = static_cast<CallRecord*>(user);
    ++rec->m_calls;
    rec->m_row = rowIndex;
    rec->m_action = actionIndex;
}

POINT CenterOf(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

bool IsEmptyRect(const RECT& rc)
{
    return rc.right <= rc.left || rc.bottom <= rc.top;
}

// 造一张 3 行、带「编辑」「删除」两个操作的表格
void SetupTable(ui::ActionTable& table)
{
    table.SetRowHeight(kRowH);
    //父容器排版时调用的是 SetRect（它保存矩形后再调 Layout）；基类的 Layout 本身不保存矩形
    table.SetRect(kTableRect);
    table.SetRowCount(kRows);
    table.SetActionAreaWidth(kActionAreaW);
    table.AddAction(_T("编辑"), theme::kColorInfo);
    table.AddAction(_T("删除"), theme::kColorDanger);
}

} // namespace

void TestPagerBarPages()
{
    BeginCase("PB-1 分页条：页数与可否翻页");

    ui::PagerBar bar;
    bar.SetPageInfo(1, 120, 50);
    FA_EXPECT_EQ(bar.PageCount(), 3);
    FA_EXPECT_FALSE(bar.CanPrev());
    FA_EXPECT_TRUE(bar.CanNext());

    bar.SetPageInfo(3, 120, 50);
    FA_EXPECT_TRUE(bar.CanPrev());
    FA_EXPECT_FALSE(bar.CanNext());

    //没有数据：仍显示 1 页，两个方向都不能翻
    bar.SetPageInfo(1, 0, 50);
    FA_EXPECT_EQ(bar.PageCount(), 1);
    FA_EXPECT_FALSE(bar.CanPrev());
    FA_EXPECT_FALSE(bar.CanNext());

    //每页条数不大于 0：按每页 1 条计算
    bar.SetPageInfo(1, 5, 0);
    FA_EXPECT_EQ(bar.PageCount(), 5);

    //页码小于 1、总数为负：分别按 1 与 0 处理
    bar.SetPageInfo(0, -3, 50);
    FA_EXPECT_EQ(bar.Page(), 1);
    FA_EXPECT_EQ(bar.Total(), 0);
}

void TestPagerBarClicks()
{
    BeginCase("PB-2 分页条：只有可用的翻页按钮触发回调，按钮几何与原企业管理页表尾一致");

    ui::PagerBar bar;
    CallRecord rec = { 0, 0, 0, 0 };
    bar.SetPageCallback(&OnPage, &rec);
    bar.SetRect(kPagerRect);
    bar.SetPageInfo(1, 120, 50);

    //几何：从右往左依次是 内边距、[下一页]、间距、页码文字、间距、[上一页]
    const RECT& next = bar.NextRect();
    const RECT& prev = bar.PrevRect();
    FA_EXPECT_EQ(next.right, kPagerRect.right - kPad);
    FA_EXPECT_EQ(next.right - next.left, kButtonW);
    FA_EXPECT_EQ(prev.right, next.left - kGap - kPageTextW - kGap);
    FA_EXPECT_EQ(prev.right - prev.left, kButtonW);

    //第 1 页点「下一页」：目标页码 2
    bar.OnLButtonUp(CenterOf(next), 0);
    FA_EXPECT_EQ(rec.m_calls, 1);
    FA_EXPECT_EQ(rec.m_page, 2);

    //第 1 页点「上一页」：按钮不可用，不触发
    bar.OnLButtonUp(CenterOf(prev), 0);
    FA_EXPECT_EQ(rec.m_calls, 1);

    //点两个按钮之间的页码文字：不触发
    const POINT between = { (prev.right + next.left) / 2, (next.top + next.bottom) / 2 };
    bar.OnLButtonUp(between, 0);
    FA_EXPECT_EQ(rec.m_calls, 1);

    //最后一页：「下一页」不可用，「上一页」的目标页码是 2
    bar.SetPageInfo(3, 120, 50);
    bar.OnLButtonUp(CenterOf(next), 0);
    FA_EXPECT_EQ(rec.m_calls, 1);
    bar.OnLButtonUp(CenterOf(prev), 0);
    FA_EXPECT_EQ(rec.m_calls, 2);
    FA_EXPECT_EQ(rec.m_page, 2);
}

void TestActionTableHitTest()
{
    BeginCase("AT-1 带操作链接的表格：命中测试");

    ui::ActionTable table;
    SetupTable(table);
    FA_EXPECT_EQ(table.ActionCount(), 2);

    const RECT edit = table.GetLinkRect(1, 0);
    const RECT del = table.GetLinkRect(1, 1);
    FA_EXPECT_FALSE(IsEmptyRect(edit));
    FA_EXPECT_FALSE(IsEmptyRect(del));
    //「编辑」在「删除」左边，两者都在操作列（每行最右侧 160 像素）之内、在第 2 行的范围之内
    FA_EXPECT_TRUE(edit.right <= del.left);
    FA_EXPECT_TRUE(edit.left >= kTableRect.right - kActionAreaW);
    FA_EXPECT_TRUE(del.right <= kTableRect.right);
    FA_EXPECT_TRUE(edit.top >= kRowH && edit.bottom <= 2 * kRowH);

    int row = -9;
    FA_EXPECT_EQ(table.ActionFromPoint(CenterOf(edit), &row), 0);
    FA_EXPECT_EQ(row, 1);
    FA_EXPECT_EQ(table.ActionFromPoint(CenterOf(del), &row), 1);
    FA_EXPECT_EQ(row, 1);

    //操作列以外的位置：在第 2 行上，但不在链接上
    const POINT otherColumn = { 100, kRowH + kRowH / 2 };
    FA_EXPECT_EQ(table.RowFromPoint(otherColumn), 1);
    FA_EXPECT_EQ(table.ActionFromPoint(otherColumn, &row), -1);
    FA_EXPECT_EQ(row, -1);

    //最后一行下方：不在任何行上
    const POINT belowRows = { CenterOf(edit).x, kRows * kRowH + 5 };
    FA_EXPECT_EQ(table.RowFromPoint(belowRows), -1);
    FA_EXPECT_EQ(table.ActionFromPoint(belowRows, &row), -1);

    //下标越界：空矩形
    FA_EXPECT_TRUE(IsEmptyRect(table.GetLinkRect(kRows, 0)));
    FA_EXPECT_TRUE(IsEmptyRect(table.GetLinkRect(1, 2)));

    //显示覆盖层时链接不响应
    table.SetOverlayText(_T("加载中…"));
    FA_EXPECT_STREQ(table.GetOverlayText(), _T("加载中…"));
    FA_EXPECT_EQ(table.ActionFromPoint(CenterOf(edit), &row), -1);
    table.SetOverlayText(CString());
    FA_EXPECT_EQ(table.ActionFromPoint(CenterOf(edit), &row), 0);
}

void TestActionTableClick()
{
    BeginCase("AT-2 带操作链接的表格：在链接上松开鼠标时回调恰好触发一次");

    ui::ActionTable table;
    SetupTable(table);
    CallRecord rec = { 0, 0, -1, -1 };
    table.SetActionCallback(&OnAction, &rec);

    table.OnLButtonUp(CenterOf(table.GetLinkRect(2, 1)), 0);
    FA_EXPECT_EQ(rec.m_calls, 1);
    FA_EXPECT_EQ(rec.m_row, 2);
    FA_EXPECT_EQ(rec.m_action, 1);

    //不在链接上：不触发
    const POINT otherColumn = { 100, kRowH / 2 };
    table.OnLButtonUp(otherColumn, 0);
    FA_EXPECT_EQ(rec.m_calls, 1);
}

void TestActionTableScrollBar()
{
    BeginCase("AT-3 带操作链接的表格：行数超出可见范围后滚动条落在最右侧，链接让出它的宽度");

    //先排版、后给数据：与页面拿到列表结果时的顺序一致
    ui::ActionTable table;
    table.SetRowHeight(kRowH);
    table.SetRect(kTableRect);
    table.SetActionAreaWidth(kActionAreaW);
    table.AddAction(_T("编辑"), theme::kColorInfo);
    table.AddAction(_T("删除"), theme::kColorDanger);
    FA_EXPECT_EQ(table.ScrollBarWidth(), 0);

    table.SetRowCount(kManyRows);
    balloonwjui::DuiScrollBar* sb = table.GetScrollBar();
    FA_EXPECT_NOTNULL(sb);
    if (sb == nullptr)
        return;
    FA_EXPECT_TRUE(sb->IsVisible());
    //滚动条的矩形必须已经排好：是控件最右侧的一条，否则既画不出来也无法拖动
    const RECT rcSb = sb->GetRect();
    FA_EXPECT_FALSE(IsEmptyRect(rcSb));
    FA_EXPECT_EQ(rcSb.right, kTableRect.right);
    FA_EXPECT_EQ(rcSb.top, kTableRect.top);
    FA_EXPECT_EQ(rcSb.bottom, kTableRect.bottom);
    FA_EXPECT_TRUE(table.ScrollBarWidth() > 0);
    FA_EXPECT_EQ(table.ScrollBarWidth(), rcSb.right - rcSb.left);

    //链接画在滚动条左侧，点击范围与之一致
    const RECT del = table.GetLinkRect(0, 1);
    FA_EXPECT_TRUE(del.right <= rcSb.left);
    int row = -9;
    FA_EXPECT_EQ(table.ActionFromPoint(CenterOf(del), &row), 1);
    FA_EXPECT_EQ(row, 0);

    //行数减少到可见范围以内：滚动条隐藏，宽度回到 0
    table.SetRowCount(kRows);
    FA_EXPECT_FALSE(sb->IsVisible());
    FA_EXPECT_EQ(table.ScrollBarWidth(), 0);
}

// AT-4 用的悬停提示回调：x 在 [kTipLeft, kTipRight) 内时给出提示文字
const int kTipLeft = 600;
const int kTipRight = 700;

CString TipAt(void* /*user*/, POINT pt)
{
    if (pt.x >= kTipLeft && pt.x < kTipRight)
        return CString(_T("全文"));
    return CString();
}

void TestActionTableTooltip()
{
    BeginCase("AT-4 带操作链接的表格：悬停提示回调");

    ui::ActionTable table;
    SetupTable(table);
    table.SetTooltipProvider(&TipAt, nullptr);

    const POINT onTip = { (kTipLeft + kTipRight) / 2, kRowH / 2 };
    const POINT offTip = { 100, kRowH / 2 };
    table.OnMouseMove(onTip, 0);
    FA_EXPECT_STREQ(table.CurrentTooltip(), _T("全文"));
    FA_EXPECT_STREQ(balloonwjui::DuiToolTipMgr::Inst().GetText(&table), _T("全文"));

    table.OnMouseMove(offTip, 0);
    FA_EXPECT_STREQ(table.CurrentTooltip(), _T(""));
    FA_EXPECT_STREQ(balloonwjui::DuiToolTipMgr::Inst().GetText(&table), _T(""));

    //显示覆盖提示时不登记
    table.SetOverlayText(_T("加载中…"));
    table.OnMouseMove(onTip, 0);
    FA_EXPECT_STREQ(table.CurrentTooltip(), _T(""));
}

void TestColumnBounds()
{
    BeginCase("CH-1 表头列边界：固定列依次相接，占满列取剩余宽度，不足时为 0");

    //敏感词页的列：词条 200、分类 100、备注（占满）、添加人 120、添加时间 150、操作 140，固定列合计 710
    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("词条"), 200));
    cols.push_back(ui::ColumnSpec(_T("分类"), 100));
    cols.push_back(ui::ColumnSpec(_T("备注"), 0, true));
    cols.push_back(ui::ColumnSpec(_T("添加人"), 120));
    cols.push_back(ui::ColumnSpec(_T("添加时间"), 150));
    cols.push_back(ui::ColumnSpec(_T("操作"), 140));

    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(cols, 0, 900, b);
    FA_EXPECT_EQ(b.size(), 6);
    if (b.size() == 6)
    {
        const int expected[6][2] = { { 0, 200 }, { 200, 300 }, { 300, 490 }, { 490, 610 }, { 610, 760 }, { 760, 900 } };
        for (int i = 0; i < 6; ++i)
        {
            FA_EXPECT_EQ(b[i].m_left, expected[i][0]);
            FA_EXPECT_EQ(b[i].m_right, expected[i][1]);
        }
    }

    //只有 600 像素：剩余宽度为负，占满列宽度为 0，固定列仍保持原宽度（整行超出右边界）
    ui::ColumnHeader::ComputeColumnBounds(cols, 0, 600, b);
    FA_EXPECT_EQ(b.size(), 6);
    if (b.size() == 6)
    {
        FA_EXPECT_EQ(b[2].m_left, 300);
        FA_EXPECT_EQ(b[2].m_right, 300);
        FA_EXPECT_EQ(b[3].m_left, 300);
        FA_EXPECT_EQ(b[5].m_right, 710);
    }

    //左边界不为 0 时整体平移
    ui::ColumnHeader::ComputeColumnBounds(cols, 50, 950, b);
    if (b.size() == 6)
    {
        FA_EXPECT_EQ(b[0].m_left, 50);
        FA_EXPECT_EQ(b[5].m_right, 950);
    }
}

void TestColumnHeaderTrailingInset()
{
    BeginCase("CH-2 表头右侧留出滚动条宽度：各列按让出后的右边界排列");

    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("词条"), 200));
    cols.push_back(ui::ColumnSpec(_T("备注"), 0, true));
    cols.push_back(ui::ColumnSpec(_T("操作"), 140));

    ui::ColumnHeader header;
    header.SetColumns(cols);
    const RECT rcHeader = { 0, 0, 900, 40 };
    header.SetRect(rcHeader);
    FA_EXPECT_EQ(header.TrailingInset(), 0);

    std::vector<ui::ColumnBound> b;
    header.GetColumnBounds(b);
    FA_EXPECT_EQ(b.size(), 3);
    if (b.size() == 3)
        FA_EXPECT_EQ(b[2].m_right, 900);

    //留出 12 像素：与表格按 888 像素宽算出的边界完全相同
    const int kInset = 12;
    header.SetTrailingInset(kInset);
    FA_EXPECT_EQ(header.TrailingInset(), kInset);
    header.GetColumnBounds(b);
    std::vector<ui::ColumnBound> expected;
    ui::ColumnHeader::ComputeColumnBounds(cols, rcHeader.left, rcHeader.right - kInset, expected);
    FA_EXPECT_EQ(b.size(), expected.size());
    if (b.size() == 3 && expected.size() == 3)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            FA_EXPECT_EQ(b[i].m_left, expected[i].m_left);
            FA_EXPECT_EQ(b[i].m_right, expected[i].m_right);
        }
        FA_EXPECT_EQ(b[1].m_right, 900 - kInset - 140);
        FA_EXPECT_EQ(b[2].m_right, 900 - kInset);
    }

    //负数按 0 处理
    header.SetTrailingInset(-5);
    FA_EXPECT_EQ(header.TrailingInset(), 0);
}

void RunAllSharedTableWidgetsTests()
{
    TestPagerBarPages();
    TestPagerBarClicks();
    TestActionTableHitTest();
    TestActionTableClick();
    TestActionTableScrollBar();
    TestActionTableTooltip();
    TestColumnBounds();
    TestColumnHeaderTrailingInset();
}

}} // namespace flamingoadmin::test
