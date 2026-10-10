/**
 *  屏幕共享发起人本机的醒目边框，ShareBorderWindow.cpp
 *  说明见 ShareBorderWindow.h。
 *
 *  balloonwj@qq.com   2026-10-02（2026-10-03 改为颜色键分层窗口，见头文件）
 */
#include "stdafx.h"

#include "ShareBorderWindow.h"

#include <string.h>
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

namespace {

//边框粗细，单位像素
const int kBorderThickness = 4;
//边框颜色（绿色，表示「正在共享」）
const COLORREF kBorderColor = RGB(52, 199, 89);
//透明色键：窗口里这个颜色的像素完全透明、点击穿透。取一个界面里几乎不会用到的颜色，且不能与边框颜色相同
const COLORREF kTransparentKey = RGB(1, 0, 1);

bool sameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

} // namespace

CShareBorderWindow::CShareBorderWindow()
    : m_insideEdges(false)
    , m_hidden(false)
{
    memset(&m_rect, 0, sizeof(m_rect));
}

CShareBorderWindow::~CShareBorderWindow()
{
    Hide();
}

void CShareBorderWindow::Show(const RECT& rect, bool insideEdges)
{
    m_rect = rect;
    m_insideEdges = insideEdges;
    m_hidden = false;
    if (!IsWindow())
    {
        const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        if (Create(NULL, CWindow::rcDefault, _T("FlamingoShareBorder"), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("screenshare: create border window failed, error=%u.", static_cast<unsigned>(::GetLastError()));
            return;
        }
        ::SetLayeredWindowAttributes(m_hWnd, kTransparentKey, 0, LWA_COLORKEY);
        //边框本身不能被采进共享画面；在显示之前设置
        ExcludeWindowFromCapture(m_hWnd);
    }
    redraw();
}

void CShareBorderWindow::Follow(const RECT& rect, bool hidden)
{
    if (!IsWindow())
        return;
    if (hidden != m_hidden)
    {
        m_hidden = hidden;
        ShowWindow(hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
    }
    if (!hidden && !sameRect(rect, m_rect))
    {
        m_rect = rect;
        redraw();
    }
}

void CShareBorderWindow::Hide()
{
    if (IsWindow())
        DestroyWindow();
}

LRESULT CShareBorderWindow::OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return HTTRANSPARENT;
}

LRESULT CShareBorderWindow::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //整个客户区在 WM_PAINT 里一次画完，不擦背景，避免闪烁
    return 1;
}

LRESULT CShareBorderWindow::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(&ps);
    RECT rc;
    GetClientRect(&rc);
    //中间涂成透明色键（完全透明），四条边涂成边框颜色
    HBRUSH key = ::CreateSolidBrush(kTransparentKey);
    ::FillRect(dc, &rc, key);
    ::DeleteObject(key);
    HBRUSH edge = ::CreateSolidBrush(kBorderColor);
    const RECT top = { rc.left, rc.top, rc.right, rc.top + kBorderThickness };
    const RECT bottom = { rc.left, rc.bottom - kBorderThickness, rc.right, rc.bottom };
    const RECT left = { rc.left, rc.top, rc.left + kBorderThickness, rc.bottom };
    const RECT right = { rc.right - kBorderThickness, rc.top, rc.right, rc.bottom };
    ::FillRect(dc, &top, edge);
    ::FillRect(dc, &bottom, edge);
    ::FillRect(dc, &left, edge);
    ::FillRect(dc, &right, edge);
    ::DeleteObject(edge);
    EndPaint(&ps);
    return 0;
}

void CShareBorderWindow::redraw()
{
    if (!IsWindow())
        return;
    RECT outer = m_rect;
    if (!m_insideEdges)
        ::InflateRect(&outer, kBorderThickness, kBorderThickness);
    const int w = outer.right - outer.left;
    const int h = outer.bottom - outer.top;
    if (w <= 2 * kBorderThickness || h <= 2 * kBorderThickness)
        return;
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, outer.left, outer.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Invalidate(FALSE);
}

} // namespace screenshare
