/**
 *  屏幕共享发起人一侧的推流管线，ShareHostStreamer.h
 *
 *  一条工作线程完成：连上 imgserver 房间中转（ShareRelayClient）→ 按共享内容建采集器（显示器 / 窗口）→ 按发帧节奏
 *  （ShareFramePacer）取帧 → 编成 H.264（ShareVideoEncoder）→ 加上真实宽高、按 64 KiB 分片（ShareFraming）→ 发给房间。
 *  另外处理：
 *    - 服务端发来的关键帧请求（新成员加入、慢观看者丢帧）：立即把最近一帧编成关键帧，画面没变也编；
 *    - 共享内容的状态（正常 / 最小化 / 暂停 / 窗口已关闭）：变化时、以及每次发关键帧时发一帧状态控制帧；
 *    - 切换共享内容、暂停与继续：可在任意线程调用，工作线程在下一轮生效。
 *  设计见 docs/屏幕共享与录制方案.md 5.5、5.6。
 *
 *  事件经 ShareHostStreamerListener 在工作线程上回调，监听方（共享控制器）负责转投 UI 线程。
 *
 *  P2P（docs/P2P通信方案.md 第 6 期）：单聊共享里服务端为观看者与发起人建了 P2P 会话时，控制器经 SetP2PViewer 把链路槽
 *  交给本类。工作线程每一轮检查链路，就绪就在通道 kShareP2PChannelId 上打开帧通道，并像收到服务端的关键帧请求那样立即
 *  出关键帧、通知控制器补发标注、让摄像头出关键帧（代替服务端在新成员报到时发的请求）。通道开着时，发往房间的包体
 *  （画面、声音、标注、控制）改经它发给这名观看者，不再经房间中转；视频按 ShareP2PVideoGate 的规则在链路积压时只发
 *  关键帧（代替服务端的慢观看者丢帧）。观看者经 P2P 发来的包体按他的座位号改写发送者座位后，与中转收到的同样处理。
 *  链路断开或观看者离开时回到经房间中转发送；房间中转连接始终保持。
 *  采集器可以由调用方注入（联调用例用合成画面），不注入时按 ShareSourceSpec 新建。
 *
 *  典型用法（共享控制器在 UI 线程上）：
 *      m_streamer.Start(relayInfo, serverPubKey, source, NULL, this);
 *      ...
 *      m_streamer.SwitchSource(newSource);
 *      m_streamer.Stop();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

//winsock2.h 必须在 windows.h 之前：后者会带进旧的 winsock.h，与房间中转客户端用的 winsock2.h 冲突
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ShareCapturer.h"
#include "ShareAudioTransport.h"
#include "ShareBitrateController.h"
#include "ShareFraming.h"
#include "ShareScreenDecoders.h"
#include "ShareScreenFeed.h"
#include "ShareCursorSampler.h"
#include "ShareMouseEffects.h"
#include "ShareProtocolClient.h"
#include "ShareRelayClient.h"
#include "ShareVideoEncoder.h"
#include "ShareP2PRoute.h"
#include "ShareViewerReceiver.h"   //kShareP2PChannelId 等 P2P 常量与 P2PLinkBroker.h（排在 winsock2.h 之后）

namespace screenshare {

//共享内容的种类
enum share_source_kind
{
    share_source_none    = 0,   //未指定
    share_source_monitor = 1,   //整块显示器
    share_source_window  = 2,   //单个窗口
};

//要共享的内容
struct ShareSourceSpec
{
    int  m_kind;          //种类，取值见 enum share_source_kind
    int  m_adapter;       //显示器：DXGI 显卡序号
    int  m_output;        //显示器：输出序号
    RECT m_monitorRect;   //显示器：在虚拟桌面上的矩形
    HWND m_hwnd;          //窗口：句柄

    ShareSourceSpec();
};

/**
 *  某个窗口所在的那块显示器，作为共享内容（第 23 期「屏幕当白板」：只共享一个窗口时换成它所在的整块屏幕）。
 *  @param hwnd 窗口；窗口跨几块显示器时取占面积最大的那块（MonitorFromWindow 的规则）
 *  @param out  [出参] 显示器共享源（种类为显示器，带 DXGI 序号与矩形）
 *  @return 找到返回 true；hwnd 不是窗口时返回 false
 */
