/**
 *  flamingoAdmin —— 组织树工具条几何计算（Util/OrgToolbarLayout）单元测试。
 *
 *  覆盖 5 组用例（与计划确认一致）：三个按钮整组右对齐且顺序正确、按钮互不
 *  重叠且竖向居中、搜索框正确让位、无按钮时搜索框独占整条、工具条过窄时搜索
 *  框宽度夹成 0 且按钮个数越界被夹取。
 *  全部为纯函数测试，不依赖 balloonui 与任何 UI 运行期状态。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/OrgToolbarLayout.h"

namespace flamingoadmin { namespace test {

namespace {

// 构造一个工具条矩形。
//   left / top / right / bottom：工具条四边（host 客户区坐标）。
RECT MakeToolbar(int left, int top, int right, int bottom)
{
    RECT r;
    r.left   = left;
    r.top    = top;
    r.right  = right;
    r.bottom = bottom;
    return r;
}

// 构造企业详情屏当前实际采用的摆位参数（与 EnterpriseDetailScreen.cpp 里
// 那几个常量保持一致），只把按钮个数留给调用方指定。
//   buttonCount：图标按钮个数。
OrgToolbarSpec MakeSpec(int buttonCount)
{
    OrgToolbarSpec spec;
    spec.m_buttonCount = buttonCount;
    spec.m_buttonSize  = 28;
    spec.m_buttonGap   = 4;
    spec.m_sidePad     = 12;
    spec.m_searchGap   = 8;
    spec.m_searchH     = 28;
    return spec;
}

// ---- 组 1：三个按钮整组右对齐，下标 0 在最左 ----
void Test_ButtonsRightAligned()
{
    BeginCase("OrgToolbarLayout：三个按钮整组右对齐");

    // 树列宽 340、工具条高 44，与企业详情屏实际取值一致。
    const RECT toolbar = MakeToolbar(0, 100, 340, 144);
    const OrgToolbarMetrics m = LayoutOrgToolbar(toolbar, MakeSpec(3));

    FA_EXPECT_EQ(m.m_buttonCount, 3);

    // 最右侧按钮贴着工具条右内边距。
    FA_EXPECT_EQ(m.m_rcButtons[2].right, 340 - 12);
    FA_EXPECT_EQ(m.m_rcButtons[2].left,  340 - 12 - 28);

    // 往左每退一个按钮，退 按钮宽 + 间距 = 32。
    FA_EXPECT_EQ(m.m_rcButtons[1].right, 340 - 12 - 32);
    FA_EXPECT_EQ(m.m_rcButtons[0].right, 340 - 12 - 64);

    // 下标 0 是屏幕上最左边那个（刷新按钮）。
    FA_EXPECT_TRUE(m.m_rcButtons[0].left < m.m_rcButtons[1].left);
    FA_EXPECT_TRUE(m.m_rcButtons[1].left < m.m_rcButtons[2].left);
}

// ---- 组 2：按钮互不重叠、尺寸正确、竖向居中 ----
void Test_ButtonsGeometry()
{
    BeginCase("OrgToolbarLayout：按钮尺寸 / 间距 / 竖向居中");

    const RECT toolbar = MakeToolbar(0, 100, 340, 144);
    const OrgToolbarMetrics m = LayoutOrgToolbar(toolbar, MakeSpec(3));

    for (int i = 0; i < m.m_buttonCount; ++i)
    {
        // 正方形，边长 = m_buttonSize。
        FA_EXPECT_EQ(m.m_rcButtons[i].right - m.m_rcButtons[i].left, 28);
        FA_EXPECT_EQ(m.m_rcButtons[i].bottom - m.m_rcButtons[i].top, 28);

        // 在工具条内竖向居中：中线 (100+144)/2 = 122。
        FA_EXPECT_EQ(m.m_rcButtons[i].top, 122 - 14);
        FA_EXPECT_EQ(m.m_rcButtons[i].bottom, 122 + 14);
    }

    // 相邻按钮之间正好隔 m_buttonGap，不重叠也不粘连。
    FA_EXPECT_EQ(m.m_rcButtons[1].left - m.m_rcButtons[0].right, 4);
    FA_EXPECT_EQ(m.m_rcButtons[2].left - m.m_rcButtons[1].right, 4);
}

// ---- 组 3：搜索框正确让位给按钮组 ----
void Test_SearchRectGivesWay()
{
    BeginCase("OrgToolbarLayout：搜索框让位给按钮组");

    const RECT toolbar = MakeToolbar(0, 100, 340, 144);
    const OrgToolbarMetrics m = LayoutOrgToolbar(toolbar, MakeSpec(3));

    // 左边界留内边距；右边界停在最左侧按钮之前 m_searchGap 处。
    FA_EXPECT_EQ(m.m_rcSearch.left, 12);
    FA_EXPECT_EQ(m.m_rcSearch.right, m.m_rcButtons[0].left - 8);

    // 高度 28、竖向居中。
    FA_EXPECT_EQ(m.m_rcSearch.bottom - m.m_rcSearch.top, 28);
    FA_EXPECT_EQ(m.m_rcSearch.top, 122 - 14);

    // 搜索框与最左侧按钮不重叠。
    FA_EXPECT_TRUE(m.m_rcSearch.right <= m.m_rcButtons[0].left);
}

// ---- 组 4：没有按钮时搜索框独占整条工具条 ----
void Test_NoButtons()
{
    BeginCase("OrgToolbarLayout：无按钮时搜索框独占");

    const RECT toolbar = MakeToolbar(0, 100, 340, 144);

    const OrgToolbarMetrics m0 = LayoutOrgToolbar(toolbar, MakeSpec(0));
    FA_EXPECT_EQ(m0.m_buttonCount, 0);
    FA_EXPECT_EQ(m0.m_rcSearch.left, 12);
    FA_EXPECT_EQ(m0.m_rcSearch.right, 340 - 12);

    // 负数按钮个数按 0 处理，不越界写数组。
    const OrgToolbarMetrics mNeg = LayoutOrgToolbar(toolbar, MakeSpec(-3));
    FA_EXPECT_EQ(mNeg.m_buttonCount, 0);
    FA_EXPECT_EQ(mNeg.m_rcSearch.right, 340 - 12);
}

// ---- 组 5：边界情形 —— 工具条过窄、按钮个数越界 ----
void Test_DegenerateCases()
{
    BeginCase("OrgToolbarLayout：过窄工具条与越界按钮数");

    // 宽 100 放不下「搜索框 + 三个按钮」：搜索框右边界会算到左边界左侧，
    // 须被夹成 0 宽，绝不能出现负宽度矩形。
    const RECT narrow = MakeToolbar(0, 0, 100, 44);
    const OrgToolbarMetrics mn = LayoutOrgToolbar(narrow, MakeSpec(3));
    FA_EXPECT_EQ(mn.m_rcSearch.left, 12);
    FA_EXPECT_EQ(mn.m_rcSearch.right, 12);
    FA_EXPECT_TRUE(mn.m_rcSearch.right >= mn.m_rcSearch.left);

    // 按钮个数超过上限时夹到 kMaxOrgToolbarButtons，不越界写数组。
    const RECT toolbar = MakeToolbar(0, 100, 340, 144);
    const OrgToolbarMetrics mo =
        LayoutOrgToolbar(toolbar, MakeSpec(kMaxOrgToolbarButtons + 5));
    FA_EXPECT_EQ(mo.m_buttonCount, kMaxOrgToolbarButtons);
    FA_EXPECT_EQ(mo.m_rcButtons[kMaxOrgToolbarButtons - 1].right, 340 - 12);
}

} // namespace

// 依次运行 OrgToolbarLayout 的全部单元测试用例。
void RunAllOrgToolbarLayoutTests()
{
    Test_ButtonsRightAligned();
    Test_ButtonsGeometry();
    Test_SearchRectGivesWay();
    Test_NoButtons();
    Test_DegenerateCases();
}

}} // namespace flamingoadmin::test
