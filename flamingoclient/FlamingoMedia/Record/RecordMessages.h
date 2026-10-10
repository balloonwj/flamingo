/**
 *  录屏控制器消息窗口的私有消息，RecordMessages.h
 *
 *  录屏控制器（CRecordController）建一个只收消息的窗口（HWND_MESSAGE），框选遮罩、录制条、完成提示与录制的写入线程
 *  都把事件 PostMessage 给它。窗口私有消息按开发约定文档「UI 约定」第 6 条走 WM_APP 段：0x70~0x7C 已归一对一通话、
 *  远程桌面与屏幕共享，0x80、0x81 归 FlamingoFileShare.dll，录屏取 0x7D~0x7F 与 0x90，录制管理窗口取 0xF0。
 *  选择录制范围复用共享的选择窗，结果消息仍是 ShareMessages.h 的 kMsgSharePickerResult（投给本控制器的窗口）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>

namespace screenrecord {

//框选遮罩的结果：wParam = 1 选好了（lParam = 堆上的 RECT*，屏幕坐标，由控制器释放）/ 0 取消
const UINT kMsgRecordRegionResult = WM_APP + 0x7D;
//录制条按钮：wParam = CRecordBar::Command
const UINT kMsgRecordBarCommand = WM_APP + 0x7E;
//录制已结束（来自写入线程）：wParam = RecordStopReason，lParam = 堆上的 RecordStoppedInfo*，由控制器释放
const UINT kMsgRecordStopped = WM_APP + 0x7F;
//完成提示的按钮：wParam = CRecordDoneToast::Command
const UINT kMsgRecordDoneCommand = WM_APP + 0x90;
//录制管理窗口（2026-10-05，docs/屏幕共享与录制方案.md 第 11.6 节 F9）投给自己、留到当前控件通知或鼠标事件处理完再做的事：
//wParam = CRecordManagerWindow::DeferredAction（弹右键菜单、提交或取消原地重命名），lParam 视动作而定（右键菜单为行号）
const UINT kMsgRecordManagerDeferred = WM_APP + 0xF0;

} // namespace screenrecord
