/**
 *  主面板列表滚轮重绘次数的单元测试（Suite MPW），MainPanelWheelRepaintTests.cpp
 *
 *  背景：主面板三个列表用的 MainPanelScrollView 在滚轮处理里先让基类完成滚动（基类
 *  会失效视口并立即 UpdateWindow 画出新位置），之后又对整个视口调一次 Invalidate，
 *  于是每滚一格列表要画两遍。悬浮滚动条的透明度只在定时器里变化、每次变化自己会
 *  Invalidate，滑块的新位置也已由基类那次重绘画出，这次失效是多余的。
 *
 *  覆盖：
 *    MPW1 滚一格之后宿主窗口没有剩余的待重绘区域，滚动位置确实变了；
 *    MPW2 已在顶端时再往上滚，不产生待重绘区域，但滚动条照样被唤出；
 *    MPW3 滚动条已处于显现态时再滚轮，不会把淡入动画定时器的计时清零；
 *    MPW4 滚动条隐藏、宿主停掉定时器之后再滚轮，定时器会被重新启动。
 *
 *  MPW3 的背景：同一窗口上以同一 id 再次调用 SetTimer 会替换原定时器并把计时清零。
 *  原先每条滚轮消息都调一次 SetTimer，滚轮消息间隔小于定时器周期（16 ms）时——快速
 *  拨动滚轮、触摸板连续滑动——定时器永远等不到触发，滚动条要等停下来才会淡入。
 *
 *  做法：宿主是放在屏幕外的可见弹出窗口（不可见窗口不维护待重绘区域），滚轮消息
 *  直接 SendMessage 给宿主，返回后向系统询问待重绘区域。框架 Catch2 v3。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../UI/BalloonMain/MainPanelScrollView.h"

#include <memory>

using balloonwjui::DuiControl;
using balloonwjui::DuiHost;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonmain::MainPanelScrollView;

namespace {

const int kMpwHostSize  = 300;      // 宿主客户区边长（像素）
const int kMpwRowH      = 100;      // 内容每行高度（像素）
const int kMpwRows      = 20;       // 内容行数，总高 2000，远超视口
const int kMpwOffscreen = -32000;   // 宿主窗口左上角坐标：放到屏幕外，测试时不在眼前闪烁
// 等定时器到期的时长（毫秒）：大于定时器周期 16 ms 加上系统定时器约 15.6 ms 的精度，
// 保证等完之后定时器一定已经到期
const DWORD kMpwTimerDueWaitMs = 40;

// 屏幕外的可见宿主窗口，内容是一个装满长内容的 MainPanelScrollView。析构时销毁窗口。
class MpwFixture
{
public:
    MpwFixture()
        : m_view(NULL)
    {
        RECT rc = { kMpwOffscreen, kMpwOffscreen,
                    kMpwOffscreen + kMpwHostSize, kMpwOffscreen + kMpwHostSize };
        m_host.Create(NULL, rc, _T("MainPanelWheelRepaintTests"), WS_POPUP | WS_VISIBLE,
                      WS_EX_TOOLWINDOW);
        if (!m_host.IsWindow())
        {
            return;
        }

        std::unique_ptr<DuiVBox> content(new DuiVBox());
        content->SetPadding(0);
        content->SetGap(0);
        for (int i = 0; i < kMpwRows; ++i)
        {
            content->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                              DuiLayout::Hint().Fixed(kMpwRowH));
        }

        std::unique_ptr<MainPanelScrollView> view(new MainPanelScrollView());
        m_view = view.get();
        view->SetContent(std::move(content));
        view->SetContentHeight(kMpwRows * kMpwRowH);
        m_host.SetRoot(std::move(view));
        ::UpdateWindow(m_host.m_hWnd);
    }

    ~MpwFixture()
    {
        if (m_host.IsWindow())
        {
            m_host.DestroyWindow();
        }
    }

    // 宿主窗口与视口是否都已建好。
    bool Ready() const
    {
        return m_host.IsWindow() && m_view != NULL;
    }

    // 在视口中心发一格滚轮。zDelta 为正向上滚、为负向下滚。
    void Wheel(short zDelta)
    {
        const RECT& rc = m_view->GetRect();
        POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        ::ClientToScreen(m_host.m_hWnd, &pt);
        ::SendMessage(m_host.m_hWnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)zDelta), MAKELPARAM(pt.x, pt.y));
    }

    // 清掉宿主当前的全部待重绘区域。
    void ClearDirty()
    {
        ::ValidateRect(m_host.m_hWnd, NULL);
    }

    // 宿主当前是否有待重绘区域；有时经 rc 返回其外接矩形。
    bool HasDirty(RECT& rc) const
    {
        ::SetRectEmpty(&rc);
        return ::GetUpdateRect(m_host.m_hWnd, &rc, FALSE) != FALSE;
    }

    // 被测视口（借用，控件树持有）。
    MainPanelScrollView* View()
    {
        return m_view;
    }

    // 宿主窗口句柄。
    HWND Hwnd() const
    {
        return m_host.m_hWnd;
    }

    // 从宿主窗口的消息队列里取出全部 WM_TIMER，返回其中是否有滚动条淡入动画的定时器。
    // 定时器到期后 WM_TIMER 才会出现在队列里；计时被清零的话就取不到。
    bool TakeScrollBarTimer()
    {
        bool found = false;
        MSG msg;
        while (::PeekMessage(&msg, m_host.m_hWnd, WM_TIMER, WM_TIMER, PM_REMOVE))
        {
            if (msg.wParam == balloonmain::kScrollbarAnimTimerId)
            {
                found = true;
            }
        }
        return found;
    }

private:
    DuiHost              m_host;   // 宿主窗口，析构时销毁
    MainPanelScrollView* m_view;   // 被测视口，借用（控件树持有）
};

} // namespace

TEST_CASE("MPW1 滚一格之后宿主没有剩余的待重绘区域", "[wheel][repaint]")
{
    MpwFixture fx;
    REQUIRE(fx.Ready());

    fx.ClearDirty();
    fx.Wheel((short)-WHEEL_DELTA);

    // 前提：确实滚动了（基类在滚动时已同步画出新位置）
    CHECK(fx.View()->GetScrollPos() > 0);

    RECT dirty;
    const bool any = fx.HasDirty(dirty);
    INFO("dirty (" << dirty.left << "," << dirty.top << ")-(" << dirty.right << "," << dirty.bottom << ")");
    CHECK_FALSE(any);
}

TEST_CASE("MPW2 已在顶端时再往上滚不产生重绘，但滚动条被唤出", "[wheel][repaint]")
{
    MpwFixture fx;
    REQUIRE(fx.Ready());
    REQUIRE(fx.View()->GetScrollPos() == 0);

    fx.ClearDirty();
    fx.Wheel((short)WHEEL_DELTA);

    CHECK(fx.View()->GetScrollPos() == 0);

    RECT dirty;
    const bool any = fx.HasDirty(dirty);
    INFO("dirty (" << dirty.left << "," << dirty.top << ")-(" << dirty.right << "," << dirty.bottom << ")");
    CHECK_FALSE(any);

    // 滚动条被唤出：推进零毫秒的一帧，仍报告需要后续帧（淡入动画与空闲计时开始了）
    CHECK(fx.View()->TickScrollBarFade(0));
}

TEST_CASE("MPW3 滚动条已显现时再滚轮不会把淡入定时器的计时清零", "[wheel][scrollbar]")
{
    MpwFixture fx;
    REQUIRE(fx.Ready());

    // 第一格：唤出滚动条，启动淡入动画定时器
    fx.Wheel((short)-WHEEL_DELTA);
    // 等到定时器一定已经到期，期间不取消息（模拟两条滚轮消息之间界面线程正忙）
    ::Sleep(kMpwTimerDueWaitMs);
    // 第二格：滚动条已在显现态，不应再重设定时器
    fx.Wheel((short)-WHEEL_DELTA);

    // 定时器的计时没有被清零，到期的 WM_TIMER 应当已在队列里
    CHECK(fx.TakeScrollBarTimer());
}

TEST_CASE("MPW4 滚动条隐藏且定时器停掉之后再滚轮会重新启动定时器", "[wheel][scrollbar]")
{
    MpwFixture fx;
    REQUIRE(fx.Ready());

    fx.Wheel((short)-WHEEL_DELTA);
    // 进入隐藏态（切换页签等场景），宿主在全部面板稳定后停掉定时器
    fx.View()->HideScrollBarNow();
    ::KillTimer(fx.Hwnd(), balloonmain::kScrollbarAnimTimerId);
    fx.TakeScrollBarTimer();   // 丢掉停掉之前已经排进队列的 WM_TIMER

    // 再次滚轮：滚动条从隐藏变为显现，必须重新启动定时器
    fx.Wheel((short)-WHEEL_DELTA);
    ::Sleep(kMpwTimerDueWaitMs);
    CHECK(fx.TakeScrollBarTimer());
}
