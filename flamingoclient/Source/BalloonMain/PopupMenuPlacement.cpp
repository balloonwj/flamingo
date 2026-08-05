/**
 *  PopupMenuPlacement.cpp
 *
 *  弹出菜单落点夹取的实现。核心 ClampMenuOriginToRect 为纯坐标运算，
 *  与 FlamingoUnit 测试工程共用、不 include stdafx.h，PCH 设 NotUsing。
 *  ClampPopupMenuToWorkArea 走 MonitorFromPoint / GetMonitorInfo 取工作区。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "PopupMenuPlacement.h"

namespace balloonmain {

// 菜单与工作区边缘（尤其任务栏一侧）之间预留的间隙，单位：像素。让菜单浮在
// 任务栏 / 屏幕边缘之上留一道缝，观感对齐 QQ 托盘菜单，也避开 popup 系统阴影
// 往下溢出到任务栏造成"底部被吃掉"的错觉。
static const int kMenuEdgeGap = 4;

RECT GetMenuPlacementArea(POINT anchor)
{
    RECT work;

    // 取锚点所在显示器的工作区（排除任务栏），多显示器安全。
    HMONITOR hMonitor = ::MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (hMonitor != NULL && ::GetMonitorInfo(hMonitor, &mi))
    {
        work = mi.rcWork;
    }
    else if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0))
    {
        // 再退化：用主屏整屏尺寸。
        work.left   = 0;
        work.top    = 0;
        work.right  = ::GetSystemMetrics(SM_CXSCREEN);
        work.bottom = ::GetSystemMetrics(SM_CYSCREEN);
    }

    // 四周向内缩一道缝（缩过头则退化为零宽/零高，由后续夹取兜底）。
    if (work.right - work.left > 2 * kMenuEdgeGap)
    {
        work.left  += kMenuEdgeGap;
        work.right -= kMenuEdgeGap;
    }
    if (work.bottom - work.top > 2 * kMenuEdgeGap)
    {
        work.top    += kMenuEdgeGap;
        work.bottom -= kMenuEdgeGap;
    }

    return work;
}

POINT ClampMenuOriginToRect(POINT anchor, SIZE menuSize, RECT area)
{
    POINT pt = anchor;

    // 水平：默认向右展开；右边放不下则翻到锚点左侧；仍越界则贴右边缘；
    // 最后保证不越过左边缘（菜单比工作区还宽时取左边缘）。
    if (pt.x + menuSize.cx > area.right)
        pt.x = anchor.x - menuSize.cx;
    if (pt.x + menuSize.cx > area.right)
        pt.x = area.right - menuSize.cx;
    if (pt.x < area.left)
        pt.x = area.left;

    // 垂直：默认向下展开；下边放不下则翻到锚点上方；仍越界则贴下边缘；
    // 最后保证不越过上边缘（菜单比工作区还高时取上边缘）。
    if (pt.y + menuSize.cy > area.bottom)
        pt.y = anchor.y - menuSize.cy;
    if (pt.y + menuSize.cy > area.bottom)
        pt.y = area.bottom - menuSize.cy;
    if (pt.y < area.top)
        pt.y = area.top;

    return pt;
}

POINT ClampPopupMenuToWorkArea(POINT anchor, SIZE menuSize)
{
    return ClampMenuOriginToRect(anchor, menuSize, GetMenuPlacementArea(anchor));
}

POINT ComputeTrayAnchoredOrigin(RECT trayIcon, SIZE size, RECT area)
{
    //托盘图标矩形为空 = 没取到（图标被折叠进溢出区，或系统接口失灵）。退回可放置矩形的
    //右下角：任务栏绝大多数时候在下方，右下角就是托盘那一侧，观感上不会跑偏。
    const bool bNoIcon = (trayIcon.right <= trayIcon.left) || (trayIcon.bottom <= trayIcon.top);
    if (bNoIcon)
    {
        POINT pt;
        pt.x = area.right - size.cx;
        pt.y = area.bottom - size.cy;

        //浮窗比可放置矩形还大时会算出负向越界，统一夹到左上角（与下面同一口径）。
        if (pt.x < area.left)
            pt.x = area.left;
        if (pt.y < area.top)
            pt.y = area.top;

        return pt;
    }

    //以托盘图标左上角为锚点，交给与菜单共用的夹取逻辑：右 / 下放不下会自动翻到锚点的
    //左 / 上，正是任务栏在下时"浮窗贴在托盘图标左上方"的期望效果。
    POINT anchor;
    anchor.x = trayIcon.left;
    anchor.y = trayIcon.top;

    return ClampMenuOriginToRect(anchor, size, area);
}

POINT PlaceTrayAnchoredWindow(RECT trayIcon, SIZE size)
{
    //取可放置矩形要一个锚点：有图标就用图标左上角（多显示器下取图标所在那块屏），
    //没取到则用主屏右下角附近的点，落到主屏工作区上。
    POINT anchor;
    if ((trayIcon.right > trayIcon.left) && (trayIcon.bottom > trayIcon.top))
    {
        anchor.x = trayIcon.left;
        anchor.y = trayIcon.top;
    }
    else
    {
        anchor.x = ::GetSystemMetrics(SM_CXSCREEN) - 1;
        anchor.y = ::GetSystemMetrics(SM_CYSCREEN) - 1;
    }

    return ComputeTrayAnchoredOrigin(trayIcon, size, GetMenuPlacementArea(anchor));
}

} // namespace balloonmain
