/**
 *  聊天窗的录音会话，说明见 VoiceRecordSession.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "VoiceRecordSession.h"

#include "ChatVoiceBubble.h"   // VoicePlayFailedHint：试听打不开时的提示
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace balloonchatwnd {

namespace {

//试听播放键的 clientMsgId 前缀：与列表里本人语音的 clientMsgId 区分开
const char kPreviewKeyPrefix[] = "preview:";

} // namespace

CVoiceRecordSession::CVoiceRecordSession()
    : m_host(NULL)
    , m_barShown(false)
{
}

CVoiceRecordSession::~CVoiceRecordSession()
{
    OnWindowClosing();
}

void CVoiceRecordSession::Init(IVoiceRecordSessionHost* host, balloonwjui::DuiHBox* holder,
                               const VoiceRecordBarIds& ids)
{
    m_host = host;
    m_ids = ids;
    m_bar.Build(holder, ids);
}

bool CVoiceRecordSession::Start(const std::wstring& voiceDir, const std::string& clientMsgId)
{
    if (m_flow.IsActive())
        return false;
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    const int result = hub.StartRecord(this, voiceDir, clientMsgId);
    if (result != flamingomedia::media_voice_record_ok)
    {
        //麦克风被通话或屏幕共享占用、打不开、另一个会话正在录：提示原因，不显示录音条
        const std::wstring hint = VoiceRecordStartHint(result);
        if (!hint.empty() && m_host != NULL)
            m_host->ShowVoiceRecordToast(hint);
        return false;
    }
    m_clientMsgId = clientMsgId;
    m_stopped = balloonvoice::VoiceRecordOutput();
    m_flow.Begin();
    m_bar.Begin(::GetTickCount64());
    m_bar.ApplyState(voice_rec_recording, false);
    if (m_host != NULL)
        m_host->ShowVoiceRecordBarUi(true);
    m_barShown = true;
    LOG_INFO("voice record session: started, clientmsgid %s.", clientMsgId.c_str());
    return true;
}

bool CVoiceRecordSession::HandleButton(unsigned int ctrlId)
{
    if (!m_flow.IsActive() || ctrlId == 0)
        return false;
    int event = -1;
    if (ctrlId == m_ids.m_pause)
        event = (m_flow.State() == voice_rec_paused) ? voice_ev_resume : voice_ev_pause;
    else if (ctrlId == m_ids.m_stop)
        event = voice_ev_stop;
    else if (ctrlId == m_ids.m_preview)
        event = voice_ev_preview_toggle;
    else if (ctrlId == m_ids.m_cancel)
        event = voice_ev_cancel;
    else if (ctrlId == m_ids.m_send)
        event = voice_ev_send;
    else if (ctrlId == m_ids.m_confirm)
        event = voice_ev_confirm_discard;
    else if (ctrlId == m_ids.m_back)
        event = voice_ev_back;
    else
        return false;
    dispatch(event);
    return true;
}

bool CVoiceRecordSession::HandleKey(UINT vk)
{
    if (!m_flow.IsActive())
        return false;
    //录音条显示时 Enter、Esc 一律截住：隐藏的输入框可能仍持有焦点，不截住会把 Enter 当成发送文字消息
    if (vk == VK_RETURN)
    {
        dispatch(voice_ev_enter);
        return true;
    }
    if (vk == VK_ESCAPE)
    {
        dispatch(voice_ev_escape);
        return true;
    }
    return false;
}

void CVoiceRecordSession::OnWindowClosing()
{
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    //先不再接收通知：正在试听的停止（不回调本对象），还没停止的录音按取消处理
    hub.DetachPlayListener(this);
    hub.DetachListener(this);
    //已停止、还没发送的录音丢弃（删除写出的文件）
    if (!m_stopped.m_path.empty())
    {
        hub.DiscardRecorded(m_stopped);
        m_stopped = balloonvoice::VoiceRecordOutput();
    }
    if (m_flow.IsActive())
        LOG_INFO("voice record session: window closing, unsent recording discarded, clientmsgid %s.",
                 m_clientMsgId.c_str());
    m_flow.Reset();
    m_clientMsgId.clear();
    //窗口正在关闭：不再调用宿主
    m_barShown = false;
}

void CVoiceRecordSession::OnVoiceRecordLevel(int level)
{
    if (m_flow.State() == voice_rec_recording)
        m_bar.PushLevel(level, ::GetTickCount64());
}

void CVoiceRecordSession::OnVoiceRecordLimitReached()
{
    LOG_INFO("voice record session: reached the 2 minute limit, entering preview.");
    m_bar.MarkLimitReached();
    dispatch(voice_ev_limit_reached);
}

void CVoiceRecordSession::OnVoicePlayProgress(const balloonvoice::VoicePlayKey& /*key*/, int64_t playedMs,
                                              int64_t totalMs)
{
    //试听状态与已暂停状态（暂停中试听）都在计时位置显示播放进度
    const int state = m_flow.State();
    if ((state == voice_rec_preview || state == voice_rec_paused) && m_flow.Previewing())
        m_bar.SetPreviewProgress(playedMs, totalMs);
}

