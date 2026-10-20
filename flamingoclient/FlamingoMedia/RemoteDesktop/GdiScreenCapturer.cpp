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
    , m_lastBlitMs(0)
    , m_hasBlit(false)
{
}

namespace {

//默认的取主屏尺寸：在 PMv2 线程下为物理像素，与 DXGI 一致（调用方推流线程已切 PMv2）
SIZE defaultScreenSize()
{
    SIZE s;
    s.cx = ::GetSystemMetrics(SM_CXSCREEN);
    s.cy = ::GetSystemMetrics(SM_CYSCREEN);
    return s;
}

//当前使用的取尺寸函数；单元测试可替换
CGdiScreenCapturer::ScreenSizeProbe g_screenSizeProbe = NULL;

SIZE probeScreenSize()
{
    return (g_screenSizeProbe != NULL) ? g_screenSizeProbe() : defaultScreenSize();
}

//默认的拷屏：从桌面 DC BitBlt。SRCCOPY（不带 CAPTUREBLT）与旧引擎一致，画面不含光标
BOOL defaultScreenBlit(HDC memDC, HDC deskDC, int width, int height)
{
    return ::BitBlt(memDC, 0, 0, width, height, deskDC, 0, 0, SRCCOPY);
}

//当前使用的拷屏函数；单元测试可替换（2026-10-17）
CGdiScreenCapturer::ScreenBlit g_screenBlit = NULL;

BOOL screenBlit(HDC memDC, HDC deskDC, int width, int height)
{
    return (g_screenBlit != NULL) ? g_screenBlit(memDC, deskDC, width, height)
                                  : defaultScreenBlit(memDC, deskDC, width, height);
}

} // namespace

void CGdiScreenCapturer::SetScreenSizeProbeForTest(ScreenSizeProbe probe)
{
    g_screenSizeProbe = probe;
}

void CGdiScreenCapturer::SetScreenBlitForTest(ScreenBlit blit)
{
    g_screenBlit = blit;
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
    //尺寸可能变了：重建之后的第一次拷屏不必等待
    m_hasBlit = false;
    m_deskDC = ::GetDC(NULL);
    if (m_deskDC == NULL)
        return false;

    //主屏尺寸（在 PMv2 线程下为物理像素，与 DXGI 一致；调用方推流线程已切 PMv2）
    const SIZE screen = probeScreenSize();
    m_width = screen.cx;
    m_height = screen.cy;
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

CaptureResult CGdiScreenCapturer::CaptureFrame(UINT timeoutMs, FrameView& out)
{
    if (m_memDC == NULL || m_deskDC == NULL || m_bits == NULL)
        return kCaptureFatal;

    //主屏尺寸变了（分辨率变化）：按新尺寸重建 DIB，之后报告新尺寸
    const SIZE screen = probeScreenSize();
    if (screen.cx > 0 && screen.cy > 0 && (screen.cx != m_width || screen.cy != m_height))
    {
        cleanup();
        if (!Init())
            return kCaptureFatal;
    }

    //两次拷屏之间至少间隔等待时长（2026-10-17）：GDI 没有「画面变了」的通知，不限频时调用方每轮都立即拿到一帧，
    //H.264 推流循环会不停地拷屏。旧格式每 200 毫秒才抓一次，不受影响
    if (m_hasBlit && timeoutMs > 0)
    {
        const ULONGLONG sinceLast = ::GetTickCount64() - m_lastBlitMs;
        if (sinceLast < timeoutMs)
            ::Sleep(static_cast<DWORD>(timeoutMs - sinceLast));
    }

    if (!screenBlit(m_memDC, m_deskDC, m_width, m_height))
        return kCaptureFatal;
    //调用方读 DIB 像素之前先让 GDI 执行完排队中的绘制
    ::GdiFlush();
    m_lastBlitMs = ::GetTickCount64();
    m_hasBlit = true;

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
