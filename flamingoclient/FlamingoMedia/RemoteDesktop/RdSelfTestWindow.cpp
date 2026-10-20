/**
 *  远程桌面引擎自环测试窗口实现，RdSelfTestWindow.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RdSelfTestWindow.h"
#include "RdScreenCapturer.h"
#include "RdScreenDecoder.h"
#include "RdInputEncoder.h"
#include "RdInputInjector.h"

using namespace remotedesktop;

//窗口类名
static const wchar_t* const kRdSelfTestClass = L"FlamingoRdSelfTestWnd";
//自环定时器 id
static const UINT_PTR kTimerId = 1;
//自环周期（毫秒）：5 帧 / 秒，足够目视确认画面与实时更新
static const UINT kTimerMs = 200;

CRdSelfTestWindow::CRdSelfTestWindow()
    : m_hWnd(NULL)
    , m_capturer(NULL)
    , m_decoder(NULL)
{
}

CRdSelfTestWindow::~CRdSelfTestWindow()
{
    if (m_capturer != NULL)
        delete m_capturer;
    if (m_decoder != NULL)
        delete m_decoder;
}

void CRdSelfTestWindow::Destroy()
{
    if (m_hWnd != NULL && ::IsWindow(m_hWnd))
    {
        ::DestroyWindow(m_hWnd);
    }
    m_hWnd = NULL;
}

void CRdSelfTestWindow::CreateAndShow(HWND hParent)
{
    (void)hParent;
    HINSTANCE hInst = ::GetModuleHandle(NULL);

    //注册窗口类（进程内只注册一次）
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = ::LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kRdSelfTestClass;
        ::RegisterClassExW(&wc);
        s_registered = true;
    }

    //先建好引擎，抓一帧、喂入 BITMAPINFO 帧与首屏帧，使解码器进入就绪态
    m_capturer = new RdScreenCapturer(16);
    m_decoder  = new RdScreenDecoder();
    std::vector<BYTE> first;
    m_capturer->captureFrame();
    m_capturer->buildKeyframe(m_buf, first);
    m_decoder->apply(m_buf.data(), m_buf.size());
    m_decoder->apply(first.data(), first.size());

    m_hWnd = ::CreateWindowExW(
        0, kRdSelfTestClass,
        L"远程桌面引擎自环测试（抓屏 → 编码 → 解码 → 绘制）",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
        NULL, NULL, hInst, this);

    if (m_hWnd != NULL)
    {
        ::SetTimer(m_hWnd, kTimerId, kTimerMs, NULL);
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::UpdateWindow(m_hWnd);
    }
}

LRESULT CALLBACK CRdSelfTestWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //WM_CREATE 时把 this 指针存进窗口，后续消息据此回到对象
    if (msg == WM_CREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    CRdSelfTestWindow* self = (CRdSelfTestWindow*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_TIMER:
        if (self != NULL)
        {
            self->onTimer();
            //只标记重绘、不擦背景，避免闪烁
            ::InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hWnd, &ps);
            if (self != NULL)
                self->onPaint(hdc);
            ::EndPaint(hWnd, &ps);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (self != NULL)
            self->onLButtonDown(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_ERASEBKGND:
        //背景由 onPaint 整屏覆盖，无需擦除（防闪烁）
        return 1;

    case WM_CLOSE:
        ::DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        ::KillTimer(hWnd, kTimerId);
        return 0;

    default:
        break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void CRdSelfTestWindow::onTimer()
{
    if (m_capturer == NULL || m_decoder == NULL)
        return;

    //抓下一帧：画面尺寸变了时编码器报告需要关键帧，喂 BITMAPINFO 与首屏；否则喂增量帧
    m_capturer->captureFrame();
    if (m_capturer->needsKeyframe())
    {
        std::vector<BYTE> first;
        m_capturer->buildKeyframe(m_buf, first);
        m_decoder->apply(m_buf.data(), m_buf.size());
        m_decoder->apply(first.data(), first.size());
        return;
    }
    m_capturer->buildNext(m_buf);
    m_decoder->apply(m_buf.data(), m_buf.size());
}

void CRdSelfTestWindow::onPaint(HDC hdc)
{
    if (m_decoder == NULL || !m_decoder->ready())
        return;

    RECT rc;
    ::GetClientRect(m_hWnd, &rc);
    //16 位色缩放用半色调模式，画面更平滑
    ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, NULL);
    m_decoder->drawTo(hdc, rc);
}

void CRdSelfTestWindow::onLButtonDown(int clientX, int clientY)
{
    if (m_decoder == NULL || !m_decoder->ready())
        return;

    RECT rc;
    ::GetClientRect(m_hWnd, &rc);

    //自环场景下被控端即本机，屏幕尺寸取解码器当前画面尺寸
    int remoteX = 0;
    int remoteY = 0;
    RdInputEncoder::mapClientToRemote(clientX, clientY,
        rc.right - rc.left, rc.bottom - rc.top,
        m_decoder->width(), m_decoder->height(), remoteX, remoteY);

    //编码成鼠标移动命令再走注入器，与端到端时走的是同一条编码 / 注入路径
    std::vector<BYTE> cmd;
    RdInputEncoder::encodeMouse(cmd, WM_MOUSEMOVE, remoteX, remoteY, 0);
    RdInputInjector::inject(cmd.data(), cmd.size());
}
