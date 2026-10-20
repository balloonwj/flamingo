/**
 *  远程桌面抓屏封装（被控端）实现，RdScreenCapturer.cpp
 *  balloonwj@qq.com   2026-06-08
 *  2026-06-11 抓取改为 IScreenCapturer（DXGI 主 + GDI 降级）+ 独立编码器
 */
#include "RdScreenCapturer.h"
#include "DxgiScreenCapturer.h"
#include "GdiScreenCapturer.h"
#include "RdScreenDecoder.h"      // DebugPipelineToBmp 闭环解码
#include "RdVideoNegotiation.h"   // RdStoreFrameIfChanged：存画面并判断是否与上一帧相同（2026-10-17）
#include "../../Source/Common/Log/IULog.h"            // LOG_INFO / LOG_ERROR
#include <stdio.h>
#include <string.h>
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace remotedesktop {

//单次抓帧的等待超时（毫秒）：屏幕无变化时 DXGI AcquireNextFrame 阻塞至多这么久。
static const UINT kCaptureTimeoutMs = 120;
//首帧抓取的最多重试次数：屏幕可能短暂静止，需多等几轮以确保拿到首屏。
static const int kFirstFrameTries = 30;

RdScreenCapturer::RdScreenCapturer(int /*bitCount*/, bool preferGdi)
    : m_capturer(NULL)
    , m_bgraW(0)
    , m_bgraH(0)
    , m_hasFrame(false)
    , m_lastNew(false)
    , m_legacyPending(false)
    , m_timeoutMs(kCaptureTimeoutMs)
    , m_capturedFrames(0)
    , m_identicalFrames(0)
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
        //DXGI 不可用而降级时（例如会话开始时正停在 UAC 提示上）之后定期重试 DXGI；调试时强制 GDI 不重试（2026-10-17）
        if (!preferGdi)
            m_policy.OnFellBackToGdi(RdStreamClockMs());
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
    //因故障降级到 GDI 之后，到了安排的时刻就试着回到 DXGI（2026-10-17；此前一旦降级，本次会话就一直用 GDI）
    if (m_capturer != NULL && m_policy.ShouldRetryDxgi(RdStreamClockMs()))
        retryDxgi();

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
        CaptureResult r = m_capturer->CaptureFrame(m_timeoutMs, fv);
        FallbackAction act = m_policy.OnResult(r);

        if (r == kCaptureOk && fv.m_data != NULL && fv.m_width > 0 && fv.m_height > 0)
        {
            //存一份紧密排列的 BGRA（抓取后端的画面只在下一次抓取之前有效）；旧格式编码器在组帧之前才喂入
            const bool changed = RdStoreFrameIfChanged(fv, m_bgra, m_bgraW, m_bgraH);
            ++m_capturedFrames;
            if (!changed)
            {
                //与上一帧完全相同：当作没有新画面，沿用上一帧（2026-10-17）。DXGI 在可见画面没有变化时也会报告新帧
                //（联调实测：整屏被静止窗口盖住时仍约每秒 20 帧，逐字节相同），原先都当作新画面编码，静止时仍有约 2 Mbps。
                //还没有画面时不会走到这里（RdStoreFrameIfChanged 对空的 m_bgra 总是返回 true）
                ++m_identicalFrames;
                return false;
            }
            m_hasFrame = true;
            m_legacyPending = true;
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
        //之后定期重试 DXGI（2026-10-17）：分辨率变化、锁屏与 UAC 提示结束后能回到 DXGI
        m_policy.OnFellBackToGdi(RdStreamClockMs());
        LOG_INFO("RdScreenCapturer: 已降级到 GDI 抓取，%dx%d。",
                 gdi->ScreenWidth(), gdi->ScreenHeight());
        return true;
    }
    delete gdi;
    LOG_ERROR("RdScreenCapturer: 降级 GDI 也初始化失败。");
    return false;
}

