/**
 *  ScreenshotGeometryTests.cpp
 *
 *  第一期（单聊截图）测试：覆盖截图浮层的纯几何运算 —— 选区归一化 / 夹取 /
 *  最小尺寸判定、尺寸读数、操作工具栏落点。对应方案用例 P1~P5。浮层本身的
 *  冻结遮罩 / 拖拽 / 工具栏渲染等只能人工核对界面，不在本文件覆盖范围内。
 *  2026-10-05 加上 P6~P10：多显示器查找、全屏选区的工具栏落点、尺寸读数与框选
 *  提示条的落点（docs/截图快捷键与隐藏当前窗口方案.md 第 4 节）。
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../../FlamingoScreenshot/Capture/ScreenshotGeometry.h"

using namespace balloonscreenshot;

namespace {

POINT MakePoint(int x, int y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

RECT MakeRect(int l, int t, int r, int b)
{
    RECT rc;
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    return rc;
}

SIZE MakeSize(int cx, int cy)
{
    SIZE s;
    s.cx = cx;
    s.cy = cy;
    return s;
}

} // anonymous namespace

// =====================================================================
// P1. 选区归一化
// =====================================================================

TEST_CASE("P1 正向拖拽：矩形原样归一", "[screenshot][geom]")
{
    RECT r = NormalizeDragRect(MakePoint(100, 80), MakePoint(300, 240));
    CHECK(r.left == 100);
    CHECK(r.top == 80);
    CHECK(r.right == 300);
    CHECK(r.bottom == 240);
}

TEST_CASE("P1 反向拖拽（终点在起点左上）：归一为正规矩形", "[screenshot][geom]")
{
    RECT r = NormalizeDragRect(MakePoint(300, 240), MakePoint(100, 80));
    CHECK(r.left == 100);
    CHECK(r.top == 80);
    CHECK(r.right == 300);
    CHECK(r.bottom == 240);
}

TEST_CASE("P1 零尺寸拖拽：起止同点 → 空矩形", "[screenshot][geom]")
{
    RECT r = NormalizeDragRect(MakePoint(150, 150), MakePoint(150, 150));
    CHECK(r.left == r.right);
    CHECK(r.top == r.bottom);
}

// =====================================================================
// P2. 选区夹取到边界
// =====================================================================

TEST_CASE("P2 选区超出边界 → 各边夹回界内", "[screenshot][geom]")
{
    RECT bounds = MakeRect(0, 0, 1920, 1080);
    RECT r = ClampRectToBounds(MakeRect(-50, -30, 2000, 1200), bounds);
    CHECK(r.left == 0);
    CHECK(r.top == 0);
    CHECK(r.right == 1920);
    CHECK(r.bottom == 1080);
}

TEST_CASE("P2 选区完全在界外 → 退化为贴边零尺寸（不会出现 left>right）", "[screenshot][geom]")
{
    RECT bounds = MakeRect(0, 0, 1920, 1080);
    RECT r = ClampRectToBounds(MakeRect(3000, 3000, 3200, 3200), bounds);
    CHECK(r.left <= r.right);
    CHECK(r.top <= r.bottom);
    CHECK(r.right == 1920);
    CHECK(r.bottom == 1080);
}

TEST_CASE("P2 多显示器负坐标边界：左屏在主屏左侧", "[screenshot][geom]")
{
    // 副屏在主屏左边：虚拟桌面 left 为负。选区夹进该边界应保留负坐标侧。
    RECT bounds = MakeRect(-1920, 0, 1920, 1080);
    RECT r = ClampRectToBounds(MakeRect(-2500, 100, -100, 500), bounds);
    CHECK(r.left == -1920);
    CHECK(r.top == 100);
    CHECK(r.right == -100);
    CHECK(r.bottom == 500);
}

// =====================================================================
// P3. 最小尺寸闸
// =====================================================================

TEST_CASE("P3 足够大的选区可用", "[screenshot][geom]")
{
    CHECK(IsSelectionUsable(MakeRect(0, 0, 100, 60), 4, 4));
}

TEST_CASE("P3 过小的选区不可用（任一维小于阈值即否）", "[screenshot][geom]")
{
    CHECK_FALSE(IsSelectionUsable(MakeRect(0, 0, 3, 100), 4, 4));   // 宽不足
    CHECK_FALSE(IsSelectionUsable(MakeRect(0, 0, 100, 2), 4, 4));   // 高不足
    CHECK_FALSE(IsSelectionUsable(MakeRect(0, 0, 0, 0), 4, 4));     // 零尺寸
}

// =====================================================================
// P4. 尺寸读数文本
// =====================================================================

TEST_CASE("P4 尺寸读数形如 \"w × h\"", "[screenshot][geom]")
{
    CHECK(FormatSizeReadout(1035, 564) == CString(_T("1035 × 564")));
    CHECK(FormatSizeReadout(1, 1) == CString(_T("1 × 1")));
}

// =====================================================================
// P5. 操作工具栏落点
// =====================================================================

TEST_CASE("P5 常态：工具栏贴选区下方、右对齐选区右边", "[screenshot][geom]")
{
    RECT work = MakeRect(0, 0, 1920, 1080);
    RECT sel = MakeRect(400, 300, 800, 600);
    SIZE bar = MakeSize(360, 44);
    const int gap = 8;
    POINT pt = PlaceActionBar(sel, bar, work, gap);
    CHECK(pt.y == sel.bottom + gap);          // 下方
    CHECK(pt.x == sel.right - bar.cx);        // 右对齐选区右边
}

TEST_CASE("P5 选区贴近屏幕底：工具栏翻到选区上方", "[screenshot][geom]")
{
    RECT work = MakeRect(0, 0, 1920, 1080);
    RECT sel = MakeRect(400, 900, 800, 1070);   // 下方放不下 44+8
    SIZE bar = MakeSize(360, 44);
    const int gap = 8;
    POINT pt = PlaceActionBar(sel, bar, work, gap);
    CHECK(pt.y == sel.top - gap - bar.cy);      // 翻到上方
}

TEST_CASE("P5 选区贴近左边：右对齐落点越界 → 夹到工作区左边缘", "[screenshot][geom]")
{
    RECT work = MakeRect(0, 0, 1920, 1080);
    RECT sel = MakeRect(10, 300, 120, 500);     // 右对齐 x = 120-360 = -240 < 0
    SIZE bar = MakeSize(360, 44);
    const int gap = 8;
    POINT pt = PlaceActionBar(sel, bar, work, gap);
    CHECK(pt.x == work.left);                   // 夹到左边缘
}

// =====================================================================
// P6 ~ P10. 多显示器与全屏选区（截图快捷键方案，2026-10-05）
// =====================================================================

namespace {

// 两块显示器：左边主屏 1920×1080，右边副屏 1280×1024 且顶边低 100 像素，
// 两屏之间的下方与上方都有不属于任何显示器的空白区
std::vector<RECT> TwoMonitors()
{
    std::vector<RECT> v;
    v.push_back(MakeRect(0, 0, 1920, 1080));
    v.push_back(MakeRect(1920, 100, 3200, 1124));
    return v;
}

} // anonymous namespace

TEST_CASE("P6 按点找显示器：点在某块屏内取那一块", "[screenshot][geom][monitor]")
{
    std::vector<RECT> mons = TwoMonitors();
    CHECK(FindMonitorIndex(mons, MakePoint(100, 100)) == 0);
    CHECK(FindMonitorIndex(mons, MakePoint(2500, 600)) == 1);
}

TEST_CASE("P6 按点找显示器：点落在各屏之间的空白区取最近的一块", "[screenshot][geom][monitor]")
{
    std::vector<RECT> mons = TwoMonitors();
    // 副屏上方的空白区（x 在副屏范围内、y 小于副屏顶边），离副屏 50、离主屏 580
    CHECK(FindMonitorIndex(mons, MakePoint(2500, 50)) == 1);
    // 主屏下方的空白区（y 超过主屏底边 20），离主屏 20、离副屏 x 方向 120
    CHECK(FindMonitorIndex(mons, MakePoint(1800, 1100)) == 0);
}

TEST_CASE("P6 按点找显示器：没有显示器返回 -1", "[screenshot][geom][monitor]")
{
    std::vector<RECT> none;
    CHECK(FindMonitorIndex(none, MakePoint(0, 0)) == -1);
}

TEST_CASE("P7 按矩形找显示器：取相交面积最大的一块", "[screenshot][geom][monitor]")
{
    std::vector<RECT> mons = TwoMonitors();
    // 跨两屏，主屏里占 100 列、副屏里占 300 列
    RECT sel = MakeRect(1820, 300, 2220, 500);
    RECT m = MonitorRectForRect(mons, sel, MakeRect(0, 0, 3200, 1124));
    CHECK(m.left == 1920);
    CHECK(m.top == 100);
    CHECK(m.right == 3200);
    CHECK(m.bottom == 1124);
}

TEST_CASE("P7 按矩形找显示器：与哪块都不相交时取离中心最近的一块；没有显示器时用后备矩形",
          "[screenshot][geom][monitor]")
{
    std::vector<RECT> mons = TwoMonitors();
    // 完全落在副屏上方的空白区
    RECT sel = MakeRect(2400, 10, 2600, 60);
    RECT m = MonitorRectForRect(mons, sel, MakeRect(0, 0, 3200, 1124));
    CHECK(m.left == 1920);

    std::vector<RECT> none;
    RECT fb = MakeRect(-5, -6, 7, 8);
    RECT got = MonitorRectForRect(none, sel, fb);
    CHECK(got.left == -5);
    CHECK(got.top == -6);
    CHECK(got.right == 7);
    CHECK(got.bottom == 8);
}

TEST_CASE("P8 全屏选区：工具栏上下都放不下时放进选区内右下角", "[screenshot][geom][fullscreen]")
{
    RECT mon = MakeRect(1920, 100, 3200, 1124);
    RECT sel = mon;                                // 选区就是整块副屏
    SIZE bar = MakeSize(600, 96);
    const int gap = 8;
    POINT pt = PlaceActionBar(sel, bar, mon, gap);
    CHECK(pt.x == sel.right - gap - bar.cx);       // 右边与选区右边留 gap
    CHECK(pt.y == sel.bottom - gap - bar.cy);      // 下边与选区下边留 gap
}

TEST_CASE("P8 选区下方超出所在显示器、上方放得下：仍翻到上方（以所在显示器为准）",
          "[screenshot][geom][fullscreen]")
{
    // 选区贴着副屏底边；副屏下方在虚拟桌面里没有显示器，不能把工具栏放过去
    RECT mon = MakeRect(1920, 100, 3200, 1124);
    RECT sel = MakeRect(2200, 900, 2800, 1120);
    SIZE bar = MakeSize(500, 44);
    const int gap = 8;
    POINT pt = PlaceActionBar(sel, bar, mon, gap);
    CHECK(pt.y == sel.top - gap - bar.cy);
}

TEST_CASE("P9 尺寸读数：常态在选区左上角上方", "[screenshot][geom][readout]")
{
    RECT mon = MakeRect(0, 0, 1920, 1080);
    RECT sel = MakeRect(400, 300, 800, 600);
    SIZE box = MakeSize(80, 20);
    const int gap = 4;
    POINT pt = PlaceSizeReadout(sel, box, mon, gap);
    CHECK(pt.x == sel.left);
    CHECK(pt.y == sel.top - gap - box.cy);
}

TEST_CASE("P9 尺寸读数：选区贴显示器顶边时放进选区内；右边越界时夹回显示器内",
          "[screenshot][geom][readout]")
{
    // 副屏顶边为 100：读数若放在上方会落进不属于任何显示器的空白区
    RECT mon = MakeRect(1920, 100, 3200, 1124);
    RECT sel = mon;
    SIZE box = MakeSize(80, 20);
    const int gap = 4;
    POINT pt = PlaceSizeReadout(sel, box, mon, gap);
    CHECK(pt.x == sel.left);
    CHECK(pt.y == sel.top + gap);

    RECT nearRight = MakeRect(3150, 400, 3200, 500);
    POINT pr = PlaceSizeReadout(nearRight, box, mon, gap);
    CHECK(pr.x == mon.right - box.cx);
}

TEST_CASE("P10 框选提示条：在显示器顶部水平居中", "[screenshot][geom][hint]")
{
    SIZE bar = MakeSize(400, 36);
    const int topGap = 24;
    POINT a = PlaceHintBar(MakeRect(0, 0, 1920, 1080), bar, topGap);
    CHECK(a.x == (1920 - 400) / 2);
    CHECK(a.y == topGap);

    // 原点不为 0 的副屏
    POINT b = PlaceHintBar(MakeRect(1920, 100, 3200, 1124), bar, topGap);
    CHECK(b.x == 1920 + (1280 - 400) / 2);
    CHECK(b.y == 100 + topGap);
}

// =====================================================================
// H. 选区命中区 HitTestSelection
// =====================================================================

TEST_CASE("H1 选区中心 → 内部", "[screenshot][hit]")
{
    RECT sel = MakeRect(100, 100, 300, 200);
    CHECK(HitTestSelection(sel, MakePoint(200, 150), 8) == kSelInside);
}

TEST_CASE("H2 贴近各边 → 对应边", "[screenshot][hit]")
{
    RECT sel = MakeRect(100, 100, 300, 200);
    CHECK(HitTestSelection(sel, MakePoint(101, 150), 8) == kSelLeft);
    CHECK(HitTestSelection(sel, MakePoint(299, 150), 8) == kSelRight);
    CHECK(HitTestSelection(sel, MakePoint(200, 101), 8) == kSelTop);
    CHECK(HitTestSelection(sel, MakePoint(200, 199), 8) == kSelBottom);
}

TEST_CASE("H3 贴近角 → 对应角（角优先于边）", "[screenshot][hit]")
{
    RECT sel = MakeRect(100, 100, 300, 200);
    CHECK(HitTestSelection(sel, MakePoint(100, 100), 8) == kSelTL);
    CHECK(HitTestSelection(sel, MakePoint(300, 100), 8) == kSelTR);
    CHECK(HitTestSelection(sel, MakePoint(100, 200), 8) == kSelBL);
    CHECK(HitTestSelection(sel, MakePoint(300, 200), 8) == kSelBR);
}

TEST_CASE("H4 选区(含容差)之外 → 外部", "[screenshot][hit]")
{
    RECT sel = MakeRect(100, 100, 300, 200);
    CHECK(HitTestSelection(sel, MakePoint(50, 150), 8) == kSelOutside);   // 左边外
    CHECK(HitTestSelection(sel, MakePoint(200, 250), 8) == kSelOutside);  // 下边外
}

// =====================================================================
// M. 选区移动 MoveSelectionClamped
// =====================================================================

TEST_CASE("M1 界内移动 → 整体平移、尺寸不变", "[screenshot][move]")
{
    RECT bounds = MakeRect(0, 0, 1920, 1080);
    RECT r = MoveSelectionClamped(MakeRect(100, 100, 300, 200), 50, 30, bounds);
    CHECK(r.left == 150);
    CHECK(r.top == 130);
    CHECK(r.right == 350);    // 宽 200 保持
    CHECK(r.bottom == 230);   // 高 100 保持
}

TEST_CASE("M2 移动越界 → 贴边、尺寸不变", "[screenshot][move]")
{
    RECT bounds = MakeRect(0, 0, 1920, 1080);
    // 往左上越界。
    RECT r = MoveSelectionClamped(MakeRect(100, 100, 300, 200), -500, -500, bounds);
    CHECK(r.left == 0);
    CHECK(r.top == 0);
    CHECK(r.right == 200);
    CHECK(r.bottom == 100);
}

// =====================================================================
// R. 选区缩放 ResizeSelection
// =====================================================================

TEST_CASE("R1 拖右边 → 只改 right", "[screenshot][resize]")
{
    RECT bounds = MakeRect(0, 0, 1920, 1080);
    RECT r = ResizeSelection(MakeRect(100, 100, 300, 200), kSelRight, 40, 0, bounds, 10, 10);
    CHECK(r.left == 100);
    CHECK(r.top == 100);
    CHECK(r.right == 340);
    CHECK(r.bottom == 200);
}

TEST_CASE("R2 拖左上角 → 改 left+top", "[screenshot][resize]")
{
    RECT bounds = MakeRect(0, 0, 1920, 1080);
    RECT r = ResizeSelection(MakeRect(100, 100, 300, 200), kSelTL, -30, -20, bounds, 10, 10);
    CHECK(r.left == 70);
    CHECK(r.top == 80);
    CHECK(r.right == 300);
    CHECK(r.bottom == 200);
}

TEST_CASE("R3 缩到最小宽以下 → 夹到 minW", "[screenshot][resize]")
{
    RECT bounds = MakeRect(0, 0, 1920, 1080);
    // 把右边往左拖很多，应夹到 left+minW。
    RECT r = ResizeSelection(MakeRect(100, 100, 300, 200), kSelRight, -1000, 0, bounds, 20, 20);
    CHECK(r.right == 120);   // left(100) + minW(20)
}

TEST_CASE("R4 拖右边越界 → 夹到 bounds 右", "[screenshot][resize]")
{
    RECT bounds = MakeRect(0, 0, 320, 240);
    RECT r = ResizeSelection(MakeRect(100, 100, 300, 200), kSelRight, 200, 0, bounds, 10, 10);
    CHECK(r.right == 320);
}
