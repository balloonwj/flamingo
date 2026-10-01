/**
 *  ImageViewerGeometry.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "ImageViewerGeometry.h"

#include <cmath>   // std::floor

namespace balloonimageviewer {

// 把 double 四舍五入到最近的 LONG（半数远离零方向取整），用于落点 / 百分比取整。
static LONG RoundToLong(double v)
{
    if (v >= 0.0)
    {
        return (LONG)std::floor(v + 0.5);
    }
    return (LONG)std::ceil(v - 0.5);
}

// 把单个坐标夹进闭区间 [lo, hi]（要求 lo ≤ hi）。
static LONG ClampLong(LONG v, LONG lo, LONG hi)
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

SIZE RotatedImageSize(SIZE imagePx, int rotationDegrees)
{
    // 归一到 [0,360) 再取直角象限；90 / 270 宽高对调，0 / 180 原样。
    int q = ((rotationDegrees % 360) + 360) % 360;
    SIZE s = imagePx;
    if (q == 90 || q == 270)
    {
        s.cx = imagePx.cy;
        s.cy = imagePx.cx;
    }
    return s;
}

double ComputeFitScale(SIZE dispPx, SIZE viewport, bool allowUpscale)
{
    // 尺寸非法时返回 1.0，避免除零 / 负比例。
    if (dispPx.cx <= 0 || dispPx.cy <= 0 || viewport.cx <= 0 || viewport.cy <= 0)
    {
        return 1.0;
    }

    double sx = (double)viewport.cx / (double)dispPx.cx;
    double sy = (double)viewport.cy / (double)dispPx.cy;
    double s = (sx < sy) ? sx : sy;

    // 只缩不放：适应比 > 1 说明图比视口小，夹到 100%（不放大）。
    if (!allowUpscale && s > 1.0)
    {
        s = 1.0;
    }
    return s;
}

double ClampZoom(double scale, double minScale, double maxScale)
{
    if (scale < minScale)
    {
        return minScale;
    }
    if (scale > maxScale)
    {
        return maxScale;
    }
    return scale;
}

double SteppedZoom(double current, int direction, double stepFactor,
                   double minScale, double maxScale)
{
    double next = current;
    if (direction > 0)
    {
        next = current * stepFactor;
    }
    else if (direction < 0)
    {
        next = current / stepFactor;
    }
    return ClampZoom(next, minScale, maxScale);
}

POINT ClampImageOrigin(POINT desiredTopLeft, SIZE scaledImg, SIZE viewport)
{
    POINT p = desiredTopLeft;

    // X 轴：图比视口宽则可平移并夹边，否则水平居中锁定。
    if (scaledImg.cx >= viewport.cx)
    {
        p.x = ClampLong(desiredTopLeft.x, viewport.cx - scaledImg.cx, 0);
    }
    else
    {
        p.x = (viewport.cx - scaledImg.cx) / 2;
    }

    // Y 轴：图比视口高则可平移并夹边，否则垂直居中锁定。
    if (scaledImg.cy >= viewport.cy)
    {
        p.y = ClampLong(desiredTopLeft.y, viewport.cy - scaledImg.cy, 0);
    }
    else
    {
        p.y = (viewport.cy - scaledImg.cy) / 2;
    }

    return p;
}

POINT OriginForZoomAtAnchor(POINT oldTopLeft, double oldScale, double newScale,
                            POINT anchor)
{
    // oldScale 非法时退回原落点，避免除零。
    if (oldScale <= 0.0)
    {
        return oldTopLeft;
    }

    // 锚点到图片左上角的视口距离按缩放比例同步缩放，锚点下的像素即保持不动：
    //   newTopLeft = anchor - (anchor - oldTopLeft) * (newScale / oldScale)
    double ratio = newScale / oldScale;
    POINT p;
    p.x = anchor.x - RoundToLong((double)(anchor.x - oldTopLeft.x) * ratio);
    p.y = anchor.y - RoundToLong((double)(anchor.y - oldTopLeft.y) * ratio);
    return p;
}

CString FormatZoomReadout(double scale)
{
    int percent = (int)RoundToLong(scale * 100.0);
    CString s;
    s.Format(_T("%d%%"), percent);
    return s;
}

CanvasDragAction ResolveCanvasDrag(bool overToolbar, bool pannable, POINT pressPt,
                                   POINT currentPt, SIZE threshold)
{
    //工具栏整块是按钮区域，按在上面的拖动一概不处理
    if (overToolbar)
    {
        return kCanvasDragNone;
    }
    //图片超出窗口：拖动用来平移，从按下起就跟手
    if (pannable)
    {
        return kCanvasDragPanImage;
    }
    //图片能完整显示（或没有图片）：移出系统拖动阈值才算拖动窗口，单击与双击因此不受影响
    const int dx = currentPt.x - pressPt.x;
    const int dy = currentPt.y - pressPt.y;
    const bool movedX = (dx > threshold.cx) || (-dx > threshold.cx);
    const bool movedY = (dy > threshold.cy) || (-dy > threshold.cy);
    return (movedX || movedY) ? kCanvasDragMoveWindow : kCanvasDragNone;
}

} // namespace balloonimageviewer
