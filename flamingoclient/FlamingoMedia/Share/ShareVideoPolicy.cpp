/**
 *  屏幕共享画面编码的纯逻辑，ShareVideoPolicy.cpp
 *  规则说明见 ShareVideoPolicy.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareVideoPolicy.h"

#include "ShareBitrateController.h"   // kShareMinBitrateBps：屏幕画面码率的下限
#include <string.h>
#include <emmintrin.h>   //SSE2：颜色转换一次处理 4 列 × 2 行
#include <vector>

namespace screenshare {

namespace {

//一秒的毫秒数
const int64_t kMsPerSecond = 1000;
//编码尺寸的下限，单位像素（H.264 4:2:0 至少 2×2）
const int kMinEncodeSide = 2;
//每像素字节数
const int kBytesPerPixel = 4;
//定点数的小数位数与 1.0 对应的值（双线性插值的权重用 16.16 定点）
const int kFixedShift = 16;
const int kFixedOne = 1 << kFixedShift;
//BT.601 limited range 的定点系数（与 call::Nv12ToBgra 的逆变换配套，右移 8 位）
const int kYr = 66;
const int kYg = 129;
const int kYb = 25;
const int kUr = -38;
const int kUg = -74;
const int kUb = 112;
const int kVr = 112;
const int kVg = -94;
const int kVb = -18;
const int kCoefShift = 8;
const int kRound = 128;
const int kYOffset = 16;
const int kUvOffset = 128;
//向量化转换一次处理的列数（4 个 BGRA 像素正好 16 字节，一个 SSE2 寄存器）
const int kSimdCols = 4;
//2×2 块取平均时的舍入值与右移位数（四个像素求和后 +2 再除以 4）
const int kAvgRound = 2;
const int kAvgShift = 2;

int roundDownEven(int v)
{
    v &= ~1;
    return v < kMinEncodeSide ? kMinEncodeSide : v;
}

uint8_t clampByte(int v)
{
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

//缩放权重的位数与 1.0 对应的值（7 位定点，0~128）：纵向混合后的中间值不超过 255 × 128，放得进有符号 16 位整数
const int kScaleWeightBits = 7;
const int kScaleWeightOne = 1 << kScaleWeightBits;
//16.16 定点的小数部分换成 7 位权重时右移的位数与舍入值
const int kScaleWeightShift = kFixedShift - kScaleWeightBits;
const int kScaleWeightRound = 1 << (kScaleWeightShift - 1);
//纵横两次加权之后右移的位数与舍入值（两次权重各 7 位）
const int kScaleFinalShift = 2 * kScaleWeightBits;
const int kScaleFinalRound = 1 << (kScaleFinalShift - 1);
//SSE2 纵向混合一次处理的字节数（一个寄存器）与横向混合一次处理的像素数（两个像素的结果收窄成 8 个字节）
const int kScaleSimdBytes = 16;
const int kScaleSimdPixels = 2;

/**
 *  目标坐标 d 对应的源坐标（像素中心对齐：源坐标 = (目标坐标 + 0.5) × 源长 / 目标长 - 0.5，先按 16.16 定点算）。
 *  @param i0 / i1 [出参] 取样的两个源坐标；在源的最后一个位置时 i1 与 i0 相同
 *  @param w       [出参] i1 的权重（0~128），i0 的权重为 128 - w
 */
void mapScaleCoord(int d, int srcLen, int dstLen, int& i0, int& i1, int& w)
{
    long long f = (static_cast<long long>(2 * d + 1) * srcLen * kFixedOne) / (2LL * dstLen) - kFixedOne / 2;
    if (f < 0)
        f = 0;
    i0 = static_cast<int>(f >> kFixedShift);
    int frac = static_cast<int>(f & (kFixedOne - 1));
    if (i0 >= srcLen - 1)
    {
        i0 = srcLen - 1;
        frac = 0;
    }
    i1 = (i0 + 1 < srcLen) ? i0 + 1 : i0;
    w = (frac + kScaleWeightRound) >> kScaleWeightShift;
}

} // namespace

