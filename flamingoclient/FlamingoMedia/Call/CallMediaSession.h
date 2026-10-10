/**
 *  音视频通话中转会话（每端一例），CallMediaSession.h
 *
 *  通话接通后（收到 cmd_call_signaling 下发的中转地址 / 令牌 / 角色），本端持有一个本类
 *  实例来管理与 imgserver 中转通道的整段交互，并把真实音视频接到中转上：在后台线程连上
 *  中转、用令牌握手配对后，起 CallAudioEngine（必起）与 CallVideoEngine（视频通话才起）。
 *  采集 / 编码出的音视频包经本类多路复用到同一条中转连接：音频包打「音频」标记、视频帧打
 *  「视频」标记并按需切片（见 CallMediaFraming），都经 cmd_av_relay_data 发出；对端来的帧
 *  经 OnRelayData 用 CallMediaDemux 分流——音频直接喂音频引擎解码播放，视频整帧重组后投到
 *  视频解码线程解码、再经显示 sink 交给通话窗绘制。通话结束时停引擎、断中转。
 *
 *  本类同时实现：
 *    - CallRelayClientHandler：接收中转数据帧 / 断开（在中转接收线程上触发）；
 *    - CallAudioEngineSink：接收音频引擎编码出的包并打标发到中转（在采集线程上触发）；
 *    - CallVideoEngineSink：接收视频引擎编码出的帧（采集线程）打标 / 切片发中转，以及解码出
 *      的远端帧（视频解码线程）转投显示 sink。
 *
 *  线程模型：约定 Start / Stop 只在 UI 线程调用。连接在后台线程完成（避免阻塞 UI）。握手成功
 *  后：① 中转接收线程回调 OnRelayData——分流后音频在本线程内联解码（Opus 快），视频包入队交
 *  给专门的「视频解码线程」（H.264 解码偏慢，避免阻塞接收线程拖累音频）；② 采集线程回调
 *  OnEncodedAudio / OnEncodedVideo（→ 打标发中转）；③ 视频解码线程回调 OnRemoteVideoFrame
 *  （→ 显示 sink）。视频解码线程自带 CoInitializeEx + MFStartup（H.264 解码器 MFT 在该线程上
 *  懒创建，须该线程已初始化 COM/MF）。
 *
 *  **Stop 顺序**（保证引擎释放时无并发访问）：先 m_relay.Close()（join 接收线程→不再有
 *  OnRelayData→不再入视频解码队列）、再 join 连接线程（其中含 engine.Start 完成）、再关 P2P 数据报
 *  通道（join 它的转交线程→不再有 OnDatagram）、再停并 join 视频解码线程（之后解码器不再被访问）、
 *  最后 m_audioEngine.Stop() / m_videoEngine.Stop()（停采集 / 播放、释放 codec）。
 *
 *  P2P（docs/P2P通信方案.md 第 6 期）：Start 带了链路槽（服务端在响铃阶段为这通电话建了 P2P 会话）时，连接线程先等
 *  链路结果（最多 kCallP2PLinkWaitMs，链路多半在响铃期间已经打通），就绪就在通道 kCallP2PChannelId 上打开数据报通道，
 *  音视频改走它（消息格式与丢包处理见 CallP2PFraming.h：音频按序号补偿丢包，视频断档后等关键帧并请对方补发）；
 *  链路失败或超时再连 imgserver 中转。数据报通道的消息在它自己的转交线程上经 OnDatagram 交来，处理方式与
 *  OnRelayData 相同（音频内联解码，视频整帧入队交视频解码线程）。
 *
 *  共享电脑声音（2026-10-06，docs/音视频通话方案.md 第 10 节）：SetSystemAudioEnabled 打开后，回环采集
 *  （screenshare::ShareSystemAudioCapture，只采 Flamingo 以外的程序）在它自己的线程上经 OnSystemAudio 交来电脑声音，
 *  CallSystemAudioSender 攒帧、闪避、编码后经 OnSystemAudioPacket 以单独的类型（中转 call_media_kind_system_audio、
 *  P2P call_p2p_kind_system_audio）发出；对方的电脑声音进音频引擎的第 kSystemAudioStreamId 路，与说话声混音播放。
 *  质量报告的标志位告诉对方「本端能接收电脑声音」与「本端正在共享电脑声音」。
 *
 *  balloonwj@qq.com   2026-06-11（2026-06-12 接入真实音频；2026-06-16 接入视频多路复用；2026-10-06 共享电脑声音）
 */
