/**
 *  LockSession.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "LockSession.h"

#include <bcrypt.h>

#include "LockPower.h"
#include "LockDpi.h"
#include "../Core/LockOverlayLayout.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"

namespace flamingolock {

namespace {

const float kMaxFrameSeconds = 0.1f;          // 一帧最多推进多少秒（窗口被拖住或机器卡顿后不至于跳一大段）
const float kMsPerSecond = 1000.0f;           // 毫秒与秒的换算
const uint64_t kNudgeIntervalMs = 25000;      // 防屏保合成输入的间隔，与 KeepAwakePolicy 的 kKeepAwakeNudgeIntervalMs 一致
const uint64_t kReassertIntervalMs = 500;     // 定时重新置顶的间隔（应对后来居上的其它置顶窗口）

// 当前进行中的锁屏会话；钩子回调据此找到会话。只在锁屏线程上读写
CLockSession* g_activeSession = NULL;

// 枚举显示器时收集各显示器的句柄与矩形
struct MonitorItem
{
    HMONITOR m_monitor;   // 显示器句柄
    RECT     m_rect;      // 显示器矩形（虚拟桌面坐标）
};

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC /*dc*/, LPRECT rect, LPARAM param)
{
    std::vector<MonitorItem>* items = reinterpret_cast<std::vector<MonitorItem>*>(param);
    MonitorItem item;
    item.m_monitor = monitor;
    item.m_rect = *rect;
    items->push_back(item);
    return TRUE;
}

} // namespace

CLockSession::CLockSession()
    : m_owner(NULL)
    , m_verifier(NULL)
    , m_anim(kAnimHonor)
    , m_keyboardHook(NULL)
    , m_mouseHook(NULL)
    , m_foregroundHook(NULL)
    , m_oldDpiContext(NULL)
    , m_active(false)
    , m_unlockPending(false)
    , m_rebuildPending(false)
    , m_reassertPending(false)
    , m_ctrlDown(false)
    , m_altDown(false)
    , m_winDown(false)
    , m_lastFrameMs(0)
    , m_lastNudgeMs(0)
    , m_lastReassertMs(0)
    , m_animStartMs(0)
{
    m_choice = DefaultChoice();
}

CLockSession::~CLockSession()
{
    End();
}

bool CLockSession::IsActive() const
{
    return m_active;
}

SceneEnv CLockSession::EnvFor(const CLockWindow& w) const
{
    SceneEnv env;
    env.m_d2d = m_d2d;
    env.m_dwrite = m_dwrite;
    env.m_scale = w.Scale();
    return env;
}

bool CLockSession::Begin(ILockSessionOwner* owner, CLockPasswordVerifier* verifier, const LockAnimationChoice& choice)
{
    End();
    if (verifier == NULL || !verifier->IsSet())
    {
        LOG_ERROR("lock session: begin without a password, ignored.");
        return false;
    }
    m_owner = owner;
    m_verifier = verifier;
    m_choice = choice;
    //「随机」方式下挑一种动画：随机数取自系统（与临时密码的盐同一来源），取不到时退回时钟
    uint32_t random = 0;
    if (::BCryptGenRandom(NULL, reinterpret_cast<PUCHAR>(&random), sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    {
        random = static_cast<uint32_t>(::GetTickCount64());
    }
    m_anim = PickInitialAnimation(choice, random);

    if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2d)) || m_d2d == NULL ||
        FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&m_dwrite))) || m_dwrite == NULL)
    {
        LOG_ERROR("lock session: Direct2D / DirectWrite factory unavailable.");
        m_d2d.Release();
        m_dwrite.Release();
        return false;
    }

    //锁屏期间本线程按 Per-Monitor-V2 运行：锁屏窗口按物理像素建在每块显示器上
    m_oldDpiContext = EnterPerMonitorV2Dpi();

    g_activeSession = this;
    m_active = true;
    m_ui.Reset();
    m_box.Clear();
    m_message.clear();
    m_ctrlDown = false;
    m_altDown = false;
    m_winDown = false;
    if (CreateWindows() == 0)
    {
        LOG_ERROR("lock session: no lock window could be created, lock aborted.");
        End();
        return false;
    }

    HINSTANCE module = _Module.GetModuleInstance();
    m_keyboardHook = ::SetWindowsHookExW(WH_KEYBOARD_LL, &CLockSession::KeyboardProc, module, 0);
    m_mouseHook = ::SetWindowsHookExW(WH_MOUSE_LL, &CLockSession::MouseProc, module, 0);
    m_foregroundHook = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL,
                                         &CLockSession::WinEventProc, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNTHREAD);
    if (m_keyboardHook == NULL)
    {
        LOG_ERROR("lock session: keyboard hook failed, error=%u.", static_cast<unsigned>(::GetLastError()));
    }
    balloonmain::CKeepAwakeSession::LogIdlePolicySnapshot();
    m_keepAwake.Acquire();

    const uint64_t now = ::GetTickCount64();
    m_lastFrameMs = now;
    m_lastNudgeMs = now;
    m_lastReassertMs = 0;
    m_animStartMs = now;
    m_reassertPending = true;
    LOG_INFO("lock session: started, %u window(s), animation %s (choice %s), keyboard hook %s, mouse hook %s.",
             static_cast<unsigned>(m_windows.size()), ChoiceToToken(ChoiceAt(m_anim)), ChoiceToToken(choice),
             m_keyboardHook != NULL ? "ok" : "failed", m_mouseHook != NULL ? "ok" : "failed");
    return true;
}

