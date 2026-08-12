/**
 *  PopupMenuPlacement.cpp
 *
 *  弹出菜单落点修正的实现。<u>几何运算本身已经下沉到 balloonui</u>
 *  （balloonui/Controls/List/DuiMenuPlacement.h），本文件的同名函数只是转发 ——
 *  这样 balloonui 内部（DuiMenuPopup 弹出前的自动修正落点）与客户端这一侧
 *  （托盘浮窗定位等）共用同一份实现，不会各写一份然后慢慢走样。
 *
 *  纯坐标运算部分与 FlamingoUnit 测试工程共用、不 include stdafx.h，PCH 设
 *  NotUsing；被转发的头文件只依赖 <windows.h>，不会把控件体系带进这个 TU。
 *
 *  balloonwj@qq.com   2026-06-03（2026-08-12 改为转发到 balloonui）
 */

#include "PopupMenuPlacement.h"

#include "../../balloonui/balloonui/Controls/List/DuiMenuPlacement.h"

namespace balloonmain {

RECT GetMenuPlacementArea(POINT anchor)
{
    return balloonwjui::GetMenuPlacementArea(anchor);
}

POINT ClampMenuOriginToRect(POINT anchor, SIZE menuSize, RECT area)
{
    return balloonwjui::ClampMenuOriginToRect(anchor, menuSize, area);
}

POINT ClampPopupMenuToWorkArea(POINT anchor, SIZE menuSize)
{
    return balloonwjui::ClampMenuOriginToWorkArea(anchor, menuSize);
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

        //浮窗比可放置矩形还大时会算出负向越界，统一收到左上角（与下面同一口径）。
        if (pt.x < area.left)
            pt.x = area.left;
        if (pt.y < area.top)
            pt.y = area.top;

        return pt;
    }

    //以托盘图标左上角为锚点，交给与菜单共用的落点修正逻辑：右 / 下放不下会自动翻到锚点的
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
