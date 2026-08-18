/**
 *  TitleBarIcon.h
 *
 *  把程序主图标做成 balloonui 标题栏可用的位图。
 *
 *  DuiFrameWindow::SetIcon 收的是 HBITMAP，标题栏绘制时用 StretchBlt + SRCCOPY
 *  直接搬运、<u>不做 alpha 混合</u>（见 balloonui/Controls/Window/DuiFrameWindow.cpp
 *  的 icon 绘制段）。所以不能把带透明通道的图标直接交过去——透明区会变成黑块。
 *  必须先把图标合成到一块与标题栏同色的不透明底上，再把这块位图挂上去。
 *
 *  各窗口的"标题栏同色"取值不同，调用方自己清楚，故由参数传入：
 *    · 标题栏透明（SetTitleBarTransparent(true)）时，穿透显示的是根控件的背景色
 *      —— 传根控件那个色（如群管理窗 / 消息记录窗的白）。
 *    · 标题栏不透明时，balloonui 画的是 RGB(245,246,250)，传它即可
 *      （见 DuiFrameWindow.cpp 里 Title strip background 那段）。
 *
 *  本文件独立成一个共用模块，是因为这段合成逻辑此前在群管理窗与群公告弹窗里各抄
 *  了一份；再往下加窗口只会越抄越多，故收口到一处，各窗口只管传自己的尺寸与底色。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <windows.h>

namespace balloonmain {

/**
 *  用程序主图标（资源 IDR_MAINFRAME）生成一块指定边长、指定底色的不透明位图，
 *  供 balloonwjui::DuiFrameWindow::SetIcon 使用。
 *
 *  @param size 目标位图边长（像素）。标题栏最终按 16x16 绘制，传 16 可免去缩放；
 *              传更大值会在绘制时被 HALFTONE 缩小，仅在需要高 DPI 清晰度时才有必要
 *  @param bg   合成底色，须与该窗口标题栏实际显示的颜色一致，否则图标周围会出现色块
 *  @return 新建的 HBITMAP，<u>所有权归调用方</u>（DuiFrameWindow 只借用不释放），
 *          用毕须 ::DeleteObject；图标资源加载失败或 GDI 资源不足时返回 NULL
 */
HBITMAP CreateProgramIconBitmap(int size, COLORREF bg);

} // namespace balloonmain