void ComputeShareEncodeSize(int srcW, int srcH, int maxLongSide, int& outW, int& outH)
{
    outW = 0;
    outH = 0;
    if (srcW <= 0 || srcH <= 0)
        return;
    int w = srcW;
    int h = srcH;
    const int longSide = (w > h) ? w : h;
    if (maxLongSide > 0 && longSide > maxLongSide)
    {
        if (w >= h)
        {
            h = static_cast<int>(static_cast<long long>(h) * maxLongSide / w);
            w = maxLongSide;
        }
        else
        {
            w = static_cast<int>(static_cast<long long>(w) * maxLongSide / h);
            h = maxLongSide;
        }
    }
    outW = roundDownEven(w);
    outH = roundDownEven(h);
}

ScreenVideoParams::ScreenVideoParams()
    : m_maxLongSide(kShareMaxLongSide)
    , m_fps(kShareMaxFps)
    , m_baseBps(kShareBitrateBps)
    , m_floorBps(kShareMinBitrateBps)
{
}

ScreenVideoProfile::ScreenVideoProfile()
    : m_encW(0)
    , m_encH(0)
    , m_maxBps(0)
    , m_minBps(0)
{
}

namespace {

//按编码像素数换算码率：不超过基准像素数时取原值，否则按比例放大并按 kScreenBitrateRoundBps 四舍五入
int scaleBitrateByPixels(int baseBps, int64_t pixels)
{
    if (pixels <= kScreenRefPixels)
        return baseBps;
    const int64_t scaled = static_cast<int64_t>(baseBps) * pixels / kScreenRefPixels;
    const int64_t rounded = (scaled + kScreenBitrateRoundBps / 2) / kScreenBitrateRoundBps * kScreenBitrateRoundBps;
    return static_cast<int>(rounded);
}

} // namespace

void ComputeScreenVideoProfile(int srcW, int srcH, const ScreenVideoParams& params, ScreenVideoProfile& out)
{
    out = ScreenVideoProfile();
    int w = 0;
    int h = 0;
    ComputeShareEncodeSize(srcW, srcH, params.m_maxLongSide, w, h);
    if (w <= 0 || h <= 0)
        return;
    const int64_t pixels = static_cast<int64_t>(w) * h;
    out.m_encW = w;
    out.m_encH = h;
    out.m_maxBps = scaleBitrateByPixels(params.m_baseBps, pixels);
    out.m_minBps = scaleBitrateByPixels(params.m_floorBps, pixels);
    if (out.m_minBps > out.m_maxBps)
        out.m_minBps = out.m_maxBps;
}

int ScaleBitrateToRange(int currentBps, int oldMaxBps, int newMaxBps, int newMinBps)
{
    const int minBps = (newMinBps > newMaxBps) ? newMaxBps : newMinBps;
    int64_t target = newMaxBps;
    if (oldMaxBps > 0)
        target = static_cast<int64_t>(currentBps) * newMaxBps / oldMaxBps;
    if (target > newMaxBps)
        target = newMaxBps;
    if (target < minBps)
        target = minBps;
    return static_cast<int>(target);
}

int ShareMaxLongSideForQuality(int quality)
{
    switch (quality)
    {
    //1080p 档：与 2026-10-15 之前相同
    case share_quality_1080p:
        return kShareMaxLongSide;
    //4K 档：4K 屏原尺寸编码
    case share_quality_4k:
        return kShare4kLongSide;
    //2K 档与未知取值（配置损坏、旧配置没有这一项）：默认档
    case share_quality_2k:
    default:
        return kShare2kLongSide;
    }
}

ScreenVideoParams ShareVideoParamsForQuality(int quality)
{
    ScreenVideoParams p;
    p.m_maxLongSide = ShareMaxLongSideForQuality(quality);
    p.m_fps = kShareMaxFps;
    p.m_baseBps = kShareBitrateBps;
    p.m_floorBps = kShareMinBitrateBps;
    return p;
}

ScreenVideoParams ShareVideoParamsForControl(int quality, bool controlled)
{
    ScreenVideoParams p = ShareVideoParamsForQuality(quality);
    //被控制期间只提高帧率：长边与码率不变，码率自适应的范围也就不变
    if (controlled)
        p.m_fps = kShareControlledFps;
    return p;
}

