/**
 *  屏幕共享观看窗回到全屏的入口单元测试（Suite VFS，标签 [viewerfs]），ShareViewerFullscreenTests.cpp
 *  背景：观看窗默认全屏，退出全屏后标题栏上没有「全屏」入口，只能按 F11（用户 2026-10-12 反馈「退出全屏后再也回不到全屏」）。
 *  2026-10-12 与用户确认的用例：
 *    - VFS1 窗口模式（非强制模式）下有「全屏」入口，点它进入全屏；强制模式下没有。第 2c 步（2026-10-12）起这个入口从
 *      标题栏图标移进浮动工具条，用例随之改为点工具条上的「全屏」按钮；
 *    - VFS2 双击画面：空闲时在窗口与全屏之间切换；正在标注、或正在控制对方屏幕时双击不切换。
 *      多人共享同时显示多块画面时，双击仍是原来的「放大 / 还原某一块」（第 22 期），不切换全屏，本用例只测单画面。
 *  这两条会建出真实的观看窗（打开时默认全屏），须在隐藏桌面上运行，不要在用户的桌面上跑。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include "../../FlamingoMedia/Share/ShareViewerWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//中转公钥的字节数（与 libsodium 的公钥长度一致；用例不连中转，内容无所谓）
const int kVfsPubKeyLen = 32;

//收命令的通知窗口
HWND VfsNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//把投给观看窗的消息处理完（点图标、双击都是先投给自己再切换全屏）
void VfsPump(HWND hwnd)
{
    MSG msg;
    while (::PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

//打开一个不连中转的观看窗：收流管线启动失败，窗口照样建好并以全屏显示
bool VfsOpen(CShareViewerWindow& wnd, HWND notify)
{
    ShareRelayInfo relay;
    relay.m_roomId = "vfs-room";
    unsigned char pubKey[kVfsPubKeyLen] = { 0 };
    wnd.Open(relay, pubKey, L"host", notify, NULL, std::shared_ptr<balloonp2p::P2PLinkSlot>());
    if (!wnd.IsWindow())
        return false;
    //把打开过程中投给窗口的消息处理完，再开始操作
    VfsPump(wnd.m_hWnd);
    return true;
}

//模拟点一下浮动工具条上的按钮
void VfsClickBarButton(HWND bar, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(bar, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//在画面左侧三分之一、上下居中处双击（避开右侧可能出现的摄像头小窗条）
void VfsDoubleClickPicture(HWND hwnd)
{
    RECT client;
    ::GetClientRect(hwnd, &client);
    const int thirdDivisor = 3;
    const int halfDivisor = 2;
    const int x = (client.right - client.left) / thirdDivisor;
    const int y = (client.bottom - client.top) / halfDivisor;
    ::SendMessageW(hwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, y));
    ::SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    VfsPump(hwnd);
}

} // namespace

TEST_CASE("VFS1 windowed viewer has a fullscreen caption icon, forced mode has none", "[viewerfs]")
{
    const HWND notify = VfsNotifyWindow();
    CShareViewerWindow wnd;
    REQUIRE(VfsOpen(wnd, notify));
    //打开时默认全屏；按 F11 退回窗口模式
    REQUIRE(wnd.IsFullscreen());
    ::SendMessageW(wnd.m_hWnd, WM_KEYDOWN, VK_F11, 0);
    REQUIRE_FALSE(wnd.IsFullscreen());

    //窗口模式：浮动工具条上有「全屏」按钮（第 2c 步起由标题栏图标移进工具条），点它回到全屏
    RECT rc;
    REQUIRE(wnd.BarButtonRectForTest(CShareViewerWindow::kBarFullscreen, rc));
    const HWND bar = wnd.BarWindowForTest();
    REQUIRE(bar != NULL);
    VfsClickBarButton(bar, CShareViewerWindow::kBarFullscreen);
    VfsPump(wnd.m_hWnd);
    CHECK(wnd.IsFullscreen());

    //强制模式：只能全屏，工具条上没有这个按钮
    wnd.SetForced(true);
    CHECK(wnd.IsFullscreen());
    CHECK_FALSE(wnd.BarButtonRectForTest(CShareViewerWindow::kBarFullscreen, rc));
    wnd.SetForced(false);
    wnd.CloseViewer();
    ::DestroyWindow(notify);
}

TEST_CASE("VFS2 double-clicking the picture toggles fullscreen unless annotating or controlling", "[viewerfs]")
{
    const HWND notify = VfsNotifyWindow();
    CShareViewerWindow wnd;
    REQUIRE(VfsOpen(wnd, notify));
    REQUIRE(wnd.IsFullscreen());

    //空闲：双击退出全屏，再双击回到全屏
    VfsDoubleClickPicture(wnd.m_hWnd);
    CHECK_FALSE(wnd.IsFullscreen());
    VfsDoubleClickPicture(wnd.m_hWnd);
    CHECK(wnd.IsFullscreen());

    //标注中：双击交给标注，不切换
    wnd.SetAnnotating(true);
    VfsDoubleClickPicture(wnd.m_hWnd);
    CHECK(wnd.IsFullscreen());
    wnd.SetAnnotating(false);

    //控制对方屏幕中：双击是对对方的操作，不切换
    wnd.SetControlState(share_viewer_control_active);
    VfsDoubleClickPicture(wnd.m_hWnd);
    CHECK(wnd.IsFullscreen());
    wnd.SetControlState(share_viewer_control_none);

    wnd.CloseViewer();
    ::DestroyWindow(notify);
}
