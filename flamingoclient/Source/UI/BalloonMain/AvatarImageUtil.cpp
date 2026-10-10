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
    // 圆角路径四边各往里收 0.5 像素，宽高是 sizePx - 1，半径上限按它的一半算。按 sizePx 的一半算时，
    // 画圆形头像的四段圆弧会彼此错开半个像素、路径自相交，形状左右上下不对称（2026-10-03 实测）
    Gdiplus::REAL maxR = ((Gdiplus::REAL)sizePx - 1.0f) * 0.5f;
    if (r > maxR)
    {
        r = maxR;
    }
    if (r < 0.0f)
    {
        r = 0.0f;
    }

    // 圆角方形：先把裁方区域缩放到目标尺寸画进一张中间位图，再拿它作纹理画刷填充圆角路径，
    // 角外保持透明。不用「SetClip 到圆角路径再 DrawImage」：GDI+ 的裁剪区按整像素划分，抗锯齿
    // 设置对它不起作用，切出来的边缘只有完全不透明与完全透明两种像素，是台阶状的锯齿
    // （2026-10-03 实测）。FillPath 遵守抗锯齿设置，边缘像素按覆盖比例得到半透明值。
    Gdiplus::Bitmap scaled(sizePx, sizePx, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics gs(&scaled);
        gs.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        gs.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::Rect destRc(0, 0, sizePx, sizePx);
        gs.DrawImage(&src, destRc, cropX, cropY, side, side, Gdiplus::UnitPixel);
    }

    // 纹理画刷不做任何变换：中间位图与目标位图同尺寸、左上角对齐，像素一一对应。两边都用
    // PixelOffsetModeHalf，纹理不会出现半像素的错位。
    Gdiplus::TextureBrush brush(&scaled);
    Gdiplus::GraphicsPath path2;
    AddRoundRectPath(path2, 0.5f, 0.5f,
                     (Gdiplus::REAL)sizePx - 1.0f, (Gdiplus::REAL)sizePx - 1.0f, r);
    g.FillPath(&brush, &path2);

    return hBmp;
}

} // namespace balloonmain
