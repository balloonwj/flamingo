/**
 *  屏幕共享控制器消息窗口的私有消息，ShareMessages.h
 *
 *  屏幕共享控制器（CShareController）建一个只收消息的窗口（HWND_MESSAGE），推流 / 收流的工作线程与 DLL 内的各个
 *  窗口（选择窗、控制条、观看窗、右下角提示）都把事件 PostMessage 给它，由它在 UI 线程上统一处理。
 *  这些都是窗口私有消息，按开发约定文档「UI 约定」第 6 条走 WM_APP 段：0x70~0x72 已归一对一通话与远程桌面，
 *  0x80 归 FlamingoFileShare.dll，屏幕共享取 0x73~0x7C，另有 0x91~0x95、0x97~0x99、0x9A~0x9C（0x7D~0x7F、0x90 已归录屏，
 *  0x96 已归语音消息）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>

namespace screenshare {

//推流管线事件：wParam = share_host_event，lParam = 附加值（来自推流工作线程）
const UINT kMsgShareHostEvent = WM_APP + 0x73;
//收流管线事件：wParam = share_viewer_event，lParam = 附加值（来自观看窗）
const UINT kMsgShareViewerEvent = WM_APP + 0x74;
//选择窗的结果：wParam = 1 选好了（lParam = 堆上的 SharePickerResult*，由控制器释放）/ 0 取消
const UINT kMsgSharePickerResult = WM_APP + 0x75;
//控制条按钮：wParam = CShareControlBar::Command
const UINT kMsgShareBarCommand = WM_APP + 0x76;
//观看窗命令：wParam = CShareViewerWindow::Command
const UINT kMsgShareViewerCommand = WM_APP + 0x77;
//右下角提示的按钮：wParam = CShareCornerToast::Command，lParam = 提示对应的房间序号（控制器据此找到房间）
const UINT kMsgShareToastCommand = WM_APP + 0x78;
//控制条跟随：定时器之外的即时刷新请求（如切换了共享内容），wParam / lParam 不用
const UINT kMsgShareFollowSource = WM_APP + 0x79;
//收到一帧标注（来自中转接收线程）：lParam = 堆上的 ShareAnnotationFrame*，由控制器释放
const UINT kMsgShareAnnotFrame = WM_APP + 0x7A;
//标注工具条按钮：wParam = CShareAnnotationBar::Command，lParam = 附加值（工具、颜色、权限）
const UINT kMsgShareAnnotCommand = WM_APP + 0x7B;
//标注文字输入框的结果：lParam = 堆上的 std::wstring*（确定，由控制器释放）/ 0（取消）
const UINT kMsgShareAnnotText = WM_APP + 0x7C;
//选择窗投给自己、留到当前通知处理完再做的事（第 12 期）：wParam = CSharePickerDialog::DeferredAction，lParam 不用。
//控件通知是同步发出的：点「刷新」后重建界面会释放发出通知的按钮；密码框的「文字变了」通知在文本引擎处理按键的中途发出，
//当场改写文字会形成对引擎的重入调用。两者都改为投递本消息后再处理
const UINT kMsgSharePickerDeferred = WM_APP + 0x91;
//加入密码输入框的结果（第 12 期）：lParam = 堆上的 std::wstring*（确定，由控制器释放）/ 0（取消）
const UINT kMsgSharePasswordResult = WM_APP + 0x92;
//观看者名单浮窗的按钮（第 13 期）：wParam = CShareViewerListPanel::Command，lParam = 被操作的观看者的座位号
const UINT kMsgShareViewerListCommand = WM_APP + 0x93;
//移出观看者确认框的结果（第 14 期）：wParam = 1 确认 / 0 取消，lParam = 「允许他再次加入」是否勾选（1 / 0）
const UINT kMsgShareRemoveConfirmResult = WM_APP + 0x94;
//「修改标题与描述」小窗的结果（第 15 期）：lParam = 堆上的 ShareInfoEditResult*（保存，由控制器释放）/ 0（取消）
const UINT kMsgShareInfoEditResult = WM_APP + 0x95;
//控制申请提示的按钮（第 18 期，右下角「某某申请控制你的屏幕」）：wParam = CShareCornerToast::Command（加入即「同意」、
//忽略即「拒绝」，超时也按拒绝），lParam = 申请人的 userid（截成 LPARAM；userid 不超过 32 位）
const UINT kMsgShareControlToast = WM_APP + 0x97;
//收到一帧键鼠输入（第 18 期，来自中转接收线程）：lParam = 堆上的 ShareInputFrameMsg*，由控制器释放
const UINT kMsgShareInputFrame = WM_APP + 0x98;
//WM_APP + 0x99 已作废、不得复用：原为「画面主人本机有动静」（第 18 期，低级鼠标、键盘钩子里判定后收回控制），
//2026-10-12 起被控制期间画面主人可以照常操作，不再自动收回，钩子与这条消息一起删除
//共享申请提示的按钮（第 22 期，主持人右下角「某某申请共享屏幕」）：wParam = CShareCornerToast::Command（加入即「同意」、
//忽略即「拒绝」，超时也按拒绝），lParam = 申请人的 userid（截成 LPARAM；userid 不超过 32 位）
const UINT kMsgShareSharingToast = WM_APP + 0x9A;
//共享者推流管线（第 22 期，主持人以外的共享者另起的那一条）的事件：wParam = share_host_event，lParam = 附带的值；
//由推流的工作线程投递
const UINT kMsgSharePublisherEvent = WM_APP + 0x9B;
//共享者控制条与主持人退出选择框的命令（第 22 期）：wParam = 命令（见 ShareSharerBar.h 的 ShareSharerCommand），
//lParam = 附带的 userid（移交主持人时为新主持人，截成 LPARAM）
const UINT kMsgShareSharerCommand = WM_APP + 0x9C;
//白板窗口与白板画布的命令（第 23 期，房间里的共享白板）：wParam = ShareBoardCommand（见 ShareWhiteboardWindow.h），
//lParam = 附带的值（换背景时为新背景）
const UINT kMsgShareBoardCommand = WM_APP + 0x9D;
//「已保存为图片」提示的按钮（第 23 期）：wParam = CShareCornerToast::Command（加入即「发送到本会话」、忽略即「关闭」），
//lParam = 提示的标识
const UINT kMsgShareSaveToast = WM_APP + 0x9E;
//新建白板申请提示的按钮（第 23 期，主持人右下角「某某申请新建白板」）：wParam = CShareCornerToast::Command（加入即「同意」、
//忽略即「拒绝」，超时也按拒绝），lParam = 申请人的 userid（截成 LPARAM）
const UINT kMsgShareBoardToast = WM_APP + 0x9F;
//凭分享链接加入的确认小窗（2026-10-07，方案第 14.5 节，替换原来的系统消息框）的结果：wParam = 1 加入、0 取消或关窗，
//lParam 不用（CShareConfirmDialog 投来）
const UINT kMsgShareLinkJoinResult = WM_APP + 0xD4;
//主持人「其他人的屏幕」窗口顶部的按钮（2026-10-07，方案第 14.3 节第 4 条）：wParam = CShareScreensWindow::Command
//（标注、申请控制），lParam 不用（CShareScreensWindow 投来）
const UINT kMsgShareScreensCommand = WM_APP + 0xD5;
//主持人关闭别人白板的确认框（2026-10-08）的结果：wParam = 1 关闭、0 取消或关窗，lParam 不用（CShareConfirmDialog 投来）
const UINT kMsgShareBoardCloseResult = WM_APP + 0xD7;

} // namespace screenshare