void CVoiceRecordSession::OnVoicePlayEnded(const balloonvoice::VoicePlayKey& /*key*/, int reason)
{
    //本对象只为试听发起播放：播完、被停止、被列表里的播放顶掉，按钮都回到「试听」
    LOG_INFO("voice record session: preview ended, reason %d.", reason);
    m_flow.OnPreviewEnded();
    m_bar.SetPreviewTotal(m_stopped.m_durationMs);
    //已暂停状态（暂停中试听）：计时位置换回已录时长
    if (m_flow.State() == voice_rec_preview || m_flow.State() == voice_rec_paused)
        m_bar.ApplyState(m_flow.State(), false);
}

bool CVoiceRecordSession::dispatch(int event)
{
    const int64_t recordedMs = m_bar.ElapsedMs(::GetTickCount64());
    const VoiceRecordStep step = m_flow.Handle(event, recordedMs);
    if (!step.m_handled)
        return false;
    for (size_t i = 0; i < step.m_actions.size(); ++i)
        runAction(step.m_actions[i]);
    //动作执行时状态可能又变了（停止太短、继续失败、试听打不开），按最新状态显示
    if (!m_flow.IsActive())
        finish();
    else
        m_bar.ApplyState(m_flow.State(), m_flow.Previewing());
    return true;
}

