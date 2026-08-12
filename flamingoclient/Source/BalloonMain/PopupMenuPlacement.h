/**
 *  PopupMenuPlacement.h
 *
 *  弹出菜单（balloonui DuiMenu）的"落点"计算：把菜单左上角限制在显示器工作区内，
 *  避免锚点靠近屏幕右 / 下边缘时整张菜单跑到桌面外看不见。
 *
 *  <u>2026-08-12 起 DuiMenu 自己会做这件事了</u>：几何运算已下沉到
 *  balloonui/Controls/List/DuiMenuPlacement.h，并由 DuiMenuPopup 在弹出前统一
 *  调用，所以<u>菜单</u>的防越界不再需要调用方操心；本模块的三个同名函数现在
 *  只是转发到库里那一份（这一步幂等，转发过去再处理一次也不会改变结果）。
 *
 *  本模块继续存在的意义有两条：一是 ComputeTrayAnchoredOrigin /
 *  PlaceTrayAnchoredWindow 这类"托盘浮窗"定位不走 DuiMenu，仍需自己算落点；
 *  二是保留客户端侧既有调用点与其单元测试，不必逐个改名。
 *
 *  核心 ClampMenuOriginToRect 是纯函数（只做坐标运算、不碰窗口 / 显示器 API），
 *  便于 FlamingoUnit 测试工程直接断言；ClampPopupMenuToWorkArea 是它的 Win32
 *  包装，负责取"锚点所在显示器的工作区"。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <windows.h>      // POINT / SIZE / RECT

namespace balloonmain {

/**
 *  把弹出菜单的左上角限制在给定矩形内（通常是显示器工作区），保证整张菜单可见。
 *
 *  策略（与 Win32 TrackPopupMenu 的默认观感一致）：
 *    · 水平：默认从锚点向右展开；右边放不下则翻到锚点左侧；仍越界则贴右边缘。
 *    · 垂直：默认从锚点向下展开；下边放不下则翻到锚点上方；仍越界则贴下边缘。
 *  两个方向独立处理；最后再限制一次，保证不越过左 / 上边缘（菜单比矩形还大时取边缘）。
 *
 *    anchor：   期望的菜单左上角（屏幕坐标，光标点 / 按钮锚点等）。
 *    menuSize： 菜单尺寸（来自 DuiMenu::MeasureSize()）。
 *    area：     允许显示的矩形（屏幕坐标，一般为显示器工作区 rcWork）。
 *
 *  返回：修正后的菜单左上角屏幕坐标。
 */
POINT ClampMenuOriginToRect(POINT anchor, SIZE menuSize, RECT area);

/**
 *  取 anchor 所在显示器、已留出边缘间隙的"菜单可放置矩形"。
 *
 *  = 显示器工作区 `rcWork`（已排除任务栏，多显示器安全）再四周向内缩一道固定
 *  间隙，使菜单浮在任务栏 / 屏幕边缘之上留一道缝，并避开
 *  popup 系统阴影下溢到任务栏造成的"底部被吃掉"错觉。取不到工作区时退化到主屏整屏。
 *
 *    anchor：菜单锚点（屏幕坐标，通常为光标点）。
 *
 *  返回：可放置矩形（屏幕坐标）。调用方把它连同 anchor / menuSize 交给
 *        ClampMenuOriginToRect 得到最终落点；需要记录日志时可先取本矩形再做修正。
 */
RECT GetMenuPlacementArea(POINT anchor);

/**
 *  ClampMenuOriginToRect 的 Win32 包装：取 anchor 所在显示器的工作区（排除任务栏，
 *  多显示器安全），再调用纯函数做修正。取不到工作区时退化到主屏整屏。
 *
 *    anchor / menuSize：含义同上。
 *
 *  返回：修正后的菜单左上角屏幕坐标，可直接传给 DuiMenu::TrackPopup。
 */
POINT ClampPopupMenuToWorkArea(POINT anchor, SIZE menuSize);

/**
 *  算"贴着托盘图标弹出的浮窗"的左上角落点（纯函数）。
 *
 *  用于被踢下线提示框这类"从托盘长出来"的小窗：锚在托盘图标上，再经与菜单同一套
 *  落点修正逻辑保证整窗可见。任务栏在下时托盘位于屏幕右下角，修正过程会自然把浮窗翻到图标
 *  的左上方；任务栏在上则向下展开 —— 方向不需要调用方判断。
 *
 *    trayIcon： 托盘图标的屏幕矩形（CTrayIcon::GetTrayIconRect 的产物）。<u>空矩形</u>
 *               表示没取到（图标被折叠进溢出区、或系统接口失灵），此时退回 area 的
 *               右下角，观感上仍在"托盘那一侧"。
 *    size：     浮窗尺寸（含边框）。
 *    area：     允许放置的矩形（屏幕坐标，一般取 GetMenuPlacementArea 的产物）。
 *
 *  返回：修正后的左上角屏幕坐标；浮窗不比 area 大时保证整窗落在 area 内。
 */
POINT ComputeTrayAnchoredOrigin(RECT trayIcon, SIZE size, RECT area);

/**
 *  ComputeTrayAnchoredOrigin 的 Win32 包装：自行取托盘图标所在显示器的可放置矩形
 *  （GetMenuPlacementArea），再调纯函数算落点。
 *
 *    trayIcon / size：含义同上。
 *
 *  返回：可直接用于 SetWindowPos / MoveWindow 的左上角屏幕坐标。
 */
POINT PlaceTrayAnchoredWindow(RECT trayIcon, SIZE size);

} // namespace balloonmain
