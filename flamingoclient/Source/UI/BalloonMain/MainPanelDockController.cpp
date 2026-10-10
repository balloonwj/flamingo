/**
 *  主面板贴边自动隐藏控制器的实现。整体说明见 MainPanelDockController.h。
 *
 *  判定规则不在这里，而在 PanelDockGeometry.h 与 PanelDockPolicy.h 中；本文件只负责
 *  采集输入、移动窗口、置顶与日志。
 *
 *  balloonwj@qq.com   2026-10-02
 */

#include "stdafx.h"
#include "MainPanelDockController.h"

#include "../../App/FlamingoClient.h"                            // CFlamingoClient::m_UserConfig
#include "../../Session/WindowPlacementPrefs.h"                  // winplace::ResolveRestoredWindowRect（显示器变化后修正位置）
#include "../../Common/Log/IULog.h"                              // LOG_INFO / LOG_WARNING
#include "../../../third-party/balloonui/balloonui/DuiDpi.h"     // DuiDpi::GetWindowDpi / Scale
#include "../../Feature/Lock/LockHost.h"                         // IsScreenLocked：锁屏期间不轮询停靠状态
#include "../../Feature/Lock/LockSuppressPolicy.h"               // DecideDuringLock

namespace balloonmain {

namespace {

//balloonui 弹出菜单的窗口类名（见 balloonui Controls/List/DuiMenu.cpp 的 DECLARE_WND_CLASS_EX）。
//菜单窗口建窗时不设所有者，无法按所有者判断它属于谁，只能按类名识别
const TCHAR kDuiMenuPopupClass[] = _T("__DuiMenuPopup__");

//GetAsyncKeyState 返回值的最高位：为 1 表示该键此刻处于按下状态
const SHORT kKeyDownMask = (SHORT)0x8000;

//取窗口类名时缓冲区的长度（字符），比上面的类名长即可
const int kClassNameChars = 64;

//EnumDisplayMonitors 的回调：把每台显示器的矩形与工作区追加到 lParam 指向的 vector
BOOL CALLBACK CollectDockMonitorProc(HMONITOR hMonitor, HDC /*hdc*/, LPRECT /*rc*/, LPARAM lParam)
{
    std::vector<DockMonitor>* monitors = reinterpret_cast<std::vector<DockMonitor>*>(lParam);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(hMonitor, &mi))
    {
        DockMonitor m;
        m.m_monitor = mi.rcMonitor;
        m.m_work = mi.rcWork;
        monitors->push_back(m);
    }
    return TRUE;
}

//EnumThreadWindows 的回调：发现可见的 balloonui 菜单窗口时把 lParam 指向的 bool 置 true 并停止枚举
BOOL CALLBACK FindDuiMenuPopupProc(HWND hWnd, LPARAM lParam)
{
    if (!::IsWindowVisible(hWnd))
    {
        return TRUE;
    }
    TCHAR cls[kClassNameChars] = { 0 };
    ::GetClassName(hWnd, cls, kClassNameChars);
    if (_tcscmp(cls, kDuiMenuPopupClass) == 0)
    {
        *reinterpret_cast<bool*>(lParam) = true;
        return FALSE;
    }
    return TRUE;
}

//日志里用的贴边名称
const char* EdgeName(PanelDockEdge edge)
{
    switch (edge)
    {
    //贴上边
    case kPanelDockTop:
        return "top";
    //贴左边
    case kPanelDockLeft:
        return "left";
    //贴右边
    case kPanelDockRight:
        return "right";
    //未贴边
    default:
        return "none";
    }
}

} // namespace

CMainPanelDockController::CMainPanelDockController()
    : m_hHost(NULL)
    , m_lpFMGClient(NULL)
    , m_pHost(NULL)
    , m_pollRunning(false)
    , m_slideRunning(false)
    , m_slideStartTick(0)
{
    //BEGIN_MSG_MAP_EX 声明的这个标志在首次派发时会被先读后写，非窗口类必须自己置初值
    m_bMsgHandled = FALSE;
    ::SetRectEmpty(&m_slideFrom);
    ::SetRectEmpty(&m_slideTo);
}

