/**
 *  主面板贴边自动隐藏的几何计算实现，PanelDockGeometry.cpp
 *  规则与约定见 PanelDockGeometry.h。
 *
 *  本文件是纯 Win32 薄层，不 include stdafx.h（工程里对应的 PrecompiledHeader 设为
 *  NotUsing），与同目录的 CornerPopupPlacement.cpp 等纯逻辑模块同例。
 *
 *  balloonwj@qq.com   2026-10-02
 */

#include "PanelDockGeometry.h"

namespace balloonmain {

namespace {

int Width(const RECT& rc)
{
    return rc.right - rc.left;
}

int Height(const RECT& rc)
{
    return rc.bottom - rc.top;
}

//把窗口的起点限制在 [lo, hi - size] 之内，使长度为 size 的窗口落在 [lo, hi] 里。
//窗口比这段区间还长时以 lo 为准（起点对齐、超出的部分落在区间之外）
int ClampOrigin(int origin, int size, int lo, int hi)
{
    if (origin > hi - size)
    {
        origin = hi - size;
    }
    if (origin < lo)
    {
        origin = lo;
    }
    return origin;
}

//除了 selfIndex 那一台之外，是否有显示器与 region 相交。用来判断某条边外是否
//紧挨着另一台显示器：region 取窗口收起后会占据的、位于该边之外的那块区域
bool OtherMonitorIntersects(const std::vector<DockMonitor>& monitors, size_t selfIndex, const RECT& region)
{
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        if (i == selfIndex)
        {
            continue;
        }
        RECT overlap;
        if (::IntersectRect(&overlap, &monitors[i].m_monitor, &region))
        {
            return true;
        }
    }
    return false;
}

} // namespace

DockSnapResult DetectDockEdge(const RECT& wnd, const std::vector<DockMonitor>& monitors, int snapPx)
{
    DockSnapResult result;
    const int w = Width(wnd);
    const int h = Height(wnd);
    if (w <= 0 || h <= 0)
    {
        return result;
    }

    //找与窗口重叠面积最大的显示器
    size_t best = monitors.size();
    long long bestArea = 0;
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        RECT overlap;
        if (!::IntersectRect(&overlap, &wnd, &monitors[i].m_monitor))
        {
            continue;
        }
        const long long area = (long long)Width(overlap) * Height(overlap);
        if (area > bestArea)
        {
            bestArea = area;
            best = i;
        }
    }
    if (best == monitors.size())
    {
        return result;
    }

    const RECT& mon = monitors[best].m_monitor;
    const RECT& work = monitors[best].m_work;

    //上边：靠近或越过工作区上边缘；任务栏不在上边；上边之外没有别的显示器
    if (wnd.top - work.top <= snapPx && work.top == mon.top)
    {
        const int left = ClampOrigin(wnd.left, w, work.left, work.right);
        RECT beyond = { left, work.top - h, left + w, work.top };
        if (!OtherMonitorIntersects(monitors, best, beyond))
        {
            result.m_edge = kPanelDockTop;
            result.m_snapped.left = left;
            result.m_snapped.top = work.top;
            result.m_snapped.right = left + w;
            result.m_snapped.bottom = work.top + h;
            result.m_work = work;
            return result;
        }
    }

    //左边与右边吸附后竖直方向的位置相同：平移到窗口落在工作区内
    const int top = ClampOrigin(wnd.top, h, work.top, work.bottom);

    //左边：靠近或越过工作区左边缘；任务栏不在左边；左边之外没有别的显示器
    if (wnd.left - work.left <= snapPx && work.left == mon.left)
    {
        RECT beyond = { work.left - w, top, work.left, top + h };
        if (!OtherMonitorIntersects(monitors, best, beyond))
        {
            result.m_edge = kPanelDockLeft;
            result.m_snapped.left = work.left;
            result.m_snapped.top = top;
            result.m_snapped.right = work.left + w;
            result.m_snapped.bottom = top + h;
            result.m_work = work;
            return result;
        }
    }

    //右边：靠近或越过工作区右边缘；任务栏不在右边；右边之外没有别的显示器
    if (work.right - wnd.right <= snapPx && work.right == mon.right)
    {
        RECT beyond = { work.right, top, work.right + w, top + h };
        if (!OtherMonitorIntersects(monitors, best, beyond))
        {
            result.m_edge = kPanelDockRight;
            result.m_snapped.left = work.right - w;
            result.m_snapped.top = top;
            result.m_snapped.right = work.right;
            result.m_snapped.bottom = top + h;
            result.m_work = work;
            return result;
        }
    }

    return result;
}

RECT ComputeDockHiddenRect(const RECT& snapped, PanelDockEdge edge, const RECT& work, int stripPx)
{
    RECT rc = snapped;
    const int w = Width(snapped);
    const int h = Height(snapped);
    switch (edge)
    {
    //贴上边：窗口整体上移，底边停在工作区上边缘之下 stripPx 处
    case kPanelDockTop:
        rc.bottom = work.top + stripPx;
        rc.top = rc.bottom - h;
        break;

    //贴左边：窗口整体左移，右边停在工作区左边缘之右 stripPx 处
    case kPanelDockLeft:
        rc.right = work.left + stripPx;
        rc.left = rc.right - w;
        break;

    //贴右边：窗口整体右移，左边停在工作区右边缘之左 stripPx 处
    case kPanelDockRight:
        rc.left = work.right - stripPx;
        rc.right = rc.left + w;
        break;

    //未贴边：不移动
    default:
        break;
    }
    return rc;
}

RECT ComputeDockStripRect(const RECT& snapped, PanelDockEdge edge, const RECT& work, int stripPx)
{
    RECT rc;
    ::SetRectEmpty(&rc);
    switch (edge)
    {
    //贴上边：屏幕顶部、与窗口同宽的一条
    case kPanelDockTop:
        rc.left = snapped.left;
        rc.right = snapped.right;
        rc.top = work.top;
        rc.bottom = work.top + stripPx;
        break;

    //贴左边：屏幕左侧、与窗口同高的一条
    case kPanelDockLeft:
        rc.left = work.left;
        rc.right = work.left + stripPx;
        rc.top = snapped.top;
        rc.bottom = snapped.bottom;
        break;

    //贴右边：屏幕右侧、与窗口同高的一条
    case kPanelDockRight:
        rc.left = work.right - stripPx;
        rc.right = work.right;
        rc.top = snapped.top;
        rc.bottom = snapped.bottom;
        break;

    //未贴边：没有细条
    default:
        break;
    }
    return rc;
}

} // namespace balloonmain
