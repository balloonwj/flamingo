/**
 *  标题栏图标位图生成实现，TitleBarIcon.cpp
 *  设计与约定见 TitleBarIcon.h。
 *  balloonwj@qq.com   2026-08-03
 */
#include "TitleBarIcon.h"
#include "../resource.h"   // IDR_MAINFRAME（程序主图标）

namespace balloonmain {

HBITMAP CreateProgramIconBitmap(int size, COLORREF bg)
{
    //边长非正会让 CreateCompatibleBitmap 失败，提前挡掉
    if (size <= 0)
    {
        return NULL;
    }

    //按目标边长直接加载对应尺寸的图标，避免先取大图再缩放导致发虚
    HICON hIcon = (HICON)::LoadImage(::GetModuleHandle(NULL),
                                     MAKEINTRESOURCE(IDR_MAINFRAME),
                                     IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (hIcon == NULL)
    {
        return NULL;
    }

    HDC hScreen = ::GetDC(NULL);
    if (hScreen == NULL)
    {
        ::DestroyIcon(hIcon);
        return NULL;
    }

    HDC     hMem = ::CreateCompatibleDC(hScreen);
    HBITMAP hBmp = ::CreateCompatibleBitmap(hScreen, size, size);
    if (hMem == NULL || hBmp == NULL)
    {
        //任一步失败都要把已拿到的资源还回去，别漏
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        if (hMem != NULL)
        {
            ::DeleteDC(hMem);
        }
        ::ReleaseDC(NULL, hScreen);
        ::DestroyIcon(hIcon);
        return NULL;
    }

    HBITMAP hOld = (HBITMAP)::SelectObject(hMem, hBmp);

    //先铺满底色，再把图标画上去——图标的透明区因此显示为底色，
    //而不是标题栏 SRCCOPY 搬运后的黑块
    RECT   rc = { 0, 0, size, size };
    HBRUSH br = ::CreateSolidBrush(bg);
    if (br != NULL)
    {
        ::FillRect(hMem, &rc, br);
        ::DeleteObject(br);
    }

    ::DrawIconEx(hMem, 0, 0, hIcon, size, size, 0, NULL, DI_NORMAL);

    ::SelectObject(hMem, hOld);
    ::DeleteDC(hMem);
    ::ReleaseDC(NULL, hScreen);
    ::DestroyIcon(hIcon);
    return hBmp;
}

} // namespace balloonmain
