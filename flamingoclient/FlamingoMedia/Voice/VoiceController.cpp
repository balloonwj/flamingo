/**
 *  语音消息的控制器，说明见 VoiceController.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "VoiceController.h"

#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace voicemsg {

namespace {

//消息窗口的窗口类名
const wchar_t kVoiceWindowClass[] = L"FlamingoMediaVoiceEventWindow";

//本模块的实例句柄（窗口类注册在本 DLL 或单元测试程序里）
HINSTANCE currentModule()
{
    HMODULE module = NULL;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&currentModule), &module);
    return module;
}

} // namespace

CVoiceController::CVoiceController(IVoiceMicBusyQuery* busy, IVoiceControllerSink* sink,
                                   IVoiceCaptureDevice* captureDevice, IVoiceRenderDevice* renderDevice)
    : m_busy(busy)
    , m_sink(sink)
    , m_capture(captureDevice)
    , m_render(renderDevice)
    , m_hwnd(NULL)
    , m_playToken(0)
    , m_eventPosted(false)
    , m_currentToken(0)
{
    //没有注入设备时用 WASAPI
    if (m_capture == NULL)
    {
        m_ownCapture.reset(new VoiceWasapiCaptureDevice());
        m_capture = m_ownCapture.get();
    }
    if (m_render == NULL)
    {
        m_ownRender.reset(new VoiceWasapiRenderDevice());
        m_render = m_ownRender.get();
    }
}

CVoiceController::~CVoiceController()
{
    Shutdown();
}

bool CVoiceController::Init()
{
    if (m_hwnd != NULL)
        return true;
    const HINSTANCE instance = currentModule();
    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CVoiceController::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kVoiceWindowClass;
    //已注册过时 RegisterClassEx 返回 0、错误码为 ERROR_CLASS_ALREADY_EXISTS，可以继续使用
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        LOG_ERROR("voice controller: register window class failed, error %lu.", ::GetLastError());
        return false;
    }
    m_hwnd = ::CreateWindowExW(0, kVoiceWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, instance, this);
    if (m_hwnd == NULL)
    {
        LOG_ERROR("voice controller: create message window failed, error %lu.", ::GetLastError());
        return false;
    }
    LOG_INFO("voice controller: initialized.");
    return true;
}

void CVoiceController::Shutdown()
{
    if (m_hwnd == NULL)
        return;
    //先停止录音与播放（设备停止后不再有新事件），再销毁窗口、丢弃排着的事件
    m_recorder.Cancel();
    m_player.Stop();
    m_playToken = 0;
    ::DestroyWindow(m_hwnd);
    m_hwnd = NULL;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_events.clear();
        m_eventPosted = false;
        m_currentToken = 0;
    }
    LOG_INFO("voice controller: shut down.");
}

int CVoiceController::StartRecord(const std::wstring& path)
{
    if (m_hwnd == NULL)
        return voice_record_not_ready;
    if (m_recorder.IsRecording())
        return voice_record_already;
    //麦克风正被通话或屏幕共享使用：不录（方案 V6）
    if (m_busy != NULL && m_busy->IsMicrophoneBusy())
    {
        LOG_INFO("voice controller: record refused, microphone is used by a call or screen share.");
        return voice_record_mic_busy;
    }
    if (!m_recorder.Start(m_capture, this, path))
        return voice_record_device_error;
    return voice_record_ok;
}

bool CVoiceController::StopRecord(VoiceRecordResult& result)
{
    return m_recorder.Stop(result);
}

void CVoiceController::CancelRecord()
{
    m_recorder.Cancel();
}

bool CVoiceController::PauseRecord()
{
    if (m_hwnd == NULL)
        return false;
    return m_recorder.Pause();
}

int CVoiceController::ResumeRecord()
{
    if (m_hwnd == NULL)
        return voice_record_not_ready;
    if (!m_recorder.IsPaused())
        return voice_record_not_paused;
    //暂停期间可能开始了通话或屏幕共享：与开始录音一样，麦克风被占用时不继续（方案 V6），录音保持暂停
    if (m_busy != NULL && m_busy->IsMicrophoneBusy())
    {
        LOG_INFO("voice controller: resume refused, microphone is used by a call or screen share.");
        return voice_record_mic_busy;
    }
    if (!m_recorder.Resume())
        return voice_record_device_error;
    return voice_record_ok;
}

bool CVoiceController::SnapshotPausedRecord(const std::wstring& path, int64_t& outDurationMs)
{
    outDurationMs = 0;
    if (m_hwnd == NULL)
        return false;
    return m_recorder.SnapshotPaused(path, outDurationMs);
}

bool CVoiceController::IsRecording() const
{
    return m_recorder.IsRecording();
}

bool CVoiceController::IsRecordPaused() const
{
    return m_recorder.IsPaused();
}

bool CVoiceController::Play(const std::wstring& path, uint32_t token)
{
    if (m_hwnd == NULL)
        return false;
    //先停掉正在播的（不通知播完），之后它排着的旧事件按编号丢弃
    m_player.Stop();
    m_playToken = 0;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_currentToken = token;
    }
    if (!m_player.Play(m_render, this, path))
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_currentToken = 0;
        return false;
    }
    m_playToken = token;
    return true;
}

void CVoiceController::StopPlay()
{
    m_player.Stop();
    m_playToken = 0;
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_currentToken = 0;
}

uint32_t CVoiceController::PlayingToken() const
{
    return m_playToken;
}

void CVoiceController::OnVoiceLevel(int level)
{
    pushEvent(event_record_level, 0, level, 0);
}

void CVoiceController::OnVoiceLimitReached()
{
    pushEvent(event_record_limit, 0, 0, 0);
}

void CVoiceController::OnVoicePlayProgress(int64_t playedMs, int64_t totalMs)
{
    uint32_t token = 0;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        token = m_currentToken;
    }
    pushEvent(event_play_progress, token, playedMs, totalMs);
}

void CVoiceController::OnVoicePlayFinished()
{
    uint32_t token = 0;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        token = m_currentToken;
    }
    pushEvent(event_play_finished, token, 0, 0);
}

void CVoiceController::pushEvent(int kind, uint32_t token, int64_t value1, int64_t value2)
{
    HWND hwnd = NULL;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        Event e;
        e.m_kind = kind;
        e.m_token = token;
        e.m_value1 = value1;
        e.m_value2 = value2;
        m_events.push_back(e);
        //已经投递过、UI 线程还没处理时不重复投递，一次处理会取走全部事件
        if (m_eventPosted)
            return;
        m_eventPosted = true;
        hwnd = m_hwnd;
    }
    if (hwnd == NULL || !::IsWindow(hwnd) || !::PostMessageW(hwnd, kMsgVoiceEvent, 0, 0))
    {
        //窗口不在或投递失败：允许下一次重新投递，事件留在队列里等下一次处理或在关闭时丢弃
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_eventPosted = false;
    }
}

void CVoiceController::DrainEvents()
{
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        events.swap(m_events);
        m_eventPosted = false;
    }
    for (size_t i = 0; i < events.size(); ++i)
    {
        const Event& e = events[i];
        switch (e.m_kind)
        {
        //录音音量：录音已结束的丢弃；暂停之前排进来、暂停之后才取出的也丢弃，录音条暂停后音量柱不再跳动
        case event_record_level:
            if (m_recorder.IsRecording() && !m_recorder.IsPaused() && m_sink != NULL)
                m_sink->OnVoiceRecordLevel(static_cast<int>(e.m_value1));
            break;

        //录满：录音已结束的丢弃
        case event_record_limit:
            if (m_recorder.IsRecording() && m_sink != NULL)
                m_sink->OnVoiceRecordLimitReached();
            break;

        //播放进度：已停止或已换文件（编号不同）的丢弃
        case event_play_progress:
            if (e.m_token != 0 && e.m_token == m_playToken && m_sink != NULL)
                m_sink->OnVoicePlayProgress(e.m_token, e.m_value1, e.m_value2);
            break;

        //播放到结尾：先在 UI 线程上停止设备，再通知；已停止或已换文件的丢弃
        case event_play_finished:
            if (e.m_token != 0 && e.m_token == m_playToken)
            {
                m_player.Stop();
                m_playToken = 0;
                {
                    std::lock_guard<std::mutex> lock(m_eventMutex);
                    m_currentToken = 0;
                }
                if (m_sink != NULL)
                    m_sink->OnVoicePlayFinished(e.m_token);
            }
            break;

        //未知的事件种类：不会出现，忽略
        default:
            break;
        }
    }
}

LRESULT CALLBACK CVoiceController::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //建立窗口时记下控制器指针，之后从窗口的用户数据里取
    if (msg == WM_NCCREATE)
    {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    else if (msg == kMsgVoiceEvent)
    {
        CVoiceController* self = reinterpret_cast<CVoiceController*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self != NULL)
            self->DrainEvents();
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace voicemsg
