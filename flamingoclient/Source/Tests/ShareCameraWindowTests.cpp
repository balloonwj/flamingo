/**
 *  屏幕共享主持人一侧摄像头小窗的「收起 / 展开」单元测试（Suite CAMW，标签 [sharecamera]），ShareCameraWindowTests.cpp
 *  背景：小窗标题栏的 X 原来只把窗口藏起来，要等摄像头清单变化才会重新出现，期间没有任何入口能找回（用户 2026-10-12 反馈）。
 *  改为「收起」：缩成贴在屏幕右边缘的小把手（摄像头图标加人数），点把手展开。2026-10-12 与用户确认的用例：
 *    - CAMW1 点 X：缩成右边缘的小把手，窗口仍然可见，把手上写着人数；
 *    - CAMW2 点把手：展开回原来的大小与位置；
 *    - CAMW3 收起期间摄像头清单变化：保持收起，把手上的人数随之更新；
 *    - CAMW4 摄像头全部关掉时小窗与把手都隐藏；再有人开摄像头时按原来的收起或展开状态出现；新的一场共享从展开开始。
 *  会建出真实的置顶小窗，在隐藏桌面上运行。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareCameraWindow.h"
#include "../../FlamingoMedia/Share/ShareCameraPanel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//参照矩形（被共享内容所在的显示器，屏幕坐标）
const RECT kCamwMonitor = { 0, 0, 1600, 900 };

//把投给小窗的消息处理完（收起、展开都是先投给自己再执行）
void CamwPump(HWND hwnd)
{
    MSG msg;
    while (::PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

//count 个小窗（第一个是本人）
std::vector<ShareCameraTile> CamwTiles(int count)
{
    std::vector<ShareCameraTile> tiles;
    for (int i = 0; i < count; ++i)
    {
        ShareCameraTile t;
        t.m_seat = i;
        t.m_name = (i == 0) ? L"me" : L"peer";
        t.m_self = (i == 0);
        tiles.push_back(t);
    }
    return tiles;
}

//把手按钮
balloonwjui::DuiButton* CamwHandle(CShareCameraWindow& wnd)
{
    balloonwjui::DuiControl* content = wnd.GetClientContent();
    return content != NULL
               ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CShareCameraWindow::kIdHandle))
               : NULL;
}

//模拟点一下把手
void CamwClickHandle(CShareCameraWindow& wnd)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = CShareCameraWindow::kIdHandle;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd.m_hWnd, WM_DUI_NOTIFY, static_cast<WPARAM>(n.ctrlId), reinterpret_cast<LPARAM>(&n));
    CamwPump(wnd.m_hWnd);
}

//点标题栏的 X（关窗）
void CamwClickClose(CShareCameraWindow& wnd)
{
    ::SendMessageW(wnd.m_hWnd, WM_CLOSE, 0, 0);
    CamwPump(wnd.m_hWnd);
}

RECT CamwWindowRect(CShareCameraWindow& wnd)
{
    RECT rc;
    ::GetWindowRect(wnd.m_hWnd, &rc);
    return rc;
}

} // namespace

TEST_CASE("CAMW1 closing the camera window collapses it to a handle at the right edge", "[sharecamera]")
{
    CShareCameraWindow wnd;
    wnd.Update(CamwTiles(2), 0, kCamwMonitor);
    REQUIRE(wnd.IsWindow());
    REQUIRE(wnd.IsWindowVisible());
    CHECK_FALSE(wnd.IsCollapsed());
    const RECT expanded = CamwWindowRect(wnd);

    CamwClickClose(wnd);
    CHECK(wnd.IsCollapsed());
    CHECK(wnd.IsWindowVisible());
    const RECT handle = CamwWindowRect(wnd);
    //贴着原来的右边缘、在原来的顶边上，比展开时小
    CHECK(handle.right == expanded.right);
    CHECK(handle.top == expanded.top);
    CHECK(handle.right - handle.left < expanded.right - expanded.left);
    CHECK(handle.bottom - handle.top < expanded.bottom - expanded.top);
    balloonwjui::DuiButton* btn = CamwHandle(wnd);
    REQUIRE(btn != NULL);
    CHECK(btn->GetText() == CString(L"2"));
    wnd.Close();
}

TEST_CASE("CAMW2 clicking the handle expands the window back", "[sharecamera]")
{
    CShareCameraWindow wnd;
    wnd.Update(CamwTiles(2), 0, kCamwMonitor);
    REQUIRE(wnd.IsWindow());
    const RECT expanded = CamwWindowRect(wnd);
    CamwClickClose(wnd);
    REQUIRE(wnd.IsCollapsed());

    CamwClickHandle(wnd);
    CHECK_FALSE(wnd.IsCollapsed());
    const RECT again = CamwWindowRect(wnd);
    CHECK(::EqualRect(&again, &expanded));
    CHECK(CamwHandle(wnd) == NULL);
    wnd.Close();
}

TEST_CASE("CAMW3 camera changes keep the window collapsed and update the count", "[sharecamera]")
{
    CShareCameraWindow wnd;
    wnd.Update(CamwTiles(1), 0, kCamwMonitor);
    REQUIRE(wnd.IsWindow());
    CamwClickClose(wnd);
    REQUIRE(wnd.IsCollapsed());

    wnd.Update(CamwTiles(3), 0, kCamwMonitor);
    CHECK(wnd.IsCollapsed());
    CHECK(wnd.IsWindowVisible());
    balloonwjui::DuiButton* btn = CamwHandle(wnd);
    REQUIRE(btn != NULL);
    CHECK(btn->GetText() == CString(L"3"));
    wnd.Close();
}

TEST_CASE("CAMW4 all cameras off hides it, the collapsed state survives, a new share starts expanded", "[sharecamera]")
{
    CShareCameraWindow wnd;
    wnd.Update(CamwTiles(1), 0, kCamwMonitor);
    REQUIRE(wnd.IsWindow());
    CamwClickClose(wnd);
    REQUIRE(wnd.IsCollapsed());

    //摄像头全部关掉：小窗与把手都隐藏
    wnd.Update(std::vector<ShareCameraTile>(), 0, kCamwMonitor);
    CHECK_FALSE(wnd.IsWindowVisible());
    //又有人开摄像头：仍是收起的把手
    wnd.Update(CamwTiles(1), 0, kCamwMonitor);
    CHECK(wnd.IsWindowVisible());
    CHECK(wnd.IsCollapsed());

    //这一场共享结束（控制器调 Close），下一场从展开开始
    wnd.Close();
    wnd.Update(CamwTiles(1), 0, kCamwMonitor);
    REQUIRE(wnd.IsWindow());
    CHECK_FALSE(wnd.IsCollapsed());
    CHECK(CamwHandle(wnd) == NULL);
    wnd.Close();
}
