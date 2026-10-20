/**
 *  屏幕共享观看者一侧的收流管线，ShareViewerReceiver.h
 *
 *  在房间中转连接（ShareRelayClient）的接收线程上完成：分片重组（ShareFrameReassembler）→ 屏幕画面的 H.264 解码
 *  （call::MfH264Decoder）→ NV12 转 BGRA 并按载荷里的真实宽高裁掉解码补齐的部分 → 存成「最新一帧」，通知观看窗取走绘制。
 *  解码只保留最新一帧：观看窗来不及画时中间帧直接被覆盖，不会积压。共享内容状态控制帧（最小化 / 暂停 / 窗口已关闭）
 *  经回调告知观看窗显示提示。声音、摄像头、标注在后续各期接入。设计见 docs/屏幕共享与录制方案.md 5.2「观看」。
 *
 *  回调在接收线程上触发，实现方（观看窗）只做 PostMessage，在 UI 线程上调用 GetLatestFrame 取画面。
 *
 *  P2P（docs/P2P通信方案.md 第 6 期）：单聊共享里服务端为本人与发起人建了 P2P 会话时，Start 带链路槽，改在后台线程上
 *  连接：先等链路（最多 kShareP2PLinkWaitMs），就绪就在通道 kShareP2PChannelId 上打开帧通道，房间包体（与 1860 的包体
 *  相同，帧头 + 载荷）一条一帧收发，处理方式与中转完全相同（P2P 帧通道的转交线程代替中转的接收线程）；链路失败、
 *  超时或通道打不开时照旧连房间中转。P2P 中途断开时自动改连房间中转（1726 的令牌只在 5 分钟内有效，过期则报
 *  share_viewer_relay_lost）。发起人在自己的通道打开时补发关键帧、标注，代替服务端在新成员报到时发的关键帧请求。
 *
 *  典型用法：
 *      m_receiver.Start(relayInfo, serverPubKey, this);
 *      ...（收到 OnViewerFrame 后在 UI 线程）
 *      if (m_receiver.GetLatestFrame(m_bgra, w, h, serial)) 画出来;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "ShareAudioTransport.h"
#include "ShareFraming.h"
#include "ShareProtocolClient.h"
#include "ShareRelayClient.h"
#include "ShareScreenDecoders.h"
#include "ShareScreenFeed.h"
#include "../Call/MfH264Codec.h"
#include "../../Source/Feature/P2P/P2PLinkBroker.h"   //P2P 链路槽与帧通道（带进 windows.h，排在 ShareRelayClient.h 的 winsock2.h 之后）

#include <memory>
#include <thread>

namespace screenshare {

//收流管线的事件
enum share_viewer_event
{
    share_viewer_relay_failed  = 1,   //连不上房间中转（报到被拒或网络不通）
    share_viewer_relay_lost    = 2,   //收流途中房间中转断开
    share_viewer_source_state  = 3,   //发起人的共享内容状态变了，value 取值见 screenshare_source_state
    share_viewer_undecodable   = 4,   //有一路画面判为超出本机系统的解码能力（value 为 1）或判定清除（value 为 0），2026-10-15
};

//单聊共享登记的 P2P 用途：与服务端 ChatSession 的 ChatSessionShareP2PLinker 建会话时的 purpose 一致
const char* const kShareP2PPurpose = "screenshare";

//单聊共享在 P2P 链路上使用的通道号：两端约定，发起人与一名观看者之间只用这一条
const uint16_t kShareP2PChannelId = 1;

//P2P 帧通道的单帧上限（字节）：一帧就是一个房间包体，视频分片的载荷最多 64 KiB，留足余量
const size_t kShareP2PMaxBodyBytes = 1024 * 1024;

//P2P 帧通道发送队列满时等可写通知的最长时间（毫秒）：观看者发的只有声音、标注与摄像头，量小
const int kShareP2PSendWaitMs = 5000;

//等 P2P 链路结果的最长时间（毫秒）：与文件夹传输、远程桌面、通话相同
const int kShareP2PLinkWaitMs = 20000;

//等链路期间检查是否已停止的间隔（毫秒）
const int kShareP2PWaitSliceMs = 100;

//单聊共享里一名观看者与发起人之间 P2P 会话的功能会话号的分隔符：房间号 + '#' + 座位号，与服务端一致
const char kShareP2PFeatureSeparator = '#';

/**
 *  一名观看者与发起人之间 P2P 会话的功能会话号。
 *  @param roomId 房间号
 *  @param seat   观看者的座位号
 *  @return 房间号 + '#' + 座位号
 */
