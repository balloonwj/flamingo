/**
 *  PopupMenuPlacementTests.cpp
 *
 *  弹出菜单落点夹取（PopupMenuPlacement）的测试：验证纯函数
 *  ClampMenuOriginToRect() 把菜单左上角夹进给定矩形的策略 —— 放得下时不动、
 *  右边放不下翻左、下边放不下翻上（托盘场景）、两向都放不下翻到左上、菜单比
 *  矩形还大时贴左上边缘、工作区原点非 0（任务栏在上/左）时尊重上/左边界。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不取显示器信息。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/PopupMenuPlacement.h"

using namespace balloonmain;

namespace {

POINT MakePt(long x, long y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

SIZE MakeSize(long cx, long cy)
{
    SIZE s;
    s.cx = cx;
    s.cy = cy;
    return s;
}

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

// 典型 1920×1080、任务栏在底部（工作区高 1040）。
static const RECT kWork = { 0, 0, 1920, 1040 };

TEST_CASE("PMP1 放得下：落点不变", "[popupmenu]")
{
    POINT out = ClampMenuOriginToRect(MakePt(100, 100), MakeSize(200, 300), kWork);
    CHECK(out.x == 100);
    CHECK(out.y == 100);
}

TEST_CASE("PMP2 右边放不下：翻到锚点左侧", "[popupmenu]")
{
    // 1900 + 200 = 2100 > 1920 → x 翻到 1900 - 200 = 1700；y 放得下不动。
    POINT out = ClampMenuOriginToRect(MakePt(1900, 100), MakeSize(200, 300), kWork);
    CHECK(out.x == 1700);
    CHECK(out.y == 100);
}

TEST_CASE("PMP3 下边放不下：翻到锚点上方（托盘场景）", "[popupmenu]")
{
    // 1030 + 250 = 1280 > 1040 → y 翻到 1030 - 250 = 780；x 放得下不动。
    POINT out = ClampMenuOriginToRect(MakePt(100, 1030), MakeSize(180, 250), kWork);
    CHECK(out.x == 100);
    CHECK(out.y == 780);
}

TEST_CASE("PMP4 右下都放不下：翻到锚点左上（托盘右下角）", "[popupmenu]")
{
    // x: 1900+180=2080>1920 → 1900-180=1720；y: 1030+250=1280>1040 → 1030-250=780。
    POINT out = ClampMenuOriginToRect(MakePt(1900, 1030), MakeSize(180, 250), kWork);
    CHECK(out.x == 1720);
    CHECK(out.y == 780);
}

TEST_CASE("PMP5 菜单比矩形还大：贴左上边缘、不越界", "[popupmenu]")
{
    // 小工作区 100×100，菜单 200×200：翻转后仍越界，最终贴 (left, top)。
    RECT  tiny = MakeRect(0, 0, 100, 100);
    POINT out = ClampMenuOriginToRect(MakePt(10, 10), MakeSize(200, 200), tiny);
    CHECK(out.x == 0);
    CHECK(out.y == 0);
}

TEST_CASE("PMP6 工作区原点非 0：尊重上/左边界", "[popupmenu]")
{
    // 任务栏在顶部：工作区从 y=40 起。锚点在 y=50、菜单上翻会越过顶边，应夹到 40。
    RECT  work = MakeRect(0, 40, 1920, 1080);
    // 让下边放不下以触发上翻：1070 + 300 = 1370 > 1080 → 1070-300=770（>=40 不再夹）。
    POINT down = ClampMenuOriginToRect(MakePt(100, 1070), MakeSize(200, 300), work);
    CHECK(down.y == 770);

    // 锚点贴顶、菜单较高，上翻后越过顶边 → 夹到 work.top(40)。
    POINT up = ClampMenuOriginToRect(MakePt(100, 1075), MakeSize(200, 1050), work);
    CHECK(up.y == 40);
}

TEST_CASE("PMP7 翻转后回到工作区内（左/上边界）", "[popupmenu]")
{
    // 锚点靠左但右边放不下 → 本应翻左，翻左后 x 变负，再夹回 left。
    RECT  work = MakeRect(0, 0, 300, 1040);
    POINT out = ClampMenuOriginToRect(MakePt(250, 100), MakeSize(280, 200), work);
    // 250+280=530>300 → 翻左 250-280=-30 → -30+280=250<=300 不再夹 → x<0 夹到 0。
    CHECK(out.x == 0);
    CHECK(out.y == 100);
}

// =====================================================================
// TRAY1 ~ TRAY4 —— 贴着托盘图标弹出的浮窗（被踢下线提示框）的落点
//
// 与菜单共用同一套夹取逻辑，这几条用例盯的是它特有的两件事：锚点取托盘图标
// 左上角、以及取不到图标矩形时的兜底。
// =====================================================================

// 被踢下线提示框的典型尺寸，几条用例共用。
static const SIZE kDlgSize = { 360, 190 };

TEST_CASE("TRAY1 任务栏在下、托盘在右下角：翻到图标左上方且整窗在工作区内", "[popupmenu][tray]")
{
    // 1920×1080、任务栏在底部（工作区高 1040）；托盘图标在右下角一小块。
    RECT  tray = MakeRect(1860, 1046, 1876, 1062);
    POINT out  = ComputeTrayAnchoredOrigin(tray, kDlgSize, kWork);

    // 右边放不下 → 翻到图标左侧。
    CHECK(out.x == tray.left - kDlgSize.cx);

    // 纵向：托盘图标在任务栏里、位于工作区<u>之外</u>，所以翻到图标上方之后仍会压住
    // 工作区下边缘，最终被夹成"贴着工作区底边"（也就是紧挨任务栏上沿）。这里断言的是
    //"整窗在图标上方"这个观感，而不是某个精确坐标 —— 精确坐标随任务栏高度而变。
    CHECK(out.y + kDlgSize.cy <= tray.top);
    CHECK(out.y + kDlgSize.cy == kWork.bottom);

    // 整窗必须落在工作区内。
    CHECK(out.x >= kWork.left);
    CHECK(out.y >= kWork.top);
    CHECK(out.x + kDlgSize.cx <= kWork.right);
    CHECK(out.y + kDlgSize.cy <= kWork.bottom);
}

TEST_CASE("TRAY2 任务栏在上、托盘在右上角：向下展开且整窗在工作区内", "[popupmenu][tray]")
{
    // 任务栏在顶部：工作区从 y=40 起；托盘图标在右上角。
    RECT  work = MakeRect(0, 40, 1920, 1080);
    RECT  tray = MakeRect(1860, 8, 1876, 24);
    POINT out  = ComputeTrayAnchoredOrigin(tray, kDlgSize, work);

    // 下方放得开 → 不翻转，纵向仍从锚点起（锚点在工作区上边之外，最后被夹回 work.top）。
    CHECK(out.y >= work.top);
    CHECK(out.y + kDlgSize.cy <= work.bottom);

    // 横向仍是右边放不下 → 翻到图标左侧。
    CHECK(out.x == tray.left - kDlgSize.cx);
    CHECK(out.x >= work.left);
}

TEST_CASE("TRAY3 取不到托盘图标矩形：退回工作区右下角", "[popupmenu][tray]")
{
    // 图标被折叠进溢出区时 GetTrayIconRect 给出空矩形（这里用全 0 表示）。
    RECT  empty = MakeRect(0, 0, 0, 0);
    POINT out   = ComputeTrayAnchoredOrigin(empty, kDlgSize, kWork);

    CHECK(out.x + kDlgSize.cx == kWork.right);
    CHECK(out.y + kDlgSize.cy == kWork.bottom);
}

TEST_CASE("TRAY4 对话框比工作区还大：夹到左上角、不出现负向越界", "[popupmenu][tray]")
{
    // 极端小工作区（如远程桌面小分辨率），窗口放不下时只能贴左上角。
    RECT tiny = MakeRect(0, 0, 200, 150);

    POINT out = ComputeTrayAnchoredOrigin(MakeRect(180, 130, 196, 146), kDlgSize, tiny);
    CHECK(out.x == tiny.left);
    CHECK(out.y == tiny.top);

    // 取不到图标的兜底分支同样不能算出负坐标。
    POINT outNoIcon = ComputeTrayAnchoredOrigin(MakeRect(0, 0, 0, 0), kDlgSize, tiny);
    CHECK(outNoIcon.x == tiny.left);
    CHECK(outNoIcon.y == tiny.top);
}
