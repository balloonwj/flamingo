/**
 *  桌面右下角浮窗落点计算的实现，CornerPopupPlacement.cpp
 *  设计与约定见 CornerPopupPlacement.h。
 *
 *  本文件是纯 Win32 薄层，<u>不</u> include stdafx.h（工程里对应的 PrecompiledHeader
 *  设为 NotUsing），与同目录的 FavoritesModel.cpp 等纯逻辑模块同例。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "CornerPopupPlacement.h"

namespace balloonmain {

POINT ComputeCornerOrigin(const RECT& work, const SIZE& wndSize, int marginX, int marginY)
{
    POINT pt;
    //贴工作区右下角，各留一段边距。按工作区反推坐标，天然落在可见范围内。
    pt.x = work.right - wndSize.cx - marginX;
    pt.y = work.bottom - wndSize.cy - marginY;

    //窗口比工作区还大时退到工作区左上角，保证标题栏可见、关得掉。
    if (pt.x < work.left)
    {
        pt.x = work.left;
    }
    if (pt.y < work.top)
    {
        pt.y = work.top;
    }
    return pt;
}

void MoveWindowToWorkAreaCorner(HWND hwnd, int marginX, int marginY)
{
    if (hwnd == NULL || !::IsWindow(hwnd))
    {
        return;
    }

    //取鼠标所在显示器的工作区（已排除任务栏），多显示器下弹在用户正看的那块屏上。
    POINT cursor;
    ::GetCursorPos(&cursor);
    HMONITOR mon = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi;
    ::ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfo(mon, &mi))
    {
        //取不到就退回不挪窗，至少别把窗口扔到屏幕外。
        return;
    }

    RECT rcWnd;
    ::GetWindowRect(hwnd, &rcWnd);
    SIZE sz;
    sz.cx = rcWnd.right - rcWnd.left;
    sz.cy = rcWnd.bottom - rcWnd.top;

    const POINT at = ComputeCornerOrigin(mi.rcWork, sz, marginX, marginY);
    ::SetWindowPos(hwnd, NULL, at.x, at.y, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace balloonmain
