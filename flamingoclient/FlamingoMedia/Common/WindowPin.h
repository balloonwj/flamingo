/**
 *  通话窗标题栏「图钉」固定（docs/音视频通话优化方案.md 第 5 项），WindowPin.h
 *
 *  一对一通话窗与群通话窗的标题栏都有一个图钉图标（DuiFrameWindow 的 caption icon）。点一下固定：窗口置顶，不能拖动、
 *  不能改大小、不能最小化和最大化；再点一下解除。本文件放两个窗口共用的部分：图钉图标位图、提示文字，以及固定期间
 *  拦哪些系统命令、怎样保住窗口位置与大小的判定（纯函数，可单测，见 Source/Tests/CallUxTests.cpp 的 WPN1）。
 *  窗口自己负责：调 ApplyWindowTopmost 置顶或取消置顶、标题栏按钮（SetButtons）与能否拖边框（SetResizable），以及在
 *  消息映射里调这里的判定拦截 WM_SYSCOMMAND 与 WM_WINDOWPOSCHANGING。
 *
 *  典型用法（窗口的消息处理里）：
 *      if (m_pinned && flamingomedia::IsSysCommandBlockedWhenPinned(wParam)) return 0;   //WM_SYSCOMMAND
 *      if (m_pinned) flamingomedia::KeepWindowPosWhenPinned(reinterpret_cast<WINDOWPOS*>(lParam));   //WM_WINDOWPOSCHANGING
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>

namespace flamingomedia {

/**
 *  生成 16×16 的图钉图标位图（两个通话窗共用）。2026-10-06 起按方案 C 绘制（docs/音视频通话-界面美化方案.md
 *  第 10 节）：竖直的图钉，自上而下是圆角钉帽、钉身、上窄下宽的托盘与细针；未固定为与标题栏三按钮字形同色的线框，
 *  已固定为实心主题蓝并在底部多一条横线。边缘按每像素 8×8 个采样点的覆盖率抗锯齿。
 *  @param active true 为已固定，false 为未固定
 *  @return 32 位预乘 alpha 的位图，调用方持有所有权、用完 DeleteObject；失败时返回 NULL
 */
HBITMAP MakePinCaptionIcon(bool active);

/**
 *  图钉图标的悬停提示。
 *  @param pinned 当前是否已固定
 *  @return 已固定时为「取消固定」的说明，未固定时为「固定在桌面」的说明（已按当前界面语言翻译）
 */
const wchar_t* PinCaptionTooltip(bool pinned);

/**
 *  固定期间某个系统命令要不要拦下：移动、改大小、最小化、最大化、还原都拦（拖标题栏、拖边框、双击标题栏、标题栏
 *  按钮、Alt+空格菜单最终都落到这几个命令上）；关闭与其余命令照常。
 *  @param wParam WM_SYSCOMMAND 的 wParam（低 4 位是系统内部用的标志，判断前先去掉）
 *  @return 要拦下返回 true
 */
bool IsSysCommandBlockedWhenPinned(WPARAM wParam);

/**
 *  固定期间保住窗口的位置与大小：给 WM_WINDOWPOSCHANGING 的 WINDOWPOS 加上 SWP_NOMOVE 与 SWP_NOSIZE。置顶、显示隐藏等
 *  其余变化照常。
 *  @param wp WM_WINDOWPOSCHANGING 的 lParam；为 NULL 时什么也不做
 */
void KeepWindowPosWhenPinned(WINDOWPOS* wp);

/**
 *  把窗口设为置顶或取消置顶。置顶时先取消置顶、再置顶：2026-10-05 界面联调实测，新建的通话窗（一对一与群通话都如此）
 *  第一次 SetWindowPos(HWND_TOPMOST) 返回成功，扩展样式里却没有 WS_EX_TOPMOST、窗口也没有置顶，重复调用结果相同；
 *  先调一次 HWND_NOTOPMOST 之后再置顶才生效。原因尚未查明（主面板、单聊窗与来电弹窗没有这个现象）。
 *  @param hwnd    窗口；不是有效窗口时什么也不做
 *  @param topmost true 置顶，false 取消置顶
 *  @return 设置之后窗口是否处于置顶状态（读扩展样式里的 WS_EX_TOPMOST）
 */
bool ApplyWindowTopmost(HWND hwnd, bool topmost);

} // namespace flamingomedia
