/**
 *  BadgeRenderTests.cpp
 *
 *  会话列表未读数字红点（BadgeRender）的纯函数测试：验证
 *    · FormatBadgeText —— 0 / 负数不显示、1..99 原样、>99 显示 "99+"；
 *    · ComputeBadgeWidth —— 单字符为正圆、多字符按位数横向加宽；
 *    · ComputeBadgeRect —— 红点矩形右下角对齐、留出右/底内距、不越行界。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不绘制。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/BadgeRender.h"

using namespace balloonmain;

namespace {

RECT MakeRect(long l, long t, long r, long b)
{
    RECT rc;
    rc.left   = l;
    rc.top    = t;
    rc.right  = r;
    rc.bottom = b;
    return rc;
}

} // anonymous namespace

// 会话行红点的画法常量（与 MainPanelTreeView.cpp 取同口径，便于核对几何）。
static const int kDiameter  = 16;    // 红点直径
static const int kExtraChar = 7;     // 每多一位数字胶囊加宽

TEST_CASE("BADGE1 文案：0 与负数不显示", "[badge]")
{
    CHECK(FormatBadgeText(0).IsEmpty());
    CHECK(FormatBadgeText(-3).IsEmpty());
}

TEST_CASE("BADGE2 文案：1..99 原样显示", "[badge]")
{
    CHECK(FormatBadgeText(1)  == _T("1"));
    CHECK(FormatBadgeText(7)  == _T("7"));
    CHECK(FormatBadgeText(42) == _T("42"));
    CHECK(FormatBadgeText(99) == _T("99"));
}

TEST_CASE("BADGE3 文案：超过 99 显示 99+", "[badge]")
{
    CHECK(FormatBadgeText(100)  == _T("99+"));
    CHECK(FormatBadgeText(1234) == _T("99+"));
}

TEST_CASE("BADGE4 宽度：单字符为正圆（宽=直径）", "[badge]")
{
    // "7" 一个字符 → 宽 = 直径。
    CHECK(ComputeBadgeWidth(1, kDiameter, kExtraChar) == 16);
    // 异常的 0 字符也当作不加宽，宽 = 直径（防御）。
    CHECK(ComputeBadgeWidth(0, kDiameter, kExtraChar) == 16);
}

TEST_CASE("BADGE5 宽度：多字符按位数横向加宽", "[badge]")
{
    // "42" 两字符 → 16 + 1*7 = 23。
    CHECK(ComputeBadgeWidth(2, kDiameter, kExtraChar) == 23);
    // "99+" 三字符 → 16 + 2*7 = 30。
    CHECK(ComputeBadgeWidth(3, kDiameter, kExtraChar) == 30);
}

TEST_CASE("BADGE6 矩形：单字符红点按右边缘 + 垂直中心定位", "[badge]")
{
    // 右边缘 192；圆直径 16；垂直中心 centerY=30（取预览行中心）。
    RECT rc = ComputeBadgeRect(192, 16, kDiameter, 30);
    CHECK(rc.right  == 192);
    CHECK(rc.left   == 176);   // 192 - 16
    CHECK(rc.top    == 22);    // 30 - 8
    CHECK(rc.bottom == 38);    // 22 + 16
}

TEST_CASE("BADGE7 矩形：胶囊红点右边缘对齐、向左加宽", "[badge]")
{
    // "99+" 宽 30：右边缘仍在 192，左边缘向左扩到 162；高度 / 垂直中心不变。
    RECT rc = ComputeBadgeRect(192, 30, kDiameter, 30);
    CHECK(rc.right  == 192);
    CHECK(rc.left   == 162);   // 192 - 30
    CHECK(rc.top    == 22);
    CHECK(rc.bottom == 38);
}

TEST_CASE("BADGE8 矩形：中心随预览行平移（偏移行）", "[badge]")
{
    // 第 3 行预览中心 centerY=126：右边缘 192，验证 top/bottom 跟随中心平移。
    RECT rc = ComputeBadgeRect(192, 16, kDiameter, 126);
    CHECK(rc.right  == 192);
    CHECK(rc.left   == 176);
    CHECK(rc.top    == 118);   // 126 - 8
    CHECK(rc.bottom == 134);   // 118 + 16
}

// =====================================================================
// PIN1~PIN6 —— 置顶行左侧强调竖条的几何
// =====================================================================

// 会话行的实际尺寸口径（与 FillConversationsTree 一致）：行高 48px；
// 竖条宽 3px、高占行高一半（与 MainPanelTreeView.cpp 的常量同口径）。
static const int kRowHeightPx    = 48;
static const int kAccentWidthPx  = 3;
static const int kAccentPercent  = 50;

TEST_CASE("PIN1 竖条贴着行左边缘，宽度即给定值", "[badge][pinned]")
{
    RECT row = MakeRect(0, 0, 320, kRowHeightPx);
    RECT bar = ComputePinnedAccentRect(row, kAccentWidthPx, kAccentPercent);

    CHECK(bar.left  == row.left);
    CHECK(bar.right == row.left + kAccentWidthPx);
}

TEST_CASE("PIN2 竖条在行内垂直居中，高度按百分比取", "[badge][pinned]")
{
    RECT row = MakeRect(0, 0, 320, kRowHeightPx);
    RECT bar = ComputePinnedAccentRect(row, kAccentWidthPx, kAccentPercent);

    // 48 的一半 = 24；上下各留 (48-24)/2 = 12。
    CHECK(bar.bottom - bar.top == 24);
    CHECK(bar.top    == 12);
    CHECK(bar.bottom == 36);
    // 上下留白相等即为居中。
    CHECK(bar.top - row.top == row.bottom - bar.bottom);
}

TEST_CASE("PIN3 竖条跟随行矩形平移（第 N 行）", "[badge][pinned]")
{
    // 第 3 行：top = 2*48 = 96，bottom = 144。
    RECT row = MakeRect(0, 96, 320, 144);
    RECT bar = ComputePinnedAccentRect(row, kAccentWidthPx, kAccentPercent);

    CHECK(bar.top    == 108);   // 96 + 12
    CHECK(bar.bottom == 132);   // 108 + 24
}

TEST_CASE("PIN4 竖条完整落在行内，不会溢到相邻行", "[badge][pinned]")
{
    RECT row = MakeRect(0, 96, 320, 144);

    // 百分比取 1 / 50 / 100 / 越界值，竖条都必须夹在行矩形内。
    const int percents[] = { -20, 0, 1, 50, 100, 200 };
    for (size_t i = 0; i < sizeof(percents) / sizeof(percents[0]); ++i)
    {
        INFO("heightPercent = " << percents[i]);
        RECT bar = ComputePinnedAccentRect(row, kAccentWidthPx, percents[i]);
        CHECK(bar.top    >= row.top);
        CHECK(bar.bottom <= row.bottom);
        CHECK(bar.left   >= row.left);
        CHECK(bar.right  <= row.right);
    }
}

TEST_CASE("PIN5 百分比越界按边界截取", "[badge][pinned]")
{
    RECT row = MakeRect(0, 0, 320, kRowHeightPx);

    // 超过 100 按 100 算：与行等高、上下无留白。
    RECT full = ComputePinnedAccentRect(row, kAccentWidthPx, 200);
    CHECK(full.top    == row.top);
    CHECK(full.bottom == row.bottom);

    // 0 与负数按 1% 算：48*1/100 = 0，再兜底抬到 1px，不至于算成看不见的零高矩形。
    RECT thin = ComputePinnedAccentRect(row, kAccentWidthPx, 0);
    CHECK(thin.bottom - thin.top == 1);
}

TEST_CASE("PIN6 宽度非正或行高非正时给出空矩形", "[badge][pinned]")
{
    RECT row = MakeRect(0, 0, 320, kRowHeightPx);

    // 宽度 <= 0：调用方据 right <= left 判空、不画。
    RECT noWidth = ComputePinnedAccentRect(row, 0, kAccentPercent);
    CHECK(noWidth.right <= noWidth.left);

    // 行高 <= 0（异常行矩形）：同样返回空矩形，不做除法也不返回负高。
    RECT emptyRow = MakeRect(0, 100, 320, 100);
    RECT noHeight = ComputePinnedAccentRect(emptyRow, kAccentWidthPx, kAccentPercent);
    CHECK(noHeight.bottom <= noHeight.top);
}
