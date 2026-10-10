/**
 *  群通话控制器的私有窗口消息，GroupCallMessages.h
 *
 *  FlamingoMedia.dll 的群通话控制器（CGroupCallController）有一个只收消息的窗口：中转连接线程上的事件、来电窗与
 *  通话窗的按钮都经它转回 UI 线程处理。这些消息只投给这个窗口（0xC4 投给通话窗自己），号段按开发约定文档「UI 约定」
 *  第 6 条取 WM_APP 段，0xC0~0xC4 归群通话。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>

namespace groupcall {

//媒体会话的事件（中转连接线程 → UI 线程）：wParam 为事件（gc_media_event），lParam 为媒体会话的代号（防止旧会话的事件
//打到新会话上）
const UINT kMsgGroupCallMediaEvent = WM_APP + 0xC0;

//来电窗的按钮（来电窗 → 控制器）：wParam 为命令（gc_incoming_command），lParam 不用
const UINT kMsgGroupCallIncomingCommand = WM_APP + 0xC1;

//通话窗的按钮（通话窗 → 控制器）：wParam 为命令（gc_window_command），lParam 为附带的值（如目标 userid 的低 32 位不够，
//管理操作的目标改由通话窗记在成员里，见 CGroupCallWindow::TakePendingTarget）
const UINT kMsgGroupCallWindowCommand = WM_APP + 0xC2;

//控制器投给自己、留到当前控件通知处理完再做的事：wParam 为 gc_deferred_action
const UINT kMsgGroupCallDeferred = WM_APP + 0xC3;

//通话窗投给自己、留到按钮通知处理完再弹的「成员管理」菜单（第 6 期）：wParam、lParam 不用
const UINT kMsgGroupCallWindowMenu = WM_APP + 0xC4;

//媒体会话的事件
enum gc_media_event
{
    gc_media_connected    = 1,   //房间中转已连上并报到成功
    gc_media_relay_failed = 2,   //连不上房间中转（报到被拒或网络不通）
    gc_media_relay_lost   = 3,   //通话途中房间中转断开
};

//来电窗的命令
enum gc_incoming_command
{
    gc_incoming_accept  = 1,   //点了「接听」
    gc_incoming_decline = 2,   //点了「拒绝」或关掉了来电窗
};

//通话窗的命令
enum gc_window_command
{
    gc_window_toggle_mic    = 1,   //点了麦克风按钮
    gc_window_toggle_camera = 2,   //点了摄像头按钮
    gc_window_invite        = 3,   //点了「邀请成员」
    gc_window_hangup        = 4,   //点了「挂断」或关掉了通话窗
    gc_window_mute_member   = 5,   //成员右键菜单「静音」（第 6 期）
    gc_window_remove_member = 6,   //成员右键菜单「移出通话」（第 6 期）
    gc_window_mute_all      = 7,   //勾选「全员静音」：开启全员静音（第 6 期；2026-10-05 起是房间状态）
    gc_window_unmute_all    = 8,   //取消勾选「全员静音」：解除全员静音（2026-10-05）
};

//留到稍后再做的事
enum gc_deferred_action
{
    gc_deferred_retry_relay = 1,   //中转断开后重新加入一次，换一份新令牌再连
};

} // namespace groupcall
