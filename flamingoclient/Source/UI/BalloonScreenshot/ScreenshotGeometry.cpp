/**
 *  ScreenshotGeometry.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "ScreenshotGeometry.h"

namespace balloonscreenshot {

RECT NormalizeDragRect(POINT anchor, POINT cur)
{
    RECT r;
    r.left   = (anchor.x < cur.x) ? anchor.x : cur.x;
    r.right  = (anchor.x < cur.x) ? cur.x : anchor.x;
    r.top    = (anchor.y < cur.y) ? anchor.y : cur.y;
    r.bottom = (anchor.y < cur.y) ? cur.y : anchor.y;
    return r;
}

// 把单个坐标夹进闭区间 [lo, hi]（要求 lo≤hi）。
static int ClampValue(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

RECT ClampRectToBounds(const RECT& r, const RECT& bounds)
{
    RECT out;
    // 四个坐标各自夹进 bounds 的区间：x 进 [left,right]、y 进 [top,bottom]。
    // 这样结果一定落在 bounds 内；又因夹取是单调的，原本 left≤right / top≤bottom
    // 的次序得以保留——选区完全在界外时会退化为贴在某个角上的零尺寸矩形（交由
    // IsSelectionUsable 判无效），而不会跑出边界，也不会出现 left>right。
    out.left   = ClampValue(r.left,   bounds.left, bounds.right);
    out.right  = ClampValue(r.right,  bounds.left, bounds.right);
    out.top    = ClampValue(r.top,    bounds.top,  bounds.bottom);
    out.bottom = ClampValue(r.bottom, bounds.top,  bounds.bottom);
    return out;
}

bool IsSelectionUsable(const RECT& r, int minW, int minH)
{
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    return (w >= minW) && (h >= minH);
}

CString FormatSizeReadout(int w, int h)
{
    CString s;
    // × 为 U+00D7 乘号；源文件按 /utf-8 解析，可直接写。
    s.Format(_T("%d × %d"), w, h);
    return s;
}

POINT PlaceActionBar(const RECT& sel, SIZE barSize, const RECT& workArea, int gap)
{
    POINT pt;
    // 默认：右边与选区右边对齐、贴在选区下方。
    pt.x = sel.right - barSize.cx;
    pt.y = sel.bottom + gap;

    // 下方放不下 → 翻到选区上方。
    if (pt.y + barSize.cy > workArea.bottom)
    {
        pt.y = sel.top - gap - barSize.cy;
    }

    // 整体夹进工作区：先夹右 / 下，再夹左 / 上（条比工作区还大时贴左 / 上边缘）。
    if (pt.x + barSize.cx > workArea.right)
    {
        pt.x = workArea.right - barSize.cx;
    }
    if (pt.x < workArea.left)
    {
        pt.x = workArea.left;
    }
    if (pt.y + barSize.cy > workArea.bottom)
    {
        pt.y = workArea.bottom - barSize.cy;
    }
    if (pt.y < workArea.top)
    {
        pt.y = workArea.top;
    }
    return pt;
}

SelHitZone HitTestSelection(const RECT& sel, POINT pt, int tol)
{
    // 在选区(含容差)之外 → 外部。
    RECT outer = { sel.left - tol, sel.top - tol, sel.right + tol, sel.bottom + tol };
    if (!::PtInRect(&outer, pt))
    {
        return kSelOutside;
    }
    bool nearL = (pt.x >= sel.left - tol)  && (pt.x <= sel.left + tol);
    bool nearR = (pt.x >= sel.right - tol) && (pt.x <= sel.right + tol);
    bool nearT = (pt.y >= sel.top - tol)   && (pt.y <= sel.top + tol);
    bool nearB = (pt.y >= sel.bottom - tol)&& (pt.y <= sel.bottom + tol);

    // 角优先。
    if (nearL && nearT) return kSelTL;
    if (nearR && nearT) return kSelTR;
    if (nearL && nearB) return kSelBL;
    if (nearR && nearB) return kSelBR;
    // 边。
    if (nearL) return kSelLeft;
    if (nearR) return kSelRight;
    if (nearT) return kSelTop;
    if (nearB) return kSelBottom;
    // 其余（含选区内部、以及很小选区落在容差带里）→ 当作内部（移动）。
    return kSelInside;
}

RECT MoveSelectionClamped(const RECT& sel, int dx, int dy, const RECT& bounds)
{
    int w = sel.right - sel.left;
    int h = sel.bottom - sel.top;
    int newL = sel.left + dx;
    int newT = sel.top + dy;
    if (newL < bounds.left)
    {
        newL = bounds.left;
    }
    if (newL + w > bounds.right)
    {
        newL = bounds.right - w;
    }
    if (newT < bounds.top)
    {
        newT = bounds.top;
    }
    if (newT + h > bounds.bottom)
    {
        newT = bounds.bottom - h;
    }
    RECT o = { newL, newT, newL + w, newT + h };
    return o;
}

RECT ResizeSelection(const RECT& sel, SelHitZone zone, int dx, int dy,
                     const RECT& bounds, int minW, int minH)
{
    RECT r = sel;
    bool left   = (zone == kSelLeft  || zone == kSelTL || zone == kSelBL);
    bool right  = (zone == kSelRight || zone == kSelTR || zone == kSelBR);
    bool top    = (zone == kSelTop   || zone == kSelTL || zone == kSelTR);
    bool bottom = (zone == kSelBottom|| zone == kSelBL || zone == kSelBR);

    if (left)
    {
        r.left += dx;
        if (r.left < bounds.left)        r.left = bounds.left;
        if (r.left > r.right - minW)     r.left = r.right - minW;
    }
    if (right)
    {
        r.right += dx;
        if (r.right > bounds.right)      r.right = bounds.right;
        if (r.right < r.left + minW)     r.right = r.left + minW;
    }
    if (top)
    {
        r.top += dy;
        if (r.top < bounds.top)          r.top = bounds.top;
        if (r.top > r.bottom - minH)     r.top = r.bottom - minH;
    }
    if (bottom)
    {
        r.bottom += dy;
        if (r.bottom > bounds.bottom)    r.bottom = bounds.bottom;
        if (r.bottom < r.top + minH)     r.bottom = r.top + minH;
    }
    return r;
}

} // namespace balloonscreenshot
