/**
 *  VoiceRecordBar.h
 *
 *  聊天窗的录音条（语音消息第 3b 步建立，第 6g 步改为四种状态，交互见 docs/语音消息方案.md 第 3.1 节）：录音时占据
 *  输入框的位置。从左到右是红点与计时（试听时换成「播放进度 / 总时长」）、随音量跳动的音量柱（确认放弃时换成
 *  「放弃这段录音？」），右边按状态显示按钮：
 *    - 录音中：「暂停」「停止」「取消」「发送」；已暂停：「继续」「停止」「取消」「发送」，红点变灰、计时停住；
 *    - 试听：「试听 / 停止试听」「取消」「发送」；确认放弃：「确认放弃」「返回」。
 *  单聊窗与群聊窗共用。本类只负责显示，状态切换由 VoiceRecordFlow 决定、由录音会话（CVoiceRecordSession）驱动。
 *
 *  全部用 balloonui 现成控件组合（开发约定文档 UI 约定第 2 条）：红点是空文字的胶囊形 DuiBadge，计时与提示是 DuiLabel，
 *  音量柱是一排竖向的 DuiSeparator（用 SetInset 调上下内缩，即柱子的高矮，只重绘不重排），按钮是 DuiButton。
 *  布局 XML 里只放一个空的横向布局占位，Build 往里面填控件。按钮宽度按文字实际宽度只增不减（译文较长时加宽）。
 *
 *  典型用法（录音会话）：
 *      m_bar.Build(holder, ids);                         //窗口 InitUi 时
 *      m_bar.Begin(::GetTickCount64());                  //开始录音
 *      m_bar.ApplyState(voice_rec_recording, false);
 *      m_bar.PushLevel(level, ::GetTickCount64());       //语音消息中心转来的音量
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "VoiceRecordBarModel.h"

namespace balloonwjui
{
class DuiControl;
class DuiHBox;
class DuiBadge;
class DuiLabel;
class DuiButton;
class DuiSeparator;
}

namespace balloonchatwnd {

/**
 *  录音条上各按钮的控件编号（各聊天窗用自己的号段，见 ChatWindowLayout.h / GroupChatWindowLayout.h）。
 */
struct VoiceRecordBarIds
{
    unsigned int m_pause;     //「暂停 / 继续」
    unsigned int m_stop;      //「停止」
    unsigned int m_preview;   //「试听 / 停止试听」
    unsigned int m_cancel;    //「取消」
    unsigned int m_send;      //「发送」
    unsigned int m_confirm;   //「确认放弃」
    unsigned int m_back;      //「返回」

    VoiceRecordBarIds()
        : m_pause(0)
        , m_stop(0)
        , m_preview(0)
        , m_cancel(0)
        , m_send(0)
        , m_confirm(0)
        , m_back(0)
    {
    }
};

/**
 *  聊天窗的录音条。用法见文件头。
 */
class CVoiceRecordBar
{
public:
    CVoiceRecordBar();

    /**
     *  在布局的占位横向布局里建出录音条的控件。只调用一次。
     *  @param holder 占位的横向布局（不持有，随窗口的控件树销毁）
     *  @param ids    各按钮的控件编号
     */
    void Build(balloonwjui::DuiHBox* holder, const VoiceRecordBarIds& ids);

    /**
     *  开始一次新的录音：计时从 0:00 起，音量柱清零，红点为红色。
     *  @param nowMs 当前时间（毫秒，GetTickCount64 的值），作为计时起点
     */
    void Begin(uint64_t nowMs);

    /**
     *  暂停计时（录音暂停时调用）：把这一段的时长累计起来，之后计时停住。
     *  @param nowMs 当前时间（毫秒）
     */
    void Pause(uint64_t nowMs);

    /**
     *  继续计时（录音继续时调用）：从 nowMs 起接着累计。
     *  @param nowMs 当前时间（毫秒）
     */
    void Resume(uint64_t nowMs);

    /**
     *  到目前为止录到的时长（毫秒）：各段之和，暂停期间不增长，录满后为 2 分钟。
     *  @param nowMs 当前时间（毫秒）
     */
    int64_t ElapsedMs(uint64_t nowMs) const;

    /**
     *  按当前时间刷新计时文字（收到音量时自动调用）。
     *  @param nowMs 当前时间（毫秒，GetTickCount64 的值）
     */
    void Tick(uint64_t nowMs);

    /**
     *  录音中的一个音量（语音消息中心经录音会话转来）：推进音量柱并刷新计时。
     *  @param level 0~255
     *  @param nowMs 当前时间（毫秒）
     */
    void PushLevel(int level, uint64_t nowMs);

    //已录满 2 分钟：计时停在 2:00
    void MarkLimitReached();

    /**
     *  按状态显示红点、计时、音量柱、提示文字与按钮，并重排录音条（SetVisible 不会重排，见 UI 约定第 5 条）。
     *  @param state      取值见 VoiceRecordFlow.h 的 enum VoiceRecordState；空闲时什么也不做
     *  @param previewing 试听状态下是否正在播放（决定按钮写「试听」还是「停止试听」）
     */
    void ApplyState(int state, bool previewing);