#pragma once

#include "CallRelayClient.h"        //先引入：其内含 winsock2.h，须在 windows.h 之前
#include "../../Source/Feature/P2P/P2PLinkBroker.h"   //P2P 链路槽与数据报通道（带进 windows.h，须排在 winsock2.h 之后）
#include "CallP2PFraming.h"         //P2P 数据报上的消息格式与丢包处理（纯逻辑）
#include "CallAudioEngine.h"        //音频引擎（采集→编码→sink；收包→解码→抖动→播放）
#include "CallVideoEngine.h"        //视频引擎（采集→H264编码→sink；收包→解码→显示）；仅含 stdint，无 windows.h
#include "CallMediaFraming.h"       //音视频多路复用 / 分片 / 接收端分流（纯逻辑）
#include "CallNetStats.h"           //接收端网络统计（丢包、抖动、断流；网络状况标识第 1 期）
#include "CallNetQuality.h"         //评级、防抖与快照结构 CallNetSnapshot（网络状况标识第 3 期）
#include "CallVideoDisplaySink.h"   //远端 / 本端视频帧显示去向接口（独立小头，UI 侧也用）
#include "CallSystemAudioSender.h"  //共享电脑声音的攒帧、闪避与编码（2026-10-06）
#include "../Share/ShareSystemAudioCapture.h"   //电脑声音的回环采集（与屏幕共享共用）
#include <windows.h>           //HWND / PostMessage
#include <string>
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>               //视频解码队列互斥
#include <condition_variable>  //视频解码线程等待队列
#include <deque>               //视频解码包队列
#include <vector>              //中转 body / 视频包缓冲

