/**
 *  「图片文字识别」入口的发起调度（纯逻辑，配单元测试 OCRH9）。
 *
 *  三个入口（聊天图片右键菜单、看图窗工具栏、截图工具栏）所在的文件有些也编进了 FlamingoUnit 测试工程，
 *  而真正发起识别要打开结果窗口（UI/BalloonOcr，只在 exe 里）。为此入口只调用本模块的 LaunchOcrForFile /
 *  LaunchOcrForImage，由 exe 在启动时经 SetOcrLaunchers 注册真正的实现（balloonocr::RegisterOcrLaunchers）；
 *  没有注册时调用什么也不做、返回 false。单元测试可以注册假的实现，检查入口传来的路径与旋转方向。
 *
 *  典型用法（入口）：
 *
 *      if (ocr::IsOcrEntryVisible())
 *          ...显示「图片文字识别」按钮...
 *      ...点击时...
 *      ocr::LaunchOcrForFile(owner, imagePath, rotationDegrees / 90);
 *
 *  所有函数只在界面线程调用。本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include <string>

#include "OcrRecognizer.h"   // OcrBgraImage

namespace ocr {

/**
 *  识别一个图片文件的实现。
 *  @param owner        发起识别的窗口
 *  @param path         图片文件的完整路径
 *  @param quarterTurns 顺时针旋转的 90° 次数，取值 0 ~ 3
 *  @return 已开始识别时返回 true
 */
typedef bool (*OcrFileLauncherFn)(HWND owner, const std::wstring& path, int quarterTurns);

/**
 *  识别内存图像的实现。
 *  @param owner 发起识别的窗口
 *  @param image 图像
 *  @return 已开始识别时返回 true
 */
typedef bool (*OcrImageLauncherFn)(HWND owner, const OcrBgraImage& image);

/**
 *  注册发起识别的实现；传 NULL 表示撤销注册。
 */
void SetOcrLaunchers(OcrFileLauncherFn fileLauncher, OcrImageLauncherFn imageLauncher);

/**
 *  识别入口要不要显示：识别模块已加载可用（COcrModuleHost::CanShowEntry）且已注册了发起实现时为 true。
 */
bool IsOcrEntryVisible();

/**
 *  从入口识别一个图片文件；没有注册实现或路径为空时返回 false。参数含义同 OcrFileLauncherFn。
 */
bool LaunchOcrForFile(HWND owner, const std::wstring& path, int quarterTurns);

/**
 *  从入口识别内存图像；没有注册实现或图像为空时返回 false。参数含义同 OcrImageLauncherFn。
 */
bool LaunchOcrForImage(HWND owner, const OcrBgraImage& image);

/**
 *  把顺时针旋转角度（度）换算成 90° 的次数，归一到 0 ~ 3；不是 90 的整数倍时向下取整。
 *  @param degreesClockwise 顺时针旋转角度，可为负
 *  @return 0 ~ 3
 */
int QuarterTurnsOf(int degreesClockwise);

} // namespace ocr
