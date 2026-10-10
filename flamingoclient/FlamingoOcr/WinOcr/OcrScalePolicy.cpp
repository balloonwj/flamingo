/**
 *  识别前的图像尺寸策略实现，说明见 OcrScalePolicy.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrScalePolicy.h"

#include <math.h>

namespace flamingoocr {

namespace {

//每像素字节数（32 位 BGRA）
const uint32_t kBytesPerPixel = 4;

//BGRA 中透明度所在的字节下标
const uint32_t kAlphaIndex = 3;

//一个通道的最大值
const uint32_t kChannelMax = 255;

//两数中的较大者
uint32_t MaxOf(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

} // namespace

uint32_t ChooseUpscaleFactor(float medianLineHeight, uint32_t width, uint32_t height, uint32_t maxDimension)
{
    uint32_t factor = 1;
    if (medianLineHeight <= 0.0f)
        factor = kRetryUpscaleFactorWhenEmpty;
    else if (medianLineHeight >= kNoUpscaleLineHeight)
        factor = 1;
    else if (medianLineHeight >= kDoubleUpscaleLineHeight)
        factor = 2;
    else
        factor = kMaxUpscaleFactor;

    //放大后最长边不得超过引擎上限，超过就降一档
    const uint64_t longest = MaxOf(width, height);
    while (factor > 1 && maxDimension > 0 && longest * factor > maxDimension)
        --factor;
    return factor;
}

float ComputeDownscale(uint32_t width, uint32_t height, uint32_t maxDimension)
{
    const uint32_t longest = MaxOf(width, height);
    if (maxDimension == 0 || longest <= maxDimension)
        return 1.0f;
    return static_cast<float>(maxDimension) / static_cast<float>(longest);
}

void ScaledSize(uint32_t width, uint32_t height, float scale, uint32_t& outWidth, uint32_t& outHeight)
{
    //乘积先按 double 算再向下取整，避免 float 精度在万级像素上丢掉一个像素
    double w = floor(static_cast<double>(width) * scale);
    double h = floor(static_cast<double>(height) * scale);
    outWidth = (w < 1.0) ? 1u : static_cast<uint32_t>(w);
    outHeight = (h < 1.0) ? 1u : static_cast<uint32_t>(h);
}

OcrRect MapRectToSource(const OcrRect& rect, float scale)
{
    if (scale <= 0.0f)
        return rect;
    OcrRect out;
    out.m_x = rect.m_x / scale;
    out.m_y = rect.m_y / scale;
    out.m_w = rect.m_w / scale;
    out.m_h = rect.m_h / scale;
    return out;
}

void FlattenOnWhite(uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride)
{
    if (pixels == NULL)
        return;
    for (uint32_t y = 0; y < height; ++y)
    {
        uint8_t* row = pixels + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < width; ++x)
        {
            uint8_t* px = row + static_cast<size_t>(x) * kBytesPerPixel;
            const uint32_t a = px[kAlphaIndex];
            if (a == kChannelMax)
                continue;
            for (uint32_t c = 0; c < kAlphaIndex; ++c)
            {
                //加 kChannelMax / 2 实现四舍五入
                const uint32_t v = (px[c] * a + kChannelMax * (kChannelMax - a) + kChannelMax / 2) / kChannelMax;
                px[c] = static_cast<uint8_t>(v);
            }
            px[kAlphaIndex] = static_cast<uint8_t>(kChannelMax);
        }
    }
}

} // namespace flamingoocr
