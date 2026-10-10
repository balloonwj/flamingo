/**
 *  录音条流程状态机的单元测试（前缀 VM，标签 [voicemsg]），用例见 docs/语音消息方案.md 第 9 节第 6 期：
 *    - VM41 录音中、已暂停、试听、确认放弃四种状态下，每个按钮、Esc、Enter、录满 2 分钟、不到 1 秒时转到哪个状态、
 *           要做什么动作（暂停、继续、停止、发送、丢弃、开始或停止试听）；继续失败、试听结束的回退（第 6f 步）；
 *    - VM48 已暂停时试听（2026-10-05）：试听 / 停止试听、试听中继续、停止、发送、取消都先停止试听，试听结束后
 *           按钮复原，确认放弃返回后回到已暂停且没在试听。
 *  纯逻辑，不碰窗口与语音消息中心。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonChatWnd/VoiceRecordFlow.h"

using namespace balloonchatwnd;

namespace {

//录到的时长（毫秒）：够 1 秒与不到 1 秒
const int64_t kLongEnoughMs = 3000;
const int64_t kTooShortMs = 600;

//只有一个动作的结果
std::vector<int> one(int action)
{
    return std::vector<int>(1, action);
}

//两个动作的结果（按顺序）
std::vector<int> two(int first, int second)
{
    std::vector<int> v;
    v.push_back(first);
    v.push_back(second);
    return v;
}

//处于录音中的状态机
VoiceRecordFlow recordingFlow()
{
    VoiceRecordFlow flow;
    flow.Begin();
    return flow;
}

//处于已暂停的状态机
VoiceRecordFlow pausedFlow()
{
    VoiceRecordFlow flow = recordingFlow();
    flow.Handle(voice_ev_pause, kLongEnoughMs);
    return flow;
}

//处于试听（没在播放）的状态机
VoiceRecordFlow previewFlow()
{
    VoiceRecordFlow flow = recordingFlow();
    flow.Handle(voice_ev_stop, kLongEnoughMs);
    return flow;
}

} // namespace

TEST_CASE("VM41 录音条状态机：空闲与开始", "[voicemsg]")
{
    VoiceRecordFlow flow;
    CHECK(flow.State() == voice_rec_idle);
    CHECK_FALSE(flow.IsActive());
    //空闲时任何事件都没有意义（Enter、Esc 留给输入框与窗口照常处理）
    const int events[] = { voice_ev_pause, voice_ev_resume, voice_ev_stop, voice_ev_send, voice_ev_cancel,
                           voice_ev_confirm_discard, voice_ev_back, voice_ev_limit_reached, voice_ev_preview_toggle,
                           voice_ev_escape, voice_ev_enter };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
    {
        const VoiceRecordStep step = flow.Handle(events[i], kLongEnoughMs);
        CHECK_FALSE(step.m_handled);
        CHECK(step.m_actions.empty());
        CHECK(flow.State() == voice_rec_idle);
    }
    flow.Begin();
    CHECK(flow.State() == voice_rec_recording);
    CHECK(flow.IsActive());
}

TEST_CASE("VM41 录音条状态机：录音中", "[voicemsg]")
{
    SECTION("暂停：进入已暂停，暂停录音")
    {
        VoiceRecordFlow flow = recordingFlow();
        const VoiceRecordStep step = flow.Handle(voice_ev_pause, kLongEnoughMs);
        CHECK(step.m_handled);
        CHECK(step.m_next == voice_rec_paused);
        CHECK(step.m_actions == one(voice_act_pause_record));
        CHECK(flow.State() == voice_rec_paused);
    }
    SECTION("停止：进入试听，停止录音写文件")
    {
        VoiceRecordFlow flow = recordingFlow();
        const VoiceRecordStep step = flow.Handle(voice_ev_stop, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_preview);
        CHECK(step.m_actions == one(voice_act_stop_record));
        CHECK_FALSE(flow.Previewing());
    }
    SECTION("发送与 Enter：停止并发送，回到空闲")
    {
        VoiceRecordFlow flow = recordingFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_send, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_send_recording));
        flow = recordingFlow();
        step = flow.Handle(voice_ev_enter, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_send_recording));
    }
    SECTION("取消与 Esc：够 1 秒时先暂停再进入确认放弃")
    {
        VoiceRecordFlow flow = recordingFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_cancel, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_confirm_discard);
        CHECK(step.m_actions == one(voice_act_pause_record));
        CHECK(flow.StateBeforeConfirm() == voice_rec_recording);
        flow = recordingFlow();
        step = flow.Handle(voice_ev_escape, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_confirm_discard);
        CHECK(step.m_actions == one(voice_act_pause_record));
    }
    SECTION("取消与 Esc：不到 1 秒直接放弃，不再确认")
    {
        VoiceRecordFlow flow = recordingFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_cancel, kTooShortMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_cancel_record));
        flow = recordingFlow();
        step = flow.Handle(voice_ev_escape, kTooShortMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_cancel_record));
    }
    SECTION("录满 2 分钟：自动停止，进入试听")
    {
        VoiceRecordFlow flow = recordingFlow();
        const VoiceRecordStep step = flow.Handle(voice_ev_limit_reached, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_preview);
        CHECK(step.m_actions == one(voice_act_stop_record));
    }
    SECTION("继续、确认放弃、返回、试听在录音中没有意义")
    {
        VoiceRecordFlow flow = recordingFlow();
        CHECK_FALSE(flow.Handle(voice_ev_resume, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_confirm_discard, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_back, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_preview_toggle, kLongEnoughMs).m_handled);
        CHECK(flow.State() == voice_rec_recording);
    }
}

TEST_CASE("VM41 录音条状态机：已暂停", "[voicemsg]")
{
    SECTION("继续：回到录音中，继续录音；继续失败时退回已暂停")
    {
        VoiceRecordFlow flow = pausedFlow();
        const VoiceRecordStep step = flow.Handle(voice_ev_resume, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_recording);
        CHECK(step.m_actions == one(voice_act_resume_record));
        flow.OnResumeFailed();
        CHECK(flow.State() == voice_rec_paused);
    }
    SECTION("停止：进入试听；发送与 Enter：停止并发送")
    {
        VoiceRecordFlow flow = pausedFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_stop, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_preview);
        CHECK(step.m_actions == one(voice_act_stop_record));
        flow = pausedFlow();
        step = flow.Handle(voice_ev_send, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_send_recording));
        flow = pausedFlow();
        step = flow.Handle(voice_ev_enter, kLongEnoughMs);
        CHECK(step.m_actions == one(voice_act_send_recording));
    }
    SECTION("取消：够 1 秒时进入确认放弃（已经暂停，不再暂停）；不到 1 秒直接放弃")
    {
        VoiceRecordFlow flow = pausedFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_cancel, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_confirm_discard);
        CHECK(step.m_actions.empty());
        CHECK(flow.StateBeforeConfirm() == voice_rec_paused);
        flow = pausedFlow();
        step = flow.Handle(voice_ev_cancel, kTooShortMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_cancel_record));
    }
    SECTION("暂停、录满在已暂停时没有意义（试听见 VM48）")
    {
        VoiceRecordFlow flow = pausedFlow();
        CHECK_FALSE(flow.Handle(voice_ev_pause, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_limit_reached, kLongEnoughMs).m_handled);
        CHECK(flow.State() == voice_rec_paused);
    }
}

TEST_CASE("VM41 录音条状态机：试听", "[voicemsg]")
{
    SECTION("试听与停止试听轮换；试听结束后按钮回到「试听」")
    {
        VoiceRecordFlow flow = previewFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_preview);
        CHECK(step.m_actions == one(voice_act_start_preview));
        CHECK(flow.Previewing());
        step = flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        CHECK(step.m_actions == one(voice_act_stop_preview));
        CHECK_FALSE(flow.Previewing());
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        flow.OnPreviewEnded();
        CHECK_FALSE(flow.Previewing());
        CHECK(flow.State() == voice_rec_preview);
    }
    SECTION("发送与 Enter：发送已停止的录音；正在试听时先停止试听")
    {
        VoiceRecordFlow flow = previewFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_send, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_send_stopped));
        flow = previewFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        step = flow.Handle(voice_ev_enter, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == two(voice_act_stop_preview, voice_act_send_stopped));
        CHECK_FALSE(flow.Previewing());
    }
    SECTION("取消与 Esc：进入确认放弃；正在试听时先停止试听")
    {
        VoiceRecordFlow flow = previewFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_cancel, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_confirm_discard);
        CHECK(step.m_actions.empty());
        CHECK(flow.StateBeforeConfirm() == voice_rec_preview);
        flow = previewFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        step = flow.Handle(voice_ev_escape, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_confirm_discard);
        CHECK(step.m_actions == one(voice_act_stop_preview));
        CHECK_FALSE(flow.Previewing());
    }
    SECTION("暂停、继续、停止、录满在试听时没有意义")
    {
        VoiceRecordFlow flow = previewFlow();
        CHECK_FALSE(flow.Handle(voice_ev_pause, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_resume, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_stop, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_limit_reached, kLongEnoughMs).m_handled);
        CHECK(flow.State() == voice_rec_preview);
    }
}

TEST_CASE("VM41 录音条状态机：确认放弃", "[voicemsg]")
{
    SECTION("从录音中进入：确认放弃即取消录音；返回与 Esc 回到录音中并继续录音")
    {
        VoiceRecordFlow flow = recordingFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        VoiceRecordStep step = flow.Handle(voice_ev_confirm_discard, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_cancel_record));

        flow = recordingFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        step = flow.Handle(voice_ev_back, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_recording);
        CHECK(step.m_actions == one(voice_act_resume_record));

        flow = recordingFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        step = flow.Handle(voice_ev_escape, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_recording);
        CHECK(step.m_actions == one(voice_act_resume_record));
        //继续失败：退回已暂停
        flow.OnResumeFailed();
        CHECK(flow.State() == voice_rec_paused);
    }
    SECTION("从已暂停进入：返回时仍是已暂停，不继续录音")
    {
        VoiceRecordFlow flow = pausedFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        const VoiceRecordStep step = flow.Handle(voice_ev_back, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_paused);
        CHECK(step.m_actions.empty());
        flow = pausedFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        CHECK(flow.Handle(voice_ev_confirm_discard, kLongEnoughMs).m_actions == one(voice_act_cancel_record));
    }
    SECTION("从试听进入：确认放弃即丢弃已停止的录音；返回回到试听（没在播放）")
    {
        VoiceRecordFlow flow = previewFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        VoiceRecordStep step = flow.Handle(voice_ev_confirm_discard, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == one(voice_act_discard_stopped));
        flow = previewFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        step = flow.Handle(voice_ev_back, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_preview);
        CHECK(step.m_actions.empty());
        CHECK_FALSE(flow.Previewing());
    }
    SECTION("Enter 与其余按钮在确认放弃时没有意义")
    {
        VoiceRecordFlow flow = recordingFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        CHECK_FALSE(flow.Handle(voice_ev_enter, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_send, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_pause, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_stop, kLongEnoughMs).m_handled);
        CHECK_FALSE(flow.Handle(voice_ev_preview_toggle, kLongEnoughMs).m_handled);
        CHECK(flow.State() == voice_rec_confirm_discard);
    }
}

TEST_CASE("VM41 录音条状态机：收起后回到空闲", "[voicemsg]")
{
    VoiceRecordFlow flow = previewFlow();
    flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
    flow.Reset();
    CHECK(flow.State() == voice_rec_idle);
    CHECK_FALSE(flow.Previewing());
    CHECK_FALSE(flow.IsActive());
    //没在继续录音时调用 OnResumeFailed：不改状态
    flow.OnResumeFailed();
    CHECK(flow.State() == voice_rec_idle);
}

TEST_CASE("VM48 录音条状态机：已暂停时试听", "[voicemsg]")
{
    SECTION("试听与停止试听：状态保持已暂停，开始时另存已录的部分再播放")
    {
        VoiceRecordFlow flow = pausedFlow();
        VoiceRecordStep step = flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        CHECK(step.m_handled);
        CHECK(step.m_next == voice_rec_paused);
        CHECK(step.m_actions == one(voice_act_start_paused_preview));
        CHECK(flow.Previewing());
        step = flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_paused);
        CHECK(step.m_actions == one(voice_act_stop_preview));
        CHECK_FALSE(flow.Previewing());
    }
    SECTION("试听播完或播放失败：按钮复原，可以再试听")
    {
        VoiceRecordFlow flow = pausedFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        flow.OnPreviewEnded();
        CHECK_FALSE(flow.Previewing());
        CHECK(flow.State() == voice_rec_paused);
        CHECK(flow.Handle(voice_ev_preview_toggle, kLongEnoughMs).m_actions == one(voice_act_start_paused_preview));
    }
    SECTION("试听中点继续：先停止试听再继续录音")
    {
        VoiceRecordFlow flow = pausedFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        const VoiceRecordStep step = flow.Handle(voice_ev_resume, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_recording);
        CHECK(step.m_actions == two(voice_act_stop_preview, voice_act_resume_record));
        CHECK_FALSE(flow.Previewing());
        //继续失败退回已暂停时，没有在试听
        flow.OnResumeFailed();
        CHECK(flow.State() == voice_rec_paused);
        CHECK_FALSE(flow.Previewing());
    }
    SECTION("试听中点停止、发送、Enter：先停止试听")
    {
        VoiceRecordFlow flow = pausedFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        VoiceRecordStep step = flow.Handle(voice_ev_stop, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_preview);
        CHECK(step.m_actions == two(voice_act_stop_preview, voice_act_stop_record));
        CHECK_FALSE(flow.Previewing());

        flow = pausedFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        step = flow.Handle(voice_ev_send, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == two(voice_act_stop_preview, voice_act_send_recording));

        flow = pausedFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        step = flow.Handle(voice_ev_enter, kLongEnoughMs);
        CHECK(step.m_actions == two(voice_act_stop_preview, voice_act_send_recording));
    }
    SECTION("试听中点取消或 Esc：先停止试听；够 1 秒进入确认放弃，返回后回到已暂停；不到 1 秒直接放弃")
    {
        VoiceRecordFlow flow = pausedFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        VoiceRecordStep step = flow.Handle(voice_ev_cancel, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_confirm_discard);
        CHECK(step.m_actions == one(voice_act_stop_preview));
        CHECK(flow.StateBeforeConfirm() == voice_rec_paused);
        CHECK_FALSE(flow.Previewing());
        step = flow.Handle(voice_ev_back, kLongEnoughMs);
        CHECK(step.m_next == voice_rec_paused);
        CHECK(step.m_actions.empty());
        CHECK_FALSE(flow.Previewing());

        flow = pausedFlow();
        flow.Handle(voice_ev_preview_toggle, kLongEnoughMs);
        step = flow.Handle(voice_ev_escape, kTooShortMs);
        CHECK(step.m_next == voice_rec_idle);
        CHECK(step.m_actions == two(voice_act_stop_preview, voice_act_cancel_record));
    }
    SECTION("录音中、确认放弃时点试听没有意义")
    {
        VoiceRecordFlow flow = recordingFlow();
        CHECK_FALSE(flow.Handle(voice_ev_preview_toggle, kLongEnoughMs).m_handled);
        flow = pausedFlow();
        flow.Handle(voice_ev_cancel, kLongEnoughMs);
        CHECK_FALSE(flow.Handle(voice_ev_preview_toggle, kLongEnoughMs).m_handled);
        CHECK(flow.State() == voice_rec_confirm_discard);
    }
}
