/**
 *  音视频通话会话提示对话框，BalloonCallDlg.h
 *
 *  基于 balloonui DuiFrameWindow 的非模态小对话框，一个类三种形态（由 Mode 区分）：
 *    - kModeCalling（主叫）：显示"正在呼叫 XXX…"，底部一个"取消"按钮；点取消
 *      向服务端发 cmd_call_cancel 撤回呼叫。
 *    - kModeIncoming（被叫）：显示"XXX 邀请你语音通话"，底部"拒绝 / 接听"两个
 *      按钮；点击向服务端发 cmd_call_response（accept 0 / 1）。接听后窗口不关，
 *      待服务端推来接通信令时由协调器切到 kModeConnected。
 *    - kModeConnected（通话中）：显示"正在与 XXX 通话中…"，底部一个"挂断"按钮；
 *      点挂断向服务端发 cmd_call_hangup。
 *
 *  本对话框只负责"显示 + 把用户的取消 / 接听 / 拒绝 / 挂断转成一条 v2 通话信令发出"，
 *  通过 CIUSocket 单例发送；不持有会话状态机（那在 CMainDlg 的通话协调逻辑里）。
 *  协调器在收到服务端应答 / 推送时调用 OpenConnected 切通话中态、或 CloseDlg 关闭，
 *  并在邀请应答回来后用 SetSessionId 回填会话 id（主叫取消时用）。
 *
 *  仿 CBalloonRemoteDesktopDlg 实现（DuiFrameWindow + 程序化控件树 + OnDuiNotify 派发）。
 *
 *  第三期接入视频：通话中态分纯语音 / 视频两种布局。视频通话的「通话中」态把窗口放大，客户区
 *  上半部是铺满的大画面（自绘 CallVideoCanvas），底部栏左侧放小画面（同类画面控件，画中画）、
 *  右侧放挂断按钮。本类实现 call::CallVideoDisplaySink，由协调器（CMainDlg）把中转会话
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
 *  balloonwj@qq.com   2026-06-11（2026-06-16 接入视频通话画面；2026-07-31 最小化 / 置顶 / 大小画面对调）
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "CallVideoCanvas.h"        //远端 / 本端画面自绘控件
#include "CallVideoDisplaySink.h"   //视频帧显示去向接口（本类实现之）
#include "CallDlgLayout.h"          //标题 / 提示文字组装与画面去向判定（纯函数，有单测）

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
     */
    void OpenIncoming(HWND owner, const CString& fromName, const std::string& sessionId, int64_t fromUserId);

    /**
     *  切到"通话中"形态（接通后由协调器调用；窗口已显示则就地切换、否则新建）。
     *  复用呼叫 / 来电时已设好的对端昵称。布局据 SetVideoCall 设的标志决定纯语音 / 视频两种。
     *  @param owner owner 窗口
     */
    void OpenConnected(HWND owner);

    //协调器主动关闭对话框（收到结束 / 拒绝 / 取消 / 超时等状态时）。未显示则无操作。
    void CloseDlg();

    /**
     *  设置本次通话是否含视频（须在 OpenConnected 之前调用，决定通话中态用视频还是纯语音布局）。
     *  @param video true=视频通话（通话中态显示远端画面 + 本地预览）；false=纯语音
     */
    void SetVideoCall(bool video) { m_videoCall = video; }

    /**
     *  切换本对话框的媒体呈现形态（语音 <-> 视频），通话中调用。由 CMainDlg 收到
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

    //本端摄像头当前是否开启（仅视频态有意义），供协调器决定引擎启停。
    bool IsCameraOn() const { return m_cameraOn; }

    //返回本对话框作为视频帧显示去向的接口指针，供协调器注入到中转会话（CallMediaSession）。
    call::CallVideoDisplaySink* GetVideoDisplaySink() { return this; }

    /**
     *  告知本对话框"本端摄像头不可用"，在本端那一路画面上显示提示（代替一块看不出所以然的黑屏）。
     *  由协调器在收到 call_relay_camera_unavailable 事件时调用。提示会跟着"本端那一路"走——
     *  用户把大小画面对调后，提示也跟着换到另一块控件上。
     *  @param reason 失败原因，取 call::CameraFailReason；文字组装见 call::MakeCameraUnavailableTip
     */
    void SetCameraUnavailable(int reason);

    //协调器在邀请应答回来后回填会话 id（主叫取消时要带上它发 cmd_call_cancel）。
    void SetSessionId(const std::string& sessionId) { m_sessionId = sessionId; }
    const std::string& GetSessionId() const { return m_sessionId; }
    int GetMode() const { return m_mode; }

    //对端 userid（呼叫中=被叫，来电=主叫，通话中=对端）。协调器据它把通话提示
    //追加到与该联系人的聊天窗里。
    int64_t GetPeerUserId() const { return m_peerUserId; }

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

    //WM_TIMER：每秒刷新一次标题栏上的通话时长（仅接通态起表）。
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //把 NV12 帧转 BGRA 拷进堆 VideoFramePacket 并 PostMessage 到本对话框（msgId=远端 / 本端帧）。
    //窗口未创建 / 已销毁则就地丢弃，避免泄漏。
    void PostNv12Frame(UINT msgId, const uint8_t* nv12, int width, int height, int stride);

    //确保窗口已创建（首次调用时 Create）。
    void EnsureWindow(HWND owner);
    //据当前 m_mode（及 m_videoCall）重建控件树。
    void BuildUiOnce();
    //据当前 m_mode 设置标题栏文字与客户区提示文字。
    void ApplyMode();
    //据当前是否视频通话，把标题栏左上角图标设成摄像机 / 电话听筒（位图懒建一次、之后复用）。
    void ApplyTitleIcon();

    //把标题栏那张图标位图转成 HICON 挂到窗口上，使任务栏 / Alt+Tab 的图案与标题栏一致。
    //随媒体类型切换重挂（视频=摄像机、语音=听筒）。
    void ApplyTaskbarIcon();

    //按 m_connectedTick 算出已通话时长，刷新标题栏文字。仅接通态有效。
    void RefreshCallDuration();

    //按当前"谁占大画面"取某一路帧该画到的控件（frameIsLocal=true 表示本端采集帧）。
    //非视频通话中态返回 NULL（此时没有画面控件）。
    call::CallVideoCanvas* CanvasForFrame(bool frameIsLocal);
    //对调大 / 小画面（单击小窗或双击大画面时调用）：翻转 m_localIsBig 并交换两块控件的帧。
    void SwapVideoCanvas();

    //把本端 / 远端两路的占位提示文字按当前"谁占大画面"贴到对应控件上（建树后、对调后都要调）。
    void ApplyCanvasPlaceholders();

    //在标题栏 close 按钮左侧装 / 更新「固定」图标按钮，按 m_topmost 取普通 / 置顶态图标与提示文字。
    void EnsurePinCaptionIcon();
    //切换窗口置顶态并同步标题栏图标（点「固定」图标时调用）。
    void TogglePinned();

