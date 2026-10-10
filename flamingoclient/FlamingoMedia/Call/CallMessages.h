/**
 *  通话控制器（CCallController）消息窗口的私有窗口消息号。
 *
 *  通话迁入 FlamingoMedia.dll 之前，这两件事走的是 exe 的 FMG_MSG_CALL_STOP_MEDIA /
 *  FMG_MSG_CALL_RELAY_EVENT，投给主面板；迁入后由 DLL 自己的消息窗口接收，消息号按仓库约定
 *  走窗口私有的 WM_APP 段（开发约定文档「UI 约定」第 6 条，偏移 0x70~0x71 登记在该条的占用清单里）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>

namespace call {

//通话窗关窗时请求控制器停掉媒体会话（释放摄像头与麦克风）。wParam、lParam 未用。
//由通话窗在 UI 线程投递，异步处理是为了避免在通话窗自己的销毁过程中停媒体会话。
const UINT kMsgCallStopMedia = WM_APP + 0x70;

//媒体会话（CallMediaSession）的中转事件，从接收线程 / 采集线程投回 UI 线程。
//wParam = 事件码（call::CallRelayEvent），lParam = 事件参数（摄像头不可用时为失败原因）。
const UINT kMsgCallRelayEvent = WM_APP + 0x71;

} // namespace call
