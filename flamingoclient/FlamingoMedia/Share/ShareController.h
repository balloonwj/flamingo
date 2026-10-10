/**
 *  屏幕共享控制器，ShareController.h
 *
 *  FlamingoMedia.dll 内屏幕共享的总调度（docs/屏幕共享与录制方案.md 第 5 节），把 chatserver 的信令（1720~1732）、
 *  推流 / 收流管线与各个窗口串起来：
 *    - 发起：exe 调 StartShare → 弹出选择窗 → 选好后发 1720 → 收到 1726 中转参数后启动推流（ShareHostStreamer），
 *      显示控制条与边框；控制条可暂停、切换内容、结束共享；
 *    - 观看：exe 调 JoinShare（共享卡片的「加入」）或单聊右下角提示点「加入」→ 发 1722 → 收到 1726 后打开观看窗；
 *    - 通知：1721 有人开始共享（单聊弹右下角提示）、1725 房间状态（观看人数、发起人掉线）、1729 共享结束；
 *      每次状态变化都经 IMediaHost::OnScreenShareStatus 告诉 exe，exe 据此刷新共享卡片与群聊横幅；
 *    - 查询：exe 打开聊天窗时调 QueryShare → 发 1727，应答同样经 OnScreenShareStatus 回给 exe；
 *    - 房间语音（第 4 期）：麦克风、电脑声音、多路混音播放与说话人提示，见 ShareAudioSession；
 *    - 摄像头（第 5 期）：本人摄像头经 ShareCameraSender 发进房间，其他人的摄像头经 ShareCameraReceiver 解码，
 *      发起人显示在摄像头浮动窗口（CShareCameraWindow），观看者显示在观看窗右侧；
 *    - 标注（第 6 期）：本人的笔迹经 ShareAnnotationSession 收发；发起人在被共享内容上盖标注层（CShareAnnotationOverlay），
 *      观看者画在观看窗的画面上；进入标注模式时显示标注工具条；有新成员连上中转时发起人补发全部笔迹；
 *    - 共享录制（第 7 期 7d）：发起人录本机采到的原始画面、观看者录解码出的画面，叠标注与当前发言人的摄像头画中画，
 *      声音为本人麦克风、其他人的声音与电脑声音；录制本身交给录屏控制器（CRecordController），本类经 1731 告诉房间里的人，
 *      并按 1725 显示「某某正在录制」、在发起人收回录制权限时停止本人的录制；
 *    - 多人同时共享（第 22 期，实现在 ShareControllerMulti.cpp）：观看者经主持人同意后另连一条中转连接推自己的屏幕，
 *      主持人可只停自己的画面、退出时移交给另一位共享者或结束整个共享，主持人在「其他人的屏幕」窗口里看其他共享者；
 *    - 白板（第 23 期，实现在 ShareControllerBoard.cpp）：新建共享白板（占共享名额，主持人直接建、其他人经主持人同意），
 *      建立者在白板窗口里画并广播背景与可见区域，其他人在观看窗里看；「屏幕当白板」；标注保存为图片并可发到本会话。
 *  一个用户同时只能在一个房间里（服务端规则），所以本类最多同时有一个「发起」或一个「观看」；观看时可以同时是共享者。
 *
 *  所有方法都在 exe 的 UI 线程上调用；推流线程与各窗口的事件经本类的消息窗口（HWND_MESSAGE，消息号见
 *  ShareMessages.h）转回 UI 线程处理。
 *
 *  典型用法（CMediaModule 里）：
 *      m_share.reset(new screenshare::CShareController());
 *      m_share->Init(host);
 *      if (screenshare::CShareController::IsShareSignal(cmd)) m_share->OnSignal(cmd, json);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include "ShareAnnotationBar.h"
#include "ShareAnnotationOverlay.h"
#include "ShareAnnotationSession.h"
#include "ShareAudioSession.h"
#include "../Call/SystemAudioExclusive.h"   //共享电脑声音的进程内互斥（与通话那一路，2026-10-06）
#include "ShareCameraReceiver.h"
#include "ShareCameraSender.h"
#include "ShareCameraWindow.h"
#include "ShareConfirmDialog.h"
#include "ShareControlBar.h"
#include "ShareBorderWindow.h"
#include "ShareCornerToast.h"
#include "ShareEndedSummary.h"   // ShareParticipantLog：结束提示的名单（2026-10-05）
#include "ShareEndedToast.h"     // CShareEndedToast：「屏幕共享已结束」右下角提示（2026-10-05）
#include "ShareHostStreamer.h"
#include "ShareImageExport.h"
#include "ShareInfoEditDialog.h"
#include "ShareMessages.h"
#include "SharePickerDialog.h"
#include "ShareProtocolClient.h"
#include "ShareRoomPolicy.h"
#include "ShareScreens.h"
#include "ShareScreensWindow.h"
#include "ShareSharerBar.h"
#include "ShareTextInputDialog.h"
#include "ShareViewerListPanel.h"
#include "ShareViewerWindow.h"
#include "ShareWhiteboardWindow.h"
#include "../Api/FlamingoMediaApi.h"
#include "../Call/CallShareLink.h"
#include "../Caption/ShareCaptionController.h"   // 实时字幕（语音字幕）
#include "../Common/MediaToast.h"                // 轻量提示（2026-10-07）
#include "../Record/RecordController.h"

namespace screenshare {

//屏幕共享控制器；消息窗口只收消息
class CShareController : public CWindowImpl<CShareController, CWindow, CWinTraits<0, 0> >,
                         private ShareHostStreamerListener,
                         private IShareCameraFrames,
                         private ShareAnnotationSink,
                         private IShareAnnotationInput,
                         private ShareInputSink,
                         private IShareControlInput,
                         private ShareAudioRecordTap,
                         private screenrecord::IShareRecordOwner,
                         private IShareCaptionHost,
                         private call::SystemAudioHolder,
                         public call::ICallShareLink
{
public:
    CShareController();
    ~CShareController();

    /**
     *  初始化：建消息窗口。
     *  @param host 宿主接口（发信令、取公钥与本人用户号、回报共享状态），不拥有，须在 Shutdown 之前一直有效
     *  @return 消息窗口建好返回 true
     */
    bool Init(flamingomedia::IMediaHost* host);

    //结束进行中的共享或观看、关闭全部窗口、销毁消息窗口；可重复调用
    void Shutdown();

    //命令号是否归屏幕共享（1720~1739，第 18 期起含控制键鼠的 1735 / 1736，第 22 期起含多人共享的 1737～1739）
    static bool IsShareSignal(uint16_t cmd);

    /**
     *  麦克风是否正被屏幕共享使用（语音消息据此决定能不能录音，见 docs/语音消息方案.md V6）。
     *  @return 共享语音在运行且麦克风打开时返回 true（发起方与观看方都算）
     */
    bool IsMicrophoneInUse() const;

    /**
     *  处理一条屏幕共享信令。
     *  @return 处理了返回 true
     */
    bool OnSignal(uint16_t cmd, const std::string& json);

    /**
     *  发起共享：弹出选择窗，选好后才真正发起。
     *  @param ctype            会话类型：conversation_single / conversation_group
     *  @param cid              会话 id：单聊为对端 userid，群聊为群 id
     *  @param conversationName 会话名（对方显示名或群名），用于提示
     *  @return 已弹出选择窗返回 true；系统不支持、已在共享或观看中返回 false（已向用户提示原因）
     */
    bool StartShare(int ctype, int64_t cid, const std::wstring& conversationName);

    /**
     *  加入某会话里进行中的共享（共享卡片的「加入」）。
     *  @param roomId   房间号（卡片 content 的 roomid）
     *  @param hostName 发起人显示名，用于观看窗标题
     *  @param forced   是否强制模式（第 13 期）：是的话先弹确认，用户取消时不加入
     *  @return 已发出加入请求返回 true；已在共享或观看中、或用户在确认框里取消返回 false
     */
    bool JoinShare(int ctype, int64_t cid, const std::string& roomId, const std::wstring& hostName,
                   bool forced = false);

    /**
     *  凭分享链接加入（第 25 期，docs/屏幕共享与录制方案.md 第 12.4 节）：从链接取出链接码，向服务端查共享概要（1753），
     *  应答到达后弹确认框（标题、发起人、描述、是否要密码、是否强制模式），确认后带链接码加入（1722）；要密码时照常弹密码框。
     *  @param url 聊天里点到的整条链接；格式不对时提示「链接无效」
     *  @return 已发出查询返回 true；格式不对、正在共享屏幕时返回 false（已向用户提示）
     */
    bool JoinShareByLink(const std::wstring& url);

    //查询某会话当前有无进行中的共享；结果经 IMediaHost::OnScreenShareStatus 回报
    void QueryShare(int ctype, int64_t cid);

    //用户登出：结束进行中的共享或观看，模块保持初始化
    void ResetForLogout();

    //锁屏开始或结束（docs/锁屏方案.md 第 4.9 节）。开始时结束本人主持的共享（共享的若是窗口，窗口采集不受锁屏遮挡）；
    //锁屏期间收到的共享开始通知不弹提示、不自动加入，强制模式下的重新加入记下来，解锁时再加入。界面线程调用。
    //  locked：true 表示锁屏开始
    void OnScreenLockChanged(bool locked);

    /**
     *  鼠标效果的设置（第 16 期）：交给推流线程（立即生效）与控制条（菜单打勾）。exe 登录后与保存新设置后经模块调用。
     *  @param settings 新设置
     */
    void SetMouseEffects(const ShareMouseEffectSettings& settings);

    //登记录屏控制器（共享录制用）；不拥有，Shutdown 之前须一直有效；可为 NULL（录屏不可用）
    void SetRecorder(screenrecord::CRecordController* recorder);

    /**
     *  本人是否正在共享屏幕（2026-10-07，录制与共享互斥，docs/屏幕共享与录制方案.md 第 13 节）：作为主持人从打开选择窗到
     *  结束共享的整个过程，或多人共享里本人作为共享者从打开选择窗到停止共享。只是在观看别人的共享时为 false。
     *  为 true 时不能发起独立录制（共享控制条上的「录下这场共享」不受影响）。
     */
    bool IsLocalShareBusy() const;

    /**
     *  登记通话控制器（通话中共享屏幕时，经它刷新通话窗「共享屏幕 / 停止共享」按钮）。
     *  @param observer 不拥有，须在本类 Shutdown 之前一直有效，或在那之前改登记为 NULL；可为 NULL
     */
    void SetCallObserver(call::ICallShareObserver* observer);

    // ---- call::ICallShareLink：通话接通 / 结束，以及通话窗的「共享屏幕」按钮（第 8 期）----

    //通话已接通：记下通话对端，此后与他的单聊共享不开房间麦克风与摄像头，他发起的共享直接加入观看
    virtual void OnCallConnected(int64_t peerId, const std::wstring& peerName) override;

    //通话已结束：清掉通话对端，进行中的共享照常继续；可重复调用
    virtual void OnCallEnded() override;

    //通话窗的「共享屏幕 / 停止共享」：没在共享时发起与通话对端的共享，正在共享时结束它，忙于别的共享时只提示
    virtual void ToggleShareInCall() override;

    // ---- call::SystemAudioHolder：通话开始共享电脑声音，屏幕共享这一路停下并提示（2026-10-06）----
    virtual void OnSystemAudioPreempted() override;

    BEGIN_MSG_MAP(CShareController)
        MESSAGE_HANDLER(kMsgShareHostEvent, OnHostEventMsg)
        MESSAGE_HANDLER(kMsgShareViewerEvent, OnViewerEventMsg)
        MESSAGE_HANDLER(kMsgSharePickerResult, OnPickerResultMsg)
        MESSAGE_HANDLER(kMsgShareBarCommand, OnBarCommandMsg)
        MESSAGE_HANDLER(kMsgShareViewerCommand, OnViewerCommandMsg)
        MESSAGE_HANDLER(kMsgShareToastCommand, OnToastCommandMsg)
        MESSAGE_HANDLER(kMsgShareAnnotFrame, OnAnnotFrameMsg)
        MESSAGE_HANDLER(kMsgShareAnnotCommand, OnAnnotCommandMsg)
        MESSAGE_HANDLER(kMsgShareAnnotText, OnAnnotTextMsg)
        MESSAGE_HANDLER(kMsgSharePasswordResult, OnPasswordResultMsg)
        MESSAGE_HANDLER(kMsgShareViewerListCommand, OnViewerListCommandMsg)
        MESSAGE_HANDLER(kMsgShareRemoveConfirmResult, OnRemoveConfirmMsg)
        MESSAGE_HANDLER(kMsgShareInfoEditResult, OnInfoEditResultMsg)
        MESSAGE_HANDLER(kMsgShareControlToast, OnControlToastMsg)
        MESSAGE_HANDLER(kMsgShareInputFrame, OnInputFrameMsg)
        MESSAGE_HANDLER(kMsgShareLocalActivity, OnLocalActivityMsg)
        MESSAGE_HANDLER(kMsgShareSharingToast, OnSharingToastMsg)
        MESSAGE_HANDLER(kMsgSharePublisherEvent, OnPublisherEventMsg)
        MESSAGE_HANDLER(kMsgShareSharerCommand, OnSharerCommandMsg)
        MESSAGE_HANDLER(kMsgShareBoardCommand, OnBoardCommandMsg)
        MESSAGE_HANDLER(kMsgShareBoardToast, OnBoardToastMsg)
        MESSAGE_HANDLER(kMsgShareLinkJoinResult, OnLinkJoinResultMsg)
        MESSAGE_HANDLER(kMsgShareBoardCloseResult, OnBoardCloseResultMsg)
        MESSAGE_HANDLER(kMsgShareScreensCommand, OnScreensCommandMsg)
        MESSAGE_HANDLER(kMsgShareSaveToast, OnSaveToastMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

private:
    //共享者边框跟随被共享窗口的定时器（第 22 期）：id 与 ShareController.cpp 里的几个定时器错开，每 200 毫秒一次
    static const UINT_PTR kPublishFollowTimerId = 405;
    static const UINT kPublishFollowTimerMs = 200;
    //本人的白板广播视野的定时器（第 23 期）：每 200 毫秒看一次有没有变化，至少每 3 秒重发一次（后加入的人据此画背景）
    static const UINT_PTR kBoardViewTimerId = 406;
    static const UINT kBoardViewTimerMs = 200;
    static const int64_t kBoardViewKeepaliveMs = 3000;
    //经移交成为主持人后，有人加入时补发已有标注的定时器（2026-10-07，方案第 14.3 节第 1 条）：等对方连上中转再补发，只触发一次
    static const UINT_PTR kTransferredSnapshotTimerId = 407;
    static const UINT kTransferredSnapshotDelayMs = 3000;

    //发起一侧的阶段
    enum HostPhase
    {
        host_idle       = 0,   //没有发起
        host_picking    = 1,   //选择窗开着，还没发 1720
        host_starting   = 2,   //已发 1720，等应答
        host_waiting    = 3,   //1720 成功，等 1726 中转参数
        host_streaming  = 4,   //推流中
    };

    //观看一侧的阶段
    enum ViewerPhase
    {
        viewer_idle     = 0,   //没有观看
        viewer_joining  = 1,   //已发 1722，等应答与 1726
        viewer_watching = 2,   //观看窗已打开
    };

    //右下角提示对应的那次共享
    struct ToastInfo
    {
        std::string  m_roomId;     //房间号
        int          m_ctype;      //会话类型
        int64_t      m_cid;        //会话 id
        std::wstring m_hostName;   //发起人显示名
        LPARAM       m_token;      //随提示按钮回传的标识
        bool         m_forced;     //是否强制模式（第 13 期），点「加入」时先确认

        ToastInfo();
    };

    //等待输入密码的那次加入（第 12 期）：服务端回 717 后记下，密码框确定时据此重发加入请求
    struct JoinTarget
    {
        std::string  m_roomId;     //房间号；为空表示没有在等密码
        int          m_ctype;      //会话类型
        int64_t      m_cid;        //会话 id
        std::wstring m_hostName;   //发起人显示名
        std::string  m_linkCode;   //凭分享链接加入时的链接码（第 25 期）：输入密码后重发加入时一并带上；不是凭链接时为空

        JoinTarget();
    };

    //ShareHostStreamerListener：推流线程上回调，转投 UI 线程
    virtual void OnHostStreamEvent(int event, int value);
    //IShareCameraFrames：摄像头小窗取画面（UI 线程）；本人取发送端的预览，其他人取接收端解出的画面
    virtual bool GetCameraFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height,
                                uint64_t& serial) override;
    //ShareAnnotationSink：中转接收线程上收到标注帧，拷贝后转投 UI 线程
    virtual void OnAnnotationFrame(int senderSeat, const std::string& payload) override;
    //IShareAnnotationInput：发起人的输入层或观看窗的画面控件上的鼠标动作（UI 线程）
    virtual void OnAnnotPointer(int phase, const AnnotationPoint& pt, int tolerance, double aspect, POINT screenPt) override;
    //IShareAnnotationInput：标注模式下的按键（第 20 期）：Delete 删除选中的对象，Esc 取消选择
    virtual bool OnAnnotKey(UINT vk) override;
    //IShareAnnotationInput：当前工具的光标（第 19 期），按工具与颜色缓存，工具或颜色变了才重建
    virtual HCURSOR AnnotCursor() override;
    //IShareAnnotationInput：接下来的鼠标动作落在哪块画面上（第 22 期 22e），换了画面时标注会话随之切换并重画预览
    virtual void OnAnnotCanvas(int canvas) override;
    //ShareAudioRecordTap：采集、渲染、回环线程上交来的房间声音，推进录制的混音器
    virtual void OnRoomAudio(int kind, const int16_t* mono, int samples) override;
    //ShareInputSink（第 18 期）：中转接收线程上收到控制者的键鼠输入，拷贝后转投 UI 线程
    virtual void OnInputFrame(int senderSeat, int targetSeat, const ShareInputEvent& ev) override;
    //IShareControlInput（第 18 期）：观看窗在控制中产生的键鼠输入（UI 线程），组帧后只发给画面主人
    virtual void OnControlInput(const ShareInputEvent& ev) override;
    //IShareRecordOwner：共享录制已结束（UI 线程）
    virtual void OnShareRecordingEnded() override;

    //发起人录制的画面来源：推流管线采到的原始画面
    class HostRecordSource : public screenrecord::IRecordVideoSource
    {
    public:
        explicit HostRecordSource(ShareHostStreamer* streamer);
        virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override;

    private:
        ShareHostStreamer* m_streamer;   //推流管线（不拥有）
    };

    //观看者录制的画面来源：观看窗收流管线解出的画面
    class ViewerRecordSource : public screenrecord::IRecordVideoSource
    {
    public:
        explicit ViewerRecordSource(CShareViewerWindow* viewer);
        virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override;

    private:
        CShareViewerWindow* m_viewer;   //观看窗（不拥有）
    };

    //共享者推流管线的监听者（第 22 期）：推流线程上的事件转投 UI 线程的 kMsgSharePublisherEvent，与主持人推流管线的
    //事件（kMsgShareHostEvent）分开处理
    class PublisherListener : public ShareHostStreamerListener
    {
    public:
        PublisherListener();
        //登记接收事件的窗口（控制器的消息窗口），推流开始之前调用；不拥有
        void SetWindow(HWND hwnd);
        //推流线程上调用：PostMessage 给登记的窗口；没有登记时丢弃
        virtual void OnHostStreamEvent(int event, int value) override;

    private:
        HWND m_hwnd;   //接收事件的窗口（不拥有）；为 NULL 时丢弃事件
    };

    //录制的画中画来源：当前发言人的摄像头（本人取预览，其他人取解码结果）
    class SpeakerCameraSource : public screenrecord::IRecordVideoSource
    {
    public:
        SpeakerCameraSource(ShareCameraSender* sender, ShareCameraReceiver* receiver);
        //指定画谁（UI 线程）；self 为 true 时画本人
        void SetSpeaker(int seat, bool self);
        virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override;

    private:
        ShareCameraSender*   m_sender;     //本人摄像头（不拥有）
        ShareCameraReceiver* m_receiver;   //其他人的摄像头（不拥有）
        std::atomic<int>     m_seat;       //画谁的座位号
        std::atomic<bool>    m_self;       //是否画本人
    };

    LRESULT OnHostEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnViewerEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPickerResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnBarCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnViewerCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnToastCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnAnnotFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnAnnotCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnAnnotTextMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPasswordResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnViewerListCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRemoveConfirmMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //「修改标题与描述」小窗的结果（第 15 期）：与当前不同时发 1733
    LRESULT OnInfoEditResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //==== 控制键鼠（第 18 期，实现在 ShareControllerControl.cpp）====
    //控制申请提示的按钮：同意即授予，拒绝（含关闭、超时）即拒绝，然后弹出下一个排队的申请
    LRESULT OnControlToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程上处理一帧键鼠输入：核对控制者、换算坐标、过滤后注入
    LRESULT OnInputFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //低级钩子判定画面主人本机有动静：收回控制
    LRESULT OnLocalActivityMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //1735 的应答：失败时按错误码提示，观看者的申请失败时回到「申请控制」
    void onControlResult(const std::string& json);
    //1736 推送：画面主人收到申请或撤回，申请人收到拒绝
    void onControlNotify(const std::string& json);
    /**
     *  按 1725 的房间快照同步控制关系：本人正有一块画面在共享时（建房间的主持人或共享者）看自己成员项的 controlledby，
     *  决定开始或结束被控制；观看者（2026-10-07 起也包括在「其他人的屏幕」窗口里的主持人）找哪位共享者的 controlledby
     *  是自己，决定进入或退出控制状态、控制的是哪一块画面。
     *  @param n      最新的房间快照
     *  @param asHost 本人是否是建房间的主持人；2026-10-07 起两种身份走同一套逻辑，此参数只为调用处保持不变
     */
    void applyControlState(const ShareStateNotify& n, bool asHost);
    //发一条 1735
    void sendControl(int action, int64_t owner, int64_t target);
    //发起人一侧：开始被某人控制（controllerId 为 0 表示结束），装卸钩子、刷新控制条、补发抬起
    void setControlledBy(int64_t controllerId);
    //发起人一侧：补发注入后仍按着的按键与鼠标按钮的抬起，并清空过滤器
    void releaseInjectedInput();
    /**
     *  发起人一侧：立即收回控制。本地马上停止注入并发 1735；服务端确认（1725 里 controlledby 清零）之前到达的、
     *  仍带着这位控制者的状态推送不再让控制恢复。没人在控制时什么也不做。
     *  @param byLocalActivity 是否因为画面主人本机有动静而收回（提示文字不同）
     */
    void revokeControlNow(bool byLocalActivity);
    //发起人一侧：安装 / 卸载检测本机动静的低级鼠标、键盘钩子
    void installActivityHooks();
    void removeActivityHooks();
    //低级鼠标、键盘钩子的回调（安装它们的界面线程上调用）
    static LRESULT CALLBACK lowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK lowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    //发起人一侧：弹出排队中的下一个控制申请（没有排队的申请时什么也不做）
    void showNextControlRequest();
    //发起人一侧：把某人的申请从队列与提示里去掉（他撤回、离开或已被授予）
    void dropControlRequest(int64_t userId);
    //观看者一侧：观看窗的「申请控制 / 取消申请 / 结束控制」
    void toggleViewerControl();
    //观看者一侧：切换控制状态并刷新观看窗
    void setViewerControlState(int state);
    //离开房间或结束共享时清掉全部控制状态（钩子、提示、排队的申请、观看窗状态）
    void resetControlLocal();

    //==== 多人同时共享（第 22 期，实现在 ShareControllerMulti.cpp）====
    //1737 的应答：失败时按错误码提示；本人的申请失败时回到「共享我的屏幕」，同意别人时人数已满则把申请放回队列
    void onSharingResult(const std::string& json);
    //1738 推送：主持人收到申请或撤回；申请人收到同意（带「只推流」中转参数）或拒绝；共享者收到被主持人停止
    void onSharingNotify(const std::string& json);
    //1739 的应答：成功时原主持人离开房间（共享仍在进行，重新查询一次会话状态），失败时提示
    void onTransferHostResult(const std::string& json);
    //1752 的应答（第 25 期）：记下链接码与免输密码的状态，再完成发请求时记下的那个菜单动作（复制、发送邀请等）
    void onLinkResult(const std::string& json);
    //共享中云端录制（1754）的应答（第 27b 期）：失败时给主持人看原因，成功时状态以 1725 为准
    void onCloudRecordResult(const std::string& json);
    /**
     *  套用 1725 里的云端录制状态（第 27b 期）：主持人的控制条、观看者的观看窗；被动结束时提示主持人。
     *  @param n      状态推送
     *  @param isHost 本人是否以主持人身份在这场共享里
     */
    void applyCloudRecordState(const ShareStateNotify& n, bool isHost);
    //1753 的应答（第 25 期）：链接无效、已结束、不同企业时提示；进行中时弹确认框，确认后带链接码加入
    void onLinkResolveResult(const std::string& json);
    /**
     *  主持人点了控制条「更多」菜单里的分享链接一项（第 25 期）。复制与发送邀请在已有链接时直接完成，没有时先向服务端取得；
     *  重置、停用、切换免输密码都要发 1752，应答到达后再完成。
     *  @param action 取值见 CShareControlBar::LinkMenuAction
     */
    void onLinkMenu(int action);
    //在已有链接码的前提下完成一个链接菜单动作：复制邀请文字、请宿主选会话发送，或提示重置 / 停用的结果
    void deliverHostLink(int action);
    //主持人的邀请文字：标题、链接；房间有密码且没有打开免输密码时附上密码
    std::wstring hostInviteText() const;
    /**
     *  按 1725 的房间快照同步多人共享：画面清单、标注所在的画面、本人作为共享者的阶段、主持人停掉的画面、排队的共享申请、
     *  主持人「其他人的屏幕」窗口，以及换了主持人时的提示。
     *  @param n      最新的房间快照
     *  @param asHost 本人是否以建房间的主持人身份推流（发起一侧处于 host_streaming）
     */
    void applyMultiState(const ShareStateNotify& n, bool asHost);
    //本人是否是房间的主持人（建房间的主持人，或经移交成为主持人的观看者）
    bool isRoomHost() const;
    /**
     *  经移交成为主持人的人在观看窗里的主持人入口（2026-10-07，docs/屏幕共享与录制方案.md 第 14.3 节第 2 条）：按最近一次
     *  1725 打开或收起观看窗的「主持人 ▾」与皇冠图标，同步菜单里两项权限的勾选；身份变了时重建开着的标注工具条与名单。
     *  观看端每次套用 1725 之后调用。
     */
    void refreshHostTools();
    /**
     *  观看窗「主持人 ▾」菜单选了一项（2026-10-07）：修改标题与描述、全员静音、切换他人的录制或标注权限，
     *  请求按当前这一场的房间号发出。本人不是主持人时忽略。
     *  @param item 菜单项，取值见 share_host_menu_item
     */
    void onHostMenu(int item);
    //主持人的小窗（修改标题与描述、移出确认框）定位用的窗口：发起人为控制条，经移交成为主持人的人为观看窗，都没有时为主窗口
    HWND hostToolsWindow();
    //发一条 1737；target 为 0 时不带对象；kind 为共享的是什么（第 23 期），取值见 share_sharing_kind
    void sendSharing(int action, int64_t target, int kind = share_kind_screen);
    //主持人一侧：弹出排队中的下一个共享申请；同时共享的人数已满时先不弹，等有人停下后再弹
    void showNextShareRequest();
    //观看窗的「共享我的屏幕 / 取消共享申请 / 停止我的共享」：按本人作为共享者的阶段决定发申请、撤回还是停止
    void toggleShareMine();
    //共享者一侧：选择窗选好内容后连上中转开始推流，显示共享者控制条与边框
    void startPublishing();
    /**
     *  停止本人作为共享者的推流、关掉控制条与边框并复位阶段；没有申请也没在共享时什么也不做。
     *  @param notifyServer 是否告诉服务端（申请中发撤回，选择内容中与推流中发停止）
     */
    void stopPublisherLocal(bool notifyServer);
    //主持人退出（控制条「结束共享」、观看窗关窗）：还有别人在共享时弹出移交或结束的选择框，没有时直接结束整个共享
    void beginHostExit();
    //主持人结束整个共享：发 1721 后本地立即收尾
    void endAllSharing();
    /**
     *  主持人移交给某位正在共享的人（发 1739），应答成功后本人离开房间。
     *  @param userId 新主持人
     */
    void transferHostTo(int64_t userId);
    /**
     *  主持人停掉或恢复自己的画面（第 29 条，只用于建房间的主持人）：停掉时推流暂停、边框与标注层收起，恢复时还原。
     *  只改本地，告诉服务端由调用方负责。
     *  @param stopped true 停掉，false 恢复
     */
    void setHostScreenStopped(bool stopped);
    /**
     *  主持人一侧刷新「其他人的屏幕」窗口与控制条上的入口；有新的画面加入时重新显示窗口，没有别人在共享时关闭；
     *  已不在共享的画面让推流管线丢掉它的解码器。
     *  @param before 上一次快照里主持人以外正在共享的画面
     *  @param others 这一次快照里主持人以外正在共享的画面
     */
    void refreshScreensWindow(const std::vector<ShareScreenInfo>& before, const std::vector<ShareScreenInfo>& others);
    //共享者一侧：边框跟随被共享的窗口（移动、缩放时重画，最小化时隐藏）；由 kPublishFollowTimerId 定时调用
    void followPublishedSource();
    /**
     *  共享者在自己共享的内容上标注（第 22 期 22e，共享者控制条的「标注」）：输入层盖在内容上，标注工具条摆在内容上方；
     *  与观看窗里的标注模式互斥，进入一个之前先退出另一个。
     *  @param on true 进入，false 退出
     */
    void setPublisherAnnotMode(bool on);
    //录制叠哪块画面的标注（第 22 期 22e）：发起人为自己的画面，观看者为观看窗当前那一块
    int recordCanvas() const;
    //把录制那块画面上的标注交给录屏控制器，并记下是哪块画面
    void pushRecordAnnotations();
    //本人是否正有一块画面在共享（建房间的主持人没停掉自己的画面，或作为共享者在推流）；控制键鼠据此判断本人是不是画面主人
    bool isScreenOwner() const;
    //本人那块画面在中转里的座位：建房间的主持人为自己的座位，共享者为服务端分配的画面座位
    int ownScreenSeat() const;
    //本人共享的内容（建房间的主持人取发起时选的内容，共享者取申请通过后选的内容）
    const ShareSourceSpec& ownSource() const;
    //本人共享的内容的矩形（屏幕坐标）
    RECT ownSourceRect() const;
    //本人的画面是否暂停（暂停期间不注入控制者的输入）；共享者没有暂停，恒为 false
    bool ownScreenPaused() const;
    //共享申请提示的按钮：同意即发 1737 同意，拒绝（含关闭、超时）即发拒绝，然后弹出下一个排队的申请
    LRESULT OnSharingToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //共享者推流管线的事件：中转断开、采集失败或共享的窗口关闭时停止本人的共享并提示
    LRESULT OnPublisherEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //共享者控制条与主持人退出选择框的命令：停止本人的共享、移交主持人、结束整个共享或取消
    LRESULT OnSharerCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //==== 白板、屏幕当白板、保存为图片（第 23 期，实现在 ShareControllerBoard.cpp）====
    //观看窗或控制条的「新建白板 / 取消白板申请 / 关闭我的白板」：按本人白板的阶段决定发申请、撤回还是关闭
    void toggleMyBoard();
    /**
     *  1738 里白板的推送：主持人收到申请或撤回；申请人（含主持人自己）收到同意（带白板座位）或拒绝；建立者收到被主持人关闭。
     *  @param n         推送内容（m_kind 为白板）
     *  @param actorName 发起动作的人的显示名
     */
    void onBoardNotify(const ShareSharingNotify& n, const std::wstring& actorName);
    /**
     *  按 1725 的房间快照同步白板：本人的白板已不在快照里时收尾，快照里有而本地没开（漏了 1738）时打开；
     *  主持人一侧排队的新建白板申请。
     */
    void syncBoards(const ShareStateNotify& n);
    /**
     *  按房间快照把各块白板的建立者交给标注会话（2026-10-07，方案第 14.1 节 ④-1）：白板的画面座位 → 建立者的成员座位。
     *  观看者的标注权限关闭后，建立者在自己的白板上仍能画。须在处理标注权限之前调用（applyMembers 开头）。
     */
    void syncBoardOwners(const ShareStateNotify& n);
    //打开本人的白板窗口并进入在白板上标注
    void openMyBoard(int seat);
    /**
     *  关闭本人的白板（白板窗口的关闭、菜单的「关闭我的白板」）：白板上有笔迹且 prompt 为 true 时先问要不要保存为图片；
     *  确定关闭后发 1737 关闭并本地收尾。
     *  @return 关掉了返回 true；用户在提示里点了「取消」返回 false
     */
    bool closeMyBoard(bool prompt);
    //本地收尾本人的白板：退出在白板上标注、关窗、清掉它的笔迹，不告诉服务端
    void closeMyBoardLocal();
    //主持人一侧：弹出排队中的下一个新建白板申请；共享名额已满时先不弹
    void showNextBoardRequest();
    //本人白板的背景或可见区域有变化：force 为 true 时马上广播，否则交给定时器限频
    void sendBoardView(bool force);
    //画面座位是不是一块白板（本人的或别人的）
    bool isBoardCanvas(int canvas) const;
    //发起一侧「屏幕当白板 / 退出屏幕白板」（第 31 条）：只共享一个窗口时换成它所在的整块屏幕并进入标注，退出时恢复
    void toggleScreenBoard();
    //共享者（主持人以外）把自己共享的窗口所在的屏幕当白板，或退出（2026-10-07，方案第 14.1 节 ③）；流程同 toggleScreenBoard，
    //切换的是共享者自己的推流（m_publisher），只影响他那一块画面
    void toggleSharerScreenBoard();
    //共享者换共享内容（2026-10-07）：推流、边框、标注层与共享者控制条跟着换，这块画面上的标注清掉
    void switchPublishSource(const ShareSourceSpec& spec);
    /**
     *  标注工具条的「保存为图片」：当前画面是白板时导出整张画布，是共享画面时导出当前画面加笔迹；选好路径保存后在右下角提示，
     *  可一键发到本会话。
     *  @return 保存成功返回 true；用户取消或画面取不到时返回 false
     */
    bool saveAnnotImage();
    //把一块白板保存为图片（关闭前提示保存与工具条共用）；保存后提示可发到本会话
    bool saveBoardImage(int seat);
    //弹出「另存为」框并写 PNG；成功后在右下角提示
    bool savePngWithDialog(const std::vector<uint8_t>& bgra, int width, int height, bool whiteboard);
    //白板窗口与白板画布的命令：背景、工具条、可见区域、关闭、移动
    LRESULT OnBoardCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //新建白板申请提示的按钮：同意即发 1737 同意（白板），拒绝（含关闭、超时）即拒绝，然后弹出下一个
    LRESULT OnBoardToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //凭分享链接加入的确认小窗的结果（kMsgShareLinkJoinResult，2026-10-07）：wParam 为 1 时加入
    LRESULT OnLinkJoinResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //关闭别人白板的确认框（2026-10-08）的结果：wParam 为 1 关闭、0 取消；确认后才发停止白板的请求
    LRESULT OnBoardCloseResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    /**
     *  主持人要关闭别人的白板（2026-10-08）：先弹确认框「关闭某某的白板？白板上的内容不会保留。」，结果经
     *  kMsgShareBoardCloseResult 回到 OnBoardCloseResultMsg。不是主持人、建立者是本人或无效时什么也不做。
     *  @param ownerId 白板的建立者 userid
     */
    void requestCloseBoardOf(int64_t ownerId);
    /**
     *  主持人「其他人的屏幕」窗口顶部的按钮（kMsgShareScreensCommand，2026-10-07，方案第 14.3 节第 4 条）：
     *  「标注」进入或退出在那个窗口里对别人画面的标注；「申请控制」向当前那一块画面的共享人申请、撤回或结束控制。
     */
    LRESULT OnScreensCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //控制中按哪块画面换算鼠标坐标：主持人一侧交给「其他人的屏幕」窗口，观看者一侧交给观看窗；-1 表示没有在控制
    void setControlScreen(int seat);
    //「已保存为图片」提示的按钮：「发送到本会话」时经宿主把图片发到当前共享所在的会话
    LRESULT OnSaveToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //白板视野的定时器（kBoardViewTimerId）
    void onBoardViewTimer();

    /**
     *  弹出「屏幕共享已结束」右下角提示（2026-10-05）：时长取结束推送里的，分享人与参与者取 m_participantLog，
     *  之后清空名单。调用方负责判断该不该弹（通话里的共享不弹）。
     *  @param n          结束推送
     *  @param shareTitle 共享标题；不知道时为空
     */
    void showEndedToast(const ShareEndedNotify& n, const std::wstring& shareTitle);

    //各条信令的处理
    void onStartResult(const std::string& json);
    void onJoinResult(const std::string& json);
    void onRelayInfo(const std::string& json);
    void onStartedNotify(const std::string& json);
    void onStateNotify(const std::string& json);
    void onEndedNotify(const std::string& json);
    void onQueryResult(const std::string& json);
    void onMemberMediaResult(const std::string& json);
    //移出观看者（1732，第 14 期）的应答：失败时提示发起人
    void onRemoveMemberResult(const std::string& json);
    //修改标题与描述的应答（第 15 期）：成功时按本次提交的内容刷新，敏感词时带着刚才的输入重新打开修改窗
    void onSetInfoResult(const std::string& json);
    //标题与描述变了的推送（第 15 期，1734）：刷新缓存、控制条、观看窗、名单与上报的状态
    void onInfoNotify(const std::string& json);
    //发起人一侧标题与描述有了新值：记下并刷新控制条、名单与上报的状态
    void applyHostInfo(const std::wstring& title, const std::wstring& desc);
    /**
     *  移出确认框开着时让发起人控制条与观看者名单不可用（第 15 期，确认框对二者模态），关闭后恢复。
     *  主面板、聊天窗与被共享的程序不受影响。
     *  @param modal true 确认框打开、二者禁用；false 确认框已关闭、二者恢复可用
     */
    void setRemoveConfirmModal(bool modal);

    bool send(uint16_t cmd, const std::string& body);
    /**
     *  打开或关闭「允许截图」（2026-10-08，第 14.11 节第 5 条）：设置本机开关，并对当前显示着、默认不允许采集的各条
     *  （控制条、共享者控制条、主持人模式或浮在本人共享内容上的标注工具条）重新设置。只对这一场共享有效。
     *  @param allowed true 允许截图，false 恢复不允许
     */
    void setBarCapture(bool allowed);
    //locked：加入是否需要密码（第 12 期）；forced：是否强制模式（第 13 期）。exe 据此在卡片、横幅上标出
    void reportStatus(int ctype, int64_t cid, const std::string& roomId, int64_t hostId, const std::wstring& hostName,
                      bool active, bool locked, bool forced = false, const std::wstring& title = std::wstring());
    /**
     *  发加入请求（JoinShare 与密码框确定后的重试共用）。
     *  @param password 加入密码（UTF-8，6 位数字）；为空表示不带密码
     *  @param linkCode 分享链接码（第 25 期）；不是凭链接加入时为空
     */
    bool joinRoom(int ctype, int64_t cid, const std::string& roomId, const std::wstring& hostName,
                  const std::string& password, const std::string& linkCode = std::string());
    /**
     *  加入被 717 拒绝后弹出密码框（不限次数重试）。
     *  @param target 要加入的那一场
     *  @param retry  上一次已经带了密码仍被拒，即密码不对
     */
    void askJoinPassword(const JoinTarget& target, bool retry);
    //关闭密码框并放弃等待中的那次加入；可重复调用
    void closePasswordDialog();
    //刷新共享时长（第 13 期）：发起时写到控制条，观看时写到观看窗工具条；每秒由定时器调用一次
    void refreshDuration();
    //发起与观看都结束后停止计时
    void stopDurationIfIdle();
    //打开或关闭观看者名单（第 13 期）：发起人锚在控制条的「N 人观看 ▾」下，观看者锚在观看窗工具条的同名按钮下
    void toggleViewerList();
    //名单开着时按最近的房间快照与说话人重算名单
    void refreshViewerList();
    //全员静音（控制条「更多」菜单与名单顶部按钮共用）
    void muteAllViewers();
    void startStreaming(const ShareRelayInfo& relay);
    void stopHostLocal();
    void stopViewerLocal();

    //向宿主的 P2P 服务登记用途 screenshare（P2P 方案第 6 期）；P2P 不可用或按诊断开关关闭时不登记
    void registerP2P();

    /**
     *  发起人一侧：按 1725 快照同步 P2P 观看者。带 p2psessionid 的新观看者认领链路槽交给推流管线；已离开的从推流管线
     *  撤下并放弃他的功能会话号。
     *  @param n 最新的房间快照
     */
    void syncHostP2PViewers(const ShareStateNotify& n);

    //发起人一侧结束共享时：撤下全部 P2P 观看者并放弃他们的功能会话号
    void releaseHostP2PViewers();

    void showError(const wchar_t* text);
    void openPicker(bool switchMode, const std::wstring& hint);
    HWND mainWindow() const;
    static std::wstring utf8ToWide(const std::string& s);

    //房间语音：随推流 / 收流开始，随其结束；transport 为本人麦克风帧的发送通道。实时字幕的会话也随之开始与结束
    void startAudio(ShareAudioTransport* transport, int seat, bool micOn);
    void stopAudio();
    //IShareCaptionHost（语音字幕）：座位的显示名、浮层区域、共享录制的字幕草稿、状态变化后刷新控制条与观看窗
    virtual std::wstring CaptionSeatName(int seat) override;
    virtual bool CaptionOverlayArea(RECT& area, bool& excludeFromCapture, HWND& owner) override;
    virtual caption::ICaptionRecording* CaptionRecording(int64_t& recordedMs) override;
    virtual void OnCaptionStateChanged() override;
    //把字幕的可用状态与两个开关同步到控制条与观看窗（语音字幕）
    void applyCaptionState();
    //打开或关闭本人麦克风：切换设备、刷新界面、经 1728 告诉房间里的人
    void setMic(bool on);
    //开始或停止共享电脑声音（发起人）：切换回环采集、刷新控制条、经 1728 告诉房间里的人
    void setSystemAudio(bool on);
    //把本人当前的麦克风与电脑声音状态经 1728 发给服务端
    void reportMedia();
    //按 1725 的成员快照：离开了的座位删掉声音路；被全员静音时关掉本人麦克风
    void applyMembers(const ShareStateNotify& n, bool asHost);
    //定时刷新说话人提示
    void refreshSpeaking();
    //当前所在房间号（发起或观看）
    const std::string& currentRoomId() const;
    //取成员显示名（经宿主，带缓存）
    std::wstring displayName(int64_t userId);
    //打开或关闭本人摄像头：开关设备、刷新界面、经 1728 告诉房间里的人；打不开时提示原因
    void setCamera(bool on);
    //按本人摄像头状态与最近一次 1725 的成员快照，刷新摄像头小窗（发起人的浮动窗口或观看窗右侧）
    void refreshCameraTiles();
    //把摄像头开关状态同步到控制条与观看窗
    void applyCameraButton();
    //当前被共享内容的矩形（发起人）
    RECT hostSourceRect() const;
    /**
     *  发起人换一个共享内容（「切换共享内容」与第 23 期「屏幕当白板」共用）：推流管线换源，边框、控制条、摄像头浮窗与标注层
     *  挪到新的内容上，清空标注（docs/屏幕共享与录制方案.md 5.9）。
     *  @param spec 新的共享内容
     */
    void switchHostSource(const ShareSourceSpec& spec);
    //进入或退出标注模式：发起人显示 / 隐藏输入层，观看者切换画面控件；同时显示 / 隐藏标注工具条
    void setAnnotMode(bool on);
    /**
     *  标注模式下进入或退出「鼠标」状态（2026-10-07，标注工具条最左边的按钮）：进入时撤掉输入层（观看者的画面控件不再拦截
     *  标注输入），点击穿透到桌面程序，工具条与已画的标注保留；退出时盖回输入层。不在标注模式时什么也不做。
     *  @param on true 进入「鼠标」状态
     */
    void setAnnotPointer(bool on);
    /**
     *  选择工具下双击（第 20 期）：点中文字、图元、连接线或数字标号时打开输入框，预填现有的文字，确定后改到这个对象上。
     *  @param pt        双击处（归一化坐标）
     *  @param tolerance 命中容差
     *  @param screenPt  双击处的屏幕坐标（输入框摆在这里）
     */
    void openObjectTextEdit(const AnnotationPoint& pt, int tolerance, POINT screenPt);
    //开始或停止本人的共享录制
    void startShareRecording();
    void stopShareRecording();
    //录制已结束（或开始失败后）的收尾：断开声音、画面旁路，刷新按钮，告诉服务端
    void finishShareRecording(bool notifyServer);
    //按发言判定选画中画的人（refreshSpeaking 调用）
    void updateRecordPip(const std::vector<int>& speakingSeats);
    //1731 的应答
    void onRecordStateResult(const std::string& json);
    //标注工具条的参照矩形：发起人为共享内容，观看者为观看窗
    RECT annotAnchorRect() const;
    //笔迹或预览有变化：重画发起人的标注层或观看窗的画面
    void annotationChanged();
    //显示临时提示（占用说话人提示的位置几秒）
    void showNotice(const wchar_t* text);
    /**
     *  独立录制占用中时拦下本次发起共享（2026-10-07，录制与共享互斥）：在发起入口所在的窗口附近弹出提示。
     *  @return 拦下了返回 true，调用方不再继续发起
     */
    bool rejectIfRecording();
    /**
     *  在控制条附近显示一条轻量提示（2026-10-07 起分享链接的复制、重置、停用与失败都用它）：控制条显示着时贴在它下方
     *  （收起成小把手时贴在小把手下方），否则放在发起入口所在的窗口附近。
     *  @param text  提示文字
     *  @param error 是否是失败提示（醒目的红色底）
     */
    void showBarToast(const wchar_t* text, bool error);
    //当前的共享或观看是否属于「通话中共享」（与已接通通话的对端在单聊里），属于时声音经由通话传输
    bool inCallShare() const;
    //把「是否正在与通话对端共享」告诉通话控制器，刷新通话窗的按钮文字
    void notifyCallObserver();
    static std::string wideToUtf8(const std::wstring& w);

    flamingomedia::IMediaHost* m_host;          //宿主接口（不拥有）
    //发起一侧
    HostPhase                  m_hostPhase;     //阶段
    int                        m_hostCtype;     //会话类型
    int64_t                    m_hostCid;       //会话 id
    std::wstring               m_hostConvName;  //会话名
    std::string                m_hostRoomId;    //房间号（1720 成功后有效）
    std::string                m_hostPassword;  //发起时设的加入密码（第 12 期，6 位数字）；没设为空，结束共享时清空
    std::string                m_hostLinkCode;  //分享链接码（第 25 期，1752 应答下发）；没有生成或已停用时为空，结束共享时清空
    bool                       m_hostLinkNoPassword;   //分享链接是否打开了「点链接加入免输密码」（第 25 期）
    int                        m_pendingLinkMenu;      //发出 1752、等应答期间记下的菜单动作（CShareControlBar::LinkMenuAction）；0 表示没有
    bool                       m_hostForced;    //发起时勾了「强制模式」（第 13 期）；结束共享时复位
    std::wstring               m_hostTitle;     //共享标题（第 15 期）：发起时在选择窗填写，共享中可修改；结束共享时清空
    std::wstring               m_hostDesc;      //共享描述（第 15 期）：同上；没填为空
    bool                       m_hostRecordAllowed; //发起时没勾「禁止观看者录制」（第 13 期）；共享中以 1725 的 recordallowed 为准
    ShareSourceSpec            m_hostSource;    //当前共享的内容
    bool                       m_hostPaused;    //是否暂停
    bool                       m_mustRepick;    //共享的窗口已关闭，选择窗取消就结束共享
    ShareHostStreamer          m_streamer;      //推流管线
    CShareControlBar           m_bar;           //控制条
    CShareBorderWindow         m_border;        //边框
    CSharePickerDialog         m_picker;        //选择窗
    //观看一侧
    ViewerPhase                m_viewerPhase;   //阶段
    int                        m_viewerCtype;   //会话类型
    int64_t                    m_viewerCid;     //会话 id
    std::string                m_viewerRoomId;  //房间号
    std::wstring               m_viewerHostName;//发起人显示名
    std::string                m_viewerJoinPassword;   //本次 1722 带的密码（第 12 期）；为空表示没带，用来区分「要密码」与「密码不对」
    std::string                m_viewerJoinLinkCode;   //本次 1722 带的分享链接码（第 25 期）；不是凭链接加入时为空
    std::string                m_pendingLinkCode;      //正在向服务端查概要（1753）的链接码（第 25 期）；没有在查时为空
    CShareTextInputDialog      m_pwdDialog;     //加入密码输入框（第 12 期）
    JoinTarget                 m_pwdJoin;       //等待输入密码的那次加入
    ShareDurationClock         m_durationClock; //共享时长计时（第 13 期）：收到 1726 时以 elapsedms 为起点开始，发起与观看都结束后停止
    CShareViewerListPanel      m_viewerList;    //观看者名单浮窗（第 13 期）
    ShareRoomInfoCache         m_roomInfo;      //各场共享的标题与描述（第 15 期）：从 1721 / 1725 / 1727 记下，加入确认等处据此显示；登出时清空
    CShareConfirmDialog        m_removeDlg;     //移出观看者的确认框（第 14 期），带「允许他再次加入」勾选项
    int64_t                    m_removeTarget;  //确认框对应的观看者 userid；没有打开确认框时为 0
    CShareConfirmDialog        m_boardCloseDlg; //主持人关闭别人白板的确认框（2026-10-08）
    int64_t                    m_boardCloseOwner; //确认框打开时要关的白板的建立者；没有打开时为 0
    CShareConfirmDialog        m_linkJoinDlg;   //凭分享链接加入的确认小窗（2026-10-07，方案第 14.5 节，替换原来的系统消息框）
    JoinTarget                 m_linkJoin;      //确认小窗对应的那次加入（会话、房间、发起人、链接码）；小窗关闭后清空
    std::wstring               m_linkJoinTitle; //确认小窗对应的共享标题（加入时记进标题缓存）
    std::wstring               m_linkJoinDesc;  //确认小窗对应的共享描述（同上）
    CShareInfoEditDialog       m_infoEdit;      //「修改标题与描述」小窗（第 15 期）；共享结束时关闭
    ShareMouseEffectSettings   m_mouseSettings; //鼠标效果的当前设置（第 16 期）：exe 推来，或在控制条菜单里改
    std::wstring               m_pendingTitle;  //最近一次 1733 提交的标题（第 15 期）：应答成功时生效，敏感词时填回修改窗
    std::wstring               m_pendingDesc;   //最近一次 1733 提交的描述（同上）
    ShareStateNotify           m_lastState;     //最近一次 1725 的房间快照（第 13 期，名单据此生成）；离开房间时清空
    //==== P2P（docs/P2P通信方案.md 第 6 期，只用于单聊共享）====
    balloonp2p::P2PLinkBroker  m_p2pBroker;     //用途 screenshare 的 P2P 业务方：Init 时登记、Shutdown 时注销
    std::map<int, std::string> m_hostP2PViewers;   //发起人一侧已交给推流管线的 P2P 观看者：座位号 → 功能会话号
    std::string                m_viewerP2PFeature; //观看者一侧本人的 P2P 功能会话号（房间号#座位号）；没有时为空
    std::set<int>              m_speakingSeats; //最近一次判定的正在说话的座位号（第 13 期，名单据此标出「正在说话」）
    CShareViewerWindow         m_viewer;        //观看窗
    //通知
    CShareCornerToast          m_toast;         //右下角提示
    ToastInfo                  m_toastInfo;     //提示对应的那次共享
    ToastInfo                  m_lockedRejoin;  //锁屏期间收到的「强制模式下重新加入」，解锁时据此加入；m_roomId 为空表示没有
    LPARAM                     m_nextToastToken;//下一个提示的标识
    std::deque<std::pair<int, int64_t> > m_pendingQueries;   //已发出、还没收到应答的查询（服务端按序应答）
    //房间语音（发起与观看共用一个，同一时间只在一个房间里）
    ShareAudioSession          m_audio;         //麦克风采集、多路播放与发言判定
    bool                       m_hostMicInitial;   //发起时勾选了「同时开启麦克风」
    bool                       m_hostSysAudioInitial;   //发起时勾选了「同时共享电脑声音」（第 4 期 4d 使用）
    int                        m_selfSeat;      //本人座位号
    std::map<int, int64_t>     m_seatUsers;     //当前房间成员：座位号 → userid（来自最近一次 1725）
    std::map<int64_t, std::wstring> m_nameCache;   //userid → 显示名（经宿主取过的，登出时清空）
    CShareCaptionController    m_caption;       //实时字幕（语音字幕）：Init 时建、Shutdown 时销毁；会话随房间语音开始与结束
    std::wstring               m_notice;        //临时提示（如「发起人已将全员静音」），显示在说话人提示的位置
    int64_t                    m_noticeUntilMs; //临时提示显示到的时刻（GetTickCount64 毫秒）
    //摄像头（发起与观看共用，同一时间只在一个房间里）
    ShareCameraSender          m_cameraSender;  //本人摄像头的发送端；发帧通道为推流管线或观看窗的收流管线
    ShareCameraReceiver        m_cameraReceiver;//其他成员摄像头的解码；随推流 / 收流开始，随其结束
    CShareCameraWindow         m_cameraWindow;  //发起人的摄像头浮动窗口
    std::set<int>              m_cameraSeats;   //开着摄像头的其他成员的座位号（来自最近一次 1725）
    //标注（发起与观看共用一个会话）
    ShareAnnotationSession     m_annot;         //本人的标注输入与全部笔迹
    CShareAnnotationOverlay    m_annotOverlay;  //发起人本机的标注层（构造时引用 m_annot，须声明在它之后）
    CShareAnnotationBar        m_annotBar;      //标注工具条
    AnnotBarState              m_annotState;    //工具条状态（工具、颜色与各项样式），工具条命令先记在这里再整体交给 m_annot
    CShareTextInputDialog      m_textDialog;    //标注文字输入框
    bool                       m_annotMode;     //是否在标注模式
    bool                       m_annotPointer;  //标注模式下是否处于「鼠标」状态（2026-10-07）；进入或退出标注模式时复位
    AnnotationPoint            m_annotHoverPt;  //最近一次鼠标在标注区域里的位置（归一化坐标），选择工具据此决定光标
    int                        m_annotHoverTol; //同上，那一次的命中容差（归一化单位）
    bool                       m_annotHoverValid;  //m_annotHoverPt 是否有效（光标离开标注区域后为 false）
    AnnotationPoint            m_textPoint;     //文字输入框对应的位置（归一化坐标）
    AnnotationRef              m_editRef;       //文字输入框正在修改的对象（第 20 期，双击修改已有文字）；空引用表示新加文字
    int64_t                    m_lastSnapshotMs;//上次补发笔迹的时刻（GetTickCount64 毫秒），补发至多每秒一次
    //共享录制
    screenrecord::CRecordController* m_recorder;   //录屏控制器（不拥有）；为 NULL 时不能录制
    HostRecordSource           m_hostRecordSource;   //发起人录制的画面来源
    ViewerRecordSource         m_viewerRecordSource; //观看者录制的画面来源
    SpeakerCameraSource        m_speakerCamera;      //录制的画中画来源
    std::atomic<screenrecord::RecordAudioMixer*> m_recordMixer;   //录制的混音器（录制中有效，声音旁路线程读取）
    bool                       m_shareRecording;     //本人是否在录这次共享
    bool                       m_recordAllowed;      //观看者能否录制（来自 1725）
    std::wstring               m_recordingText;      //其他人正在录制的提示（如「李四 正在录制」），为空表示没有人在录
    int                        m_pipSeat;            //画中画当前画谁的座位号；-1 表示没有
    //通话中共享（第 8 期）
    int64_t                    m_callPeerId;         //已接通的一对一通话的对端 userid；没有通话时为 0
    std::wstring               m_callPeerName;       //通话对端的显示名（通话中发起共享时作为会话名）
    call::ICallShareObserver*  m_callObserver;       //通话控制器（不拥有）；为 NULL 时不刷新通话窗按钮
    //控制键鼠（第 18 期）
    //排队等画面主人答复的一条控制申请
    struct ControlRequest
    {
        int64_t      m_userId;   //申请人 userid
        std::wstring m_name;     //申请人显示名

        ControlRequest();
    };
    int64_t                    m_controllerId;       //发起人一侧：正在控制本人屏幕的人；没有为 0
    int64_t                    m_revokingId;         //发起人一侧：本地已收回、等服务端确认的控制者；确认后（或没有时）为 0
    ShareInputKeyFilter        m_injectKeys;         //发起人一侧：注入按键的系统级按键过滤与按着的键
    std::vector<uint32_t>      m_injectedButtons;    //发起人一侧：已注入按下、还没注入松开的鼠标按钮（存松开消息）
    ShareLocalActivityDetector m_localActivity;      //发起人一侧：本机动静判定（钩子回调里使用）
    HHOOK                      m_mouseHook;          //发起人一侧：低级鼠标钩子；没有被控制时为 NULL
    HHOOK                      m_keyHook;            //发起人一侧：低级键盘钩子；没有被控制时为 NULL
    bool                       m_localActivityPosted;//发起人一侧：本次控制里是否已投过「本机有动静」，避免重复收回
    uint32_t                   m_injectedCount;      //发起人一侧：本次控制里注入了多少条输入（结束控制时记日志）
    uint32_t                   m_droppedCount;       //发起人一侧：本次控制里丢弃了多少条输入（不是控制者、窗口外、系统级按键等）
    std::deque<ControlRequest> m_controlRequests;    //发起人一侧：排队的控制申请，队首正显示在提示里
    CShareCornerToast          m_controlToast;       //发起人一侧：「某某申请控制你的屏幕」提示
    int                        m_viewerControl;      //观看者一侧：控制状态，取值见 share_viewer_control_state
    //画板（第 19 期）
    HCURSOR                    m_annotCursor;        //当前工具的光标（本类创建，换工具或颜色、Shutdown 时销毁）；没建时为 NULL
    int                        m_annotCursorTool;    //m_annotCursor 对应的工具
    uint32_t                   m_annotCursorColor;   //m_annotCursor 对应的颜色
    //多人同时共享（第 22 期）
    int                        m_publishPhase;       //本人作为共享者的阶段，取值见 share_publish_phase
    ShareRelayInfo             m_publishRelay;       //本人作为共享者的中转参数（1738 同意时收到：房间号、「只推流」令牌、画面座位）
    ShareSourceSpec            m_publishSource;      //本人作为共享者共享的内容
    PublisherListener          m_publisherListener;  //共享者推流管线的监听者（声明在 m_publisher 之前，析构晚于它）
    ShareHostStreamer          m_publisher;          //本人作为共享者的推流管线（与观看连接分开的另一条中转连接）
    CShareSharerBar            m_sharerBar;          //共享者控制条（「你正在共享屏幕」与「停止共享」）
    CShareBorderWindow         m_publishBorder;      //共享者被共享内容的边框
    bool                       m_hostScreenStopped;  //发起一侧：主持人停掉了自己的画面（第 29 条），仍是主持人；恢复或结束共享时复位
    CShareHostExitDialog       m_hostExitDialog;     //主持人退出时的移交或结束选择框
    ShareRequestQueue          m_shareRequests;      //主持人一侧：排队的共享申请，队首正显示在提示里
    std::set<int64_t>          m_shareAnswered;      //主持人一侧：已答复、快照里还显示「申请中」的人；快照据此不把他重新排进队列，
                                                     //快照里他不再申请时移除
    //已发出、还没收到应答的一条 1737（第 22 期，第 23 期起带共享的是什么）
    struct PendingSharing
    {
        int     m_action;   //动作，取值见 share_sharing_action
        int64_t m_target;   //对象；没有为 0
        int     m_kind;     //共享的是什么，取值见 share_sharing_kind

        PendingSharing();
    };
    std::deque<PendingSharing> m_pendingSharing;     //已发出、还没收到应答的 1737（服务端按序应答），
                                                     //同意别人而应答人数已满时据此把他放回队列
    CShareCornerToast          m_sharingToast;       //主持人一侧：「某某申请共享屏幕」提示
    CShareScreensWindow        m_screensWindow;      //主持人一侧：「其他人的屏幕」窗口（建房间的主持人用，画面取自推流管线）
    std::vector<ShareScreenInfo> m_screens;          //最近一次快照里本人要看的画面（不含本人自己的），离开房间时清空
    int64_t                    m_controlOwner;       //观看者一侧：申请控制或正在控制的画面主人；没有时为 0
    CShareAnnotationOverlay    m_publishOverlay;     //共享者本机的标注层（第 22 期 22e）：盖在本人共享的内容上，显示这块画面的标注，
                                                     //在自己屏幕上标注时接住鼠标（构造时引用 m_annot，须声明在它之后）
    bool                       m_annotOwnScreen;     //标注模式是不是共享者在自己共享的内容上标注（第 22 期 22e）；退出标注模式时复位
    int                        m_textCanvas;         //文字输入框对应的画面（第 22 期 22e）：按下时记下，确定时文字加到这块画面上
    int                        m_recordCanvas;       //录屏控制器当前叠的是哪块画面的标注（第 22 期 22e）；观看者换了当前那一块时重新交
    //白板（第 23 期）
    CShareWhiteboardWindow     m_boardWindow;        //本人建的共享白板的窗口（房间模式）
    int                        m_boardPhase;         //本人的白板：取值同 share_viewer_mine_state（0 没有、1 已申请、2 开着）
    int                        m_myBoardSeat;        //本人建的白板的画面座位；没有时为 -1
    bool                       m_annotOnBoard;       //标注模式是不是在本人的白板上；退出标注模式时复位
    bool                       m_annotOnScreens;     //标注模式是不是主持人在「其他人的屏幕」窗口里标注（2026-10-07）；退出标注模式时复位
    bool                       m_boardViewDirty;     //本人白板的背景或可见区域变了、还没广播
    int64_t                    m_boardViewSentMs;    //上次广播白板视野的时刻（GetTickCount64 毫秒）
    ShareRequestQueue          m_boardRequests;      //主持人一侧：排队的新建白板申请，队首正显示在提示里
    std::set<int64_t>          m_boardAnswered;      //主持人一侧：已答复、快照里还显示申请中的人
    CShareCornerToast          m_boardToast;         //主持人一侧：「某某申请新建白板」提示
    bool                       m_screenBoard;        //发起一侧：是否在「屏幕当白板」（第 31 条）
    ShareSourceSpec            m_screenBoardSaved;   //进入屏幕白板之前共享的窗口，退出时恢复
    bool                       m_publishScreenBoard;       //共享者一侧（2026-10-07，方案第 14.1 节 ③）：本人共享的那一块是否在
                                                           //「屏幕当白板」；停止共享时复位
    ShareSourceSpec            m_publishScreenBoardSaved;  //共享者进入屏幕白板之前共享的窗口，退出时恢复
    CShareCornerToast          m_saveToast;          //「已保存为图片」提示（带「发送到本会话」）
    ShareParticipantLog        m_participantLog;     //这一场共享里出现过的人（2026-10-05），结束提示据此写分享人与参与者；
                                                     //开始主持或发出加入请求时开始记录，主动离开、移交、出错、提示弹出后清空
    CShareEndedToast           m_endedToast;         //「屏幕共享已结束」右下角提示（2026-10-05，替换原来的 MessageBox）
    flamingomedia::CMediaToast m_noticeToast;        //轻量提示（2026-10-07）：分享链接的操作结果、录制与共享互斥被拦下；Shutdown 时销毁
    std::wstring               m_savedPath;          //最近一次保存的图片路径，提示的「发送到本会话」据此发送
    LPARAM                     m_saveToastToken;     //「已保存为图片」提示的标识（每次保存加一）
    int                        m_saveCtype;          //保存图片时所在的那场共享的会话类型，提示的「发送到本会话」发到这里
    int64_t                    m_saveCid;            //同上，会话 id；没有时为 0
    static CShareController*   s_hookOwner;          //低级钩子回调据此找到控制器；只在装了钩子时非 NULL（同一时间只有一个控制器）
};

} // namespace screenshare