ScreenVideoParams RdScreenVideoParams()
{
    ScreenVideoParams p;
    p.m_maxLongSide = kRdVideoMaxLongSide;
    p.m_fps = kRdVideoMaxFps;
    p.m_baseBps = kRdVideoBitrateBps;
    p.m_floorBps = kShareMinBitrateBps;
    return p;
}

namespace {

//把一个 2×2 块转成四个 Y 与一组 U / V（四个像素的平均颜色）：p0 / p1 为上下两行的第一个像素，
//y0 / y1 为上下两行的 Y 输出，uv 为这一块的 U、V 输出
inline void convertBlock2x2(const uint8_t* p0, const uint8_t* p1, uint8_t* y0, uint8_t* y1, uint8_t* uv)
{
    //四个像素：上左、上右、下左、下右（每像素 B、G、R、A 四个字节）
    const int b00 = p0[0];
    const int g00 = p0[1];
    const int r00 = p0[2];
    const int b01 = p0[kBytesPerPixel];
    const int g01 = p0[kBytesPerPixel + 1];
    const int r01 = p0[kBytesPerPixel + 2];
    const int b10 = p1[0];
    const int g10 = p1[1];
    const int r10 = p1[2];
    const int b11 = p1[kBytesPerPixel];
    const int g11 = p1[kBytesPerPixel + 1];
    const int r11 = p1[kBytesPerPixel + 2];
    y0[0] = static_cast<uint8_t>(((kYr * r00 + kYg * g00 + kYb * b00 + kRound) >> kCoefShift) + kYOffset);
    y0[1] = static_cast<uint8_t>(((kYr * r01 + kYg * g01 + kYb * b01 + kRound) >> kCoefShift) + kYOffset);
    y1[0] = static_cast<uint8_t>(((kYr * r10 + kYg * g10 + kYb * b10 + kRound) >> kCoefShift) + kYOffset);
    y1[1] = static_cast<uint8_t>(((kYr * r11 + kYg * g11 + kYb * b11 + kRound) >> kCoefShift) + kYOffset);
    const int r = (r00 + r01 + r10 + r11 + kAvgRound) >> kAvgShift;
    const int g = (g00 + g01 + g10 + g11 + kAvgRound) >> kAvgShift;
    const int b = (b00 + b01 + b10 + b11 + kAvgRound) >> kAvgShift;
    uv[0] = clampByte(((kUr * r + kUg * g + kUb * b + kRound) >> kCoefShift) + kUvOffset);
    uv[1] = clampByte(((kVr * r + kVg * g + kVb * b + kRound) >> kCoefShift) + kUvOffset);
}

//同尺寸的 BGRA → NV12，逐像素（参考实现）：一次处理两行
void convertSameSizeScalar(const uint8_t* bgra, int stride, uint8_t* nv12, int w, int h)
{
    uint8_t* uvPlane = nv12 + static_cast<size_t>(w) * h;
    for (int y = 0; y + 1 < h; y += 2)
    {
        const uint8_t* p0 = bgra + static_cast<size_t>(y) * stride;
        const uint8_t* p1 = p0 + stride;
        uint8_t* y0 = nv12 + static_cast<size_t>(y) * w;
        uint8_t* y1 = y0 + w;
        uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * w;
        for (int x = 0; x + 1 < w; x += 2)
            convertBlock2x2(p0 + x * kBytesPerPixel, p1 + x * kBytesPerPixel, y0 + x, y1 + x, uv + x);
    }
}

//四个像素（lo 为前两个、hi 为后两个，每像素 B、G、R、A 四个 16 位分量）各自的「系数 · 颜色」之和，
//加舍入值后右移 8 位再加偏移，结果为四个 32 位整数。用于 Y（亮度）的计算
inline __m128i weightedSum4(__m128i lo, __m128i hi, __m128i coef, __m128i round32, __m128i offset)
{
    //madd 把相邻两个分量的乘积相加：每个像素得到 (B·cb + G·cg) 与 (R·cr + A·0) 两个 32 位数
    const __m128 ml = _mm_castsi128_ps(_mm_madd_epi16(lo, coef));
    const __m128 mh = _mm_castsi128_ps(_mm_madd_epi16(hi, coef));
    //按偶数位、奇数位分开再相加，得到四个像素各自的和
    const __m128i even = _mm_castps_si128(_mm_shuffle_ps(ml, mh, _MM_SHUFFLE(2, 0, 2, 0)));
    const __m128i odd = _mm_castps_si128(_mm_shuffle_ps(ml, mh, _MM_SHUFFLE(3, 1, 3, 1)));
    const __m128i sum = _mm_add_epi32(even, odd);
    return _mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(sum, round32), kCoefShift), offset);
}

