/**
 *  聊天窗录音条的流程状态机，说明见 VoiceRecordFlow.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "VoiceRecordFlow.h"
#include "../../Feature/Voice/VoiceHub.h"   // kVoiceMinDurationMs：不到 1 秒的录音点「取消」直接放弃

namespace balloonchatwnd {

VoiceRecordFlow::VoiceRecordFlow()
    : m_state(voice_rec_idle)
    , m_beforeConfirm(voice_rec_idle)
    , m_previewing(false)
{
}

void VoiceRecordFlow::Begin()
{
    m_state = voice_rec_recording;
    m_beforeConfirm = voice_rec_idle;
    m_previewing = false;
}

VoiceRecordStep VoiceRecordFlow::Handle(int event, int64_t recordedMs)
{
    VoiceRecordStep step;
    step.m_next = m_state;
    switch (m_state)
    {
    //录音中：暂停、停止、发送、取消，录满时自动停止
    case voice_rec_recording:
        if (event == voice_ev_pause)
        {
            step.m_next = voice_rec_paused;
            step.m_actions.push_back(voice_act_pause_record);
        }
        else if (event == voice_ev_stop || event == voice_ev_limit_reached)
        {
            step.m_next = voice_rec_preview;
            step.m_actions.push_back(voice_act_stop_record);
        }
        else if (event == voice_ev_send || event == voice_ev_enter)
        {
            step.m_next = voice_rec_idle;
            step.m_actions.push_back(voice_act_send_recording);
        }
        else if (event == voice_ev_cancel || event == voice_ev_escape)
        {
            handleCancel(recordedMs, step);
        }
        else
        {
            return step;
        }
        break;

    //已暂停：继续、试听 / 停止试听（2026-10-05）、停止、发送、取消（暂停期间不会录满）。正在试听时，继续、停止、
    //发送之前都先停止试听
    case voice_rec_paused:
        if (event == voice_ev_preview_toggle)
        {
            step.m_actions.push_back(m_previewing ? voice_act_stop_preview : voice_act_start_paused_preview);
            m_previewing = !m_previewing;
        }
        else if (event == voice_ev_resume)
        {
            stopPreviewFirst(step);
            step.m_next = voice_rec_recording;
            step.m_actions.push_back(voice_act_resume_record);
        }
        else if (event == voice_ev_stop)
        {
            stopPreviewFirst(step);
            step.m_next = voice_rec_preview;
            step.m_actions.push_back(voice_act_stop_record);
        }
        else if (event == voice_ev_send || event == voice_ev_enter)
        {
            stopPreviewFirst(step);
            step.m_next = voice_rec_idle;
            step.m_actions.push_back(voice_act_send_recording);
        }
        else if (event == voice_ev_cancel || event == voice_ev_escape)
        {
            handleCancel(recordedMs, step);
        }
        else
        {
            return step;
        }
        break;

    //试听：试听 / 停止试听、发送、取消；录音已经停止，暂停、继续、停止都没有意义
    case voice_rec_preview:
        if (event == voice_ev_preview_toggle)
        {
            step.m_actions.push_back(m_previewing ? voice_act_stop_preview : voice_act_start_preview);
            m_previewing = !m_previewing;
        }
        else if (event == voice_ev_send || event == voice_ev_enter)
        {
            stopPreviewFirst(step);
            step.m_next = voice_rec_idle;
            step.m_actions.push_back(voice_act_send_stopped);
        }
        else if (event == voice_ev_cancel || event == voice_ev_escape)
        {
            handleCancel(recordedMs, step);
        }
        else
        {
            return step;
        }
        break;

    //确认放弃：确认放弃、返回（Esc 等于返回）；Enter 不起作用，免得误发
    case voice_rec_confirm_discard:
        if (event == voice_ev_confirm_discard)
        {
            step.m_next = voice_rec_idle;
            step.m_actions.push_back(m_beforeConfirm == voice_rec_preview ? voice_act_discard_stopped
                                                                         : voice_act_cancel_record);
        }
        else if (event == voice_ev_back || event == voice_ev_escape)
        {
            //回到确认之前的状态；原来在录音的，进入确认时暂停过，这里接着录
            step.m_next = m_beforeConfirm;
            if (m_beforeConfirm == voice_rec_recording)
            {
                step.m_actions.push_back(voice_act_resume_record);
            }
        }
        else
        {
            return step;
        }
        break;

    //空闲：录音条没有显示，任何事件都不处理，按键交还给窗口与输入框
    default:
        return step;
    }
    step.m_handled = true;
    m_state = step.m_next;
    return step;
}

void VoiceRecordFlow::OnPreviewEnded()
{
    m_previewing = false;
}

void VoiceRecordFlow::OnResumeFailed()
{
    //只有「继续」刚把状态切到录音中时才需要退回
    if (m_state == voice_rec_recording)
    {
        m_state = voice_rec_paused;
    }
}

void VoiceRecordFlow::Reset()
{
    m_state = voice_rec_idle;
    m_beforeConfirm = voice_rec_idle;
    m_previewing = false;
}

void VoiceRecordFlow::handleCancel(int64_t recordedMs, VoiceRecordStep& step)
{
    //正在试听的（试听状态或已暂停状态）先停止试听
    stopPreviewFirst(step);
    //还没停止、又不到 1 秒的录音：没有值得确认的内容，直接放弃
    if (m_state != voice_rec_preview && recordedMs < balloonvoice::kVoiceMinDurationMs)
    {
        step.m_next = voice_rec_idle;
        step.m_actions.push_back(voice_act_cancel_record);
        return;
    }
    //正在录的先暂停，再显示「放弃这段录音？」
    if (m_state == voice_rec_recording)
    {
        step.m_actions.push_back(voice_act_pause_record);
    }
    m_beforeConfirm = m_state;
    step.m_next = voice_rec_confirm_discard;
}

void VoiceRecordFlow::stopPreviewFirst(VoiceRecordStep& step)
{
    if (!m_previewing)
        return;
    step.m_actions.push_back(voice_act_stop_preview);
    m_previewing = false;
}

} // namespace balloonchatwnd
