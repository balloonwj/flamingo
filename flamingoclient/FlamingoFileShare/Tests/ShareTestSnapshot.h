/**
 *  文件共享测试用的窗口截图工具（只给测试用），ShareTestSnapshot.h
 *
 *  把一个窗口的画面存成 PNG，供人工核对界面（截图放在仓库根的 snapshots 目录）。原先写在 ShareTransferLiveTests.cpp 里，
 *  第 15 步发起共享对话框的截图用例也要用，移到这里共用。
 *
 *  典型用法：
 *      PumpMessagesFor(kPaintWaitMs);
 *      CHECK(sharesnapshot::SaveWindowPng(window.m_hWnd, sharetestfs::ExeDir() + kSnapshotPath));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <string.h>
#include <string>
#include <vector>

namespace sharesnapshot {

//PrintWindow 的 PW_RENDERFULLCONTENT（本工程 _WIN32_WINNT 较低，头文件里没有这个宏）
const UINT kPrintFullContent = 0x00000002;

/**
 *  在当前线程上处理消息一段时间（让窗口完成布局与绘制）。
 *  @param ms 时长，单位：毫秒
 */
inline void PumpMessagesFor(DWORD ms)
{
    const ULONGLONG end = ::GetTickCount64() + ms;
    while (::GetTickCount64() < end)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(1);
    }
}

/**
 *  把一张 GDI 位图存成 PNG（GDI+ 在本函数内启动与关闭）。
 *  @param bmp  位图（借用，不释放）
 *  @param path 保存路径
 *  @return 保存成功返回 true
 */
inline bool SaveBitmapPng(HBITMAP bmp, const std::wstring& path)
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    bool ok = false;
    if (Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok)
    {
        UINT num = 0;
        UINT size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        std::vector<BYTE> codecBuf(size);
        Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(codecBuf.data());
        Gdiplus::GetImageEncoders(num, size, codecs);
        for (UINT i = 0; i < num; ++i)
        {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
            {
                Gdiplus::Bitmap image(bmp, NULL);
                ok = (image.Save(path.c_str(), &codecs[i].Clsid, NULL) == Gdiplus::Ok);
                break;
            }
        }
        Gdiplus::GdiplusShutdown(token);
    }
    return ok;
}

/**
 *  把窗口画面存成 PNG。
 *  @param hWnd 窗口
 *  @param path 保存路径
 *  @return 保存成功返回 true
 */
inline bool SaveWindowPng(HWND hWnd, const std::wstring& path)
{
    RECT rc;
    ::GetWindowRect(hWnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    ::PrintWindow(hWnd, mem, kPrintFullContent);
    ::SelectObject(mem, old);
    const bool ok = SaveBitmapPng(bmp, path);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return ok;
}

/**
 *  画一张纯色的正方形图片存成 PNG（头像照片等用例的测试图片）。
 *  @param path   保存路径
 *  @param sizePx 边长，单位：像素
 *  @param color  颜色
 *  @return 保存成功返回 true
 */
inline bool SaveSolidColorPng(const std::wstring& path, int sizePx, COLORREF color)
{
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, sizePx, sizePx);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    HBRUSH brush = ::CreateSolidBrush(color);
    RECT rc = { 0, 0, sizePx, sizePx };
    ::FillRect(mem, &rc, brush);
    ::DeleteObject(brush);
    ::SelectObject(mem, old);
    const bool ok = SaveBitmapPng(bmp, path);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return ok;
}

/**
 *  取一张位图里某个像素的颜色（用来检查头像位图画的是照片还是字形）。
 *  @param bmp 位图（借用）
 *  @param x   横坐标，单位：像素
 *  @param y   纵坐标，单位：像素
 *  @return 该点的颜色；坐标在位图之外时返回 CLR_INVALID
 */
inline COLORREF BitmapPixel(HBITMAP bmp, int x, int y)
{
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    const COLORREF color = ::GetPixel(mem, x, y);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return color;
}

/**
 *  把窗口画到内存位图里，取其中一个像素的颜色，用来检查某处画成了什么颜色。
 *  @param hWnd 窗口
 *  @param x    横坐标，单位：像素，相对窗口左上角（DuiFrameWindow 的客户区就是整个窗口，与控件矩形同一坐标系）
 *  @param y    纵坐标，同上
 *  @return 该点的颜色；坐标在窗口之外时返回 CLR_INVALID
 */
inline COLORREF WindowPixel(HWND hWnd, int x, int y)
{
    RECT rc;
    ::GetWindowRect(hWnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    ::PrintWindow(hWnd, mem, kPrintFullContent);
    const COLORREF color = ::GetPixel(mem, x, y);
    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return color;
}

} // namespace sharesnapshot