bool MonitorSourceForWindow(HWND hwnd, ShareSourceSpec& out);

//推流管线的事件
enum share_host_event
{
    share_host_relay_connected = 1,   //已连上房间中转，开始推流
    share_host_relay_failed    = 2,   //连不上房间中转（报到被拒或网络不通），工作线程已退出
    share_host_relay_lost      = 3,   //推流途中房间中转断开，工作线程已退出
    share_host_capture_failed  = 4,   //采集器建不起来（系统不支持、显示器已拔掉等）
    share_host_source_state    = 5,   //共享内容的状态变了，value 取值见 screenshare_source_state
    share_host_key_requested   = 6,   //服务端请求画面关键帧（有新成员连上中转、或有观看者丢了帧），控制器据此补发标注
    share_host_remote_screen   = 7,   //解出了其他共享者屏幕画面的新一帧（第 22 期多人共享），value 为那一路的画面座位
};

//推流管线的事件回调；在工作线程上触发，实现方须自行转投 UI 线程，不可在回调里调用 Stop
class ShareHostStreamerListener
{
public:
    virtual ~ShareHostStreamerListener()
    {
    }

    /**
     *  @param event 事件，取值见 enum share_host_event
     *  @param value 附加值：share_host_source_state 时为新状态，其余为 0
     */
    virtual void OnHostStreamEvent(int event, int value) = 0;
};

//屏幕共享发起人一侧的推流管线
class ShareAudioSession;
class ShareCameraSender;
class ShareCameraReceiver;
class ShareAnnotationSink;
class ShareInputSink;
class ShareCaptionSink;