//同尺寸的 BGRA → NV12，SSE2：一次 4 列 × 2 行（8 个 Y、两组 U / V），剩下不足 4 列的部分逐像素。
//运算与 convertBlock2x2 完全相同（同样的定点系数、舍入与算术右移），结果逐字节一致（单元测试 SSVP7）
void convertSameSizeSse2(const uint8_t* bgra, int stride, uint8_t* nv12, int w, int h)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i coefY = _mm_setr_epi16(kYb, kYg, kYr, 0, kYb, kYg, kYr, 0);
    const __m128i coefU = _mm_setr_epi16(kUb, kUg, kUr, 0, kUb, kUg, kUr, 0);
    const __m128i coefV = _mm_setr_epi16(kVb, kVg, kVr, 0, kVb, kVg, kVr, 0);
    const __m128i round32 = _mm_set1_epi32(kRound);
    const __m128i yOffset = _mm_set1_epi32(kYOffset);
    const __m128i uvOffset = _mm_set1_epi32(kUvOffset);
    const __m128i avgRound = _mm_set1_epi16(kAvgRound);
    uint8_t* uvPlane = nv12 + static_cast<size_t>(w) * h;
    for (int y = 0; y + 1 < h; y += 2)
    {
        const uint8_t* p0 = bgra + static_cast<size_t>(y) * stride;
        const uint8_t* p1 = p0 + stride;
        uint8_t* y0 = nv12 + static_cast<size_t>(y) * w;
        uint8_t* y1 = y0 + w;
        uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * w;
        int x = 0;
        for (; x + kSimdCols <= w; x += kSimdCols)
        {
            //上下两行各取 4 个像素，展开成 16 位分量：lo 为前两个像素，hi 为后两个像素
            const __m128i a0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p0 + x * kBytesPerPixel));
            const __m128i a1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1 + x * kBytesPerPixel));
            const __m128i lo0 = _mm_unpacklo_epi8(a0, zero);
            const __m128i hi0 = _mm_unpackhi_epi8(a0, zero);
            const __m128i lo1 = _mm_unpacklo_epi8(a1, zero);
            const __m128i hi1 = _mm_unpackhi_epi8(a1, zero);

            //亮度：两行各 4 个 Y，收窄成字节后分别写入上下两行
            const __m128i yRow0 = weightedSum4(lo0, hi0, coefY, round32, yOffset);
            const __m128i yRow1 = weightedSum4(lo1, hi1, coefY, round32, yOffset);
            const __m128i y16 = _mm_packs_epi32(yRow0, yRow1);
            const __m128i y8 = _mm_packus_epi16(y16, y16);
            const int yBytes0 = _mm_cvtsi128_si32(y8);
            const int yBytes1 = _mm_cvtsi128_si32(_mm_srli_si128(y8, kSimdCols));
            memcpy(y0 + x, &yBytes0, kSimdCols);
            memcpy(y1 + x, &yBytes1, kSimdCols);

            //色度：两个 2×2 块各自四个像素的分量求和（左块在低 64 位、右块在高 64 位），再取平均
            const __m128i colLo = _mm_add_epi16(lo0, lo1);
            const __m128i colHi = _mm_add_epi16(hi0, hi1);
            const __m128i blockL = _mm_add_epi16(colLo, _mm_srli_si128(colLo, 8));
            const __m128i blockR = _mm_add_epi16(colHi, _mm_srli_si128(colHi, 8));
            const __m128i blocks = _mm_unpacklo_epi64(blockL, blockR);
            const __m128i avg = _mm_srli_epi16(_mm_add_epi16(blocks, avgRound), kAvgShift);
            //U、V：每块得到 (B·cb + G·cg) 与 (R·cr) 两个 32 位数，相加后为 [U左, U右, V左, V右]
            const __m128 mu = _mm_castsi128_ps(_mm_madd_epi16(avg, coefU));
            const __m128 mv = _mm_castsi128_ps(_mm_madd_epi16(avg, coefV));
            const __m128i even = _mm_castps_si128(_mm_shuffle_ps(mu, mv, _MM_SHUFFLE(2, 0, 2, 0)));
            const __m128i odd = _mm_castps_si128(_mm_shuffle_ps(mu, mv, _MM_SHUFFLE(3, 1, 3, 1)));
            __m128i uvSum = _mm_add_epi32(even, odd);
            uvSum = _mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(uvSum, round32), kCoefShift), uvOffset);
            //重排成 NV12 的交错顺序 [U左, V左, U右, V右]，收窄成字节（packus 限幅到 0~255，与 clampByte 相同）
            uvSum = _mm_shuffle_epi32(uvSum, _MM_SHUFFLE(3, 1, 2, 0));
            const __m128i uv16 = _mm_packs_epi32(uvSum, uvSum);
            const __m128i uv8 = _mm_packus_epi16(uv16, uv16);
            const int uvBytes = _mm_cvtsi128_si32(uv8);
            memcpy(uv + x, &uvBytes, kSimdCols);
        }
        for (; x + 1 < w; x += 2)
            convertBlock2x2(p0 + x * kBytesPerPixel, p1 + x * kBytesPerPixel, y0 + x, y1 + x, uv + x);
    }
}

