/**
 *  基于 Windows.Media.Ocr（Windows 10 起自带）的识别引擎后端，FlamingoOcr.dll 的第一个引擎。
 *
 *  流程（docs/图片文字识别方案.md 第 7 节）：
 *    - 初始化：运行时取 WinRT 入口（取不到即报告系统不支持）、列出可用的识别语言、按界面语言选默认语言、启动 GDI+；
 *    - 识别：复制像素并铺白底 → 最长边超过引擎上限时先缩小 → 第一遍识别 → 按行高中位数决定是否放大再识别一遍
 *      → 坐标换算回原图 → 拼行整理。
 *
 *  不跨线程缓存任何 WinRT 对象：初始化只保存语言列表等普通数据，每次识别重新取工厂、创建引擎。
 *  预加载线程退出后它所在的套间可能被拆除，缓存的接口指针未必还能使用。
 *  每个方法自己调用一次 CoInitializeEx（多线程套间），调用线程已初始化过 COM 时照常工作。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <string>
#include <vector>

#include "../OcrBackend.h"

namespace flamingoocr {

/**
 *  取进程内唯一的 Windows.Media.Ocr 后端，供 FlamingoOcr_GetApi 装进函数表、供单元测试直接使用。
 *  @return 后端，生命周期与进程相同
 */
IOcrBackend* GetWinOcrBackend();

/**
 *  测试用：关掉或打开第二遍（放大后再识别）。默认打开。用于比较「自动放大」与「只识别原图」的效果（OCRE3）。
 *  @param enabled false 表示只识别一遍
 */
void SetWinOcrSecondPassEnabledForTest(bool enabled);

} // namespace flamingoocr
