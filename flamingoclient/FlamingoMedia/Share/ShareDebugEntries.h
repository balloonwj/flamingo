/**
 *  屏幕共享的命令行调试入口（只在 Debug 构建中真正做事），ShareDebugEntries.h
 *
 *  采集、编码这类依赖系统设备的代码不进自动化单测（docs/屏幕共享与录制方案.md 9.4），改由调试入口运行一遍，
 *  把结果写成文本与图片供人工核对。由 IMediaModule::RunDebugEntry 调用：
 *    --sharecapture  枚举显示器与可共享窗口；取主显示器与前几个窗口的缩略图；用 DXGI 采主显示器一帧；
 *                    开一个画着红绿蓝色块的测试窗口，用 Windows.Graphics.Capture 采它一帧并核对色块颜色。
 *                    结果写到 exe 目录下的 sharedebug\（sharecapture_result.txt 与测试窗口的 wgc_pattern.bmp），
 *                    之后进程退出。用户屏幕与其它程序窗口的内容一律不落盘，只记录尺寸与「画面不是纯色」的核对结果。
 *    --shareencode   用 DXGI 采主显示器约 3 秒，按发帧节奏（15 fps 上限）编成 H.264 并当场解码，中途强制一次关键帧；
 *                    结果（编码尺寸、编码次数、关键帧数、平均编码耗时、解码帧数与尺寸）写到 sharedebug\shareencode_result.txt。
 *  后续步骤的调试入口（控制条、观看窗等）也加在这里。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include "../Api/FlamingoMediaApi.h"

namespace screenshare {

/**
 *  按命令行运行屏幕共享的调试入口。Release 构建恒返回 media_debug_not_handled。
 *  @param commandLine 进程命令行（UTF-16），为 NULL 时返回未处理
 *  @param hwndHost    宿主窗口（调试窗口的属主），可为 NULL
 *  @param host        宿主接口，可为 NULL
 *  @return 结果，取值见 flamingomedia::MediaDebugEntryResult
 */
int RunShareDebugEntry(const wchar_t* commandLine, HWND hwndHost, flamingomedia::IMediaHost* host);

} // namespace screenshare
