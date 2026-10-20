/**
 *  浮动观看工具条 CViewerFloatBar 的单元测试（Suite VB，标签 [viewerbar]），ViewerFloatBarTests.cpp
 *  背景：屏幕共享观看者与远程桌面控制端的按钮从窗口标题栏挪进一条浮动工具条，风格与共享者的控制条一致
 *  （docs/远程桌面控制权限与观看工具条方案.md 第 4.3 节）。2026-10-12 与用户确认的用例：
 *    - VB1 按钮都是 30 宽的图标按钮（带文字的按钮按文字加宽）、互不重叠、都有悬停提示；按钮增减后重排，不留空位；
 *    - VB2 点击每个按钮向通知窗口投出对应的按钮编号；
 *    - VB3 位于参照矩形（观看窗画面区域）顶部居中；参照矩形变了（观看窗移动、改变大小）后跟随；所有者窗口最小化时
 *          工具条随之隐藏，还原后重新出现；
 *    - VB4 全屏时以显示器矩形为参照，贴在显示器顶部居中；退出全屏后回到窗口模式的位置；
 *    - VB5 收起成小把手、隐藏后鼠标推到参照矩形顶边临时出现（复用 TopBarCollapse，只测接线）；
 *    - VB6 按编号取得按钮在屏幕上的矩形（菜单弹在按钮下方靠它）：矩形在工具条窗口内；编号不存在、工具条已隐藏时取不到；
 *    - VB7 屏幕共享观看窗接入后（第 2c 步）：按钮集合与从左到右的顺序；字幕、共享说明、主持人三个条件按钮的出现与消失；
 *    - VB8 源码守卫：观看窗的标题栏不再放自定义图标，也不再借用远程桌面的 CRdViewerToolbar；
 *    - VB9 全屏状态下成为主持人，「主持人」按钮立即出现（原来要等重建全屏工具条才出现）。
 *    - VB10（2026-10-12 实机联调发现后补）状态文字变长（如「仅查看」换成一句提示）时整条加宽，文字不被截断；变短时变回原宽。
 *    - VB11（2026-10-13 可见桌面实机测试发现后补）点击工具条不激活它：WM_MOUSEACTIVATE 返回 MA_NOACTIVATE。原来默认处理
 *          返回 MA_ACTIVATE，尽管带 WS_EX_NOACTIVATE，按下鼠标后工具条仍成为前台窗口，观看窗失去键盘焦点（控制中按键不再转发）；
 *    - VB12（同上）激活状态变化时系统不在工具条顶部画标题栏：收到 WM_NCACTIVATE 后标题条处仍是工具条的深色底。原来默认处理
 *          会画出一条经典样式的系统标题栏（浅蓝渐变、黑字），直到工具条重画才消失。需要读出窗口像素，读不出时（隐藏桌面上可能
 *          如此）给出警告并跳过，在可见桌面上运行才真正检查。
 *    - VB13（同上）工具条上某个按钮的悬停提示正在显示时，工具条被关闭（Hide）或随所有者窗口一起销毁，提示框随之关闭。原来
 *          按钮销毁时 DuiToolTipMgr 只取消了注册、没有关闭已显示的提示框，它一直留在屏幕上（远程桌面对方结束会话时，被控端
 *          顶部条上「允许控制」的提示就这样留了下来）。2026-10-13 起由 balloonui 的 DuiToolTipMgr::Unregister 关闭。
 *    - VB14（同上，balloonui 修复的端到端检查）在提示的弹出延时之内按下工具条按钮，提示不再弹出；只悬停时照常弹出。
 *  会建出真实的窗口，在隐藏桌面上运行。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdlib.h>
#include <vector>
#include <climits>
#include <memory>
#include <stdio.h>
#include <string>
#include "../../FlamingoMedia/Share/ViewerFloatBar.h"
#include "../../FlamingoMedia/Share/ShareViewerWindow.h"
#include "../../FlamingoMedia/Share/ShareBarButtonArt.h"
#include "../../FlamingoMedia/Share/ShareTopBarCollapse.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//用例里通知窗口收的消息号（只在本文件用，取值无意义）
const UINT kVbNotifyMsg = WM_APP + 0x7FF0;
//几个按钮的编号
const UINT kVbIdMic = kViewerBarUserIdFirst;
const UINT kVbIdCamera = kViewerBarUserIdFirst + 1;
const UINT kVbIdControl = kViewerBarUserIdFirst + 2;
const UINT kVbIdViewers = kViewerBarUserIdFirst + 3;
const UINT kVbIdFullscreen = kViewerBarUserIdFirst + 4;
const UINT kVbIdLeave = kViewerBarUserIdFirst + 5;
//所有者窗口（模拟观看窗）的位置与大小，以及画面区域在它里面的偏移（标题栏下方）
const int kVbOwnerX = 100;
const int kVbOwnerY = 80;
const int kVbOwnerW = 1200;
const int kVbOwnerH = 760;
const int kVbPictureTop = 36;

//收命令的通知窗口
HWND VbNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//模拟观看窗的所有者窗口：可见的普通顶层窗口
HWND VbOwnerWindow()
{
    HWND w = ::CreateWindowExW(0, L"STATIC", L"vb-owner", WS_OVERLAPPEDWINDOW, kVbOwnerX, kVbOwnerY, kVbOwnerW,
                               kVbOwnerH, NULL, NULL, ::GetModuleHandleW(NULL), NULL);
    ::ShowWindow(w, SW_SHOWNOACTIVATE);
    return w;
}

//所有者窗口的「画面区域」：窗口矩形去掉顶部的标题栏高度
RECT VbPictureRect(HWND owner)
{
    RECT rc;
    ::GetWindowRect(owner, &rc);
    rc.top += kVbPictureTop;
    return rc;
}

ViewerBarButtonSpec VbSpec(UINT id, int action, int state, const wchar_t* tip)
{
    return ViewerBarButtonSpec(id, action, state, tip);
}

//一组典型的按钮：麦克风、摄像头、申请控制（申请中）、N 人观看（带文字）、全屏、离开
std::vector<ViewerBarButtonSpec> VbButtons()
{
    std::vector<ViewerBarButtonSpec> v;
    v.push_back(VbSpec(kVbIdMic, share_bar_mic, share_bar_tri_off, L"mic"));
    v.push_back(VbSpec(kVbIdCamera, share_bar_camera, share_bar_tri_on, L"camera"));
    v.push_back(VbSpec(kVbIdControl, share_bar_control, share_bar_tri_pending, L"control"));
    ViewerBarButtonSpec viewers = VbSpec(kVbIdViewers, share_bar_viewers, share_bar_tri_off, L"viewers");
    viewers.m_text = L"3 people";
    v.push_back(viewers);
    v.push_back(VbSpec(kVbIdFullscreen, share_bar_fullscreen, share_bar_tri_off, L"fullscreen"));
    v.push_back(VbSpec(kVbIdLeave, share_bar_leave, share_bar_tri_off, L"leave"));
    return v;
}

balloonwjui::DuiButton* VbButton(CViewerFloatBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

//模拟点一下按钮
void VbClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//工具条窗口矩形的水平中心与参照矩形的水平中心相差不超过 1 像素、顶边在参照矩形顶边下方 kTopMarginPx
void VbCheckTopCenter(CViewerFloatBar& bar, const RECT& anchor)
{
    RECT rc;
    ::GetWindowRect(bar.m_hWnd, &rc);
    const int barCenter2 = rc.left + rc.right;
    const int anchorCenter2 = anchor.left + anchor.right;
    const int tolerance2 = 2;
    CHECK(abs(barCenter2 - anchorCenter2) <= tolerance2);
    CHECK(rc.top == anchor.top + CViewerFloatBar::kTopMarginPx);
}

} // namespace

TEST_CASE("VB1 icon buttons in a row, no overlap, tooltips, relaid out when buttons change", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());

    RECT client;
    ::GetClientRect(bar.m_hWnd, &client);
    const std::vector<ViewerBarButtonSpec> specs = VbButtons();
    std::vector<RECT> rects;
    for (size_t i = 0; i < specs.size(); ++i)
    {
        INFO("button " << specs[i].m_id);
        balloonwjui::DuiButton* b = VbButton(bar, specs[i].m_id);
        REQUIRE(b != NULL);
        const RECT rc = b->GetRect();
        CHECK(rc.left >= client.left);
        CHECK(rc.right <= client.right);
        if (specs[i].m_text.empty())
        {
            CHECK(rc.right - rc.left == kShareBarBtnPx);
            CHECK(b->GetText().IsEmpty());
        }
        else
        {
            CHECK(rc.right - rc.left > kShareBarBtnPx);
        }
        CHECK_FALSE(balloonwjui::DuiToolTipMgr::Inst().GetText(b).IsEmpty());
        for (size_t j = 0; j < rects.size(); ++j)
        {
            RECT both;
            CHECK_FALSE(::IntersectRect(&both, &rc, &rects[j]));
        }
        rects.push_back(rc);
    }
    //「收起」「隐藏」两个按钮总是有
    CHECK(VbButton(bar, CViewerFloatBar::kIdHide) != NULL);
    CHECK(VbButton(bar, CViewerFloatBar::kIdToggle) != NULL);

    //去掉中间的「申请控制」：重排后相邻按钮之间只隔标准间距，不留空位，整条变窄
    const int wideW = bar.BarWidthForTest();
    std::vector<ViewerBarButtonSpec> fewer = VbButtons();
    fewer.erase(fewer.begin() + 2);
    bar.SetBarButtons(fewer);
    CHECK(VbButton(bar, kVbIdControl) == NULL);
    CHECK(bar.BarWidthForTest() < wideW);
    for (size_t i = 1; i < fewer.size(); ++i)
    {
        balloonwjui::DuiButton* prev = VbButton(bar, fewer[i - 1].m_id);
        balloonwjui::DuiButton* cur = VbButton(bar, fewer[i].m_id);
        REQUIRE(prev != NULL);
        REQUIRE(cur != NULL);
        CHECK(cur->GetRect().left - prev->GetRect().right == kShareBarBtnGap);
    }
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

TEST_CASE("VB2 clicking a button posts its id to the notify window", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    const std::vector<ViewerBarButtonSpec> specs = VbButtons();
    for (size_t i = 0; i < specs.size(); ++i)
    {
        INFO("button " << specs[i].m_id);
        MSG msg;
        while (::PeekMessageW(&msg, notify, 0, 0, PM_REMOVE))
        {
        }
        VbClick(bar.m_hWnd, specs[i].m_id);
        bool got = false;
        while (::PeekMessageW(&msg, notify, kVbNotifyMsg, kVbNotifyMsg, PM_REMOVE))
        {
            got = true;
            CHECK(msg.wParam == static_cast<WPARAM>(specs[i].m_id));
        }
        CHECK(got);
    }
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

TEST_CASE("VB3 sits at the top center of the picture, follows it and hides with a minimized owner", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    CHECK(bar.IsWindowVisible());
    VbCheckTopCenter(bar, VbPictureRect(owner));
    //工具条以观看窗为所有者：在它上面、随它最小化
    CHECK(::GetWindow(bar.m_hWnd, GW_OWNER) == owner);

    //观看窗挪动、改变大小：工具条跟随
    const int dx = 150;
    const int dy = 60;
    const int dw = 200;
    ::SetWindowPos(owner, NULL, kVbOwnerX + dx, kVbOwnerY + dy, kVbOwnerW + dw, kVbOwnerH, SWP_NOZORDER | SWP_NOACTIVATE);
    bar.MoveTo(VbPictureRect(owner));
    VbCheckTopCenter(bar, VbPictureRect(owner));

    //观看窗最小化：工具条随之隐藏；还原后重新出现
    ::ShowWindow(owner, SW_SHOWMINNOACTIVE);
    CHECK_FALSE(bar.IsWindowVisible());
    ::ShowWindow(owner, SW_SHOWNOACTIVATE);
    CHECK(bar.IsWindowVisible());
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

TEST_CASE("VB4 fullscreen uses the monitor as the anchor, windowed mode goes back to the picture", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());

    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    REQUIRE(::GetMonitorInfoW(::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi));
    bar.MoveTo(mi.rcMonitor);
    VbCheckTopCenter(bar, mi.rcMonitor);
    bar.MoveTo(VbPictureRect(owner));
    VbCheckTopCenter(bar, VbPictureRect(owner));
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

TEST_CASE("VB5 collapse to a handle, hide and reveal at the top edge", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    const RECT anchor = VbPictureRect(owner);
    bar.Show(owner, notify, kVbNotifyMsg, anchor);
    REQUIRE(bar.IsWindow());
    RECT expanded;
    ::GetWindowRect(bar.m_hWnd, &expanded);

    //收起：窗口缩成顶边正中的小把手
    VbClick(bar.m_hWnd, CViewerFloatBar::kIdToggle);
    CHECK(bar.CollapseState() == top_bar_collapsed);
    RECT handle;
    ::GetWindowRect(bar.m_hWnd, &handle);
    CHECK(handle.bottom - handle.top == kTopBarHandleH);
    //鼠标移到把手上：临时展开
    POINT onHandle = { (handle.left + handle.right) / 2, (handle.top + handle.bottom) / 2 };
    bar.TickCollapseAt(1000, onHandle);
    CHECK(bar.CollapseState() == top_bar_peek);

    //隐藏：工具条与把手都不显示
    VbClick(bar.m_hWnd, CViewerFloatBar::kIdHide);
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(bar.IsWindowVisible());
    //鼠标推到参照矩形的顶边：临时显示整条
    POINT edge = { (expanded.left + expanded.right) / 2, anchor.top };
    bar.TickCollapseAt(2000, edge);
    CHECK(bar.CollapseState() == top_bar_hidden_peek);
    CHECK(bar.IsWindowVisible());
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

TEST_CASE("VB6 button screen rects for anchoring menus", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    RECT win;
    ::GetWindowRect(bar.m_hWnd, &win);
    RECT rc;
    REQUIRE(bar.GetButtonScreenRect(kVbIdViewers, rc));
    CHECK(rc.left >= win.left);
    CHECK(rc.right <= win.right);
    CHECK(rc.top >= win.top);
    CHECK(rc.bottom <= win.bottom);
    CHECK(rc.right > rc.left);
    const UINT kUnknownId = 999;
    CHECK_FALSE(bar.GetButtonScreenRect(kUnknownId, rc));
    bar.Hide();
    CHECK_FALSE(bar.GetButtonScreenRect(kVbIdViewers, rc));
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

// ======================= 屏幕共享观看窗接入浮动工具条（第 2c 步） =======================

namespace {

//中转公钥的字节数（用例不连中转，内容无所谓）
const int kVbPubKeyLen = 32;
//读源文件的块大小（字节）与从本文件往上几级是 flamingoclient 目录
const size_t kVbReadChunk = 4096;
const int kVbDepth = 3;

//把投给观看窗的消息处理完
void VbPump(HWND hwnd)
{
    MSG msg;
    while (::PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

//打开一个不连中转的观看窗（默认全屏）
bool VbOpenViewer(CShareViewerWindow& wnd, HWND notify)
{
    ShareRelayInfo relay;
    relay.m_roomId = "vb-room";
    unsigned char pubKey[kVbPubKeyLen] = { 0 };
    wnd.Open(relay, pubKey, L"host", notify, NULL, std::shared_ptr<balloonp2p::P2PLinkSlot>());
    if (!wnd.IsWindow())
        return false;
    VbPump(wnd.m_hWnd);
    return true;
}

//按顺序列出观看窗工具条上存在的按钮编号（只看 ids 里列出的这些），并检查它们从左到右排列
std::vector<UINT> VbPresent(CShareViewerWindow& wnd, const std::vector<UINT>& ids)
{
    std::vector<UINT> present;
    LONG lastLeft = LONG_MIN;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        RECT rc;
        if (!wnd.BarButtonRectForTest(ids[i], rc))
            continue;
        CHECK(rc.left > lastLeft);
        lastLeft = rc.left;
        present.push_back(ids[i]);
    }
    return present;
}

//观看窗工具条可能出现的全部按钮，按应有的从左到右顺序
std::vector<UINT> VbAllViewerIds()
{
    const UINT ids[] = { CShareViewerWindow::kBarMic, CShareViewerWindow::kBarCamera, CShareViewerWindow::kBarAnnotate,
                         CShareViewerWindow::kBarRecord, CShareViewerWindow::kBarCaption,
                         CShareViewerWindow::kBarControl, CShareViewerWindow::kBarShareMine,
                         CShareViewerWindow::kBarBoard, CShareViewerWindow::kBarViewers, CShareViewerWindow::kBarInfo,
                         CShareViewerWindow::kBarHost, CShareViewerWindow::kBarFullscreen,
                         CShareViewerWindow::kBarLeave };
    return std::vector<UINT>(ids, ids + sizeof(ids) / sizeof(ids[0]));
}

bool VbHas(const std::vector<UINT>& v, UINT id)
{
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i] == id)
            return true;
    }
    return false;
}

std::string VbReadSource(const std::string& rel)
{
    std::string self(__FILE__);
    for (int i = 0; i < kVbDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, (self + rel).c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kVbReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

} // namespace

TEST_CASE("VB7 share viewer bar: button set, order and conditional buttons", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    CShareViewerWindow wnd;
    REQUIRE(VbOpenViewer(wnd, notify));
    const std::vector<UINT> all = VbAllViewerIds();

    //默认：没有字幕、共享说明、主持人
    std::vector<UINT> present = VbPresent(wnd, all);
    CHECK(present.size() == all.size() - 3);
    CHECK_FALSE(VbHas(present, CShareViewerWindow::kBarCaption));
    CHECK_FALSE(VbHas(present, CShareViewerWindow::kBarInfo));
    CHECK_FALSE(VbHas(present, CShareViewerWindow::kBarHost));

    //字幕能使用、共享有描述、经移交成为主持人：三个按钮依次出现，仍按应有的顺序排列
    wnd.SetCaptionState(true, false, true);
    wnd.SetShareInfo(L"title", L"desc");
    wnd.SetHostTools(true, true, true);
    present = VbPresent(wnd, all);
    CHECK(present.size() == all.size());

    //再撤掉：按钮消失，其余的照常排列
    wnd.SetCaptionState(false, false, true);
    wnd.SetShareInfo(L"title", L"");
    wnd.SetHostTools(false, true, true);
    present = VbPresent(wnd, all);
    CHECK(present.size() == all.size() - 3);
    wnd.CloseViewer();
    ::DestroyWindow(notify);
}

TEST_CASE("VB8 share viewer title bar has no custom icons any more", "[viewerbar]")
{
    const std::string src = VbReadSource("\\FlamingoMedia\\Share\\ShareViewerWindow.cpp");
    REQUIRE_FALSE(src.empty());
    CHECK(src.find("AddCaptionIcon(") == std::string::npos);
    CHECK(src.find("DUIFW_CAPTION_ICON_CLICK") == std::string::npos);
    //全屏时借用的远程桌面文字按钮条也不再使用
    CHECK(src.find("CRdViewerToolbar") == std::string::npos);
}

TEST_CASE("VB9 becoming host while fullscreen shows the host button at once", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    CShareViewerWindow wnd;
    REQUIRE(VbOpenViewer(wnd, notify));
    REQUIRE(wnd.IsFullscreen());
    RECT rc;
    CHECK_FALSE(wnd.BarButtonRectForTest(CShareViewerWindow::kBarHost, rc));
    wnd.SetHostTools(true, true, true);
    CHECK(wnd.BarButtonRectForTest(CShareViewerWindow::kBarHost, rc));
    wnd.CloseViewer();
    ::DestroyWindow(notify);
}

TEST_CASE("VB10 the bar grows with a longer status text and shrinks back", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.SetStatusText(L"short");
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    const int shortW = bar.BarWidthForTest();
    //同样有状态区、只是文字变长：整条加宽
    bar.SetStatusText(L"a much longer status text that needs more room than the short one");
    CHECK(bar.BarWidthForTest() > shortW);
    CHECK(bar.StatusTextForTest() == L"a much longer status text that needs more room than the short one");
    //变回短文字：回到原宽
    bar.SetStatusText(L"short");
    CHECK(bar.BarWidthForTest() == shortW);
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

TEST_CASE("VB11 clicking the bar does not activate it", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    //在按钮所在的横条上按下、在顶部标题条上按下：都不激活
    const WORD hitTests[] = { HTCLIENT, HTCAPTION };
    for (size_t i = 0; i < sizeof(hitTests) / sizeof(hitTests[0]); ++i)
    {
        INFO("hit test " << hitTests[i]);
        const LRESULT ret = ::SendMessageW(bar.m_hWnd, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(owner),
                                           MAKELPARAM(hitTests[i], WM_LBUTTONDOWN));
        CHECK(ret == MA_NOACTIVATE);
    }
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

namespace {

//读窗口内一点的颜色（窗口坐标）；读不出时返回 CLR_INVALID
COLORREF VbWindowPixel(HWND wnd, int x, int y)
{
    HDC dc = ::GetWindowDC(wnd);
    if (dc == NULL)
        return CLR_INVALID;
    const COLORREF c = ::GetPixel(dc, x, y);
    ::ReleaseDC(wnd, dc);
    return c;
}

//工具条底色是深灰（ViewerFloatBar.cpp 的 kBarBgColor 为 RGB(44, 48, 56)），系统标题栏的几种颜色都是浅色：
//三个分量都不超过这个值就算深色
const int kVbDarkChannelMax = 100;

bool VbIsDark(COLORREF c)
{
    return c != CLR_INVALID && GetRValue(c) <= kVbDarkChannelMax && GetGValue(c) <= kVbDarkChannelMax &&
           GetBValue(c) <= kVbDarkChannelMax;
}

} // namespace

TEST_CASE("VB12 activation changes do not paint a system caption over the bar", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    ::UpdateWindow(bar.m_hWnd);
    //取标题条右侧的一点：左侧有标题文字，最右几个像素靠近圆角
    const int probeRightInset = 20;
    const int probeY = 4;
    RECT rc;
    ::GetWindowRect(bar.m_hWnd, &rc);
    const int probeX = (rc.right - rc.left) - probeRightInset;
    const COLORREF before = VbWindowPixel(bar.m_hWnd, probeX, probeY);
    if (!VbIsDark(before))
    {
        //当前桌面读不出窗口像素（隐藏桌面上常见）：无法判断，跳过
        WARN("window pixels are not readable on this desktop, VB12 skipped (run it on the visible desktop)");
    }
    else
    {
        const WPARAM states[] = { TRUE, FALSE };
        for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
        {
            INFO("WM_NCACTIVATE " << states[i]);
            ::SendMessageW(bar.m_hWnd, WM_NCACTIVATE, states[i], 0);
            const COLORREF after = VbWindowPixel(bar.m_hWnd, probeX, probeY);
            INFO("pixel after: " << static_cast<int>(GetRValue(after)) << "," << static_cast<int>(GetGValue(after)) << ","
                                 << static_cast<int>(GetBValue(after)));
            CHECK(VbIsDark(after));
        }
    }
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

namespace {

//VB13 里把悬停提示的弹出延时临时调成的值（毫秒）：尽快弹出，用例结束后恢复原值
const UINT kVbTipDelayMs = 1;
//等提示框弹出的最长时间（毫秒）与每轮等待的间隔（毫秒）
const ULONGLONG kVbTipWaitMs = 2000;
const DWORD kVbTipPollMs = 5;

//处理本线程的消息（提示框靠线程计时器弹出），直到提示框显示出来或超时；返回提示框是否在显示
bool VbPumpUntilTipShows()
{
    balloonwjui::DuiToolTipMgr& tt = balloonwjui::DuiToolTipMgr::Inst();
    const ULONGLONG deadline = ::GetTickCount64() + kVbTipWaitMs;
    while (!tt.IsShowing() && ::GetTickCount64() < deadline)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(kVbTipPollMs);
    }
    return tt.IsShowing();
}

} // namespace

TEST_CASE("VB13 a tooltip shown over the bar is closed when the bar goes away", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    balloonwjui::DuiButton* btn = VbButton(bar, kVbIdMic);
    REQUIRE(btn != NULL);
    RECT btnRc;
    REQUIRE(bar.GetButtonScreenRect(kVbIdMic, btnRc));
    POINT pt;
    pt.x = (btnRc.left + btnRc.right) / 2;
    pt.y = (btnRc.top + btnRc.bottom) / 2;

    //模拟鼠标停在按钮上，等它的悬停提示弹出
    balloonwjui::DuiToolTipMgr& tt = balloonwjui::DuiToolTipMgr::Inst();
    const UINT oldDelay = tt.GetDelay();
    tt.SetDelay(kVbTipDelayMs);
    tt.OnHover(btn, pt);
    const bool shown = VbPumpUntilTipShows();
    tt.SetDelay(oldDelay);
    REQUIRE(shown);

    SECTION("the bar is hidden")
    {
        bar.Hide();
        CHECK_FALSE(tt.IsShowing());
    }
    SECTION("the owner window is destroyed")
    {
        //观看窗关闭时系统连带销毁它拥有的工具条，不经过 Hide
        ::DestroyWindow(owner);
        CHECK_FALSE(bar.IsWindow());
        CHECK_FALSE(tt.IsShowing());
    }
    //用例失败时也不把提示框留给后面的用例
    tt.HideNow();
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

namespace {

//VB14 的弹出延时与等待时长（毫秒）：等待明显长于延时，没被取消的提示必然弹出
const UINT kVbPressDelayMs = 100;
const ULONGLONG kVbPressWaitMs = 400;

//同 VbPumpUntilTipShows，但丢掉 WM_MOUSELEAVE：真实光标不在测试窗口上，TrackMouseEvent 会立刻报告离开，
//宿主据此取消提示；丢掉它才能模拟「光标一直停在按钮上」
bool VbPumpKeepingHover(ULONGLONG waitMs)
{
    balloonwjui::DuiToolTipMgr& tt = balloonwjui::DuiToolTipMgr::Inst();
    const ULONGLONG deadline = ::GetTickCount64() + waitMs;
    while (!tt.IsShowing() && ::GetTickCount64() < deadline)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_MOUSELEAVE)
                continue;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(kVbTipPollMs);
    }
    return tt.IsShowing();
}

} // namespace

TEST_CASE("VB14 pressing a bar button before its tooltip pops up cancels the tooltip", "[viewerbar]")
{
    const HWND notify = VbNotifyWindow();
    const HWND owner = VbOwnerWindow();
    CViewerFloatBar bar;
    bar.SetBarButtons(VbButtons());
    bar.Show(owner, notify, kVbNotifyMsg, VbPictureRect(owner));
    REQUIRE(bar.IsWindow());
    RECT btnRc;
    REQUIRE(bar.GetButtonScreenRect(kVbIdMic, btnRc));
    POINT pt;
    pt.x = (btnRc.left + btnRc.right) / 2;
    pt.y = (btnRc.top + btnRc.bottom) / 2;
    ::ScreenToClient(bar.m_hWnd, &pt);
    const LPARAM lp = MAKELPARAM(pt.x, pt.y);
    balloonwjui::DuiToolTipMgr& tt = balloonwjui::DuiToolTipMgr::Inst();
    const UINT oldDelay = tt.GetDelay();
    tt.SetDelay(kVbPressDelayMs);

    //前提：只悬停时提示会弹出
    ::SendMessageW(bar.m_hWnd, WM_MOUSEMOVE, 0, lp);
    const bool hoverShows = VbPumpKeepingHover(kVbPressWaitMs);
    tt.OnLeave(NULL);
    ::SendMessageW(bar.m_hWnd, WM_MOUSELEAVE, 0, 0);

    //移到按钮上、在延时之内按下：之后提示不再弹出（原来会在点开的菜单上面冒出来）
    ::SendMessageW(bar.m_hWnd, WM_MOUSEMOVE, 0, lp);
    ::SendMessageW(bar.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    ::SendMessageW(bar.m_hWnd, WM_LBUTTONUP, 0, lp);
    const bool shownAfterPress = VbPumpKeepingHover(kVbPressWaitMs);
    tt.OnLeave(NULL);
    tt.SetDelay(oldDelay);
    CHECK(hoverShows);
    CHECK_FALSE(shownAfterPress);
    bar.Hide();
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}
