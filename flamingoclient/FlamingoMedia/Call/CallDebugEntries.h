/**
 *  一对一通话的命令行调试入口（只在 Debug 构建中真正做事）。
 *
 *  迁入 FlamingoMedia.dll 之前这 7 个入口写在 exe 的 App/DebugEntryPoints.cpp 里；迁入后由
 *  IMediaModule::RunDebugEntry 调用本文件的 RunCallDebugEntry，exe 在检查自己的调试入口之前先交给模块。
 *  用法与迁移前完全相同：
 *    --callrelayselftest  进程内开两条中转连接连本机 imgserver（127.0.0.1:20003）互发测试帧
 *    --callaudioloop      本地音频回环：麦克风 → Opus 编码 → 解码 → 扬声器
 *    --camerapreview      打开默认摄像头实时预览
 *    --cameratest         自包含的摄像头显示测试，逐步打日志
 *    --h264selftest       合成 NV12 → H.264 编码 → 解码 → 首帧落 BMP
 *    --videoloop          摄像头 → H.264 编码 → 回灌解码 → 首帧落 BMP
 *    --callvideoui        以「视频通话中」形态打开通话窗，用本机摄像头回环填充画面
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include "../Api/FlamingoMediaApi.h"

namespace call {

/**
 *  按命令行运行通话的调试入口。Release 构建恒返回 media_debug_not_handled。
 *  @param commandLine 进程命令行（UTF-16），为 NULL 时返回未处理
 *  @param hwndHost    宿主窗口（结果消息框的属主）
 *  @param host        宿主接口（中转自测取服务端公钥用），可为 NULL（此时中转自测按明文连接）
 *  @return 结果，取值见 flamingomedia::MediaDebugEntryResult
 */
int RunCallDebugEntry(const wchar_t* commandLine, HWND hwndHost, flamingomedia::IMediaHost* host);

} // namespace call
