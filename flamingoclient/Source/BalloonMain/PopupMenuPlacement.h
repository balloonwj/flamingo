/**
 *  PopupMenuPlacement.h
 *
 *  弹出菜单（balloonui DuiMenu）的"落点"计算。balloonui 的 DuiMenuPopup 把菜单
 *  左上角直接放在传入坐标、<u>既不自动翻转也不夹取</u>（见 DuiMenu.h 说明），
 *  所以一旦锚点靠近屏幕右/下边缘，菜单就会跑到桌面外看不见（典型：托盘图标
 *  在任务栏、右键时光标在屏幕底部）。本模块负责在调用 TrackPopup <u>之前</u>
 *  把落点夹进显示器工作区，保证整张菜单可见。
 *
 *  约定（写入开发约定文档 "UI 约定"）：所有弹出 / 右键 / 上下文菜单在 TrackPopup
 *  前都必须先经本模块夹取，不允许把原始光标 / 锚点坐标直接传给 TrackPopup。
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
 *  把弹出菜单的左上角夹进给定矩形（通常是显示器工作区），保证整张菜单可见。
 *
 *  策略（与 Win32 TrackPopupMenu 的默认观感一致）：
 *    · 水平：默认从锚点向右展开；右边放不下则翻到锚点左侧；仍越界则贴右边缘。
 *    · 垂直：默认从锚点向下展开；下边放不下则翻到锚点上方；仍越界则贴下边缘。
 *  两个方向独立处理；最后再夹一次保证不越过左 / 上边缘（菜单比矩形还大时取边缘）。
 *
 *    anchor：   期望的菜单左上角（屏幕坐标，光标点 / 按钮锚点等）。
 *    menuSize： 菜单尺寸（来自 DuiMenu::MeasureSize()）。
 *    area：     允许显示的矩形（屏幕坐标，一般为显示器工作区 rcWork）。
 *
 *  返回：夹取后的菜单左上角屏幕坐标。
 */
POINT ClampMenuOriginToRect(POINT anchor, SIZE menuSize, RECT area);

/**
 *  取 anchor 所在显示器、已留出边缘间隙的"菜单可放置矩形"。
 *
 *  = 显示器工作区 `rcWork`（已排除任务栏，多显示器安全）再四周向内缩一道固定
 *  间隙，使菜单浮在任务栏 / 屏幕边缘之上留一道缝（观感对齐 QQ 托盘菜单），并避开
 *  popup 系统阴影下溢到任务栏造成的"底部被吃掉"错觉。取不到工作区时退化到主屏整屏。
 *
 *    anchor：菜单锚点（屏幕坐标，通常为光标点）。
 *
 *  返回：可放置矩形（屏幕坐标）。调用方把它连同 anchor / menuSize 交给
 *        ClampMenuOriginToRect 得到最终落点；需要记录日志时可先取本矩形再夹取。
 */
RECT GetMenuPlacementArea(POINT anchor);

/**
 *  ClampMenuOriginToRect 的 Win32 包装：取 anchor 所在显示器的工作区（排除任务栏，
 *  多显示器安全），再调用纯函数夹取。取不到工作区时退化到主屏整屏。
 *
 *    anchor / menuSize：含义同上。
 *
 *  返回：夹取后的菜单左上角屏幕坐标，可直接传给 DuiMenu::TrackPopup。
 */
POINT ClampPopupMenuToWorkArea(POINT anchor, SIZE menuSize);

/**
 *  算"贴着托盘图标弹出的浮窗"的左上角落点（纯函数）。
 *
 *  用于被踢下线提示框这类"从托盘长出来"的小窗：锚在托盘图标上，再经与菜单同一套
 *  夹取逻辑保证整窗可见。任务栏在下时托盘位于屏幕右下角，夹取会自然把浮窗翻到图标
 *  的左上方；任务栏在上则向下展开 —— 方向不需要调用方判断。
 *
 *    trayIcon： 托盘图标的屏幕矩形（CTrayIcon::GetTrayIconRect 的产物）。<u>空矩形</u>
 *               表示没取到（图标被折叠进溢出区、或系统接口失灵），此时退回 area 的
 *               右下角，观感上仍在"托盘那一侧"。
 *    size：     浮窗尺寸（含边框）。
 *    area：     允许放置的矩形（屏幕坐标，一般取 GetMenuPlacementArea 的产物）。
 *
 *  返回：夹取后的左上角屏幕坐标；浮窗不比 area 大时保证整窗落在 area 内。
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
