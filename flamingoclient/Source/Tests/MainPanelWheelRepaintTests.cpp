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
 *    MPW2 已在顶端时再往上滚，不产生待重绘区域，但滚动条照样被唤出（淡入到完全显示）；
 *    MPW3 连续滚轮期间滚动条一直显示：每一格都重新开始空闲计时，停手之后才淡出；
 *    MPW4 HideScrollBarNow 立即隐藏滚动条，之后再滚轮又会淡入。
 *
 *  2026-10-04 起视口直接使用 balloonui 统一的滚动条，淡入淡出由 DuiAnimMgr 推进（此前由宿主的
 *  WM_TIMER 推进，原 MPW3、MPW4 测的是那套定时器的计时，已随之改写）。用例用 DuiAnimMgr::TickAll
 *  指定时间推进动画，不依赖真实时钟。
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
#include "../../../third-party/balloonui/balloonui/DuiAnimation.h"   // MPW2～MPW4：推进淡入淡出

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
// 动画时间轴的起点（毫秒，任取）
const DWORD kMpwT0 = 400000;
// 两格滚轮之间的间隔（毫秒）：比空闲计时短，滚动条不应在两格之间淡出
const DWORD kMpwWheelGapMs = 500;

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

    // 视口滚动条当前的不透明度（0 看不见，1 完全显示）。
    float BarAlpha()
    {
        return m_view->GetScrollBar()->GetAlpha();
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
    // 先清掉前面用例残留的动画，下面按指定时间推进时只推进本用例的
    balloonwjui::DuiAnimMgr::Inst().Clear();
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

    // 滚动条被唤出：推进完淡入，完全显示
    balloonwjui::DuiAnimMgr& m = balloonwjui::DuiAnimMgr::Inst();
    m.TickAll(kMpwT0);
    m.TickAll(kMpwT0 + balloonwjui::DuiScrollBar::kFadeInMs);
    CHECK(fx.BarAlpha() > 0.99f);
    m.Clear();
}

TEST_CASE("MPW3 连续滚轮期间滚动条一直显示，停手之后才淡出", "[wheel][scrollbar]")
{
    balloonwjui::DuiAnimMgr& m = balloonwjui::DuiAnimMgr::Inst();
    m.Clear();
    MpwFixture fx;
    REQUIRE(fx.Ready());

    fx.Wheel((short)-WHEEL_DELTA);
    m.TickAll(kMpwT0);
    m.TickAll(kMpwT0 + balloonwjui::DuiScrollBar::kFadeInMs);
    REQUIRE(fx.BarAlpha() > 0.99f);

    // 第二格在空闲计时到期之前到来：重新开始计时，原先的到期时刻过去之后仍然显示
    const DWORD second = kMpwT0 + kMpwWheelGapMs;
    m.TickAll(second);
    fx.Wheel((short)-WHEEL_DELTA);
    m.TickAll(second + 1);
    m.TickAll(kMpwT0 + balloonwjui::DuiScrollBar::kIdleHideMs + balloonwjui::DuiScrollBar::kFadeOutMs);
    CHECK(fx.BarAlpha() > 0.99f);

    // 停手：从第二格起空闲计时到期，再走完淡出，看不见了
    const DWORD due = second + 1 + balloonwjui::DuiScrollBar::kIdleHideMs;
    m.TickAll(due);
    m.TickAll(due + 1);
    m.TickAll(due + 1 + balloonwjui::DuiScrollBar::kFadeOutMs);
    CHECK(fx.BarAlpha() < 0.01f);
    m.Clear();
}

TEST_CASE("MPW4 HideScrollBarNow 立即隐藏，之后再滚轮又会淡入","[wheel][scrollbar]")
{
    balloonwjui::DuiAnimMgr& m = balloonwjui::DuiAnimMgr::Inst();
    m.Clear();
    MpwFixture fx;
    REQUIRE(fx.Ready());

    fx.Wheel((short)-WHEEL_DELTA);
    m.TickAll(kMpwT0);
    m.TickAll(kMpwT0 + balloonwjui::DuiScrollBar::kFadeInMs);
    REQUIRE(fx.BarAlpha() > 0.99f);

    // 切换页签一类场景：立即看不见，排着的淡入淡出与空闲计时都作废
    fx.View()->HideScrollBarNow();
    CHECK(fx.BarAlpha() < 0.01f);
    m.TickAll(kMpwT0 + balloonwjui::DuiScrollBar::kIdleHideMs);
    CHECK(fx.BarAlpha() < 0.01f);

    // 再次滚轮：重新淡入
    const DWORD again = kMpwT0 + balloonwjui::DuiScrollBar::kIdleHideMs + 1;
    fx.Wheel((short)-WHEEL_DELTA);
    m.TickAll(again);
    m.TickAll(again + balloonwjui::DuiScrollBar::kFadeInMs);
    CHECK(fx.BarAlpha() > 0.99f);
    m.Clear();
}