void CLockSession::ReleaseSystemResources()
{
    if (m_keyboardHook != NULL)
    {
        ::UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = NULL;
    }
    if (m_mouseHook != NULL)
    {
        ::UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = NULL;
    }
    if (m_foregroundHook != NULL)
    {
        ::UnhookWinEvent(m_foregroundHook);
        m_foregroundHook = NULL;
    }
    m_keepAwake.Release();
}

void CLockSession::End()
{
    ReleaseSystemResources();
    DestroyWindows();
    if (m_oldDpiContext != NULL)
    {
        RestoreThreadDpi(m_oldDpiContext);
        m_oldDpiContext = NULL;
    }
    m_box.Clear();
    m_message.clear();
    m_ui.Reset();
    if (m_verifier != NULL)
    {
        m_verifier->Clear();
    }
    m_verifier = NULL;
    m_dwrite.Release();
    m_d2d.Release();
    if (g_activeSession == this)
    {
        g_activeSession = NULL;
    }
    if (m_active)
    {
        LOG_INFO("lock session: ended.");
    }
    m_active = false;
    m_unlockPending = false;
    m_rebuildPending = false;
    m_reassertPending = false;
}

int CLockSession::CreateWindows()
{
    std::vector<MonitorItem> monitors;
    ::EnumDisplayMonitors(NULL, NULL, &CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        std::unique_ptr<CLockWindow> w(new CLockWindow());
        const float scale = LockMonitorScale(monitors[i].m_monitor);
        if (w->CreateLockWindow(static_cast<int>(m_windows.size()), monitors[i].m_rect, scale, this))
        {
            m_windows.push_back(std::move(w));
        }
    }
    ApplyAnimation(m_anim);
    //初始焦点给光标所在的那块显示器
    POINT cursor;
    ::GetCursorPos(&cursor);
    for (size_t i = 0; i < m_windows.size(); ++i)
    {
        if (::PtInRect(&m_windows[i]->MonitorRect(), cursor))
        {
            FocusWindow(static_cast<int>(i));
            break;
        }
    }
    return static_cast<int>(m_windows.size());
}

void CLockSession::DestroyWindows()
{
    for (size_t i = 0; i < m_windows.size(); ++i)
    {
        m_windows[i]->DestroyLockWindow();
    }
    m_windows.clear();
}

void CLockSession::ApplyAnimation(LockAnimation anim)
{
    m_anim = anim;
    for (size_t i = 0; i < m_windows.size(); ++i)
    {
        m_windows[i]->SetScene(CreateLockScene(anim), EnvFor(*m_windows[i]));
    }
}

