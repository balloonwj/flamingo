/**
 *  ClipboardImage.h
 *
 *  极小工具：把一个本地图片文件放进系统剪贴板（CF_BITMAP），使用户能直接
 *  Ctrl+V 粘到聊天输入框、Word、画图等任何接受位图的地方。与同目录的
 *  ClipboardText.h 并列 —— 那个管文本，这个管图片。
 *
 *  典型用法（消息记录窗行右键「复制图片」）：
 *      if (!flamingo::CopyImageFileToClipboard(imagePath))
 *      {
 *          // 文件不存在 / 解码失败 / 剪贴板被别的进程占着
 *      }
 *
 *  ------------------------------------------------------------------
 *  为什么不直接复用截图浮层里那段
 *
 *  CScreenCaptureOverlay::DoCopyToClipboard 也做同一件事，但它是那个类的私有
 *  成员函数，而且源是<u>已经在内存里的 HBITMAP</u>（刚截下来的那块），本函数的源
 *  是磁盘上的一个文件路径，得先解码。故另起一个通用件，日后截图浮层若要归位，
 *  也有地方可归。
 *  ------------------------------------------------------------------
 *
 *  两点要留意：
 *    · 放进剪贴板的是 <u>CF_BITMAP（DDB）</u>，不带 alpha 通道。PNG 的透明区域会
 *      被合成到白底上 —— 这是 CF_BITMAP 的固有限制，不是缺陷；真要保透明得写
 *      CF_DIBV5 + PNG 两种格式，本轮不做（聊天里的图绝大多数是照片与截图，不透明）。
 *    · <u>调用前进程须已初始化 GDI+</u>（用 Gdiplus::Bitmap 解码，才能吃下 PNG /
 *      JPG / GIF / BMP 等各种格式）。客户端在启动时已初始化。
 *
 *  本文件只有一个 inline 函数、纯 header，无需加进工程文件。
 *  包含它之前调用方须已引入 <windows.h> 与 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <windows.h>
#include <gdiplus.h>

namespace flamingo {

/**
 *  把一个本地图片文件放进系统剪贴板（CF_BITMAP）。
 *
 *    imagePath：图片文件完整路径（UTF-16）。NULL 或空串直接返回 false。
 *               支持 GDI+ 认得的所有格式（PNG / JPG / BMP / GIF / TIFF）。
 *
 *  @return 成功 true；文件不存在 / 解码失败 / 打开剪贴板失败 / 位图创建失败返回 false。
 *
 *  副作用：成功时会<u>清空</u>剪贴板原有内容再写入。写入成功后位图所有权转交系统，
 *          本函数不再释放；失败路径上自己释放干净，不泄漏 GDI 对象。
 *          剪贴板归属当前任务（OpenClipboard(NULL)）。
 */
inline bool CopyImageFileToClipboard(LPCWSTR imagePath)
{
    if (imagePath == NULL || imagePath[0] == L'\0')
    {
        return false;
    }

    // 先解码。用 GDI+ 而不是 LoadImage —— 后者只认 BMP，聊天里的图多是 PNG / JPG。
    Gdiplus::Bitmap* pSrc = Gdiplus::Bitmap::FromFile(imagePath);
    if (pSrc == NULL)
    {
        return false;
    }
    if (pSrc->GetLastStatus() != Gdiplus::Ok)
    {
        delete pSrc;   // 解码失败也要释放探针对象
        return false;
    }

    const int srcW = (int)pSrc->GetWidth();
    const int srcH = (int)pSrc->GetHeight();
    if (srcW <= 0 || srcH <= 0)
    {
        delete pSrc;
        return false;
    }

    // 造一张与屏幕兼容的 DDB，把源图画上去。
    //
    // 必须"画"而不是"直接拿 GDI+ 的 HBITMAP"：GetHBITMAP 产出的是 32 位带 alpha 的
    // DIBSection，多数接收方（画图、Word 的旧路径）对剪贴板里的 alpha 处理并不一致，
    // 会出现透明区域变黑。先合成到白底再交出去，各家看到的结果才一致。
    const COLORREF kCompositeBg = RGB(0xFF, 0xFF, 0xFF);   // 透明区域合成到的底色

    HDC hdcScreen = ::GetDC(NULL);
    if (hdcScreen == NULL)
    {
        delete pSrc;
        return false;
    }

    HBITMAP hBmp = ::CreateCompatibleBitmap(hdcScreen, srcW, srcH);
    HDC     hdcMem = ::CreateCompatibleDC(hdcScreen);
    bool    painted = false;
    if (hBmp != NULL && hdcMem != NULL)
    {
        HBITMAP hOldBmp = (HBITMAP)::SelectObject(hdcMem, hBmp);

        // 先铺白底，再把图叠上去 —— 顺序反了透明区域就是随机的未初始化像素。
        RECT rcAll;
        rcAll.left = 0;
        rcAll.top = 0;
        rcAll.right = srcW;
        rcAll.bottom = srcH;
        HBRUSH hBrush = ::CreateSolidBrush(kCompositeBg);
        if (hBrush != NULL)
        {
            ::FillRect(hdcMem, &rcAll, hBrush);
            ::DeleteObject(hBrush);
        }

        Gdiplus::Graphics g(hdcMem);
        // 原尺寸 1:1 绘制，不缩放，故不必设插值模式。
        painted = (g.DrawImage(pSrc, 0, 0, srcW, srcH) == Gdiplus::Ok);

        ::SelectObject(hdcMem, hOldBmp);
    }

    if (hdcMem != NULL)
    {
        ::DeleteDC(hdcMem);
    }
    ::ReleaseDC(NULL, hdcScreen);
    delete pSrc;

    if (!painted || hBmp == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return false;
    }

    if (!::OpenClipboard(NULL))
    {
        ::DeleteObject(hBmp);   // 剪贴板打不开（被别的进程占着），自行回收
        return false;
    }

    ::EmptyClipboard();
    const bool ok = (::SetClipboardData(CF_BITMAP, hBmp) != NULL);
    if (!ok)
    {
        ::DeleteObject(hBmp);   // 写入失败，所有权仍在我们手上
    }
    ::CloseClipboard();
    return ok;
}

} // namespace flamingo