std::string ShareP2PFeatureId(const std::string& roomId, int seat);

//观看端当前走的路径
enum share_viewer_path
{
    share_viewer_path_none  = 0,   //还没连上，或已停止
    share_viewer_path_p2p   = 1,   //P2P 链路上的帧通道
    share_viewer_path_relay = 2,   //房间中转
};

//收流管线的回调；在接收线程上触发
class ShareViewerSink
{
public:
    virtual ~ShareViewerSink()
    {
    }

    //解出了新的一帧画面，可在 UI 线程上调用 GetLatestFrame 取走
    virtual void OnViewerFrame() = 0;

    /**
     *  @param event 事件，取值见 enum share_viewer_event
     *  @param value 附加值：share_viewer_source_state 时为新状态，share_viewer_undecodable 时为 1 / 0，其余为 0
     */
    virtual void OnViewerEvent(int event, int value) = 0;
};

//屏幕共享观看者一侧的收流管线
class ShareAudioSession;
class ShareCameraSender;
class ShareCameraReceiver;
class ShareAnnotationSink;
class ShareCaptionSink;

//屏幕共享观看者一侧的收流管线；同时是本人麦克风帧的发送通道（ShareAudioTransport）
class ShareViewerReceiver : public ShareAudioTransport, public IShareScreenFeed, private ShareRelayClientHandler,
                            private ShareScreenDecodersListener, private balloonp2p::IP2PStreamHandler
{
public:
    ShareViewerReceiver();
    ~ShareViewerReceiver();

    ShareViewerReceiver(const ShareViewerReceiver& rhs) = delete;
    ShareViewerReceiver& operator=(const ShareViewerReceiver& rhs) = delete;

    /**
     *  连接房间中转并开始收流。没有链路槽时阻塞调用，最长约 5 秒（连接与报到）；有链路槽时在后台线程上连接（先等
     *  P2P 链路，不成再连中转），立即返回，连接失败经 share_viewer_relay_failed 告知。
     *  @param relay        1726 下发的中转参数
     *  @param serverPubKey 服务端 32 字节公钥；为 NULL 时不加密（内部复制，只在本次调用期间使用）
     *  @param sink         回调，不转移所有权，须在 Stop 返回之前一直有效
     *  @param p2pSlot      本人与发起人之间 P2P 会话的链路槽（P2P 方案第 6 期）；为空时只走中转。本对象持有一份引用，
     *                      Stop 时以「业务结束」关闭链路
     *  @return 没有链路槽时报到成功返回 true；有链路槽时已起连接线程即返回 true。失败时也会回调 share_viewer_relay_failed
     */
    bool Start(const ShareRelayInfo& relay, const unsigned char* serverPubKey, ShareViewerSink* sink,
               const std::shared_ptr<balloonp2p::P2PLinkSlot>& p2pSlot = std::shared_ptr<balloonp2p::P2PLinkSlot>());

    //当前走的路径，取值见 share_viewer_path（日志与测试用）
    int Path() const;

    /**
     *  链路实际路径（2026-10-04 起，供观看窗上的「P2P」徽章）。可在任意线程调用，不等待。
     *  @return 当前经 P2P 帧通道收画面时返回链路槽的路径（1 局域网直连、2 打洞直连、3 中转）；经房间中转或没连上时返回 0
     */
    int LinkPath() const;

    //断开并等待接收线程结束；可重复调用。不可在回调里调用
    void Stop();

    /**
     *  取最新一帧画面（拷贝）。可在任意线程调用。
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param width  [出参] 宽，单位像素
     *  @param height [出参] 高，单位像素
     *  @param serial [出参] 帧序号，每解出一帧加 1，调用方据此判断有没有新画面
     *  @return 已有至少一帧返回 true
     */
    bool GetLatestFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const;

    //已解出的帧数（日志与测试用）
    uint64_t DecodedFrames() const;

    //发起人当前的共享内容状态，取值见 screenshare_source_state
    int SourceState() const;

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

    //ShareAudioTransport：经本收流管线的中转连接发出一个包体；未连接时返回 false。可在任意线程调用
    virtual bool SendRoomBody(const std::string& body) override;

    //IShareScreenFeed（第 22 期）：按画面座位取最新一帧、列出有画面的座位、取某一路的共享内容状态
    virtual bool GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                uint64_t& serial) const override;
    virtual bool GetScreenSerial(int seat, uint64_t& serial) const override;
    virtual std::vector<int> ScreenSeats() const override;
    virtual int ScreenSourceState(int seat) const override;