void CLockSession::RenderFrame()
{
    if (!m_active)
    {
        return;
    }
    if (m_unlockPending)
    {
        End();
        if (m_owner != NULL)
        {
            m_owner->OnSessionUnlocked();
        }
        return;
    }
    if (m_rebuildPending)
    {
        m_rebuildPending = false;
        LOG_INFO("lock session: display changed, rebuilding lock windows.");
        DestroyWindows();
        m_ui.Reset();
        m_box.Clear();
        m_message.clear();
        if (CreateWindows() == 0)
        {
            LOG_ERROR("lock session: no lock window after display change.");
        }
        m_reassertPending = true;
    }

    const uint64_t now = ::GetTickCount64();
    float dt = static_cast<float>(now - m_lastFrameMs) / kMsPerSecond;
    if (dt > kMaxFrameSeconds)
    {
        dt = kMaxFrameSeconds;
    }
    m_lastFrameMs = now;

    if (m_ui.Tick(now))
    {
        m_box.Clear();
        m_message.clear();
    }
    if (m_choice.m_mode == kAnimModeRotate && now - m_animStartMs >= kRotateIntervalMs)
    {
        m_animStartMs = now;
        ApplyAnimation(NextAnimation(m_anim));
    }
    if (now - m_lastNudgeMs >= kNudgeIntervalMs)
    {
        m_lastNudgeMs = now;
        m_keepAwake.Nudge();
    }
    if (m_reassertPending || now - m_lastReassertMs >= kReassertIntervalMs)
    {
        m_reassertPending = false;
        m_lastReassertMs = now;
        Reassert();
    }

    OverlayFrame frame;
    frame.m_phase = m_ui.Phase();
    frame.m_maskChars = m_box.Length();
    frame.m_message = CurrentMessage(now);
    frame.m_dt = dt;
    frame.m_nowMs = now;
    frame.m_mouse.x = -1;
    frame.m_mouse.y = -1;
    frame.m_showClock = true;
    const uint64_t wallMs = NowWallMs();
    frame.m_wallMs = wallMs;
    for (size_t i = 0; i < m_windows.size(); ++i)
    {
        frame.m_barHere = (static_cast<int>(i) == m_ui.ActiveMonitor());
        m_windows[i]->Render(EnvFor(*m_windows[i]), frame, dt, wallMs);
    }
}

std::wstring CLockSession::CurrentMessage(uint64_t nowMs) const
{
    if (m_verifier != NULL)
    {
        const uint32_t remain = m_verifier->CooldownRemainingMs(nowMs);
        if (remain > 0)
        {
            const int seconds = static_cast<int>((remain + kMsPerSecond - 1) / kMsPerSecond);
            return TRF(L"输错次数过多，请 {0} 秒后再试", seconds);
        }
    }
    return m_message;
}

void CLockSession::TryUnlock()
{
    if (!m_ui.AcceptsKeyboard() || m_verifier == NULL)
    {
        return;
    }
    const uint64_t now = ::GetTickCount64();
    m_ui.OnActivity(now);
    if (m_verifier->CooldownRemainingMs(now) > 0)
    {
        m_box.Clear();
        return;
    }
    if (m_box.Length() == 0)
    {
        m_message = TR(L"请输入密码");
        if (m_ui.ActiveMonitor() >= 0 && m_ui.ActiveMonitor() < static_cast<int>(m_windows.size()))
        {
            m_windows[m_ui.ActiveMonitor()]->Shake(now);
        }
        return;
    }
    const VerifyResult r = m_verifier->Verify(m_box.Text(), now);
    m_box.Clear();
    if (r == kVerifyOk)
    {
        LOG_INFO("lock session: password accepted, unlocking.");
        m_unlockPending = true;
        return;
    }
    LOG_INFO("lock session: wrong password (result %d, failures %d).", static_cast<int>(r), m_verifier->FailureCount());
    m_message = TR(L"密码不正确，请重试");
    if (m_ui.ActiveMonitor() >= 0 && m_ui.ActiveMonitor() < static_cast<int>(m_windows.size()))
    {
        m_windows[m_ui.ActiveMonitor()]->Shake(now);
    }
}

bool CLockSession::IsLockWindow(HWND hwnd) const
{
    if (hwnd == NULL)
    {
        return false;
    }
    for (size_t i = 0; i < m_windows.size(); ++i)
    {
        if (m_windows[i]->m_hWnd == hwnd)
        {
            return true;
        }
    }
    return false;
}

