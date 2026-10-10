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
 *  关窗或全屏工具条「断开」时，经 SetActions 登记的 remotedesktop::IRdSessionActions 交给远程桌面控制器
 *  发 cmd_rd_close（2026-10-02 迁入 FlamingoMedia.dll 之前直接调用 CIUSocket）。
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
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify（全屏按钮点击）
#include "RdViewerToolbar.h"                        // CRdViewerToolbar（m_toolbar 成员需完整类型）
#include "RdSessionActions.h"                       // remotedesktop::IRdSessionActions（结束会话的接收方）
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

//自绘画面控件：把当前解码画面按本控件矩形缩放绘制；未就绪时填黑底。
class RdViewerCanvas : public balloonwjui::DuiControl
{
public:
    RdViewerCanvas();

    //设置画面解码器（不拥有；生命周期由观看窗保证，长于本控件）。
    void SetDecoder(remotedesktop::RdScreenDecoder* decoder) { m_decoder = decoder; }

    /**
     *  设置传输的链路实际路径（2026-10-04 起）：直连时在画面左上角叠一枚「P2P」徽章。有变化才重绘。
     *  @param linkPath 取值同 RdTransport::LinkPath（0 没走 P2P、1 局域网直连、2 打洞直连、3 中转）
     */
    void SetLinkPath(int linkPath);

    //自绘：就绪则 StretchDIBits 缩放画面，否则黑底。
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    remotedesktop::RdScreenDecoder* m_decoder;  //画面解码器（不拥有）
    int                             m_linkPath; //传输的链路实际路径（见 SetLinkPath），观看窗每秒刷新一次
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
     *  登记结束会话的接收方：用户关窗或点全屏工具条「断开」时经它发 cmd_rd_close。
     *  @param actions 接收方（远程桌面控制器，不拥有，须在本对象销毁或再次登记之前一直有效）；传 NULL 解除登记，
     *                 此后关窗只断开中转、不发信令
     */
    void SetActions(remotedesktop::IRdSessionActions* actions) { m_actions = actions; }

    // ==== RdRelayClientHandler（中转接收线程触发）====

    //收到一帧屏幕引擎帧：拷到堆并 PostMessage 到 UI 线程解码 / 重绘。
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override;

    //中转断开：PostMessage 到 UI 线程提示并关窗。
    virtual void OnRelayClosed() override;

    //本窗口私有消息（WM_APP 起，应用私有、不与系统 / balloonui 冲突）。
    enum
    {
        WM_RD_VIEWER_FRAME       = WM_APP + 1,   //收到屏幕帧（lParam = 堆 std::vector<unsigned char>*）
        WM_RD_VIEWER_CLOSED      = WM_APP + 2,   //中转断开
        WM_RD_VIEWER_TOOLBAR_CMD = WM_APP + 3,   //全屏工具条按钮命令（wParam = CRdViewerToolbar::ToolbarCmd）
        WM_RD_VIEWER_TRANSPORT   = WM_APP + 0xB1, //连接线程的结果（wParam = 本次连接的序号，lParam = 1 连上 / 0 失败），
                                                  //P2P 方案第 6 期新增，偏移登记在开发约定文档「UI 约定」第 6 条
    };

    BEGIN_MSG_MAP(CRdViewerWindow)
        MESSAGE_HANDLER(WM_RD_VIEWER_FRAME, OnRdFrameMsg)
        MESSAGE_HANDLER(WM_RD_VIEWER_CLOSED, OnRdClosedMsg)
        MESSAGE_HANDLER(WM_RD_VIEWER_TRANSPORT, OnRdTransportMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
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
        MESSAGE_HANDLER(WM_RD_VIEWER_TOOLBAR_CMD, OnToolbarCmd)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //UI 线程：取出堆帧 → 解码 apply → 变化则触发画面控件重绘。
    LRESULT OnRdFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：中转断开提示并关窗。
    LRESULT OnRdClosedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：连接线程的结果。连上则请求关键帧、开帧率定时器；失败则提示、结束会话并关窗。
    LRESULT OnRdTransportMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //用户点标题栏关闭：发 cmd_rd_close 结束会话并关窗。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //每秒定时：据本秒帧数 / 最近帧大小刷新标题栏的帧率与帧大小。
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //DUI 通知：标题栏全屏图标按钮点击（DUIFW_CAPTION_ICON_CLICK）→ 切全屏；其余交基类。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //键盘按下：F11 切换全屏 / ESC 退出全屏，其余键编码成键盘命令发给被控端。
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //键盘抬起：编码成键盘命令发给被控端（全屏快捷键不转发）。
    LRESULT OnKeyUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //画面区鼠标（移动 / 左右中键 / 双击 / 滚轮）：映射成被控端屏幕坐标并编码发出；
    //旁路不拦截，标题栏按钮 / DuiHost 照常工作。
    LRESULT OnMouseInput(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //全屏工具条按钮命令：退出全屏 / 断开（wParam = CRdViewerToolbar::ToolbarCmd）。
    LRESULT OnToolbarCmd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //切换全屏并同步全屏工具条：进全屏则在画面顶部显示工具条，出全屏则隐藏。
    void SyncFullscreenToolbar();

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
    remotedesktop::RdScreenDecoder m_decoder;     //画面解码器（仅 UI 线程访问，无需加锁）
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
    int                            m_fullscreenIconId; //标题栏全屏按钮的 caption icon id（AddCaptionIcon 返回）
    HBITMAP                        m_fullscreenIcon;   //全屏按钮图标位图（本类生成、析构释放）
    CRdViewerToolbar               m_toolbar;          //全屏态浮于画面顶部的工具条（状态 + 退出全屏 / 断开）
    remotedesktop::IRdSessionActions* m_actions;       //结束会话的接收方（远程桌面控制器，由 SetActions 登记，不拥有）；为 NULL 时不发信令
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};
