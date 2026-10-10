/**
 *  系统版本策略：判断当前 Windows 版本能否使用屏幕采集类功能（屏幕共享、屏幕录制）。
 *
 *  这两项功能依赖 Windows 10 2004（build 19041）起才提供的系统能力：排除自身窗口的采集
 *  属性 WDA_EXCLUDEFROMCAPTURE、Windows.Graphics.Capture 的指针采集开关、排除指定进程的
 *  声音回环采集。低于该版本时界面给出不支持的提示（见 docs/屏幕共享与录制方案.md 第 2 节第 16 条）。
 *
 *  判断逻辑是纯函数，便于单元测试（Tests/MediaModuleLoaderTests.cpp）；取系统版本号的函数
 *  单独提供，供运行时调用。
 *
 *  典型用法：
 *      uint32_t major = 0, minor = 0, build = 0;
 *      if (GetOsVersion(major, minor, build) && IsScreenCaptureSupportedBuild(major, minor, build))
 *          ...
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>

namespace flamingomedia {

//屏幕采集类功能要求的最低系统版本：Windows 10（版本号 10.0）的 2004 版，构建号 19041。
//Windows 11 的版本号同样报告为 10.0，构建号从 22000 起，因而也满足该条件。
const uint32_t kScreenCaptureMinMajor = 10;
const uint32_t kScreenCaptureMinMinor = 0;
const uint32_t kScreenCaptureMinBuild = 19041;

/**
 *  判断给定的系统版本能否使用屏幕采集类功能。
 *  @param major 主版本号（Windows 10 / 11 为 10，Windows 7 为 6）
 *  @param minor 次版本号（Windows 10 / 11 为 0，Windows 7 为 1）
 *  @param build 构建号（如 19045）
 *  @return 不低于 10.0.19041 返回 true
 */
bool IsScreenCaptureSupportedBuild(uint32_t major, uint32_t minor, uint32_t build);

/**
 *  取当前系统的真实版本号。使用 ntdll 的 RtlGetVersion，不受应用程序清单兼容性设置的影响
 *  （GetVersionEx 在没有声明 Windows 10 兼容性的进程里会报告 6.2）。
 *  @param major [出参] 主版本号
 *  @param minor [出参] 次版本号
 *  @param build [出参] 构建号
 *  @return 取到返回 true；取不到时三个出参均置 0 并返回 false
 */
bool GetOsVersion(uint32_t& major, uint32_t& minor, uint32_t& build);

} // namespace flamingomedia