private:
    virtual void OnRelayBody(const char* body, size_t len);
    virtual void OnRelayClosed();
    virtual void OnRelayThreadStart();
    virtual void OnRelayThreadExit();
    //ShareScreenDecodersListener（第 22 期）：某一路解出新帧，通知观看窗取画面
    virtual void OnScreenDecoded(int seat) override;
    //ShareScreenDecodersListener（2026-10-15）：某一路「超出本机解码能力」的判定变了，以 share_viewer_undecodable 事件告诉观看窗
    virtual void OnScreenUndecodable(int seat, bool undecodable) override;

    //以下为 IP2PStreamHandler 的实现，在 P2P 帧通道的转交线程上被调用（这条线程代替中转的接收线程）

    /**
     *  收到发起人的一个房间包体：与中转收到的同样处理。
     *  @param data 包体，只在本次调用期间有效
     *  @param len  长度（字节）
     */
    virtual void OnStreamFrame(const unsigned char* data, size_t len) override;

    //P2P 链路中途断开：起回退线程改连房间中转
    virtual void OnStreamClosed() override;

    //转交线程开始：初始化本线程的 COM 与 MF（解码在本线程上）
    virtual void OnStreamThreadStart() override;

    //转交线程退出：关闭解码器并释放 COM 与 MF
    virtual void OnStreamThreadExit() override;

    //连接线程：先等 P2P 链路、开帧通道，不成再连房间中转
    void ConnectProc();

    //回退线程：等 P2P 帧通道的转交线程结束后改连房间中转
    void FallbackProc();

    /**
     *  连房间中转（在连接线程或回退线程上调用）。
     *  @param failEvent 连不上时回调的事件（首次连接为 share_viewer_relay_failed，P2P 中途断开后为 share_viewer_relay_lost）
     *  @return 报到成功返回 true
     */
    bool ConnectRelay(int failEvent);

    //把各个工作线程收回（Stop 用）
    void JoinWorkers();

    ShareRelayClient          m_relayClient;    //房间中转连接
    ShareFrameReassembler     m_reassembler;    //分片重组（只在接收线程上使用）
    ShareScreenDecoders       m_screens;        //屏幕画面的解码（第 22 期起按发送者座位多路解码，各存最新一帧）
    ShareViewerSink*          m_sink;           //回调（不拥有）
    std::atomic<int>          m_sourceState;    //发起人的共享内容状态
    std::atomic<ShareAudioSession*> m_audio;    //房间语音会话（不拥有）；接收线程把声音帧交给它
    std::atomic<ShareCameraSender*> m_cameraSender;    //本人摄像头的发送端（不拥有）；摄像头轨的关键帧请求交给它
    std::atomic<ShareCameraReceiver*> m_cameraReceiver;   //其他成员摄像头的接收端（不拥有）
    std::atomic<ShareAnnotationSink*> m_annotSink;     //标注帧的接收方（不拥有）
    std::atomic<ShareCaptionSink*>    m_captionSink;   //字幕帧的接收方（不拥有，语音字幕）

    //==== P2P（第 6 期）====
    ShareRelayInfo                 m_relayInfo;        //本次的中转参数（Start 时拷贝，连接线程与回退线程使用）
    std::vector<unsigned char>     m_serverPubKey;     //服务端公钥的副本；为空表示不加密
    std::shared_ptr<balloonp2p::P2PLinkSlot>       m_p2pSlot;    //本人与发起人之间的链路槽；为空表示只走中转
    std::unique_ptr<balloonp2p::P2PStreamChannel>  m_channel;    //P2P 帧通道；只在 Start 里重建，Stop 只关闭不销毁
    std::atomic<int>               m_path;             //当前路径，取值见 share_viewer_path
    std::atomic<bool>              m_stopping;         //是否正在停止：连接线程等链路时据此放弃，回退线程据此不再连中转
    std::mutex                     m_workerMutex;      //保护下面两个线程对象的创建与回收
    std::thread                    m_connectThread;    //连接线程（有链路槽时）
    std::thread                    m_fallbackThread;   //回退线程（P2P 中途断开后改连中转）
};

} // namespace screenshare
