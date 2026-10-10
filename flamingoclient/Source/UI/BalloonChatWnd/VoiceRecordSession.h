/**
 *  VoiceRecordSession.h
 *
 *  聊天窗的录音会话（语音消息第 6g 步）：把录音条（CVoiceRecordBar）、流程状态机（VoiceRecordFlow）与语音消息中心
 *  （balloonvoice::CVoiceHub）连起来，单聊窗与群聊窗共用。
 *    - 按钮、Enter、Esc 与「录满」交给状态机，按它给出的动作调用语音消息中心（暂停、继续、停止、取消、丢弃、
 *      开始 / 停止试听），再让录音条按新状态显示；
 *    - 作为语音消息中心的录音接收方（音量、录满）与播放接收方（试听的进度与结束：开始播别的语音时试听被停下，
 *      按钮回到「试听」；开始试听时列表里正在播的语音被停下）；
 *    - 窗口只实现 IVoiceRecordSessionHost 的三件事：显隐录音条、显示提示、发送一段录好的语音。
 *
 *  典型用法（聊天窗）：
 *      m_voiceSession.Init(this, holder, ids);                      //InitUi 时
 *      m_voiceSession.Start(voiceDir, clientMsgId);                 //工具栏「语音消息」
 *      if (m_voiceSession.HandleButton(pNotify->ctrlId)) return;    //OnDuiNotify 的点击
 *      if (m_voiceSession.HandleKey(wParam)) return 0;              //OnKeyDown 的 Enter / Esc
 *      m_voiceSession.OnWindowClosing();                            //关窗与析构
 *
 *  所有方法只在 UI 线程调用。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>

#include "VoiceRecordBar.h"
#include "VoiceRecordFlow.h"
#include "../../Feature/Voice/VoiceHub.h"

namespace balloonchatwnd {

/**
 *  录音会话要宿主窗口做的事。由 CVoiceRecordSession 在 UI 线程上调用。
 */
class IVoiceRecordSessionHost
{
public:
    virtual ~IVoiceRecordSessionHost()
    {
    }

    /**
     *  显示或收起录音条（录音时替换输入框与底部发送行，收起后把焦点还给输入框）。
     *  @param show true 显示录音条，false 恢复输入框
     */
    virtual void ShowVoiceRecordBarUi(bool show) = 0;

    /**
     *  在窗口上显示一条提示（如「说话时间太短」「通话中不能录制语音消息」）。
     *  @param text 提示文字
     */
    virtual void ShowVoiceRecordToast(const std::wstring& text) = 0;

    /**
     *  发送一段录好的语音：插入发送中的语音气泡并上传。
     *  @param clientMsgId 这条语音消息的客户端消息 id（开始录音时生成）
     *  @param out         录音结果（文件已在 ChatVoice\<md5>.ogg）
     */
    virtual void SendRecordedVoice(const std::string& clientMsgId, const balloonvoice::VoiceRecordOutput& out) = 0;
};

/**
 *  聊天窗的录音会话。用法见文件头。
 */
class CVoiceRecordSession : public balloonvoice::IVoiceRecordListener, public balloonvoice::IVoicePlayListener
{
public:
    CVoiceRecordSession();

    //析构时按关窗处理（取消或丢弃没发的录音，不再接收语音消息中心的通知）
    virtual ~CVoiceRecordSession();

    /**
     *  建出录音条并记下宿主。只调用一次。
     *  @param host   宿主窗口（不持有），须存活到 OnWindowClosing 返回
     *  @param holder 录音条的占位横向布局（不持有）
     *  @param ids    录音条上各按钮的控件编号
     */
    void Init(IVoiceRecordSessionHost* host, balloonwjui::DuiHBox* holder, const VoiceRecordBarIds& ids);

    /**
     *  开始录音（工具栏「语音消息」）。失败时（麦克风被通话占用、打不开、别的会话正在录、模块未加载）提示原因，
     *  不显示录音条。已经有录音条在显示时什么也不做。
     *  @param voiceDir    ChatVoice 目录（以反斜杠结尾）
     *  @param clientMsgId 这条语音消息的客户端消息 id
     *  @return 开始成功、录音条已显示时返回 true
     */
    bool Start(const std::wstring& voiceDir, const std::string& clientMsgId);

