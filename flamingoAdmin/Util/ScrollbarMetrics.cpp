/**
 *  flamingoAdmin —— 自绘悬浮滚动条几何计算的实现。
 *
 *  见 ScrollbarMetrics.h 的接口说明。全部为纯整数运算；比例计算先提升到
 *  long long 再除，避免 trackH * viewH 在内容很高时溢出 int。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Util/ScrollbarMetrics.h"

namespace flamingoadmin {

bool ScrollbarNeeded(int contentH, int viewH)
{
    return contentH > viewH;
}

int ClampScrollOffset(int scroll, int contentH, int viewH)
{
    int maxScroll = contentH - viewH;
    if (maxScroll < 0)
    {
        maxScroll = 0;
    }
    if (scroll < 0)
    {
        return 0;
    }
    if (scroll > maxScroll)
    {
        return maxScroll;
    }
    return scroll;
}

RECT ThumbRect(const RECT& track, int contentH, int viewH,
               int scroll, int minThumb)
{
    RECT empty = { 0, 0, 0, 0 };
    if (!ScrollbarNeeded(contentH, viewH))
    {
        return empty;
    }
    int trackH = track.bottom - track.top;
    if (trackH <= 0 || contentH <= 0)
    {
        return empty;
    }

    // 滑块高度与「可视高 / 内容高」成比例，并夹在 [minThumb, trackH]。
    int thumbH = static_cast<int>(
        static_cast<long long>(trackH) * viewH / contentH);
    if (thumbH < minThumb)
    {
        thumbH = minThumb;
    }
    if (thumbH > trackH)
    {
        thumbH = trackH;
    }

    // 滑块顶部在「轨道可移动范围 travel」内按滚动比例定位。
    int maxScroll = contentH - viewH;
    int travel = trackH - thumbH;
    int s = ClampScrollOffset(scroll, contentH, viewH);
    int thumbTop = track.top;
    if (maxScroll > 0 && travel > 0)
    {
        thumbTop = track.top + static_cast<int>(
            static_cast<long long>(travel) * s / maxScroll);
    }

    RECT r;
    r.left = track.left;
    r.top = thumbTop;
    r.right = track.right;
    r.bottom = thumbTop + thumbH;
    return r;
}

int ScrollOffsetFromThumbTop(const RECT& track, int thumbH, int thumbTop,
                             int contentH, int viewH)
{
    int maxScroll = contentH - viewH;
    if (maxScroll <= 0)
    {
        return 0;
    }
    int trackH = track.bottom - track.top;
    int travel = trackH - thumbH;
    if (travel <= 0)
    {
        return 0;
    }

    // 滑块顶部相对轨道顶的位移，夹到 [0, travel]，再按比例换算成滚动偏移。
    int rel = thumbTop - track.top;
    if (rel < 0)
    {
        rel = 0;
    }
    if (rel > travel)
    {
        rel = travel;
    }
    int scroll = static_cast<int>(
        static_cast<long long>(maxScroll) * rel / travel);
    return ClampScrollOffset(scroll, contentH, viewH);
}

float FadeAlphaAt(double t01, float fromAlpha,
                  double fadeInFrac, double holdEndFrac)
{
    if (t01 < fadeInFrac)
    {
        // 淡入段：从 fromAlpha 线性过渡到 1。
        double k = (fadeInFrac > 0.0) ? (t01 / fadeInFrac) : 1.0;
        return fromAlpha + (1.0f - fromAlpha) * static_cast<float>(k);
    }
    if (t01 < holdEndFrac)
    {
        // 驻留段：恒为完全不透明。
        return 1.0f;
    }
    // 淡出段：从 1 线性过渡到 0。
    double span = 1.0 - holdEndFrac;
    double k = (span > 0.0) ? ((t01 - holdEndFrac) / span) : 1.0;
    float a = 1.0f - static_cast<float>(k);
    if (a < 0.0f)
    {
        a = 0.0f;
    }
    return a;
}

} // namespace flamingoadmin
