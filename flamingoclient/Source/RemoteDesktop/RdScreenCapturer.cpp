/**
 *  远程桌面抓屏封装（被控端）实现，RdScreenCapturer.cpp
 *  balloonwj@qq.com   2026-06-08
 *  2026-06-11 抓取改为 IScreenCapturer（DXGI 主 + GDI 降级）+ 独立编码器
 */
#include "RdScreenCapturer.h"
#include "DxgiScreenCapturer.h"
#include "GdiScreenCapturer.h"
#include "RdScreenDecoder.h"      // DebugPipelineToBmp 闭环解码
#include "../IULog.h"            // LOG_INFO / LOG_ERROR
#include <stdio.h>

namespace remotedesktop {

//单次抓帧的等待超时（毫秒）：屏幕无变化时 DXGI AcquireNextFrame 阻塞至多这么久。
static const UINT kCaptureTimeoutMs = 120;
//首帧抓取的最多重试次数：屏幕可能短暂静止，需多等几轮以确保拿到首屏。
static const int kFirstFrameTries = 30;

RdScreenCapturer::RdScreenCapturer(int /*bitCount*/, bool preferGdi)
    : m_capturer(NULL)
{
    //非强制 GDI 时优先 DXGI Desktop Duplication；初始化失败（老显卡 / 虚拟机 / RDP 等不支持）降级 GDI
    if (!preferGdi)
    {
        CDxgiScreenCapturer* dxgi = new CDxgiScreenCapturer();
        if (dxgi->Init())
        {
            m_capturer = dxgi;
            m_policy.Reset(kBackendDxgi);
            LOG_INFO("RdScreenCapturer: 抓取后端 = DXGI Desktop Duplication，%dx%d。",
                     dxgi->ScreenWidth(), dxgi->ScreenHeight());
            return;
        }
        delete dxgi;
    }

    //强制 GDI（调试）或 DXGI 不可用：用 GDI 后端
    CGdiScreenCapturer* gdi = new CGdiScreenCapturer();
    if (gdi->Init())
    {
        m_capturer = gdi;
        m_policy.Reset(kBackendGdi);
        LOG_INFO("RdScreenCapturer: 抓取后端 = GDI%s，%dx%d。",
                 preferGdi ? "（强制）" : "（DXGI 不可用降级）",
                 gdi->ScreenWidth(), gdi->ScreenHeight());
    }
    else
    {
        delete gdi;
        m_policy.Reset(kBackendNone);
        LOG_ERROR("RdScreenCapturer: DXGI 与 GDI 抓取后端均初始化失败。");
    }
}

RdScreenCapturer::~RdScreenCapturer()
{
    if (m_capturer != NULL)
    {
        delete m_capturer;
        m_capturer = NULL;
    }
}

bool RdScreenCapturer::isReady() const
{
    return m_capturer != NULL;
}

bool RdScreenCapturer::captureOneFrame(bool needFrame)
{
    //首帧多给尝试（含屏幕静止重试 + 可能的重建 / 切换）；非首帧少给（超时即沿用上一帧，
    //但留出一次额外尝试，让 AccessLost 触发的重建 / 切换后能在本轮重抓一帧）
    int maxAttempts = needFrame ? kFirstFrameTries : 2;
    for (int a = 0; a < maxAttempts; ++a)
    {
        //无后端（上次重建失败留下的空档）：先切 GDI 兜底，连 GDI 都建不起来才放弃
        if (m_capturer == NULL)
        {
            if (!fallbackToGdi())
                return false;
        }

        FrameView fv;
        CaptureResult r = m_capturer->CaptureFrame(kCaptureTimeoutMs, fv);
        FallbackAction act = m_policy.OnResult(r);

        if (r == kCaptureOk)
        {
            m_encoder.Feed(fv);
            return true;
        }
        if (act == kActionRebuildDxgi)
        {
            //AccessLost：销毁重建 DXGI（重建失败则 m_capturer 置空，下一轮切 GDI 兜底）
            rebuildDxgi();
            m_policy.NotifyRebuildAttempt();
            continue;
        }
        if (act == kActionFallbackGdi)
        {
            //重建救不回 / Fatal：降级 GDI
            fallbackToGdi();
            continue;
        }
        //kActionContinue 且非 Ok：Timeout（无新帧）—— 非首帧沿用上一帧，首帧继续等
        if (!needFrame)
            return false;
    }
    return false;
}

bool RdScreenCapturer::rebuildDxgi()
{
    if (m_capturer != NULL)
    {
        delete m_capturer;
        m_capturer = NULL;
    }

    CDxgiScreenCapturer* dxgi = new CDxgiScreenCapturer();
    if (dxgi->Init())
    {
        m_capturer = dxgi;
        //后端仍是 DXGI（状态机 backend 不变）；重建尝试计数由调用方 NotifyRebuildAttempt 维护
        LOG_INFO("RdScreenCapturer: DXGI duplication 重建成功，%dx%d。",
                 dxgi->ScreenWidth(), dxgi->ScreenHeight());
        return true;
    }
    delete dxgi;
    LOG_ERROR("RdScreenCapturer: DXGI duplication 重建失败。");
    return false;
}

bool RdScreenCapturer::fallbackToGdi()
{
    if (m_capturer != NULL)
    {
        delete m_capturer;
        m_capturer = NULL;
    }

    CGdiScreenCapturer* gdi = new CGdiScreenCapturer();
    if (gdi->Init())
    {
        m_capturer = gdi;
        m_policy.Reset(kBackendGdi);
        LOG_INFO("RdScreenCapturer: 已降级到 GDI 抓取，%dx%d。",
                 gdi->ScreenWidth(), gdi->ScreenHeight());
        return true;
    }
    delete gdi;
    LOG_ERROR("RdScreenCapturer: 降级 GDI 也初始化失败。");
    return false;
}

void RdScreenCapturer::captureBitmapInfo(std::vector<BYTE>& out)
{
    //首次产 BITMAPINFO 前确保已抓到一帧以建立画面尺寸
    if (!m_encoder.HasFrame())
        captureOneFrame(true);
    m_encoder.GetBitmapInfo(out);
}

void RdScreenCapturer::captureFirst(std::vector<BYTE>& out)
{
    //抓最新一帧作为首屏（无帧时强制抓到）
    captureOneFrame(!m_encoder.HasFrame());
    m_encoder.GetFirstFrame(out);
}

void RdScreenCapturer::captureNext(std::vector<BYTE>& out)
{
    //抓最新一帧（超时则沿用上一帧）
    captureOneFrame(false);

    //光标位置 + 类型索引：与旧引擎一致，单独写进增量帧，画面本身不含光标
    POINT cursorPos;
    cursorPos.x = 0;
    cursorPos.y = 0;
    ::GetCursorPos(&cursorPos);
    BYTE cursorType = (BYTE)m_cursorInfo.getCurrentCursorIndex();

    m_encoder.GetNextFrame(out, cursorPos, cursorType);
}

int RdScreenCapturer::screenWidth() const
{
    if (m_capturer != NULL)
        return m_capturer->ScreenWidth();
    return 0;
}

int RdScreenCapturer::screenHeight() const
{
    if (m_capturer != NULL)
        return m_capturer->ScreenHeight();
    return 0;
}

//把一块 BGRA32（top-down）像素写成 32 位 bmp（biHeight 取负保持 top-down，逐行跳过 padding）。
static bool saveBgra32Bmp(const BYTE* data, int w, int h, int stride, const wchar_t* path)
{
    if (data == NULL || w <= 0 || h <= 0)
        return false;

    int rowBytes = w * 4;
    int imgBytes = rowBytes * h;

    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    memset(&fh, 0, sizeof(fh));
    memset(&ih, 0, sizeof(ih));

    ih.biSize        = sizeof(ih);
    ih.biWidth       = w;
    ih.biHeight      = -h;   //top-down
    ih.biPlanes      = 1;
    ih.biBitCount    = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage   = (DWORD)imgBytes;

    fh.bfType    = 0x4D42;   // 'BM'
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize    = fh.bfOffBits + imgBytes;

    FILE* fp = _wfopen(path, L"wb");
    if (fp == NULL)
        return false;

    fwrite(&fh, sizeof(fh), 1, fp);
    fwrite(&ih, sizeof(ih), 1, fp);
    for (int y = 0; y < h; ++y)
    {
        const BYTE* row = data + (size_t)y * stride;
        fwrite(row, rowBytes, 1, fp);
    }
    fclose(fp);
    return true;
}

bool RdScreenCapturer::DebugPipelineToBmp(const wchar_t* path, std::wstring& msg, bool preferGdi)
{
    RdScreenCapturer cap(16, preferGdi);
    if (!cap.isReady())
    {
        msg = L"抓取后端初始化失败（DXGI 与 GDI 均不可用）。";
        return false;
    }

    //走通编码链路：BITMAPINFO + 首屏
    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    cap.captureBitmapInfo(bmi);
    cap.captureFirst(first);

    //喂给解码器还原
    RdScreenDecoder dec;
    dec.apply(bmi.data(), bmi.size());
    dec.apply(first.data(), first.size());
    if (!dec.ready())
    {
        msg = L"编码 / 解码后未就绪（可能首帧未抓到；屏幕过于静止时动一下鼠标再试）。";
        return false;
    }

    //把解码画面（16 位 DIB）经 drawTo 绘到一张 32 位 DIBSection，再存 bmp
    int w = dec.width();
    int h = dec.height();
    HDC screen = ::GetDC(NULL);
    if (screen == NULL)
    {
        msg = L"GetDC 失败。";
        return false;
    }

    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   //top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    bool ok = false;
    if (dib != NULL && bits != NULL)
    {
        HBITMAP old = (HBITMAP)::SelectObject(mem, dib);
        RECT rc;
        rc.left = 0;
        rc.top = 0;
        rc.right = w;
        rc.bottom = h;
        dec.drawTo(mem, rc);
        ::GdiFlush();
        ok = saveBgra32Bmp((const BYTE*)bits, w, h, w * 4, path);
        ::SelectObject(mem, old);
    }
    if (dib != NULL)
        ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);

    if (!ok)
    {
        msg = L"绘制 / 写 bmp 失败。";
        return false;
    }

    wchar_t buf[512];
    swprintf(buf, 512, L"抓屏链路闭环成功（%d×%d，DXGI / GDI 后端见日志），已存到：\n%s", w, h, path);
    msg = buf;
    return true;
}

} // namespace remotedesktop