CMainPanelDockController::~CMainPanelDockController()
{
    Shutdown();
}

void CMainPanelDockController::Attach(HWND hHost, CFlamingoClient* pClient, IPanelDockHost* pHost)
{
    m_hHost = hHost;
    m_lpFMGClient = pClient;
    m_pHost = pHost;
    if (m_hHost == NULL || !::IsWindow(m_hHost) || m_pHost == NULL)
    {
        LOG_ERROR("DockController::Attach: invalid host, edge auto-hide disabled.");
        m_hHost = NULL;
    }
}

void CMainPanelDockController::Shutdown()
{
    StopPoll();
    StopSlide();
}

bool CMainPanelDockController::PumpTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kDockPollTimerId)
    {
        //定时器已停但消息还在队列里时同样认领，只是不再执行轮询。
        //锁屏期间也不轮询：光标在锁屏界面上移到屏幕边缘时，停靠的主面板不应滑出（docs/锁屏方案.md 第 4.9 节）
        if (m_pollRunning
            && balloonlock::DecideDuringLock(balloonlock::kLockEvtDockSlideOut, balloonlock::IsScreenLocked())
               == balloonlock::kLockActNormal)
        {
            Poll();
        }
        return true;
    }
    if (nIDEvent == kDockSlideTimerId)
    {
        if (m_slideRunning)
        {
            StepSlide();
        }
        return true;
    }
    return false;
}

void CMainPanelDockController::OnUserMoveSizeStart()
{
    StopSlide();
}

void CMainPanelDockController::OnUserMoveSizeEnd()
{
    if (m_hHost == NULL || !::IsWindow(m_hHost))
    {
        return;
    }
    //最大化的窗口不参与贴边（拖动最大化窗口时系统会先把它还原，这里只防御键盘等其它途径）
    if (::IsZoomed(m_hHost) || ::IsIconic(m_hHost))
    {
        return;
    }

    RECT rc;
    ::GetWindowRect(m_hHost, &rc);
    DockSnapResult snap;
    if (IsFeatureEnabled())
    {
        snap = DetectDockEdge(rc, CollectMonitors(), ScaleByDpi(kDockSnapLogicalPx));
    }

    if (snap.m_edge != kPanelDockNone)
    {
        EnterDock(snap, "user-move");
    }
    else if (m_policy.IsDocked())
    {
        //窗口已在用户放下的位置，不移动
        LeaveDock("user-move", NULL);
    }
}

void CMainPanelDockController::DockIfAtEdge()
{
    if (m_hHost == NULL || !::IsWindow(m_hHost) || !IsFeatureEnabled())
    {
        return;
    }
    RECT rc;
    ::GetWindowRect(m_hHost, &rc);
    const DockSnapResult snap = DetectDockEdge(rc, CollectMonitors(), ScaleByDpi(kDockSnapLogicalPx));
    if (snap.m_edge == kPanelDockNone)
    {
        return;
    }
    EnterDock(snap, "restore");
    //窗口是程序摆上去的，此刻鼠标未必在主面板上：要求鼠标先进入过主面板才会收起
    m_policy.NotifyShownExternally();
}

void CMainPanelDockController::RevealForExternalShow()
{
    if (!m_policy.IsDocked())
    {
        return;
    }
    if (m_policy.NotifyShownExternally())
    {
        LOG_INFO("DockController: revealed by an external show request (edge %s).",
                 EdgeName(m_snap.m_edge));
        SlideTo(m_snap.m_snapped);
    }
}

void CMainPanelDockController::ResetForLogout()
{
    if (m_policy.IsDocked())
    {
        //可能正收起在屏幕外：放回吸附位置
        const RECT restore = m_snap.m_snapped;
        LeaveDock("logout", &restore);
    }
}

