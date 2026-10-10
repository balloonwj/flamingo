/**
 *  VoiceRecordFlow.h
 *
 *  聊天窗录音条的流程状态机（语音消息第 6f 步，交互见 docs/语音消息方案.md 第 3.1 节「第 6 期修改」）：
 *  录音中、已暂停、试听、确认放弃四种状态之间怎么切换，以及每次切换时宿主要做哪些动作（暂停、继续、停止、发送、
 *  丢弃、开始或停止试听）。只做判断，不碰窗口、控件与语音消息中心，可以直接做单元测试；单聊窗与群聊窗共用。
 *  2026-10-05 起已暂停状态也能试听已录的部分（docs/音视频通话优化方案.md 第 10 项）。
 *
 *  典型用法（录音会话）：
 *      balloonchatwnd::VoiceRecordFlow flow;
 *      flow.Begin();                                               //开始录音成功之后
 *      balloonchatwnd::VoiceRecordStep step = flow.Handle(balloonchatwnd::voice_ev_pause, recordedMs);
 *      for (size_t i = 0; i < step.m_actions.size(); ++i)
 *          doAction(step.m_actions[i]);                            //按顺序执行
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <vector>

namespace balloonchatwnd {

//录音条所处的状态
enum VoiceRecordState
{
    //没有录音：录音条隐藏，输入框照常显示
    voice_rec_idle = 0,
    //录音中：麦克风在采集，计时走动，按钮「暂停」「停止」「取消」「发送」
    voice_rec_recording = 1,
    //已暂停：麦克风关闭，计时停住，按钮「继续」「试听 / 停止试听」「取消」「发送」（2026-10-05 起暂停中也能试听已录的
    //部分，试听时计时换成播放进度；录音条不再显示「停止」，状态机仍接受「停止」事件）
    voice_rec_paused = 2,
    //试听：录音已停止并写成文件，按钮「试听 / 停止试听」「取消」「发送」
    voice_rec_preview = 3,
    //确认放弃：显示「放弃这段录音？」与「确认放弃」「返回」
    voice_rec_confirm_discard = 4,
};

//用户操作与录音过程中的事件
enum VoiceRecordEvent
{
    //点了「暂停」
    voice_ev_pause = 0,
    //点了「继续」
    voice_ev_resume = 1,
    //点了「停止」
    voice_ev_stop = 2,
    //点了「发送」
    voice_ev_send = 3,
    //点了「取消」
    voice_ev_cancel = 4,
    //点了「确认放弃」
    voice_ev_confirm_discard = 5,
    //点了「返回」
    voice_ev_back = 6,
    //录满 2 分钟（语音消息中心转来的通知）
    voice_ev_limit_reached = 7,
    //点了「试听」或「停止试听」
    voice_ev_preview_toggle = 8,
    //按了 Esc：确认放弃状态下等于「返回」，其余状态等于「取消」
    voice_ev_escape = 9,
    //按了 Enter：等于「发送」，确认放弃状态下不起作用
    voice_ev_enter = 10,
};

//一次切换时宿主要做的动作，按 VoiceRecordStep::m_actions 里的顺序执行
enum VoiceRecordAction
{
    //暂停录音（语音消息中心的 PauseRecord）
    voice_act_pause_record = 0,
    //继续录音（ResumeRecord）；失败时宿主调用 VoiceRecordFlow::OnResumeFailed 退回已暂停
    voice_act_resume_record = 1,
    //停止录音并写文件（StopRecord），成功后进入试听；太短或失败时宿主调用 Reset 并收起录音条
    voice_act_stop_record = 2,
    //停止还没停止的录音并发送（录音中或已暂停时点「发送」），之后收起录音条
    voice_act_send_recording = 3,
    //发送试听状态下那段已停止的录音，之后收起录音条
    voice_act_send_stopped = 4,
    //取消还没停止的录音（不写文件），收起录音条
    voice_act_cancel_record = 5,
    //丢弃试听状态下那段已停止的录音（删除写出的文件），收起录音条
    voice_act_discard_stopped = 6,
    //开始试听已停止的录音；播放失败时宿主调用 OnPreviewEnded
    voice_act_start_preview = 7,
    //停止试听（试听状态与已暂停状态共用）
    voice_act_stop_preview = 8,
    //开始试听已暂停的录音（2026-10-05）：先把已录的部分另存成临时文件再播放，录音保持暂停；另存或播放失败时
    //宿主调用 OnPreviewEnded
    voice_act_start_paused_preview = 9,
};

/**
 *  一次事件处理的结果。
 */
struct VoiceRecordStep
{
    bool             m_handled;   //事件在当前状态下有没有意义；false 时状态不变、没有动作（宿主照常处理按键等）
    int              m_next;      //处理之后的状态，取值见 enum VoiceRecordState
    std::vector<int> m_actions;   //要按顺序执行的动作，取值见 enum VoiceRecordAction

    VoiceRecordStep()
        : m_handled(false)
        , m_next(voice_rec_idle)
    {
    }
};

/**
 *  录音条的流程状态机。所有方法只在 UI 线程调用。
 */
class VoiceRecordFlow
{
public:
    //空闲状态
    VoiceRecordFlow();

    //开始录音成功：进入录音中
    void Begin();

    /**
     *  处理一个事件，返回下一个状态与要做的动作（状态已经切过去）。
     *  @param event      取值见 enum VoiceRecordEvent
     *  @param recordedMs 到目前为止录到的时长（毫秒）；只在「取消」时用来判断不到 1 秒直接放弃、不再确认
     *  @return 处理结果；事件在当前状态下没有意义时 m_handled 为 false
     */
    VoiceRecordStep Handle(int event, int64_t recordedMs);

    //当前状态，取值见 enum VoiceRecordState
    int State() const
    {
        return m_state;
    }

    //进入「确认放弃」之前的状态（「返回」时回到它）；不在确认放弃状态时无意义
    int StateBeforeConfirm() const
    {
        return m_beforeConfirm;
    }

    //试听状态或已暂停状态下是否正在播放
    bool Previewing() const
    {
        return m_previewing;
    }

    //试听播完、播放失败或被别处的播放顶掉（语音消息中心通知「已结束」）：按钮回到「试听」
    void OnPreviewEnded();

    //继续录音失败（麦克风被通话占用、打不开）：退回已暂停
    void OnResumeFailed();

    //回到空闲（录音条收起：发送完、放弃完、停止时太短或失败、窗口关闭）
    void Reset();

    //是否有一段录音在录音条上（不是空闲状态）
    bool IsActive() const
    {
        return m_state != voice_rec_idle;
    }

private:
    /**
     *  点「取消」（或 Esc）：正在试听的先停止试听；不到 1 秒直接放弃，否则进入确认放弃（录音中的先暂停）。
     *  @param recordedMs 录到的时长（毫秒）
     *  @param step       [出参] 填写下一个状态与动作
     */
    void handleCancel(int64_t recordedMs, VoiceRecordStep& step);

    /**
     *  正在试听时先加一个「停止试听」动作并记为没在试听；没在试听时什么也不做。
     *  @param step [出参] 往 m_actions 里追加动作
     */
    void stopPreviewFirst(VoiceRecordStep& step);

private:
    int  m_state;           //当前状态，取值见 enum VoiceRecordState
    int  m_beforeConfirm;   //进入确认放弃之前的状态
    bool m_previewing;      //试听状态或已暂停状态下是否正在播放
};

} // namespace balloonchatwnd
