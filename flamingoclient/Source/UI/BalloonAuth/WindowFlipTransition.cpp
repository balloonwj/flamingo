/**
 *  WindowFlipTransition.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "WindowFlipTransition.h"

#include <string.h>
#include <dwmapi.h>

#include "../../Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"

#pragma comment(lib, "dwmapi.lib")

namespace balloonauth {

namespace {

const wchar_t* const kFlipWindowClass = L"FlamingoWindowFlip";   // 过渡窗口的窗口类名
const UINT kSpiGetClientAreaAnimation = 0x1042;   // SPI_GETCLIENTAREAANIMATION（Vista 起；老 SDK 头文件按版本宏屏蔽了它）
const DWORD kFallbackFrameMs = 15;                // 桌面合成不可用（DwmFlush 失败）时两帧之间睡眠的毫秒数，约 60 帧每秒
const int kMaxFlipFrames = 240;                   // 一次翻转最多推多少帧：时钟异常时也保证循环结束（350 毫秒正常约 21 帧）
const uint32_t kOpaqueAlphaBits = 0xFF000000u;    // 预乘 BGRA 里 alpha 为 255 的那一字节
const int kBitsPerPixel = 32;                     // DIB 每像素位数
const double kMsPerSecond = 1000.0;               // 秒与毫秒的换算
const int kBytesPerPixel = 4;                     // 每像素字节数（32 位）
const BYTE kFullConstantAlpha = 255;              // UpdateLayeredWindow 的整体不透明度：不额外变淡，透明度全由像素的 alpha 决定

// 注册过渡窗口的窗口类（进程内只需一次；已注册时直接返回 true）
bool EnsureFlipWindowClass()
{
    HINSTANCE instance = ::GetModuleHandleW(NULL);
    WNDCLASSEXW existing;
    ::ZeroMemory(&existing, sizeof(existing));
    existing.cbSize = sizeof(existing);
    if (::GetClassInfoExW(instance, kFlipWindowClass, &existing))
    {
        return true;
    }
    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ::DefWindowProcW;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kFlipWindowClass;
    return ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// 建一个 32 位自上而下的 DIB 并选入一个新的内存设备上下文；失败时什么都不留下
bool CreateDibDC(int width, int height, HDC& dc, HBITMAP& dib, HGDIOBJ& oldBitmap, void*& bits)
{
    dc = NULL;
    dib = NULL;
    oldBitmap = NULL;
    bits = NULL;
    BITMAPINFO bmi;
    ::ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;   // 负高度：自上而下，与 FlipBitmap 的行序一致
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = kBitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = ::GetDC(NULL);
    if (screen == NULL)
    {
        return false;
    }
    dc = ::CreateCompatibleDC(screen);
    dib = ::CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    if (dc == NULL || dib == NULL || bits == NULL)
    {
        if (dib != NULL)
        {
            ::DeleteObject(dib);
        }
        if (dc != NULL)
        {
            ::DeleteDC(dc);
        }
        dc = NULL;
        dib = NULL;
        bits = NULL;
        return false;
    }
    oldBitmap = ::SelectObject(dc, dib);
    return true;
}

// 释放 CreateDibDC 建出的资源
void DestroyDibDC(HDC& dc, HBITMAP& dib, HGDIOBJ& oldBitmap, void*& bits)
{
    if (dc != NULL)
    {
        if (oldBitmap != NULL)
        {
            ::SelectObject(dc, oldBitmap);
        }
        ::DeleteDC(dc);
    }
    if (dib != NULL)
    {
        ::DeleteObject(dib);
    }
    dc = NULL;
    dib = NULL;
    oldBitmap = NULL;
    bits = NULL;
}

// 显示并激活窗口，并立即画好（销毁过渡窗口之前目标窗口的画面就已就位，不会闪一下空白）
void ShowAndActivate(HWND hwnd)
{
    ::ShowWindow(hwnd, SW_SHOW);
    ::SetForegroundWindow(hwnd);
    ::UpdateWindow(hwnd);
}

// 通知观察者某一步已完成（观察者为空时什么也不做）
void NotifyStep(IFlipObserver* observer, FlipStep step)
{
    if (observer != NULL)
    {
        observer->OnFlipStep(step);
    }
}

// 关掉或恢复一个窗口的 DWM 窗口动画（默认实现）。桌面合成不可用时调用失败，只记日志，不影响切换
void SwitchDwmTransitions(HWND hwnd, bool disabled)
{
    BOOL value = disabled ? TRUE : FALSE;
    const HRESULT hr = ::DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &value, sizeof(value));
    if (FAILED(hr))
    {
        LOG_WARNING("window flip: turning DWM transitions %s failed, hwnd=%p, hr=0x%08X.", disabled ? "off" : "on",
                    hwnd, static_cast<unsigned>(hr));
    }
}

// 当前使用的开关函数；测试可经 SetFlipTransitionSwitchForTest 替换。只在界面线程读写
FlipTransitionSwitchFn g_transitionSwitch = &SwitchDwmTransitions;

} // namespace

bool IsFlipAnimationEnabled()
{
    BOOL enabled = TRUE;
    if (!::SystemParametersInfoW(kSpiGetClientAreaAnimation, 0, &enabled, 0))
    {
        return true;
    }
    return enabled != FALSE;
}

bool CaptureDuiHostImage(balloonwjui::DuiHost& host, FlipBitmap& out)
{
    HWND hwnd = host.m_hWnd;
    if (hwnd == NULL || !::IsWindow(hwnd))
    {
        return false;
    }
    RECT rc;
    ::GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return false;
    }
    //让 DuiHost 把整个客户区画进后台缓冲：先把整个客户区标为待重画，再直接发 WM_PAINT。
    //窗口隐藏时系统给出的待重画区域可能为空，DuiHost::OnPaint 那时同样整块重画（见头文件说明）
    ::InvalidateRect(hwnd, NULL, FALSE);
    ::SendMessageW(hwnd, WM_PAINT, 0, 0);
    HDC src = host.GetBackBufferDC();
    if (src == NULL)
    {
        LOG_WARNING("window flip: no back buffer to capture, hwnd=%p.", hwnd);
        return false;
    }
    HDC dc = NULL;
    HBITMAP dib = NULL;
    HGDIOBJ oldBitmap = NULL;
    void* bits = NULL;
    if (!CreateDibDC(w, h, dc, dib, oldBitmap, bits))
    {
        LOG_WARNING("window flip: CreateDIBSection failed for %dx%d.", w, h);
        return false;
    }
    const BOOL copied = ::BitBlt(dc, 0, 0, w, h, src, 0, 0, SRCCOPY);
    ::GdiFlush();
    if (copied)
    {
        out.m_width = w;
        out.m_height = h;
        out.m_pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        const uint32_t* p = static_cast<const uint32_t*>(bits);
        for (size_t i = 0; i < out.m_pixels.size(); ++i)
        {
            //窗口画面不透明：GDI 绘制不维护 alpha 字节，一律补成 255
            out.m_pixels[i] = p[i] | kOpaqueAlphaBits;
        }
    }
    DestroyDibDC(dc, dib, oldBitmap, bits);
    return copied != FALSE;
}

CWindowFlipTransition::CWindowFlipTransition()
    : m_hwnd(NULL)
    , m_memDC(NULL)
    , m_dib(NULL)
    , m_oldBitmap(NULL)
    , m_dibBits(NULL)
    , m_from(NULL)
    , m_to(NULL)
    , m_dir(kFlipForward)
{
    ::ZeroMemory(&m_fromRect, sizeof(m_fromRect));
    ::ZeroMemory(&m_toRect, sizeof(m_toRect));
    ::ZeroMemory(&m_canvasRect, sizeof(m_canvasRect));
}

CWindowFlipTransition::~CWindowFlipTransition()
{
    End();
}

HWND CWindowFlipTransition::Hwnd() const
{
    return m_hwnd;
}

bool CWindowFlipTransition::Begin(HWND owner, const FlipBitmap& from, const RECT& fromRect, const FlipBitmap& to,
                                  const RECT& toRect, FlipDirection dir)
{
    End();
    m_from = &from;
    m_to = &to;
    m_fromRect = fromRect;
    m_toRect = toRect;
    m_dir = dir;
    m_canvasRect = FlipCanvasRect(fromRect, toRect);
    const int cw = m_canvasRect.right - m_canvasRect.left;
    const int ch = m_canvasRect.bottom - m_canvasRect.top;
    if (cw <= 0 || ch <= 0)
    {
        return false;
    }
    m_canvas.m_width = cw;
    m_canvas.m_height = ch;
    if (!CreateDibDC(cw, ch, m_memDC, m_dib, m_oldBitmap, m_dibBits))
    {
        LOG_WARNING("window flip: transition bitmap %dx%d could not be created.", cw, ch);
        End();
        return false;
    }
    if (!EnsureFlipWindowClass())
    {
        LOG_WARNING("window flip: window class registration failed, error=%u.", (unsigned)::GetLastError());
        End();
        return false;
    }
    //分层弹出窗口，不进任务栏；内容全部由 UpdateLayeredWindow 提供
    m_hwnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kFlipWindowClass, L"", WS_POPUP,
                               m_canvasRect.left, m_canvasRect.top, cw, ch, owner, NULL, ::GetModuleHandleW(NULL), NULL);
    if (m_hwnd == NULL)
    {
        LOG_WARNING("window flip: transition window could not be created, error=%u.", (unsigned)::GetLastError());
        End();
        return false;
    }
    //第一帧：起始画面原样大小，与起始窗口逐像素相同，盖上去时看不出变化
    if (!PushFrame(0.0f, NULL))
    {
        LOG_WARNING("window flip: UpdateLayeredWindow failed, error=%u.", (unsigned)::GetLastError());
        End();
        return false;
    }
    //显示并激活：随后隐藏起始窗口时前台留在过渡窗口上，不会落到别的程序上
    ::SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hwnd);
    return true;
}

bool CWindowFlipTransition::PushFrame(float t, IFlipObserver* observer)
{
    if (m_hwnd == NULL || m_from == NULL || m_to == NULL || m_dibBits == NULL)
    {
        return false;
    }
    const FlipFrame f = ComputeFlipFrame(m_fromRect, m_toRect, t, m_dir);
    RenderFlipFrame(f.m_showTarget ? *m_to : *m_from, f, m_canvas);
    if (!m_canvas.m_pixels.empty())
    {
        memcpy(m_dibBits, &m_canvas.m_pixels[0], m_canvas.m_pixels.size() * kBytesPerPixel);
    }
    ::GdiFlush();
    POINT dst;
    dst.x = m_canvasRect.left;
    dst.y = m_canvasRect.top;
    SIZE size;
    size.cx = m_canvas.m_width;
    size.cy = m_canvas.m_height;
    POINT srcPt;
    srcPt.x = 0;
    srcPt.y = 0;
    BLENDFUNCTION blend;
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = kFullConstantAlpha;
    blend.AlphaFormat = AC_SRC_ALPHA;
    const BOOL ok = ::UpdateLayeredWindow(m_hwnd, NULL, &dst, &size, m_memDC, &srcPt, 0, &blend, ULW_ALPHA);
    if (observer != NULL)
    {
        observer->OnFlipFrame(t, m_canvas);
    }
    return ok != FALSE;
}

void CWindowFlipTransition::Run(IFlipObserver* observer, uint32_t durationMs)
{
    if (m_hwnd == NULL)
    {
        return;
    }
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
    const bool hasQpc = ::QueryPerformanceFrequency(&freq) && freq.QuadPart > 0 && ::QueryPerformanceCounter(&start);
    const ULONGLONG startTick = ::GetTickCount64();
    float t = 0.0f;
    int frames = 0;
    for (; frames < kMaxFlipFrames && t < 1.0f; ++frames)
    {
        uint64_t elapsedMs = 0;
        LARGE_INTEGER now;
        if (hasQpc && ::QueryPerformanceCounter(&now))
        {
            elapsedMs = static_cast<uint64_t>(static_cast<double>(now.QuadPart - start.QuadPart) * kMsPerSecond /
                                              static_cast<double>(freq.QuadPart));
        }
        else
        {
            elapsedMs = ::GetTickCount64() - startTick;
        }
        t = FlipProgress(elapsedMs, durationMs);
        PushFrame(t, observer);
        if (t >= 1.0f)
        {
            break;
        }
        //等下一次桌面合成再推下一帧；合成不可用时按约 60 帧每秒睡眠
        if (FAILED(::DwmFlush()))
        {
            ::Sleep(kFallbackFrameMs);
        }
    }
    if (t < 1.0f)
    {
        //帧数上限先到（时钟异常）：补推最后一帧，保证结束时画的是目标画面
        PushFrame(1.0f, observer);
    }
    LOG_INFO("window flip: played %d frame(s) in %u ms.", frames + 1, (unsigned)(::GetTickCount64() - startTick));
}

void CWindowFlipTransition::End()
{
    if (m_hwnd != NULL)
    {
        ::DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
    DestroyDibDC(m_memDC, m_dib, m_oldBitmap, m_dibBits);
    m_from = NULL;
    m_to = NULL;
}

FlipTransitionSwitchFn SetFlipTransitionSwitchForTest(FlipTransitionSwitchFn fn)
{
    const FlipTransitionSwitchFn previous = g_transitionSwitch;
    g_transitionSwitch = (fn != NULL) ? fn : &SwitchDwmTransitions;
    return previous;
}

bool SwitchDuiWindows(balloonwjui::DuiHost& from, balloonwjui::DuiHost& to, FlipDirection dir, bool animate,
                      IFlipObserver* observer)
{
    HWND fromWnd = from.m_hWnd;
    HWND toWnd = to.m_hWnd;
    if (animate && ::IsWindow(fromWnd) && ::IsWindowVisible(fromWnd) && !::IsIconic(fromWnd) && ::IsWindow(toWnd))
    {
        FlipBitmap fromImage;
        FlipBitmap toImage;
        RECT fromRect;
        RECT toRect;
        ::GetWindowRect(fromWnd, &fromRect);
        ::GetWindowRect(toWnd, &toRect);
        if (CaptureDuiHostImage(from, fromImage) && CaptureDuiHostImage(to, toImage))
        {
            CWindowFlipTransition flip;
            if (flip.Begin(::GetWindow(fromWnd, GW_OWNER), fromImage, fromRect, toImage, toRect, dir))
            {
                //目标窗口第一次显示时系统会播放约 300 毫秒的淡入放大动画，而过渡窗口在它显示后立即销毁，那块区域会先
                //露出后面的桌面、再浮现出窗口，看上去像闪了一下（2026-10-07 逐帧截屏实证，见 WindowFlipTests.cpp 的 WFC）。
                //所以显示之前关掉它的 DWM 窗口动画，显示后立即恢复；同一实验验证过立即恢复不会让动画重新出现，
                //最小化、关闭等其它系统动画也就不受影响
                g_transitionSwitch(toWnd, true);
                NotifyStep(observer, kFlipStepCovered);
                ::ShowWindow(fromWnd, SW_HIDE);
                NotifyStep(observer, kFlipStepSourceHidden);
                flip.Run(observer, kFlipDurationMs);
                ShowAndActivate(toWnd);
                g_transitionSwitch(toWnd, false);
                NotifyStep(observer, kFlipStepTargetShown);
                flip.End();
                NotifyStep(observer, kFlipStepFinished);
                return true;
            }
        }
        LOG_WARNING("window flip: capture or transition window failed, switching directly (from=%p, to=%p).",
                    fromWnd, toWnd);
    }
    //直接切换：先显示目标窗口再隐藏起始窗口，中间不会出现两个窗口都看不见的时刻
    ShowAndActivate(toWnd);
    NotifyStep(observer, kFlipStepTargetShown);
    ::ShowWindow(fromWnd, SW_HIDE);
    NotifyStep(observer, kFlipStepSourceHidden);
    NotifyStep(observer, kFlipStepFinished);
    return false;
}

} // namespace balloonauth
