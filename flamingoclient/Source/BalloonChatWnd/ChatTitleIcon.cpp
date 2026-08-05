/**
 *  ChatTitleIcon.cpp —— 聊天窗标题栏图标共用 helper 实现。
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "ChatTitleIcon.h"

#include <vector>
#include <gdiplus.h>   // 真图头像标题栏图标：解码本地图片 + 裁圆绘制

namespace balloonchatwnd {

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

    // 实心圆（头像底色）；用同色画笔，避免 GDI 默认黑笔在圆周描出黑边。
    HBRUSH avBrush = ::CreateSolidBrush(avatarColor);
    HPEN   avPen   = ::CreatePen(PS_SOLID, 1, avatarColor);
    HBRUSH oldBr   = (HBRUSH)::SelectObject(hdc, avBrush);
    HPEN   oldPen  = (HPEN)::SelectObject(hdc, avPen);
    ::Ellipse(hdc, 0, 0, sizePx, sizePx);
    ::SelectObject(hdc, oldBr);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(avBrush);
    ::DeleteObject(avPen);

    // 首字：按头像底色亮度取白字或深字。
    int lum = (GetRValue(avatarColor) * 299 + GetGValue(avatarColor) * 587
               + GetBValue(avatarColor) * 114) / 1000;
    COLORREF txtColor = (lum > 150) ? RGB(0x4A, 0x4A, 0x4A) : RGB(0xFF, 0xFF, 0xFF);
    HFONT hFont = ::CreateFontW(
        -(int)(sizePx * 0.5), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    HFONT oldFont = (HFONT)::SelectObject(hdc, hFont);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, txtColor);
    ::DrawTextW(hdc, (LPCWSTR)glyph, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    ::SelectObject(hdc, oldFont);
    ::DeleteObject(hFont);

    ::SelectObject(hdc, oldBmp);
    ::DeleteDC(hdc);
    return hbm;
}

HBITMAP MakeRoundTitleIconFromImage(LPCWSTR imagePath, COLORREF barBg, int sizePx)
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

    // 把真图裁成圆形、高质量缩放绘制到整个图标区域。
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        Gdiplus::GraphicsPath circle;
        circle.AddEllipse(0, 0, sizePx, sizePx);
        g.SetClip(&circle);
        g.DrawImage(bmp, Gdiplus::Rect(0, 0, sizePx, sizePx));
        g.ResetClip();
    }

    delete bmp;
    ::SelectObject(hdc, oldBmp);
    ::DeleteDC(hdc);
    return hbm;
}

} // namespace balloonchatwnd