//同尺寸转换函数的类型（向量化实现与参考实现共用缩放流程）
typedef void (*SameSizeConvertFn)(const uint8_t* bgra, int stride, uint8_t* nv12, int w, int h);

//缩放函数的类型（BgraScaler 的两种实现，与同尺寸转换配对使用）
typedef void (BgraScaler::*ScaleFn)(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst,
                                    int dstStride, int dstW, int dstH);

//带缩放的转换：尺寸相同时直接转换，否则先双线性缩放成 BGRA 再转换。scaler 与 scaled 由调用方持有，可以跨帧复用
void convertWith(SameSizeConvertFn convert, ScaleFn scale, BgraScaler& scaler, std::vector<uint8_t>& scaled,
                 const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW, int dstH)
{
    if (bgra == NULL || nv12 == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    //尺寸相同（最常见：1080p 及以下的屏幕与窗口）直接转换，不经过缩放缓冲
    if (srcW == dstW && srcH == dstH)
    {
        convert(bgra, stride, nv12, dstW, dstH);
        return;
    }
    scaled.resize(static_cast<size_t>(dstW) * dstH * kBytesPerPixel);
    (scaler.*scale)(bgra, stride, srcW, srcH, &scaled[0], dstW * kBytesPerPixel, dstW, dstH);
    convert(&scaled[0], dstW * kBytesPerPixel, nv12, dstW, dstH);
}

} // namespace

void ConvertBgraToNv12(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW, int dstH)
{
    Nv12FrameConverter converter;
    converter.Convert(bgra, stride, srcW, srcH, nv12, dstW, dstH);
}

void ConvertBgraToNv12Sse2(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW, int dstH)
{
    BgraScaler scaler;
    std::vector<uint8_t> scaled;
    convertWith(convertSameSizeSse2, &BgraScaler::ScaleSse2, scaler, scaled, bgra, stride, srcW, srcH, nv12, dstW,
                dstH);
}

void ConvertBgraToNv12Reference(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW,
                                int dstH)
{
    BgraScaler scaler;
    std::vector<uint8_t> scaled;
    convertWith(convertSameSizeScalar, &BgraScaler::ScaleReference, scaler, scaled, bgra, stride, srcW, srcH, nv12,
                dstW, dstH);
}

