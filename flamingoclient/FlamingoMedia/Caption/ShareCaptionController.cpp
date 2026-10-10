/**
 *  屏幕共享实时字幕的调度，见 ShareCaptionController.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "ShareCaptionController.h"

#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

namespace {

//过期检查与浮层跟随区域的定时器（毫秒）；id 取较大值避开基类可能占用的小 id
const UINT_PTR kExpireTimerId = 811;
const UINT kExpireTimerMs = 500;

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
    if (n <= 0)
        return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
    return s;
}

//单调递增的毫秒时钟
int64_t NowMs()
{
    return static_cast<int64_t>(::GetTickCount64());
}

} // namespace

CShareCaptionController::CShareCaptionController()
    : m_host(NULL)
    , m_inSession(false)
    , m_selfEnabled(false)
    , m_showEnabled(true)
    , m_audio(NULL)
    , m_transport(NULL)
    , m_selfSeat(0)
    , m_stream(NULL)
    , m_sentenceId(1)
    , m_sentenceStartMs(0)
    , m_posted(false)
{
}

CShareCaptionController::~CShareCaptionController()
{
    Shutdown();
}

bool CShareCaptionController::Init(IShareCaptionHost* host)
{
    m_host = host;
    if (!IsWindow() && Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("share caption: create message window failed.");
        return false;
    }
    flamingomedia::CMediaCaptionBridge::Instance().AddObserver(this);
    return true;
}

void CShareCaptionController::Shutdown()
{
    flamingomedia::CMediaCaptionBridge::Instance().RemoveObserver(this);
    EndSession();
    m_overlay.Destroy();
    if (IsWindow())
        DestroyWindow();
    m_host = NULL;
}

bool CShareCaptionController::IsAvailable() const
{
    return flamingomedia::CMediaCaptionBridge::Instance().IsAvailable();
}

void CShareCaptionController::BeginSession(ShareAudioTransport* transport, int selfSeat, ShareAudioSession* audio)
{
    EndSession();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_transport = transport;
        m_selfSeat = selfSeat;
    }
    m_audio = audio;
    m_inSession = true;
    //记住的选择（D2）：字幕可用时从字幕模块读；第一次默认不识别自己、显示别人的字幕
    caption::ICaptionModule* module = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (module != NULL)
    {
        m_selfEnabled = module->GetPreference(caption::caption_pref_live_self);
        m_showEnabled = module->GetPreference(caption::caption_pref_live_show);
    }
    if (m_audio != NULL)
        m_audio->SetCaptionTap(this);
    if (IsWindow())
        SetTimer(kExpireTimerId, kExpireTimerMs, NULL);
    updateStream();
    LOG_INFO("share caption: session begun, seat=%d, self=%d, show=%d, available=%d.", selfSeat, m_selfEnabled ? 1 : 0,
             m_showEnabled ? 1 : 0, module != NULL ? 1 : 0);
}

void CShareCaptionController::EndSession()
{
    if (!m_inSession)
        return;
    m_inSession = false;
    if (m_audio != NULL)
        m_audio->SetCaptionTap(NULL);
    m_audio = NULL;
    closeStream();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_transport = NULL;
        m_pending.clear();
    }
    if (IsWindow())
        KillTimer(kExpireTimerId);
    m_board.Clear();
    m_overlay.Hide();
    LOG_INFO("share caption: session ended.");
}

void CShareCaptionController::SetSelfEnabled(bool on)
{
    if (on == m_selfEnabled)
        return;
    m_selfEnabled = on;
    caption::ICaptionModule* module = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (module != NULL)
        module->SetPreference(caption::caption_pref_live_self, on);
    updateStream();
    if (!on)
    {
        int seat = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            seat = m_selfSeat;
        }
        m_board.RemoveSeat(seat);
        refreshOverlay();
    }
    LOG_INFO("share caption: self captions %s.", on ? "on" : "off");
    if (m_host != NULL)
        m_host->OnCaptionStateChanged();
}

bool CShareCaptionController::IsSelfEnabled() const
{
    return m_selfEnabled;
}

void CShareCaptionController::SetShowEnabled(bool on)
{
    if (on == m_showEnabled)
        return;
    m_showEnabled = on;
    caption::ICaptionModule* module = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (module != NULL)
        module->SetPreference(caption::caption_pref_live_show, on);
    refreshOverlay();
    LOG_INFO("share caption: show captions %s.", on ? "on" : "off");
    if (m_host != NULL)
        m_host->OnCaptionStateChanged();
}

bool CShareCaptionController::IsShowEnabled() const
{
    return m_showEnabled;
}

void CShareCaptionController::OnSeatLeft(int seat)
{
    m_board.RemoveSeat(seat);
    refreshOverlay();
}

void CShareCaptionController::updateStream()
{
    const bool want = m_inSession && m_selfEnabled && IsAvailable();
    if (!want)
    {
        closeStream();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stream != NULL)
            return;
    }
    caption::ICaptionModule* module = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (module == NULL)
        return;
    caption::ICaptionStream* stream = module->CreateStream(caption::caption_purpose_live, this);
    if (stream == NULL)
    {
        LOG_WARNING("share caption: recognition engine unavailable, self captions not started.");
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stream = stream;
    m_throttle.Reset();
    m_sentenceStartMs = 0;
}

void CShareCaptionController::closeStream()
{
    caption::ICaptionStream* stream = NULL;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        stream = m_stream;
        m_stream = NULL;
    }
    //Close 返回之后识别线程不再回调；在锁外调用，免得与回调里取锁互相等待
    if (stream != NULL)
        stream->Close();
}

void CShareCaptionController::OnRoomAudio(int kind, const int16_t* mono, int samples)
{
    if (kind != share_audio_tap_mic || mono == NULL || samples <= 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stream != NULL)
        m_stream->PushPcm(mono, static_cast<uint32_t>(samples), NowMs());
}

void CShareCaptionController::OnCaptionPartial(const wchar_t* text)
{
    if (text == NULL)
        return;
    const int64_t now = NowMs();
    if (m_sentenceStartMs == 0)
        m_sentenceStartMs = now;
    emitSelf(text, false, static_cast<uint32_t>(now - m_sentenceStartMs));
}

void CShareCaptionController::OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs)
{
    if (text == NULL)
        return;
    const int64_t duration = (endMs > startMs) ? (endMs - startMs) : 0;
    emitSelf(text, true, static_cast<uint32_t>(duration));
    //下一句
    ++m_sentenceId;
    m_sentenceStartMs = 0;
}

void CShareCaptionController::emitSelf(const std::wstring& text, bool final, uint32_t durationMs)
{
    Pending p;
    p.m_self = true;
    p.m_caption.m_final = final;
    p.m_caption.m_sentenceId = m_sentenceId;
    p.m_caption.m_durationMs = durationMs;
    p.m_caption.m_utf8 = TruncateUtf8(WideToUtf8(text), kShareCaptionMaxTextBytes);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_throttle.Allow(final, NowMs()))
            return;
        p.m_seat = m_selfSeat;
        if (m_transport != NULL)
            m_transport->SendRoomBody(BuildShareCaptionBody(m_selfSeat, p.m_caption));
    }
    enqueue(p);
}

void CShareCaptionController::OnCaptionFrame(int senderSeat, const ShareCaptionText& caption)
{
    Pending p;
    p.m_seat = senderSeat;
    p.m_self = false;
    p.m_caption = caption;
    enqueue(p);
}

void CShareCaptionController::enqueue(const Pending& p)
{
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(p);
        if (!m_posted)
        {
            m_posted = true;
            post = true;
        }
    }
    if (post && IsWindow())
        PostMessage(kMsgShareCaptionPending);
}

void CShareCaptionController::OnCaptionAvailabilityChanged(bool available)
{
    if (available)
    {
        //字幕重新可用（管理员打开了字幕、重新登录）：按记住的选择恢复识别，入口随之出现
        updateStream();
        if (m_host != NULL)
            m_host->OnCaptionStateChanged();
        return;
    }
    //管理员关闭了字幕或已退出登录：停止识别，清掉显示（D5）
    closeStream();
    m_board.Clear();
    m_overlay.Hide();
    LOG_INFO("share caption: caption became unavailable, recognition stopped.");
    if (m_host != NULL)
        m_host->OnCaptionStateChanged();
}

LRESULT CShareCaptionController::OnPendingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    std::vector<Pending> items;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        items.swap(m_pending);
        m_posted = false;
    }
    if (!m_inSession || m_host == NULL || !IsAvailable())
        return 0;
    const int64_t now = NowMs();
    bool changed = false;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Pending& p = items[i];
        const std::wstring text = Utf8ToWide(p.m_caption.m_utf8);
        const std::wstring speaker = m_host->CaptionSeatName(p.m_seat);
        if (m_board.OnCaption(p.m_seat, speaker, text, p.m_caption.m_final, p.m_caption.m_sentenceId, now))
            changed = true;
        //录制共享时：每句最终结果写进字幕草稿（时刻按收到时的录制时刻推算）
        if (p.m_caption.m_final && !text.empty())
        {
            int64_t recordedMs = 0;
            caption::ICaptionRecording* rec = m_host->CaptionRecording(recordedMs);
            if (rec != NULL)
            {
                int64_t startMs = 0;
                int64_t endMs = 0;
                ShareCaptionRemoteTimes(recordedMs, p.m_caption.m_durationMs, startMs, endMs);
                rec->AddRemoteCaption(speaker.c_str(), text.c_str(), startMs, endMs);
            }
        }
    }
    if (changed)
        refreshOverlay();
    return 0;
}

LRESULT CShareCaptionController::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kExpireTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    m_board.Expire(NowMs());
    //每次都重画：显示区域可能随观看窗、被共享的窗口移动
    refreshOverlay();
    return 0;
}

void CShareCaptionController::refreshOverlay()
{
    if (!m_inSession || !m_showEnabled || m_board.Empty() || m_host == NULL)
    {
        m_overlay.Hide();
        return;
    }
    RECT area;
    bool exclude = false;
    HWND owner = NULL;
    if (!m_host->CaptionOverlayArea(area, exclude, owner))
    {
        m_overlay.Hide();
        return;
    }
    m_overlay.Update(area, exclude, owner, m_board.Visible(), flamingomedia::CMediaCaptionBridge::Instance().Acquire());
}

} // namespace screenshare
