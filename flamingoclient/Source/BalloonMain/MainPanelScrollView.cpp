/**
 *  MainPanelScrollView.cpp
 *
 *  主面板滚动视口实现。继承 DuiScrollView，内置滚动条宽度置 0，改为自绘
 *  一条悬浮式细滚动条（GDI+ 抗锯齿圆角）：滚动 / 悬停时淡入，空闲一段时间
 *  后淡出，滑块可用鼠标拖动。几何（含给窗口缩放边框让位）交给
 *  OverlayScrollBarGeometry 算，本文件只负责交互与绘制。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "MainPanelScrollView.h"
#include "../../balloonui/balloonui/DuiHost.h"

namespace balloonmain {

namespace {

// 滑块底色（不含 alpha）—— 深灰，实际绘制时按当前淡入淡出进度取 alpha。
const BYTE kThumbColorR = 0x5A;
const BYTE kThumbColorG = 0x5A;
const BYTE kThumbColorB = 0x5A;

// 向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

} // anonymous namespace

MainPanelScrollView::MainPanelScrollView()
    : m_alpha(0)
    , m_awake(false)
    , m_idleMs(0)
    , m_hoverBar(false)
    , m_dragging(false)
    , m_dragGrabOffsetY(0)
    , m_edgeInsetPx(0)
{
    // 不走自动测高（方案 1a）。原因：内容是一个 vbox，里面装 label + 单列
    // DuiTreeView（weight=1）。DuiVBox::GetDesiredSize 只把固定高（fixedMain）
    // 累加进 mainSum；treeview 作为 weight 子贡献 0，而 DuiTreeView 自己又
    // 没覆写 GetDesiredSize（默认返回 {0,0}）。所以 auto 测出来的高度只
    // 等于 label 高 + padding，远小于实际内容总高，scrollview 会误以为
    // "内容未溢出"不滚。改由业务侧（FillContactsTree / FillGroupsTree /
    // FillMessagesTree）在数据填完后调 SetContentHeight 显式同步。
    SetAutoContentHeight(false);
    // 内置滚动条宽度置 0：不占布局、内容用满宽度；改为自绘 overlay 条。
    SetScrollBarWidth(0);
}

OverlayScrollBarMetrics MainPanelScrollView::CurrentScrollBar() const
{
    return ComputeOverlayScrollBar(m_rcItem, GetContentHeight(),
                                   GetScrollPos(), m_edgeInsetPx);
}

void MainPanelScrollView::SetEdgeInsetPx(int px)
{
    int v = px;
    if (v < 0)
    {
        v = 0;
    }
    if (v == m_edgeInsetPx)
    {
        return;
    }
    m_edgeInsetPx = v;
    Invalidate();
}

void MainPanelScrollView::StartAnimPulse()
{
    balloonwjui::DuiHost* host = GetHost();
    if (host == NULL || host->m_hWnd == NULL)
    {
        return;
    }
    // 同 id 重复 SetTimer 等于重设该定时器，三个面板共用一份，互不冲突。
    ::SetTimer(host->m_hWnd, kScrollbarAnimTimerId, kScrollbarAnimIntervalMs, NULL);
}

void MainPanelScrollView::WakeScrollBar()
{
    m_awake  = true;
    m_idleMs = 0;
    StartAnimPulse();
}

void MainPanelScrollView::HideScrollBarNow()
{
    m_awake    = false;
    m_idleMs   = 0;
    m_hoverBar = false;
    m_dragging = false;
    if (m_alpha != 0)
    {
        m_alpha = 0;
        Invalidate();
    }
}

bool MainPanelScrollView::TickScrollBarFade(int elapsedMs)
{
    const int step = (elapsedMs > 0) ? elapsedMs : 0;

    // 拖动中或鼠标停在滚动条上时不计空闲 —— 用户正要用它，不能淡出。
    if (m_dragging || m_hoverBar)
    {
        m_idleMs = 0;
    }
    else if (m_awake)
    {
        m_idleMs += step;
        if (m_idleMs >= kScrollbarIdleHideMs)
        {
            m_awake = false;
        }
    }

    const int target = m_awake ? kScrollbarThumbAlpha : 0;
    if (m_alpha != target)
    {
        // 一次淡入 / 淡出跑满 kScrollbarFadeMs，按本帧耗时折算 alpha 增量；
        // 帧间隔极短时至少走 1，免得停在原地永远到不了目标。
        int delta = kScrollbarThumbAlpha * step / kScrollbarFadeMs;
        if (delta < 1)
        {
            delta = 1;
        }
        if (m_alpha < target)
        {
            m_alpha += delta;
            if (m_alpha > target)
            {
                m_alpha = target;
            }
        }
        else
        {
            m_alpha -= delta;
            if (m_alpha < target)
            {
                m_alpha = target;
            }
        }
        Invalidate();
    }

    // 还在动画中、或仍处于显现态（空闲计时还得继续跑）都需要下一帧。
    return (m_alpha != target) || m_awake;
}

balloonwjui::DuiControl* MainPanelScrollView::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return NULL;
    }

    // 落在滚动条命中带上就认领为自己 —— 否则命中的会是铺满视口的
    // DuiTreeView 行，鼠标事件到不了本控件，滑块也就抓不住。内容没溢出
    // （不需要滚动条）时不认领，免得列表右侧平白多出一条吃掉点击的死区。
    if (m_bEnabled)
    {
        OverlayScrollBarMetrics bar = CurrentScrollBar();
        if (bar.m_visible && ::PtInRect(&bar.m_rcHit, ptHostClient))
        {
            return this;
        }
    }

    return DuiScrollView::HitTest(ptHostClient);
}

bool MainPanelScrollView::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    // 先让基类完成实际滚动。
    const bool handled = DuiScrollView::OnMouseWheel(pt, zDelta, mkFlags);

    // 滚动 → 滚动条淡入显现，并重新开始空闲计时。
    WakeScrollBar();
    Invalidate();
    return handled;
}

bool MainPanelScrollView::OnMouseMove(POINT pt, UINT mkFlags)
{
    if (m_dragging)
    {
        OverlayScrollBarMetrics bar = CurrentScrollBar();
        if (bar.m_visible)
        {
            // 拖动期间滑块顶边始终跟着鼠标走，两者的相对位置（按下那一刻
            // 抓在滑块什么高度）保持不变，手感才不会跳。
            const int thumbH   = bar.m_rcThumb.bottom - bar.m_rcThumb.top;
            const int thumbTop = pt.y - m_dragGrabOffsetY;
            const int pos = OverlayScrollPosFromThumbTop(m_rcItem, GetContentHeight(),
                                                         thumbH, thumbTop);
            if (pos != GetScrollPos())
            {
                SetScrollPos(pos);
            }
        }
        m_idleMs = 0;
        Invalidate();
        return true;
    }

    OverlayScrollBarMetrics bar = CurrentScrollBar();
    const bool hover = bar.m_visible && (::PtInRect(&bar.m_rcHit, pt) != FALSE);
    m_hoverBar = hover;
    if (hover)
    {
        // 鼠标移到滚动条上就把它唤出来（哪怕此刻是隐藏的），用户才有得抓。
        WakeScrollBar();
        return true;
    }

    return DuiScrollView::OnMouseMove(pt, mkFlags);
}

bool MainPanelScrollView::OnMouseLeave()
{
    m_hoverBar = false;
    // 悬停结束，空闲计时从头开始跑；pulse 定时器要还在，否则没人推进淡出。
    m_idleMs = 0;
    if (m_awake)
    {
        StartAnimPulse();
    }
    return DuiScrollView::OnMouseLeave();
}

bool MainPanelScrollView::OnLButtonDown(POINT pt, UINT mkFlags)
{
    OverlayScrollBarMetrics bar = CurrentScrollBar();
    if (bar.m_visible && ::PtInRect(&bar.m_rcHit, pt))
    {
        if (::PtInRect(&bar.m_rcThumb, pt))
        {
            m_dragging = true;
            m_dragGrabOffsetY = pt.y - bar.m_rcThumb.top;
            // 抓 capture：鼠标拖出视口甚至拖出窗口时仍然收得到移动与抬起。
            Capture();
        }
        WakeScrollBar();
        // 命中带上的点击一律消费掉 —— 按在轨道空白处也不该穿透去点中列表行。
        return true;
    }

    return DuiScrollView::OnLButtonDown(pt, mkFlags);
}

bool MainPanelScrollView::OnLButtonUp(POINT pt, UINT mkFlags)
{
    if (m_dragging)
    {
        m_dragging = false;
        ReleaseCapture();
        // 松手后重新开始空闲计时（鼠标若仍停在滚动条上，m_hoverBar 会拦住淡出）。
        m_idleMs = 0;
        StartAnimPulse();
        return true;
    }

    return DuiScrollView::OnLButtonUp(pt, mkFlags);
}

void MainPanelScrollView::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 先画滚动内容（基类负责裁剪到视口 + 按 scrollPos 偏移）。
    DuiScrollView::OnPaint(hdc, rcDirty);

    // 完全透明时不必进 GDI+。
    if (m_alpha <= 0)
    {
        return;
    }

    OverlayScrollBarMetrics bar = CurrentScrollBar();
    if (!bar.m_visible)
    {
        return;
    }

    const Gdiplus::REAL x = (Gdiplus::REAL)bar.m_rcThumb.left;
    const Gdiplus::REAL y = (Gdiplus::REAL)bar.m_rcThumb.top;
    const Gdiplus::REAL w = (Gdiplus::REAL)(bar.m_rcThumb.right - bar.m_rcThumb.left);
    const Gdiplus::REAL h = (Gdiplus::REAL)(bar.m_rcThumb.bottom - bar.m_rcThumb.top);
    if (w <= 0.0f || h <= 0.0f)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, x, y, w, h, w / 2.0f);
    Gdiplus::Color thumbColor((BYTE)m_alpha, kThumbColorR, kThumbColorG, kThumbColorB);
    Gdiplus::SolidBrush brush(thumbColor);
    g.FillPath(&brush, &path);
}

bool ForwardWheelToScrollView(balloonwjui::DuiControl* from,
                              POINT pt, short zDelta, UINT mkFlags)
{
    balloonwjui::DuiControl* p = (from != NULL) ? from->GetParent() : NULL;
    while (p != NULL)
    {
        balloonwjui::DuiScrollView* sv =
            dynamic_cast<balloonwjui::DuiScrollView*>(p);
        if (sv != NULL)
        {
            return sv->OnMouseWheel(pt, zDelta, mkFlags);
        }
        p = p->GetParent();
    }
    return false;
}

} // namespace balloonmain
