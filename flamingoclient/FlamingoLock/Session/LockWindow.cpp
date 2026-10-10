/**
 *  LockWindow.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "LockWindow.h"

#include <imm.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "imm32.lib")

namespace flamingolock {

namespace {

const float kRenderTargetDpi = 96.0f;        // 渲染目标的 DPI：设为 96，1 个 DIP 等于 1 个物理像素
const uint32_t kBackgroundColor = 0x05060F;  // 场景缺失或首帧之前的深色底
//WM_SYSCOMMAND 的 wParam 低 4 位由系统内部使用，判断命令前须屏蔽（见 WM_SYSCOMMAND 文档）
const WPARAM kSysCommandMask = 0xFFF0;

} // namespace

CLockWindow::CLockWindow()
    : m_index(-1)
    , m_width(0)
    , m_height(0)
    , m_scale(1.0f)
    , m_sink(NULL)
    , m_sceneInited(false)
    , m_trackingLeave(false)
{
    ::ZeroMemory(&m_monitorRect, sizeof(m_monitorRect));
    m_mouse.x = -1;
    m_mouse.y = -1;
}

CLockWindow::~CLockWindow()
{
    DestroyLockWindow();
}

bool CLockWindow::CreateLockWindow(int index, const RECT& monitorRect, float scale, ILockWindowSink* sink)
{
    m_index = index;
    m_monitorRect = monitorRect;
    m_width = monitorRect.right - monitorRect.left;
    m_height = monitorRect.bottom - monitorRect.top;
    m_scale = (scale > 0.0f) ? scale : 1.0f;
    m_sink = sink;
    RECT rc = monitorRect;
    if (Create(NULL, rc, L"FlamingoLock") == NULL)
    {
        LOG_ERROR("lock window %d: create failed, error=%u.", index, static_cast<unsigned>(::GetLastError()));
        return false;
    }
    //锁屏窗口一律关闭输入法：密码只接受键盘直接输入的 ASCII
    ::ImmAssociateContext(m_hWnd, NULL);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, rc.left, rc.top, m_width, m_height, SWP_SHOWWINDOW);
    LOG_INFO("lock window %d created: (%ld,%ld) %dx%d, scale %.2f.", index, rc.left, rc.top, m_width, m_height,
             static_cast<double>(m_scale));
    return true;
}

void CLockWindow::DestroyLockWindow()
{
    DiscardTarget();
    m_scene.reset();
    m_sceneInited = false;
    if (IsWindow())
    {
        DestroyWindow();
    }
    m_hWnd = NULL;
}

void CLockWindow::SetScene(ILockScene* scene, const SceneEnv& env)
{
    m_scene.reset(scene);
    m_sceneInited = false;
    if (m_scene)
    {
        m_scene->Init(env, static_cast<float>(m_width), static_cast<float>(m_height));
        m_sceneInited = true;
    }
}

bool CLockWindow::EnsureTarget(const SceneEnv& env)
{
    if (m_target != NULL)
    {
        return true;
    }
    if (env.m_d2d == NULL || !IsWindow())
    {
        return false;
    }
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
        kRenderTargetDpi, kRenderTargetDpi);
    //每帧画完不等垂直同步：同一线程上有多块显示器的窗口，节拍由锁屏线程统一用 DwmFlush 对齐
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hWnd, D2D1::SizeU(static_cast<UINT32>(m_width), static_cast<UINT32>(m_height)),
        D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    HRESULT hr = env.m_d2d->CreateHwndRenderTarget(props, hwndProps, &m_target);
    if (FAILED(hr) || m_target == NULL)
    {
        LOG_ERROR("lock window %d: CreateHwndRenderTarget failed, hr=0x%08X.", m_index, static_cast<unsigned>(hr));
        m_target.Release();
        return false;
    }
    m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    m_overlay.Init(env.m_dwrite, m_scale, m_width, m_height);
    return true;
}

void CLockWindow::DiscardTarget()
{
    if (m_scene)
    {
        m_scene->DiscardDeviceResources();
    }
    m_overlay.DiscardDeviceResources();
    m_target.Release();
}

void CLockWindow::Render(const SceneEnv& env, OverlayFrame frame, float dt, uint64_t wallMs)
{
    if (!EnsureTarget(env))
    {
        return;
    }
    if (m_scene)
    {
        m_scene->Update(dt, wallMs);
    }
    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    if (m_scene)
    {
        m_scene->Draw(m_target);
    }
    else
    {
        m_target->Clear(ColorRgb(kBackgroundColor));
    }
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    frame.m_mouse = m_mouse;
    frame.m_showClock = frame.m_showClock && m_scene && m_scene->ShowsCornerClock();
    m_overlay.Draw(m_target, frame);
    HRESULT hr = m_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        LOG_WARNING("lock window %d: render target lost, recreating.", m_index);
        DiscardTarget();
    }
}

const LockOverlayLayout& CLockWindow::Layout() const
{
    return m_overlay.Layout();
}

void CLockWindow::Shake(uint64_t nowMs)
{
    m_overlay.StartShake(nowMs);
}

int CLockWindow::Index() const
{
    return m_index;
}

const RECT& CLockWindow::MonitorRect() const
{
    return m_monitorRect;
}

float CLockWindow::Scale() const
{
    return m_scale;
}

LRESULT CLockWindow::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    if (m_sink != NULL)
    {
        m_sink->OnLockMouseDown(m_index, pt);
    }
    return 0;
}

LRESULT CLockWindow::OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    //合成的零位移移动（防休眠）也会到这里，位置没变就不算用户操作
    const bool moved = (pt.x != m_mouse.x || pt.y != m_mouse.y);
    m_mouse = pt;
    if (!m_trackingLeave)
    {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        tme.dwHoverTime = 0;
        m_trackingLeave = (::TrackMouseEvent(&tme) != FALSE);
    }
    if (moved && m_scene)
    {
        m_scene->OnPointer(static_cast<float>(pt.x), static_cast<float>(pt.y));
    }
    if (moved && m_sink != NULL)
    {
        m_sink->OnLockMouseMove(m_index, pt);
    }
    return 0;
}

LRESULT CLockWindow::OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    m_trackingLeave = false;
    m_mouse.x = -1;
    m_mouse.y = -1;
    if (m_scene)
    {
        m_scene->OnPointer(-1.0f, -1.0f);
    }
    if (m_sink != NULL)
    {
        m_sink->OnLockMouseLeave(m_index);
    }
    return 0;
}

LRESULT CLockWindow::OnChar(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_sink != NULL)
    {
        m_sink->OnLockChar(m_index, static_cast<wchar_t>(wParam));
    }
    return 0;
}

LRESULT CLockWindow::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_sink != NULL)
    {
        m_sink->OnLockKeyDown(m_index, static_cast<UINT>(wParam));
    }
    return 0;
}

LRESULT CLockWindow::OnSysKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //与原先一样不交给系统默认处理（Alt+F4 不会变成关闭命令），只把按键告知接收者（预览据此结束）
    if (m_sink != NULL)
    {
        m_sink->OnLockSysKeyDown(m_index, static_cast<UINT>(wParam));
    }
    return 0;
}

LRESULT CLockWindow::OnOtherButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    if (m_sink != NULL)
    {
        m_sink->OnLockOtherButtonDown(m_index, pt);
    }
    //其余照常交给系统默认处理
    bHandled = FALSE;
    return 0;
}

LRESULT CLockWindow::OnActivate(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (LOWORD(wParam) == WA_INACTIVE && m_sink != NULL)
    {
        m_sink->OnLockDeactivate(m_index);
    }
    //激活与焦点的默认处理不变
    bHandled = FALSE;
    return 0;
}

LRESULT CLockWindow::OnSwallow(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //关闭请求、系统键与系统字符一律忽略：锁屏窗口只能由解锁结束
    return 0;
}

LRESULT CLockWindow::OnSysCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    switch (wParam & kSysCommandMask)
    {
    //关闭、屏保、关闭显示器、最小化、移动、改尺寸：一律忽略
    case SC_CLOSE:
    case SC_SCREENSAVE:
    case SC_MONITORPOWER:
    case SC_MINIMIZE:
    case SC_MOVE:
    case SC_SIZE:
    case SC_KEYMENU:
    case SC_TASKLIST:
        return 0;

    //其余交给系统默认处理
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CLockWindow::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //整窗由 Direct2D 绘制，不擦背景，避免闪烁
    return 1;
}

LRESULT CLockWindow::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //画面由锁屏线程按帧绘制，这里只把无效区标记为已处理
    ValidateRect(NULL);
    return 0;
}

LRESULT CLockWindow::OnSetCursor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    ::SetCursor(::LoadCursor(NULL, IDC_ARROW));
    return TRUE;
}

LRESULT CLockWindow::OnMouseActivate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    return MA_ACTIVATE;
}

LRESULT CLockWindow::OnDisplayChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_sink != NULL)
    {
        m_sink->OnLockDisplayChange();
    }
    return 0;
}

LRESULT CLockWindow::OnQueryEndSession(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //系统注销或关机：锁屏不阻止（关机按钮发起的也走这里）
    return TRUE;
}

LRESULT CLockWindow::OnEndSession(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (wParam != FALSE && m_sink != NULL)
    {
        m_sink->OnLockEndSession();
    }
    return 0;
}

LRESULT CLockWindow::OnReassert(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_sink != NULL)
    {
        m_sink->OnLockReassert();
    }
    return 0;
}

} // namespace flamingolock
