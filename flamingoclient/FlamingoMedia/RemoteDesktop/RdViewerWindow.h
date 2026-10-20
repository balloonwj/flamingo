/**
 *  远程桌面控制端观看窗，RdViewerWindow.h
 *
 *  控制端在收到中转参数(cmd_rd_relay_info, role=控制端)后用本窗口观看对方屏幕：
 *  基于 balloonui DuiFrameWindow 的可缩放窗口外壳 + 一个自绘画面控件
 *  (RdViewerCanvas)，建立传输（RdTransport：服务端建了 P2P 会话时优先走 P2P 链路，
 *  否则 / 失败时连 imgserver 中转），把收到的屏幕帧喂给 RdScreenDecoder 解码，并在
 *  画面控件里按窗口大小缩放绘制(StretchDIBits)。
 *
 *  线程模型：P2P 方案第 6 期起，连接在本窗口自己的连接线程上进行（等 P2P 链路最多 20 秒，
 *  不能占着 UI 线程），结果经 WM_RD_VIEWER_TRANSPORT 交回 UI 线程。P2P 帧通道的转交线程
 *  或中转接收线程通过 RdRelayClientHandler::OnRelayEngineFrame 把屏幕帧回调上来——本类
 *  把该帧拷到堆上并 PostMessage 到 UI 线程，由 UI 线程独占地 apply 解码 + 触发画面控件
 *  重绘，故 RdScreenDecoder 无需加锁。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp
 *  里先 #include "stdafx.h"）。
 *
 *  关窗或浮动工具条「断开」时，经 SetActions 登记的 remotedesktop::IRdSessionActions 交给远程桌面控制器
 *  发 cmd_rd_close（2026-10-02 迁入 FlamingoMedia.dll 之前直接调用 CIUSocket）。
 *
 *  2026-10-12 起（docs/远程桌面控制权限与观看工具条方案.md 第 4.5 节）：接通后默认只能查看，在浮动工具条上点「申请控制」、
 *  被控端同意后才发送键鼠（状态见 RdViewerControlState）。原来标题栏上的「全屏」图标与全屏时的 CRdViewerToolbar 都换成
 *  一条与屏幕共享同样式的浮动工具条（CViewerFloatBar）：左侧状态文字「仅查看 / 申请中 / 控制中」，按钮「申请控制」「全屏」
 *  「断开」「隐藏」「收起」，窗口模式与全屏共用。
 *
 *  2026-10-15 起（docs/远程桌面H264与共享画质提升方案.md 4.7）：画面有旧格式（RdScreenDecoder，UI 线程解码）与 H.264
 *  （RdVideoPicture，自带解码线程）两种，画布只认 IRdPicture，按 RdPictureSelector 选最近就绪的一种。连接线程先试建解码器，
 *  连上后发带能力位的关键帧请求；4 号视频帧在接收线程上直接交给 RdVideoPicture，不经 UI 线程；5 号指针帧、6 号能力帧与
 *  旧格式的帧一样投到 UI 线程。收 H.264 期间每 500 毫秒回执一次（RdAckEchoMs）。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include "RdTransport.h"        // winsock2.h + RdRelayClientHandler + RdTransport / RdTransportParams
#include "RdScreenDecoder.h"     // RdScreenDecoder（含 windows.h）
#include "RdVideoPicture.h"      // RdVideoPicture：H.264 画面（2026-10-15）
#include "RdVideoNegotiation.h"  // RdCapsResendPacer / RdKeyRequestLimiter：重发能力请求与关键帧请求的限频（2026-10-15）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../Share/ViewerFloatBar.h"               // CViewerFloatBar：浮动工具条（2026-10-12）
#include "RdViewerControl.h"                        // RdViewerControlState：控制权限状态（2026-10-12）
#include "RdViewFit.h"                              // ComputeRdFitRect / RdMouseMapper：等比显示与坐标换算（2026-10-12）
#include "CursorInfo.h"                             // CCursorInfo：指针类型索引对应的系统指针（画远端指针，2026-10-12）
#include "RemoteDesktopProtocolClient.h"            // net::RDControlResponse / RDControlNotify
#include "RdSessionActions.h"                       // remotedesktop::IRdSessionActions（结束会话的接收方）
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

//自绘画面控件：把当前解码画面按宽高比放进本控件矩形、居中绘制，四周黑边（2026-10-12 前拉伸铺满）；未就绪时填黑底。
class RdViewerCanvas : public balloonwjui::DuiControl
{
public:
    RdViewerCanvas();

    /**
     *  设置要画的画面（2026-10-15 起由 SetDecoder 改为画面接口）：观看窗持有旧格式与 H.264 两种画面，按最近就绪的一种设置。
     *  @param picture 画面（不拥有；生命周期由观看窗保证，长于本控件）；NULL 表示还没有画面、画黑底
     */
    void SetPicture(const remotedesktop::IRdPicture* picture) { m_picture = picture; }

    /**
     *  设置传输的链路实际路径（2026-10-04 起）：直连时在画面左上角叠一枚「P2P」徽章。有变化才重绘。
     *  @param linkPath 取值同 RdTransport::LinkPath（0 没走 P2P、1 局域网直连、2 打洞直连、3 中转）
     */
    void SetLinkPath(int linkPath);

    //画面实际所占的矩形（宿主客户区坐标，等比放进本控件后去掉黑边）；还没有画面时为本控件矩形
    RECT PictureRect() const;

    //自绘：就绪则 StretchDIBits 等比缩放画面、四周黑边，再画出远端指针（2026-10-12）；否则黑底。
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    //在画面上画出被控端的指针：位置与类型取自画面（画面本身不含指针），指针在别的显示器上时不画
    void drawRemoteCursor(HDC hdc, const RECT& picture);

    CCursorInfo                       m_cursors;  //指针类型索引到系统指针的对照（与被控端 RdScreenCapturer 用的是同一张表）
    const remotedesktop::IRdPicture*  m_picture;  //当前画面（不拥有）；NULL 表示还没有画面
    int                             m_linkPath; //传输的链路实际路径（见 SetLinkPath），观看窗每秒刷新一次
};

