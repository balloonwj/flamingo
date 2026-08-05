/**
 *  OverlayScrollBarGeometryTests.cpp
 *
 *  主面板 overlay 滚动条几何（OverlayScrollBarGeometry）的测试。覆盖三组：
 *    A. 滑块 / 命中带矩形 —— 内容是否溢出、滑块高度的比例与下限、滚到顶 /
 *       底 / 中间时滑块的位置、给窗口缩放边框让位后滑块与命中带的横向坐标。
 *    B. 拖动反算 —— 由滑块顶边 y 反推滚动位置，含上下界夹取与轨道为 0 的除零防护。
 *    C. 让位宽度 —— 窗口可否缩放、DPI 缩放、DPI 取不到有效值时的兜底。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰 GDI。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/OverlayScrollBarGeometry.h"

using namespace balloonmain;

namespace {

RECT MakeRect(long l, long t, long r, long b)
{
    RECT rc;
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    return rc;
}

} // anonymous namespace

// 典型视口：宽 340（主面板窄高窗），高 200，左上角不在原点（上面还有
// Banner / 搜索框 / Tab 条），用来验证坐标是相对视口而非相对窗口算的。
static const RECT kView = { 0, 100, 340, 300 };

// 主面板实际的缩放边框让位宽度（DuiFrameWindow 默认 8 逻辑像素，100% DPI）。
static const int kInset = 8;

// =======================================================================
// A. 滑块 / 命中带矩形
// =======================================================================

TEST_CASE("OSB1 内容未溢出：不显示滚动条", "[overlayscrollbar]")
{
    // 内容高 == 视口高（200）以及更矮时都不该出滚动条。
    OverlayScrollBarMetrics m = ComputeOverlayScrollBar(kView, 200, 0, kInset);
    CHECK_FALSE(m.m_visible);

    m = ComputeOverlayScrollBar(kView, 50, 0, kInset);
    CHECK_FALSE(m.m_visible);
}

TEST_CASE("OSB2 视口无效：不显示滚动条", "[overlayscrollbar]")
{
    // 高度为 0（尚未布局）。
    OverlayScrollBarMetrics m =
        ComputeOverlayScrollBar(MakeRect(0, 100, 340, 100), 800, 0, kInset);
    CHECK_FALSE(m.m_visible);

    // 高度为负（异常入参）。
    m = ComputeOverlayScrollBar(MakeRect(0, 300, 340, 100), 800, 0, kInset);
    CHECK_FALSE(m.m_visible);

    // 宽度为 0。
    m = ComputeOverlayScrollBar(MakeRect(0, 100, 0, 300), 800, 0, kInset);
    CHECK_FALSE(m.m_visible);
}

TEST_CASE("OSB3 滑块高度 = 视口高的平方 / 内容高", "[overlayscrollbar]")
{
    // 视口 200、内容 800 → 200 * 200 / 800 = 50。
    OverlayScrollBarMetrics m = ComputeOverlayScrollBar(kView, 800, 0, kInset);
    REQUIRE(m.m_visible);
    CHECK((m.m_rcThumb.bottom - m.m_rcThumb.top) == 50);
}

TEST_CASE("OSB4 比例算出的滑块过细时抬到最小高度", "[overlayscrollbar]")
{
    // 视口 100、内容 10000 → 100 * 100 / 10000 = 1，抬到 kOverlayBarMinThumb。
    OverlayScrollBarMetrics m =
        ComputeOverlayScrollBar(MakeRect(0, 0, 340, 100), 10000, 0, kInset);
    REQUIRE(m.m_visible);
    CHECK((m.m_rcThumb.bottom - m.m_rcThumb.top) == kOverlayBarMinThumb);
}

TEST_CASE("OSB5 滚到顶：滑块顶边与视口顶边齐平", "[overlayscrollbar]")
{
    OverlayScrollBarMetrics m = ComputeOverlayScrollBar(kView, 800, 0, kInset);
    REQUIRE(m.m_visible);
    CHECK(m.m_rcThumb.top == kView.top);
}

TEST_CASE("OSB6 滚到底：滑块底边与视口底边齐平", "[overlayscrollbar]")
{
    // 最大滚动位置 = 内容高 - 视口高 = 800 - 200 = 600。
    OverlayScrollBarMetrics m = ComputeOverlayScrollBar(kView, 800, 600, kInset);
    REQUIRE(m.m_visible);
    CHECK(m.m_rcThumb.bottom == kView.bottom);
}

TEST_CASE("OSB7 滚到中间：滑块落在轨道中点", "[overlayscrollbar]")
{
    // 滑块高 50，轨道长 = 200 - 50 = 150；滚到一半（300 / 600）→ 顶边偏移 75。
    OverlayScrollBarMetrics m = ComputeOverlayScrollBar(kView, 800, 300, kInset);
    REQUIRE(m.m_visible);
    CHECK(m.m_rcThumb.top == kView.top + 75);
}

TEST_CASE("OSB8 滑块横向让开窗口缩放边框", "[overlayscrollbar]")
{
    OverlayScrollBarMetrics m = ComputeOverlayScrollBar(kView, 800, 0, kInset);
    REQUIRE(m.m_visible);

    // 右缘 = 视口右缘 - 让位宽 - 间隙；宽度固定。
    CHECK(m.m_rcThumb.right == kView.right - kInset - kOverlayBarMargin);
    CHECK(m.m_rcThumb.left == m.m_rcThumb.right - kOverlayBarWidth);

    // 让位宽为 0（窗口不可缩放）时退化成"贴着视口右缘只留间隙"。
    OverlayScrollBarMetrics m0 = ComputeOverlayScrollBar(kView, 800, 0, 0);
    REQUIRE(m0.m_visible);
    CHECK(m0.m_rcThumb.right == kView.right - kOverlayBarMargin);
}

TEST_CASE("OSB9 命中带覆盖滑块且不侵入缩放边框", "[overlayscrollbar]")
{
    OverlayScrollBarMetrics m = ComputeOverlayScrollBar(kView, 800, 300, kInset);
    REQUIRE(m.m_visible);

    // 右缘顶到"视口右缘 - 让位宽"为止 —— 再往右就是窗口缩放边框的地盘。
    CHECK(m.m_rcHit.right == kView.right - kInset);

    // 左缘 = 滑块左缘再往左扩一段，便于鼠标瞄准。
    CHECK(m.m_rcHit.left == m.m_rcThumb.left - kOverlayBarHitPad);

    // 纵向盖满整个视口（鼠标在轨道任意高度悬停都能唤出滚动条）。
    CHECK(m.m_rcHit.top == kView.top);
    CHECK(m.m_rcHit.bottom == kView.bottom);

    // 滑块整体落在命中带里。
    CHECK(m.m_rcHit.left <= m.m_rcThumb.left);
    CHECK(m.m_rcHit.right >= m.m_rcThumb.right);
    CHECK(m.m_rcHit.top <= m.m_rcThumb.top);
    CHECK(m.m_rcHit.bottom >= m.m_rcThumb.bottom);
}

TEST_CASE("OSB10 滚动位置越界时被夹回范围", "[overlayscrollbar]")
{
    // 负数按 0 处理 —— 滑块贴顶。
    OverlayScrollBarMetrics mNeg = ComputeOverlayScrollBar(kView, 800, -500, kInset);
    REQUIRE(mNeg.m_visible);
    CHECK(mNeg.m_rcThumb.top == kView.top);

    // 超过最大值按最大值处理 —— 滑块贴底，不会跑到视口外面。
    OverlayScrollBarMetrics mBig = ComputeOverlayScrollBar(kView, 800, 99999, kInset);
    REQUIRE(mBig.m_visible);
    CHECK(mBig.m_rcThumb.bottom == kView.bottom);
}

// =======================================================================
// B. 拖动反算
// =======================================================================

TEST_CASE("OSB11 滑块拖到顶：滚动位置为 0", "[overlayscrollbar]")
{
    CHECK(OverlayScrollPosFromThumbTop(kView, 800, 50, kView.top) == 0);
}

TEST_CASE("OSB12 滑块拖到底：滚动位置为最大值", "[overlayscrollbar]")
{
    // 滑块顶边 = 视口底 - 滑块高 = 300 - 50 = 250 → 最大滚动位置 600。
    CHECK(OverlayScrollPosFromThumbTop(kView, 800, 50, kView.bottom - 50) == 600);
}

TEST_CASE("OSB13 滑块拖到中点：滚动位置为最大值的一半", "[overlayscrollbar]")
{
    // 轨道长 150，拖到偏移 75 → 75 * 600 / 150 = 300。
    CHECK(OverlayScrollPosFromThumbTop(kView, 800, 50, kView.top + 75) == 300);
}

TEST_CASE("OSB14 滑块拖出上下界时被夹住", "[overlayscrollbar]")
{
    // 鼠标拖到视口上方很远处。
    CHECK(OverlayScrollPosFromThumbTop(kView, 800, 50, kView.top - 1000) == 0);
    // 鼠标拖到视口下方很远处。
    CHECK(OverlayScrollPosFromThumbTop(kView, 800, 50, kView.bottom + 1000) == 600);
}

TEST_CASE("OSB15 轨道长度为 0 时返回 0 且不除零", "[overlayscrollbar]")
{
    // 滑块与视口等高 —— 无处可拖。
    CHECK(OverlayScrollPosFromThumbTop(kView, 800, 200, kView.top + 30) == 0);
    // 滑块比视口还高（异常入参）同样安全返回。
    CHECK(OverlayScrollPosFromThumbTop(kView, 800, 260, kView.top + 30) == 0);
    // 内容未溢出时也没有可滚区间。
    CHECK(OverlayScrollPosFromThumbTop(kView, 200, 50, kView.top + 30) == 0);
}

// =======================================================================
// C. 让位宽度
// =======================================================================

TEST_CASE("OSB16 窗口不可缩放：不必让位", "[overlayscrollbar]")
{
    CHECK(ComputeResizeBorderInsetPx(8, 96, false) == 0);
    CHECK(ComputeResizeBorderInsetPx(8, 144, false) == 0);
}

TEST_CASE("OSB17 100% DPI：让位宽等于边框宽", "[overlayscrollbar]")
{
    CHECK(ComputeResizeBorderInsetPx(8, 96, true) == 8);
}

TEST_CASE("OSB18 150% DPI：让位宽按比例放大", "[overlayscrollbar]")
{
    // 8 * 144 / 96 = 12。
    CHECK(ComputeResizeBorderInsetPx(8, 144, true) == 12);
    // 125%：8 * 120 / 96 = 10。
    CHECK(ComputeResizeBorderInsetPx(8, 120, true) == 10);
}

TEST_CASE("OSB19 DPI 取不到有效值：按 96 兜底", "[overlayscrollbar]")
{
    CHECK(ComputeResizeBorderInsetPx(8, 0, true) == 8);
    CHECK(ComputeResizeBorderInsetPx(8, -1, true) == 8);

    // 边框宽本身为 0（SetBorderPx(0)，等价关掉边缘 hit-test）时也是 0。
    CHECK(ComputeResizeBorderInsetPx(0, 96, true) == 0);
}