bool RdScreenCapturer::retryDxgi()
{
    const int64_t now = RdStreamClockMs();
    CDxgiScreenCapturer* dxgi = new CDxgiScreenCapturer();
    if (!dxgi->Init())
    {
        delete dxgi;
        m_policy.OnDxgiRetryFailed(now);
        LOG_INFO("RdScreenCapturer: DXGI retry failed, keep using GDI.");
        return false;
    }
    delete m_capturer;
    m_capturer = dxgi;
    m_policy.Reset(kBackendDxgi);
    LOG_INFO("RdScreenCapturer: back to DXGI Desktop Duplication, %dx%d.", dxgi->ScreenWidth(), dxgi->ScreenHeight());
    return true;
}

bool RdScreenCapturer::captureFrame()
{
    //还没抓到过画面时强制抓到（多次重试）；之后单次抓取，超时则沿用上一帧
    m_lastNew = captureOneFrame(!m_hasFrame);
    return m_hasFrame;
}

bool RdScreenCapturer::lastCaptureNew() const
{
    return m_lastNew;
}

unsigned int RdScreenCapturer::capturedFrames() const
{
    return m_capturedFrames;
}

unsigned int RdScreenCapturer::identicalFrames() const
{
    return m_identicalFrames;
}

void RdScreenCapturer::setCaptureTimeout(UINT timeoutMs)
{
    if (timeoutMs > 0)
        m_timeoutMs = timeoutMs;
}

void RdScreenCapturer::feedLegacy()
{
    if (!m_legacyPending || m_bgra.empty())
        return;
    FrameView v;
    v.m_data = &m_bgra[0];
    v.m_width = m_bgraW;
    v.m_height = m_bgraH;
    v.m_stride = m_bgraW * 4;
    m_encoder.Feed(v);
    m_legacyPending = false;
}

bool RdScreenCapturer::needsKeyframe()
{
    feedLegacy();
    return m_encoder.NeedsKeyframe();
}

void RdScreenCapturer::buildKeyframe(std::vector<BYTE>& bitmapInfo, std::vector<BYTE>& first)
{
    feedLegacy();
    m_encoder.GetBitmapInfo(bitmapInfo);
    m_encoder.GetFirstFrame(first);
}

void RdScreenCapturer::buildNext(std::vector<BYTE>& out)
{
    feedLegacy();
    //光标位置 + 类型索引：与旧引擎一致，单独写进增量帧，画面本身不含光标
    POINT cursorPos;
    BYTE cursorType = 0;
    cursor(cursorPos, cursorType);
    m_encoder.GetNextFrame(out, cursorPos, cursorType);
}

void RdScreenCapturer::cursor(POINT& pos, BYTE& type)
{
    pos.x = 0;
    pos.y = 0;
    ::GetCursorPos(&pos);
    type = static_cast<BYTE>(m_cursorInfo.getCurrentCursorIndex());
}

const BYTE* RdScreenCapturer::frameData() const
{
    return m_bgra.empty() ? NULL : &m_bgra[0];
}

int RdScreenCapturer::frameStride() const
{
    return m_bgraW * 4;
}

int RdScreenCapturer::frameWidth() const
{
    return m_bgraW;
}

int RdScreenCapturer::frameHeight() const
{
    return m_bgraH;
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

// i18n-ignore-begin: 只供远程桌面调试入口自测编解码链路，不是界面文字
bool RdScreenCapturer::DebugPipelineToBmp(const wchar_t* path, std::wstring& msg, bool preferGdi)
{
    RdScreenCapturer cap(16, preferGdi);
    if (!cap.isReady())
    {
        msg = L"抓取后端初始化失败（DXGI 与 GDI 均不可用）。";
        return false;
    }

    //走通编码链路：抓一帧，组 BITMAPINFO + 首屏
    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    cap.captureFrame();
    cap.buildKeyframe(bmi, first);

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
// i18n-ignore-end

} // namespace remotedesktop
