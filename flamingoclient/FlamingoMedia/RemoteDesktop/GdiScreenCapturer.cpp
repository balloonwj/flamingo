/**
 *  远程桌面 GDI 抓取器（被控端，降级后端）实现，GdiScreenCapturer.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "GdiScreenCapturer.h"

namespace remotedesktop {

CGdiScreenCapturer::CGdiScreenCapturer()
    : m_deskDC(NULL)
    , m_memDC(NULL)
    , m_dib(NULL)
    , m_oldBmp(NULL)
    , m_bits(NULL)
    , m_width(0)
    , m_height(0)
{
}

CGdiScreenCapturer::~CGdiScreenCapturer()
{
    cleanup();
}

void CGdiScreenCapturer::cleanup()
{
    if (m_memDC != NULL)
    {
        if (m_oldBmp != NULL)
        {
            ::SelectObject(m_memDC, m_oldBmp);
            m_oldBmp = NULL;
        }
        ::DeleteDC(m_memDC);
        m_memDC = NULL;
    }
    if (m_dib != NULL)
    {
        ::DeleteObject(m_dib);
        m_dib = NULL;
    }
    if (m_deskDC != NULL)
    {
        ::ReleaseDC(NULL, m_deskDC);
        m_deskDC = NULL;
    }
    m_bits = NULL;
}

bool CGdiScreenCapturer::Init()
{
    m_deskDC = ::GetDC(NULL);
    if (m_deskDC == NULL)
        return false;

    //主屏尺寸（在 PMv2 线程下为物理像素，与 DXGI 一致；调用方推流线程已切 PMv2）
    m_width = ::GetSystemMetrics(SM_CXSCREEN);
    m_height = ::GetSystemMetrics(SM_CYSCREEN);
    if (m_width <= 0 || m_height <= 0)
    {
        cleanup();
        return false;
    }

    m_memDC = ::CreateCompatibleDC(m_deskDC);
    if (m_memDC == NULL)
    {
        cleanup();
        return false;
    }

    //32 位 top-down DIBSection（biHeight 取负），产出 BGRA32 与 DXGI 后端同格式
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = m_width;
    bi.bmiHeader.biHeight      = -m_height;   //top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    m_dib = ::CreateDIBSection(m_deskDC, &bi, DIB_RGB_COLORS, &m_bits, NULL, 0);
    if (m_dib == NULL || m_bits == NULL)
    {
        cleanup();
        return false;
    }

    m_oldBmp = (HBITMAP)::SelectObject(m_memDC, m_dib);
    return true;
}

CaptureResult CGdiScreenCapturer::CaptureFrame(UINT /*timeoutMs*/, FrameView& out)
{
    if (m_memDC == NULL || m_deskDC == NULL || m_bits == NULL)
        return kCaptureFatal;

    //SRCCOPY（不带 CAPTUREBLT）：与旧引擎一致，画面不含光标
    if (!::BitBlt(m_memDC, 0, 0, m_width, m_height, m_deskDC, 0, 0, SRCCOPY))
        return kCaptureFatal;

    out.m_data   = (const BYTE*)m_bits;
    out.m_stride = m_width * 4;   //32 位无行尾 padding
    out.m_width  = m_width;
    out.m_height = m_height;
    return kCaptureOk;
}

int CGdiScreenCapturer::ScreenWidth() const
{
    return m_width;
}

int CGdiScreenCapturer::ScreenHeight() const
{
    return m_height;
}

} // namespace remotedesktop