LRESULT CMainPanelDockController::OnDisplayChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SetMsgHandled(FALSE);
    Revalidate("display-change");
    return 0;
}

LRESULT CMainPanelDockController::OnSettingChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    SetMsgHandled(FALSE);
    if (wParam == SPI_SETWORKAREA)
    {
        Revalidate("work-area-change");
    }
    return 0;
}

void CMainPanelDockController::Poll()
{
    if (m_hHost == NULL || !::IsWindow(m_hHost))
    {
        StopPoll();
        return;
    }
    //隐藏到托盘或最小化期间不判定，保持原状态，等再次显示
    if (!::IsWindowVisible(m_hHost) || ::IsIconic(m_hHost))
    {
        return;
    }

    const PanelDockInput input = BuildInput();
    const PanelDockAction action = m_policy.Tick(input);
    switch (action)
    {
    //鼠标离开满延时：滑到屏幕外，只留细条
    case kDockActionHide:
        LOG_INFO("DockController: hide to edge %s.", EdgeName(m_snap.m_edge));
        SlideTo(HiddenRect());
        break;

    //鼠标碰到细条：滑回吸附位置
    case kDockActionShow:
        LOG_INFO("DockController: show from edge %s.", EdgeName(m_snap.m_edge));
        SlideTo(m_snap.m_snapped);
        break;

    //开关被关掉：状态机已退出贴边状态，这里把窗口放回吸附位置并取消置顶
    case kDockActionUndock:
    {
        const RECT restore = m_snap.m_snapped;
        LeaveDock("feature-disabled", &restore);
        break;
    }

    //保持现状
    default:
        break;
    }
}

PanelDockInput CMainPanelDockController::BuildInput()
{
    PanelDockInput in;
    in.m_enabled = IsFeatureEnabled();
    in.m_nowTick = ::GetTickCount();

    POINT pt;
    ::GetCursorPos(&pt);
    if (m_policy.IsHidden())
    {
        const RECT strip = StripRect();
        in.m_cursorOver = (::PtInRect(&strip, pt) != FALSE);
    }
    else
    {
        in.m_cursorOver = IsCursorOverPanel(pt);
    }

    //左右键互换时，GetAsyncKeyState 的 VK_LBUTTON 反映的是物理左键，此时主键是物理右键
    const int primaryButton = (::GetSystemMetrics(SM_SWAPBUTTON) != 0) ? VK_RBUTTON : VK_LBUTTON;
    in.m_mouseButtonDown = ((::GetAsyncKeyState(primaryButton) & kKeyDownMask) != 0);
    in.m_menuOpen = IsAnyMenuOpen();
    in.m_editingText = (m_pHost != NULL) && m_pHost->IsDockEditingText();
    return in;
}

bool CMainPanelDockController::IsCursorOverPanel(POINT pt)
{
    RECT rc;
    ::GetWindowRect(m_hHost, &rc);
    if (::PtInRect(&rc, pt))
    {
        return true;
    }

    //鼠标在宿主所属的窗口上（如以主面板为所有者的对话框），或在宿主自己的浮层上
    HWND hUnder = ::WindowFromPoint(pt);
    if (hUnder == NULL)
    {
        return false;
    }
    if (::GetAncestor(hUnder, GA_ROOTOWNER) == m_hHost)
    {
        return true;
    }
    HWND hTop = ::GetAncestor(hUnder, GA_ROOT);
    return hTop != NULL && m_pHost != NULL && m_pHost->IsDockOwnPopup(hTop);
}

