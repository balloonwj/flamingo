/**
 *  摄像头显示自包含测试（调试入口），CameraDisplayTest.h
 *
 *  专为「摄像头数据显示不出来」定位问题：不经过 MfCameraCapture / CallVideoEngine / 通话窗，
 *  在本文件内自己走一遍 Media Foundation 摄像头采集的全过程，并在**每一步**打详细日志到固定
 *  文件 E:\flamingo\cameratest.log（同时镜像到客户端日志器），再把采到的 NV12 帧转 BGRA 显示
 *  到一个 Win32 窗口里。日志覆盖：COM/MF 初始化、设备枚举（含设备名）、源 / 读取器创建、相机
 *  原生支持的格式列表、NV12 输出协商、协商到的宽高 / 行跨度、逐帧（前若干帧 + 周期性）的标志 /
 *  数据长度 / Y 平面亮度极值（判断是真实画面还是灰色占位图）、窗口绘制次数。关窗时弹框汇总。
 *
 *  用法：客户端命令行带 --cameratest 启动，跳过登录直接弹测试窗；关窗即停并弹统计。
 *
 *  balloonwj@qq.com   2026-06-16
 */
#pragma once

namespace call {

//运行摄像头显示自包含测试（阻塞到关窗）。成功打开摄像头并进入采集循环返回 true。
bool RunCameraDisplayTest();

} // namespace call
