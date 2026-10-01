/**
 *  摄像头显示自包含测试（调试入口）实现，CameraDisplayTest.cpp
 *  balloonwj@qq.com   2026-06-16
 *
 *  专为定位「摄像头数据显示不出来」：自包含走一遍 MF 摄像头采集全过程并逐步打日志，采到的帧
 *  转 BGRA 显示到 Win32 窗口。日志写到固定文件 E:\flamingo\cameratest.log（每步 flush，便于
 *  随时查看 / 发回分析）。
 */
#include "CameraDisplayTest.h"
#include "VideoFrameConvert.h"   //call::Nv12ToBgra

#include <windows.h>
#include <tchar.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include "../../Common/Log/IULog.h"   //须在 windows.h 之后

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace call {

//测试日志文件路径（固定，便于查看 / 发回）。
static const char*  kLogPath        = "E:\\flamingo\\cameratest.log";
//首帧落盘 BMP 路径（离线核对采到的是否真实画面）。
static const char*  kFirstFrameBmp  = "E:\\flamingo\\cameratest_first.bmp";
//测试窗口类名。
static const TCHAR* kTestClassName  = _T("FlamingoCameraTest");

//日志文件句柄与起始 tick（仅本测试单线程入口使用，简单全局即可）。
static FILE* g_logFile  = NULL;
static DWORD g_startTick = 0;

//打一条测试日志：写固定文件（带相对毫秒前缀、每条 flush）并镜像到客户端日志器。
static void TestLog(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    DWORD ms = ::GetTickCount() - g_startTick;
    if (g_logFile != NULL)
    {
        fprintf(g_logFile, "[%6lu ms] %s\n", ms, buf);
        fflush(g_logFile);
    }
    LOG_INFO("CameraTest: %s", buf);
}

//把视频子类型 GUID 的 FOURCC（Data1）转成可读 4 字符（非打印字符以 '.' 代替）。
static void FourccToText(const GUID& sub, char out[5])
{
    unsigned char* p = reinterpret_cast<unsigned char*>(const_cast<GUID*>(&sub));
    int i;
    for (i = 0; i < 4; ++i)
    {
        unsigned char c = p[i];
        out[i] = (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
    }
    out[4] = '\0';
}

//把 BGRA(自上而下)写成 32 位 BMP。
static void WriteBmp32(const char* path, const uint8_t* bgra, int w, int h)
{
    if (bgra == NULL || w <= 0 || h <= 0)
        return;
    FILE* f = fopen(path, "wb");
    if (f == NULL)
        return;
    int imgSize = w * h * 4;
    int fileSize = 54 + imgSize;
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    *reinterpret_cast<int*>(&hdr[2]) = fileSize;
    *reinterpret_cast<int*>(&hdr[10]) = 54;
    *reinterpret_cast<int*>(&hdr[14]) = 40;
    *reinterpret_cast<int*>(&hdr[18]) = w;
    *reinterpret_cast<int*>(&hdr[22]) = -h;
    *reinterpret_cast<short*>(&hdr[26]) = 1;
    *reinterpret_cast<short*>(&hdr[28]) = 32;
    *reinterpret_cast<int*>(&hdr[34]) = imgSize;
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(bgra, 1, imgSize, f);
    fclose(f);
}

//测试共享状态：采集线程写帧、UI 线程绘制；计数供关窗汇总。
struct CamTestState
{
    HWND                 hwnd;        //显示窗口
    std::mutex           mtx;         //保护 bgra / w / h
    std::vector<uint8_t> bgra;        //最近一帧 BGRA（自上而下）
    int                  w;           //帧宽
    int                  h;           //帧高
    std::atomic<bool>    stopping;    //通知采集线程退出
    std::atomic<int>     frameCount;  //已采集帧数
    std::atomic<int>     paintCount;  //已绘制次数
    std::atomic<bool>    gotFrame;    //是否至少采到一帧

    CamTestState()
        : hwnd(NULL)
        , w(0)
        , h(0)
        , stopping(false)
        , frameCount(0)
        , paintCount(0)
        , gotFrame(false)
    {
    }
};

//把最近一帧拉伸绘到窗口客户区，并记录绘制次数（前几次打日志）。
static void PaintState(CamTestState* st, HDC hdc)
{
    std::lock_guard<std::mutex> guard(st->mtx);
    int n = st->paintCount.fetch_add(1) + 1;

    if (st->bgra.empty() || st->w <= 0 || st->h <= 0)
    {
        //还没有帧：填深灰底，让窗口可见（区别于纯黑＝没在画）
        RECT rc;
        ::GetClientRect(st->hwnd, &rc);
        HBRUSH br = ::CreateSolidBrush(RGB(40, 40, 40));
        ::FillRect(hdc, &rc, br);
        ::DeleteObject(br);
        if (n <= 3)
            TestLog("WM_PAINT #%d：尚无帧，画深灰底。", n);
        return;
    }

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = st->w;
    bmi.bmiHeader.biHeight = -st->h;   //负高 = 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    RECT rc;
    ::GetClientRect(st->hwnd, &rc);
    ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, NULL);
    int ret = ::StretchDIBits(hdc, 0, 0, rc.right, rc.bottom, 0, 0, st->w, st->h,
                              st->bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    if (n <= 3)
        TestLog("WM_PAINT #%d：StretchDIBits 返回 %d（源 %dx%d，目标 %dx%d）。",
                n, ret, st->w, st->h, (int)rc.right, (int)rc.bottom);
}

static LRESULT CALLBACK TestWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProc(hwnd, msg, wParam, lParam);
    }

    CamTestState* st = reinterpret_cast<CamTestState*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(hwnd, &ps);
        if (st != NULL)
            PaintState(st, hdc);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;   //自绘整屏，吞掉擦背景避免闪烁
    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

//采集线程：自包含走 MF 摄像头采集全过程，逐步打日志；采到的帧转 BGRA 存入 st 并触发重绘。
static void CaptureThread(CamTestState* st)
{
    HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInited = SUCCEEDED(hr);
    TestLog("CoInitializeEx(MTA) hr=0x%08X（%s）。", (unsigned)hr, comInited ? "OK" : "失败");
    bool mfInited = false;

    IMFAttributes*   attrs = NULL;
    IMFActivate**    devices = NULL;
    UINT32           deviceCount = 0;
    IMFMediaSource*  source = NULL;
    IMFSourceReader* reader = NULL;
    IMFMediaType*    wantType = NULL;
    IMFMediaType*    actualType = NULL;
    int              width = 0;
    int              height = 0;
    int              stride = 0;
    bool             ok = false;

    do
    {
        hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
        TestLog("MFStartup hr=0x%08X（%s）。", (unsigned)hr, SUCCEEDED(hr) ? "OK" : "失败");
        if (FAILED(hr))
            break;
        mfInited = true;

        hr = ::MFCreateAttributes(&attrs, 1);
        TestLog("MFCreateAttributes hr=0x%08X。", (unsigned)hr);
        if (FAILED(hr))
            break;
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        hr = ::MFEnumDeviceSources(attrs, &devices, &deviceCount);
        TestLog("MFEnumDeviceSources hr=0x%08X，设备数=%u。", (unsigned)hr, deviceCount);
        if (FAILED(hr) || deviceCount == 0)
        {
            TestLog("没有视频采集设备：无法继续。");
            break;
        }
        //打印每个设备的友好名
        for (UINT32 i = 0; i < deviceCount; ++i)
        {
            WCHAR* name = NULL;
            UINT32 nameLen = 0;
            if (devices[i] != NULL
                && SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen))
                && name != NULL)
            {
                char nameU8[256];
                ::WideCharToMultiByte(CP_UTF8, 0, name, -1, nameU8, sizeof(nameU8), NULL, NULL);
                TestLog("  设备[%u]：%s", i, nameU8);
                ::CoTaskMemFree(name);
            }
            else
            {
                TestLog("  设备[%u]：（取友好名失败）", i);
            }
        }

        hr = devices[0]->ActivateObject(IID_PPV_ARGS(&source));
        TestLog("ActivateObject(设备0) hr=0x%08X（%s）。", (unsigned)hr, (SUCCEEDED(hr) && source != NULL) ? "OK" : "失败");
        if (FAILED(hr) || source == NULL)
            break;

        hr = ::MFCreateSourceReaderFromMediaSource(source, NULL, &reader);
        TestLog("MFCreateSourceReaderFromMediaSource hr=0x%08X（%s）。", (unsigned)hr, (SUCCEEDED(hr) && reader != NULL) ? "OK" : "失败");
        if (FAILED(hr) || reader == NULL)
            break;

        //枚举相机原生支持的格式（诊断：看相机给的是 NV12 / YUY2 / MJPG 等）
        TestLog("---- 相机原生格式列表 ----");
        for (DWORD ti = 0; ti < 64; ++ti)
        {
            IMFMediaType* nt = NULL;
            HRESULT hrn = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, ti, &nt);
            if (hrn == MF_E_NO_MORE_TYPES || FAILED(hrn) || nt == NULL)
                break;
            GUID sub = GUID_NULL;
            nt->GetGUID(MF_MT_SUBTYPE, &sub);
            UINT32 nw = 0;
            UINT32 nh = 0;
            ::MFGetAttributeSize(nt, MF_MT_FRAME_SIZE, &nw, &nh);
            char fcc[5];
            FourccToText(sub, fcc);
            TestLog("  原生[%lu]：fourcc='%s'(0x%08X) %ux%u", ti, fcc, (unsigned)sub.Data1, nw, nh);
            nt->Release();
        }
        TestLog("---- 原生格式列表结束 ----");

        //请求输出 NV12（MF 自动插入 设备原始格式→NV12 转换器）
        hr = ::MFCreateMediaType(&wantType);
        if (FAILED(hr))
            break;
        wantType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        wantType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, wantType);
        TestLog("SetCurrentMediaType(NV12) hr=0x%08X（%s）。", (unsigned)hr, SUCCEEDED(hr) ? "OK" : "失败");
        if (FAILED(hr))
        {
            TestLog("强制 NV12 输出失败——相机/MF 可能不支持转 NV12，需换格式。");
            break;
        }

        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType);
        if (FAILED(hr) || actualType == NULL)
        {
            TestLog("GetCurrentMediaType 失败 hr=0x%08X。", (unsigned)hr);
            break;
        }
        UINT32 w = 0;
        UINT32 h = 0;
        ::MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &w, &h);
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        UINT32 defStride = 0;
        if (FAILED(actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, &defStride)) || static_cast<int>(defStride) <= 0)
            stride = width;
        else
            stride = static_cast<int>(defStride);
        TestLog("协商结果：%dx%d，stride=%d（NV12 期望帧字节=%d）。",
                width, height, stride, width * height * 3 / 2);

        if (width <= 0 || height <= 0)
        {
            TestLog("协商到的尺寸非法：放弃。");
            break;
        }
        ok = true;
    } while (false);

    if (!ok)
    {
        TestLog("初始化未通过，采集线程退出。");
        if (actualType != NULL) actualType->Release();
        if (wantType != NULL) wantType->Release();
        if (reader != NULL) reader->Release();
        if (source != NULL) { source->Shutdown(); source->Release(); }
        if (devices != NULL)
        {
            for (UINT32 i = 0; i < deviceCount; ++i)
                if (devices[i] != NULL) devices[i]->Release();
            ::CoTaskMemFree(devices);
        }
        if (attrs != NULL) attrs->Release();
        if (mfInited) ::MFShutdown();
        if (comInited) ::CoUninitialize();
        return;
    }

    TestLog("进入采集循环……");
    int loopCount = 0;
    while (!st->stopping.load())
    {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = NULL;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                &streamIndex, &flags, &timestamp, &sample);
        ++loopCount;
        if (FAILED(hr))
        {
            TestLog("ReadSample 失败 hr=0x%08X（循环第 %d 次），退出采集。", (unsigned)hr, loopCount);
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            TestLog("收到 ENDOFSTREAM（循环第 %d 次），退出采集。", loopCount);
            if (sample != NULL) sample->Release();
            break;
        }
        if (sample == NULL)
        {
            //无数据（格式变更通知 / 暂无帧）——前几次记一下，避免日志爆量
            if (loopCount <= 5 || (loopCount % 60) == 0)
                TestLog("ReadSample 返回空样本 flags=0x%08X（循环第 %d 次），继续。", (unsigned)flags, loopCount);
            continue;
        }

        IMFMediaBuffer* buf = NULL;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf != NULL)
        {
            BYTE* data = NULL;
            DWORD maxLen = 0;
            DWORD curLen = 0;
            if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen)) && data != NULL)
            {
                int fn = st->frameCount.fetch_add(1) + 1;
                st->gotFrame.store(true);

                //前若干帧 + 周期性：记录长度 / 标志 / Y 平面亮度极值（判断真实画面 vs 占位图）
                if (fn <= 5 || (fn % 30) == 0)
                {
                    int ymin = 255;
                    int ymax = 0;
                    long ysum = 0;
                    int yCount = width * height;
                    if (yCount > static_cast<int>(curLen))
                        yCount = static_cast<int>(curLen);   //防越界
                    int step = (yCount > 4096) ? (yCount / 4096) : 1;   //采样最多 ~4096 点
                    int sampled = 0;
                    int i;
                    for (i = 0; i < yCount; i += step)
                    {
                        int y = data[i];
                        if (y < ymin) ymin = y;
                        if (y > ymax) ymax = y;
                        ysum += y;
                        ++sampled;
                    }
                    int yavg = (sampled > 0) ? static_cast<int>(ysum / sampled) : 0;
                    TestLog("帧 #%d：curLen=%lu flags=0x%08X，Y 亮度 min=%d max=%d avg=%d（min==max 多半是纯色/占位）。",
                            fn, curLen, (unsigned)flags, ymin, ymax, yavg);
                }

                //NV12 → BGRA 存入共享缓冲
                {
                    std::lock_guard<std::mutex> guard(st->mtx);
                    if (st->w != width || st->h != height)
                    {
                        st->w = width;
                        st->h = height;
                        st->bgra.resize(static_cast<size_t>(width) * height * 4);
                    }
                    call::Nv12ToBgra(data, width, height, stride, st->bgra.data(), width * 4);

                    //首帧落 BMP
                    if (fn == 1)
                    {
                        WriteBmp32(kFirstFrameBmp, st->bgra.data(), width, height);
                        TestLog("首帧已落 BMP：%s", kFirstFrameBmp);
                    }
                }

                buf->Unlock();

                //触发窗口重绘
                if (::IsWindow(st->hwnd))
                    ::InvalidateRect(st->hwnd, NULL, FALSE);
            }
            else
            {
                if (loopCount <= 5)
                    TestLog("buf->Lock 失败（循环第 %d 次）。", loopCount);
            }
            buf->Release();
        }
        else
        {
            if (loopCount <= 5)
                TestLog("ConvertToContiguousBuffer 失败（循环第 %d 次）。", loopCount);
        }
        sample->Release();
    }

    TestLog("采集循环结束：共 %d 帧，ReadSample 调用 %d 次。", st->frameCount.load(), loopCount);

    if (actualType != NULL) actualType->Release();
    if (wantType != NULL) wantType->Release();
    if (reader != NULL) reader->Release();
    if (source != NULL) { source->Shutdown(); source->Release(); }
    if (devices != NULL)
    {
        for (UINT32 i = 0; i < deviceCount; ++i)
            if (devices[i] != NULL) devices[i]->Release();
        ::CoTaskMemFree(devices);
    }
    if (attrs != NULL) attrs->Release();
    if (mfInited) ::MFShutdown();
    if (comInited) ::CoUninitialize();
}

