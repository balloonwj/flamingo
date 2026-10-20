/**
 *  远程桌面被控端推流器，RdControlledStreamer.h
 *
 *  被控端在收到中转参数(cmd_rd_relay_info, role=被控端)后用本类推流：起一条抓屏线程，
 *  它先建立传输（RdTransport：服务端建了 P2P 会话时优先走 P2P 链路，否则 / 失败时连
 *  imgserver 中转；P2P 方案第 6 期起连接挪到本线程里做，等链路最多 20 秒不能占着 UI 线程），
 *  连上后先发一次屏幕 BITMAPINFO 帧与首屏全量帧让控制端解码器就绪，之后按固定周期抓
 *  增量帧推给控制端。连接失败时向 Start 给的通知窗口投 kMsgRdStreamerConnectFailed。抓屏线程内把本线程切到 Per-Monitor-V2
 *  DPI 上下文，使 GetSystemMetrics / BitBlt 工作在物理像素（Flamingo 是 DPI-unaware
 *  进程，否则抓到的是被 DWM 拉伸的逻辑像素、画面模糊放大）。
 *
 *  本类同时作为传输的事件接收者：控制端发来的鼠标键盘命令先经 RdControlGate 判定（2026-10-12 起
 *  接通后默认不允许控制，被控者同意或直接允许后才注入），通过的才注入本机；连接断开时标记
 *  停止、由协调器据 state_notify 收尾会话。一次被控会话
 *  持有一个本类实例。
 *
 *  一期固定 16 位色 + 差异(DIFF)算法、抓主显示器、5 帧/秒。
 *
 *  2026-10-15 起（docs/远程桌面H264与共享画质提升方案.md 4.4～4.6）另有 H.264 格式：开始推流后等控制端的关键帧请求（最多 2 秒），
 *  请求里带「能解 H.264」且本机能建起编码器时改发 4 号视频帧（屏幕共享的 ShareVideoEncoder，远程桌面参数：长边 2560、30 fps、
 *  1080p 基准 3 Mbps），指针另以 5 号帧发出（最多每秒约 30 次，每个关键帧之后补发一次）；否则照旧发 16 位 DIB。
 *  每个关键帧之前（不论新旧格式）先发 6 号能力帧，促使最初的请求被中转丢掉的新控制端重发带能力位的请求。
 *  码率自适应在 send 阻塞与关键帧请求之外，另用控制端的回执算出的延迟（一对一中转对控制端下行没有积压保护）。
 *  编码器在会话中途彻底失败时退回旧格式。选格式的规则见 RdVideoNegotiation.h。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <winsock2.h>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "RdTransport.h"   //含 RdRelayClient.h（winsock2.h）与 P2PLinkBroker.h
#include "RdControlGate.h" //控制把关（2026-10-12）
#include "RdVideoFrames.h" //RdKeyframeRequestInfo：控制端请求里的能力位（2026-10-15）
#include "RdVideoNegotiation.h"            //RdFormatChooser：选新旧格式（2026-10-15）
#include "../Share/ShareVideoEncoder.h"     //H.264 编码（远程桌面参数，2026-10-15）
#include "../Share/ShareBitrateController.h" //码率自适应（2026-10-15）

namespace remotedesktop {

class RdScreenCapturer;

//被控端推流器：连中转后周期抓屏推流，并接收控制端命令。
class RdControlledStreamer : public RdRelayClientHandler, private screenshare::ShareVideoEncoderSink
{
public:
    RdControlledStreamer();
    ~RdControlledStreamer();

    RdControlledStreamer(const RdControlledStreamer&) = delete;
    RdControlledStreamer& operator=(const RdControlledStreamer&) = delete;

    /**
     *  启动推流：起抓屏线程，由它先建立传输、连上后推流。不阻塞。
     *  @param params    传输参数（拷贝保存）：中转参数与可选的 P2P 链路槽
     *  @param notifyWnd 连接失败时接收 kMsgRdStreamerConnectFailed 的窗口（远程桌面控制器的消息窗口，借用）；
     *                   为 NULL 时不通知
     *  @return 已起推流线程返回 true；已在运行时直接返回 true
     */
    bool Start(const RdTransportParams& params, HWND notifyWnd);

    //停止推流：停抓屏线程、关传输。幂等。线程还在连接中转时最多等连接超时（5 秒）才返回。
    void Stop();

    //是否处于推流运行态（推流线程已起、尚未结束或断开）。
    bool IsRunning() const;

    //最近一次 Start 的序号：连接失败通知的 wParam 带着它，接收方据此丢弃上一次会话迟到的通知
    unsigned int StartSeq() const { return m_startSeq; }

    //当前传输走的路径（RdTransport::rd_transport_path），日志与测试用
    int TransportPath() const { return m_transport.Path(); }

    //传输的链路实际路径（2026-10-04 起，供受控提示条上的「P2P」徽章）；取值同 RdTransport::LinkPath，可在任意线程调用
    int LinkPath() const { return m_transport.LinkPath(); }

    /**
     *  打开或关闭控制（2026-10-12 起接通后默认关闭，只能查看）。在 UI 线程上调用。
     *  @param allowed true：此后控制端发来的键鼠输入按 RdControlGate 的规则注入；
     *                 false：此后一律丢弃，并对控制者已按下、尚未松开的键与鼠标按钮补发一次松开
     */
    void SetControlAllowed(bool allowed);

    // ==== RdRelayClientHandler（P2P 帧通道的转交线程或中转的接收线程上触发）====

    //控制端发来的帧：请求关键帧，或鼠标键盘命令（注入本机）。
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override;

    //连接断开（对端关闭 / 网络错误）：标记停止，UI 侧收尾由协调器据信令处理。
    virtual void OnRelayClosed() override;

