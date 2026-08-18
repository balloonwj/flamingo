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
 *  OnRelayData→不再入视频解码队列）、再 join 连接线程（其中含 engine.Start 完成）、再停并 join
 *  视频解码线程（之后解码器不再被访问）、最后 m_audioEngine.Stop() / m_videoEngine.Stop()
 *  （停采集 / 播放、释放 codec）。
 *
 *  balloonwj@qq.com   2026-06-11（2026-06-12 接入真实音频；2026-06-16 接入视频多路复用）
 */
#pragma once

#include "CallRelayClient.h"        //先引入：其内含 winsock2.h，须在 windows.h 之前
#include "CallAudioEngine.h"        //音频引擎（采集→编码→sink；收包→解码→抖动→播放）
#include "CallVideoEngine.h"        //视频引擎（采集→H264编码→sink；收包→解码→显示）；仅含 stdint，无 windows.h
#include "CallMediaFraming.h"       //音视频多路复用 / 分片 / 接收端分流（纯逻辑）
#include "CallVideoDisplaySink.h"   //远端 / 本端视频帧显示去向接口（独立小头，UI 侧也用）
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
};

//CallVideoDisplaySink（远端 / 本端视频帧显示去向）已抽到独立头 CallVideoDisplaySink.h，
//通话窗 UI 直接 include 该小头实现它，无需 include 本（含 winsock2 的）重头文件。

//音视频通话的中转会话：管理一条到 imgserver 中转通道的连接，并把真实音视频多路复用到其上收发。
class CallMediaSession : public CallRelayClientHandler,
                         public CallAudioEngineSink,
                         public CallVideoEngineSink
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
     *  @param role        本端角色（net::call_role_caller / call_role_callee），仅用于日志标注
     *  @param sessionId   会话 id，仅用于日志标注
     *  @param hwndNotify  接收事件消息的窗口（一般为主面板）；须在本会话存活期间保持有效（不拥有）
     *  @param notifyMsg   向 hwndNotify 投递事件用的窗口消息 id（约定为 FMG_MSG_CALL_RELAY_EVENT）
     *  @param enableVideo true=音视频通话（起摄像头采集 + H.264 编解码 + 视频解码线程）；
     *                     false=纯语音通话（只起音频引擎）。由调用方据本次通话媒体类型传入。
     */
    void Start(const std::string& ip, int port, const std::string& token,
               int role, const std::string& sessionId, HWND hwndNotify, UINT notifyMsg,
               bool enableVideo);

    /**
     *  设置远端视频帧的显示去向（通话窗注入）。可在 Start 前后调用；传 NULL 解绑。
     *  解码出的远端帧会在视频解码线程上回调 sink->OnRemoteVideoFrame。
     *  @param sink 显示 sink（不拥有；须在本会话使用期间保持有效，至少到 Stop 返回 / 解绑）
     */
    void SetVideoDisplaySink(CallVideoDisplaySink* sink);

    //停止音视频引擎并断开中转，等待内部线程结束后返回。幂等；Stop 后不再投递事件 / 显示帧。
    void Stop();

    /**
     *  通话中启停视频链路（语音 <-> 视频切换用）。**只在 UI 线程调用**，与 Start / Stop
     *  的线程约定一致。
     *
     *  与 Stop() 的关键区别：本方法<u>不动</u>中转连接 m_relay 与音频引擎 m_audioEngine，
     *  只增删视频那一段——这正是切换过程中音频不中断的保证。
     *
     *  - on=true：起视频解码线程（先起，确保对端早到的帧不被丢）再起视频引擎（摄像头
     *    采集 + H.264 编码）。摄像头拿不到时返回 false，但通话继续（音频照常、也仍能
     *    收看对端画面），调用方据此把本端摄像头呈现为关闭态。
     *  - on=false：按与 Stop() 相同的次序先停并 join 视频解码线程（之后解码器不再被访问）、
     *    清空残留待解码包、再停视频引擎。
     *
     *  幂等：重复调用同一状态直接返回 true，不做任何事。
     *
     *  @param on 是否启用本端视频链路
     *  @return 目标状态已达成返回 true；on=true 但摄像头启动失败返回 false
     */
    bool EnableVideo(bool on);

    //是否处于「已 Start 且未 Stop」状态。
    bool IsRunning() const
    {
        return m_running.load();
    }

private:
    //==== CallRelayClientHandler：在中转接收线程触发 ====

    //收到对端中转帧：经 CallMediaDemux 分流——音频内联喂音频引擎解码；视频整帧入队交视频解码线程。
    virtual void OnRelayData(const unsigned char* data, size_t len) override;

    //中转异常断开（非本端主动 Stop）：投 call_relay_closed。
    virtual void OnRelayClosed() override;

    //==== CallAudioEngineSink：在采集线程触发 ====

    //音频引擎编码出一个音频包：打「音频 / whole」标记经中转发给对端。
    virtual void OnEncodedAudio(const unsigned char* data, size_t len) override;

    //==== CallVideoEngineSink：OnEncodedVideo 在采集线程触发，OnRemoteVideoFrame 在视频解码线程触发 ====

    //视频引擎编码出一帧：打「视频」标记并按需切片，逐片经中转发给对端。
    virtual void OnEncodedVideo(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override;

    //视频引擎采集到一帧本端画面（编码前）：转投显示 sink 作本地预览（若已注入）。
    virtual void OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride) override;

    //视频引擎解出一帧远端画面：转投显示 sink（若已注入）。
    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) override;

    //本端摄像头采集中途断掉：投 call_relay_camera_unavailable（带原因）让 UI 提示用户。
    virtual void OnLocalVideoUnavailable(int reason) override;

    //后台连接线程体：阻塞连中转、握手；成功投 connected 并起音视频引擎，失败投 failed。
    //ip / token 按值传入，确保线程内访问安全（与 Start 入参解耦）。
    void ConnectProc(std::string ip, int port, std::string token);

    //视频解码线程体：自带 CoInitializeEx + MFStartup；循环取队列里的远端视频包喂视频引擎解码。
    void VideoDecodeProc();

    //向 UI 线程投递一个中转会话事件（仅在仍运行时投，避免 Stop 后的滞后投递）。
    void PostEvent(int eventCode, unsigned int arg);

private:
    CallRelayClient              m_relay;          //中转连接（本类持有；其 handler 指向本类）
    CallAudioEngine              m_audioEngine;    //音频引擎（采集→编码→OnEncodedAudio；解码→播放）
    CallVideoEngine              m_videoEngine;    //视频引擎（采集→编码→OnEncodedVideo；解码→OnRemoteVideoFrame）；仅视频通话启用
    CallMediaDemux               m_demux;          //接收端解复用 / 视频重组（仅中转接收线程访问）
    CallVideoDisplaySink*        m_displaySink;    //远端帧显示去向（不拥有；UI 注入；可空）
    std::unique_ptr<std::thread> m_connectThread;  //后台连接线程，避免 Connect 的阻塞握手卡住 UI 线程

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
    bool                         m_enableVideo;     //本次通话是否含视频（Start 传入；决定是否起视频引擎 / 解码线程）
    std::atomic<bool>            m_running;          //是否已 Start 且未 Stop；同时作事件投递门控
};

} // namespace call
