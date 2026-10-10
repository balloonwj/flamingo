/**
 *  用 GDI+ 缩放 BGRA 图像，说明见 OcrBitmapScaler.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrBitmapScaler.h"

#include <windows.h>
#include <gdiplus.h>
#include <string.h>

#include "OcrScalePolicy.h"

namespace flamingoocr {

namespace {

//每像素字节数（32 位 BGRA）
const uint32_t kBytesPerPixel = 4;

//GDI+ 的启动状态：0 未尝试，1 成功，2 失败。只在第一次 EnsureOcrGdiplus 时改写
volatile LONG g_gdiplusState = 0;

//GdiplusStartup 返回的令牌；进程内不关闭
ULONG_PTR g_gdiplusToken = 0;

//保护第一次启动的锁
SRWLOCK g_gdiplusLock = SRWLOCK_INIT;

//GDI+ 启动状态的取值
const LONG kGdiplusNotTried = 0;
const LONG kGdiplusReady = 1;
const LONG kGdiplusFailed = 2;

} // namespace

bool EnsureOcrGdiplus()
{
    if (g_gdiplusState != kGdiplusNotTried)
        return g_gdiplusState == kGdiplusReady;

    ::AcquireSRWLockExclusive(&g_gdiplusLock);
    if (g_gdiplusState == kGdiplusNotTried)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::Status status = Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, NULL);
        ::InterlockedExchange(&g_gdiplusState, (status == Gdiplus::Ok) ? kGdiplusReady : kGdiplusFailed);
    }
    ::ReleaseSRWLockExclusive(&g_gdiplusLock);
    return g_gdiplusState == kGdiplusReady;
}

bool ScaleBgra(const uint8_t* src, uint32_t width, uint32_t height, uint32_t stride,
               uint32_t dstWidth, uint32_t dstHeight, std::vector<uint8_t>& dst)
{
    dst.clear();
    if (src == NULL || width == 0 || height == 0 || dstWidth == 0 || dstHeight == 0)
        return false;
    if (!EnsureOcrGdiplus())
        return false;

    //GDI+ 直接引用调用方的像素，不复制；构造函数不修改数据，const_cast 只是为了满足参数类型
    Gdiplus::Bitmap source(static_cast<INT>(width), static_cast<INT>(height), static_cast<INT>(stride),
                           PixelFormat32bppARGB, const_cast<BYTE*>(src));
    if (source.GetLastStatus() != Gdiplus::Ok)
        return false;

    Gdiplus::Bitmap target(static_cast<INT>(dstWidth), static_cast<INT>(dstHeight), PixelFormat32bppARGB);
    if (target.GetLastStatus() != Gdiplus::Ok)
        return false;

    {
        Gdiplus::Graphics g(&target);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        //边缘按镜像取样，避免插值时混入图像外的透明像素，使字贴边时边缘发灰
        Gdiplus::ImageAttributes attrs;
        attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        Gdiplus::Rect destRect(0, 0, static_cast<INT>(dstWidth), static_cast<INT>(dstHeight));
        Gdiplus::Status drawn = g.DrawImage(&source, destRect, 0, 0, static_cast<INT>(width), static_cast<INT>(height),
                                            Gdiplus::UnitPixel, &attrs);
        if (drawn != Gdiplus::Ok)
            return false;
    }

    Gdiplus::BitmapData data;
    Gdiplus::Rect lockRect(0, 0, static_cast<INT>(dstWidth), static_cast<INT>(dstHeight));
    if (target.LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        return false;

    const size_t rowBytes = static_cast<size_t>(dstWidth) * kBytesPerPixel;
    dst.resize(rowBytes * dstHeight);
    for (uint32_t y = 0; y < dstHeight; ++y)
    {
        const uint8_t* line = static_cast<const uint8_t*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        memcpy(&dst[y * rowBytes], line, rowBytes);
    }
    target.UnlockBits(&data);

    FlattenOnWhite(&dst[0], dstWidth, dstHeight, static_cast<uint32_t>(rowBytes));
    return true;
}

} // namespace flamingoocr