    /**
     *  试听状态的时间文字改为总时长（进入试听、试听结束时调用）。
     *  @param totalMs 录音的总时长（毫秒）
     */
    void SetPreviewTotal(int64_t totalMs);

    /**
     *  试听播放中的进度：时间文字写「播放进度 / 总时长」，如「0:03 / 0:12」。
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    void SetPreviewProgress(int64_t playedMs, int64_t totalMs);

    //是否已录满 2 分钟
    bool LimitReached() const
    {
        return m_limitReached;
    }

    //录音中的计时文字（单元测试与窗口取文字用）
    balloonwjui::DuiLabel* TimerLabel() const
    {
        return m_timer;
    }

    //试听状态的时间文字
    balloonwjui::DuiLabel* PreviewTimeLabel() const
    {
        return m_previewTime;
    }

    //确认放弃状态的提示文字「放弃这段录音？」
    balloonwjui::DuiLabel* ConfirmLabel() const
    {
        return m_confirmText;
    }

    //红点
    balloonwjui::DuiBadge* Dot() const
    {
        return m_dot;
    }

    //音量柱所在的横向布局
    balloonwjui::DuiHBox* Wave() const
    {
        return m_wave;
    }

private:
    //录音条按钮的样式
    enum ButtonStyle
    {
        //次操作：白底灰边（暂停 / 继续、停止、试听、取消、返回）
        button_secondary = 0,
        //主操作：品牌主色实心（发送）
        button_primary = 1,
        //不可逆操作：红色实心（确认放弃）
        button_danger = 2,
    };

    //按 m_history 设置每根音量柱的内缩
    void refreshBars();

    /**
     *  建一个按钮并按文字宽度定下它的宽度（取 texts 里最宽的一个，不小于设计宽度）。
     *  @param id    控件编号
     *  @param texts 按钮可能显示的文字（切换文字的按钮给两个）
     *  @param style 样式，取值见 enum ButtonStyle
     *  @return 建好的按钮（属于 m_holder，不持有）
     */
    balloonwjui::DuiButton* addButton(unsigned int id, const std::vector<const wchar_t*>& texts, int style);

private:
    balloonwjui::DuiHBox*                   m_holder;        //占位的横向布局，不持有
    balloonwjui::DuiBadge*                  m_dot;           //红点，不持有（属于 m_holder 的子树）
    balloonwjui::DuiLabel*                  m_timer;         //录音中 / 已暂停 / 确认放弃时的计时文字，不持有
    balloonwjui::DuiLabel*                  m_previewTime;   //试听状态的时间文字，不持有
    balloonwjui::DuiHBox*                   m_wave;          //音量柱所在的横向布局，不持有
    balloonwjui::DuiLabel*                  m_confirmText;   //确认放弃状态的「放弃这段录音？」，不持有
    balloonwjui::DuiButton*                 m_pauseBtn;      //「暂停 / 继续」，不持有
    balloonwjui::DuiButton*                 m_stopBtn;       //「停止」，不持有
    balloonwjui::DuiButton*                 m_previewBtn;    //「试听 / 停止试听」，不持有
    balloonwjui::DuiButton*                 m_cancelBtn;     //「取消」，不持有
    balloonwjui::DuiButton*                 m_sendBtn;       //「发送」，不持有
    balloonwjui::DuiButton*                 m_confirmBtn;    //「确认放弃」，不持有
    balloonwjui::DuiButton*                 m_backBtn;       //「返回」，不持有
    std::vector<balloonwjui::DuiSeparator*> m_bars;          //音量柱，从左到右，不持有
    VoiceLevelHistory                       m_history;       //滚动显示的音量
    uint64_t                                m_segmentStartMs;//当前这一段录音的起点（GetTickCount64 毫秒）
    int64_t                                 m_accumulatedMs; //之前各段（暂停之前）累计的时长（毫秒）
    bool                                    m_timerPaused;   //计时是否停住（录音暂停中）
    bool                                    m_limitReached;  //是否已录满
};

/**
 *  录音时显示录音条、隐藏输入框与底部的发送行；结束后恢复。显隐之后对它们所在的输入区（同一个父容器）
 *  调用 ForceLayout（开发约定文档 UI 约定第 5 条：SetVisible 不会重排）。
 *  @param inputEdit 输入框控件，可以为 NULL
 *  @param bottomRow 底部「提示文字 + 发送按钮」那一行，可以为 NULL
 *  @param voiceBar  录音条的占位横向布局，不能为 NULL
 *  @param show      true 显示录音条，false 恢复输入框
 */
void ShowVoiceRecordBar(balloonwjui::DuiControl* inputEdit, balloonwjui::DuiControl* bottomRow,
                        balloonwjui::DuiControl* voiceBar, bool show);

/**
 *  试听时间文字：「播放进度 / 总时长」，两段都按 FormatVoiceElapsed 写。
 *  @param playedMs 已播放的毫秒数
 *  @param totalMs  总毫秒数
 *  @return 如 L"0:03 / 0:12"
 */
std::wstring FormatVoicePreviewProgress(int64_t playedMs, int64_t totalMs);

} // namespace balloonchatwnd
