/**
 *  摄像头本地预览（调试入口）实现，CameraPreviewDebug.cpp
 *  balloonwj@qq.com   2026-06-15
 */
#include "CameraPreviewDebug.h"
#include "MfCameraCapture.h"
#include "VideoFrameConvert.h"

#include <windows.h>
#include <tchar.h>

#include "../../Common/Log/IULog.h"   //须在 windows.h 之后

#include <vector>
#include <mutex>
#include <stdio.h>
#include <string.h>

namespace call {

//预览窗口类名。
static const TCHAR* kPreviewClassName = _T("FlamingoCameraPreview");

//把 BGRA(自上而下)首帧写成 32 位 BMP（仅诊断：离线核对采到的是否真实画面）。
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

//摄像头预览：作采集回调，把 NV12 转 BGRA 存下并触发重绘；WM_PAINT 时 StretchDIBits 显示。
class CameraPreview : public MfVideoFrameHandler
{
public:
    CameraPreview()
        : m_hwnd(NULL)
        , m_w(0)
        , m_h(0)
    {
    }

    HWND Hwnd() const
    {
        return m_hwnd;
    }

    //创建并显示预览窗口。成功返回 true。
    bool CreateWin()
    {
        HINSTANCE hInst = ::GetModuleHandle(NULL);

        //注册窗口类（幂等：已注册则忽略）
        WNDCLASSEX wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &CameraPreview::WndProc;
        wc.hInstance = hInst;
        wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kPreviewClassName;
        ::RegisterClassEx(&wc);

        //样式直接带 WS_VISIBLE：进程首次 ShowWindow 的 nCmdShow 会被 STARTUPINFO 覆盖，
        //故不依赖 ShowWindow 显示，而在创建时即标记可见。
        m_hwnd = ::CreateWindowEx(WS_EX_APPWINDOW, kPreviewClassName, _T("摄像头预览（关窗停止）"),
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 660, 520,
                                  NULL, NULL, hInst, this);
        return m_hwnd != NULL;
    }

    //采集线程上下文：NV12 → BGRA 存入 m_bgra，触发窗口重绘。
    virtual void OnVideoFrame(const uint8_t* data, int width, int height, int stride) override
    {
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            if (m_w != width || m_h != height)
            {
                m_w = width;
                m_h = height;
                m_bgra.resize(static_cast<size_t>(width) * height * 4);
            }
            Nv12ToBgra(data, width, height, stride, m_bgra.data(), width * 4);
        }
        static int s_firstFrame = 0;
        if (s_firstFrame++ == 0)
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            WriteBmp32("E:\\flamingo\\camera_first_frame.bmp", m_bgra.data(), m_w, m_h);
            LOG_INFO("CameraPreview: dumped first frame to E:\\flamingo\\camera_first_frame.bmp (%dx%d).", m_w, m_h);
        }
        if (::IsWindow(m_hwnd))
            ::InvalidateRect(m_hwnd, NULL, FALSE);
    }

private:
    //把最近一帧 BGRA 拉伸绘到窗口客户区。
    void Paint(HDC hdc)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (m_bgra.empty() || m_w <= 0 || m_h <= 0)
            return;

        BITMAPINFO bmi;
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_w;
        bmi.bmiHeader.biHeight = -m_h;   //负高 = 自上而下位图（与 Nv12ToBgra 输出一致）
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        RECT rc;
        ::GetClientRect(m_hwnd, &rc);
        ::SetStretchBltMode(hdc, HALFTONE);
        ::SetBrushOrgEx(hdc, 0, 0, NULL);
        ::StretchDIBits(hdc, 0, 0, rc.right, rc.bottom, 0, 0, m_w, m_h,
                        m_bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            //把传入的 this 存进窗口用户数据，后续消息据此取回实例
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return ::DefWindowProc(hwnd, msg, wParam, lParam);
        }

        CameraPreview* self = reinterpret_cast<CameraPreview*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hwnd, &ps);
            if (self != NULL)
                self->Paint(hdc);
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

private:
    HWND                 m_hwnd;   //预览窗口
    std::mutex           m_mtx;    //保护 m_bgra / m_w / m_h（采集线程写、UI 线程读）
    std::vector<uint8_t> m_bgra;   //最近一帧 BGRA（自上而下）
    int                  m_w;      //当前帧宽
    int                  m_h;      //当前帧高
};

bool RunCameraPreviewDebug()
{
    CameraPreview preview;
    if (!preview.CreateWin())
    {
        ::MessageBox(NULL, _T("创建预览窗口失败。"), _T("摄像头预览"), MB_OK | MB_ICONWARNING);
        return false;
    }

    MfCameraCapture capture;
    int w = 0;
    int h = 0;
    if (!capture.Start(&preview, w, h))
    {
        ::MessageBox(preview.Hwnd(),
                     _T("打开摄像头失败：可能无摄像头或被其它程序占用。详见日志。"),
                     _T("摄像头预览"), MB_OK | MB_ICONWARNING);
        ::DestroyWindow(preview.Hwnd());
        return false;
    }

    ::SetForegroundWindow(preview.Hwnd());
    ::UpdateWindow(preview.Hwnd());
    LOG_INFO("CameraPreview: running, %dx%d, hwnd=%p.", w, h, (void*)preview.Hwnd());

    //消息循环：直到预览窗关闭（WM_DESTROY → PostQuitMessage）
    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    capture.Stop();
    return true;
}

} // namespace call
