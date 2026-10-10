/**
 *  LockPreviewSession.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "LockPreviewSession.h"

#include <bcrypt.h>

#include "LockDpi.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"

namespace flamingolock {

namespace {

const float kMaxFrameSeconds = 0.1f;   // 一帧最多推进多少秒（与锁屏会话相同，卡顿后不至于跳一大段）
const float kMsPerSecond = 1000.0f;    // 毫秒与秒的换算
const int kPreviewWindowIndex = 0;     // 预览只有一个窗口，序号恒为 0

// 「随机」方式用的随机数：取自系统（与锁屏会话同一来源），取不到时退回时钟
uint32_t PreviewRandom()
{
    uint32_t random = 0;
    if (::BCryptGenRandom(NULL, reinterpret_cast<PUCHAR>(&random), sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    {
        random = static_cast<uint32_t>(::GetTickCount64());
    }
    return random;
}

} // namespace

CLockPreviewSession::CLockPreviewSession()
    : m_owner(NULL)
    , m_lastFrameMs(0)
    , m_beginMs(0)
    , m_active(false)
    , m_firstFramePending(false)
{
}

CLockPreviewSession::~CLockPreviewSession()
{
    End();
}

bool CLockPreviewSession::IsActive() const
{
    return m_active;
}

SceneEnv CLockPreviewSession::Env() const
{
    SceneEnv env;
    env.m_d2d = m_d2d;
    env.m_dwrite = m_dwrite;
    env.m_scale = m_window ? m_window->Scale() : 1.0f;
    return env;
}

bool CLockPreviewSession::Begin(ILockPreviewOwner* owner, HWND anchor, const LockAnimationChoice& choice)
{
    End();
    if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2d)) || m_d2d == NULL ||
        FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&m_dwrite))) || m_dwrite == NULL)
    {
        LOG_ERROR("lock preview: Direct2D / DirectWrite factory unavailable.");
        m_d2d.Release();
        m_dwrite.Release();
        return false;
    }

    const uint64_t now = ::GetTickCount64();
    const LockAnimation anim = m_state.Start(choice, PreviewRandom(), now);
    m_hint = TR(L"按 Esc 或单击退出预览");

    //预览窗口与锁屏窗口一样按物理像素建在显示器上：建窗期间把本线程临时切到 Per-Monitor-V2，
    //显示器矩形也要在这个上下文里取，才是物理像素。窗口建好后它自己的 DPI 感知方式不再随线程改变
    const HANDLE oldDpi = EnterPerMonitorV2Dpi();
    HMONITOR monitor = NULL;
    if (anchor != NULL && ::IsWindow(anchor))
    {
        monitor = ::MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST);
    }
    else
    {
        const POINT origin = { 0, 0 };
        monitor = ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO mi;
    ::ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    bool created = (monitor != NULL && ::GetMonitorInfoW(monitor, &mi) != FALSE);
    std::unique_ptr<CLockWindow> w(new CLockWindow());
    if (created)
    {
        created = w->CreateLockWindow(kPreviewWindowIndex, mi.rcMonitor, LockMonitorScale(monitor), this);
    }
    RestoreThreadDpi(oldDpi);
    if (!created)
    {
        LOG_ERROR("lock preview: the preview window could not be created.");
        w->DestroyLockWindow();
        m_d2d.Release();
        m_dwrite.Release();
        return false;
    }

    m_owner = owner;
    m_window = std::move(w);
    m_active = true;
    m_window->SetScene(CreateLockScene(anim), Env());
    //取得前台与键盘焦点，按键才会送到预览窗口
    ::SetForegroundWindow(m_window->m_hWnd);
    ::SetFocus(m_window->m_hWnd);
    //输入保护时间与轮流播放的计时要等第一帧画出来之后才开始算（见 RenderFrame）：建窗、场景初始化与第一帧
    //可能用去几百毫秒（调试版更久），从 Start 算起的话保护时间可能在用户看到画面之前就已过完
    m_beginMs = now;
    m_firstFramePending = true;
    m_lastFrameMs = ::GetTickCount64();
    LOG_INFO("lock preview: started, animation %s (choice %s), monitor (%ld,%ld)-(%ld,%ld).",
             ChoiceToToken(ChoiceAt(anim)), ChoiceToToken(choice), mi.rcMonitor.left, mi.rcMonitor.top,
             mi.rcMonitor.right, mi.rcMonitor.bottom);
    return true;
}

void CLockPreviewSession::End()
{
    const uint64_t startMs = ::GetTickCount64();
    if (m_window)
    {
        m_window->DestroyLockWindow();
        m_window.reset();
    }
    m_dwrite.Release();
    m_d2d.Release();
    if (m_active)
    {
        LOG_INFO("lock preview: ended (cleanup %u ms).", static_cast<unsigned>(::GetTickCount64() - startMs));
    }
    m_active = false;
    m_firstFramePending = false;
    m_owner = NULL;
}

void CLockPreviewSession::RenderFrame()
{
    if (!m_active || !m_window)
    {
        return;
    }
    if (m_state.IsEnding())
    {
        //先让设置窗口恢复可用并取回前台，再销毁预览窗口：反过来的话，预览窗口消失的那一刻系统会把前台交给
        //别的程序（设置窗口此时还是禁用的），之后再取回前台可能被系统拒绝
        ILockPreviewOwner* owner = m_owner;
        if (owner != NULL)
        {
            owner->OnPreviewEnding();
        }
        End();
        return;
    }

    const uint64_t now = ::GetTickCount64();
    float dt = static_cast<float>(now - m_lastFrameMs) / kMsPerSecond;
    if (dt > kMaxFrameSeconds)
    {
        dt = kMaxFrameSeconds;
    }
    m_lastFrameMs = now;
    if (m_state.Tick(now))
    {
        LOG_INFO("lock preview: rotating to %s.", ChoiceToToken(ChoiceAt(m_state.Current())));
        m_window->SetScene(CreateLockScene(m_state.Current()), Env());
    }

    OverlayFrame frame;
    frame.m_phase = kUiIdle;
    frame.m_barHere = false;
    frame.m_maskChars = 0;
    frame.m_dt = dt;
    frame.m_nowMs = now;
    frame.m_mouse.x = -1;
    frame.m_mouse.y = -1;
    //左下角的时间与真正锁屏时一样显示（场景自己显示时间的除外，由锁屏窗口判断）
    frame.m_showClock = true;
    const uint64_t wallMs = NowWallMs();
    frame.m_wallMs = wallMs;
    frame.m_hint = m_hint;
    m_window->Render(Env(), frame, dt, wallMs);
    if (m_firstFramePending)
    {
        //第一帧已经画出来：输入保护时间与轮流播放的计时从这一刻算起。画第一帧期间排进队列的单击、按键
        //要到这之后才处理，因而都落在保护时间之内
        m_firstFramePending = false;
        const uint64_t drawnMs = ::GetTickCount64();
        m_state.RestartClock(drawnMs);
        m_lastFrameMs = drawnMs;
        LOG_INFO("lock preview: first frame drawn %u ms after the start.", static_cast<unsigned>(drawnMs - m_beginMs));
    }
}

void CLockPreviewSession::HandleInput(LockPreviewInput input, const char* what)
{
    if (m_state.OnInput(input, ::GetTickCount64()))
    {
        LOG_INFO("lock preview: %s, ending at the next frame.", what);
    }
}

// ---------------------------------------------------------------------------
// ILockWindowSink
// ---------------------------------------------------------------------------

void CLockPreviewSession::OnLockMouseDown(int /*index*/, POINT /*pt*/)
{
    HandleInput(kPreviewInputMouseButton, "left click");
}

