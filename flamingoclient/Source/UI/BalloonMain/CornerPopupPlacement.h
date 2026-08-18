/**
 *  桌面右下角浮窗的落点计算，CornerPopupPlacement.h
 *
 *  「新公告」提示窗与「特别关注」上下线提示窗都是贴桌面右下角弹出的置顶浮窗，
 *  落点算法完全相同。此前那份算法写在 `CAnnouncePopupWindow::MoveToWorkAreaCorner`
 *  里，第二个窗口要用时只能照抄一份 —— 与开发约定文档「UI 约定」第 3 条对菜单落点的
 *  要求同理：<u>几何运算只应有一份</u>，各调用点各写一份迟早会走样（改了边距只改一处、
 *  或一处忘了做工作区兜底）。故抽到本文件，两个窗口共用。
 *
 *  拆成两个函数：
 *    · ComputeCornerOrigin —— 纯函数，只做算术，可被 FlamingoUnit 直接断言。
 *    · MoveWindowToWorkAreaCorner —— 取显示器工作区、挪窗，碰 Win32，不进测试。
 *
 *  「工作区」（MONITORINFO::rcWork）是显示器里<u>排除任务栏之后</u>的那块矩形，故按它
 *  反推坐标能保证浮窗不被任务栏压住、也不会跑到屏幕外。
 *
 *  本文件只依赖 <windows.h>，不依赖任何控件库。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <windows.h>

namespace balloonmain {

//浮窗贴角时与工作区边缘留的间距（像素，逻辑坐标）。两个窗口用同一组值，
//免得同时弹出来时一个贴得紧、一个贴得松，看着像没对齐。
const int kCornerPopupMarginX = 16;   //距工作区右边缘
const int kCornerPopupMarginY = 16;   //距工作区下边缘

/**
 *  算出浮窗贴工作区右下角时的左上角坐标。纯函数、无副作用。
 *
 *    work：    目标显示器的工作区矩形（屏幕坐标，已排除任务栏）。
 *    wndSize： 浮窗的窗口尺寸（含边框，即 GetWindowRect 的宽高）。
 *    marginX： 距工作区右边缘的间距，像素；一般传 kCornerPopupMarginX。
 *    marginY： 距工作区下边缘的间距，像素；一般传 kCornerPopupMarginY。
 *
 *  @return 浮窗左上角的屏幕坐标。
 *
 *  兜底：窗口比工作区还大时（极端小分辨率 / 超大缩放），算出来的坐标会跑到工作区
 *  左上角之外，此时夹回工作区的左 / 上边界 —— 宁可右下角被截掉一块，也要保证左上角
 *  那部分（标题栏、关闭按钮）可见、关得掉。
 */
POINT ComputeCornerOrigin(const RECT& work, const SIZE& wndSize, int marginX, int marginY);

/**
 *  把窗口挪到"鼠标所在显示器"工作区的右下角。
 *
 *  取鼠标所在的显示器而不是主显示器：多显示器时用户正在看哪块屏，提示就该弹在哪块屏上。
 *  取不到显示器信息时<u>不挪窗</u>直接返回 —— 保持原位至少不会把窗口扔到屏幕外。
 *
 *    hwnd：   要挪的窗口；NULL 或已销毁时什么都不做。
 *    marginX：距工作区右边缘的间距，像素。
 *    marginY：距工作区下边缘的间距，像素。
 *
 *  副作用：调用 SetWindowPos 挪动窗口（不改尺寸、不改 Z 序、不激活）。
 */
void MoveWindowToWorkAreaCorner(HWND hwnd, int marginX, int marginY);

} // namespace balloonmain
