/**
 *  截图模块里的 32 位 BGRA 图像与「位图取区域转 BGRA」。「识别文字」要把冻结位图里选区的原始像素交给宿主；
 *  截图代码迁进 DLL 之前用的是 exe 的 ocr::OcrBgraImage / ocr::BitmapToBgra（Source/Feature/Ocr/OcrRecognizer），
 *  DLL 里用不到 exe 的那份实现，这里按同样的做法写一份（docs/截图独立模块方案.md 第 5 节 S6）。
 *
 *  典型用法：
 *      ShotBgraImage image;
 *      if (ShotBitmapToBgra(frozen, &selection, image)) ShotLaunchOcr(owner, image);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <vector>

namespace flamingoshot {

/**
 *  一张紧凑排列的 32 位 BGRA 图像（行跨度为宽 × 4，自上而下）。
 */
struct ShotBgraImage
{
    std::vector<uint8_t> m_pixels;   // 像素，共 m_width * m_height * 4 字节
    uint32_t             m_width;    // 宽（像素）
    uint32_t             m_height;   // 高（像素）

    ShotBgraImage();
};

/**
 *  把位图（DDB 或 DIB 段）的指定区域转成 BGRA，透明度一律置为 255。与 ocr::BitmapToBgra 的结果相同。
 *  @param bitmap 位图句柄，调用方保留所有权
 *  @param crop   要取的区域（位图坐标）；为 NULL 时取整张位图；超出位图的部分被裁掉
 *  @param out    [出参] 转换后的图像；失败时为空图像
 *  @return 成功返回 true；位图无效或区域为空时返回 false
 */
bool ShotBitmapToBgra(HBITMAP bitmap, const RECT* crop, ShotBgraImage& out);

} // namespace flamingoshot
