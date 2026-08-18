/**
 *  通话窗视频界面（调试入口），CallVideoUiDebug.h
 *
 *  单机目检视频通话窗的画面渲染：不连服务器、不需第二台机器——直接以「视频通话中」形态打开
 *  通话窗（CBalloonCallDlg），起一个本机 CallVideoEngine，把它采集到的本端帧喂到通话窗的本地
 *  预览小窗（PiP），并把它编码出的帧本机回灌解码、解出的帧喂到通话窗的远端大画面区。于是远端
 *  画面区显示的就是「自己采集→编码→解码」后的画面、本地预览显示原始采集画面，两块都能在一台
 *  机器上看到（画面内容取决于摄像头；摄像头被策略锁时为灰色占位图，但足以核对窗口布局与渲染）。
 *
 *  用法：客户端命令行带 --callvideoui 启动，跳过登录直接弹该窗；关窗即停。仿 --videoloop /
 *  --camerapreview 等调试入口。
 *
 *  balloonwj@qq.com   2026-06-16
 */
#pragma once

namespace call {

//运行通话窗视频界面目检（阻塞到关窗）。成功（窗口与引擎都起来过）返回 true。
bool RunCallVideoUiDebug();

} // namespace call
