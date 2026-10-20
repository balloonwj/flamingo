/**
 *  窗口最小尺寸随 DPI 等比换算的单元测试（标签 [dpiminsize]），DpiScaledMinSizeTests.cpp
 *
 *  背景：2026-10-17 远程桌面联调时发现，显示器缩放由 125% 变成 100% 再变回来（临时把分辨率由 1920×1080 改成 1600×900，
 *  实测 DPI 120 → 96 → 120），主面板从 338 宽变成 375 宽：96 DPI 时系统给的建议宽度约 270，被 SetMinSize 设的 300 像素
 *  下限抬到 300，回到 120 DPI 时又按 1.25 倍放大。balloonmain::CDpiScaledMinSize 在窗口收到 WM_DPICHANGED 时先把最小尺寸
 *  按新 DPI 等比换算，再交给基类按建议矩形摆窗口。
 *    - DPM1 换算：按 120 DPI 定的 300×480 下限，96 DPI 时为 240×384，144 DPI 时为 360×576；DPI 非法时取原值；
 *    - DPM2 真实窗口（屏幕外、与主面板相同的 WS_OVERLAPPEDWINDOW）照系统的做法收到 120 → 96 → 120 两次 WM_DPICHANGED：
 *           96 DPI 时宽度跟着建议矩形缩到 270，不被抬到 300；回到 120 DPI 后与原尺寸相差不超过 1 像素（系统取整）；
 *           窗口本来就在最小尺寸时，来回一次之后仍是最小尺寸。
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-17
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"
#include "../UI/BalloonMain/DpiScaledMinSize.h"

#include <stdlib.h>

namespace {

//主面板的设计尺寸与最小尺寸（整窗，物理像素，按 120 DPI 定的值），与 BalloonMainPanel.cpp 的常量相同
const int kDpmWidth = 338;
const int kDpmHeight = 727;
const int kDpmMinWidth = 300;
const int kDpmMinHeight = 480;
//125%、100%、150% 缩放对应的 DPI
const int kDpm125 = 120;
const int kDpm100 = 96;
const int kDpm150 = 144;
//窗口左上角坐标：放到屏幕外，测试时不在眼前闪烁
const int kDpmOffscreen = -32000;
//系统换算建议矩形时的取整误差（像素）
const int kDpmRoundingPx = 1;

//被测窗口：消息映射里先交给 CDpiScaledMinSize 换算最小尺寸，再交给基类按建议矩形摆窗口（与主面板的接法相同）
class DpmWindow : public balloonwjui::DuiFrameWindow
{
public:
    balloonmain::CDpiScaledMinSize m_minSize;   //被测对象

    BEGIN_MSG_MAP(DpmWindow)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChangedMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

    LRESULT OnDpiChangedMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
    {
        m_minSize.OnDpiChanged(*this, LOWORD(wParam));
        bHandled = FALSE;
        return 0;
    }
};

//模拟系统在 DPI 变化时发的 WM_DPICHANGED：建议矩形左上角不变，宽高按新旧 DPI 等比换算（四舍五入）
void dpmSendDpiChange(DpmWindow& wnd, int oldDpi, int newDpi)
{
    RECT rc;
    wnd.GetWindowRect(&rc);
    RECT suggested;
    suggested.left = rc.left;
    suggested.top = rc.top;
    suggested.right = rc.left + ::MulDiv(rc.right - rc.left, newDpi, oldDpi);
    suggested.bottom = rc.top + ::MulDiv(rc.bottom - rc.top, newDpi, oldDpi);
    wnd.SendMessage(WM_DPICHANGED, MAKEWPARAM(newDpi, newDpi), reinterpret_cast<LPARAM>(&suggested));
}

//窗口当前的宽 / 高
SIZE dpmSize(DpmWindow& wnd)
{
    RECT rc;
    wnd.GetWindowRect(&rc);
    SIZE s;
    s.cx = rc.right - rc.left;
    s.cy = rc.bottom - rc.top;
    return s;
}

/**
 *  建一个屏幕外的被测窗口，最小尺寸按 120 DPI 设好。
 *  @param wnd 被测窗口
 *  @param w / h 初始宽高（物理像素）
 *  @return 建成返回 true
 */
bool dpmCreate(DpmWindow& wnd, int w, int h)
{
    RECT rc = { kDpmOffscreen, kDpmOffscreen, kDpmOffscreen + w, kDpmOffscreen + h };
    if (wnd.Create(NULL, rc, _T("DpiScaledMinSizeTests"), WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW) == NULL)
        return false;
    wnd.m_minSize.Init(wnd, kDpmMinWidth, kDpmMinHeight, kDpm125);
    return true;
}

} // namespace

TEST_CASE("DPM1 最小尺寸按 DPI 等比换算", "[dpiminsize]")
{
    DpmWindow wnd;
    REQUIRE(dpmCreate(wnd, kDpmWidth, kDpmHeight));
    CHECK(wnd.m_minSize.ScaledWidth(kDpm125) == kDpmMinWidth);
    CHECK(wnd.m_minSize.ScaledHeight(kDpm125) == kDpmMinHeight);
    CHECK(wnd.m_minSize.ScaledWidth(kDpm100) == 240);
    CHECK(wnd.m_minSize.ScaledHeight(kDpm100) == 384);
    CHECK(wnd.m_minSize.ScaledWidth(kDpm150) == 360);
    CHECK(wnd.m_minSize.ScaledHeight(kDpm150) == 576);
    //DPI 非法：取原值
    CHECK(wnd.m_minSize.ScaledWidth(0) == kDpmMinWidth);
    wnd.DestroyWindow();
}

TEST_CASE("DPM2 缩放 125% → 100% → 125% 之后窗口回到原尺寸，不被最小尺寸抬宽", "[dpiminsize]")
{
    //WM_DPICHANGED 会改 balloonui 的全局 DPI，用例结束时恢复，免得影响别的用例
    const int savedDpi = balloonwjui::DuiResMgr::Inst().GetDpi();

    //设计尺寸：96 DPI 时跟着建议矩形缩到 270，回到 120 DPI 后回到原尺寸
    {
        DpmWindow wnd;
        REQUIRE(dpmCreate(wnd, kDpmWidth, kDpmHeight));
        dpmSendDpiChange(wnd, kDpm125, kDpm100);
        const SIZE at96 = dpmSize(wnd);
        CHECK(at96.cx == ::MulDiv(kDpmWidth, kDpm100, kDpm125));
        dpmSendDpiChange(wnd, kDpm100, kDpm125);
        const SIZE back = dpmSize(wnd);
        CHECK(abs(back.cx - kDpmWidth) <= kDpmRoundingPx);
        CHECK(abs(back.cy - kDpmHeight) <= kDpmRoundingPx);
        wnd.DestroyWindow();
    }

    //窗口本来就在最小尺寸：来回一次之后仍是最小尺寸
    {
        DpmWindow wnd;
        REQUIRE(dpmCreate(wnd, kDpmMinWidth, kDpmMinHeight));
        dpmSendDpiChange(wnd, kDpm125, kDpm100);
        dpmSendDpiChange(wnd, kDpm100, kDpm125);
        const SIZE back = dpmSize(wnd);
        CHECK(abs(back.cx - kDpmMinWidth) <= kDpmRoundingPx);
        CHECK(abs(back.cy - kDpmMinHeight) <= kDpmRoundingPx);
        wnd.DestroyWindow();
    }

    balloonwjui::DuiResMgr::Inst().SetDpi(savedDpi);
}
