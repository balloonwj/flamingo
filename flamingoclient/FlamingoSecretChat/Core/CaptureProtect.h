/**
 *  密聊窗口的防截图（docs/密聊方案.md 4.6），CaptureProtect.h
 *
 *  对聊天窗调用 SetWindowDisplayAffinity：Windows 10 2004（build 19041）起用 WDA_EXCLUDEFROMCAPTURE，截图、录屏、
 *  屏幕共享里这个窗口整个不出现；更早的系统（或前者失败时）用 WDA_MONITOR，截图里显示为一块黑；两者都失败（典型为
 *  Win7 关闭了桌面合成）时级别为「无」，仍允许密聊，由界面提示双方（用户 2026-10-06 确认）。
 *
 *  系统版本（RtlGetVersion）与桌面合成状态（DwmIsCompositionEnabled）一律在运行时动态取，不静态导入，
 *  保证 DLL 在 Win7 上能载入。级别判定写成纯函数 DecideProtectLevel，便于单元测试。
 *
 *  典型用法：
 *      int level = ApplyCaptureProtect(hwnd);       // 进入密聊
 *      if (!IsCaptureProtectIntact(hwnd, level)) ApplyCaptureProtect(hwnd);   // 定时核对
 *      RemoveCaptureProtect(hwnd);                  // 退出密聊
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace secretchat {

//WDA_EXCLUDEFROMCAPTURE 的取值；较老的 SDK 头文件没有这个宏，按文档值补上
const DWORD kWdaExcludeFromCapture = 0x00000011;

//WDA_MONITOR 的取值
const DWORD kWdaMonitor = 0x00000001;

//WDA_EXCLUDEFROMCAPTURE 从这个系统内部版本号起可用（Windows 10 2004）
const uint32_t kExcludeFromCaptureMinBuild = 19041;

/**
 *  按系统条件判定能达到的防截图级别（纯函数）。
 *  @param dwmEnabled 桌面合成是否开着
 *  @param osBuild    系统内部版本号
 *  @return 取值见 secretchat::CaptureProtectLevel
 */
int DecideProtectLevel(bool dwmEnabled, uint32_t osBuild);

/**
 *  按 GetWindowDisplayAffinity 读到的值换算成级别（纯函数）。
 */
int AffinityToLevel(DWORD affinity);

/**
 *  查询本机能达到的防截图级别（不改动任何窗口）。
 */
int ProbeCaptureProtectLevel();

/**
 *  对一个顶层窗口打开防截图，先试 WDA_EXCLUDEFROMCAPTURE（系统支持时），失败再试 WDA_MONITOR。
 *  @return 实际达到的级别
 */
int ApplyCaptureProtect(HWND hwnd);

/**
 *  核对窗口当前的防截图设置是否仍不低于 level（被别的代码改掉时返回 false）。
 */
bool IsCaptureProtectIntact(HWND hwnd, int level);

/**
 *  关闭窗口的防截图（恢复 WDA_NONE）。
 */
void RemoveCaptureProtect(HWND hwnd);

} // namespace secretchat
