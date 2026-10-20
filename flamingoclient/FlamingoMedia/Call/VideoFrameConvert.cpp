/**
 *  视频帧颜色空间转换实现，VideoFrameConvert.cpp
 *  2026-10-15 起 NV12 → BGRA 另有 SSE2 实现（docs/远程桌面H264与共享画质提升方案.md 4.2「接收端」），与逐像素实现逐字节一致。
 *  balloonwj@qq.com   2026-06-15
 */
#include "VideoFrameConvert.h"

#include <stddef.h>
#include <emmintrin.h>   //SSE2：一次转换 8 个像素

namespace call {

namespace {

//BT.601 limited range：亮度偏置、色度偏置，以及 R / G / B 的定点系数（右移 8 位）
const int kYBias = 16;
const int kUvBias = 128;
const int kCoefY = 298;
const int kCoefRv = 409;
const int kCoefGu = -100;
const int kCoefGv = -208;
const int kCoefBu = 516;
const int kCoefRound = 128;
const int kCoefShift = 8;
//每像素字节数与不透明的 Alpha
const int kBgraBytes = 4;
const int kOpaqueAlpha = 255;
//SSE2 一次处理的像素数：8 个亮度样本（一个 64 位）与它们共用的 4 组 U / V
const int kSimdPixels = 8;

//把整数夹取到 [0,255]。
inline uint8_t clamp8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return static_cast<uint8_t>(v);
}

//转换一个像素（逐像素实现与 SSE2 实现的行尾共用）
inline void convertPixel(int yy, int uu, int vv, uint8_t* px)
{
    //BT.601 limited-range：先去偏置，再定点系数换算
    const int c = yy - kYBias;
    const int d = uu - kUvBias;
    const int e = vv - kUvBias;
    const int r = (kCoefY * c + kCoefRv * e + kCoefRound) >> kCoefShift;
    const int g = (kCoefY * c + kCoefGu * d + kCoefGv * e + kCoefRound) >> kCoefShift;
    const int b = (kCoefY * c + kCoefBu * d + kCoefRound) >> kCoefShift;
    px[0] = clamp8(b);              //B
    px[1] = clamp8(g);              //G
    px[2] = clamp8(r);              //R
    px[3] = kOpaqueAlpha;           //A
}

//转换一行中从 x 起到行尾的像素（逐像素）
inline void convertRowTail(const uint8_t* yRow, const uint8_t* uvRow, uint8_t* outRow, int x, int width)
{
    for (; x < width; ++x)
    {
        //每 2 个像素共用一组 U/V：列对 = (x/2)*2，U 在偶位、V 在奇位
        const int uvCol = (x / 2) * 2;
        convertPixel(yRow[x], uvRow[uvCol], uvRow[uvCol + 1], outRow + static_cast<size_t>(x) * kBgraBytes);
    }
}

//两个 16 位系数拼成 _mm_madd_epi16 的一对：低 16 位乘每对的第一个数，高 16 位乘第二个数
inline __m128i coefPair(int first, int second)
{
    const uint32_t lo = static_cast<uint32_t>(first) & 0xFFFFu;
    const uint32_t hi = static_cast<uint32_t>(second) & 0xFFFFu;
    return _mm_set1_epi32(static_cast<int>((hi << 16) | lo));
}

//逐像素实现：转换左上角 width × height；UV 平面位于 nv12 + stride × planeHeight
void convertReference(const uint8_t* nv12, int planeHeight, int stride, int width, int height,
                      uint8_t* bgra, int bgraStride)
{
    if (nv12 == NULL || bgra == NULL || width <= 0 || height <= 0)
        return;

    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(stride) * static_cast<size_t>(planeHeight);

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* yRow = yPlane + static_cast<size_t>(y) * static_cast<size_t>(stride);
        //UV 平面行号 = y/2（每 2 行 Y 共用一行 UV）
        const uint8_t* uvRow = uvPlane + static_cast<size_t>(y / 2) * static_cast<size_t>(stride);
        uint8_t* outRow = bgra + static_cast<size_t>(y) * static_cast<size_t>(bgraStride);
        convertRowTail(yRow, uvRow, outRow, 0, width);
    }
}

