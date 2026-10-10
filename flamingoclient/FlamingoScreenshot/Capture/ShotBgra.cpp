/**
 *  ShotBgra.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ShotBgra.h"

#include <string.h>

namespace flamingoshot {

namespace {

const size_t kBytesPerPixel = 4;     // 32 位 BGRA 每像素字节数
const WORD kBitsPerPixel = 32;       // GetDIBits 取 32 位 DIB
const size_t kAlphaIndex = 3;        // 每像素 4 字节里透明度所在的下标
const uint8_t kChannelMax = 255;     // 不透明

} // namespace

ShotBgraImage::ShotBgraImage()
    : m_width(0)
    , m_height(0)
{
}

bool ShotBitmapToBgra(HBITMAP bitmap, const RECT* crop, ShotBgraImage& out)
{
    out = ShotBgraImage();
    BITMAP info;
    if (bitmap == NULL || ::GetObject(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight == 0)
        return false;
    const LONG fullHeight = (info.bmHeight > 0) ? info.bmHeight : -info.bmHeight;

    RECT area = { 0, 0, info.bmWidth, fullHeight };
    if (crop != NULL)
    {
        RECT bounds = area;
        if (!::IntersectRect(&area, crop, &bounds))
            return false;
    }
    const uint32_t width = static_cast<uint32_t>(area.right - area.left);
    const uint32_t height = static_cast<uint32_t>(area.bottom - area.top);
    if (width == 0 || height == 0)
        return false;

    //整张位图按自上而下的 32 位 DIB 取出，再拷出选区
    std::vector<uint8_t> full(static_cast<size_t>(info.bmWidth) * fullHeight * kBytesPerPixel);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = info.bmWidth;
    bmi.bmiHeader.biHeight = -fullHeight;   //负值表示自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = kBitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = ::GetDC(NULL);
    const int lines = ::GetDIBits(screen, bitmap, 0, static_cast<UINT>(fullHeight), &full[0], &bmi, DIB_RGB_COLORS);
    ::ReleaseDC(NULL, screen);
    if (lines != fullHeight)
        return false;

    const size_t fullRow = static_cast<size_t>(info.bmWidth) * kBytesPerPixel;
    const size_t rowBytes = static_cast<size_t>(width) * kBytesPerPixel;
    out.m_pixels.resize(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y)
    {
        memcpy(&out.m_pixels[y * rowBytes], &full[(area.top + y) * fullRow + static_cast<size_t>(area.left) * kBytesPerPixel],
               rowBytes);
    }
    //GDI 位图的透明度字节没有意义，一律置为不透明
    for (size_t i = kAlphaIndex; i < out.m_pixels.size(); i += kBytesPerPixel)
        out.m_pixels[i] = kChannelMax;
    out.m_width = width;
    out.m_height = height;
    return true;
}

} // namespace flamingoshot
