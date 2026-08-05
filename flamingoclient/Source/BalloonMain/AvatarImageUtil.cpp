/**
 *  AvatarImageUtil.cpp —— 本地上传头像图片处理实现（GDI+）。
 *
 *  ProcessAvatarSourceFile：加载源图 → 居中裁方 → 缩放到目标边长 → 存 PNG。
 *  MakeRoundedAvatarFromFile：加载源图 → 居中裁方 → 缩放 + 圆角 → 32bpp 预乘
 *  alpha 圆角方形 HBITMAP（角外透明，配 AlphaBlend 渲染，与 monogram 位图同款）。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"
#include "AvatarImageUtil.h"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace balloonmain {

namespace {

// GDI+ 懒初始化（与 MainPanelAvatar.cpp 同款模式；本 TU 留自己的 token）。
ULONG_PTR& GdipToken()
{
    static ULONG_PTR s_token = 0;
    return s_token;
}

void EnsureGdiplus()
{
    if (GdipToken() != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&GdipToken(), &input, nullptr);
}

// 取 PNG 编码器 CLSID。失败返回 false。
bool GetPngEncoderClsid(CLSID& clsid)
{
    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0)
    {
        return false;
    }
    Gdiplus::ImageCodecInfo* pInfo = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (pInfo == NULL)
    {
        return false;
    }
    bool found = false;
    if (Gdiplus::GetImageEncoders(num, size, pInfo) == Gdiplus::Ok)
    {
        for (UINT i = 0; i < num; ++i)
        {
            if (::lstrcmpiW(pInfo[i].MimeType, L"image/png") == 0)
            {
                clsid = pInfo[i].Clsid;
                found = true;
                break;
            }
        }
    }
    free(pInfo);
    return found;
}

// 往 path 追加一个圆角矩形子路径（与 MainPanelAvatar.cpp 同款）。
void AddRoundRectPath(Gdiplus::GraphicsPath& path,
                      Gdiplus::REAL x, Gdiplus::REAL y,
                      Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r)
{
    if (r <= 0.0f)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    path.AddArc(x,         y + h - d, d, d,  90.0f, 90.0f);
    path.CloseFigure();
}

} // anonymous namespace

bool ProcessAvatarSourceFile(LPCWSTR srcPath, int targetPx, LPCWSTR outPath)
{
    if (srcPath == NULL || outPath == NULL || targetPx <= 0)
    {
        return false;
    }

    EnsureGdiplus();

    Gdiplus::Bitmap src(srcPath);
    if (src.GetLastStatus() != Gdiplus::Ok)
    {
        return false;
    }

    int srcW = (int)src.GetWidth();
    int srcH = (int)src.GetHeight();
    int cropX = 0;
    int cropY = 0;
    int side = 0;
    if (!ComputeCenterSquareCrop(srcW, srcH, cropX, cropY, side))
    {
        return false;
    }

    // 目标 32bpp ARGB 位图：居中裁方区域高质量缩放到 targetPx。
    Gdiplus::Bitmap dst(targetPx, targetPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&dst);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::Rect destRc(0, 0, targetPx, targetPx);
        g.DrawImage(&src, destRc, cropX, cropY, side, side, Gdiplus::UnitPixel);
    }

    CLSID pngClsid;
    if (!GetPngEncoderClsid(pngClsid))
    {
        return false;
    }
    return dst.Save(outPath, &pngClsid, NULL) == Gdiplus::Ok;
}

HBITMAP MakeRoundedAvatarFromFile(LPCWSTR path, int sizePx, int radiusPx)
{
    if (path == NULL || sizePx <= 0)
    {
        return NULL;
    }

    EnsureGdiplus();

    Gdiplus::Bitmap src(path);
    if (src.GetLastStatus() != Gdiplus::Ok)
    {
        return NULL;
    }

    int srcW = (int)src.GetWidth();
    int srcH = (int)src.GetHeight();
    int cropX = 0;
    int cropY = 0;
    int side = 0;
    if (!ComputeCenterSquareCrop(srcW, srcH, cropX, cropY, side))
    {
        return NULL;
    }

    // 32bpp top-down 预乘 alpha DIBSection（与 MakeMonogramSquareAvatar 同款，
    // 配 AlphaBlend 渲染方向正确）。
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sizePx;
    bi.bmiHeader.biHeight      = -sizePx;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hBmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBmp == NULL || bits == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return NULL;
    }
    ::ZeroMemory(bits, (size_t)sizePx * sizePx * 4);

    int stride = sizePx * 4;
    Gdiplus::Bitmap canvas(sizePx, sizePx, stride,
                           PixelFormat32bppPARGB, static_cast<BYTE*>(bits));

    Gdiplus::Graphics g(&canvas);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::REAL r = (Gdiplus::REAL)radiusPx;
    Gdiplus::REAL maxR = (Gdiplus::REAL)sizePx * 0.5f;
    if (r > maxR)
    {
        r = maxR;
    }
    if (r < 0.0f)
    {
        r = 0.0f;
    }

    // 圆角方形裁剪：clip 到圆角路径后把裁方区域缩放铺满。角外保持透明。
    Gdiplus::GraphicsPath path2;
    AddRoundRectPath(path2, 0.5f, 0.5f,
                     (Gdiplus::REAL)sizePx - 1.0f, (Gdiplus::REAL)sizePx - 1.0f, r);
    g.SetClip(&path2);
    Gdiplus::Rect destRc(0, 0, sizePx, sizePx);
    g.DrawImage(&src, destRc, cropX, cropY, side, side, Gdiplus::UnitPixel);
    g.ResetClip();

    return hBmp;
}

} // namespace balloonmain
