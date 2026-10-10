/**
 *  FlamingoCaption.dll 里各窗口投给自己的私有消息号，CaptionEditorMessages.h
 *
 *  按开发约定文档「UI 约定」第 6 条，窗口私有消息走 WM_APP 段并在开发约定文档里登记占用的偏移：本 DLL 占 0x110～0x114（0x100～0x104 已归 FlamingoLock.dll）。
 *  窗口私有消息即便与别的窗口同号也互不干扰，错开只是为了排查时一眼看出号属于谁。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

namespace caption {

//视频预览播放器的事件（媒体引擎线程投给编辑器窗口）：wParam 为 CaptionPlayerEvent
const UINT kMsgCaptionPlayer = WM_APP + 0x110;
//事后识别的进度与结束（识别线程投给编辑器窗口）：wParam 为 CaptionPostRecEvent，进度时 lParam 为千分比
const UINT kMsgCaptionPostRec = WM_APP + 0x111;
//导出进度（导出线程投给导出窗口）：wParam 为整次导出的千分比，lParam 为正在生成的种类（CaptionExportKind）
const UINT kMsgCaptionExportProgress = WM_APP + 0x112;
//导出结束（导出线程投给导出窗口）：lParam 为 new 出来的 CaptionExportResult*，窗口处理后 delete
const UINT kMsgCaptionExportDone = WM_APP + 0x113;
//编辑器窗口投给自己、留到当前控件通知或鼠标事件处理完再做的事：wParam 为编辑器内部的动作编号
const UINT kMsgCaptionDeferred = WM_APP + 0x114;

} // namespace caption
