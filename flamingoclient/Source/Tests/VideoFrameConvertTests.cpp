/**
 *  视频帧颜色转换单元测试（Suite VFC），VideoFrameConvertTests.cpp
 *  覆盖 Nv12ToBgra：
 *    - 纯白(Y=235,U=V=128) → BGRA 接近 (255,255,255,255)；
 *    - 纯黑(Y=16,U=V=128)  → BGRA 接近 (0,0,0,255)；
 *    - 高 V(偏红) → R 明显大于 G、B；A 恒为 255；
 *    - 各像素 alpha=255、按行跨度正确写入。
 *  框架 Catch2 v3。纯逻辑，不触及设备/GDI。
 *  balloonwj@qq.com   2026-06-15
 */
#include "catch_amalgamated.hpp"
#include "../Call/VideoFrameConvert.h"

#include <vector>
#include <stdint.h>

using namespace call;

namespace {

//构造 w×h 的纯色 NV12（stride=w），全图同一 (Y,U,V)。
static std::vector<uint8_t> makeSolidNv12(int w, int h, uint8_t Y, uint8_t U, uint8_t V)
{
    int stride = w;
    std::vector<uint8_t> buf(static_cast<size_t>(stride) * h + static_cast<size_t>(stride) * (h / 2), 0);
    for (int i = 0; i < stride * h; ++i)
        buf[static_cast<size_t>(i)] = Y;
    uint8_t* uv = buf.data() + static_cast<size_t>(stride) * h;
    for (int r = 0; r < h / 2; ++r)
    {
        for (int c = 0; c < w; c += 2)
        {
            uv[static_cast<size_t>(r) * stride + c] = U;
            uv[static_cast<size_t>(r) * stride + c + 1] = V;
        }
    }
    return buf;
}

} // namespace

TEST_CASE("VFC1 纯白 NV12 → BGRA 近白")
{
    const int w = 4;
    const int h = 4;
    std::vector<uint8_t> nv12 = makeSolidNv12(w, h, 235, 128, 128);
    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4, 0);
    Nv12ToBgra(nv12.data(), w, h, w, bgra.data(), w * 4);

    for (int i = 0; i < w * h; ++i)
    {
        const uint8_t* px = &bgra[static_cast<size_t>(i) * 4];
        CHECK(px[0] >= 250);   //B
        CHECK(px[1] >= 250);   //G
        CHECK(px[2] >= 250);   //R
        CHECK(px[3] == 255);   //A
    }
}

TEST_CASE("VFC2 纯黑 NV12 → BGRA 近黑")
{
    const int w = 4;
    const int h = 4;
    std::vector<uint8_t> nv12 = makeSolidNv12(w, h, 16, 128, 128);
    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4, 0xAB);
    Nv12ToBgra(nv12.data(), w, h, w, bgra.data(), w * 4);

    for (int i = 0; i < w * h; ++i)
    {
        const uint8_t* px = &bgra[static_cast<size_t>(i) * 4];
        CHECK(px[0] <= 5);
        CHECK(px[1] <= 5);
        CHECK(px[2] <= 5);
        CHECK(px[3] == 255);
    }
}

TEST_CASE("VFC3 高 V(偏红) → R 明显大于 G/B")
{
    const int w = 4;
    const int h = 4;
    std::vector<uint8_t> nv12 = makeSolidNv12(w, h, 128, 128, 255);
    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4, 0);
    Nv12ToBgra(nv12.data(), w, h, w, bgra.data(), w * 4);

    const uint8_t* px = &bgra[0];
    CHECK(px[2] > px[1]);   //R > G
    CHECK(px[2] > px[0]);   //R > B
    CHECK(px[3] == 255);
}

TEST_CASE("VFC4 alpha 恒为 255、逐像素写满")
{
    const int w = 8;
    const int h = 6;
    std::vector<uint8_t> nv12 = makeSolidNv12(w, h, 128, 100, 150);
    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4, 0);
    Nv12ToBgra(nv12.data(), w, h, w, bgra.data(), w * 4);

    for (int i = 0; i < w * h; ++i)
        CHECK(bgra[static_cast<size_t>(i) * 4 + 3] == 255);
}