void Nv12FrameConverter::Convert(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW,
                                 int dstH)
{
    //Debug 版不优化向量指令，SSE2 实现比逐像素实现慢约一倍，所以只在 Release 版使用（实测数据见头文件）
#ifdef _DEBUG
    convertWith(convertSameSizeScalar, &BgraScaler::ScaleReference, m_scaler, m_scaled, bgra, stride, srcW, srcH,
                nv12, dstW, dstH);
#else
    convertWith(convertSameSizeSse2, &BgraScaler::ScaleSse2, m_scaler, m_scaled, bgra, stride, srcW, srcH, nv12, dstW,
                dstH);
#endif
}

BgraScaler::BgraScaler()
    : m_tableSrcW(0)
    , m_tableDstW(0)
{
}

void BgraScaler::prepare(int srcW, int dstW)
{
    m_row.resize(static_cast<size_t>(srcW) * kBytesPerPixel);
    if (srcW == m_tableSrcW && dstW == m_tableDstW)
        return;
    m_x0.resize(dstW);
    m_x1.resize(dstW);
    m_wx.resize(dstW);
    m_wpair.resize(dstW);
    for (int dx = 0; dx < dstW; ++dx)
    {
        int x0 = 0;
        int x1 = 0;
        int w = 0;
        mapScaleCoord(dx, srcW, dstW, x0, x1, w);
        m_x0[dx] = x0;
        m_x1[dx] = x1;
        m_wx[dx] = w;
        //低 16 位乘左边的中间值、高 16 位乘右边的中间值（_mm_madd_epi16 的成对乘加）
        m_wpair[dx] = static_cast<int32_t>((static_cast<uint32_t>(w) << 16) | static_cast<uint32_t>(kScaleWeightOne - w));
    }
    m_tableSrcW = srcW;
    m_tableDstW = dstW;
}

void BgraScaler::Scale(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride, int dstW,
                       int dstH)
{
#ifdef _DEBUG
    ScaleReference(src, srcStride, srcW, srcH, dst, dstStride, dstW, dstH);
#else
    ScaleSse2(src, srcStride, srcW, srcH, dst, dstStride, dstW, dstH);
#endif
}

