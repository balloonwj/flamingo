/**
 *  识别前的图像尺寸策略（纯函数，不依赖 WinRT 与 GDI+，配单元测试 OCRP1 ~ OCRP4）：
 *    - 两遍识别的放大倍数：先识别原图，按识别出的行高中位数决定要不要放大再识别一遍；
 *    - 超大图的缩小比例：最长边超过引擎上限时等比缩小；
 *    - 坐标换算：缩放后的图上识别出的矩形换算回原图坐标；
 *    - 透明背景铺白：把不预乘的 BGRA 像素铺在白底上，避免透明 PNG 上的深色字变成黑底黑字。
 *
 *  阈值来自第 0 期实测（docs/图片文字识别方案.md 第 4.2 节）：九张样本图上，按这些阈值选出的倍数都是准确率最高的。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>

#include "../FlamingoOcrApi.h"

namespace flamingoocr {

//原图行高中位数不低于该值（像素）时不放大
const float kNoUpscaleLineHeight = 20.0f;

//原图行高中位数不低于该值、但低于 kNoUpscaleLineHeight 时放大 2 倍；低于该值时放大 3 倍
const float kDoubleUpscaleLineHeight = 12.0f;

//放大倍数的上限
const uint32_t kMaxUpscaleFactor = 3;

//原图一行都没识别出来时，放大这个倍数再试一次（小字图在原尺寸下可能什么都读不到）
const uint32_t kRetryUpscaleFactorWhenEmpty = 2;

/**
 *  决定第二遍识别的放大倍数。
 *  @param medianLineHeight 第一遍识别出的行高中位数（像素）；第一遍没有识别出任何行时传 0
 *  @param width            第一遍识别所用图像的宽（像素）
 *  @param height           第一遍识别所用图像的高（像素）
 *  @param maxDimension     引擎允许的最长边（像素），即 OcrEngine::MaxImageDimension
 *  @return 放大倍数，1 表示不需要第二遍；放大后最长边超过 maxDimension 时逐档降低
 */
uint32_t ChooseUpscaleFactor(float medianLineHeight, uint32_t width, uint32_t height, uint32_t maxDimension);

/**
 *  超大图的缩小比例：最长边超过 maxDimension 时等比缩小到恰好不超过它。
 *  @param width        原图宽（像素）
 *  @param height       原图高（像素）
 *  @param maxDimension 允许的最长边（像素），为 0 时不缩小
 *  @return 缩放比例，取值 (0, 1]；不需要缩小时为 1
 */
float ComputeDownscale(uint32_t width, uint32_t height, uint32_t maxDimension);

/**
 *  按比例计算缩放后的尺寸，向下取整，至少为 1 像素。
 *  @param width     原宽（像素）
 *  @param height    原高（像素）
 *  @param scale     缩放比例，大于 0
 *  @param outWidth  [出参] 缩放后的宽
 *  @param outHeight [出参] 缩放后的高
 */
void ScaledSize(uint32_t width, uint32_t height, float scale, uint32_t& outWidth, uint32_t& outHeight);

/**
 *  把缩放后的图上识别出的矩形换算回原图坐标。
 *  @param rect  缩放后图上的矩形
 *  @param scale 缩放比例（缩放后尺寸 ÷ 原图尺寸），大于 0；不大于 0 时原样返回
 *  @return 原图坐标下的矩形
 */
OcrRect MapRectToSource(const OcrRect& rect, float scale);

/**
 *  把不预乘的 BGRA 像素就地铺在白底上：每个通道 c 变成 (c × a + 255 × (255 − a)) ÷ 255（四舍五入），
 *  透明度一律置为 255。
 *  @param pixels 第一行第一个像素的地址，不可为 NULL
 *  @param width  宽（像素）
 *  @param height 高（像素）
 *  @param stride 每行字节数，不小于 width × 4
 */
void FlattenOnWhite(uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride);

} // namespace flamingoocr