private:
    //推流循环的状态（只在推流线程上使用；2026-10-15 起两种格式的推流拆成两个函数，共用这份状态）
    struct PushState
    {
        bool               m_periodicKeyframes;   //旧格式是否定时重发关键帧（走中转时；P2P 帧通道可靠有序，不重发）
        int                m_framesSinceKeyframe; //旧格式：上次关键帧之后的增量帧数
        bool               m_needKeyframe;        //要发关键帧（首轮、控制端请求、换了格式）
        int                m_sentW;               //上一次关键帧的画面宽（只用于日志）
        int                m_sentH;               //上一次关键帧的画面高
        int                m_framesSent;          //已发的引擎帧数
        int                m_keyframesSent;       //其中关键帧的轮数
        unsigned long long m_bytesSent;           //已发的字节数
        bool               m_sendFailed;          //发送失败（传输断了），推流循环应当退出
        int64_t            m_startMs;             //推流开始的时刻（毫秒），H.264 时间戳的起点
        bool               m_firstVideoSent;      //是否发过 H.264 视频帧
        uint32_t           m_firstVideoMs;        //第一个视频帧的发送时刻（低 32 位毫秒），更早的回执是上一会话的残留
        POINT              m_cursorPos;           //上一次发出的指针位置（H.264 时）
        BYTE               m_cursorType;          //上一次发出的指针类型
        bool               m_cursorSent;          //是否发过指针帧
        int                m_srcW;                //当前编码这一帧的源宽（视频帧头用）
        int                m_srcH;                //当前编码这一帧的源高
        bool               m_canEncode;           //本机能否编 H.264（推流开始时试建一次）；能编时每个关键帧之前发能力帧
    };

    //抓屏线程主体：先建立传输，连上后进入 PushLoop；连接失败时通知 m_notifyWnd。
    void PushProc();

    //推流循环（传输已连上之后）：切 PMv2 → 建抓屏引擎 → 按选定的格式推流（2026-10-15 起两种格式）。
    void PushLoop();

    //旧格式的一轮：抓一帧，发 BITMAPINFO+首屏或增量帧（关键帧之前先发能力帧）。返回 false 表示发送失败
    bool pushLegacyRound(RdScreenCapturer& capturer, PushState& st);

    /**
     *  H.264 的一轮：抓一帧，按发帧节奏编码发出视频帧，指针变了发指针帧。
     *  @param capturer 抓屏引擎
     *  @param st       推流状态
     *  @param hasFrame [出参] 手里是否有画面（这一轮或之前抓到过；2026-10-17，推流循环据它补足每轮时长）
     *  @param newFrame [出参] 这一轮是否抓到了新画面
     *  @param cursorMoved [出参] 这一轮指针的位置或类型是否变了（变了就发了指针帧；2026-10-17，推流循环据它缩短这一轮的时长）
     *  @return false 表示发送失败或编码器彻底失败（st.m_sendFailed 区分两者）
     */
    bool pushVideoRound(RdScreenCapturer& capturer, PushState& st, bool& hasFrame, bool& newFrame, bool& cursorMoved);

    //发一个引擎帧并计入统计；失败时置 m_sendFailed
    bool sendFrame(const std::vector<unsigned char>& frame, PushState& st);

    //发能力帧（本机能编 H.264 时，每个关键帧之前）
    bool sendCaps(PushState& st);

    //ShareVideoEncoderSink：编好一个访问单元，组 4 号视频帧发出（关键帧之前发能力帧、之后补发指针帧）
    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs) override;

    //取走控制端最近一条关键帧请求（接收线程存、推流线程取）；没有时返回 false
    bool takeKeyframeRequest(RdKeyframeRequestInfo& info);