void BgraScaler::ScaleReference(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride,
                                int dstW, int dstH)
{
    if (src == NULL || dst == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    prepare(srcW, dstW);
    const int rowValues = srcW * kBytesPerPixel;
    for (int dy = 0; dy < dstH; ++dy)
    {
        int y0 = 0;
        int y1 = 0;
        int wy = 0;
        mapScaleCoord(dy, srcH, dstH, y0, y1, wy);
        const uint8_t* r0 = src + static_cast<size_t>(y0) * srcStride;
        const uint8_t* r1 = src + static_cast<size_t>(y1) * srcStride;
        //纵向：两个源行按权重混合成中间行
        for (int k = 0; k < rowValues; ++k)
            m_row[k] = static_cast<int16_t>(r0[k] * (kScaleWeightOne - wy) + r1[k] * wy);
        //横向：每个目标像素取两个中间值按权重混合
        uint8_t* out = dst + static_cast<size_t>(dy) * dstStride;
        for (int dx = 0; dx < dstW; ++dx)
        {
            const int16_t* t0 = &m_row[static_cast<size_t>(m_x0[dx]) * kBytesPerPixel];
            const int16_t* t1 = &m_row[static_cast<size_t>(m_x1[dx]) * kBytesPerPixel];
            const int wx = m_wx[dx];
            for (int c = 0; c < kBytesPerPixel; ++c)
            {
                const int v = (t0[c] * (kScaleWeightOne - wx) + t1[c] * wx + kScaleFinalRound) >> kScaleFinalShift;
                out[dx * kBytesPerPixel + c] = static_cast<uint8_t>(v);
            }
        }
    }
}

void BgraScaler::ScaleSse2(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride,
                           int dstW, int dstH)
{
    if (src == NULL || dst == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    prepare(srcW, dstW);
    const int rowValues = srcW * kBytesPerPixel;
    const __m128i zero = _mm_setzero_si128();
    const __m128i finalRound = _mm_set1_epi32(kScaleFinalRound);
    for (int dy = 0; dy < dstH; ++dy)
    {
        int y0 = 0;
        int y1 = 0;
        int wy = 0;
        mapScaleCoord(dy, srcH, dstH, y0, y1, wy);
        const uint8_t* r0 = src + static_cast<size_t>(y0) * srcStride;
        const uint8_t* r1 = src + static_cast<size_t>(y1) * srcStride;
        //纵向：一次 16 个字节，展开成 16 位后乘权重相加（结果不超过 255 × 128，16 位乘法不会溢出）
        const __m128i w0 = _mm_set1_epi16(static_cast<short>(kScaleWeightOne - wy));
        const __m128i w1 = _mm_set1_epi16(static_cast<short>(wy));
        int k = 0;
        for (; k + kScaleSimdBytes <= rowValues; k += kScaleSimdBytes)
        {
            const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(r0 + k));
            const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(r1 + k));
            const __m128i lo = _mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(a, zero), w0),
                                             _mm_mullo_epi16(_mm_unpacklo_epi8(b, zero), w1));
            const __m128i hi = _mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(a, zero), w0),
                                             _mm_mullo_epi16(_mm_unpackhi_epi8(b, zero), w1));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&m_row[k]), lo);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&m_row[k + kScaleSimdBytes / 2]), hi);
        }
        for (; k < rowValues; ++k)
            m_row[k] = static_cast<int16_t>(r0[k] * (kScaleWeightOne - wy) + r1[k] * wy);

        //横向：一次两个像素。左右两个中间值按通道交错，madd 一步得到「左 × (128 - wx) + 右 × wx」的四个 32 位和
        uint8_t* out = dst + static_cast<size_t>(dy) * dstStride;
        int dx = 0;
        for (; dx + kScaleSimdPixels <= dstW; dx += kScaleSimdPixels)
        {
            const __m128i p0 = _mm_unpacklo_epi16(
                _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&m_row[static_cast<size_t>(m_x0[dx]) * kBytesPerPixel])),
                _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&m_row[static_cast<size_t>(m_x1[dx]) * kBytesPerPixel])));
            const __m128i p1 = _mm_unpacklo_epi16(
                _mm_loadl_epi64(
                    reinterpret_cast<const __m128i*>(&m_row[static_cast<size_t>(m_x0[dx + 1]) * kBytesPerPixel])),
                _mm_loadl_epi64(
                    reinterpret_cast<const __m128i*>(&m_row[static_cast<size_t>(m_x1[dx + 1]) * kBytesPerPixel])));
            const __m128i s0 = _mm_srai_epi32(
                _mm_add_epi32(_mm_madd_epi16(p0, _mm_set1_epi32(m_wpair[dx])), finalRound), kScaleFinalShift);
            const __m128i s1 = _mm_srai_epi32(
                _mm_add_epi32(_mm_madd_epi16(p1, _mm_set1_epi32(m_wpair[dx + 1])), finalRound), kScaleFinalShift);
            const __m128i v16 = _mm_packs_epi32(s0, s1);
            const __m128i v8 = _mm_packus_epi16(v16, v16);
            _mm_storel_epi64(reinterpret_cast<__m128i*>(out + static_cast<size_t>(dx) * kBytesPerPixel), v8);
        }
        for (; dx < dstW; ++dx)
        {
            const int16_t* t0 = &m_row[static_cast<size_t>(m_x0[dx]) * kBytesPerPixel];
            const int16_t* t1 = &m_row[static_cast<size_t>(m_x1[dx]) * kBytesPerPixel];
            const int wx = m_wx[dx];
            for (int c = 0; c < kBytesPerPixel; ++c)
            {
                const int v = (t0[c] * (kScaleWeightOne - wx) + t1[c] * wx + kScaleFinalRound) >> kScaleFinalShift;
                out[dx * kBytesPerPixel + c] = static_cast<uint8_t>(v);
            }
        }
    }
}

ShareFramePacer::Decision::Decision()
    : m_encode(false)
    , m_keyframe(false)
{
}

ShareFramePacer::ShareFramePacer(int maxFps, int64_t keyFrameIntervalMs)
    : m_minIntervalMs(kMsPerSecond / (maxFps > 0 ? maxFps : kShareMaxFps))
    , m_keyIntervalMs(keyFrameIntervalMs > 0 ? keyFrameIntervalMs : kShareKeyFrameIntervalMs)
    , m_everEncoded(false)
    , m_lastEncodeMs(0)
    , m_lastKeyFrameMs(0)
{
}

