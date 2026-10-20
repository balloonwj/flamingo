/**
 *  观看端鼠标按钮的记录与「要不要发送」（标签 [mousebuttons]），ViewerMouseButtonsTests.cpp
 *  背景见 FlamingoMedia/Share/ViewerMouseButtons.h 与 docs/远程桌面控制权限与观看工具条方案.md 8.2 第 1 条、8.4。
 *  与用户确认的用例：
 *    - MBT1 画面外按下不记、不发；画面内按下后画面外的移动照发（由调用方夹到边缘）；松开后画面外的移动不发；
 *    - MBT2 没记过的按钮在画面外松开不发、在画面内松开照发；双击按按下处理；
 *    - MBT3 复位后忘掉按下的按钮。
 *  框架 Catch2 v3，纯逻辑。
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Share/ViewerMouseButtons.h"

using namespace screenshare;

TEST_CASE("MBT1 press outside is ignored, a drag started inside keeps sending outside until released", "[mousebuttons]")
{
    ViewerMouseButtons b;
    //画面外按下：不发、不记；之后画面外的移动也不发
    CHECK_FALSE(b.Decide(WM_LBUTTONDOWN, false));
    CHECK(b.ButtonsDown() == 0);
    CHECK_FALSE(b.Decide(WM_MOUSEMOVE, false));
    //画面内的移动照发
    CHECK(b.Decide(WM_MOUSEMOVE, true));
    //画面内按下：发、记下；拖到画面外的移动照发
    CHECK(b.Decide(WM_LBUTTONDOWN, true));
    CHECK(b.ButtonsDown() == kViewerButtonLeft);
    CHECK(b.Decide(WM_MOUSEMOVE, false));
    //画面外松开：照发，按钮清掉；之后画面外的移动不发
    CHECK(b.Decide(WM_LBUTTONUP, false));
    CHECK(b.ButtonsDown() == 0);
    CHECK_FALSE(b.Decide(WM_MOUSEMOVE, false));
    //滚轮只在画面内发，按住按钮也一样
    CHECK(b.Decide(WM_RBUTTONDOWN, true));
    CHECK_FALSE(b.Decide(WM_MOUSEWHEEL, false));
    CHECK(b.Decide(WM_MOUSEWHEEL, true));
}

TEST_CASE("MBT2 an unrecorded release is sent only inside, and a double click counts as a press", "[mousebuttons]")
{
    ViewerMouseButtons b;
    //没记过的按钮：画面外松开不发，画面内松开照发
    CHECK_FALSE(b.Decide(WM_RBUTTONUP, false));
    CHECK(b.Decide(WM_RBUTTONUP, true));
    //双击按按下处理：画面内发、记下，待补发的松开里有它
    CHECK(b.Decide(WM_MBUTTONDBLCLK, true));
    CHECK(b.ButtonsDown() == kViewerButtonMiddle);
    const std::vector<uint32_t> ups = b.PendingButtonUps();
    REQUIRE(ups.size() == 1);
    CHECK(ups[0] == (uint32_t)WM_MBUTTONUP);
    //画面外的双击：不发、不记
    CHECK_FALSE(b.Decide(WM_LBUTTONDBLCLK, false));
    CHECK(b.ButtonsDown() == kViewerButtonMiddle);
}

TEST_CASE("MBT3 reset forgets pressed buttons", "[mousebuttons]")
{
    ViewerMouseButtons b;
    REQUIRE(b.Decide(WM_LBUTTONDOWN, true));
    REQUIRE(b.Decide(WM_RBUTTONDOWN, true));
    CHECK(b.PendingButtonUps().size() == 2);
    b.Reset();
    CHECK(b.ButtonsDown() == 0);
    CHECK(b.PendingButtonUps().empty());
    //复位后画面外的松开与移动都不发
    CHECK_FALSE(b.Decide(WM_LBUTTONUP, false));
    CHECK_FALSE(b.Decide(WM_MOUSEMOVE, false));
}
