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

// 标题栏程序图标的常用边长（像素）：标题栏最终按 16x16 绘制
const int kProgramTitleIconSize = 16;

// 不透明标题栏的底色：SetTitleBarTransparent(false)（默认）时 balloonui 画的就是这个颜色
const COLORREF kOpaqueTitleBarColor = RGB(245, 246, 250);

/**
 *  ProgramWindowIcons —— 一个窗口的程序图标：标题栏左侧的位图，以及任务栏 / Alt-Tab 用的大、小两枚图标。
 *
 *  三者都由本对象持有、析构时释放：DuiFrameWindow::SetIcon 只借用位图不释放，WM_SETICON 交给系统的图标系统也不释放。
 *  第一次用到时才加载，窗口关闭再打开（同一个 C++ 对象重新建窗）时复用，不重复加载。图标资源一律从 exe 模块
 *  （GetModuleHandle(NULL)）加载，所以编进 FlamingoMedia.dll、FlamingoFileShare.dll 的窗口同样取得到。
 *  2026-10-03 起文件共享的各窗口使用；此前各窗口把这段代码各写了一份（如远程桌面对话框）。
 *
 *  典型用法（DuiFrameWindow 子类建窗之后，m_programIcons 是成员）：
 *      SetIcon(m_programIcons.TitleBitmap(kProgramTitleIconSize, kOpaqueTitleBarColor));
 *      m_programIcons.ApplyTaskIcons(m_hWnd);
 */
class ProgramWindowIcons
{
public:
    ProgramWindowIcons();

    // 释放持有的位图与图标。用它们的窗口此时应已销毁（窗口是同一个对象的成员时，由所属对象先关闭窗口）。
    ~ProgramWindowIcons();

    /**
     *  标题栏位图：第一次调用时按 size / bg 生成（见 CreateProgramIconBitmap），之后返回同一块，参数不再起作用。
     *  @param size 边长（像素），一般传 kProgramTitleIconSize
     *  @param bg   合成底色，须与标题栏实际显示的颜色一致，不透明标题栏传 kOpaqueTitleBarColor
     *  @return 位图句柄，所有权归本对象；图标资源加载失败时返回 NULL（标题栏不显示图标）
     */
    HBITMAP TitleBitmap(int size, COLORREF bg);

    /**
     *  给窗口设置任务栏 / Alt-Tab 图标（WM_SETICON 的 ICON_BIG 与 ICON_SMALL）。图标第一次调用时按系统的大、小图标尺寸加载。
     *  @param hwnd 目标窗口；不是有效窗口时不做任何事。图标加载失败的那一枚不设置
     */
    void ApplyTaskIcons(HWND hwnd);

private:
    // 持有 GDI 句柄，不可复制
    ProgramWindowIcons(const ProgramWindowIcons&);
    ProgramWindowIcons& operator=(const ProgramWindowIcons&);

    HBITMAP m_titleBitmap;  // 标题栏位图，第一次 TitleBitmap 时生成；本对象拥有，析构时 DeleteObject
    HICON   m_bigIcon;      // 任务栏 / Alt-Tab 的大图标，第一次 ApplyTaskIcons 时加载；本对象拥有，析构时 DestroyIcon
    HICON   m_smallIcon;    // 任务栏 / Alt-Tab 的小图标（按小尺寸显示时使用），同上
};

} // namespace balloonmain
