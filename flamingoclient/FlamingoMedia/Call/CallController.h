/**
 *  一对一通话控制器：FlamingoMedia.dll 里把一通电话从拨出 / 来电到挂断的全过程集中在一个类里。
 *
 *  由 exe 的通话协调器（UI/BalloonMain/MainPanelCallCoordinator，第 1c 步删除）改写而来，行为保持一致，
 *  只是依赖的来源换了：
 *    - 信令：收到的是主连接原样转交的 JSON（IMediaModule::OnSignal → OnSignal），用 CallProtocolClient
 *      解析；发出时用 CallProtocolClient 编好包体，经宿主接口 IMediaHost::SendSignal 从主连接发出；
 *    - 静音开关、本人用户号、通话结果提示、主窗口句柄，都改从宿主接口取；
 *    - 中转会话事件与「通话窗关窗请停媒体」两条异步消息，由本类自己的消息窗口接收
 *      （仅接收消息的 HWND_MESSAGE 窗口，消息号见 CallMessages.h）。
 *
 *  一通电话涉及的两个对象（通话窗 CBalloonCallDlg、中转会话 CallMediaSession）都归本类持有，
 *  登出、关闭模块时的收尾写在一处（ResetForLogout）。通话窗的用户操作（取消 / 接听 / 拒绝 / 挂断 /
 *  切换媒体）经 ICallDialogActions 交回本类发信令。
 *
 *  P2P（docs/P2P通信方案.md 第 6 期）：Init 时经宿主取 P2P 服务登记用途 call（环境变量 FLAMINGO_CALL_P2P 为 0 时
 *  不登记）。服务端在响铃阶段就为通话建 P2P 会话，邀请应答（主叫）与来电推送（被叫）带 p2psessionid 时本类按通话
 *  会话号认领链路槽，接通时交给媒体会话（音视频走数据报通道）；没接通就结束（拒绝、取消、超时、关窗）时放弃这个
 *  会话号，链路随之关闭。
 *
 *  线程模型：所有公开方法只在 UI 线程调用（与 exe 的消息循环同一线程）。
 *
 *  典型用法（见 Host/MediaModule.cpp）：
 *      m_call.reset(new call::CCallController());
 *      m_call->Init(host);
 *      ...
 *      m_call->OnSignal(cmd, body);        //主连接转交来的 1701~1709
 *      m_call->StartCall(peer, name, video);
 *      ...
 *      m_call->Shutdown();
 *
 *  包含本头文件之前调用方须已引入 ATL / WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include "BalloonCallDlg.h"
#include "CallMediaSession.h"
#include "CallDialogActions.h"
#include "CallShareLink.h"
#include "SystemAudioExclusive.h"   //共享电脑声音的进程内互斥（与屏幕共享那一路，2026-10-06）
#include "CallMessages.h"
#include "CallProtocolClient.h"
#include "../Api/FlamingoMediaApi.h"

namespace call {

class CCallController : public CWindowImpl<CCallController, CWindow, CWinTraits<0, 0> >,
                        public ICallDialogActions,
                        public ICallShareObserver,
                        public SystemAudioHolder
{
public:
    DECLARE_WND_CLASS(_T("FlamingoMediaCallController"))

    CCallController();

    //析构时收尾（等同 Shutdown）：通话窗与消息窗口都是 ATL 窗口，析构时仍持有 HWND 会触发断言。
    ~CCallController();

    CCallController(const CCallController&) = delete;
    CCallController& operator=(const CCallController&) = delete;

    /**
     *  初始化：登记宿主接口，建本类的消息窗口，把本类登记为通话窗的操作接收方。
     *  @param host 宿主接口（不拥有；须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @return 成功返回 true；host 为 NULL 或消息窗口建不出来返回 false
     */
    bool Init(flamingomedia::IMediaHost* host);

    //关闭：先按登出收尾（停铃声、停媒体、关通话窗），再销毁消息窗口。未初始化或重复调用都安全。
    void Shutdown();

    /**
     *  把通话收尾到「没有会话」的状态：停铃声、停中转会话（释放摄像头与麦克风）、关掉通话窗。
     *  幂等，没有通话时调用也安全。用户登出、被踢下线与模块关闭时都要调它。
     */
    void ResetForLogout();

    /**
     *  有用户的头像变了（IMediaModule::OnAvatarsUpdated 转来）：通话窗重新取对方与本人的头像。没有通话窗时什么也不做。
     */
    void OnAvatarsUpdated();

    /**
     *  锁屏开始或结束（docs/锁屏方案.md 第 4.9 节）。开始时停掉正在响的铃声；锁屏期间来电在 HandleInviteNotify 里
     *  静默记下（不弹窗、不响铃、不回绝）；解锁时若那通来电仍在响铃期内，补弹来电窗并响铃。界面线程调用。
     *  @param locked true 表示锁屏开始
     */
    void OnScreenLockChanged(bool locked);

    /**
     *  判断一个命令号是否归通话处理（cmd_call_invite 1701 ~ cmd_call_media_changed_notify 1709）。
     *  @param cmd 命令号
     *  @return 属于通话返回 true
     */
    static bool IsCallSignal(uint16_t cmd);

    /**
     *  麦克风是否正被通话使用（语音消息据此决定能不能录音，见 docs/语音消息方案.md V6）。
     *  @return 已接通、媒体会话在运行时返回 true；呼叫中、来电振铃中与空闲时返回 false
     */
    bool IsMicrophoneInUse() const;

    /**
     *  处理主连接转交来的一条通话信令。
     *  @param cmd  命令号，须满足 IsCallSignal
     *  @param body 包体（UTF-8 JSON）
     *  @return 命令号归通话处理返回 true（包体解析失败也返回 true，只记错误日志）；未初始化或命令号不归通话返回 false
     */
    bool OnSignal(uint16_t cmd, const std::string& body);

    /**
     *  向某个联系人发起一对一通话：发 cmd_call_invite，并弹出「呼叫中」通话窗。
     *  @param peerUserId 对方用户号（>0）
     *  @param peerName   对方显示名（呼叫窗标题用）
     *  @param video      true 视频通话，false 语音通话
     *  @return 已发出邀请并打开呼叫窗返回 true；未初始化、参数非法或信令发送失败返回 false
     */
    bool StartCall(int64_t peerUserId, const CString& peerName, bool video);

    /**
     *  登记屏幕共享控制器（第 8 期：通话中共享屏幕）。接通、结束时告诉它，通话窗的「共享屏幕」转给它。
     *  @param link 不拥有，须在本类 Shutdown 之前一直有效，或在那之前改登记为 NULL；可为 NULL（不能在通话中共享）
     */
    void SetShareLink(ICallShareLink* link);

    // ---- ICallShareObserver：屏幕共享告知是否正在与通话对方共享，刷新通话窗按钮 ----
    virtual void OnInCallShareChanged(bool hostingWithPeer) override;

    // ---- ICallDialogActions：通话窗的用户操作，编好包体后经宿主发出 ----

    //主叫在接通前取消呼叫（cmd_call_cancel）。
    virtual void CancelCall(const std::string& sessionId) override;

    //被叫接听 / 拒绝来电（cmd_call_response）。
    virtual void RespondCall(const std::string& sessionId, bool accept) override;

    //接通后挂断（cmd_call_hangup）。
    virtual void HangupCall(const std::string& sessionId) override;

    //通话中请求切换媒体类型（cmd_call_switch_media）。
    virtual void SwitchCallMedia(const std::string& sessionId, int media) override;

    //通话中点了「共享屏幕 / 停止共享」：转给屏幕共享控制器
    virtual void ToggleScreenShare() override;

    //通话中点了「共享电脑声音」（2026-10-06）：开关媒体会话里的电脑声音共享，打开成功时登记互斥、让屏幕共享那一路停下
    virtual int SetSystemAudio(bool on) override;

    // ---- SystemAudioHolder：屏幕共享开始共享电脑声音，通话这一路停下并提示（2026-10-06）----
    virtual void OnSystemAudioPreempted() override;

    /**
     *  视频通话中点了「开启 / 关闭摄像头」：经媒体会话真正启停本端摄像头（打开时先关一次再开，采集中途断掉后也能重新打开）。
     *  @param on true 打开，false 关闭
     *  @return 操作之后本端摄像头是否在采集
     */
    virtual bool SetLocalCamera(bool on) override;

    /**
     *  通话中点了麦克风按钮：经媒体会话打开或关闭本端麦克风采集（接通前后都可调用，引擎启动时按最后的状态打开）。
     *  @param on true 打开，false 关闭
     *  @return 操作之后本端麦克风是否打开
     */
    virtual bool SetLocalMic(bool on) override;

    /**
     *  音量浮层的滑块变化：转交媒体会话设置本端播放音量。音量跨通话保留（进程存活期间）。
     *  @param percent 音量百分比，0~CallAudioEngine::kVolumeMax
     */
    virtual void SetPlaybackVolume(int percent) override;

    //当前播放音量（%）
    virtual int GetPlaybackVolume() const override;

    //通话网络状况的快照：转交媒体会话（会话内部加锁，任意线程可调）
    virtual CallNetSnapshot GetNetSnapshot() const override;

    BEGIN_MSG_MAP(CCallController)
        MESSAGE_HANDLER(kMsgCallStopMedia, OnStopMediaMsg)       //通话窗关窗：停媒体会话释放摄像头 / 麦克风
        MESSAGE_HANDLER(kMsgCallRelayEvent, OnRelayEventMsg)     //中转会话事件（连通 / 失败 / 断开 / 摄像头不可用）
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)                    //中转意外断开后等待状态推送的定时器到期
    END_MSG_MAP()

