/**
 *  LockThread.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "LockThread.h"

#include <process.h>
#include <stdlib.h>   // srand
#include <dwmapi.h>

#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

namespace flamingolock {

namespace {

const DWORD kReadyWaitMs = 5000;        // 等锁屏线程建好消息队列的最长时间
const DWORD kStopWaitMs = 5000;         // 关闭模块时等锁屏线程退出的最长时间
const DWORD kFallbackFrameMs = 16;      // 桌面合成关闭（DwmFlush 失败）时每帧的间隔，约 60 帧每秒
const uint32_t kChoiceTokenChars = 32;  // 读锁屏画面配置值的缓冲区大小（字符数）

} // namespace

CLockThread::CLockThread()
    : m_host(NULL)
    , m_thread(NULL)
    , m_threadId(0)
    , m_readyEvent(NULL)
    , m_busy(false)
    , m_locked(false)
{
}

CLockThread::~CLockThread()
{
    Stop();
}

void CLockThread::SetHost(ILockHost* host)
{
    m_host = host;
}

bool CLockThread::IsLocked() const
{
    return m_locked.load();
}

bool CLockThread::RequestLock()
{
    if (m_busy.load())
    {
        //设置窗口开着时把它提到前面；正在锁屏时线程会忽略这条消息
        if (m_threadId != 0)
        {
            ::PostThreadMessage(m_threadId, kThreadMsgRequestLock, 0, 0);
        }
        return false;
    }
    if (m_thread == NULL)
    {
        m_readyEvent = ::CreateEventW(NULL, TRUE, FALSE, NULL);
        unsigned int tid = 0;
        m_thread = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, &CLockThread::ThreadEntry, this, 0, &tid));
        if (m_thread == NULL)
        {
            LOG_ERROR("lock thread: create failed, errno=%d.", errno);
            ::CloseHandle(m_readyEvent);
            m_readyEvent = NULL;
            return false;
        }
        m_threadId = tid;
        if (::WaitForSingleObject(m_readyEvent, kReadyWaitMs) != WAIT_OBJECT_0)
        {
            LOG_WARNING("lock thread: message queue not ready within %u ms.", static_cast<unsigned>(kReadyWaitMs));
        }
        LOG_INFO("lock thread started, id %u.", static_cast<unsigned>(m_threadId));
    }
    m_busy = true;
    if (!::PostThreadMessage(m_threadId, kThreadMsgRequestLock, 0, 0))
    {
        LOG_ERROR("lock thread: post request failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        m_busy = false;
        return false;
    }
    return true;
}

void CLockThread::Stop()
{
    if (m_thread == NULL)
    {
        return;
    }
    ::PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    if (::WaitForSingleObject(m_thread, kStopWaitMs) != WAIT_OBJECT_0)
    {
        LOG_WARNING("lock thread: did not exit within %u ms.", static_cast<unsigned>(kStopWaitMs));
    }
    ::CloseHandle(m_thread);
    m_thread = NULL;
    m_threadId = 0;
    if (m_readyEvent != NULL)
    {
        ::CloseHandle(m_readyEvent);
        m_readyEvent = NULL;
    }
    m_busy = false;
    m_locked = false;
}

unsigned __stdcall CLockThread::ThreadEntry(void* param)
{
    CLockThread* self = static_cast<CLockThread*>(param);
    self->Run();
    return 0;
}

void CLockThread::Run()
{
    //balloonui 的输入框基于无窗口 RichEdit，剪贴板与拖放要求本线程初始化 OLE
    const HRESULT oleHr = ::OleInitialize(NULL);
    //动画场景用 rand 摆放星星、粒子等；rand 的状态每个线程各自一份，不播种的话每次锁屏的画面布局都相同
    ::srand(static_cast<unsigned>(::GetTickCount()));
    MSG msg;
    //先窥探一次，让系统为本线程建好消息队列，之后宿主才能投递线程消息
    ::PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    ::SetEvent(m_readyEvent);

    bool quit = false;
    while (!quit)
    {
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                quit = true;
                break;
            }
            if (msg.hwnd == NULL)
            {
                HandleThreadMessage(msg);
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        if (quit)
        {
            break;
        }
        if (m_session && m_session->IsActive())
        {
            m_session->RenderFrame();
            if (m_session && !m_session->IsActive())
            {
                //本帧里解锁了（OnSessionUnlocked 已通知宿主），会话对象可以释放
                m_session.reset();
            }
            else if (FAILED(::DwmFlush()))
            {
                ::Sleep(kFallbackFrameMs);
            }
        }
        else if (m_setup && m_setup->IsPreviewActive())
        {
            //设置窗口的全屏预览：与锁屏一样逐帧绘制，用 DwmFlush 对齐节拍
            m_setup->RenderPreviewFrame();
            if (FAILED(::DwmFlush()))
            {
                ::Sleep(kFallbackFrameMs);
            }
        }
        else
        {
            ::MsgWaitForMultipleObjects(0, NULL, FALSE, INFINITE, QS_ALLINPUT);
        }
    }

    //模块关闭：结束进行中的锁屏、关掉设置窗口
    if (m_session)
    {
        const bool wasLocked = m_session->IsActive();
        m_session->End();
        m_session.reset();
        if (wasLocked)
        {
            m_locked = false;
            NotifyHost(false);
        }
    }
    m_verifier.Clear();
    m_setup.reset();
    if (SUCCEEDED(oleHr))
    {
        ::OleUninitialize();
    }
    LOG_INFO("lock thread exited.");
}

void CLockThread::HandleThreadMessage(const MSG& msg)
{
    switch (msg.message)
    {
    //宿主请求锁屏：锁屏中忽略；设置窗口开着就提到前面；否则打开设置窗口
    case kThreadMsgRequestLock:
        if (m_session && m_session->IsActive())
        {
            break;
        }
        if (m_setup && m_setup->IsWindow())
        {
            m_setup->BringToFront();
            break;
        }
        OpenSetup();
        break;

    //设置窗口确认：开始锁屏
    case kThreadMsgSetupDone:
        BeginLockFromSetup();
        break;

    //设置窗口被取消：可以再次请求
    case kThreadMsgSetupCancelled:
        m_busy = false;
        break;

    //不认识的线程消息：忽略
    default:
        break;
    }
}

void CLockThread::OpenSetup()
{
    LockAnimationChoice choice = DefaultChoice();
    char token[kChoiceTokenChars] = { 0 };
    if (m_host != NULL && m_host->ReadAnimationChoice(token, kChoiceTokenChars))
    {
        choice = ChoiceFromToken(token);
    }
    if (!m_setup)
    {
        m_setup.reset(new CLockSetupDialog());
    }
    if (!m_setup->Open(choice))
    {
        m_busy = false;
    }
}

void CLockThread::BeginLockFromSetup()
{
    if (!m_setup)
    {
        m_busy = false;
        return;
    }
    std::wstring password;
    LockAnimationChoice choice = DefaultChoice();
    m_setup->TakeResult(password, choice);
    if (m_host != NULL)
    {
        m_host->WriteAnimationChoice(ChoiceToToken(choice));
    }
    const bool set = m_verifier.Set(password);
    if (!password.empty())
    {
        ::SecureZeroMemory(&password[0], password.size() * sizeof(wchar_t));
    }
    if (!set)
    {
        LOG_ERROR("lock thread: could not store the temporary password, lock aborted.");
        m_busy = false;
        return;
    }
    m_session.reset(new CLockSession());
    if (!m_session->Begin(this, &m_verifier, choice))
    {
        m_session.reset();
        m_verifier.Clear();
        m_busy = false;
        return;
    }
    m_locked = true;
    NotifyHost(true);
}

void CLockThread::OnSessionUnlocked()
{
    m_locked = false;
    m_busy = false;
    NotifyHost(false);
}

void CLockThread::NotifyHost(bool locked)
{
    if (m_host != NULL)
    {
        m_host->OnLockStateChanged(locked);
    }
}

} // namespace flamingolock
