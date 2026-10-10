/**
 *  标注另存为图片（第 23 期 23d，docs/屏幕共享与录制方案.md 第 2 节第 40 条），ShareImageExport.h
 *
 *  标注工具条的「保存为图片」与关闭有内容的白板前的保存都用这里的函数：
 *    - RenderBoardImage：把一块白板画成图片：白底，按背景画网格或横线（按世界缩放，与屏幕上看到的一致），再画上笔迹；
 *    - RenderScreenImage：把一帧共享画面与它上面的笔迹合成一张图片；
 *    - SaveBgraAsPng：存成 PNG；
 *    - MakeAnnotExportFileName：默认文件名（如「白板_20261005_153012.png」，前缀随界面语言）。
 *  单元测试见 Source/Tests/ShareBoardTests.cpp（SSWV4、SSWV5）。
 *
 *  典型用法（屏幕共享控制器里）：
 *      std::vector<uint8_t> bgra;
 *      RenderBoardImage(kBoardExportWidth, kBoardExportHeight, background, AnnotStrokesOnCanvas(all, seat), bgra);
 *      SaveBgraAsPng(path, bgra, kBoardExportWidth, kBoardExportHeight);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <windows.h>

#include "ShareAnnotation.h"

namespace screenshare {

//白板导出成图片的尺寸（像素）：整个世界（16:9）按 3840×2160 导出
const int kBoardExportWidth = 3840;
const int kBoardExportHeight = 2160;

/**
 *  把一块白板画成图片。
 *  @param width、height 图片尺寸（像素），对应整个世界
 *  @param background    背景，取值见 WhiteboardBackground（ShareWhiteboard.h）
 *  @param strokes       这块白板上的笔迹（调用方已按画面筛过）
 *  @param bgra          [出参] 自上而下的 BGRA，不透明，大小为 width × height × 4；尺寸不合法时为空
 */
void RenderBoardImage(int width, int height, int background, const std::vector<AnnotationStroke>& strokes,
                      std::vector<uint8_t>& bgra);

/**
 *  把一帧共享画面与它上面的笔迹合成一张图片。
 *  @param frame         自上而下的 BGRA，大小为 width × height × 4
 *  @param width、height 画面尺寸（像素）
 *  @param strokes       这块画面上的笔迹（调用方已按画面筛过），按画面的大小换算
 *  @param out           [出参] 合成结果，不透明；画面为空或尺寸不符时为空
 */
void RenderScreenImage(const std::vector<uint8_t>& frame, int width, int height,
                       const std::vector<AnnotationStroke>& strokes, std::vector<uint8_t>& out);

/**
 *  把 BGRA 存成 PNG（按不透明处理）。
 *  @param path          文件路径；已存在时覆盖
 *  @param bgra          自上而下的 BGRA，大小为 width × height × 4
 *  @param width、height 尺寸（像素）
 *  @return 写成功返回 true
 */
bool SaveBgraAsPng(const std::wstring& path, const std::vector<uint8_t>& bgra, int width, int height);

/**
 *  导出图片的默认文件名（不含目录）。
 *  @param whiteboard 是否白板：白板前缀为「白板」，共享画面为「屏幕标注」（随界面语言翻译）
 *  @param t          时刻，写成「年月日_时分秒」
 */
std::wstring MakeAnnotExportFileName(bool whiteboard, const SYSTEMTIME& t);

} // namespace screenshare
