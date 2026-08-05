/**
 *  OverlayScrollBarGeometry.cpp
 *
 *  overlay 滚动条几何计算的实现。全部为纯坐标运算，与 FlamingoUnit 测试
 *  工程共用、不 include stdafx.h，PCH 设 NotUsing。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "OverlayScrollBarGeometry.h"

namespace balloonmain {

OverlayScrollBarMetrics ComputeOverlayScrollBar(const RECT& rcView,
                                                int contentHeight,
                                                int scrollPos,
                                                int edgeInsetPx)
{
    OverlayScrollBarMetrics m;
    m.m_visible = false;
    m.m_rcThumb.left   = 0;
    m.m_rcThumb.top    = 0;
    m.m_rcThumb.right  = 0;
    m.m_rcThumb.bottom = 0;
    m.m_rcHit = m.m_rcThumb;

    const int viewW = rcView.right - rcView.left;
    const int viewH = rcView.bottom - rcView.top;

    // 视口还没被布局过（宽或高为 0 甚至为负）时什么都算不出来。
    if (viewW <= 0 || viewH <= 0)
    {
        return m;
    }

    // 内容装得下 —— 不需要滚动条。
    if (contentHeight <= viewH)
    {
        return m;
    }

    // 滑块高度按"视口 / 内容"的比例，再受最小高度与视口高度双向约束。
    // 用 LONGLONG 中转，避免 viewH * viewH 在长列表下溢出 int。
    int thumbH = (int)((LONGLONG)viewH * viewH / contentHeight);
    if (thumbH < kOverlayBarMinThumb)
    {
        thumbH = kOverlayBarMinThumb;
    }
    if (thumbH > viewH)
    {
        thumbH = viewH;
    }

    // 滚动位置夹回有效范围，避免调用方传进来越界值时滑块跑到视口外面。
    const int maxScroll = contentHeight - viewH;
    int pos = scrollPos;
    if (pos < 0)
    {
        pos = 0;
    }
    if (pos > maxScroll)
    {
        pos = maxScroll;
    }

    // 滑块纵向位置：滚动比例线性映射到轨道（轨道长 = 视口高 - 滑块高）。
    int thumbTop = rcView.top;
    if (maxScroll > 0)
    {
        thumbTop += (int)((LONGLONG)pos * (viewH - thumbH) / maxScroll);
    }

    // 让位宽度不接受负值（负值等于把滚动条推进缩放边框，正是要避免的事）。
    int inset = edgeInsetPx;
    if (inset < 0)
    {
        inset = 0;
    }

    // 可用右缘 —— 滑块与命中带都不越过它，缩放边框那道带子保持完整可拖。
    const int usableRight = rcView.right - inset;
    const int thumbRight  = usableRight - kOverlayBarMargin;
    const int thumbLeft   = thumbRight - kOverlayBarWidth;

    m.m_visible = true;

    m.m_rcThumb.left   = thumbLeft;
    m.m_rcThumb.top    = thumbTop;
    m.m_rcThumb.right  = thumbRight;
    m.m_rcThumb.bottom = thumbTop + thumbH;

    // 命中带：横向从滑块左缘再往左扩一段（滑块太窄不好点），右到可用右缘；
    // 纵向盖满整个视口，使鼠标在轨道任意高度悬停都能把滚动条唤出来。
    m.m_rcHit.left   = thumbLeft - kOverlayBarHitPad;
    m.m_rcHit.top    = rcView.top;
    m.m_rcHit.right  = usableRight;
    m.m_rcHit.bottom = rcView.bottom;

    // 视口窄到让位宽度都放不下时，命中带左缘可能被推到视口外面，夹回来。
    if (m.m_rcHit.left < rcView.left)
    {
        m.m_rcHit.left = rcView.left;
    }
    if (m.m_rcHit.right < m.m_rcHit.left)
    {
        m.m_rcHit.right = m.m_rcHit.left;
    }

    return m;
}

int OverlayScrollPosFromThumbTop(const RECT& rcView,
                                 int contentHeight,
                                 int thumbHeight,
                                 int thumbTop)
{
    const int viewH = rcView.bottom - rcView.top;
    if (viewH <= 0)
    {
        return 0;
    }
    if (contentHeight <= viewH)
    {
        return 0;
    }

    // 轨道长度 = 视口高 - 滑块高。滑块与视口等高（或因异常入参更高）时轨道
    // 为 0，滑块无处可拖，直接返回 0，顺带避开除零。
    const int trackH = viewH - thumbHeight;
    if (trackH <= 0)
    {
        return 0;
    }

    // 滑块顶边相对视口顶边的偏移，夹在轨道范围内 —— 用户可以把鼠标拖到视口
    // 外面去，不夹就会算出越界的滚动位置。
    int offset = thumbTop - rcView.top;
    if (offset < 0)
    {
        offset = 0;
    }
    if (offset > trackH)
    {
        offset = trackH;
    }

    const int maxScroll = contentHeight - viewH;
    int pos = (int)((LONGLONG)offset * maxScroll / trackH);
    if (pos < 0)
    {
        pos = 0;
    }
    if (pos > maxScroll)
    {
        pos = maxScroll;
    }
    return pos;
}

int ComputeResizeBorderInsetPx(int borderPx, int dpi, bool resizable)
{
    // 窗口不可缩放时没有缩放边框，不必让路。
    if (!resizable)
    {
        return 0;
    }
    if (borderPx <= 0)
    {
        return 0;
    }

    // 窗口尚未创建时 GetDpi() 可能返回 0 / 负值，按 100% 缩放兜底。
    int effectiveDpi = dpi;
    if (effectiveDpi <= 0)
    {
        effectiveDpi = kOverlayBarBaseDpi;
    }

    return ::MulDiv(borderPx, effectiveDpi, kOverlayBarBaseDpi);
}

} // namespace balloonmain
