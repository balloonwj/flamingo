/**
 *  音视频通话会话提示对话框，BalloonCallDlg.h
 *
 *  基于 balloonui DuiFrameWindow 的非模态小对话框，一个类三种形态（由 Mode 区分）：
 *    - kModeCalling（主叫）：与接通后同一个窗口形态（776×569，可最小化、最大化、固定、拖边框改大小），画面区显示
 *      对方头像与"正在呼叫，等待对方接听…"，底部栏右侧一个"取消"按钮；点取消向服务端发 cmd_call_cancel 撤回呼叫。
 *      对方接听后原地换成通话中态的控件树，窗口尺寸与位置不变（2026-10-05 起，见 CallingLayout.h）。
 *    - kModeIncoming（被叫）：显示对方名字与"邀请你语音通话"，底部"拒绝 / 接听"两个
 *      按钮；点击向服务端发 cmd_call_response（accept 0 / 1）。接听后窗口不关，
 *      待服务端推来接通信令时由通话控制器切到 kModeConnected。
 *    - kModeConnected（通话中）：显示"正在与 XXX 通话中…"，底部一个"挂断"按钮；
 *      点挂断向服务端发 cmd_call_hangup。
 *
 *  本对话框只负责"显示 + 把用户的取消 / 接听 / 拒绝 / 挂断 / 切换媒体交给通话控制器"：经 SetActions
 *  登记的 call::ICallDialogActions 转交，由通话控制器（call::CCallController，FlamingoMedia.dll）编好
 *  v2 通话信令经宿主发出；本类不持有会话状态机。控制器在收到服务端应答 / 推送时调用 OpenConnected
 *  切通话中态、或 CloseDlg 关闭，并在邀请应答回来后用 SetSessionId 回填会话 id（主叫取消时用）。
 *
 *  仿 CBalloonRemoteDesktopDlg 实现（DuiFrameWindow + 程序化控件树 + OnDuiNotify 派发）。
 *
 *  第三期接入视频：通话中态分纯语音 / 视频两种布局。视频通话的「通话中」态把窗口放大，客户区
 *  上半部是铺满的大画面（自绘 CallVideoCanvas），底部栏左侧放小画面（同类画面控件，画中画）、
 *  右侧放挂断按钮。本类实现 call::CallVideoDisplaySink，由通话控制器把中转会话
 *  的视频显示去向指向本对话框；远端帧（视频解码线程）/ 本端采集帧（采集线程）经 sink 回调到来后，
 *  本类把 NV12 转 BGRA、拷到堆、PostMessage 转投 UI 线程，再在消息处理里喂给对应画面控件重绘
 *  （仿远程桌面观看窗 RdViewerWindow 的帧投递做法）。
 *
 *  2026-07-31 视频通话窗的三处改进：
 *    · 标题栏加最小化按钮，并在 close 左侧加一个「固定」图标按钮（caption icon）切换窗口置顶，
 *      置顶态图标变蓝、提示文字随之改成「取消置顶」。
 *    · 标题栏文字写明在跟谁通话（"与 XXX 视频通话中"），文字组装见 CallDlgLayout。
 *    · 大 / 小画面可对调：默认对方在大画面、自己在左下角小窗；单击小窗或双击大画面即互换
 *      （m_localIsBig 翻转 + 两块控件交换已持有的帧，画面即时互换）。
 *
 *  2026-10-05 音视频通话优化（docs/音视频通话优化方案.md）：
 *    · 来电态改为右下角 420×200 的弹窗，左边对方头像、右边对方名字与「邀请你语音 / 视频通话」（Common/IncomingCallLayout）。
 *    · 没有画面时显示头像：语音态画面区的听筒换成对方头像；视频态对方那一路在还没收到画面、或画面停了 2 秒以上时显示
 *      对方头像，本端摄像头关着时本端那一路显示本人头像（头像经宿主取得，见 SetHost）。
 *    · 「固定」改为固定在桌面：置顶，并且不能拖动、改大小、最小化（Common/WindowPin），再点一次解除。
 *    · 用户追加：呼叫态直接用接通后的窗口形态，只是画面区显示对方头像与「正在呼叫…」、底部栏只有「取消」；
 *      接通态加最大化按钮（固定期间隐藏）。
 *
 *  2026-10-05 按钮改为圆形图标按钮（CallButtonArt），并新增麦克风开关与音量调节：
 *    · 接通态底部栏按 CallActionBarModel 的序列建按钮：麦克风、音量、摄像头（仅视频态）、切换媒体、共享屏幕、挂断；
 *      呼叫中的「取消」与来电的「拒绝 / 接听」同样改为圆形图标按钮。原来的按钮文字改作悬停提示。
 *    · 麦克风：经通话控制器真正停开本端采集；关着时按钮呈深灰底、麦克风带斜杠。
 *    · 音量：点击在按钮上方弹出浮层（balloonui DuiPopupHost + DuiSlider，0~150%），只调本端听到的声音。
 *
 *  balloonwj@qq.com   2026-06-11（2026-06-16 接入视频通话画面；2026-07-31 最小化 / 置顶 / 大小画面对调；
 *  2026-10-02 随一对一通话迁入 FlamingoMedia.dll，信令改经 ICallDialogActions 交给通话控制器）
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "CallVideoCanvas.h"        //远端 / 本端画面自绘控件

namespace balloonwjui
{
    class DuiBadge;   //「P2P」徽章（成员仅持裸指针引用，前向声明即可）
    class DuiAvatar;  //来电态左侧的头像（成员仅持裸指针引用）
    class DuiHBox;    //接通态的底部栏（AddActionButton 的参数）
}
#include "CallVideoDisplaySink.h"   //视频帧显示去向接口（本类实现之）
#include "CallDlgLayout.h"          //标题 / 提示文字组装与画面去向判定（纯函数，有单测）
#include "CallDialogActions.h"      //用户操作的接收方接口（通话控制器实现之）
#include "CallActionBarModel.h"     //底部栏按钮序列（纯函数，有单测）
#include "CallButtonArt.h"          //圆形图标按钮的外观与位图缓存（2026-10-05 起）
#include "CallSignalArt.h"          //底部栏的网络信号图标（网络状况标识第 3 期，2026-10-06）
#include "CallOverlayBar.h"         //视频态浮在画面底部、可淡入淡出的操作条（2026-10-06）
#include "CallBarAutoHide.h"        //操作条自动隐藏的时序（纯逻辑，有单测）
#include "CallNetQuality.h"         //档位、降级提示与悬停详情文字（纯函数，有单测）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"   //网络变差时画面区上方的提示
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"   //音量浮层
#include "../../Source/UI/BalloonMain/CornerPopupCountdown.h"   //来电标题后的倒计时（第 15 期）
#include "../Common/MediaAvatar.h"   //CMediaAvatarSource：来电窗与画面区里的头像（docs/音视频通话优化方案.md 第 1、5 项）

class CBalloonCallDlg : public balloonwjui::DuiFrameWindow,
                        public call::CallVideoDisplaySink
{
public:
    //对话框形态
    enum Mode
    {
        kModeCalling   = 0,   //主叫：正在呼叫，等待对方接听
        kModeIncoming  = 1,   //被叫：收到来电，等待本人接听 / 拒绝
        kModeConnected = 2,   //通话中：双方已接通
    };

    CBalloonCallDlg();
    ~CBalloonCallDlg();

    /**
     *  以"主叫呼叫中"形态显示（已显示则更新内容并前置）。
     *  @param owner      owner 窗口（一般为主面板，居中参照）
     *  @param peerName   被叫昵称（显示在提示文字里）
     *  @param peerUserId 被叫 userid
     */
    void OpenCalling(HWND owner, const CString& peerName, int64_t peerUserId);

    /**
     *  以"被叫来电"形态显示（已显示则更新内容并前置）。
     *  @param owner      owner 窗口
     *  @param fromName   主叫昵称
     *  @param sessionId  会话 id（应答时原样回带）
     *  @param fromUserId 主叫 userid
     *  @param ringMs     剩余振铃毫秒数（第 15 期，来自 1702 的 ringms），标题后据此倒数「N 秒后无人接听自动结束」；
     *                    小于 0 表示服务端没有给出，按服务端的振铃超时 45 秒倒数
     */
    void OpenIncoming(HWND owner, const CString& fromName, const std::string& sessionId, int64_t fromUserId,
                      int64_t ringMs);

    /**
     *  切到"通话中"形态（接通后由通话控制器调用；窗口已显示则就地切换、否则新建）。
     *  复用呼叫 / 来电时已设好的对端昵称。布局据 SetVideoCall 设的标志决定纯语音 / 视频两种。
     *  @param owner owner 窗口
     */
    void OpenConnected(HWND owner);

    //通话控制器主动关闭对话框（收到结束 / 拒绝 / 取消 / 超时等状态时）。未显示则无操作。
    void CloseDlg();

    /**
     *  有用户的头像变了：重新取对方与本人的头像并重画（来电态左侧头像、通话画面在没开摄像头时的头像）。窗口没开时什么也不做。
     */
    void RefreshAvatars();

    /**
     *  登记宿主：取对方与本人的头像、取本人 userid 都经它（docs/音视频通话优化方案.md 第 1、5 项）。通话控制器在 Init 时调用。
     *  @param host 宿主（借用，不拥有；须比本对象的使用期长）；为 NULL 时头像一律画问号
     */
    void SetHost(flamingomedia::IMediaHost* host);

    /**
     *  设置本次通话是否含视频（须在 OpenConnected 之前调用，决定通话中态用视频还是纯语音布局）。
     *  一开始就是视频通话时双方的摄像头都会打开，本端摄像头状态随之记为开启（按钮显示「关闭摄像头」）。
     *  @param video true=视频通话（通话中态显示远端画面 + 本地预览）；false=纯语音
     */
    void SetVideoCall(bool video)
    {
        m_videoCall = video;
        m_cameraOn = video;
        m_cameraFailed = false;
    }

    //本次通话是否含视频（SetVideoCall / ApplyMediaMode 设过的值）
    bool IsVideoCall() const { return m_videoCall; }

    /**
     *  登记接收用户操作的通话控制器：取消 / 接听 / 拒绝 / 挂断 / 切换媒体都经它发信令。
     *  @param actions 操作接收方（不拥有，须在本对象销毁或再次登记之前一直有效）；传 NULL 解除登记，
     *                 此后用户操作只关窗、不发信令
     */
    void SetActions(call::ICallDialogActions* actions) { m_actions = actions; }

    /**
     *  登记关窗通知的接收窗口：用户点标题栏关闭按钮时向它投递 call::kMsgCallStopMedia，请求停掉媒体会话。
     *  @param hwnd 接收窗口（通话控制器的消息窗口）；传 NULL 解除登记
     */
    void SetNotifyWindow(HWND hwnd) { m_hNotifyWnd = hwnd; }

    /**
     *  切换本对话框的媒体呈现形态（语音 <-> 视频），通话中调用。由通话控制器收到
     *  cmd_call_media_changed_notify 推送后调用 —— 本端不自作主张切，一律等推送。
     *
     *  以视频对话框为基座：语音态即"视频布局但画面区换成对端头像 + 昵称"，故窗口尺寸、
     *  标题栏与底部按钮条的位置在切换前后保持稳定，不会跳变。
     *
     *  @param video      切换后是否为视频通话
     *  @param openCamera 本端摄像头是否处于开启态。按已定交互模型：升级到视频时只有
     *                    发起方为 true，另一方进入视频界面但摄像头关闭；降级回语音时恒为 false
     */
    void ApplyMediaMode(bool video, bool openCamera);

    //本端摄像头当前是否开启（仅视频态有意义），供通话控制器决定引擎启停。
    bool IsCameraOn() const { return m_cameraOn; }

    //返回本对话框作为视频帧显示去向的接口指针，供通话控制器注入到中转会话（CallMediaSession）。
    call::CallVideoDisplaySink* GetVideoDisplaySink() { return this; }

    /**
     *  告知本对话框"本端摄像头不可用"，在本端那一路画面上显示提示（代替一块看不出所以然的黑屏）。
     *  由通话控制器在收到 call_relay_camera_unavailable 事件时调用。提示会跟着"本端那一路"走——
     *  用户把大小画面对调后，提示也跟着换到另一块控件上。
     *  @param reason 失败原因，取 call::CameraFailReason；文字组装见 call::MakeCameraUnavailableTip
     */
    void SetCameraUnavailable(int reason);

    //通话控制器在邀请应答回来后回填会话 id（主叫取消时要带上它发 cmd_call_cancel）。
    void SetSessionId(const std::string& sessionId) { m_sessionId = sessionId; }
    const std::string& GetSessionId() const { return m_sessionId; }
    int GetMode() const { return m_mode; }

    //对端 userid（呼叫中=被叫，来电=主叫，通话中=对端）。通话控制器据它把通话提示
    //追加到与该联系人的聊天窗里。
    int64_t GetPeerUserId() const { return m_peerUserId; }

    //对端显示名（通话中发起屏幕共享时作为会话名）
    const CString& GetPeerName() const { return m_peerName; }

    /**
     *  刷新「共享屏幕 / 停止共享」按钮：由通话控制器在屏幕共享的状态变化时调用（第 8 期）。
     *  @param sharing 本端是否正在与通话对方共享屏幕
     */
    void SetScreenSharing(bool sharing);

    /**
     *  刷新「共享电脑声音」按钮（2026-10-06）：由通话控制器在本端电脑声音共享的状态变化时调用（用户开关、被屏幕共享
     *  接替、通话结束）。只在通话中态有这个按钮，其余形态只记下状态。
     *  @param on 本端是否正在共享电脑声音
     */
    void SetSystemAudioOn(bool on);

    /**
     *  在画面区上方的提示条里显示一句话（与网络变差的提示共用同一个提示条，几秒后自动消失）。
     *  只在通话中态有提示条，其余形态什么也不做。
     *  @param text 提示文字（已翻译）；为空时什么也不做
     */
    void ShowNotice(const std::wstring& text);

    /**
     *  设置通话音视频的链路实际路径（2026-10-04 起）：直连时在接通态底部栏显示「P2P」徽章。通话控制器在中转会话连上时调用；
     *  重建控件树时按记下的值恢复。
     *  @param linkPath 取值同 CallMediaSession::LinkPath
     */
    void SetLinkPath(int linkPath);

    /**
     *  告知本对话框音频引擎运行在 Debug 版的无设备模式（FLAMINGO_CALL_NO_AUDIO=1，2026-10-05 起）：在大画面顶部显示
     *  「调试模式：未使用麦克风和扬声器」，避免把听不到声音误认为通话故障。重建控件树时保留，每通新电话接通时复位。
     *  @param on 是否处于无设备模式
     */
    void SetNoAudioDevices(bool on);

    //已通话时长文本（"MM:SS" / 超一小时 "H:MM:SS"）。未接通过返回空串 ——
    //呼叫被拒 / 无应答这类根本没接通的情况，提示里不该带时长。
    CString GetCallDurationText() const;

    BEGIN_MSG_MAP(CBalloonCallDlg)
        //WM_DESTROY 须排在 CHAIN_MSG_MAP 之前：先清掉本类持有的控件裸指针，再让基类释放控件树
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CALL_REMOTE_FRAME, OnRemoteFrameMsg)
        MESSAGE_HANDLER(WM_CALL_LOCAL_FRAME, OnLocalFrameMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        //视频态操作条的自动隐藏只旁听鼠标移动与离开（bHandled 置 FALSE），消息照常交给基类分发给控件
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMoveMsg)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeaveMsg)
        //固定期间拦下移动、改大小、最小化、最大化（须排在 CHAIN_MSG_MAP 之前，见 Common/WindowPin.h）
        MESSAGE_HANDLER(WM_SYSCOMMAND, OnSysCommandMsg)
        MESSAGE_HANDLER(WM_WINDOWPOSCHANGING, OnWindowPosChangingMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //本对话框私有窗口消息（仅 PostMessage 到本对话框自身 HWND；仿 RdViewerWindow 用 WM_APP 区间）。
    enum
    {
        WM_CALL_REMOTE_FRAME = WM_APP + 1,   //远端解码帧到达（lParam = 堆 VideoFramePacket*）
        WM_CALL_LOCAL_FRAME  = WM_APP + 2,   //本端采集帧到达（lParam = 堆 VideoFramePacket*）
    };

    //经 PostMessage 跨线程转投 UI 线程的一帧画面（已转成 BGRA、自上而下、行紧排）。
    struct VideoFramePacket
    {
        std::vector<uint8_t> m_bgra;   //BGRA 像素（w*h*4 字节，自上而下）
        int                  m_w;      //帧宽（像素）
        int                  m_h;      //帧高（像素）

        VideoFramePacket()
            : m_w(0)
            , m_h(0)
        {
        }
    };

    //==== call::CallVideoDisplaySink：远端帧在视频解码线程触发、本端帧在采集线程触发 ====
    //两者都把 NV12 转 BGRA、拷到堆 VideoFramePacket，PostMessage 转投 UI 线程绘制。
    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) override;
    virtual void OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride) override;

    //窗口销毁：清空本类持有的、指向控件树的裸指针（控件树随后由基类释放），
    //避免通话结束后晚到的事件（迟到的视频帧、摄像头不可用通知）解引用悬空指针。
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：取出堆 VideoFramePacket 喂远端 / 本端画面控件重绘。
    LRESULT OnRemoteFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLocalFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_TIMER：每秒刷新一次标题栏上的通话时长（仅接通态起表），并检查对方画面是否已停（视频态）。
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_SYSCOMMAND：固定期间拦下移动、改大小、最小化、最大化、还原，其余交给基类
    LRESULT OnSysCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_WINDOWPOSCHANGING：固定期间保住窗口位置与大小（Aero Snap、Win+方向键等不经 WM_SYSCOMMAND 的路径），其余交给基类
    LRESULT OnWindowPosChangingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //把 NV12 帧转 BGRA 拷进堆 VideoFramePacket 并 PostMessage 到本对话框（msgId=远端 / 本端帧）。
    //窗口未创建 / 已销毁则就地丢弃，避免泄漏。
    void PostNv12Frame(UINT msgId, const uint8_t* nv12, int width, int height, int stride);

    //用户主动关窗的唯一出口（「取消」「拒绝」「挂断」按钮与标题栏关闭按钮都走这里）：按当前形态发取消 / 拒绝 / 挂断信令
    //（见 call::PickUserCloseSignal），再向通话控制器投递 call::kMsgCallStopMedia 请求停媒体会话，最后销毁窗口。
    //服务端只把结束推送发给对方，本端的媒体会话、P2P 链路槽与屏幕共享的通话状态只能靠这条请求收尾。
    void CloseByUser();

    //确保窗口已创建（首次调用时 Create）。
    void EnsureWindow(HWND owner);
    //据当前 m_mode（及 m_videoCall）重建控件树。
    void BuildUiOnce();
    //据当前 m_mode 设置标题栏文字与客户区提示文字。
    void ApplyMode();
    //把标题栏左上角图标设成程序图标（第 11 期 11a；位图懒建一次、之后复用），并顺带确保 GDI+ 已初始化。
    void ApplyTitleIcon();

    //把程序图标（exe 模块的 IDR_MAINFRAME，大、小两种尺寸）挂到窗口上，作为任务栏 / Alt+Tab 的图标。
    void ApplyTaskbarIcon();

    //来电态按剩余振铃时间刷新标题栏文字（第 15 期）：「语音通话 · 42 秒后无人接听自动结束」。其它形态不做任何事。
    void RefreshRingTitle();

    //按 m_connectedTick 算出已通话时长，刷新标题栏文字。仅接通态有效。
    void RefreshCallDuration();

    /**
     *  每秒一次（网络状况标识第 3 期）：经 m_actions 取网络快照，刷新底部栏信号图标与它的悬停详情；显示档位降到「较差」
     *  及以下时在画面区上方浮出一句提示（同一次降级只提示一次）。仅接通态且信号图标已建好时有效；m_actions 为空时
     *  按「正在检测」显示。
     *  @param allowHint 是否判断并浮出降级提示：计时器调用时为 true；建控件树时为 false（提示控件尚未建好，
     *                   此时判断会把这一次降级的提示丢掉）
     */
    void RefreshNetStatus(bool allowHint);

    /**
     *  在底部栏加网络信号图标（接通态建控件树时调用），并记下控件指针、贴上当前档位的位图。
     *  @param row        底部栏（借用）
     *  @param leftMargin 图标左边距（像素）：视频态紧挨小画面为 0，语音态在最左为 kCallSideMargin
     */
    void AddSignalIcon(balloonwjui::DuiHBox* row, int leftMargin);

    //鼠标在客户区移动（位置确有变化时）：视频态操作条淡入并重新起算静止时长，记下鼠标是否停在操作条上
    LRESULT OnMouseMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //鼠标离开客户区：不再停在操作条上，静止时长照常起算
    LRESULT OnMouseLeaveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按 m_barFade 的时序刷新视频态操作条（2026-10-06 起）：先按「鼠标停在操作条上或音量浮层开着」更新保持状态，
     *  再设置不透明度，最后安排下一次计时器（淡入淡出期间逐帧、等静止时长时到点一次、保持显示时低频复查、
     *  完全隐藏后停掉）。没有浮动操作条（语音态、非接通态）时停掉计时器。
     */
    void ApplyBarFade();

    //按当前"谁占大画面"取某一路帧该画到的控件（frameIsLocal=true 表示本端采集帧）。
    //非视频通话中态返回 NULL（此时没有画面控件）。
    call::CallVideoCanvas* CanvasForFrame(bool frameIsLocal);
    //对调大 / 小画面（单击小窗或双击大画面时调用）：翻转 m_localIsBig 并交换两块控件的帧。
    void SwapVideoCanvas();

    //把本端 / 远端两路的占位提示文字与头像按当前"谁占大画面"贴到对应控件上（建树后、对调后、头像更新后都要调）。
    //语音态大画面放对方头像；视频态对方那一路放对方头像、本端那一路放本人头像，只在该路没有画面时才画出来。
    void ApplyCanvasPlaceholders();

    //视频通话中：对方画面超过 call::kRemoteVideoStaleMs 没有新帧时清掉定格的最后一帧，露出对方头像与提示（每秒检查一次）
    void CheckRemoteVideoStale();

    /**
     *  在底部栏末尾加一个圆形图标按钮（接通态按 CallActionBarModel 的序列逐个调用），并记下对应的控件指针。
     *  @param row  底部栏（借用）
     *  @param item 按钮种类与激活态
     *  @param last 是否为最右边的一个（右侧留 kCallSideMargin 的边距）
     */
    void AddActionButton(balloonwjui::DuiHBox* row, const call::CallActionItem& item, bool last);

    //按新的激活态重新装上某个按钮的位图与提示文字（麦克风、共享、音量的状态变化时调用）；按钮为 NULL 时什么也不做
    void RefreshActionButton(balloonwjui::DuiButton* button, int actionId, bool active);

    //点了麦克风按钮：经通话控制器打开或关闭本端麦克风，按结果刷新按钮
    void ToggleMic();

    //点了音量按钮：在按钮上方弹出音量浮层；浮层开着时再点一次则关闭
    void ToggleVolumePopup();

    //音量滑块变化：交给通话控制器设置播放音量，刷新百分比文字；跨过 0 时刷新音量按钮的图标
    void OnVolumeSliderChanged(int percent);

    //音量浮层关闭时的回调（DuiPopupHost 的关闭回调，userdata 是本对象）：记下最终音量与关闭时刻
    static void OnVolumePopupDismissed(void* userdata, balloonwjui::DuiPopupHost::Reason reason);

    //当前播放音量（%）；没有登记通话控制器时按默认值
    int CurrentVolume() const;

    //在标题栏 close 按钮左侧装 / 更新「固定」图标按钮，按 m_topmost 取普通 / 已固定态图标与提示文字。
    void EnsurePinCaptionIcon();
    //切换固定态并同步标题栏图标（点「固定」图标时调用）：固定时置顶、不能拖动和改大小、隐藏最小化按钮，解除时恢复。
    void TogglePinned();