void CLockSession::FocusWindow(int index)
{
    if (index < 0 || index >= static_cast<int>(m_windows.size()))
    {
        return;
    }
    HWND target = m_windows[index]->m_hWnd;
    if (::GetForegroundWindow() == target)
    {
        ::SetFocus(target);
        return;
    }
    if (!::SetForegroundWindow(target))
    {
        //前台属于别的线程时 SetForegroundWindow 可能被系统拒绝：临时挂接到前台线程的输入队列再试一次
        HWND fg = ::GetForegroundWindow();
        const DWORD fgThread = (fg != NULL) ? ::GetWindowThreadProcessId(fg, NULL) : 0;
        const DWORD self = ::GetCurrentThreadId();
        if (fgThread != 0 && fgThread != self && ::AttachThreadInput(self, fgThread, TRUE))
        {
            ::BringWindowToTop(target);
            ::SetForegroundWindow(target);
            ::AttachThreadInput(self, fgThread, FALSE);
        }
    }
    ::SetFocus(target);
}

void CLockSession::Reassert()
{
    for (size_t i = 0; i < m_windows.size(); ++i)
    {
        ::SetWindowPos(m_windows[i]->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (m_windows.empty() || IsLockWindow(::GetForegroundWindow()))
    {
        return;
    }
    int target = m_ui.ActiveMonitor();
    if (target < 0 || target >= static_cast<int>(m_windows.size()))
    {
        target = 0;
        POINT cursor;
        ::GetCursorPos(&cursor);
        for (size_t i = 0; i < m_windows.size(); ++i)
        {
            if (::PtInRect(&m_windows[i]->MonitorRect(), cursor))
            {
                target = static_cast<int>(i);
                break;
            }
        }
    }
    FocusWindow(target);
}

void CLockSession::TrackModifier(DWORD vk, bool keyUp)
{
    switch (vk)
    {
    //Ctrl：通用与左右键码
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        m_ctrlDown = !keyUp;
        break;

    //Alt：通用与左右键码
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        m_altDown = !keyUp;
        break;

    //左右 Win 键
    case VK_LWIN:
    case VK_RWIN:
        m_winDown = !keyUp;
        break;

    //其它键不影响修饰键状态
    default:
        break;
    }
}

LRESULT CALLBACK CLockSession::KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    CLockSession* self = g_activeSession;
    if (code == HC_ACTION && self != NULL && lParam != 0)
    {
        const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        const bool injected = (k->flags & LLKHF_INJECTED) != 0;
        if (!injected)
        {
            self->TrackModifier(k->vkCode, keyUp);
        }
        LockKeyEvent e;
        e.m_vk = k->vkCode;
        e.m_keyUp = keyUp;
        e.m_altDown = self->m_altDown || (k->flags & LLKHF_ALTDOWN) != 0;
        e.m_ctrlDown = self->m_ctrlDown;
        e.m_winDown = self->m_winDown;
        e.m_injected = injected;
        const bool fg = self->IsLockWindow(::GetForegroundWindow());
        if (DecideLockKey(e, fg, self->m_ui.AcceptsKeyboard()) == kKeyBlock)
        {
            return 1;
        }
    }
    return ::CallNextHookEx(NULL, code, wParam, lParam);
}

LRESULT CALLBACK CLockSession::MouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_activeSession != NULL && lParam != 0)
    {
        const MSLLHOOKSTRUCT* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const bool injected = (m->flags & LLMHF_INJECTED) != 0;
        if (DecideLockMouse(static_cast<uint32_t>(wParam), injected) == kKeyBlock)
        {
            return 1;
        }
    }
    return ::CallNextHookEx(NULL, code, wParam, lParam);
}

void CALLBACK CLockSession::WinEventProc(HWINEVENTHOOK /*hook*/, DWORD event, HWND hwnd, LONG /*idObject*/,
                                         LONG /*idChild*/, DWORD /*eventThread*/, DWORD /*eventTime*/)
{
    CLockSession* self = g_activeSession;
    if (event == EVENT_SYSTEM_FOREGROUND && self != NULL && !self->IsLockWindow(hwnd))
    {
        self->m_reassertPending = true;
    }
}

// ---------------------------------------------------------------------------
// ILockWindowSink
// ---------------------------------------------------------------------------

