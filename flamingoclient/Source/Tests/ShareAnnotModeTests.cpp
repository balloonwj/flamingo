/**
 *  标注模式输入面与工具条锚点的单元测试（标签 [annotmode]），ShareAnnotModeTests.cpp
 *  背景：2026-10-07 用户实测，主持人在自己的白板上标注时白板窗口拖不动：setAnnotMode 在主持人一侧无条件打开共享内容上的
 *  输入层，几乎透明的置顶窗口盖住整块屏幕、压住了白板窗口的标题栏；工具条也锚在共享内容而不是白板上。同时新增「鼠标」
 *  状态：保留工具条与标注，但不拦截鼠标。
 *  覆盖：
 *    - SAMD1 主持人在白板上标注：不打开任何屏幕输入层，画布接收标注，工具条锚在白板上；
 *    - SAMD2 主持人在共享内容上、共享者在自己的内容上、观看者在观看窗里标注：各自只打开自己的那一个输入面，锚点对应；
 *    - SAMD3 「鼠标」状态：各路径的输入面全部放开，锚点不变（工具条保留）；
 *    - SAMD4 不在标注模式，或既不在推流也不在观看：全部关闭，没有锚点。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Share/ShareAnnotModePolicy.h"

using namespace screenshare;

namespace {

//四个输入面都关着
bool SamdAllOff(const ShareAnnotSurfacePlan& p)
{
    return !p.m_hostOverlayInput && !p.m_viewerCanvasInput && !p.m_publishOverlayInput && !p.m_boardInput;
}

} // namespace

TEST_CASE("SAMD1 host annotating on own whiteboard opens no screen input layer", "[annotmode]")
{
    const ShareAnnotSurfacePlan p = DecideAnnotSurfaces(true, true, false, false, true, false);
    CHECK_FALSE(p.m_hostOverlayInput);
    CHECK_FALSE(p.m_viewerCanvasInput);
    CHECK_FALSE(p.m_publishOverlayInput);
    CHECK(p.m_boardInput);
    CHECK(p.m_anchor == annot_anchor_board);
    //观看者在自己的白板上标注同理
    const ShareAnnotSurfacePlan v = DecideAnnotSurfaces(true, false, true, false, true, false);
    CHECK_FALSE(v.m_viewerCanvasInput);
    CHECK(v.m_boardInput);
    CHECK(v.m_anchor == annot_anchor_board);
}

TEST_CASE("SAMD2 each annotation path opens only its own input surface", "[annotmode]")
{
    //主持人在共享内容上
    ShareAnnotSurfacePlan p = DecideAnnotSurfaces(true, true, false, false, false, false);
    CHECK(p.m_hostOverlayInput);
    CHECK_FALSE(p.m_viewerCanvasInput);
    CHECK_FALSE(p.m_boardInput);
    CHECK(p.m_anchor == annot_anchor_host_source);
    //共享者在自己的内容上
    p = DecideAnnotSurfaces(true, false, true, true, false, false);
    CHECK(p.m_publishOverlayInput);
    CHECK_FALSE(p.m_viewerCanvasInput);
    CHECK(p.m_anchor == annot_anchor_own_source);
    //观看者在观看窗里
    p = DecideAnnotSurfaces(true, false, true, false, false, false);
    CHECK(p.m_viewerCanvasInput);
    CHECK_FALSE(p.m_hostOverlayInput);
    CHECK_FALSE(p.m_publishOverlayInput);
    CHECK(p.m_anchor == annot_anchor_viewer);
}

TEST_CASE("SAMD3 pointer mode releases every input surface but keeps the anchor", "[annotmode]")
{
    ShareAnnotSurfacePlan p = DecideAnnotSurfaces(true, true, false, false, false, true);
    CHECK(SamdAllOff(p));
    CHECK(p.m_anchor == annot_anchor_host_source);
    p = DecideAnnotSurfaces(true, false, true, false, false, true);
    CHECK(SamdAllOff(p));
    CHECK(p.m_anchor == annot_anchor_viewer);
    p = DecideAnnotSurfaces(true, false, true, true, false, true);
    CHECK(SamdAllOff(p));
    CHECK(p.m_anchor == annot_anchor_own_source);
    p = DecideAnnotSurfaces(true, true, false, false, true, true);
    CHECK(SamdAllOff(p));
    CHECK(p.m_anchor == annot_anchor_board);
}

TEST_CASE("SAMD4 not annotating closes everything", "[annotmode]")
{
    ShareAnnotSurfacePlan p = DecideAnnotSurfaces(false, true, true, true, true, false);
    CHECK(SamdAllOff(p));
    CHECK(p.m_anchor == annot_anchor_none);
    p = DecideAnnotSurfaces(true, false, false, false, false, false);
    CHECK(SamdAllOff(p));
    CHECK(p.m_anchor == annot_anchor_none);
}