bool RunCameraDisplayTest()
{
    g_startTick = ::GetTickCount();
    g_logFile = fopen(kLogPath, "w");
    TestLog("=== 摄像头显示测试开始 ===");
    if (g_logFile == NULL)
        LOG_ERROR("CameraTest: 打开日志文件 %s 失败（仍会写客户端日志）。", kLogPath);

    //先建窗口（带 WS_VISIBLE：进程首次 ShowWindow 的 nCmdShow 会被 STARTUPINFO 覆盖）
    CamTestState st;
    HINSTANCE hInst = ::GetModuleHandle(NULL);
    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TestWndProc;
    wc.hInstance = hInst;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kTestClassName;
    ::RegisterClassEx(&wc);

    st.hwnd = ::CreateWindowEx(WS_EX_APPWINDOW, kTestClassName,
                               _T("摄像头显示测试（关窗结束，日志见 E:\\flamingo\\cameratest.log）"),
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 720, 560,
                               NULL, NULL, hInst, &st);
    if (st.hwnd == NULL)
    {
        TestLog("创建测试窗口失败。");
        ::MessageBox(NULL, _T("创建测试窗口失败。"), _T("摄像头显示测试"), MB_OK | MB_ICONWARNING);
        if (g_logFile != NULL) { fclose(g_logFile); g_logFile = NULL; }
        return false;
    }
    TestLog("测试窗口已创建 hwnd=%p。", (void*)st.hwnd);
    ::SetForegroundWindow(st.hwnd);
    ::UpdateWindow(st.hwnd);

    //起采集线程
    std::thread capThread(&CaptureThread, &st);

    //消息循环：直到关窗（WM_DESTROY → PostQuitMessage）
    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    //关窗后停采集线程
    st.stopping.store(true);
    if (capThread.joinable())
        capThread.join();

    int frames = st.frameCount.load();
    int paints = st.paintCount.load();
    TestLog("=== 测试结束：采集 %d 帧，绘制 %d 次 ===", frames, paints);
    if (g_logFile != NULL) { fclose(g_logFile); g_logFile = NULL; }

    //弹框汇总，便于直接口头反馈
    TCHAR summary[512];
    _stprintf_s(summary, _countof(summary),
                _T("摄像头显示测试结束。\n\n采集帧数：%d\n绘制次数：%d\n\n")
                _T("日志：E:\\flamingo\\cameratest.log\n首帧：E:\\flamingo\\cameratest_first.bmp\n\n")
                _T("· 采集帧数为 0 → 相机没出帧（看日志哪步失败）。\n")
                _T("· 帧数>0 但窗口黑/不动 → 显示环节问题。\n")
                _T("· 日志里 Y min==max → 多半是灰色占位图（相机被关/被策略锁）。"),
                frames, paints);
    ::MessageBox(NULL, summary, _T("摄像头显示测试"), MB_OK | MB_ICONINFORMATION);
    return (frames > 0);
}

} // namespace call