private:
    int         m_mode;         //对话框形态，见 enum Mode
    std::string m_sessionId;    //会话 id
    int64_t     m_peerUserId;   //对端 userid（calling=被叫，incoming=主叫，connected=对端）
    CString     m_peerName;     //对端昵称（标题栏与提示文字用）
    bool        m_built;        //控件树是否已构建
    bool        m_videoCall;    //本次通话是否含视频：决定通话中态用视频布局还是纯语音布局
    bool        m_cameraOn;     //本端摄像头是否开启（仅视频态有意义）：决定小窗画自己画面还是"摄像头已关闭"占位
    bool        m_cameraFailed; //本端摄像头是否打不开或中途断掉（SetCameraUnavailable 置位，重新开启或切到视频时复位）：
                                //为 true 时本端那一路显示失败原因而不是「摄像头已关闭」
    bool        m_localIsBig;   //视频通话中：是否本端画面占大画面（false=对方占大画面，默认）
    bool        m_topmost;      //通话窗当前是否已「固定」（置顶，不能移动、改大小、最小化）；窗口重建时复位为 false
    CString     m_localTip;     //本端那一路画面尚无画面时的提示（初始"正在打开摄像头…"；摄像头出问题时换成失败原因）
    CString     m_remoteTip;    //远端那一路画面尚无画面时的提示（"正在等待对方的画面…"，收到首帧后自然不再显示）

    balloonwjui::DuiButton* m_btnLeft;    //次按钮：incoming=拒绝；calling/connected 隐藏
    balloonwjui::DuiButton* m_btnRight;   //主按钮：incoming=接听 / calling=取消 / connected=挂断
    call::CallVideoCanvas*  m_bigCanvas;  //大画面控件（视频通话中态铺满上半部）；非视频态为空
    call::CallVideoCanvas*  m_pipCanvas;  //小画面控件（视频通话中态底部栏左下角画中画）；非视频态为空
    balloonwjui::DuiButton* m_btnShare;   //通话中态的「共享屏幕 / 停止共享」按钮（借用，随控件树重建）；其余形态为空
    bool                    m_screenSharing; //本端是否正在与通话对方共享屏幕（决定按钮外观，重建控件树时沿用）
    balloonwjui::DuiButton* m_btnSysAudio;   //通话中态的「共享电脑声音」按钮（借用，随控件树重建；2026-10-06）；其余形态为空
    bool                    m_systemAudioOn; //本端是否正在共享电脑声音（决定按钮外观，重建控件树时沿用；每通电话从 false 开始）
    bool                    m_peerSysAudio;  //上次刷新时对方是否正在共享电脑声音（变化时提示一次；每通电话从 false 开始）
    balloonwjui::DuiButton* m_btnMic;     //接通态的麦克风按钮（借用，随控件树重建）；其余形态为空
    balloonwjui::DuiButton* m_btnVolume;  //接通态的音量按钮（借用，随控件树重建）；其余形态为空
    balloonwjui::DuiButton* m_btnCamera;  //视频态的摄像头按钮（借用，随控件树重建）；语音态与其余形态为空
    balloonwjui::DuiButton* m_btnSwitch;  //接通态的切换媒体按钮（借用，随控件树重建）；其余形态为空
    bool                    m_micOff;     //本端麦克风是否已关闭（接通时复位为 false；重建控件树时沿用）
    bool                    m_noAudioDevices; //音频引擎是否运行在 Debug 版无设备模式（SetNoAudioDevices 设；接通时复位）
    //音量浮层（按钮上方弹出，内含滑块与百分比文字）。以本窗口为所有者弹出，滑块的通知经所有者送到本窗口的 OnDuiNotify；
    //本窗口销毁时系统随之销毁浮层窗口，下次打开时重新创建、重新设内容
    balloonwjui::DuiPopupHost m_volumePopup;
    //浮层里的百分比文字（归浮层的控件树所有，本指针仅引用）；浮层没打开过或内容已重建时为空
    balloonwjui::DuiLabel*  m_volumeLabel;
    //音量浮层最近一次关闭的时刻（GetTickCount64，毫秒）：点音量按钮时浮层先因失去焦点关闭、再收到点击，
    //据此判断这次点击是「关闭浮层」而不是「重新打开」
    ULONGLONG               m_volumePopupClosedTick;

    int     m_pinIconId;       //标题栏「固定」图标按钮的 caption icon id（AddCaptionIcon 返回）；0=未装
    HBITMAP m_pinIconNormal;   //未置顶态的「固定」图标位图（本类拥有，析构时 DeleteObject）
    HBITMAP m_pinIconActive;   //已置顶态的「固定」图标位图（本类拥有，析构时 DeleteObject）
    //关窗通知的接收窗口（通话控制器的消息窗口，由 SetNotifyWindow 登记）。关窗时要向它请求停掉媒体会话，
    //否则采集线程继续跑、摄像头灯不灭（通话窗自己不持有 CallMediaSession）。
    HWND    m_hNotifyWnd;
    //用户操作的接收方（通话控制器，由 SetActions 登记，不拥有）。为 NULL 时用户操作只关窗、不发信令
    call::ICallDialogActions* m_actions;
    HBITMAP m_titleIcon;       //标题栏左上角图标：程序图标（第 11 期 11a；本类拥有，析构时 DeleteObject）
    //通话接通的时刻（GetTickCount64）。0 表示尚未接通 —— 呼叫中 / 来电态不计时。
    //时长由它与当前时刻之差算出，不受系统时钟调整影响。
    ULONGLONG m_connectedTick;
    //来电的振铃倒计时（第 15 期）：来电态打开时开始，接通或关窗时停止。到 0 不由客户端关窗，等服务端的超时推送
    balloonmain::CornerCountdown m_ringCountdown;
    //任务栏 / Alt+Tab 用的窗口图标：程序图标的大、小两种尺寸（从 exe 模块的 IDR_MAINFRAME 加载，第 11 期 11a）。
    //本类持有所有权，窗口销毁时 DestroyIcon，下次打开窗口时重新加载。
    HICON   m_taskbarIcon;
    HICON   m_taskbarIconSmall;
    //视频态底部栏小画面右边的通话时长标签（第 11 期 11b，控件树持有，本指针仅引用）；语音态时长画在画面区里，本指针为空
    balloonwjui::DuiLabel* m_durationLabel;
    //接通态底部栏的「P2P」徽章（2026-10-04 起，控件树持有，本指针仅引用）；非接通态为空
    balloonwjui::DuiBadge* m_p2pBadge;
    //通话音视频的链路实际路径（见 SetLinkPath）；重建控件树时据它决定徽章显隐
    int                    m_linkPath;
    //宿主（SetHost 登记，借用不拥有）：取本人 userid；为 NULL 时本端那一路不画头像
    flamingomedia::IMediaHost* m_host;
    //来电态左侧的头像（控件树持有，本指针仅引用）；呼叫中态与接通态为空（头像画在画面区里）
    balloonwjui::DuiAvatar* m_incomingAvatar;
    //最近一次收到对方画面的时刻（GetTickCount64，毫秒）；0 表示本次视频还没收到过。语音切到视频时清零
    ULONGLONG m_lastRemoteFrameTick;
    //对方画面是否已判定为停止（已清掉最后一帧、改显示头像）；收到新帧时复位
    bool      m_remoteStale;
    //来电窗与画面区里用到的头像位图（位图归它所有）。声明在全部控件指针之后：析构时先于基类释放，此时窗口早已销毁、
    //借用位图的控件都已不存在
    flamingomedia::CMediaAvatarSource m_avatars;
    //全部圆形图标按钮的四态位图（位图归它所有，2026-10-05 起）。与 m_avatars 同理，析构时借用位图的按钮都已不存在
    call::CallButtonArtCache m_buttonArt;
    //==== 网络状况标识（第 3 期，2026-10-06）====
    //接通态底部栏的信号图标（控件树持有，本指针仅引用）；非接通态为空
    call::CallSignalIcon*   m_signalIcon;
    //网络变差时画面区上方的提示（控件树持有，本指针仅引用）；非接通态为空
    balloonwjui::DuiToast*  m_netToast;
    //降级提示的触发状态：每通电话接通时复位
    call::CallDegradeNotifier m_degradeNotifier;
    //上一次显示的档位（取值见 call::CallNetGrade，未评级为 call::kCallGradeUnknown），档位变化时记一条日志
    int                     m_shownGrade;
    //==== 视频态操作条浮在画面上并自动隐藏（2026-10-06，docs/音视频通话-界面美化方案.md 第 3.4 节）====
    //视频态浮在画面底部的操作条（控件树持有，本指针仅引用）；语音态与非接通态为空
    call::CallOverlayBar*   m_overlayBar;
    //操作条的淡入淡出时序：每次建视频态控件树时复位
    call::CallBarAutoHide   m_barFade;
    //鼠标是否停在操作条上（据窗口收到的鼠标消息判断，离开客户区时复位）；停在上面时操作条保持显示
    bool                    m_mouseOverBar;
    //上一次鼠标移动消息的位置（客户区坐标），用来滤掉位置没变的 WM_MOUSEMOVE（窗口变化时系统也会补发）
    POINT                   m_lastMousePt;
    //m_lastMousePt 是否有效
    bool                    m_hasMousePt;
    //信号图标的位图（位图归它所有）。声明在最后：析构时先于其它成员释放，此时窗口早已销毁、借用位图的图标已不存在
    call::CallSignalArtCache m_signalArt;
};
