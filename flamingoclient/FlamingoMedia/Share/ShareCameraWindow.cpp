/**
 *  屏幕共享发起人一侧的摄像头浮动窗口，见 ShareCameraWindow.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareCameraWindow.h"

#include <memory>
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//标题栏高（与 ComputeCameraWindowRect 计算高度时用的一致）
const int kTitleBarH = 28;
//小窗的刷新定时器：每 100 毫秒一次；id 取较大值避开 balloonui 基类可能占用的小 id
const UINT_PTR kRefreshTimerId = 501;
const UINT kRefreshMs = 100;

} // namespace

CShareCameraWindow::CShareCameraWindow()
    : m_panel(NULL)
    , m_source(NULL)
{
}

CShareCameraWindow::~CShareCameraWindow()
{
    Close();
}

void CShareCameraWindow::SetSource(IShareCameraFrames* source)
{
    m_source = source;
    if (m_panel != NULL)
        m_panel->SetSource(source);
}

void CShareCameraWindow::EnsureWindow()
{
    if (IsWindow())
        return;
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (Create(NULL, CWindow::rcDefault, TR(L"摄像头"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR("screenshare: create camera window failed.");
        return;
    }
    SetTitle(TR(L"摄像头"));
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);
    SetResizable(false);
    std::unique_ptr<CShareCameraPanel> panel(new CShareCameraPanel());
    panel->SetSource(m_source);
    m_panel = panel.get();
    SetClientContent(std::move(panel));
    //摄像头小窗不能被采进共享画面
    ExcludeWindowFromCapture(m_hWnd);
    SetTimer(kRefreshTimerId, kRefreshMs, NULL);
}

void CShareCameraWindow::Update(const std::vector<ShareCameraTile>& tiles, int overflowCount, const RECT& monitorRect)
{
    if (tiles.empty() && overflowCount <= 0)
    {
        if (IsWindow())
            ShowWindow(SW_HIDE);
        return;
    }
    EnsureWindow();
    if (!IsWindow() || m_panel == NULL)
        return;
    m_panel->SetTiles(tiles, overflowCount);
    //贴在被共享显示器工作区的右边缘，高度随人数变化、不超过工作区
    HMONITOR mon = ::MonitorFromRect(&monitorRect, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    const RECT rc = ComputeCameraWindowRect(work, static_cast<int>(tiles.size()), overflowCount);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CShareCameraWindow::Close()
{
    m_panel = NULL;
    if (IsWindow())
        DestroyWindow();
}

LRESULT CShareCameraWindow::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kRefreshTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    if (m_panel != NULL && IsWindowVisible())
        m_panel->Refresh();
    return 0;
}

LRESULT CShareCameraWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    ShowWindow(SW_HIDE);
    return 0;
}

} // namespace screenshare