//屏幕共享发起人一侧的推流管线；同时是本人麦克风帧的发送通道（ShareAudioTransport）
class ShareHostStreamer : public ShareAudioTransport, public IShareScreenFeed, private ShareRelayClientHandler,
                          private ShareVideoEncoderSink, private ShareScreenDecodersListener
{
public:
    ShareHostStreamer();
    ~ShareHostStreamer();

    ShareHostStreamer(const ShareHostStreamer& rhs) = delete;
    ShareHostStreamer& operator=(const ShareHostStreamer& rhs) = delete;

    /**
     *  启动工作线程（连接中转、采集、编码、推流都在工作线程上做，本方法立即返回）。
     *  @param relay        1726 下发的中转参数
     *  @param serverPubKey 服务端 32 字节公钥；为 NULL 时中转不加密。会拷贝一份
     *  @param source       要共享的内容
     *  @param injected     调用方注入的采集器（转移所有权，联调用例用）；为 NULL 时按 source 新建
     *  @param listener     事件回调，不转移所有权，须在 Stop 返回之前一直有效；可为 NULL
     *  @return 已在运行或参数非法返回 false
     */
    bool Start(const ShareRelayInfo& relay, const unsigned char* serverPubKey, const ShareSourceSpec& source,
               IShareCapturer* injected, ShareHostStreamerListener* listener);

    //停止推流、断开中转并等待工作线程结束；可重复调用。不可在事件回调里调用
    void Stop();

    //是否在运行
    bool IsRunning() const;

    //切换共享内容；工作线程下一轮生效，生效后的第一帧是关键帧
    void SwitchSource(const ShareSourceSpec& source);

    //暂停 / 继续推新画面（暂停期间仍响应关键帧请求，重发暂停前的最后一帧）
    void SetPaused(bool paused);

    //要求下一帧编成关键帧（如本机刚改了画质）；可在任意线程调用
    void RequestKeyFrame();

    //当前被采集内容在虚拟桌面上的矩形（边框窗口据此跟随）；可在任意线程调用
    RECT CurrentSourceRect() const;

    //已发出的视频帧数与关键帧数（日志与测试用）
    uint64_t SentFrames() const;
    uint64_t SentKeyFrames() const;

    /**
     *  登记房间语音会话：接收线程上收到的声音帧交给它。可在 Start 前后调用；传 NULL 取消登记。
     *  @param audio 语音会话，不拥有；取消登记之前须一直有效
     */
    void SetAudioSession(ShareAudioSession* audio);

    /**
     *  登记摄像头的收发端：接收线程上收到的摄像头轨的帧交给 receiver，摄像头轨的关键帧请求交给 sender。
     *  传 NULL 取消登记。两者都不拥有，取消登记之前须一直有效。
     */
    void SetCameraEndpoints(ShareCameraSender* sender, ShareCameraReceiver* receiver);

    /**
     *  登记标注帧的接收方：接收线程上收到的标注轨帧交给它。传 NULL 取消登记。
     *  @param sink 接收方，不拥有，取消登记之前须一直有效
     */
    void SetAnnotationSink(ShareAnnotationSink* sink);

    /**
     *  登记字幕帧的接收方（语音字幕，docs/语音字幕方案.md 第 9 节）：接收线程上收到的字幕控制帧解析后交给它。传 NULL 取消登记。
     *  @param sink 接收方，不拥有，取消登记之前须一直有效
     */
    void SetCaptionSink(ShareCaptionSink* sink);

    /**
     *  登记键鼠输入帧的接收方（第 18 期控制键鼠）：接收线程上收到的输入控制帧解析后交给它。传 NULL 取消登记。
     *  @param sink 接收方，不拥有，取消登记之前须一直有效
     */
    void SetInputSink(ShareInputSink* sink);

    //录制旁路：打开后工作线程每编码一帧就留一份拷贝（第 16 期起是画上鼠标效果后的画面，与观看者看到的一致），
    //供共享录制取用（第 7 期 7d）；关闭时丢弃拷贝
    void SetRecordTapEnabled(bool on);

    /**
     *  换鼠标效果的设置（第 16 期，任意线程）：工作线程下一轮生效，画面随之更新。默认只开光圈。
     *  @param settings 新设置
     */
    void SetMouseEffects(const ShareMouseEffectSettings& settings);

    /**
     *  单聊共享：为一名观看者登记 P2P 链路槽（控制器在 1725 快照里看到这名观看者带 p2psessionid 时调用）。可在任意线程
     *  调用；工作线程在链路就绪时打开帧通道。已登记的另一名观看者被替换（单聊共享里同时只有一名观看者）。
     *  @param seat 观看者的座位号
     *  @param slot 链路槽（共享所有权）；为空时等同 ClearP2PViewer
     */
    void SetP2PViewer(int seat, const std::shared_ptr<balloonp2p::P2PLinkSlot>& slot);

    /**
     *  这名观看者离开了房间：关闭他的帧通道并以「业务结束」关闭链路，之后经房间中转发送。可在任意线程调用。
     *  @param seat 观看者的座位号；与登记的不同时不做事
     */
    void ClearP2PViewer(int seat);

    //当前是否经 P2P 帧通道向观看者发送（日志与测试用）
    bool IsP2PViewerOpen() const;

    /**
     *  经 P2P 帧通道向观看者发送时链路的实际路径（2026-10-04 起，供发起人控制条上的「P2P」徽章）。可在任意线程调用，不等待。
     *  @return 帧通道已打开时返回链路槽的路径（1 局域网直连、2 打洞直连、3 中转）；没有 P2P 观看者或还没打开时返回 0
     */
    int P2PViewerLinkPath() const;

    //当前的鼠标效果设置（任意线程）
    ShareMouseEffectSettings MouseEffects() const;

    /**
     *  取录制旁路留下的最新一帧（任意线程）。
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param serial [出参] 帧序号
     *  @return 已有画面返回 true
     */
    bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial);

    //IShareScreenFeed（第 22 期）：其他共享者的屏幕画面，主持人据此看别人的屏幕
    virtual bool GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                uint64_t& serial) const override;
    virtual std::vector<int> ScreenSeats() const override;
    virtual int ScreenSourceState(int seat) const override;

    //去掉某个共享者的画面（他停止共享或离开房间，第 22 期）
    void RemoveRemoteScreen(int seat);

    //ShareAudioTransport：经本推流管线的中转连接发出一个包体；未连接时返回 false。可在任意线程调用
    virtual bool SendRoomBody(const std::string& body) override;

