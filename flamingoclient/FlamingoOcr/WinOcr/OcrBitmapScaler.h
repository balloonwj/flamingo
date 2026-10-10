/**
 *  用 GDI+ 缩放 32 位 BGRA 图像（高质量双三次插值、像素中心对齐），供两遍识别放大小字、缩小超大图使用。
 *  第 0 期实测里，GDI+ 的高质量插值比 PIL 的双三次插值识别率略高（docs/图片文字识别方案.md 第 4.4 节）。
 *
 *  典型用法：
 *
 *      if (!EnsureOcrGdiplus())
 *          return ocr_engine_error;
 *      std::vector<uint8_t> up;
 *      ScaleBgra(&src[0], w, h, w * 4, w * 2, h * 2, up);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <vector>

namespace flamingoocr {

/**
 *  确保本模块的 GDI+ 已启动（进程内只启动一次，之后不关闭；多次 GdiplusStartup 是安全的）。
 *  不得在 DllMain 里调用。
 *  @return GDI+ 可用时返回 true
 */
bool EnsureOcrGdiplus();

/**
 *  把一张不透明的 32 位 BGRA 图像缩放到指定尺寸。缩放后的边缘与透明区域再铺一次白底。
 *  @param src       源像素，第一行第一个像素的地址，不可为 NULL
 *  @param width     源宽（像素），大于 0
 *  @param height    源高（像素），大于 0
 *  @param stride    源每行字节数，不小于 width × 4
 *  @param dstWidth  目标宽（像素），大于 0
 *  @param dstHeight 目标高（像素），大于 0
 *  @param dst       [出参] 目标像素，行跨度为 dstWidth × 4
 *  @return 成功返回 true；GDI+ 不可用或内存不足时返回 false
 */
bool ScaleBgra(const uint8_t* src, uint32_t width, uint32_t height, uint32_t stride,
               uint32_t dstWidth, uint32_t dstHeight, std::vector<uint8_t>& dst);

} // namespace flamingoocr