void CLockSession::OnLockMouseDown(int index, POINT pt)
{
    if (index < 0 || index >= static_cast<int>(m_windows.size()))
    {
        return;
    }
    const uint64_t now = ::GetTickCount64();
    const LockOverlayHit hit = (index == m_ui.ActiveMonitor())
                               ? HitTestOverlay(m_windows[index]->Layout(), m_ui.Phase(), pt) : kHitNothing;
    switch (hit)
    {
    //「解锁」：出现密码框并取得键盘焦点
    case kHitUnlock:
        if (m_ui.OnButton(kBtnUnlock, now) == kCmdFocusPassword)
        {
            m_message.clear();
            FocusWindow(index);
        }
        break;

    //「重启」：出现确认框
    case kHitRestart:
        m_ui.OnButton(kBtnRestart, now);
        break;

    //「关机」：出现确认框
    case kHitShutdown:
        m_ui.OnButton(kBtnShutdown, now);
        break;

    //密码框右边的箭头：比对密码
    case kHitPasswordGo:
        TryUnlock();
        break;

    //点在密码框上：保持焦点
    case kHitPasswordBox:
        m_ui.OnActivity(now);
        FocusWindow(index);
        break;

    //确认框「取消」：回到按钮
    case kHitConfirmCancel:
        m_ui.OnButton(kBtnConfirmCancel, now);
        break;

    //确认框「关机 / 重启」：执行电源操作
    case kHitConfirmOk:
        {
            const LockUiCommand cmd = m_ui.OnButton(kBtnConfirmOk, now);
            if (cmd == kCmdShutdown)
            {
                RunPowerAction(kPowerShutdown);
            }
            else if (cmd == kCmdRestart)
            {
                RunPowerAction(kPowerRestart);
            }
        }
        break;

    //确认框内的其它地方：吸收点击
    case kHitConfirmPanel:
        break;

    //没点中任何部件：只有动画时唤出按钮栏，否则把按钮栏挪到被点的显示器
    default:
        m_ui.OnClick(index, now);
        if (m_ui.AcceptsKeyboard())
        {
            FocusWindow(index);
        }
        break;
    }
}

void CLockSession::OnLockMouseMove(int /*index*/, POINT /*pt*/)
{
    m_ui.OnActivity(::GetTickCount64());
}

void CLockSession::OnLockMouseLeave(int /*index*/)
{
}

void CLockSession::OnLockChar(int /*index*/, wchar_t ch)
{
    if (!m_ui.AcceptsKeyboard())
    {
        return;
    }
    const uint64_t now = ::GetTickCount64();
    m_ui.OnActivity(now);
    if (m_verifier != NULL && m_verifier->CooldownRemainingMs(now) > 0)
    {
        return;
    }
    if (m_box.InsertChar(ch))
    {
        m_message.clear();
    }
}

void CLockSession::OnLockKeyDown(int /*index*/, UINT vk)
{
    const uint64_t now = ::GetTickCount64();
    switch (vk)
    {
    //回车：比对密码
    case VK_RETURN:
        TryUnlock();
        break;

    //退格：删除最后一个字符
    case VK_BACK:
        if (m_ui.AcceptsKeyboard())
        {
            m_ui.OnActivity(now);
            m_box.Backspace();
        }
        break;

    //Esc：逐级退回（确认框或密码框 → 按钮 → 只有动画）
    case VK_ESCAPE:
        if (m_ui.OnEscape(now))
        {
            m_box.Clear();
        }
        m_message.clear();
        break;

    //其它键：只算一次操作
    default:
        m_ui.OnActivity(now);
        break;
    }
}

void CLockSession::OnLockDisplayChange()
{
    m_rebuildPending = true;
}

void CLockSession::OnLockEndSession()
{
    //系统正在注销或关机：立即摘除钩子、释放电源请求，进程随后会被结束。
    //本函数在锁屏窗口自己的消息处理函数里被调用，不能在这里销毁窗口
    LOG_INFO("lock session: system is ending the user session, releasing hooks.");
    ReleaseSystemResources();
}

void CLockSession::OnLockReassert()
{
    m_reassertPending = true;
}

} // namespace flamingolock
