/**
 *  群通话窗、屏幕共享观看窗在弹出菜单期间被关闭的单元测试（标签 [mediamenu]），MediaMenuDestroyTests.cpp
 *
 *  背景：2026-10-07 修共享控制条「更多」菜单开着时结束共享的问题（docs/屏幕共享与录制方案.md 13.1，用例 SBMN1~SBMN8）
 *  之后，核查同类写法：群通话窗的「成员管理」菜单、屏幕共享观看窗标题栏的「字幕」菜单同样在本窗口的消息处理里同步
 *  TrackPopup（嵌套消息循环），而菜单开着时网络事件照常处理：通话结束、共享结束时控制器关闭这两个窗口
 *  （CGroupCallWindow::Close、CShareViewerWindow::CloseViewer 里 DestroyWindow），窗口对象是控制器的成员、之后还会再用。
 *  录制管理窗口的两处右键菜单只在用户关窗或程序退出时才会被关闭，菜单开着时碰不到，不在本文件里。
 *
 *  覆盖：
 *    MMD1 群通话窗「成员管理」菜单开着时通话窗被关闭：菜单随之关闭，窗口句柄始终是有效窗口或空，不留失效句柄；
 *    MMD2 屏幕共享观看窗「字幕」菜单开着时观看窗被关闭：同上；
 *    MMD3 群通话窗菜单开着时关闭、紧接着再打开（通话结束后马上又开始下一场）：复用同一个窗口、不再 Create，菜单关闭后
 *         窗口仍在且可见；
 *    MMD4 观看窗同上（共享结束后马上又加入下一场）。
 *  修改前（只在本窗口的消息处理里同步 TrackPopup、关闭时直接 DestroyWindow）MMD1 三项都失败（句柄失效、菜单留在屏幕上），
 *  MMD2 句柄失效（菜单随观看窗失去激活而关闭）；修改后两个窗口接入 CShareBarPopupGuard（tmp/verify7/step3_menu.log）。
 *  MMD3、MMD4 只在修改后运行：修改前再 Create 会触发 atlwin.h 的断言，测试进程随之崩溃。
 *  做法与 ShareBarMenuTests.cpp 相同：线程定时器在菜单的嵌套消息循环里驱动，菜单弹出后模拟控制器关闭窗口，
 *  下一拍观察菜单与窗口句柄。运行时会弹出窗口与菜单，须放在不可见桌面上运行。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>

#include "../../FlamingoMedia/GroupCall/GroupCallMessages.h"
#include "../../FlamingoMedia/GroupCall/GroupCallWindow.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareViewerWindow.h"

namespace {

//驱动用例的线程定时器间隔（毫秒）
const UINT kMmdStepMs = 150;
//等菜单弹出的最多拍数：超过仍没弹出就结束用例（断言 m_menuOpened 失败）
const int kMmdMaxWaitSteps = 20;
//整个用例的超时（毫秒）
const DWORD kMmdTimeoutMs = 10000;
//DuiMenu 弹出窗口的窗口类名（balloonui DuiMenu.cpp 的 DECLARE_WND_CLASS_EX）
const wchar_t* const kMmdMenuClass = L"__DuiMenuPopup__";
//GetClassNameW 的缓冲区长度（字符）
const int kMmdClassBuf = 64;
//群通话里本人与另一位成员的 userid、座位号
const int64_t kMmdSelfUserId = 1001;
const int64_t kMmdPeerUserId = 1002;
const int kMmdSelfSeat = 1;
const int kMmdPeerSeat = 2;
//观看窗打开时用的服务端公钥长度（字节，libsodium crypto_box 公钥长度）；内容全 0，收流管线连不上也没关系
const size_t kMmdPubKeyLen = 32;

//被测的窗口：打开、请求弹出菜单、关闭、取窗口句柄
class IMmdWindow
{
public:
    virtual ~IMmdWindow()
    {
    }
    //建窗并显示，命令与事件发给 notify；成功时返回 true
    virtual bool Open(HWND notify) = 0;
    //请求弹出菜单（与点了按钮 / 标题栏图标时一样，投递给窗口自己）
    virtual void RequestMenu() = 0;
    //关闭窗口（控制器在通话结束、共享结束时调用的那个函数）
    virtual void Close() = 0;
    //窗口当前的句柄（m_hWnd）
    virtual HWND Handle() const = 0;
};

class MmdGroupCallWindow : public IMmdWindow
{
public:
    virtual bool Open(HWND notify) override
    {
        groupcall::GroupCallWindowView view;
        view.m_title = L"MMD group call";
        view.m_canManage = true;
        view.m_joinedCount = 2;
        groupcall::GroupCallTile self;
        self.m_userId = kMmdSelfUserId;
        self.m_seat = kMmdSelfSeat;
        self.m_self = true;
        groupcall::GroupCallTile peer;
        peer.m_userId = kMmdPeerUserId;
        peer.m_seat = kMmdPeerSeat;
        view.m_grid.push_back(self);
        view.m_grid.push_back(peer);
        view.m_names[kMmdSelfUserId] = L"self";
        view.m_names[kMmdPeerUserId] = L"peer";
        m_wnd.Open(NULL, notify, NULL, view);
        return m_wnd.IsWindow() != FALSE;
    }
    virtual void RequestMenu() override
    {
        ::PostMessageW(m_wnd.m_hWnd, groupcall::kMsgGroupCallWindowMenu, 0, 0);
    }
    virtual void Close() override
    {
        m_wnd.Close();
    }
    virtual HWND Handle() const override
    {
        return m_wnd.m_hWnd;
    }

    groupcall::CGroupCallWindow m_wnd;   //被测的群通话窗
};

class MmdShareViewerWindow : public IMmdWindow
{
public:
    virtual bool Open(HWND notify) override
    {
        screenshare::ShareRelayInfo relay;
        relay.m_roomId = "mmd-room";
        unsigned char pubKey[kMmdPubKeyLen] = { 0 };
        //没有中转地址，收流管线启动失败、返回 false，窗口照样建好并显示
        m_wnd.Open(relay, pubKey, L"host", notify, NULL, std::shared_ptr<balloonp2p::P2PLinkSlot>());
        //字幕可用时标题栏才有「字幕」图标、才弹得出字幕菜单
        m_wnd.SetCaptionState(true, false, true);
        return m_wnd.IsWindow() != FALSE;
    }
    virtual void RequestMenu() override
    {
        ::PostMessageW(m_wnd.m_hWnd, screenshare::CShareViewerWindow::WM_SHARE_VIEWER_CAPTION_MENU, 0, 0);
    }
    virtual void Close() override
    {
        m_wnd.CloseViewer();
    }
    virtual HWND Handle() const override
    {
        return m_wnd.m_hWnd;
    }

    screenshare::CShareViewerWindow m_wnd;   //被测的屏幕共享观看窗
};

//菜单弹出后做什么
enum MmdAction
{
    mmd_close = 0,              //关闭窗口（通话结束、共享结束）
    mmd_close_then_open = 1,    //关闭后立即再打开（结束后马上开始下一场）
};

//一次用例的进度与观察结果；线程定时器回调经 s_mmdProbe 访问
struct MmdProbe
{
    IMmdWindow* m_wnd;              //被测的窗口（不拥有）
    HWND        m_notify;           //通知窗口（再打开时用）
    int         m_action;           //菜单弹出后做什么，取值见 MmdAction
    HWND        m_handleBeforeClose;    //关窗之前窗口的句柄
    HWND        m_handleAfterOpen;  //mmd_close_then_open：再打开后窗口的句柄
    int         m_step;             //状态机：0 等菜单弹出，1 已关窗、观察，2 结束
    int         m_waited;           //等菜单弹出已经等了几拍
    bool        m_menuOpened;       //菜单弹出过
    bool        m_staleAfterClose;  //关窗后窗口句柄不为空却已不是有效窗口（再次建窗会断言）
    bool        m_menuOpenAfterClose;   //关窗一拍之后菜单弹出窗口仍然可见
    bool        m_handleUsable;     //关窗一拍之后窗口句柄为空或仍是有效窗口
    bool        m_done;             //用例结束

    MmdProbe()
        : m_wnd(NULL)
        , m_notify(NULL)
        , m_action(mmd_close)
        , m_handleBeforeClose(NULL)
        , m_handleAfterOpen(NULL)
        , m_step(0)
        , m_waited(0)
        , m_menuOpened(false)
        , m_staleAfterClose(false)
        , m_menuOpenAfterClose(false)
        , m_handleUsable(false)
        , m_done(false)
    {
    }
};

MmdProbe* s_mmdProbe = NULL;

//EnumThreadWindows 的回调：数本线程里可见的菜单弹出窗口；lParam 为 int*
BOOL CALLBACK MmdCountMenus(HWND hwnd, LPARAM lParam)
{
    wchar_t cls[kMmdClassBuf] = { 0 };
    ::GetClassNameW(hwnd, cls, kMmdClassBuf);
    if (wcscmp(cls, kMmdMenuClass) == 0 && ::IsWindowVisible(hwnd))
        ++*reinterpret_cast<int*>(lParam);
    return TRUE;
}

//EnumThreadWindows 的回调：给本线程里的菜单弹出窗口投 WM_CLOSE（用例收尾）
BOOL CALLBACK MmdCloseMenus(HWND hwnd, LPARAM lParam)
{
    (void)lParam;
    wchar_t cls[kMmdClassBuf] = { 0 };
    ::GetClassNameW(hwnd, cls, kMmdClassBuf);
    if (wcscmp(cls, kMmdMenuClass) == 0)
        ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

int MmdVisibleMenus()
{
    int count = 0;
    ::EnumThreadWindows(::GetCurrentThreadId(), MmdCountMenus, reinterpret_cast<LPARAM>(&count));
    return count;
}

//线程定时器回调：菜单弹出后关闭窗口并记录句柄状态，下一拍观察菜单是否还在
VOID CALLBACK MmdStep(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    (void)hwnd;
    (void)msg;
    (void)id;
    (void)time;
    MmdProbe* p = s_mmdProbe;
    if (p == NULL || p->m_done)
        return;
    switch (p->m_step)
    {
    //等菜单弹出；弹出后模拟控制器在菜单的消息循环里关闭窗口（通话结束、共享结束）
    case 0:
        if (MmdVisibleMenus() == 0)
        {
            if (++p->m_waited > kMmdMaxWaitSteps)
                p->m_done = true;
            return;
        }
        p->m_menuOpened = true;
        p->m_handleBeforeClose = p->m_wnd->Handle();
        p->m_wnd->Close();
        p->m_staleAfterClose = (p->m_wnd->Handle() != NULL && !::IsWindow(p->m_wnd->Handle()));
        if (p->m_action == mmd_close_then_open)
        {
            p->m_wnd->Open(p->m_notify);
            p->m_handleAfterOpen = p->m_wnd->Handle();
        }
        p->m_step = 1;
        return;
    //关窗一拍之后：菜单应已关闭，句柄应可用；再收尾关掉残留的菜单
    case 1:
        p->m_menuOpenAfterClose = MmdVisibleMenus() > 0;
        p->m_handleUsable = (p->m_wnd->Handle() == NULL || ::IsWindow(p->m_wnd->Handle()));
        ::EnumThreadWindows(::GetCurrentThreadId(), MmdCloseMenus, 0);
        p->m_step = 2;
        return;
    //结束
    default:
        p->m_done = true;
        return;
    }
}

//跑一遍：打开、请求弹出菜单、在菜单开着时按 action 动作，返回观察结果；visibleAtEnd 返回结束时窗口是否可见
MmdProbe MmdRun(IMmdWindow& wnd, int action, bool* visibleAtEnd)
{
    MmdProbe probe;
    probe.m_wnd = &wnd;
    probe.m_action = action;
    s_mmdProbe = &probe;
    const HWND notify =
        ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
    probe.m_notify = notify;
    if (!wnd.Open(notify))
    {
        ::DestroyWindow(notify);
        s_mmdProbe = NULL;
        return probe;
    }
    const UINT_PTR timer = ::SetTimer(NULL, 0, kMmdStepMs, MmdStep);
    wnd.RequestMenu();
    const DWORD start = ::GetTickCount();
    MSG msg;
    while (!probe.m_done && ::GetTickCount() - start < kMmdTimeoutMs)
    {
        if (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        else
        {
            ::MsgWaitForMultipleObjects(0, NULL, FALSE, kMmdStepMs, QS_ALLINPUT);
        }
    }
    ::KillTimer(NULL, timer);
    //把收尾投出的 WM_CLOSE 与推迟的销毁处理完
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    if (visibleAtEnd != NULL)
        *visibleAtEnd = wnd.Handle() != NULL && ::IsWindow(wnd.Handle()) && ::IsWindowVisible(wnd.Handle());
    wnd.Close();
    ::DestroyWindow(notify);
    s_mmdProbe = NULL;
    return probe;
}

} // namespace

TEST_CASE("MMD1 closing the group call window while its manage menu is open", "[mediamenu]")
{
    MmdGroupCallWindow wnd;
    const MmdProbe p = MmdRun(wnd, mmd_close, NULL);
    REQUIRE(p.m_menuOpened);
    CHECK_FALSE(p.m_staleAfterClose);
    CHECK_FALSE(p.m_menuOpenAfterClose);
    CHECK(p.m_handleUsable);
}

TEST_CASE("MMD2 closing the share viewer window while its caption menu is open", "[mediamenu]")
{
    MmdShareViewerWindow wnd;
    const MmdProbe p = MmdRun(wnd, mmd_close, NULL);
    REQUIRE(p.m_menuOpened);
    CHECK_FALSE(p.m_staleAfterClose);
    CHECK_FALSE(p.m_menuOpenAfterClose);
    CHECK(p.m_handleUsable);
}

TEST_CASE("MMD3 reopening the group call window right after closing it during its menu reuses the window",
          "[mediamenu]")
{
    MmdGroupCallWindow wnd;
    bool visible = false;
    const MmdProbe p = MmdRun(wnd, mmd_close_then_open, &visible);
    REQUIRE(p.m_menuOpened);
    CHECK(p.m_handleAfterOpen == p.m_handleBeforeClose);
    CHECK_FALSE(p.m_menuOpenAfterClose);
    CHECK(p.m_handleUsable);
    CHECK(visible);
}

TEST_CASE("MMD4 reopening the share viewer right after closing it during its caption menu reuses the window",
          "[mediamenu]")
{
    MmdShareViewerWindow wnd;
    bool visible = false;
    const MmdProbe p = MmdRun(wnd, mmd_close_then_open, &visible);
    REQUIRE(p.m_menuOpened);
    CHECK(p.m_handleAfterOpen == p.m_handleBeforeClose);
    CHECK_FALSE(p.m_menuOpenAfterClose);
    CHECK(p.m_handleUsable);
    CHECK(visible);
}