namespace call {

//中转会话事件码，随 FMG_MSG_CALL_RELAY_EVENT 的 wParam 投递给 UI 线程。UI 侧据此记日志 /
//更新通话状态。第二期真实音频下主要投 connected / failed / closed；test_ok 为第一期测试帧
//遗留枚举，真实音视频不再投递（保留以兼容 MainDlg 现有分支）。
enum CallRelayEvent
{
    call_relay_connected = 1,   //中转握手成功、音视频引擎已启动：本端通道就绪
    call_relay_test_ok   = 2,   //（第一期遗留）收到对端测试帧校验一致；真实音视频不再投递
    call_relay_failed    = 3,   //中转连接 / 握手失败：通道未能建立
    call_relay_closed    = 4,   //中转异常断开：通话期间通道中断（非本端主动 Stop）
    call_relay_camera_unavailable = 5,   //视频通话本端摄像头不可用（开不起来或采集中途断掉）：
                                         //随事件带 CameraFailReason 作参数，UI 据此提示用户；
                                         //通话本身继续（音频照常，仍能看到对方画面）
    call_relay_no_audio_devices = 6,     //Debug 版无设备模式（FLAMINGO_CALL_NO_AUDIO=1）：音频引擎没有打开麦克风与扬声器，
                                         //双方都听不到声音；UI 据此在画面顶部提示，避免被误认为通话故障（2026-10-05 起）
};

//统计线程每隔多久醒一次（毫秒）：检查断流状态；以后的质量报告也按这个间隔发送
const int kCallStatsTickMs = 1000;

//每隔多少次醒来记一条完整的通话统计日志：5 次即 5 秒
const int kCallStatsLogEveryTicks = 5;

//一对一通话在 P2P 链路上使用的数据报通道号：两端约定，一通电话只用这一条
const uint16_t kCallP2PChannelId = 1;

//等 P2P 链路结果的最长时间（毫秒）：与文件夹传输、远程桌面相同（打洞超时 + 地址探测 + DLL 回落中转，再留余量）；
//链路在响铃阶段就开始建，接听时多半已经就绪，不必等
const int kCallP2PLinkWaitMs = 20000;

//等链路期间检查是否已停止的间隔（毫秒）
const int kCallP2PWaitSliceMs = 100;

//数据报通道排队等待转交的消息数上限：音频每秒 50 个、视频每秒 30 帧，64 条约合 0.8 秒，再多说明处理跟不上，丢旧的
const size_t kCallDatagramQueueLimit = 64;


//本端当前走的路径
enum CallMediaPath
{
    call_media_path_none  = 0,   //还没连上，或已停止
    call_media_path_p2p   = 1,   //P2P 链路上的数据报通道
    call_media_path_relay = 2,   //imgserver 中转
};

//CallVideoDisplaySink（远端 / 本端视频帧显示去向）已抽到独立头 CallVideoDisplaySink.h，
//通话窗 UI 直接 include 该小头实现它，无需 include 本（含 winsock2 的）重头文件。

//音视频通话的中转会话：管理一条到 imgserver 中转通道的连接，并把真实音视频多路复用到其上收发。
class CallMediaSession : public CallRelayClientHandler,
                         public CallAudioEngineSink,
                         public CallVideoEngineSink,
                         public balloonp2p::IP2PDatagramHandler,
                         public screenshare::ShareSystemAudioHandler,
                         public CallSystemAudioPacketSink
{
public:
    CallMediaSession();
    virtual ~CallMediaSession();

    CallMediaSession(const CallMediaSession&) = delete;
    CallMediaSession& operator=(const CallMediaSession&) = delete;

    /**
     *  接通后启动中转会话：后台线程连 imgserver 中转、握手；成功后起音频引擎（必起），并按
     *  enableVideo 决定是否起视频引擎与视频解码线程，开始收发真实音视频。幂等——已在运行则先
     *  内部 Stop() 清掉上一会话。连接结果与后续事件经 hwndNotify 投递 FMG_MSG_CALL_RELAY_EVENT
     *  （wParam 见 enum CallRelayEvent）。
     *  @param ip          中转通道地址（cmd_call_signaling 下发，点分十进制 / 主机名）
     *  @param port        中转通道端口（cmd_call_signaling 下发）
     *  @param token       relay 令牌（cmd_call_signaling 下发，非空），作握手帧包体
     *  @param serverPubKey 服务端公钥（net::IDENTITY_PUBLICKEY_BYTES 字节），由调用方取自主连接或宿主；
     *                     为 NULL 表示服务端未配置公钥、中转全程明文。内部复制保存，返回后调用方即可释放
     *  @param role        本端角色（net::call_role_caller / call_role_callee），仅用于日志标注
     *  @param sessionId   会话 id，仅用于日志标注
     *  @param hwndNotify  接收事件消息的窗口（一般为主面板）；须在本会话存活期间保持有效（不拥有）
     *  @param notifyMsg   向 hwndNotify 投递事件用的窗口消息 id（约定为 FMG_MSG_CALL_RELAY_EVENT）
     *  @param enableVideo true=音视频通话（起摄像头采集 + H.264 编解码 + 视频解码线程）；
     *                     false=纯语音通话（只起音频引擎）。由调用方据本次通话媒体类型传入。
     *  @param p2pSlot     这通电话的 P2P 链路槽（P2P 方案第 6 期，控制器在收到带 p2psessionid 的邀请应答或来电推送时
     *                     认领）；为空时只走中转。本对象持有一份引用，Stop 时以「业务结束」关闭链路
     */
    void Start(const std::string& ip, int port, const std::string& token, const unsigned char* serverPubKey,
               int role, const std::string& sessionId, HWND hwndNotify, UINT notifyMsg,
               bool enableVideo,
               const std::shared_ptr<balloonp2p::P2PLinkSlot>& p2pSlot = std::shared_ptr<balloonp2p::P2PLinkSlot>());

    /**
     *  设置远端视频帧的显示去向（通话窗注入）。可在 Start 前后调用；传 NULL 解绑。
     *  解码出的远端帧会在视频解码线程上回调 sink->OnRemoteVideoFrame。
     *  @param sink 显示 sink（不拥有；须在本会话使用期间保持有效，至少到 Stop 返回 / 解绑）
     */
    void SetVideoDisplaySink(CallVideoDisplaySink* sink);

    //停止音视频引擎并断开中转，等待内部线程结束后返回。幂等；Stop 后不再投递事件 / 显示帧。
    void Stop();

    /**
     *  通话中启停视频的接收（语音 <-> 视频切换用）。**只在 UI 线程调用**，与 Start / Stop
     *  的线程约定一致。2026-10-05 起只管「收」，不碰本端摄像头：语音中途切到视频时双方都要
     *  看到对方画面，但只有发起方打开摄像头，摄像头另由 SetCameraEnabled 启停。
     *
     *  与 Stop() 的关键区别：本方法<u>不动</u>中转连接 m_relay 与音频引擎 m_audioEngine，
     *  只增删视频那一段——这正是切换过程中音频不中断的保证。
     *
     *  - on=true：起视频解码线程（先起，确保对端早到的帧不被丢），再让视频引擎开始接收。
     *  - on=false：按与 Stop() 相同的次序先停并 join 视频解码线程（之后解码器不再被访问）、
     *    清空残留待解码包，再关闭本端摄像头、停止接收。
     *
     *  幂等：重复调用同一状态直接返回，不做任何事。
     *
     *  @param on 是否接收对方画面
     *  @return 总是返回 true（目标状态已达成）
     */
    bool EnableVideo(bool on);

    /**
     *  打开或关闭本端摄像头（采集 + 编码 + 发送，含本地预览）。不影响接收对方画面。**只在 UI 线程或连接线程调用**，
     *  内部加锁，两者不会同时启停摄像头。用于通话窗的「开启 / 关闭摄像头」按钮与切到视频时发起方开摄像头。
     *  @param on true 打开，false 关闭
     *  @return on=true：摄像头打开成功（或本来就开着）返回 true；还没开始接收视频（EnableVideo / Start 带视频之前）或
     *          摄像头打不开返回 false，打不开时另向 UI 投递 call_relay_camera_unavailable 事件说明原因。
     *          on=false：总是返回 true
     */
    bool SetCameraEnabled(bool on);

    //本端摄像头是否正在采集（SetCameraEnabled 的结果；采集中途断掉时仍为 true，直到下一次关闭）
    bool IsCameraOn() const
    {
        return m_cameraOn.load();
    }

    /**
     *  打开或关闭本端麦克风（通话窗的麦克风按钮，2026-10-05 起）。关闭时真正停掉麦克风采集，对方听不到本端，系统的
     *  麦克风占用提示随之消失；不影响本端听对方。**只在 UI 线程调用**。
     *  可以在接通前后任意时刻调用：音频引擎还没启动时只记下想要的状态，连接线程启动引擎时据此决定是否打开麦克风，
     *  两者用 m_audioCtlMtx 串行化，不会出现「先点了关麦、引擎启动时又把麦克风打开」。每次 Start 复位为打开。
     *  @param on true 打开，false 关闭
     *  @return 达到目标状态返回 true；打开时麦克风设备不可用返回 false，此时麦克风保持关闭、IsMicEnabled 为 false
     */
    bool SetMicEnabled(bool on);

    //本端麦克风是否打开（用户想要的状态；引擎没启动时也有意义）
    bool IsMicEnabled() const
    {
        return m_micWanted.load();
    }

    /**
     *  设置本端播放音量（通话窗的音量滑块）。任意线程调用，转交音频引擎；跨通话保留，Start / Stop 不重置。
     *  @param percent 音量百分比，0~CallAudioEngine::kVolumeMax
     */
    void SetPlaybackVolume(int percent)
    {
        m_audioEngine.SetPlaybackVolume(percent);
    }

    //当前播放音量（%）
    int PlaybackVolume() const
    {
        return m_audioEngine.PlaybackVolume();
    }

    //通话网络状况的快照（任意线程调用，内部加锁）：往返时延、对方报告，以及本端档位与最近一个接收窗口（第 3 期）
    CallNetSnapshot GetNetSnapshot() const;

    //对方共享的电脑声音在音频引擎里的路号（说话声是 CallAudioEngine::kDefaultStreamId）
    static const int kSystemAudioStreamId = 1;

    /**
     *  打开或关闭共享电脑声音（2026-10-06，docs/音视频通话方案.md 第 10 节）。**只在 UI 线程调用**。
     *  打开：先检查通话已接通、对方能接收（CallSystemAudioPrecheck），再启动编码与回环采集（采集最长约 3 秒才打开），
     *  此后电脑声音单独作为一路发给对方；回环采集排除了本进程树时，电脑声音同时加进本端回声消除的参考信号。
     *  关闭：停掉回环采集与编码。Stop 时自动关闭；每通电话从关闭开始。
     *  @param on true 打开，false 关闭
     *  @return 取值见 CallSystemAudioResult；关闭恒为 call_sysaudio_ok，已经打开时再打开也返回 call_sysaudio_ok
     */
    int SetSystemAudioEnabled(bool on);

    //本端是否正在共享电脑声音
    bool IsSystemAudioEnabled() const
    {
        return m_sysOn.load();
    }

    //对方是否正在共享电脑声音（取最近一份对方质量报告的标志位；任意线程调用，内部加锁）
    bool IsPeerSharingSystemAudio() const;

    /**
     *  无设备模式下喂入一段电脑声音，相当于回环采集的回调；开关打开时才处理。仅供测试使用。
     *  @param mono    48 kHz 单声道 16 位采样（借用，调用期间有效）
     *  @param samples 采样数
     */
    void FeedTestSystemAudio(const int16_t* mono, int samples);

    /**
     *  无设备模式（FLAMINGO_CALL_NO_AUDIO=1）下喂入一段采集，相当于麦克风回调。仅供实网校准用例使用
     *  （2026-10-06，网络状况标识第 3 期 3d：没有采集就没有音频包，丢包、抖动、卡顿都测不到）。
     *  @param mono   单声道 16 位采样，内部采样率 48 kHz（借用，调用期间有效）
     *  @param frames 采样个数
     */
    void FeedTestCapture(const int16_t* mono, int frames)
    {
        m_audioEngine.FeedCapture(mono, frames);
    }

    /**
     *  无设备模式下取出一段播放，相当于扬声器回调；播放端的卡顿计数随之累计。仅供实网校准用例使用。
     *  @param mono   [出参] 单声道 16 位采样，内部采样率 48 kHz，至少 frames 个
     *  @param frames 采样个数
     */
    void PullTestRender(int16_t* mono, int frames)
    {
        m_audioEngine.PullRender(mono, frames);
    }

    //是否处于「已 Start 且未 Stop」状态。
    bool IsRunning() const
    {
        return m_running.load();
    }

    //当前走的路径，取值见 CallMediaPath（日志与测试用）
    int Path() const
    {
        return m_path.load();
    }

    /**
     *  链路实际路径（2026-10-04 起，供通话窗底栏的「P2P」徽章）。在 UI 线程调用（与 Start / Stop 同一线程，m_p2pSlot 不会被同时改）。
     *  @return 走 P2P 数据报通道时返回链路槽的路径（1 局域网直连、2 打洞直连、3 中转）；走 imgserver 中转或没连上时返回 0
     */
    int LinkPath() const
    {
        return balloonp2p::LinkPathWhenUsing(m_path.load() == call_media_path_p2p, m_p2pSlot);
    }

    //==== IP2PDatagramHandler：在 P2P 数据报通道的转交线程上触发 ====

    /**
     *  收到对端的一条数据报消息：音频按序号补偿丢包后解码；视频按丢帧规则解码或丢弃，必要时请对方补关键帧；
     *  关键帧请求让本端编码器出一个关键帧（限频）。
     *  @param data 消息，只在本次回调内有效
     *  @param len  长度（字节）
     */
    virtual void OnDatagram(const unsigned char* data, size_t len) override;

    //P2P 链路断开：与中转断开同样处理，投 call_relay_closed
    virtual void OnDatagramClosed() override;

private:
    //==== CallRelayClientHandler：在中转接收线程触发 ====

    //收到对端中转帧：经 CallMediaDemux 分流——音频内联喂音频引擎解码；视频整帧入队交视频解码线程。
    virtual void OnRelayData(const unsigned char* data, size_t len) override;

    //中转异常断开（非本端主动 Stop）：投 call_relay_closed。
    virtual void OnRelayClosed() override;

    //==== CallAudioEngineSink：在采集线程触发 ====

    //音频引擎编码出一个音频包：打「音频 / whole」标记经中转发给对端。
    virtual void OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex) override;