private:
    //用户是否关闭了提示音；未初始化时按「不静音」处理
    bool IsMuted() const;

    //宿主主窗口（提示框属主、通话窗居中参照）；取不到时为 NULL
    HWND MainWindow() const;

    //经宿主发出一条通话信令；包体为空或发送失败时记错误日志并返回 false
    bool SendCallSignal(uint16_t cmd, const std::string& body, const char* what);

    //通话结束（任何原因）：告诉屏幕共享控制器，它不再把与对方的共享当作通话中共享
    void NotifyShareCallEnded();

    //六类有内容的信令各自的处理（解析成功后调用）
    void HandleInviteAck(const net::CallInviteResponse& resp);
    void HandleInviteNotify(const net::CallInviteNotify& notify);
    void HandleSignaling(const net::CallSignalingInfo& info);
    void HandleStateNotify(const net::CallStateNotify& state);
    void HandleSwitchMediaResult(const net::CallSwitchMediaResponse& resp);
    void HandleMediaChanged(const net::CallMediaChangedNotify& notify);

    //通话窗关窗时请求停掉媒体会话
    LRESULT OnStopMediaMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //中转会话事件（wParam = call::CallRelayEvent，lParam = 事件参数）
    LRESULT OnRelayEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //中转意外断开后等待状态推送的定时器到期：仍没收到推送就由本端自行结束通话
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //取消「中转断开后等待状态推送」的定时器（收到任何状态推送、通话收尾时调用）
    void CancelRelayLossTimer();

    //向宿主的 P2P 服务登记用途 call；P2P 不可用或按诊断开关关闭时不登记
    void RegisterP2P();

    /**
     *  服务端为这通电话建了 P2P 会话时认领链路槽（邀请应答与来电推送时调用）；先放弃上一通残留的槽。
     *  @param sessionId    通话会话号（也是 P2P 会话的功能会话号）
     *  @param p2pSessionId 应答或推送里的 p2psessionid；为空时不认领
     */
    void ClaimCallSlot(const std::string& sessionId, const std::string& p2pSessionId);

    //这通电话没接通就结束了：放弃持有的链路槽，链路随之关闭（已交给媒体会话的由媒体会话 Stop 时关闭）
    void ReleaseCallSlot();

    /**
     *  按通话窗当前的状态向宿主回报本人的通话状态（IMediaHost::OnCallPresence，主面板据此显示「通话中」徽章）：
     *  通话窗处于通话中态时为「与对方通话中」，否则为不在通话中。与上次回报的相同时不重复回报；对方或媒体类型变了时
     *  先回报旧的结束、再回报新的。接通、切换音视频、通话结束之后各调一次。
     */
    void SyncPresence();

    flamingomedia::IMediaHost* m_host;          //宿主接口（不拥有）；Init 登记，Shutdown 置空
    CBalloonCallDlg            m_callDlg;       //通话会话窗，呼叫中 / 来电 / 通话中三种形态共用一个对象
    CallMediaSession           m_callMedia;     //中转会话：接通后连 imgserver 中转通道收发音视频，持有摄像头与麦克风
    int                        m_callMediaType; //本次通话的媒体类型（net::call_media_audio / call_media_video），发起或来电时记下，接通后据此决定是否启用视频
    ICallShareLink*            m_shareLink;     //屏幕共享控制器（不拥有）；为 NULL 时通话窗的「共享屏幕」不起作用
    balloonp2p::P2PLinkBroker  m_p2pBroker;     //用途 call 的 P2P 业务方：Init 时登记、Shutdown 时注销
    std::shared_ptr<balloonp2p::P2PLinkSlot> m_p2pSlot;   //当前这通电话的链路槽（响铃阶段认领，接通时交给媒体会话后清空）
    std::string                m_p2pSlotSessionId;   //m_p2pSlot 对应的通话会话号；没有槽时为空
    int64_t                    m_presencePeer;  //上次向宿主回报「通话中」的对方 userid；没有回报过或已回报结束时为 0
    int                        m_presenceMedia; //上次回报的媒体类型：1 语音、2 视频；m_presencePeer 为 0 时无意义
    bool                       m_hasLockedInvite;   //锁屏期间是否有一通静默记下、尚无结果的来电
    net::CallInviteNotify      m_lockedInvite;      //那通来电的推送内容，解锁时据此补弹来电窗
    uint64_t                   m_lockedInviteTick;  //收到那通来电的时刻（GetTickCount64），用于扣减剩余振铃时间
};

} // namespace call
