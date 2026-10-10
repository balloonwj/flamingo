/**
 *  选会话窗口 CForwardTargetDlg 的窗口用例（标签 [fwd-wnd]，隐藏用例：会在屏幕上弹出窗口，须单独运行 "[fwd-wnd]"），
 *  ForwardTargetWindowTests.cpp
 *  背景：2026-10-05 用户反馈主面板「发起屏幕共享」先弹出的选会话窗口点标题栏关闭按钮关不掉，并要求加最小化、最大化，
 *  支持拖拽改大小。原因：窗口自己的控件号从 1 编起，「联系人」页签为 3，与 DuiFrameWindow 标题栏关闭按钮的控件号 3 相同，
 *  点关闭时通知被当成点了页签截住（开发约定文档「UI 约定」第 4 条）。
 *  覆盖：
 *    - FWDW1 窗口有最小化、最大化所需的样式与任务栏按钮，可拖拽改大小，不能缩到最小尺寸以下；
 *    - FWDW2 标题栏关闭按钮的点击通知（控件号 3）关闭窗口，不再被当成「联系人」页签；
 *    - FWDW3 标题栏最小化按钮的点击通知（控件号 1）把窗口最小化，不再被当成搜索框；
 *    - FWDW4 标题栏最大化按钮的点击通知（控件号 2）把窗口最大化，不再被当成「最近会话」页签。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonMain/ForwardTargetDlg.h"
#include "../UI/BalloonMain/ForwardRequest.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace {

//DuiFrameWindow 标题栏三个按钮的控件号（DuiFrameWindow.cpp 的 OnDuiNotify：1 最小化、2 最大化 / 还原、3 关闭）
const UINT kFwdwCaptionMin = 1;
const UINT kFwdwCaptionMax = 2;
const UINT kFwdwCaptionClose = 3;
//本人 userid（不参与断言）
const int64_t kFwdwSelf = 5127;
//试图缩到的过小尺寸与窗口的最小尺寸（与 ForwardTargetDlg.cpp 的 kDlgMinWidth / kDlgMinHeight 一致）
const int kFwdwTinyW = 200;
const int kFwdwTinyH = 150;
const int kFwdwMinW = 600;
const int kFwdwMinH = 460;

void FwdwPump()
{
    MSG msg;
    for (int i = 0; i < 3; ++i)
    {
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(20);
    }
}

//以主面板「发起屏幕共享」的用途打开
void FwdwOpen(balloonmain::CForwardTargetDlg& dlg)
{
    balloonmain::ForwardRequest req;
    req.m_screenShare = true;
    dlg.Open(NULL, kFwdwSelf, req, std::vector<balloonmain::ForwardTarget>(),
             std::vector<balloonmain::ForwardTarget>(), std::vector<net::ContactItem>(),
             std::vector<net::DepartmentItem>());
}

//模拟标题栏按钮的点击：与 DuiFrameWindow 标题栏按钮发出的通知相同
void FwdwCaptionClick(balloonmain::CForwardTargetDlg& dlg, UINT ctrlId)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = ctrlId;
    n.pCtrl = NULL;
    n.extra = 0;
    dlg.SendMessage(WM_DUI_NOTIFY, static_cast<WPARAM>(ctrlId), reinterpret_cast<LPARAM>(&n));
}

} // namespace

TEST_CASE("FWDW1 forward picker can be minimized, maximized and resized", "[.][fwd-wnd]")
{
    balloonmain::CForwardTargetDlg dlg;
    FwdwOpen(dlg);
    REQUIRE(dlg.IsWindow());
    CHECK(dlg.IsResizable());
    CHECK(dlg.HasMinButton());
    CHECK(dlg.HasMaxButton());
    CHECK(dlg.HasCloseButton());
    CHECK((dlg.GetStyle() & WS_MINIMIZEBOX) != 0);
    CHECK((dlg.GetStyle() & WS_MAXIMIZEBOX) != 0);
    CHECK((dlg.GetExStyle() & WS_EX_APPWINDOW) != 0);
    dlg.ResizeClient(kFwdwTinyW, kFwdwTinyH);
    FwdwPump();
    RECT rc;
    dlg.GetWindowRect(&rc);
    CHECK(rc.right - rc.left >= kFwdwMinW);
    CHECK(rc.bottom - rc.top >= kFwdwMinH);
    dlg.DestroyWindow();
}

TEST_CASE("FWDW2 caption close button closes the forward picker", "[.][fwd-wnd]")
{
    balloonmain::CForwardTargetDlg dlg;
    FwdwOpen(dlg);
    REQUIRE(dlg.IsWindow());
    FwdwCaptionClick(dlg, kFwdwCaptionClose);
    FwdwPump();
    CHECK_FALSE(dlg.IsWindow());
    if (dlg.IsWindow())
        dlg.DestroyWindow();
}

TEST_CASE("FWDW3 caption minimize button minimizes the forward picker", "[.][fwd-wnd]")
{
    balloonmain::CForwardTargetDlg dlg;
    FwdwOpen(dlg);
    REQUIRE(dlg.IsWindow());
    const HWND hwnd = dlg.m_hWnd;
    FwdwCaptionClick(dlg, kFwdwCaptionMin);
    FwdwPump();
    CHECK(::IsIconic(hwnd) != FALSE);
    if (dlg.IsWindow())
        dlg.DestroyWindow();
}

TEST_CASE("FWDW4 caption maximize button maximizes the forward picker", "[.][fwd-wnd]")
{
    balloonmain::CForwardTargetDlg dlg;
    FwdwOpen(dlg);
    REQUIRE(dlg.IsWindow());
    const HWND hwnd = dlg.m_hWnd;
    FwdwCaptionClick(dlg, kFwdwCaptionMax);
    FwdwPump();
    CHECK(::IsZoomed(hwnd) != FALSE);
    if (dlg.IsWindow())
        dlg.DestroyWindow();
}
