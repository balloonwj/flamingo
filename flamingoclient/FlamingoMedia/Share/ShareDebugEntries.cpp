/**
 *  屏幕共享的命令行调试入口，ShareDebugEntries.cpp
 *  入口清单与输出位置见 ShareDebugEntries.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareDebugEntries.h"

#include <objbase.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <vector>
#include <mfapi.h>
#include "ShareCapturer.h"
#include "ShareSources.h"
#include "ShareVideoEncoder.h"
#include "ShareVideoPolicy.h"
#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

#ifdef _DEBUG

namespace {

//测试窗口的客户区尺寸与色块布局（像素）
const int kPatternW = 640;
const int kPatternH = 400;
const int kBlock = 100;
const int kBlockY = 60;
const int kRedX = 40;
const int kGreenX = 200;
const int kBlueX = 360;
//色块颜色比对的容差（每通道）
const int kColorTol = 24;
//缩略图框尺寸（像素）
const int kThumbW = 320;
const int kThumbH = 180;
//最多取几个窗口的缩略图
const size_t kMaxWindowThumbs = 3;
//采集一帧的最多尝试次数与每次等待（毫秒）：DXGI 首帧常因画面没变而超时，多试几次
const int kCaptureTries = 40;
const UINT kCaptureWaitMs = 100;
//等测试窗口画好的时间（毫秒）
const DWORD kSettleMs = 300;
//32 位 BMP 的位深与每像素字节数
const WORD kBitsPerPixel32 = 32;
const int kBytesPerPixel = 4;
//BMP 文件头的魔数 'BM'
const WORD kBmpMagic = 0x4D42;
//测试窗口类名
const wchar_t* const kPatternClass = L"FlamingoSharePatternWnd";

bool hasSwitch(const wchar_t* commandLine, const wchar_t* sw)
{
    return ::wcsstr(commandLine, sw) != NULL;
}

std::wstring debugDir()
{
    wchar_t path[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        dir.resize(slash);
    dir += L"\\sharedebug";
    ::CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

//把自上而下的 BGRA 写成 32 位 BMP
bool saveBmp(const std::wstring& path, const uint8_t* data, int stride, int w, int h)
{
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    memset(&fh, 0, sizeof(fh));
    memset(&ih, 0, sizeof(ih));
    const DWORD rowBytes = static_cast<DWORD>(w) * kBytesPerPixel;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = kBitsPerPixel32;
    ih.biCompression = BI_RGB;
    fh.bfType = kBmpMagic;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + rowBytes * h;
    FILE* fp = _wfopen(path.c_str(), L"wb");
    if (fp == NULL)
        return false;
    fwrite(&fh, sizeof(fh), 1, fp);
    fwrite(&ih, sizeof(ih), 1, fp);
    for (int y = 0; y < h; ++y)
        fwrite(data + static_cast<size_t>(y) * stride, rowBytes, 1, fp);
    fclose(fp);
    return true;
}

//在画面上按网格取样，数不同颜色的个数（用户屏幕与窗口的内容不落盘，只据此判断取到的不是一片纯色）
int countSampleColors(const uint8_t* data, int stride, int w, int h)
{
    //每个方向的取样点数
    const int kGrid = 16;
    std::vector<uint32_t> seen;
    for (int gy = 0; gy < kGrid; ++gy)
    {
        for (int gx = 0; gx < kGrid; ++gx)
        {
            const int x = (w - 1) * gx / (kGrid - 1);
            const int y = (h - 1) * gy / (kGrid - 1);
            const uint8_t* p = data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * kBytesPerPixel;
            const uint32_t c = (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
            bool found = false;
            for (size_t i = 0; i < seen.size() && !found; ++i)
                found = (seen[i] == c);
            if (!found)
                seen.push_back(c);
        }
    }
    return static_cast<int>(seen.size());
}

//描述一张缩略图：尺寸与取样颜色数；取不到时写 none
std::string describeBitmap(HBITMAP bmp)
{
    DIBSECTION ds;
    if (bmp == NULL || ::GetObjectW(bmp, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBits == NULL)
        return "none";
    char buf[96] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%ldx%ld colors=%d", ds.dsBm.bmWidth, ds.dsBm.bmHeight,
                countSampleColors(static_cast<const uint8_t*>(ds.dsBm.bmBits), ds.dsBm.bmWidthBytes, ds.dsBm.bmWidth,
                                  ds.dsBm.bmHeight));
    return buf;
}

LRESULT CALLBACK patternProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = ::BeginPaint(hwnd, &ps);
        RECT rc;
        ::GetClientRect(hwnd, &rc);
        HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(dc, &rc, white);
        ::DeleteObject(white);
        const int xs[3] = { kRedX, kGreenX, kBlueX };
        const COLORREF colors[3] = { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255) };
        for (int i = 0; i < 3; ++i)
        {
            RECT b = { xs[i], kBlockY, xs[i] + kBlock, kBlockY + kBlock };
            HBRUSH br = ::CreateSolidBrush(colors[i]);
            ::FillRect(dc, &b, br);
            ::DeleteObject(br);
        }
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

//一次自测的结果文本
struct CaptureReport
{
    std::vector<std::string> m_lines;   //结果行
    bool                     m_dxgiOk;  //DXGI 采集是否成功
    bool                     m_wgcOk;   //窗口采集与色块核对是否通过

    CaptureReport()
        : m_dxgiOk(false)
        , m_wgcOk(false)
    {
    }

    void add(const char* fmt, ...)
    {
        char buf[512] = { 0 };
        va_list args;
        va_start(args, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
        va_end(args);
        m_lines.push_back(buf);
        LOG_INFO("sharecapture: %s", buf);
    }
};

//在采集线程上运行：DXGI 采主显示器、WGC 采测试窗口
void captureWorker(ShareMonitor primary, HWND patternWnd, std::wstring dir, CaptureReport* report)
{
    //WGC 的激活工厂与自由线程帧池要求本线程已初始化 COM
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (primary.m_adapter >= 0)
    {
        IShareCapturer* cap = CreateMonitorCapturer(primary.m_adapter, primary.m_output, primary.m_rect);
        if (cap->Start())
        {
            ShareCaptureFrame f;
            int r = share_capture_no_frame;
            for (int i = 0; i < kCaptureTries && r != share_capture_ok; ++i)
                r = cap->Capture(kCaptureWaitMs, f);
            if (r == share_capture_ok)
            {
                //用户屏幕的内容不落盘，只核对画面不是一片纯色
                const int colors = countSampleColors(f.m_data, f.m_stride, f.m_width, f.m_height);
                report->m_dxgiOk = (colors > 1);
                report->add("dxgi primary monitor frame %dx%d, distinct sample colors=%d", f.m_width, f.m_height,
                            colors);
            }
            else
            {
                report->add("dxgi capture failed, result=%d", r);
            }
        }
        else
        {
            report->add("dxgi capturer start failed");
        }
        delete cap;
    }
    else
    {
        report->add("primary monitor has no dxgi output");
    }

    if (!IsWindowCaptureSupported())
    {
        report->add("window capture (WGC) not supported on this system");
    }
    else
    {
        IShareCapturer* cap = CreateWindowCapturer(patternWnd);
        if (cap->Start())
        {
            ShareCaptureFrame f;
            int r = share_capture_no_frame;
            for (int i = 0; i < kCaptureTries && r != share_capture_ok; ++i)
                r = cap->Capture(kCaptureWaitMs, f);
            if (r == share_capture_ok)
            {
                saveBmp(dir + L"\\wgc_pattern.bmp", f.m_data, f.m_stride, f.m_width, f.m_height);
                //色块在客户区里的位置要加上客户区在可见外框里的偏移
                const RECT vb = GetShareWindowBounds(patternWnd);
                POINT origin = { 0, 0 };
                ::ClientToScreen(patternWnd, &origin);
                const int offX = origin.x - vb.left;
                const int offY = origin.y - vb.top;
                const int xs[3] = { kRedX, kGreenX, kBlueX };
                const int expect[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
                int bad = 0;
                for (int i = 0; i < 3; ++i)
                {
                    const int x = offX + xs[i] + kBlock / 2;
                    const int y = offY + kBlockY + kBlock / 2;
                    if (x < 0 || y < 0 || x >= f.m_width || y >= f.m_height)
                    {
                        ++bad;
                        continue;
                    }
                    const uint8_t* p = f.m_data + static_cast<size_t>(y) * f.m_stride + x * kBytesPerPixel;
                    const int rgb[3] = { p[2], p[1], p[0] };
                    for (int c = 0; c < 3; ++c)
                    {
                        if (abs(rgb[c] - expect[i][c]) > kColorTol)
                        {
                            ++bad;
                            break;
                        }
                    }
                }
                report->m_wgcOk = (bad == 0);
                report->add("wgc pattern window frame %dx%d, color blocks bad=%d", f.m_width, f.m_height, bad);
            }
            else
            {
                report->add("wgc capture failed, result=%d", r);
            }
        }
        else
        {
            report->add("wgc capturer start failed");
        }
        delete cap;
    }
    ::CoUninitialize();
}

//编码自测的时长，单位毫秒；第几次编码时强制关键帧
const DWORD kEncodeTestMs = 3000;
const int kForceKeyAtEncode = 20;

//编码自测：收集编码输出，并当场解码核对
class EncodeTestSink : public ShareVideoEncoderSink, public call::MfH264DecoderSink
{
public:
    EncodeTestSink()
        : m_expectW(0)
        , m_expectH(0)
        , m_packets(0)
        , m_keyframes(0)
        , m_bytes(0)
        , m_decoded(0)
        , m_decodedW(0)
        , m_decodedH(0)
        , m_lastWasKey(false)
    {
    }

    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs)
    {
        ++m_packets;
        m_bytes += len;
        if (keyframe)
            ++m_keyframes;
        m_lastWasKey = keyframe;
        if (!m_decoder.IsReady())
            m_decoder.Init(m_expectW, m_expectH);
        m_decoder.DecodePacket(data, len, static_cast<int64_t>(timestampUs), this);
    }

    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns)
    {
        (void)nv12;
        (void)stride;
        (void)ptsHns;
        ++m_decoded;
        m_decodedW = width;
        m_decodedH = height;
    }

    call::MfH264Decoder m_decoder;      //回环解码器
    int                 m_expectW;      //编码宽（解码器初始化用）
    int                 m_expectH;      //编码高
    int                 m_packets;      //编码输出的访问单元数
    int                 m_keyframes;    //其中的关键帧数
    size_t              m_bytes;        //编码输出的总字节数
    int                 m_decoded;      //解出的帧数
    int                 m_decodedW;     //解出帧的宽
    int                 m_decodedH;     //解出帧的高
    bool                m_lastWasKey;   //最近一个访问单元是否关键帧
};

void encodeWorker(ShareMonitor primary, CaptureReport* report)
{
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    IShareCapturer* cap = CreateMonitorCapturer(primary.m_adapter, primary.m_output, primary.m_rect);
    if (primary.m_adapter < 0 || !cap->Start())
    {
        report->add("encode test: capturer start failed");
    }
    else
    {
        ShareVideoEncoder encoder;
        EncodeTestSink sink;
        ShareFramePacer pacer(kShareMaxFps, kShareKeyFrameIntervalMs);
        LARGE_INTEGER freq;
        ::QueryPerformanceFrequency(&freq);
        double encodeMsTotal = 0;
        int encodes = 0;
        bool forcedKeyOk = false;
        //最近一帧画面的拷贝：画面没变时 DXGI 不给新帧，自测里按帧率上限重编这一帧，凑够编码次数
        std::vector<uint8_t> last;
        ShareCaptureFrame lastFrame;
        const DWORD start = ::GetTickCount();
        while (::GetTickCount() - start < kEncodeTestMs)
        {
            ShareCaptureFrame f;
            if (cap->Capture(static_cast<UINT>(pacer.minIntervalMs()), f) == share_capture_ok)
            {
                last.resize(static_cast<size_t>(f.m_width) * f.m_height * kBytesPerPixel);
                for (int y = 0; y < f.m_height; ++y)
                    memcpy(&last[static_cast<size_t>(y) * f.m_width * kBytesPerPixel],
                           f.m_data + static_cast<size_t>(y) * f.m_stride,
                           static_cast<size_t>(f.m_width) * kBytesPerPixel);
                lastFrame.m_data = &last[0];
                lastFrame.m_stride = f.m_width * kBytesPerPixel;
                lastFrame.m_width = f.m_width;
                lastFrame.m_height = f.m_height;
            }
            if (last.empty())
                continue;
            const bool forceKey = (encodes == kForceKeyAtEncode);
            const int64_t now = static_cast<int64_t>(::GetTickCount());
            const ShareFramePacer::Decision d = pacer.decide(now, true, forceKey, true);
            if (!d.m_encode)
                continue;
            sink.m_expectW = encoder.EncodeWidth() > 0 ? encoder.EncodeWidth() : lastFrame.m_width;
            sink.m_expectH = encoder.EncodeHeight() > 0 ? encoder.EncodeHeight() : lastFrame.m_height;
            LARGE_INTEGER t0;
            LARGE_INTEGER t1;
            ::QueryPerformanceCounter(&t0);
            encoder.Encode(lastFrame, d.m_keyframe, static_cast<uint64_t>(now) * 1000, &sink);
            ::QueryPerformanceCounter(&t1);
            encodeMsTotal += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
            if (forceKey && sink.m_lastWasKey)
                forcedKeyOk = true;
            pacer.onEncoded(now, d.m_keyframe);
            ++encodes;
        }
        report->add("encode test: %dx%d -> %dx%d, encodes=%d packets=%d keyframes=%d bytes=%u avg encode %.2f ms",
                    primary.m_rect.right - primary.m_rect.left, primary.m_rect.bottom - primary.m_rect.top,
                    encoder.EncodeWidth(), encoder.EncodeHeight(), encodes, sink.m_packets, sink.m_keyframes,
                    static_cast<unsigned>(sink.m_bytes), encodes > 0 ? encodeMsTotal / encodes : 0.0);
        //单独计时颜色转换：与上面的平均耗时对比，看出转换与编码器各占多少
        const int kConvertRuns = 5;
        double convertMs = 0;
        if (!last.empty() && encoder.EncodeWidth() > 0)
        {
            std::vector<uint8_t> nv12(static_cast<size_t>(encoder.EncodeWidth()) * encoder.EncodeHeight() * 3 / 2);
            LARGE_INTEGER c0;
            LARGE_INTEGER c1;
            ::QueryPerformanceCounter(&c0);
            for (int i = 0; i < kConvertRuns; ++i)
                ConvertBgraToNv12(lastFrame.m_data, lastFrame.m_stride, lastFrame.m_width, lastFrame.m_height, &nv12[0],
                                  encoder.EncodeWidth(), encoder.EncodeHeight());
            ::QueryPerformanceCounter(&c1);
            convertMs = static_cast<double>(c1.QuadPart - c0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart) /
                        kConvertRuns;
        }
        report->add("encode test: bgra->nv12 conversion alone %.2f ms per frame", convertMs);
        report->add("encode test: decoded=%d size=%dx%d forced keyframe at encode %d: %s", sink.m_decoded,
                    sink.m_decodedW, sink.m_decodedH, kForceKeyAtEncode,
                    encodes > kForceKeyAtEncode ? (forcedKeyOk ? "yes" : "no") : "not reached");
        //解码器输出的高会补齐到 16 的倍数（1080 → 1088），观看端按载荷里的真实尺寸裁剪，这里允许这部分差异
        const int kMacroblock = 16;
        report->m_dxgiOk = sink.m_packets > 0 && sink.m_decoded > 0 && sink.m_decodedW >= encoder.EncodeWidth() &&
                           sink.m_decodedW - encoder.EncodeWidth() < kMacroblock &&
                           sink.m_decodedH >= encoder.EncodeHeight() &&
                           sink.m_decodedH - encoder.EncodeHeight() < kMacroblock && sink.m_keyframes >= 1;
        report->m_wgcOk = (encodes <= kForceKeyAtEncode) || forcedKeyOk;
    }
    delete cap;
    ::MFShutdown();
    ::CoUninitialize();
}

int runShareEncode()
{
    const std::wstring dir = debugDir();
    CaptureReport report;
    std::vector<ShareMonitor> monitors;
    EnumerateShareMonitors(monitors);
    if (monitors.empty())
    {
        report.add("encode test: no monitor");
    }
    else
    {
        std::thread worker(encodeWorker, monitors[0], &report);
        worker.join();
    }
    report.add("RESULT encode+decode=%s forcedkey=%s", report.m_dxgiOk ? "PASS" : "FAIL",
               report.m_wgcOk ? "PASS" : "FAIL");
    FILE* fp = _wfopen((dir + L"\\shareencode_result.txt").c_str(), L"wb");
    if (fp != NULL)
    {
        for (size_t i = 0; i < report.m_lines.size(); ++i)
            fprintf(fp, "%s\r\n", report.m_lines[i].c_str());
        fclose(fp);
    }
    return flamingomedia::media_debug_request_exit;
}

int runShareCapture(HWND hwndHost)
{
    const std::wstring dir = debugDir();
    CaptureReport report;

    std::vector<ShareMonitor> monitors;
    EnumerateShareMonitors(monitors);
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        const ShareMonitor& m = monitors[i];
        report.add("monitor %u primary=%d rect=(%ld,%ld)-(%ld,%ld) dxgi=%d/%d", static_cast<unsigned>(i),
                   m.m_primary ? 1 : 0, m.m_rect.left, m.m_rect.top, m.m_rect.right, m.m_rect.bottom, m.m_adapter,
                   m.m_output);
    }
    if (!monitors.empty())
    {
        HBITMAP thumb = CaptureMonitorThumbnail(monitors[0].m_rect, kThumbW, kThumbH);
        report.add("monitor thumbnail %s", describeBitmap(thumb).c_str());
        if (thumb != NULL)
            ::DeleteObject(thumb);
    }

    std::vector<ShareWindow> windows;
    EnumerateShareWindows(windows);
    report.add("%u shareable window(s)", static_cast<unsigned>(windows.size()));
    for (size_t i = 0; i < windows.size() && i < kMaxWindowThumbs; ++i)
    {
        HBITMAP thumb = CaptureWindowThumbnail(windows[i].m_hwnd, kThumbW, kThumbH);
        report.add("window %u minimized=%d thumbnail %s", static_cast<unsigned>(i), windows[i].m_minimized ? 1 : 0,
                   describeBitmap(thumb).c_str());
        if (thumb != NULL)
            ::DeleteObject(thumb);
    }

    //测试窗口：本进程自己的窗口，采集它不涉及用户的其它程序
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = patternProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.lpszClassName = kPatternClass;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    ::RegisterClassW(&wc);
    RECT rc = { 0, 0, kPatternW, kPatternH };
    ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND pattern = ::CreateWindowW(kPatternClass, L"Flamingo share capture pattern", WS_OVERLAPPEDWINDOW, 100, 100,
                                   rc.right - rc.left, rc.bottom - rc.top, hwndHost, NULL, wc.hInstance, NULL);
    ::ShowWindow(pattern, SW_SHOWNOACTIVATE);
    ::UpdateWindow(pattern);
    const DWORD settleStart = ::GetTickCount();
    MSG msg;
    while (::GetTickCount() - settleStart < kSettleMs)
    {
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(10);
    }

    ShareMonitor primary;
    if (!monitors.empty())
        primary = monitors[0];
    std::thread worker(captureWorker, primary, pattern, dir, &report);
    //采集线程工作期间 UI 线程继续处理消息，测试窗口才能被采到
    while (true)
    {
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (::WaitForSingleObject(worker.native_handle(), 10) == WAIT_OBJECT_0)
            break;
    }
    worker.join();
    ::DestroyWindow(pattern);
    ::UnregisterClassW(kPatternClass, wc.hInstance);

    report.add("RESULT dxgi=%s wgc=%s", report.m_dxgiOk ? "PASS" : "FAIL", report.m_wgcOk ? "PASS" : "FAIL");
    FILE* fp = _wfopen((dir + L"\\sharecapture_result.txt").c_str(), L"wb");
    if (fp != NULL)
    {
        for (size_t i = 0; i < report.m_lines.size(); ++i)
            fprintf(fp, "%s\r\n", report.m_lines[i].c_str());
        fclose(fp);
    }
    return flamingomedia::media_debug_request_exit;
}

} // namespace

#endif // _DEBUG

int RunShareDebugEntry(const wchar_t* commandLine, HWND hwndHost, flamingomedia::IMediaHost* host)
{
    (void)host;
#ifdef _DEBUG
    if (commandLine == NULL)
        return flamingomedia::media_debug_not_handled;
    //采集自测：枚举、缩略图、DXGI 与 WGC 各采一帧
    if (hasSwitch(commandLine, L"--sharecapture"))
        return runShareCapture(hwndHost);
    //编码自测：采主显示器约 3 秒，按发帧节奏编码并当场解码核对，中途强制一次关键帧
    if (hasSwitch(commandLine, L"--shareencode"))
        return runShareEncode();
#else
    (void)commandLine;
    (void)hwndHost;
#endif
    return flamingomedia::media_debug_not_handled;
}

} // namespace screenshare
