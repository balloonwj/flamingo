/**
 *  OcrEntry.h
 *
 *  「图片文字识别」发起识别的真正实现：先按识别模块的状态做前置判断，系统不支持或没有识别语言时弹提示；
 *  可以识别时打开结果窗口并开始识别（docs/图片文字识别方案.md 第 5.2、8.5 节）。
 *
 *  三个入口不直接调用这里，而是调用 Feature/Ocr/OcrLauncher 的 ocr::LaunchOcrForFile / LaunchOcrForImage
 *  （那些入口文件有的也编进了单元测试工程，而结果窗口只在 exe 里）。exe 在登录成功、开始预加载识别模块时
 *  调用 RegisterOcrLaunchers，把本文件的实现注册给 OcrLauncher。
 *
 *  只在界面线程调用。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>

namespace balloonocr {

/**
 *  把发起识别的实现注册给 ocr::SetOcrLaunchers。重复调用是安全的。
 */
void RegisterOcrLaunchers();

/**
 *  主面板系统菜单「图片文字识别」：前置判断（系统不支持、没有识别语言时弹提示）通过后，打开还没有图片的
 *  识别窗口，由用户点「打开图片」选图（docs/图片文字识别方案.md 第 14 节）。只在界面线程调用。
 *  @param owner 主面板窗口：提示框的所有者，新建的识别窗口居中摆在它上面
 *  @return 已打开识别窗口时返回 true；弹了提示或打开失败时返回 false
 */
bool LaunchOcrWindow(HWND owner);

} // namespace balloonocr
