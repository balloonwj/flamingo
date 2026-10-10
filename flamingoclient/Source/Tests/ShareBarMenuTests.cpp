/**
 *  共享控制条、录制条、标注工具条弹出菜单期间被关闭的单元测试（标签 [sharebarmenu]），ShareBarMenuTests.cpp
 *  背景：2026-10-07 用户实测，共享控制条的「更多」菜单开着时点「结束共享」，菜单留在屏幕上关不掉；再次发起共享时
 *  FlamingoMedia.dll 在 atlwin.h 断言 m_hWnd == 0。原因是菜单在按钮点击通知里同步 TrackPopup（嵌套消息循环），控制器
 *  在这个循环里收到「结束共享」后销毁控制条：ATL 要等最外层消息返回才清 m_hWnd，句柄于是失效而成员不为空；菜单弹出窗口
 *  没有所有者，也不随控制条销毁而关闭。修正前 SBMN1~SBMN3 在三个条上都失败（tmp/media7/logs/repro_sharebarmenu.log）。
 *  覆盖：
 *    - SBMN1 共享控制条「更多」菜单开着时隐藏控制条：菜单随之关闭，控制条的窗口句柄始终是有效窗口或空，不留失效句柄；
 *    - SBMN2 录制条「鼠标效果」菜单开着时隐藏录制条：同上；
 *    - SBMN3 标注工具条「绘图工具」菜单开着时隐藏工具条：同上；
 *    - SBMN4 菜单开着时隐藏、紧接着再显示（结束共享后马上再次发起）：复用同一个窗口、不再 Create，菜单关闭后窗口仍在且可见；
 *    - SBMN5 菜单开着时再点一下弹出菜单的按钮：菜单关闭，条保持显示；
 *    - SBMN6 子菜单开着时隐藏：根菜单与子菜单都关闭；
 *    - SBMN7 纯函数：弹出期间推迟销毁；被打断、没有选择、通知窗口无效时都不交出选择；
 *    - SBMN8 共享控制条「更多」菜单开着时字幕变为可用（字幕模块在这期间加载完成）：菜单关闭后控制条随即重建，
 *      多出「为我的发言生成字幕」按钮、整条变宽（2026-10-07 补；原先要等下一次状态变化或重新显示才重建）。
 *  框架 Catch2 v3。用例在嵌套消息循环里由线程定时器驱动，运行时会弹出菜单窗口，须放在不可见桌面上运行。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Record/RecordBar.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../FlamingoMedia/Share/ShareBarPopupGuard.h"
#include "../../FlamingoMedia/Share/ShareControlBar.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace {

//各条的参照矩形（屏幕坐标）
const RECT kSbmnAnchor = { 0, 0, 1600, 900 };
//驱动用例的线程定时器间隔（毫秒）
const UINT kSbmnStepMs = 150;
//等菜单弹出的最多拍数：超过仍没弹出就结束用例（断言 menuOpened 失败）
const int kSbmnMaxWaitSteps = 20;
//整个用例的超时（毫秒）
const DWORD kSbmnTimeoutMs = 10000;
//DuiMenu 弹出窗口的窗口类名（balloonui DuiMenu.cpp 的 DECLARE_WND_CLASS_EX）
const wchar_t* const kSbmnMenuClass = L"__DuiMenuPopup__";
//GetClassNameW 的缓冲区长度（字符）
const int kSbmnClassBuf = 64;
//打开子菜单时最多试几次（每次把悬停项往上挪一项再按右方向键）
const int kSbmnSubmenuTries = 12;

//菜单弹出后做什么
enum SbmnAction
{
    sbmn_hide = 0,            //隐藏条（控制器结束共享、结束录制、退出标注）
    sbmn_hide_then_show = 1,  //隐藏后立即再显示（结束共享后马上再次发起）
    sbmn_click_again = 2,     //再点一下弹出菜单的按钮
    sbmn_submenu_hide = 3,    //先打开一个子菜单，再隐藏条
    sbmn_caption_available = 4,   //字幕变为可用（只对共享控制条有意义），不隐藏条
};

//被测的条：显示、点开菜单、隐藏、取窗口句柄
class ISbmnBar
{
public:
    virtual ~ISbmnBar()
    {
    }
    //显示在 kSbmnAnchor 上，命令发给 notify
    virtual void Show(HWND notify) = 0;
    //模拟点一下弹出菜单的按钮
    virtual void ClickMenuButton() = 0;
    //隐藏（控制器结束共享、结束录制、退出标注时调用的那个函数）
    virtual void Hide() = 0;
    //条当前的窗口句柄（m_hWnd）
    virtual HWND Handle() const = 0;
    //字幕变为可用（控制器在字幕模块加载完成时调用的那个函数）；没有字幕按钮的条不做任何事
    virtual void MakeCaptionAvailable()
    {
    }
};

//向某个条发一条按钮点击通知（与 DuiHost 发出的格式相同）
void SbmnClick(HWND bar, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(bar, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

class SbmnShareBar : public ISbmnBar
{
public:
    virtual void Show(HWND notify) override
    {
        m_bar.Show(kSbmnAnchor, notify);
    }
    virtual void ClickMenuButton() override
    {
        SbmnClick(m_bar.m_hWnd, screenshare::CShareControlBar::kIdMore);
    }
    virtual void Hide() override
    {
        m_bar.Hide();
    }
    virtual HWND Handle() const override
    {
        return m_bar.m_hWnd;
    }
    virtual void MakeCaptionAvailable() override
    {
        m_bar.SetCaptionState(true, false, false);
    }

    screenshare::CShareControlBar m_bar;   //被测的共享控制条
};

class SbmnRecordBar : public ISbmnBar
{
public:
    virtual void Show(HWND notify) override
    {
        m_bar.Show(kSbmnAnchor, notify, 0);
    }
    virtual void ClickMenuButton() override
    {
        SbmnClick(m_bar.m_hWnd, screenrecord::CRecordBar::kIdMouse);
    }
    virtual void Hide() override
    {
        m_bar.Hide();
    }
    virtual HWND Handle() const override
    {
        return m_bar.m_hWnd;
    }

    screenrecord::CRecordBar m_bar;   //被测的录制条
};

class SbmnAnnotBar : public ISbmnBar
{
public:
    virtual void Show(HWND notify) override
    {
        m_bar.Show(kSbmnAnchor, notify, true);
    }
    virtual void ClickMenuButton() override
    {
        SbmnClick(m_bar.m_hWnd, screenshare::CShareAnnotationBar::kIdTools);
    }
    virtual void Hide() override
    {
        m_bar.Hide();
    }
    virtual HWND Handle() const override
    {
        return m_bar.m_hWnd;
    }

    screenshare::CShareAnnotationBar m_bar;   //被测的标注工具条
};

//一次用例的进度与观察结果；线程定时器回调经 s_sbmnProbe 访问
struct SbmnProbe
{
    ISbmnBar* m_bar;            //被测的条（不拥有）
    HWND      m_notify;         //通知窗口（再显示时用）
    int       m_action;         //菜单弹出后做什么，取值见 SbmnAction
    int       m_step;           //状态机：0 等菜单弹出，1 已动作、观察，2 结束
    int       m_waited;         //等菜单弹出已经等了几拍
    bool      m_menuOpened;     //菜单弹出过
    int       m_menusBeforeAction;  //动作之前可见的菜单窗口个数（打开子菜单后应为 2）
    bool      m_staleAfterHide; //动作后窗口句柄不为空却已不是有效窗口（再次 Create 会断言）
    bool      m_menuOpenAfterHide; //动作一拍之后菜单弹出窗口仍然可见
    bool      m_handleUsable;   //动作一拍之后窗口句柄为空或仍是有效窗口
    HWND      m_handleAfterShow;    //sbmn_hide_then_show：再显示后条的窗口句柄
    HWND      m_handleBeforeHide;   //动作之前条的窗口句柄
    int       m_widthBeforeAction;  //动作之前条的窗口宽度（像素）
    int       m_widthAtEnd;     //菜单关闭、消息处理完之后条的窗口宽度（像素）；条已不是有效窗口时为 0
    bool      m_done;           //用例结束

    SbmnProbe()
        : m_bar(NULL)
        , m_notify(NULL)
        , m_action(sbmn_hide)
        , m_step(0)
        , m_waited(0)
        , m_menuOpened(false)
        , m_menusBeforeAction(0)
        , m_staleAfterHide(false)
        , m_menuOpenAfterHide(false)
        , m_handleUsable(false)
        , m_handleAfterShow(NULL)
        , m_handleBeforeHide(NULL)
        , m_widthBeforeAction(0)
        , m_widthAtEnd(0)
        , m_done(false)
    {
    }
};

SbmnProbe* s_sbmnProbe = NULL;

//EnumThreadWindows 的回调：数本线程里可见的菜单弹出窗口；lParam 为 int*
BOOL CALLBACK SbmnCountMenus(HWND hwnd, LPARAM lParam)
{
    wchar_t cls[kSbmnClassBuf] = { 0 };
    ::GetClassNameW(hwnd, cls, kSbmnClassBuf);
    if (wcscmp(cls, kSbmnMenuClass) == 0 && ::IsWindowVisible(hwnd))
        ++*reinterpret_cast<int*>(lParam);
    return TRUE;
}

//EnumThreadWindows 的回调：给本线程里的菜单弹出窗口投 WM_CLOSE（用例收尾）
BOOL CALLBACK SbmnCloseMenus(HWND hwnd, LPARAM lParam)
{
    (void)lParam;
    wchar_t cls[kSbmnClassBuf] = { 0 };
    ::GetClassNameW(hwnd, cls, kSbmnClassBuf);
    if (wcscmp(cls, kSbmnMenuClass) == 0)
        ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

//EnumThreadWindows 的回调：找第一个可见的菜单弹出窗口（根菜单先建，枚举顺序里在子菜单后面也没关系，只用来发按键）
BOOL CALLBACK SbmnFindMenu(HWND hwnd, LPARAM lParam)
{
    wchar_t cls[kSbmnClassBuf] = { 0 };
    ::GetClassNameW(hwnd, cls, kSbmnClassBuf);
    if (wcscmp(cls, kSbmnMenuClass) == 0 && ::IsWindowVisible(hwnd))
    {
        *reinterpret_cast<HWND*>(lParam) = hwnd;
        return FALSE;
    }
    return TRUE;
}

//窗口宽度（像素）；不是有效窗口时为 0
int SbmnWindowWidth(HWND hwnd)
{
    RECT rc;
    if (hwnd == NULL || !::IsWindow(hwnd) || !::GetWindowRect(hwnd, &rc))
        return 0;
    return rc.right - rc.left;
}

int SbmnVisibleMenus()
{
    int count = 0;
    ::EnumThreadWindows(::GetCurrentThreadId(), SbmnCountMenus, reinterpret_cast<LPARAM>(&count));
    return count;
}

//在根菜单里打开一个子菜单：悬停项往上挪一项再按右方向键，直到出现第二个菜单窗口
void SbmnOpenSubmenu()
{
    HWND root = NULL;
    ::EnumThreadWindows(::GetCurrentThreadId(), SbmnFindMenu, reinterpret_cast<LPARAM>(&root));
    if (root == NULL)
        return;
    for (int i = 0; i < kSbmnSubmenuTries && SbmnVisibleMenus() < 2; ++i)
    {
        ::SendMessageW(root, WM_KEYDOWN, VK_UP, 0);
        ::SendMessageW(root, WM_KEYDOWN, VK_RIGHT, 0);
    }
}

//线程定时器回调：菜单弹出后按 m_action 动作并记录句柄状态，下一拍观察菜单是否还在
VOID CALLBACK SbmnStep(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    (void)hwnd;
    (void)msg;
    (void)id;
    (void)time;
    SbmnProbe* p = s_sbmnProbe;
    if (p == NULL || p->m_done)
        return;
    switch (p->m_step)
    {
    //等菜单弹出；弹出后模拟控制器（或用户）在菜单的消息循环里动作
    case 0:
        if (SbmnVisibleMenus() == 0)
        {
            if (++p->m_waited > kSbmnMaxWaitSteps)
                p->m_done = true;
            return;
        }
        p->m_menuOpened = true;
        if (p->m_action == sbmn_submenu_hide)
            SbmnOpenSubmenu();
        p->m_menusBeforeAction = SbmnVisibleMenus();
        p->m_handleBeforeHide = p->m_bar->Handle();
        p->m_widthBeforeAction = SbmnWindowWidth(p->m_bar->Handle());
        if (p->m_action == sbmn_click_again)
        {
            p->m_bar->ClickMenuButton();
        }
        else if (p->m_action == sbmn_caption_available)
        {
            p->m_bar->MakeCaptionAvailable();
        }
        else
        {
            p->m_bar->Hide();
            p->m_staleAfterHide = (p->m_bar->Handle() != NULL && !::IsWindow(p->m_bar->Handle()));
            if (p->m_action == sbmn_hide_then_show)
            {
                p->m_bar->Show(p->m_notify);
                p->m_handleAfterShow = p->m_bar->Handle();
            }
        }
        p->m_step = 1;
        return;
    //动作一拍之后：菜单应已关闭，句柄应可用；再收尾关掉残留的菜单
    case 1:
        p->m_menuOpenAfterHide = SbmnVisibleMenus() > 0;
        p->m_handleUsable = (p->m_bar->Handle() == NULL || ::IsWindow(p->m_bar->Handle()));
        ::EnumThreadWindows(::GetCurrentThreadId(), SbmnCloseMenus, 0);
        p->m_step = 2;
        return;
    //结束
    default:
        p->m_done = true;
        return;
    }
}

//跑一遍：显示、点开菜单、在菜单开着时按 action 动作，返回观察结果；visibleAtEnd 返回结束时条是否可见
SbmnProbe SbmnRun(ISbmnBar& bar, int action, bool* visibleAtEnd)
{
    SbmnProbe probe;
    probe.m_bar = &bar;
    probe.m_action = action;
    s_sbmnProbe = &probe;
    const HWND notify =
        ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
    probe.m_notify = notify;
    bar.Show(notify);
    const UINT_PTR timer = ::SetTimer(NULL, 0, kSbmnStepMs, SbmnStep);
    bar.ClickMenuButton();
    const DWORD start = ::GetTickCount();
    MSG msg;
    while (!probe.m_done && ::GetTickCount() - start < kSbmnTimeoutMs)
    {
        if (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        else
        {
            ::MsgWaitForMultipleObjects(0, NULL, FALSE, kSbmnStepMs, QS_ALLINPUT);
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
        *visibleAtEnd = bar.Handle() != NULL && ::IsWindow(bar.Handle()) && ::IsWindowVisible(bar.Handle());
    probe.m_widthAtEnd = SbmnWindowWidth(bar.Handle());
    bar.Hide();
    ::DestroyWindow(notify);
    s_sbmnProbe = NULL;
    return probe;
}

} // namespace

TEST_CASE("SBMN1 hiding the share control bar while its more menu is open", "[sharebarmenu]")
{
    SbmnShareBar bar;
    const SbmnProbe p = SbmnRun(bar, sbmn_hide, NULL);
    REQUIRE(p.m_menuOpened);
    CHECK_FALSE(p.m_staleAfterHide);
    CHECK_FALSE(p.m_menuOpenAfterHide);
    CHECK(p.m_handleUsable);
    CHECK_FALSE(bar.m_bar.IsPopupActive());
}

TEST_CASE("SBMN2 hiding the record bar while its mouse effect menu is open", "[sharebarmenu]")
{
    SbmnRecordBar bar;
    const SbmnProbe p = SbmnRun(bar, sbmn_hide, NULL);
    REQUIRE(p.m_menuOpened);
    CHECK_FALSE(p.m_staleAfterHide);
    CHECK_FALSE(p.m_menuOpenAfterHide);
    CHECK(p.m_handleUsable);
    CHECK_FALSE(bar.m_bar.IsPopupActive());
}

TEST_CASE("SBMN3 hiding the annotation bar while its tools menu is open", "[sharebarmenu]")
{
    SbmnAnnotBar bar;
    const SbmnProbe p = SbmnRun(bar, sbmn_hide, NULL);
    REQUIRE(p.m_menuOpened);
    CHECK_FALSE(p.m_staleAfterHide);
    CHECK_FALSE(p.m_menuOpenAfterHide);
    CHECK(p.m_handleUsable);
    CHECK_FALSE(bar.m_bar.IsPopupActive());
}

TEST_CASE("SBMN4 showing a bar again right after hiding it during its menu reuses the window", "[sharebarmenu]")
{
    //共享控制条：结束共享后马上再次发起
    {
        SbmnShareBar bar;
        bool visible = false;
        const SbmnProbe p = SbmnRun(bar, sbmn_hide_then_show, &visible);
        REQUIRE(p.m_menuOpened);
        CHECK(p.m_handleAfterShow == p.m_handleBeforeHide);
        CHECK_FALSE(p.m_menuOpenAfterHide);
        CHECK(visible);
    }
    //录制条：结束录制后马上再次录制
    {
        SbmnRecordBar bar;
        bool visible = false;
        const SbmnProbe p = SbmnRun(bar, sbmn_hide_then_show, &visible);
        REQUIRE(p.m_menuOpened);
        CHECK(p.m_handleAfterShow == p.m_handleBeforeHide);
        CHECK(visible);
    }
    //标注工具条：退出标注后马上再进入
    {
        SbmnAnnotBar bar;
        bool visible = false;
        const SbmnProbe p = SbmnRun(bar, sbmn_hide_then_show, &visible);
        REQUIRE(p.m_menuOpened);
        CHECK(p.m_handleAfterShow == p.m_handleBeforeHide);
        CHECK(visible);
    }
}

TEST_CASE("SBMN5 clicking the menu button again closes the menu and keeps the bar", "[sharebarmenu]")
{
    SbmnShareBar share;
    bool visible = false;
    SbmnProbe p = SbmnRun(share, sbmn_click_again, &visible);
    REQUIRE(p.m_menuOpened);
    CHECK_FALSE(p.m_menuOpenAfterHide);
    CHECK(visible);
    SbmnAnnotBar annot;
    p = SbmnRun(annot, sbmn_click_again, &visible);
    REQUIRE(p.m_menuOpened);
    CHECK_FALSE(p.m_menuOpenAfterHide);
    CHECK(visible);
}

TEST_CASE("SBMN6 hiding a bar while a submenu is open closes both menus", "[sharebarmenu]")
{
    SbmnShareBar bar;
    const SbmnProbe p = SbmnRun(bar, sbmn_submenu_hide, NULL);
    REQUIRE(p.m_menuOpened);
    CHECK(p.m_menusBeforeAction == 2);
    CHECK_FALSE(p.m_staleAfterHide);
    CHECK_FALSE(p.m_menuOpenAfterHide);
    CHECK(p.m_handleUsable);
}

TEST_CASE("SBMN7 popup guard policies", "[sharebarmenu]")
{
    CHECK(screenshare::ShareBarShouldDeferDestroy(true));
    CHECK_FALSE(screenshare::ShareBarShouldDeferDestroy(false));
    const UINT kChosen = 101;
    CHECK(screenshare::ShareBarShouldDeliverChoice(kChosen, false, true));
    CHECK_FALSE(screenshare::ShareBarShouldDeliverChoice(0, false, true));
    CHECK_FALSE(screenshare::ShareBarShouldDeliverChoice(kChosen, true, true));
    CHECK_FALSE(screenshare::ShareBarShouldDeliverChoice(kChosen, false, false));
    //推迟销毁的标记：取走后清除；被打断的标记保留到下一次弹出
    screenshare::CShareBarPopupGuard guard;
    CHECK_FALSE(guard.IsActive());
    guard.BeginDialog();
    CHECK(guard.IsActive());
    guard.SetPendingDestroy();
    guard.End();
    CHECK_FALSE(guard.IsActive());
    CHECK(guard.WasInterrupted());
    CHECK(guard.TakePendingDestroy());
    CHECK_FALSE(guard.TakePendingDestroy());
    guard.BeginDialog();
    CHECK_FALSE(guard.WasInterrupted());
    guard.End();
}

TEST_CASE("SBMN8 caption becoming available while the more menu is open rebuilds the bar after the menu closes",
          "[sharebarmenu]")
{
    SbmnShareBar bar;
    bool visible = false;
    const SbmnProbe p = SbmnRun(bar, sbmn_caption_available, &visible);
    REQUIRE(p.m_menuOpened);
    CHECK(visible);
    INFO("width before " << p.m_widthBeforeAction << ", width at end " << p.m_widthAtEnd);
    REQUIRE(p.m_widthBeforeAction > 0);
    //多了「为我的发言生成字幕」按钮，整条变宽
    CHECK(p.m_widthAtEnd > p.m_widthBeforeAction);
    CHECK_FALSE(bar.m_bar.IsPopupActive());
}