void CLockPreviewSession::OnLockMouseMove(int /*index*/, POINT /*pt*/)
{
    //鼠标移动不结束预览；粒子时钟推开粒子由锁屏窗口直接交给场景
}

void CLockPreviewSession::OnLockMouseLeave(int /*index*/)
{
}

void CLockPreviewSession::OnLockChar(int /*index*/, wchar_t /*ch*/)
{
    //字符输入不单独处理：对应的按键按下已经结束预览
}

void CLockPreviewSession::OnLockKeyDown(int /*index*/, UINT vk)
{
    LOG_INFO("lock preview: key down, vk=0x%02X.", vk);
    HandleInput(kPreviewInputKey, "key down");
}

void CLockPreviewSession::OnLockDisplayChange()
{
    HandleInput(kPreviewInputDisplayChange, "display changed");
}

void CLockPreviewSession::OnLockEndSession()
{
    HandleInput(kPreviewInputEndSession, "user session ending");
}

void CLockPreviewSession::OnLockReassert()
{
    //预览不装前台钩子，不会收到重新置顶的请求
}

void CLockPreviewSession::OnLockSysKeyDown(int /*index*/, UINT vk)
{
    LOG_INFO("lock preview: system key down, vk=0x%02X.", vk);
    HandleInput(kPreviewInputSysKey, "system key down");
}

void CLockPreviewSession::OnLockOtherButtonDown(int /*index*/, POINT /*pt*/)
{
    HandleInput(kPreviewInputMouseButton, "right or middle click");
}

void CLockPreviewSession::OnLockDeactivate(int /*index*/)
{
    HandleInput(kPreviewInputDeactivate, "window deactivated");
}

} // namespace flamingolock