//SSE2 实现：参数同 convertReference
void convertSse2(const uint8_t* nv12, int planeHeight, int stride, int width, int height,
                 uint8_t* bgra, int bgraStride)
{
    if (nv12 == NULL || bgra == NULL || width <= 0 || height <= 0)
        return;

    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(stride) * static_cast<size_t>(planeHeight);
    const __m128i zero = _mm_setzero_si128();
    const __m128i one16 = _mm_set1_epi16(1);
    const __m128i yBias = _mm_set1_epi16(static_cast<short>(kYBias));
    const __m128i uvBias = _mm_set1_epi16(static_cast<short>(kUvBias));
    const __m128i alpha = _mm_set1_epi8(static_cast<char>(kOpaqueAlpha));
    //(c, 1) × (298, 128)：每个像素的 298·c + 128；(d, e) × 各通道的色度系数
    const __m128i coefBase = coefPair(kCoefY, kCoefRound);
    const __m128i coefR = coefPair(0, kCoefRv);
    const __m128i coefG = coefPair(kCoefGu, kCoefGv);
    const __m128i coefB = coefPair(kCoefBu, 0);

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* yRow = yPlane + static_cast<size_t>(y) * static_cast<size_t>(stride);
        const uint8_t* uvRow = uvPlane + static_cast<size_t>(y / 2) * static_cast<size_t>(stride);
        uint8_t* outRow = bgra + static_cast<size_t>(y) * static_cast<size_t>(bgraStride);
        int x = 0;
        for (; x + kSimdPixels <= width; x += kSimdPixels)
        {
            //8 个亮度去偏置：c0..c7（16 位）
            const __m128i c = _mm_sub_epi16(
                _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(yRow + x)), zero), yBias);
            //4 组 U / V 去偏置：(d0,e0)(d1,e1)(d2,e2)(d3,e3)，每组是一个 32 位，供相邻两个像素共用
            const __m128i de = _mm_sub_epi16(
                _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(uvRow + x)), zero), uvBias);
            //前 4 个像素与后 4 个像素各自的 (c, 1) 与 (d, e)
            const __m128i c1Lo = _mm_unpacklo_epi16(c, one16);
            const __m128i c1Hi = _mm_unpackhi_epi16(c, one16);
            const __m128i deLo = _mm_unpacklo_epi32(de, de);
            const __m128i deHi = _mm_unpackhi_epi32(de, de);
            const __m128i baseLo = _mm_madd_epi16(c1Lo, coefBase);
            const __m128i baseHi = _mm_madd_epi16(c1Hi, coefBase);
            //三个通道：base + 色度项，右移 8 位（算术右移，与逐像素实现的 >> 相同）
            const __m128i rLo = _mm_srai_epi32(_mm_add_epi32(baseLo, _mm_madd_epi16(deLo, coefR)), kCoefShift);
            const __m128i rHi = _mm_srai_epi32(_mm_add_epi32(baseHi, _mm_madd_epi16(deHi, coefR)), kCoefShift);
            const __m128i gLo = _mm_srai_epi32(_mm_add_epi32(baseLo, _mm_madd_epi16(deLo, coefG)), kCoefShift);
            const __m128i gHi = _mm_srai_epi32(_mm_add_epi32(baseHi, _mm_madd_epi16(deHi, coefG)), kCoefShift);
            const __m128i bLo = _mm_srai_epi32(_mm_add_epi32(baseLo, _mm_madd_epi16(deLo, coefB)), kCoefShift);
            const __m128i bHi = _mm_srai_epi32(_mm_add_epi32(baseHi, _mm_madd_epi16(deHi, coefB)), kCoefShift);
            //收窄成字节：结果在 16 位范围之内，packus 限幅到 0~255，与 clamp8 相同
            const __m128i r16 = _mm_packs_epi32(rLo, rHi);
            const __m128i g16 = _mm_packs_epi32(gLo, gHi);
            const __m128i b16 = _mm_packs_epi32(bLo, bHi);
            const __m128i r8 = _mm_packus_epi16(r16, r16);
            const __m128i g8 = _mm_packus_epi16(g16, g16);
            const __m128i b8 = _mm_packus_epi16(b16, b16);
            //交错成 B G R A：先两两交错成 BG、RA，再按 16 位交错成整像素
            const __m128i bg = _mm_unpacklo_epi8(b8, g8);
            const __m128i ra = _mm_unpacklo_epi8(r8, alpha);
            const __m128i px0 = _mm_unpacklo_epi16(bg, ra);
            const __m128i px1 = _mm_unpackhi_epi16(bg, ra);
            uint8_t* out = outRow + static_cast<size_t>(x) * kBgraBytes;
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out), px0);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + kSimdPixels / 2 * kBgraBytes), px1);
        }
        convertRowTail(yRow, uvRow, outRow, x, width);
    }
}

} // namespace

void Nv12ToBgraReference(const uint8_t* nv12, int width, int height, int stride,
                         uint8_t* bgra, int bgraStride)
{
    convertReference(nv12, height, stride, width, height, bgra, bgraStride);
}

void Nv12ToBgraSse2(const uint8_t* nv12, int width, int height, int stride,
                    uint8_t* bgra, int bgraStride)
{
    convertSse2(nv12, height, stride, width, height, bgra, bgraStride);
}

void Nv12ToBgra(const uint8_t* nv12, int width, int height, int stride,
                uint8_t* bgra, int bgraStride)
{
    //Debug 版不优化向量指令，SSE2 实现反而更慢，所以只在 Release 版使用（与 screenshare::ConvertBgraToNv12 相同）
#ifdef _DEBUG
    convertReference(nv12, height, stride, width, height, bgra, bgraStride);
#else
    convertSse2(nv12, height, stride, width, height, bgra, bgraStride);
#endif
}

void Nv12ToBgraCropped(const uint8_t* nv12, int planeHeight, int stride, int width, int height,
                       uint8_t* bgra, int bgraStride)
{
    if (height > planeHeight)
        height = planeHeight;
#ifdef _DEBUG
    convertReference(nv12, planeHeight, stride, width, height, bgra, bgraStride);
#else
    convertSse2(nv12, planeHeight, stride, width, height, bgra, bgraStride);
#endif
}

} // namespace call