private:
    void WorkerProc();
    IShareCapturer* createCapturer(const ShareSourceSpec& source);

    //一名 P2P 观看者：链路槽、帧通道与状态；它同时是帧通道的回调，定义在 .cpp
    class P2PViewerLink;

    //工作线程每一轮调用：链路就绪就打开帧通道（随即出关键帧、补发标注），等太久或链路失败就放弃，断开的收走
    void pollP2PViewer();

    //当前打开着的 P2P 观看者；没有时为空
    std::shared_ptr<P2PViewerLink> openP2PViewer() const;

    /**
     *  把一组包体发出去：有打开着的 P2P 观看者时经它的帧通道，否则经房间中转。
     *  @param bodies   包体（同一帧的各分片按序）
     *  @param isVideo  是否画面轨的视频帧（经 P2P 时按 ShareP2PVideoGate 决定是否丢弃）
     *  @param keyframe 是否关键帧
     *  @return 已发出（或按规则丢弃）返回 true；连接断开或发送失败返回 false
     */
    bool routeBodies(const std::vector<std::string>& bodies, bool isVideo, bool keyframe);

    /**
     *  处理一个房间包体（中转的接收线程与 P2P 帧通道的转交线程共用）。
     *  @param body        包体
     *  @param len         长度（字节）
     *  @param reassembler 这条来源的分片重组器
     */
    void handleRoomBody(const char* body, size_t len, ShareFrameReassembler& reassembler);
    //ShareRelayClientHandler：接收线程开始与退出（第 22 期起要在接收线程上解码其他共享者的画面，初始化与释放 COM、MF）
    virtual void OnRelayThreadStart() override;
    virtual void OnRelayThreadExit() override;
    //ShareScreenDecodersListener：解出其他共享者的一帧，通知控制器
    virtual void OnScreenDecoded(int seat) override;

    //P2P 观看者发来的一个包体（转交线程上）：按他的座位号改写发送者座位后处理
    void onP2PBody(int seat, const unsigned char* data, size_t len);
    void sendSourceState(int state);
    void copyLastFrame(const ShareCaptureFrame& f);
    void notify(int event, int value);

    //ShareRelayClientHandler：接收线程上收到的帧（关键帧请求）与断开
    virtual void OnRelayBody(const char* body, size_t len);
    virtual void OnRelayClosed();
    //ShareVideoEncoderSink：编好的 H.264 访问单元
    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs);

    ShareRelayInfo                 m_relay;            //中转参数
    unsigned char                  m_pubKey[32];       //服务端公钥的拷贝
    bool                           m_hasPubKey;        //是否有公钥
    ShareHostStreamerListener*     m_listener;         //事件回调（不拥有）
    ShareRelayClient               m_relayClient;      //房间中转连接
    ShareFrameReassembler          m_reassembler;      //接收线程上的分片重组（控制帧，以及第 22 期起其他共享者的屏幕画面）
    ShareScreenDecoders            m_screens;          //其他共享者屏幕画面的解码（第 22 期，只在接收线程上解码）
    ShareFrameReassembler          m_p2pReassembler;   //P2P 帧通道转交线程上的分片重组（观看者经 P2P 发来的包体）
    mutable std::mutex             m_p2pMutex;         //保护 m_p2pViewer
    std::shared_ptr<P2PViewerLink> m_p2pViewer;        //登记的 P2P 观看者；没有时为空
    ShareP2PVideoGate              m_p2pGate;          //发给 P2P 观看者的视频的丢帧规则（只在工作线程上使用）
    ShareVideoEncoder              m_encoder;          //画面编码器（只在工作线程上使用）
    std::unique_ptr<std::thread>   m_thread;           //工作线程
    std::atomic<bool>              m_running;          //工作线程是否在运行
    std::atomic<bool>              m_stop;             //是否要求停止
    std::atomic<bool>              m_paused;           //是否暂停
    std::atomic<bool>              m_keyRequested;     //是否收到了关键帧请求
    std::atomic<int>               m_keyRequestCount;  //中转送来的画面关键帧请求次数（接收线程累加，工作线程取走交给码率自适应）
    ShareBitrateController         m_bitrate;          //码率自适应（只在工作线程上使用，每次开始推流时复位）
    std::atomic<bool>              m_relayLost;        //推流途中中转是否已断开
    mutable std::mutex             m_mutex;            //保护以下三项
    bool                           m_switchPending;    //是否有待生效的切换
    ShareSourceSpec                m_pendingSource;    //待生效的共享内容
    RECT                           m_sourceRect;       //当前被采集内容的矩形
    std::unique_ptr<IShareCapturer> m_injected;        //调用方注入的采集器（Start 时转入，工作线程取走）
    ShareSourceSpec                m_source;           //当前的共享内容（只在工作线程上读写）
    std::vector<uint8_t>           m_lastFrame;        //最近一帧画面的拷贝（BGRA，行跨度 = 宽 × 4）
    int                            m_lastW;            //最近一帧的宽
    int                            m_lastH;            //最近一帧的高
    int                            m_sourceState;      //当前的共享内容状态（只在工作线程上读写）
    std::atomic<uint64_t>          m_sentFrames;       //已发出的视频帧数
    std::atomic<uint64_t>          m_sentKeyFrames;    //已发出的关键帧数
    std::atomic<ShareAudioSession*> m_audio;           //房间语音会话（不拥有）；接收线程把声音帧交给它
    std::atomic<ShareCameraSender*> m_cameraSender;    //本人摄像头的发送端（不拥有）；摄像头轨的关键帧请求交给它
    std::atomic<ShareCameraReceiver*> m_cameraReceiver;   //其他成员摄像头的接收端（不拥有）
    std::atomic<ShareAnnotationSink*> m_annotSink;     //标注帧的接收方（不拥有）
    std::atomic<ShareCaptionSink*>    m_captionSink;   //字幕帧的接收方（不拥有，语音字幕）
    std::atomic<ShareInputSink*>   m_inputSink;        //键鼠输入帧的接收方（第 18 期，不拥有）
    std::atomic<bool>              m_recordTap;        //录制旁路是否打开
    std::atomic<bool>              m_recordSeed;       //刚打开录制旁路：工作线程下一轮把最近一帧补给录制（画面不变时采集器不出新帧）
    std::mutex                     m_recordMutex;      //保护以下四项
    std::vector<uint8_t>           m_recordFrame;      //录制旁路的最新一帧（BGRA，行跨度 = 宽 × 4）
    int                            m_recordW;          //该帧的宽
    int                            m_recordH;          //该帧的高
    uint64_t                       m_recordSerial;     //帧序号
    ShareMouseFxStage              m_mouseFx;          //鼠标效果（第 16 期）：设置任意线程可换，其余只在工作线程上使用
    ShareCursorSampler             m_cursorSampler;    //取光标位置与逻辑按键（第 16 期，工作线程；Debug 版可改读模拟光标文件）
};

} // namespace screenshare