bool CMainPanelDockController::IsAnyMenuOpen()
{
    //系统原生菜单（托盘菜单、聊天气泡的右键菜单等）
    GUITHREADINFO gti;
    ::ZeroMemory(&gti, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (::GetGUIThreadInfo(::GetCurrentThreadId(), &gti)
        && (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE)) != 0)
    {
        return true;
    }

    //balloonui 菜单：它跑自己的消息循环，期间本类的定时器照常触发，所以必须专门判断
    bool found = false;
    ::EnumThreadWindows(::GetCurrentThreadId(), FindDuiMenuPopupProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool CMainPanelDockController::IsFeatureEnabled() const
{
    return m_lpFMGClient != NULL && m_lpFMGClient->m_UserConfig.IsEnableDockAutoHide() != FALSE;
}

std::vector<DockMonitor> CMainPanelDockController::CollectMonitors()
{
    std::vector<DockMonitor> monitors;
    ::EnumDisplayMonitors(NULL, NULL, CollectDockMonitorProc, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

int CMainPanelDockController::ScaleByDpi(int logicalPx) const
{
    return balloonwjui::DuiDpi::Scale(logicalPx, balloonwjui::DuiDpi::GetWindowDpi(m_hHost));
}

void CMainPanelDockController::EnterDock(const DockSnapResult& snap, const char* pszTrigger)
{
    const bool wasDocked = m_policy.IsDocked();
    m_snap = snap;
    m_policy.Dock(snap.m_edge);
    MoveNow(snap.m_snapped);

    //置顶：不置顶的话，别的窗口最大化后会盖住屏幕边缘的细条，鼠标碰不到，主面板就无法展开
    ::SetWindowPos(m_hHost, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (!m_pollRunning)
    {
        if (::SetTimer(m_hHost, kDockPollTimerId, kDockPollIntervalMs, NULL) != 0)
        {
            m_pollRunning = true;
        }
        else
        {
            LOG_WARNING("DockController: SetTimer for polling failed (error %lu), panel will not auto-hide.",
                        ::GetLastError());
        }
    }

    LOG_INFO("DockController: dock to edge %s (%s, was docked %d), rect (%ld,%ld %ldx%ld).",
             EdgeName(snap.m_edge), pszTrigger, (int)wasDocked, snap.m_snapped.left, snap.m_snapped.top,
             snap.m_snapped.right - snap.m_snapped.left, snap.m_snapped.bottom - snap.m_snapped.top);
}

void CMainPanelDockController::LeaveDock(const char* pszTrigger, const RECT* pRestoreRect)
{
    const PanelDockEdge oldEdge = m_snap.m_edge;
    m_policy.Undock();
    StopPoll();

    if (m_hHost != NULL && ::IsWindow(m_hHost))
    {
        if (pRestoreRect != NULL)
        {
            //MoveNow 会先停掉滑动动画，窗口不会停在半路
            MoveNow(*pRestoreRect);
        }
        else
        {
            StopSlide();
        }
        ::SetWindowPos(m_hHost, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    m_snap = DockSnapResult();

    if (pRestoreRect != NULL)
    {
        LOG_INFO("DockController: undock from edge %s (%s), window restored to (%ld,%ld).",
                 EdgeName(oldEdge), pszTrigger, pRestoreRect->left, pRestoreRect->top);
    }
    else
    {
        LOG_INFO("DockController: undock from edge %s (%s), window left where it is.",
                 EdgeName(oldEdge), pszTrigger);
    }
}

void CMainPanelDockController::Revalidate(const char* pszTrigger)
{
    if (!m_policy.IsDocked() || m_hHost == NULL || !::IsWindow(m_hHost))
    {
        return;
    }

    const DockSnapResult snap = DetectDockEdge(m_snap.m_snapped, CollectMonitors(),
                                               ScaleByDpi(kDockSnapLogicalPx));
    if (snap.m_edge == m_snap.m_edge)
    {
        //仍可贴同一条边：更新吸附位置与工作区，收起状态下同步移到新的收起位置
        m_snap = snap;
        MoveNow(m_policy.IsHidden() ? HiddenRect() : m_snap.m_snapped);
        LOG_INFO("DockController: revalidated after %s, still docked to edge %s.",
                 pszTrigger, EdgeName(m_snap.m_edge));
        return;
    }

    //原来的边不能再贴（显示器被拔掉、任务栏挪到了这一侧等）：退出贴边状态。原来的吸附位置
    //可能已经不在任何屏幕上，先按当前屏幕布局修正到一定看得见的位置再移过去
    std::vector<RECT> workAreas;
    const std::vector<DockMonitor> monitors = CollectMonitors();
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        workAreas.push_back(monitors[i].m_work);
    }
    MONITORINFO fallback;
    fallback.cbSize = sizeof(fallback);
    ::GetMonitorInfo(::MonitorFromWindow(m_hHost, MONITOR_DEFAULTTOPRIMARY), &fallback);

    winplace::WindowPlacementPrefs saved;
    saved.m_hasSaved = true;
    saved.m_rect = m_snap.m_snapped;
    //最小尺寸取窗口当前尺寸：这里只是要一个看得见的位置，不希望改变窗口大小
    SIZE size;
    size.cx = m_snap.m_snapped.right - m_snap.m_snapped.left;
    size.cy = m_snap.m_snapped.bottom - m_snap.m_snapped.top;
    const RECT restore = winplace::ResolveRestoredWindowRect(saved, size, size, workAreas, fallback.rcWork);
    LeaveDock(pszTrigger, &restore);
}

void CMainPanelDockController::SlideTo(const RECT& target)
{
    if (m_hHost == NULL || !::IsWindow(m_hHost))
    {
        return;
    }
    if (!::IsWindowVisible(m_hHost))
    {
        MoveNow(target);
        return;
    }

    ::GetWindowRect(m_hHost, &m_slideFrom);
    m_slideTo = target;
    m_slideStartTick = ::GetTickCount();
    if (!m_slideRunning)
    {
        if (::SetTimer(m_hHost, kDockSlideTimerId, kDockSlideFrameMs, NULL) == 0)
        {
            //起不了动画就直接移过去，功能不受影响
            MoveNow(target);
            return;
        }
        m_slideRunning = true;
    }
}

void CMainPanelDockController::MoveNow(const RECT& target)
{
    StopSlide();
    if (m_hHost == NULL || !::IsWindow(m_hHost))
    {
        return;
    }
    ::SetWindowPos(m_hHost, NULL, target.left, target.top, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CMainPanelDockController::StepSlide()
{
    if (m_hHost == NULL || !::IsWindow(m_hHost))
    {
        StopSlide();
        return;
    }

    const DWORD elapsed = ::GetTickCount() - m_slideStartTick;
    if (elapsed >= kDockSlideDurationMs)
    {
        MoveNow(m_slideTo);
        return;
    }

    //先快后慢的缓动：进度 t 映射为 1 - (1 - t)^2
    const double t = (double)elapsed / (double)kDockSlideDurationMs;
    const double eased = 1.0 - (1.0 - t) * (1.0 - t);
    const int x = m_slideFrom.left + (int)((m_slideTo.left - m_slideFrom.left) * eased);
    const int y = m_slideFrom.top + (int)((m_slideTo.top - m_slideFrom.top) * eased);
    ::SetWindowPos(m_hHost, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CMainPanelDockController::StopSlide()
{
    if (m_slideRunning)
    {
        if (m_hHost != NULL && ::IsWindow(m_hHost))
        {
            ::KillTimer(m_hHost, kDockSlideTimerId);
        }
        m_slideRunning = false;
    }
}

void CMainPanelDockController::StopPoll()
{
    if (m_pollRunning)
    {
        if (m_hHost != NULL && ::IsWindow(m_hHost))
        {
            ::KillTimer(m_hHost, kDockPollTimerId);
        }
        m_pollRunning = false;
    }
}

RECT CMainPanelDockController::HiddenRect() const
{
    return ComputeDockHiddenRect(m_snap.m_snapped, m_snap.m_edge, m_snap.m_work,
                                 ScaleByDpi(kDockStripLogicalPx));
}

RECT CMainPanelDockController::StripRect() const
{
    return ComputeDockStripRect(m_snap.m_snapped, m_snap.m_edge, m_snap.m_work,
                                ScaleByDpi(kDockStripLogicalPx));
}

} // namespace balloonmain
