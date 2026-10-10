/**
 *  通话窗把用户操作交给通话控制器的接口。
 *
 *  通话窗（CBalloonCallDlg）只负责显示与收集用户操作，不直接发信令：取消、接听 / 拒绝、
 *  挂断、切换媒体都经本接口交给通话控制器（CCallController），由它编好信令包体再经宿主
 *  接口 IMediaHost::SendSignal 从 exe 的主连接发出。迁入 DLL 之前这几处直接调用
 *  CIUSocket 单例，DLL 里没有它。
 *
 *  所有方法只在 UI 线程调用。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>

#include "CallNetQuality.h"   //CallNetSnapshot：通话窗每秒取一次，画信号图标与悬停详情

namespace call {

class ICallDialogActions
{
public:
    /**
     *  主叫在对方接听之前取消呼叫（cmd_call_cancel）。
     *  @param sessionId 会话 id，为空时忽略（邀请应答还没回来）
     */
    virtual void CancelCall(const std::string& sessionId) = 0;

    /**
     *  被叫接听或拒绝来电（cmd_call_response）。
     *  @param sessionId 会话 id，为空时忽略
     *  @param accept    true 接听，false 拒绝
     */
    virtual void RespondCall(const std::string& sessionId, bool accept) = 0;

    /**
     *  接通后挂断（cmd_call_hangup）。
     *  @param sessionId 会话 id，为空时忽略
     */
    virtual void HangupCall(const std::string& sessionId) = 0;

    /**
     *  通话中请求切换媒体类型（cmd_call_switch_media）。界面与引擎等服务端推回媒体已变更再改。
     *  @param sessionId 会话 id，为空时忽略
     *  @param media     目标媒体类型，取值见 net::call_media
     */
    virtual void SwitchCallMedia(const std::string& sessionId, int media) = 0;

    //通话中点了「共享屏幕 / 停止共享」：交给屏幕共享发起或结束与通话对方的共享（第 8 期）
    virtual void ToggleScreenShare() = 0;

    /**
     *  通话中点了「共享电脑声音」按钮（2026-10-06，docs/音视频通话方案.md 第 10 节）：打开或关闭本端的电脑声音共享。
     *  打开成功时屏幕共享那边若也在共享电脑声音，会被停下（同一时刻只允许一处共享，见 SystemAudioExclusive）。
     *  @param on true 打开，false 关闭
     *  @return 取值见 CallSystemAudioResult（CallSystemAudioSender.h）；关闭恒为 call_sysaudio_ok。操作之后的实际状态
     *          另经 CBalloonCallDlg::SetSystemAudioOn 告诉通话窗
     */
    virtual int SetSystemAudio(bool on) = 0;

    /**
     *  视频通话中点了「开启 / 关闭摄像头」：真正启停本端摄像头的采集与发送，不发信令、
     *  不影响会话的媒体类型，也不影响接收对方画面。
     *  @param on true 打开，false 关闭
     *  @return 操作之后本端摄像头是否在采集；打开失败时返回 false（失败原因另经 SetCameraUnavailable 告诉通话窗）
     */
    virtual bool SetLocalCamera(bool on) = 0;

    /**
     *  通话中点了麦克风按钮：打开或关闭本端麦克风采集（2026-10-05 起），不发信令，对方据此听不到或重新听到本端。
     *  @param on true 打开，false 关闭
     *  @return 操作之后本端麦克风是否打开；打开失败（设备不可用）时返回 false
     */
    virtual bool SetLocalMic(bool on) = 0;

    /**
     *  音量浮层的滑块变化：设置本端播放音量（2026-10-05 起），只影响本端听到的声音。拖动期间会连续调用。
     *  @param percent 音量百分比，0~CallAudioEngine::kVolumeMax
     */
    virtual void SetPlaybackVolume(int percent) = 0;

    //当前播放音量（%），打开音量浮层时用来初始化滑块
    virtual int GetPlaybackVolume() const = 0;

    /**
     *  通话网络状况的快照（网络状况标识第 3 期，2026-10-06）：通话窗的计时器每秒取一次，换算成信号图标、悬停详情
     *  与降级提示。媒体会话还没开始时返回默认值（各项为「未评级」「没有数据」）。
     */
    virtual CallNetSnapshot GetNetSnapshot() const = 0;

protected:
    ~ICallDialogActions()
    {
    }
};

} // namespace call
