/**
 *  发起人本机的标注层，说明见 ShareAnnotationOverlay.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareAnnotationOverlay.h"

#include <windowsx.h>
#include "ShareAnnotationRender.h"
#include "ShareAnnotationSession.h"
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

namespace {

//显示层的键色：这个颜色的像素完全透明（与边框窗口同一个取值，笔迹的颜色里没有它）
const COLORREF kLayerKeyColor = RGB(1, 0, 1);
//不要重定向位图的扩展样式 WS_EX_NOREDIRECTIONBITMAP（Windows 8 起；本工程按 Windows 7 编译，WinUser.h 不定义它）
const DWORD kExNoRedirectionBitmap = 0x00200000L;
//逐像素透明显示层的整窗不透明度：全不透明，透明与否只看每个像素
const BYTE kLayerAlpha = 255;
//32 位 DIB 的位深与每像素字节数
const WORD kLayerBitsPerPixel = 32;
const int kLayerBytesPerPixel = 4;
//输入层的整窗不透明度：1/255，肉眼看不见，但仍能接住鼠标
const BYTE kCatcherAlpha = 1;

} // namespace

//==================== CShareAnnotationLayer ====================

CShareAnnotationLayer::CShareAnnotationLayer(const ShareAnnotationSession* session)
    : m_session(session)
    , m_canvas(kAnnotDefaultCanvas)
{
}

bool CShareAnnotationLayer::AttachAlpha()
{
    return IsWindow() && m_composition.Attach(m_hWnd);
}

bool CShareAnnotationLayer::IsAlpha() const
{
    return m_composition.IsAttached();
}

LRESULT CShareAnnotationLayer::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    m_composition.Detach();
    bHandled = FALSE;
    return 0;
}

void CShareAnnotationLayer::paintAlpha(int width, int height)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kLayerBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL || dib == NULL || bits == NULL)
    {
        if (dib != NULL)
            ::DeleteObject(dib);
        if (mem != NULL)
            ::DeleteDC(mem);
        return;
    }
    //全透明的底（DIB 初始为 0）上合成笔迹：AlphaBlend 写进 32 位目标时连透明度一起写，结果就是预乘透明度的图层
    memset(bits, 0, static_cast<size_t>(width) * height * kLayerBytesPerPixel);
    HGDIOBJ old = ::SelectObject(mem, dib);
    const RECT rc = { 0, 0, width, height };
    if (m_session != NULL)
        DrawSessionCanvas(mem, rc, *m_session, m_canvas, false, static_cast<int64_t>(::GetTickCount64()));
    ::GdiFlush();
    m_composition.Present(static_cast<const uint8_t*>(bits), width, height);
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
}

void CShareAnnotationLayer::SetCanvas(int canvas)
{
    m_canvas = canvas;
}

LRESULT CShareAnnotationLayer::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return 1;
}

LRESULT CShareAnnotationLayer::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(&ps);
    RECT rc;
    GetClientRect(&rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    //逐像素透明：窗口没有重定向位图，内容整帧交给 DirectComposition
    if (m_composition.IsAttached())
    {
        if (w > 0 && h > 0)
            paintAlpha(w, h);
        EndPaint(&ps);
        return 0;
    }
    if (w > 0 && h > 0)
    {
        //先画进内存位图再一次拷上去，避免闪烁
        HDC mem = ::CreateCompatibleDC(hdc);
        HBITMAP bmp = ::CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = ::SelectObject(mem, bmp);
        HBRUSH key = ::CreateSolidBrush(kLayerKeyColor);
        ::FillRect(mem, &rc, key);
        ::DeleteObject(key);
        //这块画面上的笔迹、别人的画笔位置与名字，本人当前画面是它时再加正在画的预览与选中框（第 22 期 22e）
        if (m_session != NULL)
            DrawSessionCanvas(mem, rc, *m_session, m_canvas, true, static_cast<int64_t>(::GetTickCount64()));
        ::BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        ::SelectObject(mem, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
    }
    EndPaint(&ps);
    return 0;
}

//==================== CShareAnnotationCatcher ====================

CShareAnnotationCatcher::CShareAnnotationCatcher()
    : m_input(NULL)
    , m_pressed(false)
    , m_tracking(false)
    , m_canvas(kAnnotDefaultCanvas)
{
    m_last.x = 0;
    m_last.y = 0;
}

void CShareAnnotationCatcher::SetInput(IShareAnnotationInput* input)
{
    m_input = input;
}

void CShareAnnotationCatcher::SetCanvas(int canvas)
{
    m_canvas = canvas;
}

void CShareAnnotationCatcher::deliver(int phase, LPARAM lParam)
{
    POINT client = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    m_last = client;
    if (m_input == NULL)
        return;
    RECT rc;
    GetClientRect(&rc);
    POINT screen = client;
    ClientToScreen(&screen);
    const double aspect = (rc.bottom > rc.top) ? static_cast<double>(rc.right - rc.left) / (rc.bottom - rc.top) : 1.0;
    //先告诉接收方动作落在哪块画面上（第 22 期 22e）：同一个接收方可能还接着别的画面（如观看窗）
    m_input->OnAnnotCanvas(m_canvas);
    m_input->OnAnnotPointer(phase, ScreenToAnnot(rc, client), AnnotTolerance(rc, kAnnotEraserTolerancePx), aspect, screen);
}

LRESULT CShareAnnotationCatcher::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    m_pressed = true;
    SetCapture();
    deliver(annot_pointer_down, lParam);
    return 0;
}

LRESULT CShareAnnotationCatcher::OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    //请求光标离开通知：离开时告诉房间里的人不再显示本人的画笔（第 19 期）
    if (!m_tracking)
    {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        tme.dwHoverTime = 0;
        m_tracking = (::TrackMouseEvent(&tme) != FALSE);
    }
    deliver(m_pressed ? annot_pointer_move : annot_pointer_hover, lParam);
    return 0;
}

LRESULT CShareAnnotationCatcher::OnFinishClick(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    deliver(annot_pointer_finish, lParam);
    return 0;
}

LRESULT CShareAnnotationCatcher::OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    m_tracking = false;
    deliver(annot_pointer_leave, MAKELPARAM(m_last.x, m_last.y));
    return 0;
}

LRESULT CShareAnnotationCatcher::OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    if (!m_pressed)
        return 0;
    m_pressed = false;
    deliver(annot_pointer_up, lParam);
    if (::GetCapture() == m_hWnd)
        ::ReleaseCapture();
    return 0;
}

LRESULT CShareAnnotationCatcher::OnCaptureChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //捕获被别的窗口拿走（如弹出了提示框）：按最后的位置收尾，这一笔不会一直停在「按着」的状态
    if (m_pressed)
    {
        m_pressed = false;
        deliver(annot_pointer_up, MAKELPARAM(m_last.x, m_last.y));
    }
    return 0;
}

LRESULT CShareAnnotationCatcher::OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //当前工具的光标（第 19 期）；取不到时用十字
    HCURSOR cursor = (m_input != NULL) ? m_input->AnnotCursor() : NULL;
    ::SetCursor(cursor != NULL ? cursor : ::LoadCursor(NULL, IDC_CROSS));
    return TRUE;
}

LRESULT CShareAnnotationCatcher::OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //点击不激活：被共享的程序保持前台
    return MA_NOACTIVATE;
}

//==================== CShareAnnotationOverlay ====================

CShareAnnotationOverlay::CShareAnnotationOverlay(const ShareAnnotationSession* session)
    : m_layer(session)
    , m_visible(false)
    , m_inputOn(false)
{
    memset(&m_rect, 0, sizeof(m_rect));
}

CShareAnnotationOverlay::~CShareAnnotationOverlay()
{
    Close();
}

bool CShareAnnotationOverlay::createAlphaLayer()
{
    //鼠标完全穿透、不抢焦点；没有重定向位图，内容只来自 DirectComposition
    const DWORD ex = kExNoRedirectionBitmap | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                     WS_EX_NOACTIVATE;
    if (m_layer.Create(NULL, CWindow::rcDefault, _T("FlamingoShareAnnotation"), WS_POPUP, ex) == NULL)
        return false;
    ::SetLayeredWindowAttributes(m_layer.m_hWnd, 0, kLayerAlpha, LWA_ALPHA);
    if (!m_layer.AttachAlpha())
    {
        m_layer.DestroyWindow();
        return false;
    }
    //排除不了采集就不能用：笔迹会被采进共享画面，观看者那边又各自画一遍
    if (!ExcludeWindowFromCapture(m_layer.m_hWnd))
    {
        LOG_WARNING("screenshare: alpha annotation layer cannot be excluded from capture, fall back to color key.");
        m_layer.DestroyWindow();
        return false;
    }
    LOG_INFO("screenshare: annotation layer uses per-pixel alpha (DirectComposition).");
    return true;
}

void CShareAnnotationOverlay::createColorKeyLayer()
{
    const DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (m_layer.Create(NULL, CWindow::rcDefault, _T("FlamingoShareAnnotation"), WS_POPUP, ex) == NULL)
    {
        LOG_ERROR("screenshare: create annotation layer failed.");
        return;
    }
    ::SetLayeredWindowAttributes(m_layer.m_hWnd, kLayerKeyColor, 0, LWA_COLORKEY);
    if (!ExcludeWindowFromCapture(m_layer.m_hWnd))
        LOG_WARNING("screenshare: annotation layer not excluded from capture.");
}

void CShareAnnotationOverlay::ensureWindows()
{
    if (!m_layer.IsWindow())
    {
        if (!createAlphaLayer())
            createColorKeyLayer();
    }
    if (!m_catcher.IsWindow())
    {
        const DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        if (m_catcher.Create(NULL, CWindow::rcDefault, _T("FlamingoShareAnnotationInput"), WS_POPUP, ex) == NULL)
        {
            LOG_ERROR("screenshare: create annotation input layer failed.");
            return;
        }
        ::SetLayeredWindowAttributes(m_catcher.m_hWnd, 0, kCatcherAlpha, LWA_ALPHA);
        //1/255 的不透明度即使被采进画面也看不出来；排除失败只记日志
        if (!ExcludeWindowFromCapture(m_catcher.m_hWnd))
            LOG_WARNING("screenshare: annotation input layer not excluded from capture.");
    }
}

void CShareAnnotationOverlay::Place(const RECT& rect, bool visible)
{
    m_rect = rect;
    m_visible = visible;
    ensureWindows();
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    //已经显示的窗口只改位置与大小、不动叠放次序：控制条、标注工具条等置顶小窗要保持在输入层之上，否则点不到
    if (m_layer.IsWindow())
    {
        if (visible && w > 0 && h > 0)
        {
            const UINT z = m_layer.IsWindowVisible() ? SWP_NOZORDER : 0;
            ::SetWindowPos(m_layer.m_hWnd, HWND_TOPMOST, rect.left, rect.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW | z);
            m_layer.Invalidate(FALSE);
        }
        else
        {
            m_layer.ShowWindow(SW_HIDE);
        }
    }
    if (m_catcher.IsWindow())
    {
        if (visible && m_inputOn && w > 0 && h > 0)
        {
            const UINT z = m_catcher.IsWindowVisible() ? SWP_NOZORDER : 0;
            ::SetWindowPos(m_catcher.m_hWnd, HWND_TOPMOST, rect.left, rect.top, w, h,
                           SWP_NOACTIVATE | SWP_SHOWWINDOW | z);
        }
        else
        {
            m_catcher.ShowWindow(SW_HIDE);
        }
    }
}

void CShareAnnotationOverlay::SetInputMode(bool on, IShareAnnotationInput* input)
{
    m_inputOn = on;
    m_catcher.SetInput(on ? input : NULL);
    Place(m_rect, m_visible);
}

void CShareAnnotationOverlay::SetCanvas(int canvas)
{
    m_layer.SetCanvas(canvas);
    m_catcher.SetCanvas(canvas);
    Redraw();
}

void CShareAnnotationOverlay::Redraw()
{
    if (m_layer.IsWindow() && m_layer.IsWindowVisible())
    {
        m_layer.Invalidate(FALSE);
        m_layer.UpdateWindow();
    }
}

void CShareAnnotationOverlay::Close()
{
    m_inputOn = false;
    m_visible = false;
    m_catcher.SetInput(NULL);
    if (m_catcher.IsWindow())
        m_catcher.DestroyWindow();
    if (m_layer.IsWindow())
        m_layer.DestroyWindow();
}

const RECT& CShareAnnotationOverlay::Rect() const
{
    return m_rect;
}

bool CShareAnnotationOverlay::IsAlphaLayer() const
{
    return m_layer.IsWindow() && m_layer.IsAlpha();
}

} // namespace screenshare