//远程桌面观看窗浮动工具条上的按钮编号（点击时作为 WM_RD_VIEWER_BAR_CMD 的 wParam）
enum RdViewerBarButtonId
{
    kRdViewBarControl    = screenshare::kViewerBarUserIdFirst,       //申请控制 / 取消申请 / 结束控制
    kRdViewBarFullscreen = screenshare::kViewerBarUserIdFirst + 1,   //全屏 / 退出全屏
    kRdViewBarDisconnect = screenshare::kViewerBarUserIdFirst + 2,   //断开：发 cmd_rd_close 并关窗
};

//控制端观看窗：balloonui 窗口外壳 + 自绘画面控件，连中转收屏幕帧解码显示。
class CRdViewerWindow : public balloonwjui::DuiFrameWindow,
                        public remotedesktop::RdRelayClientHandler
{
public:
    CRdViewerWindow();
    ~CRdViewerWindow();

    /**
     *  启动观看：建窗并显示，起连接线程建立传输，连上后请求关键帧、异步收帧显示。不阻塞。
     *  连接失败时提示用户、经 IRdSessionActions 结束会话并关窗。
     *  @param params    传输参数（拷贝保存）：中转参数与可选的 P2P 链路槽；m_sessionId 是会话 id（关窗时发 cmd_rd_close 用）
     *  @return 建窗并起连接线程返回 true；建窗失败返回 false
     */
    bool Start(const remotedesktop::RdTransportParams& params);

    //关闭观看：停连接线程（还在连中转时最多等连接超时 5 秒）、断开传输、关窗。幂等。
    void CloseViewer();

    /**
     *  登记结束会话的接收方：用户关窗或点浮动工具条「断开」时经它发 cmd_rd_close，点「申请控制」等按钮时经它发 1718。
     *  @param actions 接收方（远程桌面控制器，不拥有，须在本对象销毁或再次登记之前一直有效）；传 NULL 解除登记，
     *                 此后关窗只断开中转、不发信令
     */
    void SetActions(remotedesktop::IRdSessionActions* actions) { m_actions = actions; }

    // ==== RdRelayClientHandler（中转接收线程触发）====

    //收到一帧屏幕引擎帧：4 号视频帧直接交给 H.264 解码线程（2026-10-15）；其余拷到堆并 PostMessage 到 UI 线程处理。
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override;

    //中转断开：PostMessage 到 UI 线程提示并关窗。
    virtual void OnRelayClosed() override;

    /**
     *  本端 1718 的应答（2026-10-12，远程桌面控制器转交）：失败时按服务端的状态纠正浮动工具条。会话 id 不符时忽略。
     *  @param resp 解析后的应答
     */
    void OnControlResponse(const net::RDControlResponse& resp);

    /**
     *  1719 推送（2026-10-12，远程桌面控制器转交）：被控端同意、拒绝或收回控制。会话 id 不符时忽略。
     *  @param notify 解析后的推送
     */
    void OnControlNotify(const net::RDControlNotify& notify);

    //浮动工具条的窗口（单元测试用）；没有显示时为 NULL
    HWND BarWindowForTest() const;

    /**
     *  取浮动工具条上某个按钮的屏幕矩形（单元测试用）。
     *  @param id  按钮编号，取值见 RdViewerBarButtonId 或 CViewerFloatBar::kIdHide / kIdToggle
     *  @param out [出参] 屏幕坐标
     *  @return 工具条可见且有这个按钮时返回 true
     */
    bool BarButtonRectForTest(UINT id, RECT& out) const;

    //浮动工具条当前的状态文字（单元测试用）
    std::wstring BarStatusForTest() const;

    //当前是否会把键鼠发给被控端（单元测试用）：画面就绪且已获授权
    bool IsInputAllowedForTest() const;

    /**
     *  只建窗、不连传输（单元测试用）：窗口、画面控件与浮动工具条与 Start 相同。
     *  @param sessionId 会话 id
     *  @return 建窗成功返回 true
     */
    bool OpenForTest(const std::string& sessionId);

    //画布当前用的画面（单元测试用），取值见 remotedesktop::rd_picture_kind
    int ActivePictureForTest() const;

    //当前画面的源尺寸（单元测试用）；还没有画面时为 0×0
    SIZE PictureSourceSizeForTest() const;

    //画面在客户区里实际所占的矩形（单元测试用，扣除黑边）
    RECT PictureRectForTest() const;

    /**
     *  把客户区里的一次鼠标移动换算成被控端屏幕坐标（单元测试用，与发送键鼠时是同一套换算）。
     *  @param x / y            客户区坐标
     *  @param remoteX / remoteY [出参] 被控端屏幕坐标
     *  @return 落在画面内返回 true；落在黑边上或还没有画面返回 false
     */
    bool MapMouseMoveForTest(int x, int y, int& remoteX, int& remoteY);

    //H.264 画面（单元测试用：直接设一帧、读指针）
    remotedesktop::RdVideoPicture& VideoPictureForTest() { return m_video; }

    /**
     *  设本机的 H.264 解码能力（单元测试用，代替连接线程里的试建解码器）。
     *  @param canDecode   能否解 H.264
     *  @param maxLongSide 能解的最大长边（像素）
     */
    void SetDecodeCapsForTest(bool canDecode, int maxLongSide);

    //本窗口累计发出的关键帧请求数（单元测试用；没有连上时发送失败，照样计数）
    int KeyRequestsForTest() const { return m_keyRequests; }

    //本窗口私有消息（WM_APP 起，应用私有、不与系统 / balloonui 冲突）。
    enum
    {
        WM_RD_VIEWER_FRAME       = WM_APP + 1,   //收到屏幕帧（lParam = 堆 std::vector<unsigned char>*）
        WM_RD_VIEWER_CLOSED      = WM_APP + 2,   //中转断开
        WM_RD_VIEWER_BAR_CMD     = WM_APP + 3,   //浮动工具条按钮命令（wParam = RdViewerBarButtonId，2026-10-12 前为全屏工具条的命令）
        WM_RD_VIEWER_TRANSPORT   = WM_APP + 0xB1, //连接线程的结果（wParam = 本次连接的序号，lParam = 1 连上 / 0 失败），
                                                  //P2P 方案第 6 期新增，偏移登记在开发约定文档「UI 约定」第 6 条
        WM_RD_VIEWER_PICTURE     = WM_APP + 0xB4, //H.264 解码线程解出了新画面（2026-10-15），偏移同样登记在第 6 条
        WM_RD_VIEWER_KEYREQ      = WM_APP + 0xB5, //H.264 收帧队列溢出或解码器建不起来，需要请求关键帧（2026-10-15）
    };

    BEGIN_MSG_MAP(CRdViewerWindow)
        MESSAGE_HANDLER(WM_RD_VIEWER_FRAME, OnRdFrameMsg)
        MESSAGE_HANDLER(WM_RD_VIEWER_CLOSED, OnRdClosedMsg)
        MESSAGE_HANDLER(WM_RD_VIEWER_TRANSPORT, OnRdTransportMsg)
        MESSAGE_HANDLER(WM_RD_VIEWER_PICTURE, OnRdPictureMsg)
        MESSAGE_HANDLER(WM_RD_VIEWER_KEYREQ, OnRdKeyReqMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOVE, OnMoveSizeMsg)
        MESSAGE_HANDLER(WM_SIZE, OnMoveSizeMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_KEYUP, OnKeyUp)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseInput)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnMouseInput)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnMouseInput)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnMouseInput)
        MESSAGE_HANDLER(WM_RBUTTONDOWN, OnMouseInput)
        MESSAGE_HANDLER(WM_RBUTTONUP, OnMouseInput)
        MESSAGE_HANDLER(WM_MBUTTONDOWN, OnMouseInput)
        MESSAGE_HANDLER(WM_MBUTTONUP, OnMouseInput)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseInput)
        MESSAGE_HANDLER(WM_RD_VIEWER_BAR_CMD, OnBarCmd)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //UI 线程：取出堆帧 → 解码 apply → 变化则触发画面控件重绘。
    LRESULT OnRdFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：中转断开提示并关窗。
    LRESULT OnRdClosedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：连接线程的结果。连上则请求关键帧、开帧率与回执定时器；失败则提示、结束会话并关窗。
    LRESULT OnRdTransportMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：H.264 解出了新画面，画布换到 H.264 画面（第一次时写日志）并重画
    LRESULT OnRdPictureMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：H.264 需要关键帧，经限频后请求
    LRESULT OnRdKeyReqMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //用户点标题栏关闭：发 cmd_rd_close 结束会话并关窗。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //每秒定时：据本秒帧数 / 最近帧大小刷新标题栏的帧率与帧大小。
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //窗口移动或改变大小：浮动工具条跟随（最小化时随所有者隐藏，不挪）
    LRESULT OnMoveSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //键盘按下：F11 切换全屏 / ESC 退出全屏，其余键编码成键盘命令发给被控端。
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //键盘抬起：编码成键盘命令发给被控端（全屏快捷键不转发）。
    LRESULT OnKeyUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //画面区鼠标（移动 / 左右中键 / 双击 / 滚轮）：已获授权时映射成被控端屏幕坐标并编码发出；
    //旁路不拦截，标题栏按钮 / DuiHost 照常工作。
    LRESULT OnMouseInput(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //浮动工具条按钮命令（wParam = RdViewerBarButtonId）：申请 / 撤回 / 结束控制、切换全屏、断开。
    LRESULT OnBarCmd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //切换全屏，浮动工具条随之换位置与「全屏 / 退出全屏」图标
    void toggleFullscreen();
    //浮动工具条的参照矩形：全屏时为显示器矩形，窗口模式时为标题栏以下的画面区域（屏幕坐标）
    RECT barAnchor() const;
    //按控制状态、提示与全屏状态刷新浮动工具条的状态文字与按钮
    void refreshBar();
    //执行控制权限的一组动作：发 1718、记下提示、刷新浮动工具条
    void applyControlEffects(const remotedesktop::RdViewerEffects& effects);
    //是否把键鼠发给被控端：画面就绪且已获授权
    bool canSendInput() const;
    //当前选用的画面（2026-10-15）；还没有画面时为 NULL
    const remotedesktop::IRdPicture* activePicture() const;
    //按 m_selector 把画布换到对应的画面并重画
    void applySelectedPicture();
    /**
     *  向被控端发带能力位的关键帧请求（2026-10-15），计数、登记到 m_capsPacer 并写日志。
     *  @param reason 日志里写的原因（ASCII）
     */
    void sendKeyframeRequest(const char* reason);
    /**
     *  处理 H.264 格式的附属帧：5 号指针帧交给 m_video，6 号能力帧决定要不要重发请求（UI 线程）。
     *  @param frame 引擎帧（非空）
     *  @return 是这两种帧（已处理）返回 true；其余返回 false，由调用方交给旧格式解码器
     */
    bool handleVideoSideFrame(const std::vector<unsigned char>& frame);
    //回执定时器（每 500 毫秒）：收 H.264 期间回执一次；收帧队列在等关键帧而请求曾被限频挡下时补发
    void onAckTimer();
    //建窗、建画面控件、显示窗口与浮动工具条（Start 与 OpenForTest 共用）
    bool openWindow(const std::string& sessionId);

    //连接线程主体：建立传输，把结果投回 UI 线程。
    void ConnectProc(unsigned int seq);

    //停连接线程并断开传输（CloseViewer 与析构共用）
    void StopTransport();

    //连上之后还没有画面时写一条诊断日志：收到的帧数（按类型）、字节数、路径与 P2P 链路统计。
    //@param now 当前时刻（GetTickCount64，毫秒）
    void LogNoPicture(ULONGLONG now);

    //确保窗口已创建（首次调用 Create + 设外观）。
    void EnsureWindow();
    //构建客户区：一个填满的自绘画面控件。
    void BuildUi();

private:
    remotedesktop::RdTransport     m_transport;   //传输（控制端这条用于收屏幕帧、发控制命令）
    remotedesktop::RdTransportParams m_params;    //本次会话的传输参数（Start 时拷贝，连接线程使用）
    std::thread                    m_connectThread;   //连接线程；连接结束（成功或失败）后退出，CloseViewer 时回收
    std::atomic<bool>              m_connectCancel;   //取消连接的标志（CloseViewer 置位，连接线程等链路时检查）
    unsigned int                   m_connectSeq;      //连接的序号，每次 Start 加 1；结果消息带着它，迟到的结果丢弃
    remotedesktop::RdScreenDecoder m_decoder;     //旧格式的画面解码器（仅 UI 线程访问，无需加锁）
    remotedesktop::RdVideoPicture  m_video;       //H.264 画面（2026-10-15）：自带解码线程，openWindow 时启动，CloseViewer 时在断开传输之后停止
    remotedesktop::RdPictureSelector m_selector;  //画布用哪一种画面（UI 线程）：最近就绪的那一种
    remotedesktop::RdCapsResendPacer m_capsPacer; //收到能力帧时要不要重发带能力位的请求（UI 线程；连上时按试建结果重建）
    remotedesktop::RdKeyRequestLimiter m_keyLimiter; //H.264 需要关键帧时的请求限频（UI 线程，每秒最多一次）
    std::atomic<bool>              m_canDecodeH264;     //本机能否解 H.264（连接线程试建解码器后写入，连上之后 UI 线程读）
    std::atomic<int>               m_decodeMaxLongSide; //本机能解的最大长边（像素，同上）；不能解时为 0
    int                            m_keyRequests;       //本窗口累计发出的关键帧请求数（日志与单元测试用，UI 线程）
    std::atomic<int>               m_videoFramesSinceTick; //本统计周期（1 秒）内收到的 4 号视频帧数（接收线程加，帧率定时器读后清零）
    std::atomic<unsigned>          m_videoLastBytes;    //最近一个 4 号视频帧的字节数（接收线程写，标题栏显示帧大小用）
    std::atomic<int>               m_videoFramesTotal;  //连上以来收到的 4 号视频帧数（诊断日志用，连上时清零）
    RdViewerCanvas*                m_canvas;      //自绘画面控件（SetClientContent 后由控件树持有，本指针仅引用）
    std::string                    m_sessionId;   //会话 id（关窗时发 cmd_rd_close 用）
    int                            m_framesSinceTick;  //本统计周期（1 秒）内收到的帧数，定时器读后清零
    size_t                         m_lastFrameBytes;   //最近一帧引擎帧字节数（标题栏显示帧大小用）
    //以下为诊断统计，只在 UI 线程上访问，每次连上时清零；首帧就绪与「迟迟没有画面」的日志用它们
    int                            m_framesTotal;      //连上以来收到的引擎帧总数
    int                            m_bitmapInfoFrames; //其中 BITMAPINFO 帧的个数
    int                            m_firstFrames;      //其中首屏全量帧的个数
    unsigned long long             m_bytesTotal;       //连上以来收到的引擎帧总字节数
    ULONGLONG                      m_connectedTick;    //连上的时刻（GetTickCount64，毫秒）；0 表示还没连上
    ULONGLONG                      m_lastNoPictureLogTick; //上一次写「还没有画面」日志的时刻（GetTickCount64，毫秒）
    screenshare::CViewerFloatBar   m_bar;              //浮动工具条（2026-10-12，窗口模式与全屏共用；以本窗口为所有者）
    remotedesktop::RdViewerControlState m_control;     //控制权限状态（2026-10-12）：每次 Start 从未授权开始
    remotedesktop::RdMouseMapper   m_mouse;            //鼠标坐标换算（2026-10-12）：扣除黑边，记住在画面内按下的按钮
    std::wstring                   m_notice;           //浮动工具条状态区临时显示的提示（如「对方拒绝了你的控制申请」）；为空时显示控制状态
    ULONGLONG                      m_noticeUntil;      //提示显示到什么时刻（GetTickCount64，毫秒）；过时后由帧率定时器恢复状态文字
    remotedesktop::IRdSessionActions* m_actions;       //结束会话的接收方（远程桌面控制器，由 SetActions 登记，不拥有）；为 NULL 时不发信令
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};