    /**
     *  处理录音条上的按钮点击（窗口 OnDuiNotify 收到点击时先交给本方法）。
     *  @param ctrlId 被点的控件编号
     *  @return 是录音条上的按钮并已处理时返回 true
     */
    bool HandleButton(unsigned int ctrlId);

    /**
     *  处理 Enter 与 Esc（窗口 OnKeyDown 先交给本方法）。录音条显示时这两个键一律截住，免得隐藏的输入框把 Enter
     *  当成发送文字消息；确认放弃状态下 Enter 不起作用但同样截住。
     *  @param vk 虚拟键码
     *  @return 录音条正在显示且是 Enter 或 Esc 时返回 true
     */
    bool HandleKey(UINT vk);

    //录音条是否正在显示（有一段录音在录、暂停、试听或确认放弃）
    bool IsActive() const
    {
        return m_flow.IsActive();
    }

    //窗口关闭（OnCloseMsg 与析构）：还没停止的录音按取消处理，已停止的录音丢弃，停止试听，不再接收通知。可以重复调用
    void OnWindowClosing();

    //当前状态（取值见 enum VoiceRecordState，单元测试用）
    int State() const
    {
        return m_flow.State();
    }

    //试听状态下是否正在播放（单元测试用）
    bool Previewing() const
    {
        return m_flow.Previewing();
    }

    //录音条（单元测试检查显示用）
    CVoiceRecordBar& Bar()
    {
        return m_bar;
    }

    //以下为 balloonvoice::IVoiceRecordListener 的实现：语音消息中心在 UI 线程上调用

    //录音音量：推进录音条的音量柱
    virtual void OnVoiceRecordLevel(int level) override;

    //已录满 2 分钟：自动停止，进入试听
    virtual void OnVoiceRecordLimitReached() override;

    //以下为 balloonvoice::IVoicePlayListener 的实现：试听的进度与结束

    //试听进度：时间文字写「播放进度 / 总时长」
    virtual void OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs, int64_t totalMs) override;

    //试听结束（播完、被停止、被别处的播放顶掉）：按钮回到「试听」，时间文字回到总时长
    virtual void OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason) override;

private:
    /**
     *  把一个事件交给状态机，按顺序执行它给出的动作，再按新状态更新录音条（回到空闲时收起录音条）。
     *  @param event 取值见 enum VoiceRecordEvent
     *  @return 事件在当前状态下有意义并已处理时返回 true
     */
    bool dispatch(int event);

    /**
     *  执行一个动作。
     *  @param action 取值见 enum VoiceRecordAction
     */
    void runAction(int action);

    /**
     *  停止录音并取得结果；太短或失败时提示原因。
     *  @param out [出参] 录音结果，返回 true 时有效
     *  @return 停止成功、可以发送或试听时返回 true
     */
    bool stopRecording(balloonvoice::VoiceRecordOutput& out);

    //收起录音条、清掉本次录音的记录，状态机回到空闲
    void finish();

private:
    IVoiceRecordSessionHost*        m_host;         //宿主窗口，不持有；Init 之前为 NULL
    CVoiceRecordBar                 m_bar;          //录音条（只负责显示）
    VoiceRecordFlow                 m_flow;         //流程状态机
    VoiceRecordBarIds               m_ids;          //录音条上各按钮的控件编号
    std::string                     m_clientMsgId;  //本次录音的客户端消息 id；没有录音时为空
    balloonvoice::VoiceRecordOutput m_stopped;      //试听状态下那段已停止的录音；没有时 m_path 为空
    bool                            m_barShown;     //录音条是否已经显示（避免重复调用宿主显隐）
};

/**
 *  试听用的播放键：msgid 为 0，clientMsgId 加前缀，与列表里的语音区分开。
 *  @param clientMsgId 本次录音的客户端消息 id
 *  @return 播放键
 */
balloonvoice::VoicePlayKey VoicePreviewKey(const std::string& clientMsgId);

} // namespace balloonchatwnd