    //==== CallVideoEngineSink：OnEncodedVideo 在采集线程触发，OnRemoteVideoFrame 在视频解码线程触发 ====

    //视频引擎编码出一帧：打「视频」标记并按需切片，逐片经中转发给对端。
    virtual void OnEncodedVideo(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override;

    //视频引擎采集到一帧本端画面（编码前）：转投显示 sink 作本地预览（若已注入）。
    virtual void OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride) override;

    //视频引擎解出一帧远端画面：转投显示 sink（若已注入）。
    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) override;

    //本端摄像头采集中途断掉：投 call_relay_camera_unavailable（带原因）让 UI 提示用户。
    virtual void OnLocalVideoUnavailable(int reason) override;

    //后台连接线程体：有链路槽时先等 P2P 链路、开数据报通道，不成再阻塞连中转、握手；成功投 connected 并起
    //音视频引擎，失败投 failed。ip / token 按值传入，确保线程内访问安全（与 Start 入参解耦）。
    void ConnectProc(std::string ip, int port, std::string token);

    /**
     *  等 P2P 链路并打开数据报通道（连接线程上调用）。
     *  @param stopped [出参] 等待期间会话被 Stop 时置为 true
     *  @return 打开成功返回 true；链路失败、超时、被停止或通道打不开返回 false
     */
    bool ConnectP2P(bool& stopped);

    //经 P2P 数据报通道发一条消息；发送失败时只计数并按间隔记日志
    void SendP2P(int kind, uint8_t flags, uint32_t seq, uint64_t tsMs, const uint8_t* data, size_t len);

    //把一帧完整的远端视频连同时间戳交给视频解码线程（中转与 P2P 两条路径共用）
    void EnqueueRemoteVideo(std::vector<uint8_t>& frame, uint64_t tsMs);

    //视频解码线程体：自带 CoInitializeEx + MFStartup；循环取队列里的远端视频包喂视频引擎解码。
    void VideoDecodeProc();

    //向 UI 线程投递一个中转会话事件（仅在仍运行时投，避免 Stop 后的滞后投递）。
    void PostEvent(int eventCode, unsigned int arg);

    //收包线程：把一个音频包交给接收统计（加锁）；参数同 CallRecvStats::OnAudioPacket
    void NoteRecvAudio(bool hasSeq, uint32_t seq, uint64_t tsMs, int64_t nowMs);

    //收包线程：收到对方的其它消息（视频、关键帧请求），只刷新断流计时（加锁）
    void NoteRecvOther(int64_t nowMs);

    //统计线程体：每 kCallStatsTickMs 醒一次，断流状态变化时记一条日志，每 kCallStatsLogEveryTicks 次记一条完整统计
    void StatsProc();

    //记一条完整的通话统计日志（统计线程上调用）：用这 5 秒累加的接收统计，与上一次比较各项累计计数，并带上往返时延与对方报告
    void LogStats(bool peerSilent);

    /**
     *  统计线程每秒一次（第 2 期）：取一秒的接收窗口累加给日志，评出本端档位（第 3 期），填一份质量报告经当前路径发出。
     *  @param peerSilent 对方此刻是否已断流（StatsProc 刚判断过），断流时本端档位直接评为断流
     */
    void TickReport(bool peerSilent);

    /**
     *  收包线程：收到对方的一份质量报告（两条路径共用）。解析后交给 m_reportX，算往返时延。
     *  @param data     报告负载
     *  @param len      长度（字节）
     *  @param peerTsMs 对方发出它的时刻（对方会话毫秒，取自封装头）
     */
    void OnPeerReportBytes(const uint8_t* data, size_t len, uint64_t peerTsMs);

    //停掉并等待统计线程结束（Stop 与 Start 里调用，幂等）
    void StopStatsThread();

    //==== 共享电脑声音（2026-10-06）====

    //ShareSystemAudioHandler：回环采集线程交来一段电脑声音（48 kHz 单声道）→ 回声参考 + 闪避 + 编码发送
    virtual void OnSystemAudio(const int16_t* mono, int samples) override;

    //CallSystemAudioPacketSink：编好的一个电脑声音包，按当前路径发往中转或 P2P（回环采集线程上回调）
    virtual void OnSystemAudioPacket(const uint8_t* data, size_t len, uint64_t tsMs) override;

    //停掉共享电脑声音：回环采集、编码、回声参考（SetSystemAudioEnabled(false) 与 Stop 里调用，幂等，UI 线程）
    void stopSystemAudio();