void CVoiceRecordSession::runAction(int action)
{
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    const uint64_t now = ::GetTickCount64();
    switch (action)
    {
    //暂停录音：语音消息中心交给模块关闭麦克风，计时停住
    case voice_act_pause_record:
        if (!hub.PauseRecord())
            LOG_WARNING("voice record session: pause failed, clientmsgid %s.", m_clientMsgId.c_str());
        m_bar.Pause(now);
        break;

    //继续录音：麦克风被通话占用或打不开时提示原因，退回已暂停
    case voice_act_resume_record:
    {
        const int result = hub.ResumeRecord();
        if (result == flamingomedia::media_voice_record_ok)
        {
            m_bar.Resume(now);
        }
        else
        {
            m_flow.OnResumeFailed();
            const std::wstring hint = VoiceRecordStartHint(result);
            if (!hint.empty() && m_host != NULL)
                m_host->ShowVoiceRecordToast(hint);
        }
        break;
    }

    //停止录音并进入试听：太短或失败时状态机回到空闲，录音条随后收起
    case voice_act_stop_record:
        if (stopRecording(m_stopped))
        {
            m_bar.Pause(now);
            m_bar.SetPreviewTotal(m_stopped.m_durationMs);
        }
        else
        {
            m_stopped = balloonvoice::VoiceRecordOutput();
            m_flow.Reset();
        }
        break;

    //录音中或已暂停时点「发送」：停止后直接发送
    case voice_act_send_recording:
    {
        balloonvoice::VoiceRecordOutput out;
        if (stopRecording(out) && m_host != NULL)
            m_host->SendRecordedVoice(m_clientMsgId, out);
        break;
    }

    //试听状态下点「发送」：发送已停止的那段录音
    case voice_act_send_stopped:
        if (!m_stopped.m_path.empty() && m_host != NULL)
            m_host->SendRecordedVoice(m_clientMsgId, m_stopped);
        m_stopped = balloonvoice::VoiceRecordOutput();
        break;

    //放弃还没停止的录音：不写文件
    case voice_act_cancel_record:
        hub.CancelRecord();
        LOG_INFO("voice record session: recording cancelled, clientmsgid %s.", m_clientMsgId.c_str());
        break;

    //放弃已停止的录音：删除写出的文件
    case voice_act_discard_stopped:
        hub.DiscardRecorded(m_stopped);
        m_stopped = balloonvoice::VoiceRecordOutput();
        break;

    //开始试听：打不开时提示，按钮仍为「试听」
    case voice_act_start_preview:
        if (m_stopped.m_path.empty() || !hub.StartPlay(this, VoicePreviewKey(m_clientMsgId), m_stopped.m_path))
        {
            m_flow.OnPreviewEnded();
            if (m_host != NULL)
                m_host->ShowVoiceRecordToast(VoicePlayFailedHint());
        }
        else
        {
            m_bar.SetPreviewProgress(0, m_stopped.m_durationMs);
        }
        break;

    //暂停中试听（2026-10-05）：先请语音消息中心把已录的部分另存成临时文件，再播放它；另存或播放失败时提示，
    //按钮仍为「试听」。录音保持暂停
    case voice_act_start_paused_preview:
    {
        std::wstring snapshotPath;
        int64_t snapshotMs = 0;
        if (!hub.SnapshotPausedRecord(snapshotPath, snapshotMs) ||
            !hub.StartPlay(this, VoicePreviewKey(m_clientMsgId), snapshotPath))
        {
            m_flow.OnPreviewEnded();
            if (m_host != NULL)
                m_host->ShowVoiceRecordToast(VoicePlayFailedHint());
        }
        else
        {
            m_bar.SetPreviewProgress(0, snapshotMs);
        }
        break;
    }

    //停止试听：语音消息中心随即回调 OnVoicePlayEnded，按钮回到「试听」
    case voice_act_stop_preview:
        if (hub.IsPlayingFor(this))
            hub.StopPlay();
        break;

    //其它取值不会出现
    default:
        break;
    }
}

bool CVoiceRecordSession::stopRecording(balloonvoice::VoiceRecordOutput& out)
{
    const int result = balloonvoice::CVoiceHub::GetInstance().StopRecord(out);
    if (result == balloonvoice::voice_stop_ok)
    {
        LOG_INFO("voice record session: recording stopped, clientmsgid %s, %lld ms, %lld bytes.",
                 m_clientMsgId.c_str(), static_cast<long long>(out.m_durationMs),
                 static_cast<long long>(out.m_fileBytes));
        return true;
    }
    LOG_INFO("voice record session: stop result %d, clientmsgid %s.", result, m_clientMsgId.c_str());
    if (m_host != NULL)
    {
        if (result == balloonvoice::voice_stop_too_short)
            m_host->ShowVoiceRecordToast(VoiceTooShortHint());
        else
            m_host->ShowVoiceRecordToast(TR(L"录音失败，请重试"));
    }
    return false;
}

void CVoiceRecordSession::finish()
{
    if (m_barShown && m_host != NULL)
        m_host->ShowVoiceRecordBarUi(false);
    m_barShown = false;
    m_flow.Reset();
    m_clientMsgId.clear();
}

balloonvoice::VoicePlayKey VoicePreviewKey(const std::string& clientMsgId)
{
    balloonvoice::VoicePlayKey key;
    key.m_msgid = 0;
    key.m_clientMsgId = std::string(kPreviewKeyPrefix) + clientMsgId;
    return key;
}

} // namespace balloonchatwnd