private:
    RdTransport                  m_transport;    //传输（被控端这条用于发屏幕帧、收控制命令）
    RdTransportParams            m_params;       //本次会话的传输参数（Start 时拷贝，推流线程使用）
    HWND                         m_notifyWnd;    //连接失败的通知窗口（借用）；可为 NULL
    std::unique_ptr<std::thread> m_pushThread;   //抓屏推流线程
    std::atomic<bool>            m_running;       //是否处于推流运行态
    std::atomic<bool>            m_stopping;      //是否正在停止（推流线程据此放弃连接、退出循环）
    std::atomic<bool>            m_keyframeRequested;  //控制端请求重发关键帧的标志（接收线程置，推流线程取并清）
    std::mutex                   m_connectMutex;  //保护 m_transportReady：Stop 据它决定能否在推流线程运行时关闭传输
    bool                         m_transportReady; //传输是否已连上（推流线程在 Connect 成功后置位）；连接期间不能关闭传输
    std::atomic<unsigned int>    m_startSeq;      //Start 的序号，从 1 起每次加 1
    RdControlGate                m_controlGate;   //控制把关：未授权时丢弃键鼠输入，收回时给出补发的松开；每次 Start 复位为关闭
    std::atomic<unsigned int>    m_droppedInputs; //本次会话被把关丢弃的键鼠输入条数，只用于日志（接收线程累加，Stop 时打出）
    std::mutex                   m_requestMutex;  //保护 m_request（2026-10-15）
    RdKeyframeRequestInfo        m_request;       //控制端最近一条关键帧请求里的能力（接收线程写，推流线程取）
    std::atomic<bool>            m_ackPending;    //收到了还没处理的回执（接收线程置，推流线程取并清）
    std::atomic<uint32_t>        m_ackEchoMs;     //最近一条回执回送的发送时刻（低 32 位毫秒）
    //以下只在推流线程上使用（2026-10-15）
    RdScreenCapturer*            m_capturer;      //当前推流循环的抓屏器（PushLoop 期间有效，不拥有）
    PushState*                   m_state;         //当前推流循环的状态（PushLoop 期间有效，不拥有）
    screenshare::ShareVideoEncoder      m_videoEncoder;  //H.264 编码器（远程桌面参数）
    screenshare::ShareBitrateController m_bitrate;       //H.264 的码率自适应（每次进入推流循环时复位）
    screenshare::ShareFramePacer        m_pacer;         //H.264 的发帧节奏：30 fps 上限、4 秒兜底关键帧（每次进入推流循环时复位）
};

} // namespace remotedesktop