ShareFramePacer::Decision ShareFramePacer::decide(int64_t nowMs, bool hasNewFrame, bool keyFrameRequested,
                                                  bool hasLastFrame) const
{
    Decision d;
    //从没编过码：有新画面就编成关键帧
    if (!m_everEncoded)
    {
        d.m_encode = hasNewFrame;
        d.m_keyframe = hasNewFrame;
        return d;
    }
    //关键帧请求：立即编，不受帧率上限约束；没有新画面就编最近一帧
    if (keyFrameRequested && (hasNewFrame || hasLastFrame))
    {
        d.m_encode = true;
        d.m_keyframe = true;
        return d;
    }
    if (!hasNewFrame || nowMs - m_lastEncodeMs < m_minIntervalMs)
        return d;
    d.m_encode = true;
    d.m_keyframe = (nowMs - m_lastKeyFrameMs >= m_keyIntervalMs);
    return d;
}

void ShareFramePacer::onEncoded(int64_t nowMs, bool keyframe)
{
    m_everEncoded = true;
    m_lastEncodeMs = nowMs;
    if (keyframe)
        m_lastKeyFrameMs = nowMs;
}

void ShareFramePacer::SetMaxFps(int maxFps)
{
    m_minIntervalMs = kMsPerSecond / (maxFps > 0 ? maxFps : kShareMaxFps);
}

int64_t ShareFramePacer::minIntervalMs() const
{
    return m_minIntervalMs;
}

void ComputeCameraEncodeSize(int srcW, int srcH, int& outW, int& outH)
{
    outW = 0;
    outH = 0;
    if (srcW <= 0 || srcH <= 0)
        return;
    //按比例缩进框内，只缩不放：取宽、高两个方向缩小比例中更小的那个
    double scale = 1.0;
    if (srcW > kShareCameraMaxW)
        scale = static_cast<double>(kShareCameraMaxW) / srcW;
    if (srcH * scale > kShareCameraMaxH)
        scale = static_cast<double>(kShareCameraMaxH) / srcH;
    int w = static_cast<int>(srcW * scale);
    int h = static_cast<int>(srcH * scale);
    //H.264 的 4:2:0 要求宽高为偶数
    w &= ~1;
    h &= ~1;
    outW = (w < kMinEncodeSide) ? kMinEncodeSide : w;
    outH = (h < kMinEncodeSide) ? kMinEncodeSide : h;
}

void ScaleNv12(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstW, int dstH)
{
    if (src == NULL || dst == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    const uint8_t* srcUv = src + static_cast<size_t>(srcStride) * srcH;
    uint8_t* dstUv = dst + static_cast<size_t>(dstW) * dstH;
    //亮度：最近邻取样
    for (int y = 0; y < dstH; ++y)
    {
        const int sy = static_cast<int>(static_cast<int64_t>(y) * srcH / dstH);
        const uint8_t* srow = src + static_cast<size_t>(sy) * srcStride;
        uint8_t* drow = dst + static_cast<size_t>(y) * dstW;
        for (int x = 0; x < dstW; ++x)
            drow[x] = srow[static_cast<int64_t>(x) * srcW / dstW];
    }
    //色度：UV 交错存放，按半分辨率的坐标取样，U、V 成对拷贝
    const int srcCw = srcW / 2;
    const int srcCh = srcH / 2;
    const int dstCw = dstW / 2;
    const int dstCh = dstH / 2;
    for (int y = 0; y < dstCh; ++y)
    {
        const int sy = static_cast<int>(static_cast<int64_t>(y) * srcCh / dstCh);
        const uint8_t* srow = srcUv + static_cast<size_t>(sy) * srcStride;
        uint8_t* drow = dstUv + static_cast<size_t>(y) * dstW;
        for (int x = 0; x < dstCw; ++x)
        {
            const int sx = static_cast<int>(static_cast<int64_t>(x) * srcCw / dstCw);
            drow[x * 2] = srow[sx * 2];
            drow[x * 2 + 1] = srow[sx * 2 + 1];
        }
    }
}

} // namespace screenshare
