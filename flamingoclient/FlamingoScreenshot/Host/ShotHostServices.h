/**
 *  截图界面向宿主（Flamingo.exe）要的服务：「识别文字」要不要显示、发起识别。截图界面的代码迁进 DLL 之前直接调用
 *  exe 的 ocr::IsOcrEntryVisible / ocr::LaunchOcrForImage，迁进来以后改为经本文件转给 IScreenshotHost
 *  （docs/截图独立模块方案.md 3.2 节）。
 *
 *  宿主接口在 IScreenshotModule::Init 时登记、Shutdown 时清除；未登记时「识别文字」不显示，发起识别返回 false。
 *  只在宿主的界面线程上调用（截图界面运行在调用 Run 的线程上）。
 *
 *  典型用法：
 *      bool withOcr = flamingoshot::ShotOcrAvailable();          //建操作栏时
 *      flamingoshot::ShotLaunchOcr(owner, image);                  //截图界面关闭后
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include "../ScreenshotApi.h"
#include "../Capture/ShotBgra.h"

namespace flamingoshot {

/**
 *  登记或清除宿主接口。
 *  @param host 宿主接口（借用）；传 NULL 表示清除
 */
void ShotHostServicesSetHost(IScreenshotHost* host);

/**
 *  操作栏要不要显示「识别文字」。
 *  @return 宿主已登记且报告 OCR 可用时返回 true
 */
bool ShotOcrAvailable();

/**
 *  请宿主识别一张图像（打开识别结果窗口）。
 *  @param owner 发起截图的窗口，可为 NULL
 *  @param image 选区的原始像素
 *  @return 宿主已开始识别返回 true；宿主未登记、图像为空或宿主拒绝时返回 false
 */
bool ShotLaunchOcr(HWND owner, const ShotBgraImage& image);

} // namespace flamingoshot
