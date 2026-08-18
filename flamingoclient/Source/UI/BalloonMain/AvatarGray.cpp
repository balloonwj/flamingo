/**
 *  AvatarGray.cpp
 *
 *  头像置灰的实现。MakeGrayColor 是纯计算，可被 FlamingoUnit 直接断言；
 *  MakeGrayAvatarBitmap 要建 DIBSection、读源位图像素，只能靠运行期目视检查。
 *  两者共用同一组亮度系数，保证"字形头像的底色"和"真图头像的像素"灰得一样深。
 *
 *  本文件不 include stdafx.h，与 FlamingoUnit 测试工程逐字共用；在 Flamingo.vcxproj
 *  里须标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "AvatarGray.h"

namespace balloonmain {

namespace {

// Rec.601 亮度权重放大 1024 倍后取整得到的定点系数：0.299 / 0.587 / 0.114 分别对应
// 306 / 601 / 117。与 CImageEx::GrayScale（ImageEx.cpp:184）用的是同一组系数，
// 这样新旧两套置灰路径灰出来的深浅一致。三个系数之和恰为 1024。
const int kLumaWeightRed   = 306;
const int kLumaWeightGreen = 601;
const int kLumaWeightBlue  = 117;

// 定点换算的移位位数（1 << 10 = 1024，即上面系数的放大倍数）与四舍五入偏移（取半个 1024）。
const int kLumaShiftBits  = 10;
const int kLumaRoundHalf  = 512;

// 32 位像素占的字节数，以及 B / G / R / A 四个分量在 DIB 像素里的字节下标。
// Windows 的 32bpp DIB 是 BGRA 顺序，不是 RGBA，下标写反会让红蓝互换。
const int kBytesPerPixel32 = 4;
const int kPixelOffsetBlue  = 0;
const int kPixelOffsetGreen = 1;
const int kPixelOffsetRed   = 2;

// 本模块只处理 32 位色深的位图，其它色深一律拒绝（见 MakeGrayAvatarBitmap 的判空返回）。
const int kSupportedBitsPerPixel = 32;

// 按 Rec.601 算一个 RGB 三元组的灰阶值。三个系数之和为 1024，故结果必落在 0~255，
// 不需要再夹取上下界。
BYTE ComputeLuma(BYTE red, BYTE green, BYTE blue)
{
    const int luma = ((int)red   * kLumaWeightRed
                    + (int)green * kLumaWeightGreen
                    + (int)blue  * kLumaWeightBlue
                    + kLumaRoundHalf) >> kLumaShiftBits;
    return (BYTE)luma;
}

} // anonymous namespace

COLORREF MakeGrayColor(COLORREF color)
{
    const BYTE gray = ComputeLuma(GetRValue(color), GetGValue(color), GetBValue(color));
    return RGB(gray, gray, gray);
}

HBITMAP MakeGrayAvatarBitmap(HBITMAP hbmSource)
{
    if (hbmSource == NULL)
    {
        return NULL;
    }

    BITMAP bmSource = {};
    if (::GetObject(hbmSource, sizeof(bmSource), &bmSource) == 0)
    {
        return NULL;
    }

    // 只处理 32bpp 且能直接拿到像素指针的 DIBSection。头像位图两个生成入口都满足
    // （AvatarImageUtil.cpp:167 的 CreateDIBSection），不满足时返回 NULL，由调用方
    // 退回画彩色原图 —— 宁可没灰掉，也不要画出一片空白。
    if (bmSource.bmWidth <= 0
        || bmSource.bmHeight <= 0
        || bmSource.bmBitsPixel != kSupportedBitsPerPixel
        || bmSource.bmBits == NULL)
    {
        return NULL;
    }

    // 目标位图与源位图同宽高、同为自上而下的 32bpp 预乘 alpha DIBSection。
    // biHeight 取负即"自上而下"，与源位图的建法保持一致，逐行拷贝才不会上下颠倒。
    BITMAPINFO biTarget = {};
    biTarget.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    biTarget.bmiHeader.biWidth       = bmSource.bmWidth;
    biTarget.bmiHeader.biHeight      = -bmSource.bmHeight;
    biTarget.bmiHeader.biPlanes      = 1;
    biTarget.bmiHeader.biBitCount    = kSupportedBitsPerPixel;
    biTarget.bmiHeader.biCompression = BI_RGB;

    void* pTargetBits = NULL;
    HBITMAP hbmGray = ::CreateDIBSection(NULL, &biTarget, DIB_RGB_COLORS, &pTargetBits, NULL, 0);
    if (hbmGray == NULL || pTargetBits == NULL)
    {
        if (hbmGray != NULL)
        {
            ::DeleteObject(hbmGray);
        }
        return NULL;
    }

    // 逐行搬运并就地灰化。源位图的行间距用 bmWidthBytes（DIBSection 会按 4 字节对齐），
    // 目标位图是我们刚建的、宽度 × 4 即行间距，两者不一定相等，所以要各用各的。
    const BYTE* pSourceRow = (const BYTE*)bmSource.bmBits;
    BYTE*       pTargetRow = (BYTE*)pTargetBits;
    const int   nSourceStride = bmSource.bmWidthBytes;
    const int   nTargetStride = bmSource.bmWidth * kBytesPerPixel32;

    for (int y = 0; y < bmSource.bmHeight; ++y)
    {
        for (int x = 0; x < bmSource.bmWidth; ++x)
        {
            const BYTE* pSrcPixel = pSourceRow + (size_t)x * kBytesPerPixel32;
            BYTE*       pDstPixel = pTargetRow + (size_t)x * kBytesPerPixel32;

            const BYTE gray = ComputeLuma(pSrcPixel[kPixelOffsetRed],
                                          pSrcPixel[kPixelOffsetGreen],
                                          pSrcPixel[kPixelOffsetBlue]);

            pDstPixel[kPixelOffsetBlue]  = gray;
            pDstPixel[kPixelOffsetGreen] = gray;
            pDstPixel[kPixelOffsetRed]   = gray;

            // 第 4 个字节是 alpha，原样搬过去 —— 圆角外的透明区域必须继续透明，
            // 否则头像会多出一圈黑角。
            pDstPixel[kBytesPerPixel32 - 1] = pSrcPixel[kBytesPerPixel32 - 1];
        }

        pSourceRow += nSourceStride;
        pTargetRow += nTargetStride;
    }

    return hbmGray;
}

} // namespace balloonmain
