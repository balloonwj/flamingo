/**
 *  画面按宽高比放进一块区域的计算，说明见 ShareAspectFit.h。
 *  balloonwj@qq.com   2026-10-14
 */
#include "ShareAspectFit.h"

#include <stdint.h>

namespace screenshare {

namespace {

//居中时两边各分一半
const int kAspectFitHalf = 2;

} // namespace

RECT FitRectKeepAspect(const RECT& area, int srcW, int srcH)
{
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top;
    if (srcW <= 0 || srcH <= 0 || areaW <= 0 || areaH <= 0)
        return area;
    RECT out = area;
    //比较 areaW / areaH 与 srcW / srcH：区域更宽时上下铺满、左右留黑边，否则左右铺满、上下留黑边
    if (static_cast<int64_t>(areaW) * srcH > static_cast<int64_t>(areaH) * srcW)
    {
        const int w = static_cast<int>(static_cast<int64_t>(areaH) * srcW / srcH);
        out.left = area.left + (areaW - w) / kAspectFitHalf;
        out.right = out.left + w;
    }
    else
    {
        const int h = static_cast<int>(static_cast<int64_t>(areaW) * srcH / srcW);
        out.top = area.top + (areaH - h) / kAspectFitHalf;
        out.bottom = out.top + h;
    }
    return out;
}

} // namespace screenshare
