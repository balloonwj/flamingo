/**
 *  ChatScrollFadeTests.cpp
 *
 *  聊天窗一族自绘细滚动条的淡入淡出控制器（UI/BalloonChatWnd/ChatScrollFade，2026-10-04 起）的测试，标签
 *  [chatscrollfade]。时序与 balloonui DuiScrollBar 的自动隐藏相同，用 DuiAnimMgr::TickAll 推进时间：
 *    CF1 新建时不透明度为 0；滚动后淡入到 1，空闲 kIdleHideMs 后淡出回 0；
 *    CF2 悬停在命中带上时空闲计时到期也不淡出；移开后重新计时，到期淡出；
 *    CF3 拖动期间不淡出，离开宿主控件也不淡出；松开后重新计时，到期淡出；
 *    CF4 没在拖动时离开宿主控件：立即开始淡出，kFadeOutMs 后为 0；
 *    CF5 PaintChatScrollThumb 的不透明度：为 0 时不画；0.5 时画出来比 1 时浅。
 *  不创建窗口。框架 Catch2 v3；全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatScrollFade.h"
#include "../UI/BalloonChatWnd/ChatScrollThumb.h"
#include "../../../third-party/balloonui/balloonui/DuiAnimation.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // 淡入淡出的时长常量

#include <gdiplus.h>

using balloonchatwnd::CChatScrollFade;
using balloonwjui::DuiAnimMgr;
using balloonwjui::DuiScrollBar;

namespace {

// 动画时间轴的起点（毫秒，任取）
const DWORD kCfT0 = 300000;
// 判断不透明度「已到 0 / 1」的容差
const float kCfTol = 0.01f;
// 连续走过的空闲计时轮数（检查悬停、拖动期间一直不淡出）
const int kCfRounds = 3;

bool Near(float a, float b)
{
    const float d = a - b;
    return d < kCfTol && d > -kCfTol;
}

// 推进完一轮「空闲计时到期 → 新的计时或淡出开始」：先到期，再记下新动画的起点
DWORD TickIdleRound(DuiAnimMgr& m, DWORD t)
{
    t += DuiScrollBar::kIdleHideMs + DuiScrollBar::kFadeOutMs;
    m.TickAll(t);
    m.TickAll(t + 1);
    return t + 1;
}

// 推进淡出：记下起点并走完 kFadeOutMs
DWORD TickFadeOut(DuiAnimMgr& m, DWORD t)
{
    m.TickAll(t);
    m.TickAll(t + DuiScrollBar::kFadeOutMs);
    return t + DuiScrollBar::kFadeOutMs;
}

// 进程内启动 / 关闭 GDI+（CF5 画滑块用）
class CfScopedGdiplus
{
public:
    CfScopedGdiplus() : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }
    ~CfScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }
    bool Ok() const { return m_ok; }
private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌
    bool      m_ok;      // 是否启动成功
};

// 在白底位图上按给定不透明度画一个滑块，取滑块中部一点的红色分量（越小越深）
int PaintedGray(float opacity)
{
    const int kW = 20;           // 位图宽
    const int kH = 60;           // 位图高
    const RECT thumb = { 8, 5, 13, 55 };   // 宽 5 的滑块
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kW;
    bi.bmiHeader.biHeight = -kH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dc = ::CreateCompatibleDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(dc != NULL);
    REQUIRE(bmp != NULL);
    HGDIOBJ old = ::SelectObject(dc, bmp);
    RECT rc = { 0, 0, kW, kH };
    ::FillRect(dc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        balloonchatwnd::PaintChatScrollThumb(g, thumb, opacity);
    }
    const DWORD v = static_cast<const DWORD*>(bits)[(kH / 2) * kW + (thumb.left + thumb.right) / 2];
    ::SelectObject(dc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
    return (int)((v >> 16) & 0xFF);
}

} // namespace

TEST_CASE("CF1 新建时看不见；滚动后淡入，空闲到期后淡出", "[chatscrollfade]")
{
    DuiAnimMgr& m = DuiAnimMgr::Inst();
    m.Clear();
    CChatScrollFade fade;
    CHECK(Near(fade.Alpha(), 0.0f));

    fade.OnScrolled();
    m.TickAll(kCfT0);
    m.TickAll(kCfT0 + DuiScrollBar::kFadeInMs);
    CHECK(Near(fade.Alpha(), 1.0f));
    m.TickAll(kCfT0 + DuiScrollBar::kIdleHideMs - 1);
    CHECK(Near(fade.Alpha(), 1.0f));
    m.TickAll(kCfT0 + DuiScrollBar::kIdleHideMs);
    TickFadeOut(m, kCfT0 + DuiScrollBar::kIdleHideMs + 1);
    CHECK(Near(fade.Alpha(), 0.0f));
    m.Clear();
}

TEST_CASE("CF2 悬停在命中带上时不淡出，移开后重新计时再淡出", "[chatscrollfade]")
{
    DuiAnimMgr& m = DuiAnimMgr::Inst();
    m.Clear();
    CChatScrollFade fade;
    fade.SetHover(true);
    CHECK(fade.IsHover());
    m.TickAll(kCfT0);
    m.TickAll(kCfT0 + DuiScrollBar::kFadeInMs);
    CHECK(Near(fade.Alpha(), 1.0f));
    DWORD t = kCfT0 + DuiScrollBar::kFadeInMs;
    for (int i = 0; i < kCfRounds; ++i)
    {
        t = TickIdleRound(m, t);
    }
    CHECK(Near(fade.Alpha(), 1.0f));

    fade.SetHover(false);
    t += 1;
    m.TickAll(t);
    m.TickAll(t + DuiScrollBar::kIdleHideMs - 1);
    CHECK(Near(fade.Alpha(), 1.0f));
    m.TickAll(t + DuiScrollBar::kIdleHideMs);
    TickFadeOut(m, t + DuiScrollBar::kIdleHideMs + 1);
    CHECK(Near(fade.Alpha(), 0.0f));
    m.Clear();
}

TEST_CASE("CF3 拖动期间不淡出，离开宿主控件也不淡出；松开后重新计时再淡出", "[chatscrollfade]")
{
    DuiAnimMgr& m = DuiAnimMgr::Inst();
    m.Clear();
    CChatScrollFade fade;
    fade.SetDragging(true);
    CHECK(fade.IsDragging());
    m.TickAll(kCfT0);
    m.TickAll(kCfT0 + DuiScrollBar::kFadeInMs);
    fade.OnOwnerMouseLeave();   //拖出了宿主控件：应忽略
    DWORD t = kCfT0 + DuiScrollBar::kFadeInMs;
    for (int i = 0; i < kCfRounds; ++i)
    {
        t = TickIdleRound(m, t);
    }
    CHECK(Near(fade.Alpha(), 1.0f));

    fade.SetDragging(false);
    CHECK_FALSE(fade.IsDragging());
    t += 1;
    m.TickAll(t);
    m.TickAll(t + DuiScrollBar::kIdleHideMs);
    TickFadeOut(m, t + DuiScrollBar::kIdleHideMs + 1);
    CHECK(Near(fade.Alpha(), 0.0f));
    m.Clear();
}

TEST_CASE("CF4 没在拖动时离开宿主控件，立即开始淡出", "[chatscrollfade]")
{
    DuiAnimMgr& m = DuiAnimMgr::Inst();
    m.Clear();
    CChatScrollFade fade;
    fade.SetHover(true);
    m.TickAll(kCfT0);
    m.TickAll(kCfT0 + DuiScrollBar::kFadeInMs);
    REQUIRE(Near(fade.Alpha(), 1.0f));
    fade.OnOwnerMouseLeave();
    CHECK_FALSE(fade.IsHover());
    //不必等空闲计时：kFadeOutMs 之后就看不见了
    TickFadeOut(m, kCfT0 + DuiScrollBar::kFadeInMs + 1);
    CHECK(Near(fade.Alpha(), 0.0f));
    m.Clear();
}

TEST_CASE("CF5 滑块按不透明度画：0 不画，0.5 比 1 浅", "[chatscrollfade]")
{
    CfScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const int kWhite = 255;
    const int none = PaintedGray(0.0f);
    const int half = PaintedGray(0.5f);
    const int full = PaintedGray(1.0f);
    CHECK(none == kWhite);
    CHECK(full < half);
    CHECK(half < kWhite);
}
