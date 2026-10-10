/**
 *  摄像头本地预览（调试入口），CameraPreviewDebug.h
 *
 *  第三期视频「采集 + 显示」层的本地验证：打开默认摄像头（MfCameraCapture），把采集到的
 *  NV12 帧转成 BGRA 画到一个预览窗口里——对着摄像头能看到自己的实时画面。验证采集与显示
 *  这层（与 H.264 编解码无关），是接入真实视频通话前的本地目视验证，仿音频的 --callaudioloop。
 *
 *  balloonwj@qq.com   2026-06-15
 */
#pragma once

namespace call {

/**
 *  运行一次摄像头本地预览（--camerapreview 调试入口）：建预览窗、起摄像头采集，进消息
 *  循环显示实时画面，用户关窗后停止。需有可用摄像头。
 *  @return 成功启动并运行过返回 true；摄像头打开失败返回 false
 */
bool RunCameraPreviewDebug();

} // namespace call
