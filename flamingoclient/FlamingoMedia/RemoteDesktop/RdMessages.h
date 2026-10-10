/**
 *  远程桌面控制器（CRdController）与调试入口（CRdDebugEntries）消息窗口的私有窗口消息号。
 *
 *  远程桌面迁入 FlamingoMedia.dll 之前，受控提示条的「结束控制」走 exe 的
 *  FMG_MSG_RD_CONTROLLED_END_BY_USER，投给主面板；迁入后由 DLL 自己的消息窗口接收，消息号按仓库约定
 *  走窗口私有的 WM_APP 段（开发约定文档「UI 约定」第 6 条，偏移 0x72 登记在该条的占用清单里）。
 *  P2P 方案第 6 期新增推流器连接失败的通知（偏移 0xB0，同样登记在那份清单里）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>

namespace remotedesktop {

//被控端用户点受控提示条的「结束控制」，请求收尾本次会话（发 cmd_rd_close、停推流、关提示条）。
//wParam、lParam 未用。由受控提示条在 UI 线程投递，目标是调用 CRdControlledBar::Show 时登记的通知窗口。
const UINT kMsgRdControlledEndByUser = WM_APP + 0x72;

//被控端推流器建立传输失败（P2P 与 imgserver 中转都没连上，P2P 方案第 6 期起连接在推流线程上异步进行），请求
//提示用户并结束会话。wParam 为 RdControlledStreamer::StartSeq（与当前值不同的是上一次会话的迟到通知，丢弃），
//lParam 未用。由推流线程投递，目标是 RdControlledStreamer::Start 时给的通知窗口（远程桌面控制器的消息窗口）。
const UINT kMsgRdStreamerConnectFailed = WM_APP + 0xB0;

} // namespace remotedesktop
