/**
 *  把一帧 BGRA 画面画到窗口上的绘制器，见 ShareFrameDrawer.h。
 *  balloonwj@qq.com   2026-10-15
 */
#include "ShareFrameDrawer.h"

#include <string.h>

namespace screenshare {

namespace {

//每像素字节数（BGRA）
const int kDrawerBpp = 4;
//位图头里的每像素位数
const WORD kDrawerBitsPerPixel = 32;
//一次双线性最多缩小的倍数：超过它先逐次减半
const int kSinglePassMaxShrink = 2;

//填一个自上而下、32 位的位图头
void fillHeader(BITMAPINFO& bi, int width, int height)
{
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    //高为负数：自上而下的位图
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kDrawerBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
}

} // namespace

bool ShareUsesFastDownscale(int srcW, int srcH, int dstW, int dstH)
{
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return false;
    //两个方向都不放大，且不是原尺寸（原尺寸直接贴）
    return dstW <= srcW && dstH <= srcH && (dstW != srcW || dstH != srcH);
}

BgraDownscaler::BgraDownscaler()
{
}

int BgraDownscaler::HalvingSteps(int srcW, int srcH, int dstW, int dstH)
{
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return 0;
    int w = srcW;
    int h = srcH;
    int steps = 0;
    while (steps < kMaxHalvingSteps && (dstW * kSinglePassMaxShrink < w || dstH * kSinglePassMaxShrink < h))
    {
        if (dstW * kSinglePassMaxShrink < w)
            w /= kSinglePassMaxShrink;
        if (dstH * kSinglePassMaxShrink < h)
            h /= kSinglePassMaxShrink;
        ++steps;
    }
    return steps;
}

void BgraDownscaler::Scale(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride, int dstW,
                           int dstH)
{
    if (src == NULL || dst == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    //逐次减半：每一步只减那些仍超过 2 倍的方向，结果放进各自的中间缓冲
    const uint8_t* cur = src;
    int curStride = srcStride;
    int curW = srcW;
    int curH = srcH;
    const int steps = HalvingSteps(srcW, srcH, dstW, dstH);
    for (int i = 0; i < steps; ++i)
    {
        const int nextW = (dstW * kSinglePassMaxShrink < curW) ? curW / kSinglePassMaxShrink : curW;
        const int nextH = (dstH * kSinglePassMaxShrink < curH) ? curH / kSinglePassMaxShrink : curH;
        m_work[i].resize(static_cast<size_t>(nextW) * nextH * kDrawerBpp);
        m_halvers[i].Scale(cur, curStride, curW, curH, &m_work[i][0], nextW * kDrawerBpp, nextW, nextH);
        cur = &m_work[i][0];
        curStride = nextW * kDrawerBpp;
        curW = nextW;
        curH = nextH;
    }
    //剩下的比例不超过 2：一次双线性（没有减半时与 BgraScaler 直接缩放逐字节一致）
    m_final.Scale(cur, curStride, curW, curH, dst, dstStride, dstW, dstH);
}

ShareFrameDrawer::ShareFrameDrawer()
    : m_lastPath(share_draw_none)
{
}

int ShareFrameDrawer::LastPath() const
{
    return m_lastPath;
}

void ShareFrameDrawer::blit(HDC hdc, int x, int y, const uint8_t* bgra, int w, int h)
{
    BITMAPINFO bi;
    fillHeader(bi, w, h);
    //SetDIBitsToDevice 不按缩放模式处理，1:1 时比 HALFTONE 的 StretchDIBits 快得多
    ::SetDIBitsToDevice(hdc, x, y, static_cast<DWORD>(w), static_cast<DWORD>(h), 0, 0, 0, static_cast<UINT>(h), bgra, &bi,
                        DIB_RGB_COLORS);
}

int ShareFrameDrawer::Draw(HDC hdc, const RECT& dest, const uint8_t* bgra, int width, int height, int stride)
{
    const int dstW = dest.right - dest.left;
    const int dstH = dest.bottom - dest.top;
    if (hdc == NULL || bgra == NULL || width <= 0 || height <= 0 || stride < width * kDrawerBpp || dstW <= 0 || dstH <= 0)
    {
        m_lastPath = share_draw_none;
        return m_lastPath;
    }

    //原尺寸：直接贴（行跨度不紧凑时先排紧，SetDIBitsToDevice 要求行跨度 = 宽 × 4）
    if (dstW == width && dstH == height)
    {
        const uint8_t* rows = bgra;
        if (stride != width * kDrawerBpp)
        {
            const size_t rowBytes = static_cast<size_t>(width) * kDrawerBpp;
            m_packed.resize(rowBytes * height);
            for (int y = 0; y < height; ++y)
                memcpy(&m_packed[rowBytes * y], bgra + static_cast<size_t>(stride) * y, rowBytes);
            rows = &m_packed[0];
        }
        blit(hdc, dest.left, dest.top, rows, width, height);
        m_lastPath = share_draw_copy;
        return m_lastPath;
    }

    //缩小：BgraDownscaler 缩到目标尺寸再贴
    if (ShareUsesFastDownscale(width, height, dstW, dstH))
    {
        m_scaled.resize(static_cast<size_t>(dstW) * dstH * kDrawerBpp);
        m_scaler.Scale(bgra, stride, width, height, &m_scaled[0], dstW * kDrawerBpp, dstW, dstH);
        blit(hdc, dest.left, dest.top, &m_scaled[0], dstW, dstH);
        m_lastPath = share_draw_fast_scale;
        return m_lastPath;
    }

    //放大：半色调模式，屏幕上的文字更平滑
    BITMAPINFO bi;
    fillHeader(bi, width, height);
    const uint8_t* rows = bgra;
    if (stride != width * kDrawerBpp)
    {
        const size_t rowBytes = static_cast<size_t>(width) * kDrawerBpp;
        m_packed.resize(rowBytes * height);
        for (int y = 0; y < height; ++y)
            memcpy(&m_packed[rowBytes * y], bgra + static_cast<size_t>(stride) * y, rowBytes);
        rows = &m_packed[0];
    }
    ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, NULL);
    ::StretchDIBits(hdc, dest.left, dest.top, dstW, dstH, 0, 0, width, height, rows, &bi, DIB_RGB_COLORS, SRCCOPY);
    m_lastPath = share_draw_gdi;
    return m_lastPath;
}

} // namespace screenshare
