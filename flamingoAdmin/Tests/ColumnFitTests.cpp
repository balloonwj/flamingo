/**
 *  flamingoAdmin —— 表格列宽自适应的单元测试（COL1）。
 *
 *  企业列表的列宽原先是固定值，合计 1064 像素，窗口不够宽时「操作」列的三个点会落到表格外，
 *  审核、停用、删除都打不开。2026-10-04 起列宽改由 Util/ColumnFit.h 的 FitColumnWidths 按可用
 *  宽度分配。本文件覆盖：
 *    - COL1：宽度充足时各列保持设计宽度；不足时只压缩有权重的列、按权重分摊且合计正好等于
 *            可用宽度；极窄时可压缩的列都压到最小宽度、不再继续压；不压缩的列始终不变；
 *            非法输入（空列表、可用宽度为负）不出错。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/ColumnFit.h"

#include <vector>

namespace flamingoadmin { namespace test {

namespace {

// 构造一列的规格。
//   width：设计宽度；minWidth：最小宽度；weight：压缩权重，0 表示不压缩。
ColumnSpec MakeCol(int width, int minWidth, int weight)
{
    ColumnSpec c;
    c.m_width    = width;
    c.m_minWidth = minWidth;
    c.m_weight   = weight;
    return c;
}

// 各列宽度之和。
int SumOf(const std::vector<int>& widths)
{
    int sum = 0;
    for (size_t i = 0; i < widths.size(); ++i)
    {
        sum += widths[i];
    }
    return sum;
}

// COL1：列宽分配。
void TestFitColumnWidths()
{
    BeginCase("COL1 列宽按可用宽度分配：充足时不变，不足时按权重压缩且不低于最小宽度");

    // 三列：可压缩（权重 2）、不压缩、可压缩（权重 1），设计宽度合计 600。
    std::vector<ColumnSpec> cols;
    cols.push_back(MakeCol(300, 150, 2));
    cols.push_back(MakeCol(100, 100, 0));
    cols.push_back(MakeCol(200, 120, 1));

    // 宽度充足（含正好相等）：各列保持设计宽度。
    std::vector<int> w = FitColumnWidths(cols, 800);
    FA_EXPECT_EQ(w.size(), 3);
    FA_EXPECT_EQ(w[0], 300);
    FA_EXPECT_EQ(w[1], 100);
    FA_EXPECT_EQ(w[2], 200);
    w = FitColumnWidths(cols, 600);
    FA_EXPECT_EQ(SumOf(w), 600);
    FA_EXPECT_EQ(w[0], 300);

    // 差 90：按权重 2 : 1 分摊，第一列少 60、第三列少 30，合计正好等于可用宽度。
    w = FitColumnWidths(cols, 510);
    FA_EXPECT_EQ(w[0], 240);
    FA_EXPECT_EQ(w[1], 100);
    FA_EXPECT_EQ(w[2], 170);
    FA_EXPECT_EQ(SumOf(w), 510);

    // 差 200：按 2 : 1 向下取整分得 133 与 66，剩下的 1 像素按列的顺序给第一列。
    w = FitColumnWidths(cols, 400);
    FA_EXPECT_EQ(w[0], 166);
    FA_EXPECT_EQ(w[1], 100);
    FA_EXPECT_EQ(w[2], 134);
    FA_EXPECT_EQ(SumOf(w), 400);

    // 差 7：分得 4 与 2，零头 1 像素给第一列，合计仍正好等于可用宽度。
    w = FitColumnWidths(cols, 593);
    FA_EXPECT_EQ(w[0], 295);
    FA_EXPECT_EQ(w[2], 198);
    FA_EXPECT_EQ(SumOf(w), 593);

    // 某列先压到最小宽度：第三列最小 190，只能让出 10；它没分掉的部分下一轮由第一列承担。
    // 差 150：第一轮分得 100 与 50（第三列只让出 10），第二轮第一列再让出 40。
    std::vector<ColumnSpec> capped;
    capped.push_back(MakeCol(300, 150, 2));
    capped.push_back(MakeCol(100, 100, 0));
    capped.push_back(MakeCol(200, 190, 1));
    w = FitColumnWidths(capped, 450);
    FA_EXPECT_EQ(w[0], 160);
    FA_EXPECT_EQ(w[1], 100);
    FA_EXPECT_EQ(w[2], 190);
    FA_EXPECT_EQ(SumOf(w), 450);

    // 极窄：可压缩的列都停在最小宽度，不压缩的列不变，合计大于可用宽度（由调用方裁剪）。
    w = FitColumnWidths(cols, 200);
    FA_EXPECT_EQ(w[0], 150);
    FA_EXPECT_EQ(w[1], 100);
    FA_EXPECT_EQ(w[2], 120);

    // 非法输入：空列表返回空；可用宽度为负按极窄处理。
    FA_EXPECT_EQ(FitColumnWidths(std::vector<ColumnSpec>(), 500).size(), 0);
    w = FitColumnWidths(cols, -10);
    FA_EXPECT_EQ(w[0], 150);
    FA_EXPECT_EQ(w[2], 120);
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllColumnFitTests()
{
    TestFitColumnWidths();
}

}} // namespace flamingoadmin::test
