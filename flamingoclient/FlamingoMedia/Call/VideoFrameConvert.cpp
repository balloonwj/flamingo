/**
 *  视频帧颜色空间转换实现，VideoFrameConvert.cpp
 *  balloonwj@qq.com   2026-06-15
 */
#include "VideoFrameConvert.h"

namespace call {

//把整数夹取到 [0,255]。
static inline uint8_t clamp8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return static_cast<uint8_t>(v);
}

void Nv12ToBgra(const uint8_t* nv12, int width, int height, int stride,
                uint8_t* bgra, int bgraStride)
{
    if (nv12 == NULL || bgra == NULL || width <= 0 || height <= 0)
        return;

    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(stride) * static_cast<size_t>(height);

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* yRow = yPlane + static_cast<size_t>(y) * static_cast<size_t>(stride);
        //UV 平面行号 = y/2（每 2 行 Y 共用一行 UV）
        const uint8_t* uvRow = uvPlane + static_cast<size_t>(y / 2) * static_cast<size_t>(stride);
        uint8_t* outRow = bgra + static_cast<size_t>(y) * static_cast<size_t>(bgraStride);

        for (int x = 0; x < width; ++x)
        {
            //每 2 个像素共用一组 U/V：列对 = (x/2)*2，U 在偶位、V 在奇位
            int uvCol = (x / 2) * 2;
            int yy = static_cast<int>(yRow[x]);
            int uu = static_cast<int>(uvRow[uvCol]);
            int vv = static_cast<int>(uvRow[uvCol + 1]);

            //BT.601 limited-range：先去偏置，再定点系数换算
            int c = yy - 16;
            int d = uu - 128;
            int e = vv - 128;
            int r = (298 * c + 409 * e + 128) >> 8;
            int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            int b = (298 * c + 516 * d + 128) >> 8;

            uint8_t* px = outRow + static_cast<size_t>(x) * 4;
            px[0] = clamp8(b);   //B
            px[1] = clamp8(g);   //G
            px[2] = clamp8(r);   //R
            px[3] = 255;         //A
        }
    }
}

} // namespace call
