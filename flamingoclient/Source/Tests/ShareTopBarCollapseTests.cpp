/**
 *  顶部浮动工具条「收起 / 悬停展开」状态的单元测试（Suite SSTB，标签 [sharetopbar]），ShareTopBarCollapseTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareTopBarCollapse（docs/屏幕共享与录制方案.md 第 10 期 10c）：
 *    - SSTB1 默认一直展开、按钮为「收起」；点按钮收起；收起后不点、鼠标不来就一直收着；
 *    - SSTB2 收起后鼠标移到把手上临时展开、按钮为「固定」；鼠标在工具条上时不收；离开满 2 秒收回，不足 2 秒不收；
 *    - SSTB3 临时展开时点「固定」回到一直展开，之后鼠标离开也不收；Reset 回到一直展开；
 *    - SSTB4 小把手在工具条顶边正中，宽高 120×18（balloonui 标题栏的最小高度）；工具条在副屏（坐标为负）时同样居中。
 *  2026-10-07 起（方案第 14.2 节）工具条可以「完全隐藏」，与「收起」并存：
 *    - SSTB5 一直展开或临时展开时点「隐藏」进入隐藏：工具条与把手都不显示；鼠标不到屏幕边缘就一直隐藏；隐藏时「收起」
 *            按钮不可见，点击忽略；收起（只剩把手）时没有「隐藏」按钮，点击忽略；
 *    - SSTB6 隐藏后鼠标到了屏幕边缘的感应区：临时显示，按钮为「固定」；鼠标在工具条上或仍在感应区里时不隐藏，离开满
 *            2 秒再次隐藏；
 *    - SSTB7 从隐藏临时显示时点「固定」回到一直展开；再点「隐藏」又立即隐藏；Reset 回到一直展开；
 *    - SSTB8 屏幕边缘的感应区：工具条离显示器顶边近就在顶边，离底边近就在底边；横向是工具条的范围左右各放宽一段，
 *            不超出显示器；副屏坐标为负时同样成立。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../../FlamingoMedia/Share/ShareTopBarCollapse.h"

using namespace screenshare;

TEST_CASE("SSTB1 默认一直展开，点「收起」后收起", "[sharetopbar]")
{
    TopBarCollapse c;
    CHECK(c.State() == top_bar_pinned);
    CHECK(std::wstring(c.ToggleText()) == L"收起");
    //一直展开时定时器不应改变状态
    CHECK_FALSE(c.OnTick(1000, false, false));
    REQUIRE(c.OnToggleClicked());
    CHECK(c.State() == top_bar_collapsed);
    //收起后鼠标不在把手上：一直收着
    CHECK_FALSE(c.OnTick(5000, false, false));
    CHECK(c.State() == top_bar_collapsed);
    //收起状态下工具条不可见，按钮点击忽略
    CHECK_FALSE(c.OnToggleClicked());
}

TEST_CASE("SSTB2 悬停把手临时展开，离开满 2 秒收回", "[sharetopbar]")
{
    TopBarCollapse c;
    c.OnToggleClicked();
    //鼠标移到把手上：临时展开
    REQUIRE(c.OnTick(10000, true, false));
    CHECK(c.State() == top_bar_peek);
    CHECK(std::wstring(c.ToggleText()) == L"固定");
    //鼠标在工具条上：不收
    CHECK_FALSE(c.OnTick(13000, false, true));
    CHECK(c.State() == top_bar_peek);
    //离开不足 2 秒：不收
    CHECK_FALSE(c.OnTick(13000 + kTopBarPeekHideMs - 1, false, false));
    CHECK(c.State() == top_bar_peek);
    //离开满 2 秒：收回
    REQUIRE(c.OnTick(13000 + kTopBarPeekHideMs, false, false));
    CHECK(c.State() == top_bar_collapsed);
}

TEST_CASE("SSTB3 临时展开时点「固定」回到一直展开；Reset 回到一直展开", "[sharetopbar]")
{
    TopBarCollapse c;
    c.OnToggleClicked();
    c.OnTick(1000, true, false);
    REQUIRE(c.State() == top_bar_peek);
    REQUIRE(c.OnToggleClicked());
    CHECK(c.State() == top_bar_pinned);
    CHECK(std::wstring(c.ToggleText()) == L"收起");
    //固定后鼠标离开也不收
    CHECK_FALSE(c.OnTick(1000 + kTopBarPeekHideMs * 3, false, false));
    CHECK(c.State() == top_bar_pinned);

    c.OnToggleClicked();
    REQUIRE(c.State() == top_bar_collapsed);
    c.Reset();
    CHECK(c.State() == top_bar_pinned);
}

TEST_CASE("SSTB4 小把手在工具条顶边正中", "[sharetopbar]")
{
    //观看端全屏工具条：宽 484（420 + 1 个额外按钮），在主屏顶部
    const RECT bar = { 718, 6, 1202, 68 };
    RECT h = ComputeTopBarHandleRect(bar);
    CHECK(h.right - h.left == kTopBarHandleW);
    CHECK(h.bottom - h.top == kTopBarHandleH);
    CHECK(h.top == bar.top);
    CHECK(h.left == (718 + 1202) / 2 - kTopBarHandleW / 2);

    //副屏在左边，坐标为负
    const RECT left = { -1500, 8, -302, 72 };
    h = ComputeTopBarHandleRect(left);
    CHECK(h.left == (-1500 + -302) / 2 - kTopBarHandleW / 2);
    CHECK(h.top == 8);
}

TEST_CASE("SSTB5 点「隐藏」后工具条与把手都不显示", "[sharetopbar]")
{
    TopBarCollapse c;
    CHECK(TopBarShowsBar(c.State()));
    CHECK_FALSE(TopBarShowsHandle(c.State()));
    REQUIRE(c.OnHideClicked());
    CHECK(c.State() == top_bar_hidden);
    CHECK_FALSE(TopBarShowsBar(c.State()));
    CHECK_FALSE(TopBarShowsHandle(c.State()));
    //鼠标不在感应区：一直隐藏，即使「在把手上」「在工具条上」（此时都不可见，不会为真，这里检查不受影响）
    CHECK_FALSE(c.OnTick(1000, false, false, false));
    CHECK_FALSE(c.OnTick(9000, true, true, false));
    CHECK(c.State() == top_bar_hidden);
    //隐藏时按钮不可见，点击忽略
    CHECK_FALSE(c.OnToggleClicked());
    CHECK_FALSE(c.OnHideClicked());
    CHECK(c.State() == top_bar_hidden);

    //收起（只剩把手）时没有「隐藏」按钮
    TopBarCollapse d;
    d.OnToggleClicked();
    REQUIRE(d.State() == top_bar_collapsed);
    CHECK(TopBarShowsHandle(d.State()));
    CHECK_FALSE(d.OnHideClicked());
    CHECK(d.State() == top_bar_collapsed);
    //收起后临时展开时有「隐藏」按钮：点了进入隐藏
    REQUIRE(d.OnTick(1000, true, false));
    REQUIRE(d.State() == top_bar_peek);
    REQUIRE(d.OnHideClicked());
    CHECK(d.State() == top_bar_hidden);
}

TEST_CASE("SSTB6 隐藏后鼠标到屏幕边缘临时显示，离开满 2 秒再隐藏", "[sharetopbar]")
{
    TopBarCollapse c;
    c.OnHideClicked();
    REQUIRE(c.OnTick(10000, false, false, true));
    CHECK(c.State() == top_bar_hidden_peek);
    CHECK(TopBarShowsBar(c.State()));
    CHECK(std::wstring(c.ToggleText()) == L"固定");
    //鼠标仍在感应区：不隐藏
    CHECK_FALSE(c.OnTick(10000 + kTopBarPeekHideMs * 2, false, false, true));
    //鼠标移到工具条上：不隐藏
    CHECK_FALSE(c.OnTick(20000, false, true, false));
    CHECK(c.State() == top_bar_hidden_peek);
    //离开不足 2 秒：不隐藏
    CHECK_FALSE(c.OnTick(20000 + kTopBarPeekHideMs - 1, false, false, false));
    //离开满 2 秒：再次隐藏（回到隐藏，不是收起成把手）
    REQUIRE(c.OnTick(20000 + kTopBarPeekHideMs, false, false, false));
    CHECK(c.State() == top_bar_hidden);
    //三个参数的老写法等于不在感应区
    CHECK_FALSE(c.OnTick(30000, false, false));
    CHECK(c.State() == top_bar_hidden);
}

TEST_CASE("SSTB7 隐藏后临时显示时点「固定」回到一直展开", "[sharetopbar]")
{
    TopBarCollapse c;
    c.OnHideClicked();
    c.OnTick(1000, false, false, true);
    REQUIRE(c.State() == top_bar_hidden_peek);
    REQUIRE(c.OnToggleClicked());
    CHECK(c.State() == top_bar_pinned);
    CHECK_FALSE(c.OnTick(1000 + kTopBarPeekHideMs * 3, false, false, false));
    CHECK(c.State() == top_bar_pinned);
    //临时显示时点「隐藏」：立即隐藏
    c.OnHideClicked();
    c.OnTick(5000, false, false, true);
    REQUIRE(c.State() == top_bar_hidden_peek);
    REQUIRE(c.OnHideClicked());
    CHECK(c.State() == top_bar_hidden);
    c.Reset();
    CHECK(c.State() == top_bar_pinned);
}

TEST_CASE("SSTB8 屏幕边缘的感应区", "[sharetopbar]")
{
    const RECT monitor = { 0, 0, 1920, 1080 };
    //工具条在顶部：感应区贴着显示器顶边，横向左右各放宽 kHiddenEdgeSlackPx
    const RECT top = { 600, 8, 1300, 70 };
    RECT z = ComputeHiddenEdgeZone(top, monitor);
    CHECK(z.top == 0);
    CHECK(z.bottom == kHiddenEdgeZonePx);
    CHECK(z.left == 600 - kHiddenEdgeSlackPx);
    CHECK(z.right == 1300 + kHiddenEdgeSlackPx);
    POINT pt = { 950, 0 };
    CHECK(::PtInRect(&z, pt));
    //工具条在底部：感应区贴着显示器底边
    const RECT bottom = { 500, 960, 1400, 1010 };
    z = ComputeHiddenEdgeZone(bottom, monitor);
    CHECK(z.bottom == 1080);
    CHECK(z.top == 1080 - kHiddenEdgeZonePx);
    pt.x = 950;
    pt.y = 1079;
    CHECK(::PtInRect(&z, pt));
    //工具条贴近显示器左右边：放宽的部分不超出显示器
    const RECT wide = { 20, 8, 1900, 70 };
    z = ComputeHiddenEdgeZone(wide, monitor);
    CHECK(z.left == 0);
    CHECK(z.right == 1920);
    //副屏在左边，坐标为负
    const RECT leftMon = { -1920, 0, 0, 1080 };
    const RECT leftBar = { -1500, 8, -302, 72 };
    z = ComputeHiddenEdgeZone(leftBar, leftMon);
    CHECK(z.top == 0);
    CHECK(z.left == -1500 - kHiddenEdgeSlackPx);
    CHECK(z.right == -302 + kHiddenEdgeSlackPx);
}
