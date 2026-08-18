/**
 *  ScreenshotGeometryTests.cpp
 *
 *  第一期（单聊截图）测试：覆盖截图浮层的纯几何运算 —— 选区归一化 / 夹取 /
 *  最小尺寸判定、尺寸读数、操作工具栏落点。对应方案用例 P1~P5。浮层本身的
 *  冻结遮罩 / 拖拽 / 工具栏渲染等只能目视核对，不在本文件覆盖范围内。
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonScreenshot/ScreenshotGeometry.h"

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
