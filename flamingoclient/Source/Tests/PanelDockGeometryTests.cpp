/**
 *  主面板贴边自动隐藏的几何计算单元测试（PDG1～PDG9），PanelDockGeometryTests.cpp
 *
 *  覆盖：上 / 左 / 右三条边的贴边判定与吸附后的矩形、超出阈值不贴边、角落里优先贴上边、
 *  边外紧挨着另一台显示器时不贴、任务栏所在一侧不贴、收起后在屏幕内恰好留出细条，
 *  以及鼠标是否碰到细条的判定。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <vector>
#include "../UI/BalloonMain/PanelDockGeometry.h"

using namespace balloonmain;

namespace {

//主面板在 100% 缩放下的大小（与 BalloonMainPanel.cpp 的默认客户区同量级），物理像素
const int kPanelW = 320;
const int kPanelH = 680;

//吸附阈值与细条宽度，物理像素（100% 缩放下与逻辑像素相同）
const int kSnap = kDockSnapLogicalPx;
const int kStrip = kDockStripLogicalPx;

RECT MakeRect(int left, int top, int width, int height)
{
    RECT rc = { left, top, left + width, top + height };
    return rc;
}

DockMonitor MakeMonitor(const RECT& monitor, const RECT& work)
{
    DockMonitor m;
    m.m_monitor = monitor;
    m.m_work = work;
    return m;
}

//主屏 1920x1080，任务栏在底部占 40 像素
DockMonitor PrimaryMonitor()
{
    return MakeMonitor(MakeRect(0, 0, 1920, 1080), MakeRect(0, 0, 1920, 1040));
}

std::vector<DockMonitor> SingleMonitor()
{
    std::vector<DockMonitor> v;
    v.push_back(PrimaryMonitor());
    return v;
}

bool SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

} // namespace

TEST_CASE("PDG1 上边缘在阈值内时贴上边，吸附后顶边对齐工作区、尺寸不变", "[paneldock]")
{
    const RECT wnd = MakeRect(800, 6, kPanelW, kPanelH);
    const DockSnapResult r = DetectDockEdge(wnd, SingleMonitor(), kSnap);
    REQUIRE(r.m_edge == kPanelDockTop);
    CHECK(SameRect(r.m_snapped, MakeRect(800, 0, kPanelW, kPanelH)));
    CHECK(SameRect(r.m_work, PrimaryMonitor().m_work));
}

TEST_CASE("PDG2 左边缘在阈值内或越过边缘时贴左边", "[paneldock]")
{
    SECTION("离左边缘 5 像素")
    {
        const RECT wnd = MakeRect(5, 200, kPanelW, kPanelH);
        const DockSnapResult r = DetectDockEdge(wnd, SingleMonitor(), kSnap);
        REQUIRE(r.m_edge == kPanelDockLeft);
        CHECK(SameRect(r.m_snapped, MakeRect(0, 200, kPanelW, kPanelH)));
    }

    SECTION("有 50 像素拖出了屏幕左边")
    {
        const RECT wnd = MakeRect(-50, 200, kPanelW, kPanelH);
        const DockSnapResult r = DetectDockEdge(wnd, SingleMonitor(), kSnap);
        REQUIRE(r.m_edge == kPanelDockLeft);
        CHECK(SameRect(r.m_snapped, MakeRect(0, 200, kPanelW, kPanelH)));
    }

    SECTION("底部超出工作区时竖直方向平移回工作区内")
    {
        const RECT wnd = MakeRect(3, 500, kPanelW, kPanelH);
        const DockSnapResult r = DetectDockEdge(wnd, SingleMonitor(), kSnap);
        REQUIRE(r.m_edge == kPanelDockLeft);
        CHECK(SameRect(r.m_snapped, MakeRect(0, 1040 - kPanelH, kPanelW, kPanelH)));
    }
}

TEST_CASE("PDG3 右边缘在阈值内时贴右边，吸附后右边对齐工作区", "[paneldock]")
{
    const RECT wnd = MakeRect(1920 - kPanelW - 7, 150, kPanelW, kPanelH);
    const DockSnapResult r = DetectDockEdge(wnd, SingleMonitor(), kSnap);
    REQUIRE(r.m_edge == kPanelDockRight);
    CHECK(SameRect(r.m_snapped, MakeRect(1920 - kPanelW, 150, kPanelW, kPanelH)));
}

TEST_CASE("PDG4 离边缘超过阈值时不贴边，恰好等于阈值时贴边", "[paneldock]")
{
    CHECK(DetectDockEdge(MakeRect(800, kSnap + 1, kPanelW, kPanelH), SingleMonitor(), kSnap).m_edge
          == kPanelDockNone);
    CHECK(DetectDockEdge(MakeRect(kSnap + 1, 200, kPanelW, kPanelH), SingleMonitor(), kSnap).m_edge
          == kPanelDockNone);
    CHECK(DetectDockEdge(MakeRect(1920 - kPanelW - kSnap - 1, 200, kPanelW, kPanelH), SingleMonitor(), kSnap).m_edge
          == kPanelDockNone);
    CHECK(DetectDockEdge(MakeRect(800, kSnap, kPanelW, kPanelH), SingleMonitor(), kSnap).m_edge
          == kPanelDockTop);
}

TEST_CASE("PDG5 上边与左边同时在阈值内时贴上边", "[paneldock]")
{
    const RECT wnd = MakeRect(4, 3, kPanelW, kPanelH);
    const DockSnapResult r = DetectDockEdge(wnd, SingleMonitor(), kSnap);
    REQUIRE(r.m_edge == kPanelDockTop);
    //贴上边时水平方向保持原位（已在工作区内）
    CHECK(SameRect(r.m_snapped, MakeRect(4, 0, kPanelW, kPanelH)));
}

TEST_CASE("PDG6 边外紧挨着另一台显示器时那条边不能贴", "[paneldock]")
{
    SECTION("副屏在主屏右边，靠在两屏交界处不贴右边")
    {
        std::vector<DockMonitor> mons = SingleMonitor();
        mons.push_back(MakeMonitor(MakeRect(1920, 0, 1920, 1080), MakeRect(1920, 0, 1920, 1040)));
        const RECT wnd = MakeRect(1920 - kPanelW - 3, 200, kPanelW, kPanelH);
        CHECK(DetectDockEdge(wnd, mons, kSnap).m_edge == kPanelDockNone);
    }

    SECTION("副屏在主屏左边，靠在两屏交界处不贴左边")
    {
        std::vector<DockMonitor> mons = SingleMonitor();
        mons.push_back(MakeMonitor(MakeRect(-1920, 0, 1920, 1080), MakeRect(-1920, 0, 1920, 1040)));
        const RECT wnd = MakeRect(3, 200, kPanelW, kPanelH);
        CHECK(DetectDockEdge(wnd, mons, kSnap).m_edge == kPanelDockNone);
    }

    SECTION("副屏在主屏上方，靠在两屏交界处不贴上边")
    {
        std::vector<DockMonitor> mons = SingleMonitor();
        mons.push_back(MakeMonitor(MakeRect(0, -1080, 1920, 1080), MakeRect(0, -1080, 1920, 1080)));
        const RECT wnd = MakeRect(800, 3, kPanelW, kPanelH);
        CHECK(DetectDockEdge(wnd, mons, kSnap).m_edge == kPanelDockNone);
    }

    SECTION("副屏在主屏右边时，主屏的上边与左边照常可贴")
    {
        std::vector<DockMonitor> mons = SingleMonitor();
        mons.push_back(MakeMonitor(MakeRect(1920, 0, 1920, 1080), MakeRect(1920, 0, 1920, 1040)));
        CHECK(DetectDockEdge(MakeRect(800, 3, kPanelW, kPanelH), mons, kSnap).m_edge == kPanelDockTop);
        CHECK(DetectDockEdge(MakeRect(3, 200, kPanelW, kPanelH), mons, kSnap).m_edge == kPanelDockLeft);
    }
}

TEST_CASE("PDG7 任务栏所在的一侧不能贴", "[paneldock]")
{
    SECTION("任务栏在上边")
    {
        std::vector<DockMonitor> mons;
        mons.push_back(MakeMonitor(MakeRect(0, 0, 1920, 1080), MakeRect(0, 40, 1920, 1040)));
        CHECK(DetectDockEdge(MakeRect(800, 42, kPanelW, kPanelH), mons, kSnap).m_edge == kPanelDockNone);
    }

    SECTION("任务栏在左边")
    {
        std::vector<DockMonitor> mons;
        mons.push_back(MakeMonitor(MakeRect(0, 0, 1920, 1080), MakeRect(60, 0, 1860, 1080)));
        CHECK(DetectDockEdge(MakeRect(62, 200, kPanelW, kPanelH), mons, kSnap).m_edge == kPanelDockNone);
    }

    SECTION("任务栏在右边")
    {
        std::vector<DockMonitor> mons;
        mons.push_back(MakeMonitor(MakeRect(0, 0, 1920, 1080), MakeRect(0, 0, 1860, 1080)));
        CHECK(DetectDockEdge(MakeRect(1860 - kPanelW - 2, 200, kPanelW, kPanelH), mons, kSnap).m_edge
              == kPanelDockNone);
    }
}

TEST_CASE("PDG8 收起后屏幕内恰好留出细条宽度，尺寸不变", "[paneldock]")
{
    const RECT work = PrimaryMonitor().m_work;
    const RECT mon = PrimaryMonitor().m_monitor;

    SECTION("贴上边")
    {
        const RECT snapped = MakeRect(800, 0, kPanelW, kPanelH);
        const RECT hidden = ComputeDockHiddenRect(snapped, kPanelDockTop, work, kStrip);
        CHECK(SameRect(hidden, MakeRect(800, kStrip - kPanelH, kPanelW, kPanelH)));
        RECT visible;
        REQUIRE(::IntersectRect(&visible, &hidden, &mon));
        CHECK(visible.bottom - visible.top == kStrip);
        CHECK(visible.right - visible.left == kPanelW);
    }

    SECTION("贴左边")
    {
        const RECT snapped = MakeRect(0, 200, kPanelW, kPanelH);
        const RECT hidden = ComputeDockHiddenRect(snapped, kPanelDockLeft, work, kStrip);
        CHECK(SameRect(hidden, MakeRect(kStrip - kPanelW, 200, kPanelW, kPanelH)));
        RECT visible;
        REQUIRE(::IntersectRect(&visible, &hidden, &mon));
        CHECK(visible.right - visible.left == kStrip);
        CHECK(visible.bottom - visible.top == kPanelH);
    }

    SECTION("贴右边")
    {
        const RECT snapped = MakeRect(1920 - kPanelW, 200, kPanelW, kPanelH);
        const RECT hidden = ComputeDockHiddenRect(snapped, kPanelDockRight, work, kStrip);
        CHECK(SameRect(hidden, MakeRect(1920 - kStrip, 200, kPanelW, kPanelH)));
        RECT visible;
        REQUIRE(::IntersectRect(&visible, &hidden, &mon));
        CHECK(visible.right - visible.left == kStrip);
    }

    SECTION("未贴边时原样返回")
    {
        const RECT snapped = MakeRect(500, 200, kPanelW, kPanelH);
        CHECK(SameRect(ComputeDockHiddenRect(snapped, kPanelDockNone, work, kStrip), snapped));
    }
}

TEST_CASE("PDG9 鼠标在细条上时命中，细条外 1 像素时不命中", "[paneldock]")
{
    const RECT work = PrimaryMonitor().m_work;

    SECTION("贴上边")
    {
        const RECT snapped = MakeRect(800, 0, kPanelW, kPanelH);
        const RECT strip = ComputeDockStripRect(snapped, kPanelDockTop, work, kStrip);
        const POINT onStrip = { 900, 0 };
        const POINT belowStrip = { 900, kStrip };
        const POINT besideStrip = { 800 + kPanelW, 0 };
        CHECK(::PtInRect(&strip, onStrip));
        CHECK_FALSE(::PtInRect(&strip, belowStrip));
        CHECK_FALSE(::PtInRect(&strip, besideStrip));
    }

    SECTION("贴左边")
    {
        const RECT snapped = MakeRect(0, 200, kPanelW, kPanelH);
        const RECT strip = ComputeDockStripRect(snapped, kPanelDockLeft, work, kStrip);
        const POINT onStrip = { kStrip - 1, 300 };
        const POINT rightOfStrip = { kStrip, 300 };
        const POINT aboveStrip = { 0, 199 };
        CHECK(::PtInRect(&strip, onStrip));
        CHECK_FALSE(::PtInRect(&strip, rightOfStrip));
        CHECK_FALSE(::PtInRect(&strip, aboveStrip));
    }

    SECTION("贴右边")
    {
        const RECT snapped = MakeRect(1920 - kPanelW, 200, kPanelW, kPanelH);
        const RECT strip = ComputeDockStripRect(snapped, kPanelDockRight, work, kStrip);
        const POINT onStrip = { 1919, 300 };
        const POINT leftOfStrip = { 1920 - kStrip - 1, 300 };
        CHECK(::PtInRect(&strip, onStrip));
        CHECK_FALSE(::PtInRect(&strip, leftOfStrip));
    }

    SECTION("未贴边时没有细条")
    {
        const RECT strip = ComputeDockStripRect(MakeRect(500, 200, kPanelW, kPanelH), kPanelDockNone, work, kStrip);
        CHECK(::IsRectEmpty(&strip));
    }
}