private:
    CallRelayClient              m_relay;          //中转连接（本类持有；其 handler 指向本类）
    CallAudioEngine              m_audioEngine;    //音频引擎（采集→编码→OnEncodedAudio；解码→播放）
    CallVideoEngine              m_videoEngine;    //视频引擎（采集→编码→OnEncodedVideo；解码→OnRemoteVideoFrame）；仅视频通话启用
    CallMediaDemux               m_demux;          //接收端解复用 / 视频重组（仅中转接收线程访问）
    CallVideoDisplaySink*        m_displaySink;    //远端帧显示去向（不拥有；UI 注入；可空）
    std::unique_ptr<std::thread> m_connectThread;  //后台连接线程，避免 Connect 的阻塞握手卡住 UI 线程
    std::vector<unsigned char>   m_serverPubKey;   //服务端公钥副本（Start 时复制，连接线程交给中转连接握手用）；为空表示全程明文

    //一条待解码的远端视频帧：完整 H.264 帧字节 + 其会话相对时间戳（毫秒，A/V 同步用）。
    struct VideoInPacket
    {
        std::vector<uint8_t> m_data;   //完整 H.264 帧
        uint64_t             m_tsMs;   //会话相对时间戳（毫秒）

        VideoInPacket()
            : m_tsMs(0)
        {
        }
    };

    //视频解码线程及其输入队列：接收线程分流出的完整视频帧入队，解码线程取出喂引擎解码。
    //把 H.264 解码从接收线程剥离，避免解码耗时阻塞接收线程而拖累音频实时性。
    std::unique_ptr<std::thread>   m_videoDecodeThread; //视频解码线程（enableVideo 才创建）
    std::deque<VideoInPacket>      m_videoInQueue;       //待解码远端视频帧队列（m_videoQueueMtx 保护）
    std::mutex                     m_videoQueueMtx;      //保护 m_videoInQueue
    std::condition_variable        m_videoQueueCv;       //队列非空 / 停止 时唤醒解码线程
    std::atomic<bool>              m_videoDecodeStop;    //通知视频解码线程退出
    unsigned long long             m_clockStartMs;       //会话时钟起点（GetTickCount64 值）；发包时间戳 = 当前 - 此值

    HWND                         m_hwndNotify;      //事件投递目标窗口（不拥有；生命周期由调用方保证）
    UINT                         m_notifyMsg;       //向 m_hwndNotify 投递事件用的窗口消息 id（Start 传入）
    int                          m_role;            //本端角色，仅日志用，取值见 net::call_role
    std::string                  m_sessionId;       //本次会话 id，仅日志用
    bool                         m_enableVideo;     //是否在接收对方画面（Start 传入、EnableVideo 改写；决定是否起解码线程）
    std::atomic<bool>            m_cameraOn;        //本端摄像头是否在采集（SetCameraEnabled 改写；连接线程与 UI 线程都会读写）
    std::mutex                   m_cameraMtx;       //保护本端摄像头的启停：视频通话接通时在连接线程上打开，按钮在 UI 线程上开关
    std::atomic<bool>            m_running;          //是否已 Start 且未 Stop；同时作事件投递门控
    std::atomic<bool>            m_micWanted;        //用户想要的麦克风状态（SetMicEnabled 写；Start 复位为 true；连接线程启动引擎时读）
    std::mutex                   m_audioCtlMtx;      //串行化「连接线程启动音频引擎」与「UI 线程开关麦克风」

    //==== P2P（第 6 期）====
    std::shared_ptr<balloonp2p::P2PLinkSlot>        m_p2pSlot;     //这通电话的链路槽；为空表示只走中转。Start 设置，Stop 时关闭链路
    std::unique_ptr<balloonp2p::P2PDatagramChannel> m_datagram;    //数据报通道；只在 Start 里重建，Stop 只关闭不销毁，
                                                                   //避免采集线程正在发送时对象被销毁
    std::atomic<int>             m_path;            //当前路径，取值见 CallMediaPath；连接成功后由连接线程设置
    uint32_t                     m_audioSeq;        //下一个音频包的序号（仅音频采集线程访问）
    uint32_t                     m_videoSeq;        //下一帧视频的序号（仅视频采集线程访问）
    CallVideoLossGuard           m_videoGuard;      //对端视频帧的丢帧处理（仅数据报转交线程访问）
    CallKeyframeForceLimiter     m_keyLimiter;      //对端关键帧请求的限频（仅数据报转交线程访问）
    std::atomic<bool>            m_videoGuardReset; //本端重新开始收视频时置位，转交线程据此重置 m_videoGuard
    std::atomic<uint64_t>        m_p2pSendFailures; //数据报发送失败的次数（超长或链路已断），用于日志

    //==== 通话统计（网络状况标识第 1 期，2026-10-05）====
    CallRecvStats                m_recvStats;       //对方 → 本端的接收统计（收包线程写、统计线程取窗口；m_recvStatsMtx 保护）
    CallReportExchange           m_reportX;         //双方互发质量报告的状态（第 2 期；收包线程收、统计线程发；m_recvStatsMtx 保护）
    mutable std::mutex           m_recvStatsMtx;    //保护 m_recvStats 与 m_reportX（GetNetSnapshot 是 const 方法，故 mutable）
    std::atomic<uint64_t>        m_videoSent;       //本次会话发出的视频帧数（采集线程写，统计线程读）
    std::atomic<uint64_t>        m_videoRecv;       //本次会话收到的完整视频帧数（收包线程写，统计线程读）
    std::unique_ptr<std::thread> m_statsThread;     //统计线程；Start 时创建，Stop 时最先停掉
    std::mutex                   m_statsMtx;        //配合 m_statsCv 等待；保护 m_statsStop
    std::condition_variable      m_statsCv;         //Stop 时唤醒统计线程
    bool                         m_statsStop;       //统计线程是否该退出（m_statsMtx 保护）
    //以下只在统计线程上使用：上一条统计日志时的各项累计值，用来算这 5 秒的增量
    uint64_t                     m_lastAudioSent;       //上次的已送出音频包数
    uint64_t                     m_lastVideoSent;       //上次的已发出视频帧数
    uint64_t                     m_lastVideoRecv;       //上次的已收到视频帧数
    uint64_t                     m_lastPlayRequested;   //上次的播放请求采样数
    uint64_t                     m_lastPlayConcealed;   //上次的补静音采样数
    //以下同样只在统计线程上使用（第 2 期）
    uint32_t                     m_reportSeq;           //下一份 P2P 质量报告的序号
    uint64_t                     m_tickPlayRequested;   //上一秒的播放请求采样数（算每秒报告里的补静音比例）
    uint64_t                     m_tickPlayConcealed;   //上一秒的补静音采样数
    uint32_t                     m_logRecv;             //这 5 秒累计收到的音频包数（每秒的窗口累加）
    uint32_t                     m_logLost;             //这 5 秒累计判定丢掉的音频包数
    uint32_t                     m_logLate;             //这 5 秒累计迟到或重复的音频包数
    CallGradeSmoother            m_gradeSmoother;       //本端「我收对方」档位的防抖（第 3 期）
    //以下由统计线程每秒写、GetNetSnapshot 读，m_recvStatsMtx 保护（第 3 期）
    int                          m_myGrade;             //本端「我收对方」防抖后的档位；还没结算过窗口时为 kCallGradeUnknown
    bool                         m_hasMyWindow;         //是否已结算过至少一个接收窗口
    uint32_t                     m_myLossPermille;      //最近一个接收窗口的丢包率（千分比）
    uint32_t                     m_myJitterMs;          //最近一个接收窗口结束时的抖动（毫秒）
    uint32_t                     m_myStallPermille;     //最近一个接收窗口的卡顿率（千分比）；没有应收的音频包时为 0
    uint32_t                     m_myBufferMs;          //最近一个接收窗口正常播放时的平均缓冲延迟（毫秒，2026-10-06 B4）
    //以下只在统计线程上使用（2026-10-06 B4）：上一秒、上一条统计日志时取到的播放统计累计值，用来算差值
    AudioStreamStats             m_tickStreamStats;     //上一秒的累计值（算这一秒的平均缓冲延迟）
    AudioStreamStats             m_logStreamStats;      //上一条统计日志时的累计值（算这 5 秒的各项增量）

    //==== 共享电脑声音（2026-10-06，docs/音视频通话方案.md 第 10 节）====
    screenshare::ShareSystemAudioCapture m_sysCapture;  //电脑声音的回环采集（无设备模式下不用）；UI 线程启停
    CallSystemAudioSender        m_sysSender;           //电脑声音的攒帧、闪避与编码（回环采集线程上调用，内部带锁）
    std::atomic<bool>            m_sysOn;               //本端是否正在共享电脑声音（UI 线程写；回环采集、统计线程读）
    std::atomic<bool>            m_sysExcludingSelf;    //回环采集是否排除了本进程树（false 时对方说话就闪避，且不进回声参考）
    uint32_t                     m_sysSeq;              //下一个电脑声音 P2P 消息的序号（只在回环采集线程上使用，打开前清零）
    std::atomic<uint64_t>        m_sysSent;             //本次会话发出的电脑声音包数（回环采集线程写，统计线程读）
    std::atomic<uint64_t>        m_sysRecv;             //本次会话收到的电脑声音包数（收包线程写，统计线程读）
    uint64_t                     m_lastSysSent;         //上一条统计日志时的 m_sysSent（只在统计线程上使用）
    uint64_t                     m_lastSysRecv;         //上一条统计日志时的 m_sysRecv（只在统计线程上使用）
    bool                         m_peerSysSharing;      //最近一份对方报告里「正在共享电脑声音」位（m_recvStatsMtx 保护）
};

} // namespace call
