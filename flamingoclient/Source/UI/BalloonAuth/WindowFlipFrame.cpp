/**
 *  WindowFlipFrame.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "WindowFlipFrame.h"

#include <math.h>

namespace balloonauth {

namespace {

const float kPi = 3.14159265358979f;    // 圆周率
const float kHalf = 0.5f;               // 一半：中点、像素中心
const float kFixedOne = 256.0f;         // 像素缩放用的定点数：256 表示 1.0
const uint32_t kFixedShift = 8;         // 定点数右移位数，与 kFixedOne 对应
const uint32_t kByteMask = 0xFF;        // 取一个颜色分量
const uint32_t kAlphaShift = 24;        // 预乘 BGRA 里 alpha 分量的位置
const uint32_t kRedShift = 16;          // 红色分量的位置
const uint32_t kGreenShift = 8;         // 绿色分量的位置

// 截到 [0, 1]
float Clamp01(float v)
{
    return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

// 线性插值：k 为 0 时得 a，为 1 时得 b
float Lerp(float a, float b, float k)
{
    return a + (b - a) * k;
}

// [a0, a1) 与 [b0, b1) 的重叠长度（不重叠时为 0）
float OverlapLength(float a0, float a1, float b0, float b1)
{
    const float lo = (a0 > b0) ? a0 : b0;
    const float hi = (a1 < b1) ? a1 : b1;
    return (hi > lo) ? hi - lo : 0.0f;
}

// 预乘像素整体缩放：颜色分量乘 rgbScale、alpha 乘 alphaScale（rgbScale 不大于 alphaScale，结果仍是合法的预乘像素）
uint32_t ScalePixel(uint32_t src, float rgbScale, float alphaScale)
{
    const uint32_t kc = static_cast<uint32_t>(rgbScale * kFixedOne + kHalf);
    const uint32_t ka = static_cast<uint32_t>(alphaScale * kFixedOne + kHalf);
    const uint32_t a = (((src >> kAlphaShift) & kByteMask) * ka) >> kFixedShift;
    const uint32_t r = (((src >> kRedShift) & kByteMask) * kc) >> kFixedShift;
    const uint32_t g = (((src >> kGreenShift) & kByteMask) * kc) >> kFixedShift;
    const uint32_t b = ((src & kByteMask) * kc) >> kFixedShift;
    return (a << kAlphaShift) | (r << kRedShift) | (g << kGreenShift) | b;
}

// 截到 [lo, hi]
int ClampInt(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

} // namespace

float FlipEase(float t)
{
    const float x = Clamp01(t);
    return kHalf - kHalf * cosf(kPi * x);
}

float FlipProgress(uint64_t elapsedMs, uint32_t durationMs)
{
    if (durationMs == 0 || elapsedMs >= durationMs)
    {
        return 1.0f;
    }
    return static_cast<float>(elapsedMs) / static_cast<float>(durationMs);
}

RECT FlipCanvasRect(const RECT& from, const RECT& to)
{
    RECT rc;
    rc.left = (from.left < to.left) ? from.left : to.left;
    rc.top = (from.top < to.top) ? from.top : to.top;
    rc.right = (from.right > to.right) ? from.right : to.right;
    rc.bottom = (from.bottom > to.bottom) ? from.bottom : to.bottom;
    return rc;
}

FlipFrame ComputeFlipFrame(const RECT& from, const RECT& to, float t, FlipDirection dir)
{
    const RECT canvas = FlipCanvasRect(from, to);
    const float e = FlipEase(t);
    const float theta = kPi * e;
    //转角取 π 时 sinf 的结果是一个极小的负数，截到 0：两端的帧不压暗、不缩进
    const float sinRaw = sinf(theta);
    const float sinT = (sinRaw > 0.0f) ? sinRaw : 0.0f;

    //画面矩形在起始与目标之间插值：水平中心、上边缘、宽、高
    const float fromCx = (static_cast<float>(from.left) + static_cast<float>(from.right)) * kHalf;
    const float toCx = (static_cast<float>(to.left) + static_cast<float>(to.right)) * kHalf;
    const float cx = Lerp(fromCx, toCx, e) - static_cast<float>(canvas.left);
    const float top = Lerp(static_cast<float>(from.top), static_cast<float>(to.top), e) - static_cast<float>(canvas.top);
    const float w = Lerp(static_cast<float>(from.right - from.left), static_cast<float>(to.right - to.left), e);
    const float h = Lerp(static_cast<float>(from.bottom - from.top), static_cast<float>(to.bottom - to.top), e);

    //以竖直中线为轴水平压扁；转到 90 度（e = 0.5）时宽度为 0
    const float halfW = w * fabsf(cosf(theta)) * kHalf;
    //远端竖边上下各缩进的距离
    const float inset = h * kFlipPerspective * sinT * kHalf;
    const bool firstHalf = (e < kHalf);
    //哪一侧是远端：正向前半程在右、后半程在左；反向与之镜像
    const bool farRight = ((dir == kFlipForward) == firstHalf);

    FlipFrame f;
    f.m_showTarget = !firstHalf;
    f.m_left = cx - halfW;
    f.m_right = cx + halfW;
    f.m_leftTop = top + (farRight ? 0.0f : inset);
    f.m_leftBottom = top + h - (farRight ? 0.0f : inset);
    f.m_rightTop = top + (farRight ? inset : 0.0f);
    f.m_rightBottom = top + h - (farRight ? inset : 0.0f);
    f.m_shade = kFlipMaxShade * sinT;
    return f;
}

RECT AlignTopCenter(const RECT& anchor, int width, int height, const RECT& workArea)
{
    int left = (anchor.left + anchor.right) / 2 - width / 2;
    int top = anchor.top;
    //先保证右、下边不出界，再保证左、上边不出界：窗口比工作区还大时以左上角为准
    if (left + width > workArea.right)
    {
        left = workArea.right - width;
    }
    if (top + height > workArea.bottom)
    {
        top = workArea.bottom - height;
    }
    if (left < workArea.left)
    {
        left = workArea.left;
    }
    if (top < workArea.top)
    {
        top = workArea.top;
    }
    RECT rc;
    rc.left = left;
    rc.top = top;
    rc.right = left + width;
    rc.bottom = top + height;
    return rc;
}

void RenderFlipFrame(const FlipBitmap& image, const FlipFrame& frame, FlipBitmap& canvas)
{
    const int cw = (canvas.m_width > 0) ? canvas.m_width : 0;
    const int ch = (canvas.m_height > 0) ? canvas.m_height : 0;
    canvas.m_pixels.assign(static_cast<size_t>(cw) * static_cast<size_t>(ch), 0);
    const int iw = image.m_width;
    const int ih = image.m_height;
    if (cw == 0 || ch == 0 || iw <= 0 || ih <= 0 ||
        image.m_pixels.size() < static_cast<size_t>(iw) * static_cast<size_t>(ih))
    {
        return;
    }
    const float span = frame.m_right - frame.m_left;
    if (span <= 0.0f)
    {
        //正好转到侧面：本帧什么都不画
        return;
    }
    const float shadeMul = 1.0f - Clamp01(frame.m_shade);
    //整像素（横纵都完全被梯形盖住）只按压暗比例缩放颜色分量、alpha 不变，走定点数整数运算：调试版里每帧
    //二十多万个像素都走浮点与函数调用的话一帧要七八十毫秒，翻转会明显卡顿（2026-10-07 实测）
    const uint32_t shadeFixed = static_cast<uint32_t>(shadeMul * kFixedOne + kHalf);
    //直接用指针读写：调试版的 std::vector 下标运算带边界检查，逐像素调用开销很大
    const uint32_t* srcPixels = &image.m_pixels[0];
    uint32_t* dstPixels = &canvas.m_pixels[0];
    const int x0 = ClampInt(static_cast<int>(floorf(frame.m_left)), 0, cw);
    const int x1 = ClampInt(static_cast<int>(ceilf(frame.m_right)), 0, cw);
    for (int x = x0; x < x1; ++x)
    {
        const float px0 = static_cast<float>(x);
        const float px1 = px0 + 1.0f;
        //本列与梯形横向重叠的宽度就是横向覆盖率；取样位置取重叠部分的中点
        const float covX = OverlapLength(px0, px1, frame.m_left, frame.m_right);
        if (covX <= 0.0f)
        {
            continue;
        }
        const float midX = ((px0 > frame.m_left ? px0 : frame.m_left) + (px1 < frame.m_right ? px1 : frame.m_right)) * kHalf;
        const float fx = Clamp01((midX - frame.m_left) / span);
        const int u = ClampInt(static_cast<int>(fx * static_cast<float>(iw)), 0, iw - 1);
        //本列的上下端在左右两条竖边之间线性插值（梯形）
        const float colTop = Lerp(frame.m_leftTop, frame.m_rightTop, fx);
        const float colBottom = Lerp(frame.m_leftBottom, frame.m_rightBottom, fx);
        const float colH = colBottom - colTop;
        if (colH <= 0.0f)
        {
            continue;
        }
        const float vScale = static_cast<float>(ih) / colH;
        const bool fullColumn = (covX >= 1.0f);
        const int y0 = ClampInt(static_cast<int>(floorf(colTop)), 0, ch);
        const int y1 = ClampInt(static_cast<int>(ceilf(colBottom)), 0, ch);
        uint32_t* dst = dstPixels + static_cast<size_t>(y0) * static_cast<size_t>(cw) + static_cast<size_t>(x);
        for (int y = y0; y < y1; ++y, dst += cw)
        {
            const float py0 = static_cast<float>(y);
            const float py1 = py0 + 1.0f;
            const bool fullRow = (py0 >= colTop && py1 <= colBottom);
            float midY = py0 + kHalf;
            float covY = 1.0f;
            if (!fullRow)
            {
                covY = OverlapLength(py0, py1, colTop, colBottom);
                if (covY <= 0.0f)
                {
                    continue;
                }
                midY = ((py0 > colTop ? py0 : colTop) + (py1 < colBottom ? py1 : colBottom)) * kHalf;
            }
            int v = static_cast<int>((midY - colTop) * vScale);
            v = (v < 0) ? 0 : ((v >= ih) ? ih - 1 : v);
            const uint32_t s = srcPixels[static_cast<size_t>(v) * static_cast<size_t>(iw) + static_cast<size_t>(u)];
            if (fullColumn && fullRow)
            {
                const uint32_t r = (((s >> kRedShift) & kByteMask) * shadeFixed) >> kFixedShift;
                const uint32_t g = (((s >> kGreenShift) & kByteMask) * shadeFixed) >> kFixedShift;
                const uint32_t b = ((s & kByteMask) * shadeFixed) >> kFixedShift;
                *dst = (s & (kByteMask << kAlphaShift)) | (r << kRedShift) | (g << kGreenShift) | b;
                continue;
            }
            //梯形边缘的像素：按覆盖面积给出半透明
            const float cov = covX * covY;
            *dst = ScalePixel(s, shadeMul * cov, cov);
        }
    }
}

} // namespace balloonauth
