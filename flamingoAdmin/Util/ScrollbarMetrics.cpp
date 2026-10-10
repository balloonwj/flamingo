/**
 *  flamingoAdmin —— 自绘悬浮滚动条几何计算的实现。
 *
 *  见 ScrollbarMetrics.h 的接口说明。全部为纯整数运算。
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

} // namespace flamingoadmin
