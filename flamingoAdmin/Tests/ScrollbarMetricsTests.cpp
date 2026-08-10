/**
 *  flamingoAdmin —— 自绘悬浮滚动条几何计算（ScrollbarMetrics）单元测试。
 *
 *  覆盖 5 组用例（与计划确认一致）：是否需要滚动条、滚动偏移夹取、滑块
 *  矩形（不溢出 / 贴顶 / 贴底）、滑块高度比例与下限、拖动反算滚动偏移。
 *  全部为纯函数测试，不依赖 balloonui 与任何 UI 运行期状态。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/ScrollbarMetrics.h"

namespace flamingoadmin { namespace test {

namespace {

// 构造一个轨道矩形；left / right 不影响纵向几何，仅用 top / bottom。
//   top / bottom：轨道纵向区间。
RECT MakeTrack(int top, int bottom)
{
    RECT r;
    r.left = 100;
    r.top = top;
    r.right = 105;
    r.bottom = bottom;
    return r;
}

// 浮点近似相等断言（测试框架无浮点宏，用绝对误差 0.001）。
//   actual / expected：实际值 / 期望值。
//   file / line：断言所在源码位置。
void ExpectFloatNear(float actual, float expected, const char* file, int line)
{
    float d = actual - expected;
    if (d < 0.0f)
    {
        d = -d;
    }
    CString detail;
    detail.Format(_T("期望≈%.4f，实际 %.4f"), expected, actual);
    flamingoadmin::test::Report(d < 0.001f, "FadeAlphaAt", detail, file, line);
}

} // namespace

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

    // ---- 组 3：滑块矩形（不溢出返回空矩形）----
    BeginCase("ThumbRect：内容不溢出返回空矩形");
    {
        RECT track = MakeTrack(0, 200);
        RECT t = ThumbRect(track, 100, 200, 0, 24);
        FA_EXPECT_EQ(t.bottom - t.top, 0);   // 空矩形，高度为 0
    }

    BeginCase("ThumbRect：scroll=0 贴轨道顶，scroll=max 贴轨道底");
    {
        RECT track = MakeTrack(0, 200);      // trackH = 200
        // content=400, view=200 → maxScroll = 200
        RECT top = ThumbRect(track, 400, 200, 0, 24);
        FA_EXPECT_EQ(top.top, 0);            // 滑块贴轨道顶
        RECT bot = ThumbRect(track, 400, 200, 200, 24);
        FA_EXPECT_EQ(bot.bottom, 200);       // 滑块贴轨道底
    }

    // ---- 组 4：滑块高度比例与下限 ----
    BeginCase("ThumbRect：滑块高度与 view/content 成比例");
    {
        RECT track = MakeTrack(0, 200);
        // content=400, view=200 → thumbH = 200 * 200 / 400 = 100
        RECT t = ThumbRect(track, 400, 200, 0, 24);
        FA_EXPECT_EQ(t.bottom - t.top, 100);
    }

    BeginCase("ThumbRect：滑块不小于最小高度");
    {
        RECT track = MakeTrack(0, 200);
        // content=4000, view=200 → 比例高仅 10px，应被抬到 minThumb=24
        RECT t = ThumbRect(track, 4000, 200, 0, 24);
        FA_EXPECT_EQ(t.bottom - t.top, 24);
    }

    // ---- 组 5：拖动反算滚动偏移 ----
    BeginCase("ScrollOffsetFromThumbTop：滑块顶/底 ↔ 偏移 0/max");
    {
        RECT track = MakeTrack(0, 200);
        // content=400, view=200, thumbH=100 → travel=100, maxScroll=200
        FA_EXPECT_EQ(ScrollOffsetFromThumbTop(track, 100, 0,   400, 200), 0);
        FA_EXPECT_EQ(ScrollOffsetFromThumbTop(track, 100, 100, 400, 200), 200);
        FA_EXPECT_EQ(ScrollOffsetFromThumbTop(track, 100, 50,  400, 200), 100);
        // 拖过头：thumbTop 远超轨道底 → 夹到 max
        FA_EXPECT_EQ(ScrollOffsetFromThumbTop(track, 100, 999, 400, 200), 200);
    }

    // ---- 组 6：淡入淡出时间线 → 不透明度（fadeInFrac=0.2, holdEndFrac=0.8）----
    BeginCase("FadeAlphaAt：t=0 返回起始 alpha");
    ExpectFloatNear(FadeAlphaAt(0.0, 0.0f, 0.2, 0.8), 0.0f, __FILE__, __LINE__);

    BeginCase("FadeAlphaAt：淡入段末到达 1.0");
    ExpectFloatNear(FadeAlphaAt(0.2, 0.0f, 0.2, 0.8), 1.0f, __FILE__, __LINE__);

    BeginCase("FadeAlphaAt：驻留段恒为 1.0");
    ExpectFloatNear(FadeAlphaAt(0.5, 0.0f, 0.2, 0.8), 1.0f, __FILE__, __LINE__);

    BeginCase("FadeAlphaAt：淡出段末（t=1）为 0.0");
    ExpectFloatNear(FadeAlphaAt(1.0, 0.0f, 0.2, 0.8), 0.0f, __FILE__, __LINE__);

    BeginCase("FadeAlphaAt：起始 alpha=1 时淡入段恒为 1.0（连续滚动不闪）");
    ExpectFloatNear(FadeAlphaAt(0.1, 1.0f, 0.2, 0.8), 1.0f, __FILE__, __LINE__);
}

}} // namespace flamingoadmin::test
