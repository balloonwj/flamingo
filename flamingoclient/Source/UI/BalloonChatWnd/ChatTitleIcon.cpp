/**
 *  ChatTitleIcon.cpp —— 聊天窗标题栏图标共用 helper 实现。
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "ChatTitleIcon.h"
#include "../BalloonMain/AvatarGlyph.h"   // DrawCenteredGlyphGdi：头像字居中

#include <vector>
#include <gdiplus.h>   // 真图头像标题栏图标：解码本地图片 + 裁圆绘制

namespace balloonchatwnd {

namespace {

// Rec.601 亮度权重，与 balloonmain::MakeGrayColor（AvatarGray.cpp）相同，标题栏的灰度头像与
// 通讯录、气泡里的灰度头像亮度一致。
const Gdiplus::REAL kLumaWeightRed   = 0.299f;
const Gdiplus::REAL kLumaWeightGreen = 0.587f;
const Gdiplus::REAL kLumaWeightBlue  = 0.114f;

// 圆形图标的圆往里收的距离（像素）：配合 PixelOffsetModeHalf，抗锯齿的边缘整圈落在位图之内。
const Gdiplus::REAL kCircleInset = 0.5f;

// GDI+ 颜色矩阵的阶数（R、G、B、A 四个分量加一行平移）。
const int kColorMatrixOrder = 5;

// 颜色矩阵里 R、G、B、A 与平移各自所在的行 / 列下标。
const int kMatrixRed   = 0;
const int kMatrixGreen = 1;
const int kMatrixBlue  = 2;
const int kMatrixAlpha = 3;
const int kMatrixShift = 4;

// 填一个把图像画成灰度的颜色矩阵：输出的 R、G、B 三个分量都等于输入按亮度权重的加权和，
// 透明度原样保留。GDI+ 的颜色矩阵按「行向量 × 矩阵」相乘，故第 i 行是输入分量 i 对各输出分量的贡献。
void FillGrayColorMatrix(Gdiplus::ColorMatrix& m)
{
    for (int r = 0; r < kColorMatrixOrder; ++r)
    {
        for (int c = 0; c < kColorMatrixOrder; ++c)
        {
            m.m[r][c] = 0.0f;
        }
    }
    const int outChannels[3] = { kMatrixRed, kMatrixGreen, kMatrixBlue };
    for (int i = 0; i < 3; ++i)
    {
        m.m[kMatrixRed][outChannels[i]]   = kLumaWeightRed;
        m.m[kMatrixGreen][outChannels[i]] = kLumaWeightGreen;
        m.m[kMatrixBlue][outChannels[i]]  = kLumaWeightBlue;
    }
    m.m[kMatrixAlpha][kMatrixAlpha] = 1.0f;
    m.m[kMatrixShift][kMatrixShift] = 1.0f;
}

} // anonymous namespace

HICON IconFromBitmap(HBITMAP hbmColor)
{
    if (hbmColor == NULL)
    {
        return NULL;
    }
    BITMAP bm = { 0 };
    if (::GetObject(hbmColor, sizeof(bm), &bm) == 0)
    {
        return NULL;
    }
    // 1bpp AND 掩码，全 0（不透明）；32bpp 彩色位图自带 alpha，显示靠 alpha。
    int rowBytes = ((bm.bmWidth + 15) / 16) * 2;   // 1bpp 行按 WORD 对齐
    std::vector<BYTE> maskBits((size_t)rowBytes * bm.bmHeight, 0);
    HBITMAP hbmMask = ::CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, maskBits.data());
    if (hbmMask == NULL)
    {
        return NULL;
    }
    ICONINFO ii = { 0 };
    ii.fIcon    = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask  = hbmMask;
    HICON hIcon = ::CreateIconIndirect(&ii);
    ::DeleteObject(hbmMask);
    return hIcon;
}

HBITMAP MakeRoundTitleIcon(LPCTSTR glyph, COLORREF avatarColor,
                           COLORREF barBg, int sizePx)
{
    if (sizePx <= 0)
    {
        return NULL;
    }
    // 用屏幕兼容的 DDB（无独立 alpha 通道）：StretchBlt SRCCOPY 只拷 RGB，
    // DDB 不会像 32bpp DIBSection 那样残留 alpha=0 的"黑角"。
    HDC hdcScreen = ::GetDC(NULL);
    HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, sizePx, sizePx);
    HDC hdc = ::CreateCompatibleDC(hdcScreen);
    ::ReleaseDC(NULL, hdcScreen);
    if (hbm == NULL || hdc == NULL)
    {
        if (hbm != NULL)
        {
            ::DeleteObject(hbm);
        }
        if (hdc != NULL)
        {
            ::DeleteDC(hdc);
        }
        return NULL;
    }
    HBITMAP oldBmp = (HBITMAP)::SelectObject(hdc, hbm);

    // 整块先填标题栏底色（圆外区域），DDB 无 alpha 通道，不会出现"黑角"。
    RECT rc = { 0, 0, sizePx, sizePx };
    HBRUSH bgBrush = ::CreateSolidBrush(barBg);
    ::FillRect(hdc, &rc, bgBrush);
    ::DeleteObject(bgBrush);

    // 实心圆（头像底色），用 GDI+ 抗锯齿填充，圆外是上面填好的标题栏底色。原先用 GDI 的 Ellipse，
    // 没有抗锯齿，圆周是台阶状的锯齿（2026-10-03 实测）。PixelOffsetModeHalf 加上往里收 0.5 像素，
    // 让圆与位图的像素格对齐、整圆落在位图内。GDI+ 对象放在作用域内，析构后再用 GDI 画字
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::SolidBrush avBrush(Gdiplus::Color(255, GetRValue(avatarColor), GetGValue(avatarColor),
                                                   GetBValue(avatarColor)));
        g.FillEllipse(&avBrush, kCircleInset, kCircleInset,
                      (Gdiplus::REAL)sizePx - 2.0f * kCircleInset, (Gdiplus::REAL)sizePx - 2.0f * kCircleInset);
    }

    // 首字：按头像底色亮度取白字或深字。
    int lum = (GetRValue(avatarColor) * 299 + GetGValue(avatarColor) * 587
               + GetBValue(avatarColor) * 114) / 1000;
    COLORREF txtColor = (lum > 150) ? RGB(0x4A, 0x4A, 0x4A) : RGB(0xFF, 0xFF, 0xFF);
    // 字号为边长的一半、加粗；按字形笔画外框居中（见 AvatarGlyph.h），DrawText 按单元格居中会让字偏低
    balloonmain::DrawCenteredGlyphGdi(hdc, (LPCWSTR)glyph, rc, sizePx / 2, true, txtColor);

    ::SelectObject(hdc, oldBmp);
    ::DeleteDC(hdc);
    return hbm;
}

HBITMAP MakeRoundTitleIconFromImage(LPCWSTR imagePath, COLORREF barBg, int sizePx, bool gray)
{
    if (imagePath == NULL || imagePath[0] == L'\0' || sizePx <= 0)
    {
        return NULL;
    }

    // 先解码本地图片；解码失败直接返回 NULL，让调用方回退到字形图标。
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(imagePath, FALSE);
    if (bmp == NULL || bmp->GetLastStatus() != Gdiplus::Ok)
    {
        delete bmp;
        return NULL;
    }

    // 与 MakeRoundTitleIcon 同样用屏幕兼容 DDB（无 alpha 通道，StretchBlt 不留黑角）。
    HDC hdcScreen = ::GetDC(NULL);
    HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, sizePx, sizePx);
    HDC hdc = ::CreateCompatibleDC(hdcScreen);
    ::ReleaseDC(NULL, hdcScreen);
    if (hbm == NULL || hdc == NULL)
    {
        if (hbm != NULL)
        {
            ::DeleteObject(hbm);
        }
        if (hdc != NULL)
        {
            ::DeleteDC(hdc);
        }
        delete bmp;
        return NULL;
    }
    HBITMAP oldBmp = (HBITMAP)::SelectObject(hdc, hbm);

    // 整块先填标题栏底色（圆外区域）。
    RECT rc = { 0, 0, sizePx, sizePx };
    HBRUSH bgBrush = ::CreateSolidBrush(barBg);
    ::FillRect(hdc, &rc, bgBrush);
    ::DeleteObject(bgBrush);

    // 先把真图高质量缩放到图标尺寸、画进一张中间位图，再拿它作纹理画刷按抗锯齿填充圆形；圆外仍是
    // 前面填好的标题栏底色。不用「SetClip 到圆形再 DrawImage」：GDI+ 的裁剪区按整像素划分，抗锯齿
    // 设置对它不起作用，圆周是台阶状的锯齿（2026-10-03 实测）。
    {
        Gdiplus::Bitmap scaled(sizePx, sizePx, PixelFormat32bppPARGB);
        {
            Gdiplus::Graphics gs(&scaled);
            gs.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gs.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            // 镜像平铺取样：插值取到图片边界之外时用边缘像素的镜像，而不是透明，图片四边不会变淡
            Gdiplus::ImageAttributes attrs;
            attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
            Gdiplus::ColorMatrix cm;
            if (gray)
            {
                // 灰度只作用于图片本身；圆外是标题栏底色，不经过颜色矩阵，保持原色
                FillGrayColorMatrix(cm);
                attrs.SetColorMatrix(&cm);
            }
            gs.DrawImage(bmp, Gdiplus::Rect(0, 0, sizePx, sizePx),
                         0, 0, (INT)bmp->GetWidth(), (INT)bmp->GetHeight(),
                         Gdiplus::UnitPixel, &attrs);
        }

        // 纹理画刷不做变换：中间位图与图标同尺寸、左上角对齐，两边都用 PixelOffsetModeHalf，像素一一对应
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::TextureBrush brush(&scaled);
        g.FillEllipse(&brush, kCircleInset, kCircleInset,
                      (Gdiplus::REAL)sizePx - 2.0f * kCircleInset, (Gdiplus::REAL)sizePx - 2.0f * kCircleInset);
    }

    delete bmp;
    ::SelectObject(hdc, oldBmp);
    ::DeleteDC(hdc);
    return hbm;
}

} // namespace balloonchatwnd