private:
    int         m_mode;         //对话框形态，见 enum Mode
    std::string m_sessionId;    //会话 id
    int64_t     m_peerUserId;   //对端 userid（calling=被叫，incoming=主叫，connected=对端）
    CString     m_peerName;     //对端昵称（标题栏与提示文字用）
    bool        m_built;        //控件树是否已构建
    bool        m_videoCall;    //本次通话是否含视频：决定通话中态用视频布局还是纯语音布局
    bool        m_cameraOn;     //本端摄像头是否开启（仅视频态有意义）：决定小窗画自己画面还是"摄像头已关闭"占位
    bool        m_localIsBig;   //视频通话中：是否本端画面占大画面（false=对方占大画面，默认）
    bool        m_topmost;      //视频通话窗当前是否已「固定」（置顶）；窗口重建时复位为 false
    CString     m_localTip;     //本端那一路画面尚无画面时的提示（初始"正在打开摄像头…"；摄像头出问题时换成失败原因）
    CString     m_remoteTip;    //远端那一路画面尚无画面时的提示（"正在等待对方的画面…"，收到首帧后自然不再显示）

    balloonwjui::DuiLabel*  m_msgLabel;   //主提示文字（纯语音态 / 切形态时改）；视频态可能为空
    balloonwjui::DuiButton* m_btnLeft;    //次按钮：incoming=拒绝；calling/connected 隐藏
    balloonwjui::DuiButton* m_btnRight;   //主按钮：incoming=接听 / calling=取消 / connected=挂断
    call::CallVideoCanvas*  m_bigCanvas;  //大画面控件（视频通话中态铺满上半部）；非视频态为空
    call::CallVideoCanvas*  m_pipCanvas;  //小画面控件（视频通话中态底部栏左下角画中画）；非视频态为空

    int     m_pinIconId;       //标题栏「固定」图标按钮的 caption icon id（AddCaptionIcon 返回）；0=未装
    HBITMAP m_pinIconNormal;   //未置顶态的「固定」图标位图（本类拥有，析构时 DeleteObject）
    HBITMAP m_pinIconActive;   //已置顶态的「固定」图标位图（本类拥有，析构时 DeleteObject）
    //打开本窗口时传入的 owner（即 CMainDlg 的 HWND）。关窗时要经它请求协调器停掉媒体会话，
    //否则采集线程继续跑、摄像头灯不灭（通话窗自己不持有 CallMediaSession）。
    HWND    m_hOwnerWnd;
    HBITMAP m_titleIconVideo;  //标题栏左上角图标：摄像机（视频通话用；本类拥有，析构时 DeleteObject）
    //通话接通的时刻（GetTickCount64）。0 表示尚未接通 —— 呼叫中 / 来电态不计时。
    //时长由它与当前时刻之差算出，不受系统时钟调整影响。
    ULONGLONG m_connectedTick;
    //任务栏 / Alt+Tab 用的窗口图标（由标题栏那张位图转出，与标题栏图案一致）。
    //本类持有所有权，析构与切换媒体类型时 DestroyIcon。
    HICON   m_taskbarIcon;
    HBITMAP m_titleIconAudio;  //标题栏左上角图标：电话听筒（纯语音用；本类拥有，析构时 DeleteObject）
};
