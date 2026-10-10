/**
 *  flamingoAdmin —— 自绘悬浮滚动条几何计算（ScrollbarMetrics）单元测试。
 *
 *  覆盖 2 组用例：是否需要滚动条、滚动偏移夹取。全部为纯函数测试，不依赖
 *  balloonui 与任何 UI 运行期状态。滑块矩形、拖动反算与淡入淡出时间线自
 *  2026-10-04 起由 balloonui DuiScrollBar 计算，相应用例在 balloonui 的
 *  DuiScrollBarTests 里（原先这里的组 3～6 随被测函数一并删除）。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/ScrollbarMetrics.h"

namespace flamingoadmin { namespace test {

// 依次运行 ScrollbarMetrics 的全部单元测试用例。
void RunAllScrollbarMetricsTests()
{
    // ---- 组 1：是否需要滚动条 ----
    BeginCase("ScrollbarNeeded：内容能否放下");
    FA_EXPECT_FALSE(ScrollbarNeeded(100, 100));   // 恰好放下
    FA_EXPECT_FALSE(ScrollbarNeeded(80, 100));    // 内容更短
    FA_EXPECT_TRUE(ScrollbarNeeded(101, 100));    // 溢出 1px

    // ---- 组 2：滚动偏移夹取 ----
    BeginCase("ClampScrollOffset：夹到 [0, max]");
    FA_EXPECT_EQ(ClampScrollOffset(-30, 300, 100), 0);    // 负数夹到 0
    FA_EXPECT_EQ(ClampScrollOffset(500, 300, 100), 200);  // 超界夹到 max=200
    FA_EXPECT_EQ(ClampScrollOffset(120, 300, 100), 120);  // 区间内不变
    FA_EXPECT_EQ(ClampScrollOffset(50, 100, 100), 0);     // 不溢出 → max=0
}

}} // namespace flamingoadmin::test
