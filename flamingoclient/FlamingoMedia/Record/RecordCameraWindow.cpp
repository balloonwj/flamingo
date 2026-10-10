/**
 *  录制中的摄像头小窗，说明见 RecordCameraWindow.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "RecordCameraWindow.h"

#include <string.h>
#include "../Share/ShareSources.h"            // ExcludeWindowFromCapture
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"    // TR：界面文字的多语言查找

namespace screenrecord {

namespace {

//取帧定时器的 id
const UINT_PTR kFrameTimerId = 1;
//外圈白边的粗细（像素），与录像里画中画的白边一致
const int kBorderPx = 2;
//还没有画面时的提示文字颜色
const COLORREF kPlaceholderColor = RGB(160, 164, 172);
//32 位 DIB 的位深与每像素字节数
const WORD kBitsPerPixel32 = 32;

} // namespace

CRecordCameraWindow::CRecordCameraWindow()
    : m_source(NULL)
    , m_sink(NULL)
    , m_frameW(0)
    , m_frameH(0)
    , m_serial(0)
{
}

CRecordCameraWindow::~CRecordCameraWindow()
{
    Hide();
}

bool CRecordCameraWindow::Show(const RECT& screenRect, IRecordVideoSource* source, IRecordCameraWindowSink* sink)
{
    //建窗之前先记下接收方：建窗与摆位时的 WM_MOVE 也要通知
    m_source = source;
    m_sink = sink;
    if (!IsWindow())
    {
        m_frame.clear();
        m_frameW = 0;
        m_frameH = 0;
        m_serial = 0;
        //置顶、不进任务栏、点击时不激活
        const DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        RECT rc = screenRect;
        if (Create(NULL, rc, _T("FlamingoRecordCamera"), WS_POPUP, ex) == NULL)
        {
            LOG_ERROR("record: create camera window failed.");
            return false;
        }
        //小窗本身不能被录进去，录像里的画中画由合成器按它的位置另画
        if (!screenshare::ExcludeWindowFromCapture(m_hWnd))
            LOG_WARNING("record: camera window not excluded from capture.");
        SetTimer(kFrameTimerId, kRecordCameraFrameMs, NULL);
    }
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, screenRect.left, screenRect.top, screenRect.right - screenRect.left,
                   screenRect.bottom - screenRect.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    LOG_INFO("record: camera window shown at %d,%d %dx%d.", static_cast<int>(screenRect.left),
             static_cast<int>(screenRect.top), static_cast<int>(screenRect.right - screenRect.left),
             static_cast<int>(screenRect.bottom - screenRect.top));
    return true;
}

void CRecordCameraWindow::Hide()
{
    m_sink = NULL;
    m_source = NULL;
    if (IsWindow())
    {
        KillTimer(kFrameTimerId);
        DestroyWindow();
    }
    m_frame.clear();
    m_frameW = 0;
    m_frameH = 0;
}

bool CRecordCameraWindow::IsShown() const
{
    return IsWindow() && IsWindowVisible();
}

RECT CRecordCameraWindow::ScreenRect() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (IsWindow())
        GetWindowRect(&rc);
    return rc;
}

LRESULT CRecordCameraWindow::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
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
    if (w > 0 && h > 0)
    {
        //先画进内存位图再一次拷上去，避免闪烁
        HDC mem = ::CreateCompatibleDC(hdc);
        HBITMAP bmp = ::CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = ::SelectObject(mem, bmp);
        ::FillRect(mem, &rc, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
        RECT inner = { kBorderPx, kBorderPx, w - kBorderPx, h - kBorderPx };
        ::FillRect(mem, &inner, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
        const int boxW = inner.right - inner.left;
        const int boxH = inner.bottom - inner.top;
        if (m_frameW > 0 && m_frameH > 0 && !m_frame.empty() && boxW > 0 && boxH > 0)
        {
            //按画面自身的宽高比放进框里，与录像里画中画的摆法一致（4:3 的摄像头左右留黑边）
            int dw = boxW;
            int dh = static_cast<int>(static_cast<int64_t>(m_frameH) * boxW / m_frameW);
            if (dh > boxH)
            {
                dh = boxH;
                dw = static_cast<int>(static_cast<int64_t>(m_frameW) * boxH / m_frameH);
            }
            BITMAPINFO bi;
            memset(&bi, 0, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = m_frameW;
            bi.bmiHeader.biHeight = -m_frameH;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = kBitsPerPixel32;
            bi.bmiHeader.biCompression = BI_RGB;
            ::SetStretchBltMode(mem, HALFTONE);
            ::SetBrushOrgEx(mem, 0, 0, NULL);
            ::StretchDIBits(mem, inner.left + (boxW - dw) / 2, inner.top + (boxH - dh) / 2, dw, dh, 0, 0, m_frameW,
                            m_frameH, &m_frame[0], &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        else
        {
            //还没有画面：居中写「摄像头」
            ::SetBkMode(mem, TRANSPARENT);
            ::SetTextColor(mem, kPlaceholderColor);
            HGDIOBJ oldFont = ::SelectObject(mem, ::GetStockObject(DEFAULT_GUI_FONT));
            ::DrawTextW(mem, TR(L"摄像头"), -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(mem, oldFont);
        }
        ::BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        ::SelectObject(mem, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
    }
    EndPaint(&ps);
    return 0;
}

LRESULT CRecordCameraWindow::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return 1;
}

LRESULT CRecordCameraWindow::OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return HTCAPTION;
}

LRESULT CRecordCameraWindow::OnMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    if (m_sink != NULL && IsWindow())
    {
        RECT rc;
        GetWindowRect(&rc);
        m_sink->OnCameraWindowMoved(rc);
    }
    return 0;
}

LRESULT CRecordCameraWindow::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kFrameTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    if (m_source == NULL)
        return 0;
    std::vector<uint8_t> frame;
    int w = 0;
    int h = 0;
    uint64_t serial = 0;
    //有新帧才重画
    if (m_source->GetRecordFrame(frame, w, h, serial) && serial != m_serial && w > 0 && h > 0)
    {
        m_frame.swap(frame);
        m_frameW = w;
        m_frameH = h;
        m_serial = serial;
        Invalidate(FALSE);
    }
    return 0;
}

LRESULT CRecordCameraWindow::OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return MA_NOACTIVATE;
}

} // namespace screenrecord
